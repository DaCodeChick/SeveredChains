/* emit.c — code generation for `fate compile`.
 *
 * Output model: op lines are rendered straight into a per-function buffer, and that
 * buffer gets its prologue prefixed when the function finishes. Nothing is
 * back-patched, because every `_expr_`/`_label_` name is final at allocation time.
 *
 * Three orderings are load-bearing and visible in the golden files' slot numbering:
 *   * an expression's result slot is allocated *before* its operands;
 *   * an assignment lowers its destination before its source;
 *   * an engine-call or function-call argument list is emitted raw, without the
 *     copy-to-slot step that operator operands get. That is why
 *     `call SMap::scriptScaleUniform, stor[inl[:updateParty__expr_16]],
 *     inl[:characterScales][...]` prints the operands in place.
 *
 * @note Thread: single-threaded; all storage belongs to the caller's arena. */

#include "emit.h"

#include "optable.h"

#include <stdio.h>
#include <string.h>

/* ------------------------------------------------------------------ values ------- */

/** Rendering classes, one per `FateValue` subclass with a `toString()`. */
typedef enum Vk
{
  VK_IMM,   /**< literal text */
  VK_VAR,   /**< `inl[:name]` */
  VK_LABEL, /**< `inl[:_label_N]` */
  VK_ARRAY, /**< `<base>[<index>]` */
  VK_GV1,   /**< `var[i]` */
  VK_GV2,   /**< `var[i][j]` */
  VK_STOR1, /**< `stor[i]` */
  VK_STOR2, /**< `stor[s, i]` */
  VK_REG,   /**< `reg[i]` */
  VK_LIST   /**< `[a, b]` */
} Vk;

/** A variable slot: the unit of the prologue and of `inl[:]` rendering. */
typedef struct VarDecl
{
  const char* display; /**< already prefixed, exactly as it prints */
  int length;
  const char* value; /**< text after `data ` in the prologue */
} VarDecl;

/** A value, i.e. one thing printable as an op parameter. */
typedef struct Val Val;

struct Val
{
  Vk kind;
  const char* text;
  VarDecl* var;
  struct Val* a;
  struct Val* b;
  Val** items;
  int n;
};

/** One name binding in the scope chain. */
typedef struct Binding
{
  const char* name;
  VarDecl* var;
} Binding;

/* ------------------------------------------------------------- scope chain ------ */

/** @brief Bind or reuse the slot named @p base.
 *
 *  Bindings are keyed by the *bare* name and carry the prefixed form only in
 *  `VarDecl::display`. Keying by the printed form would hide globals: `tbl` is bound
 *  once outside any function and referenced from inside one, where the active prefix is
 *  the function name.
 *
 *  A name already visible keeps its slot, matching `FateScope`'s `computeIfAbsent`.
 *
 *  @return The slot, or NULL when the binding table could not grow. */
static VarDecl* declareVar(Emit* e, const char* base, int length, const char* value)
{
  for (int i = 0; i < e->bindCount; i++)
    if (strcmp(e->binds[i].name, base) == 0)
      return e->binds[i].var;

  if (e->bindCount == e->bindCap)
  {
    const int newCap = e->bindCap ? e->bindCap * 2 : 128;
    Binding* grown = (Binding*)arena_alloc(e->arena, (size_t)newCap * sizeof(Binding));

    if (!grown)
      return NULL;

    if (e->bindCount > 0)
      memcpy(grown, e->binds, (size_t)e->bindCount * sizeof(Binding));

    e->binds = grown;
    e->bindCap = newCap;
  }

  if (e->bindCount == 0)
    e->bindCap = (e->bindCap < 128) ? 128 : e->bindCap;

  VarDecl* var = (VarDecl*)arena_alloc(e->arena, sizeof(VarDecl));

  if (!var)
    return NULL;

  char display[288];

  if (e->prefix[0] != '\0')
    snprintf(display, sizeof(display), "%s_%s", e->prefix, base);
  else
    snprintf(display, sizeof(display), "%s", base);

  var->display = arena_dup(e->arena, display, strlen(display));
  var->length = length;
  var->value = value;

  if (e->varCount < (int)(sizeof(e->vars) / sizeof(e->vars[0])))
    e->vars[e->varCount++] = var;

  e->binds[e->bindCount].name = arena_dup(e->arena, base, strlen(base));
  e->binds[e->bindCount].var = var;
  e->bindCount++;

  return var;
}

/** Find a binding by bare name, innermost scope first. */
static VarDecl* lookupVar(Emit* e, Str name)
{
  for (int i = e->bindCount - 1; i >= 0; i--)
    if ((int)strlen(e->binds[i].name) == name.len && memcmp(e->binds[i].name, name.p, (size_t)name.len) == 0)
      return e->binds[i].var;

  return NULL;
}

static bool hasVar(Emit* e, Str name)
{
  return lookupVar(e, name) != NULL;
}

void emit_bindGlobal(Emit* e, const char* name, int length, const char* value)
{
  const char* saved = e->prefix;
  char empty[1] = {0};

  e->prefix[0] = '\0';
  (void)saved;
  (void)empty;

  /* Declared outside any function, so it takes no prefix and joins no function's
   * prologue; the prologue for a `var` block is rendered from the TopItem instead. */
  (void)declareVar(e, name, length, value);

  e->globalMark = e->bindCount;
}

/* ------------------------------------------------------------- op rendering ------ */

static void renderVal(Buf* out, const Val* v);

/** One rendered op line, including its terminating newline. */
static void putOp(Emit* e, const char* name, Val* const* params, int n)
{
  buf_puts(&e->body, name);
  buf_putc(&e->body, ' ');

  for (int i = 0; i < n; i++)
  {
    if (i > 0)
      buf_puts(&e->body, ", ");

    renderVal(&e->body, params[i]);
  }

  buf_putc(&e->body, '\n');
}

static void putLabelLine(Emit* e, int label)
{
  buf_printf(&e->body, "_label_%d:\n", label);
}

/* ---------------------------------------------------------------- value helpers -- */

static Val* mkVal(Emit* e, Vk kind)
{
  Val* v = (Val*)arena_alloc(e->arena, sizeof(Val));

  if (!v)
  {
    e->aborted = true;
    return NULL;
  }

  v->kind = kind;
  return v;
}

static Val* mkImm(Emit* e, const char* text)
{
  Val* v = mkVal(e, VK_IMM);

  if (v)
    v->text = text;

  return v;
}

static Val* mkVar(Emit* e, VarDecl* var)
{
  Val* v = mkVal(e, VK_VAR);

  if (v)
    v->var = var;

  return v;
}

static Val* mkLabelRef(Emit* e, int label)
{
  char name[32];
  snprintf(name, sizeof(name), "_label_%d", label);

  Val* v = mkVal(e, VK_LABEL);

  if (v)
    v->text = arena_dup(e->arena, name, strlen(name));

  return v;
}

static Val* mkBinaryForm(Emit* e, Vk kind, Val* a, Val* b)
{
  Val* v = mkVal(e, kind);

  if (v)
  {
    v->a = a;
    v->b = b;
  }

  return v;
}

/** @brief The placeholder the reference substitutes for an unresolvable value. */
/** @brief Immediate built from a source slice.
 *
 *  `Str` is not NUL-terminated, and immediates render through `buf_puts`, so the text
 *  is copied to a terminated string here. Handing `v->text` a raw slice made the
 *  rendered operand run on to the next NUL in the source buffer. */
static Val* mkImmStr(Emit* e, Str text)
{
  Val* v = mkVal(e, VK_IMM);

  if (!v)
    return NULL;

  const int len = (text.len < 0) ? 0 : (text.len >= (int)sizeof(e->scratch)) ? (int)sizeof(e->scratch) - 1 : text.len;

  memcpy(e->scratch, text.p, (size_t)len);
  e->scratch[len] = '\0';
  v->text = arena_dup(e->arena, e->scratch, (size_t)len);

  return v;
}

static Val* mkUndefined(Emit* e)
{
  return mkImm(e, "undefined");
}

/** A variable reference is "pure": it prints without first being copied to a slot. */
static bool isPure(const Val* v)
{
  return v != NULL && v->kind == VK_VAR;
}

static void renderVal(Buf* out, const Val* v)
{
  if (!v)
    return;

  switch (v->kind)
  {
    /* Labels are referenced through `inl[:]` exactly like variables; only their text
     * differs. Keeping them on the unwrapped path printed `_label_0` instead of
     * `inl[:_label_0]`. */
    case VK_LABEL:
      buf_puts(out, "inl[:");
      buf_puts(out, v->text);
      buf_putc(out, ']');
      break;

    case VK_IMM:
      buf_puts(out, v->text);
      break;

    case VK_VAR:
      buf_puts(out, "inl[:");
      buf_puts(out, v->var->display);
      buf_putc(out, ']');
      break;

    case VK_ARRAY:
      renderVal(out, v->a);
      buf_putc(out, '[');
      renderVal(out, v->b);
      buf_putc(out, ']');
      break;

    case VK_GV1:
      buf_puts(out, "var[");
      renderVal(out, v->a);
      buf_putc(out, ']');
      break;

    case VK_GV2:
      buf_puts(out, "var[");
      renderVal(out, v->a);
      buf_puts(out, "][");
      renderVal(out, v->b);
      buf_putc(out, ']');
      break;

    case VK_STOR1:
      buf_puts(out, "stor[");
      renderVal(out, v->a);
      buf_putc(out, ']');
      break;

    case VK_STOR2:
      buf_puts(out, "stor[");
      renderVal(out, v->a);
      buf_puts(out, ", ");
      renderVal(out, v->b);
      buf_putc(out, ']');
      break;

    case VK_REG:
      buf_puts(out, "reg[");
      renderVal(out, v->a);
      buf_putc(out, ']');
      break;

    case VK_LIST:
      buf_putc(out, '[');

      for (int i = 0; i < v->n; i++)
      {
        if (i > 0)
          buf_puts(out, ", ");

        renderVal(out, v->items[i]);
      }

      buf_putc(out, ']');
      break;
  }
}

/** @brief Allocate a fresh `_expr_N` slot.
 *
 *  Called for every expression result, and for every index operand that is not
 *  already a variable reference. The counter is per translation unit, not per
 *  function, which is why `addChar__expr_20` continues where `updateParty` stopped.
 */
static Val* allocSlot(Emit* e)
{
  char name[32];
  snprintf(name, sizeof(name), "_expr_%d", e->exprIndex++);

  return mkVar(e, declareVar(e, name, 1, "0"));
}

/** @brief Allocate a fresh `_expr_N` slot carrying string data.
 *
 *  A string literal passed to an engine call becomes a slot whose prologue line is
 *  `data str[<body>]`. The AST node already dropped the surrounding quotes, and the
 *  body is copied verbatim, so `\n`, an escaped quote and an escaped backslash each
 *  keep the spelling they have in the source.
 *
 *  @param e    Emission state (owns the arena and the slot counter).
 *  @param body String text without its quotes; not NUL-terminated.
 *  @return The slot value, or NULL if the arena is exhausted (sets `aborted`). */
static Val* allocStrSlot(Emit* e, Str body)
{
  char name[32];
  snprintf(name, sizeof(name), "_expr_%d", e->exprIndex++);

  const int len = (body.len < 0) ? 0 : body.len;
  char* data = (char*)arena_alloc(e->arena, (size_t)len + 8);

  if (!data)
  {
    e->aborted = true;
    return NULL;
  }

  /* Four bytes only: copying the literal together with its terminating NUL put a
   * zero byte in front of the body and the prologue printed `data str[`. */
  memcpy(data, "str[", 4);

  if (len > 0)
    memcpy(data + 4, body.p, (size_t)len);

  data[4 + len] = ']';
  data[5 + len] = '\0';

  VarDecl* var = declareVar(e, name, 1, data);

  if (!var)
  {
    e->aborted = true;
    return NULL;
  }

  return mkVar(e, var);
}

/** @brief Give @p v as an operand, copying it to a slot unless it already is one
 *         (`valueToVariable` in the reference). */
static Val* toSlot(Emit* e, Val* v)
{
  if (isPure(v))
    return v;

  Val* slot = allocSlot(e);

  if (!slot)
    return NULL;

  Val* const params[2] = {v, slot};
  putOp(e, "mov", params, 2);

  return slot;
}

/* ---------------------------------------------------------- function table ------ */

FuncDef* funcTableAdd(FuncTable* t, Arena* arena, Str name, Str params[], int paramCount, int returns, int unitIndex,
                      int line)
{
  FuncDef* slot = NULL;

  /* Last registration for a name wins, matching the reference's `Map.put` after each
   * preprocessed file. */
  for (int i = 0; i < t->count; i++)
  {
    if (t->items[i].name.len == name.len && memcmp(t->items[i].name.p, name.p, (size_t)name.len) == 0)
    {
      slot = &t->items[i];
      break;
    }
  }

  if (!slot)
  {
    if (t->count == t->cap)
    {
      const int newCap = t->cap ? t->cap * 2 : 32;
      FuncDef* grown = (FuncDef*)arena_alloc(arena, (size_t)newCap * sizeof(FuncDef));

      if (!grown)
        return NULL;

      if (t->count > 0)
        memcpy(grown, t->items, (size_t)t->count * sizeof(FuncDef));

      t->items = grown;
      t->cap = newCap;
    }

    slot = &t->items[t->count++];
  }

  slot->name = name;
  slot->paramCount = paramCount < 16 ? paramCount : 16;
  slot->returns = returns;
  slot->unitIndex = unitIndex;
  slot->line = line;

  for (int i = 0; i < slot->paramCount; i++)
    slot->params[i] = params[i];

  return slot;
}

const FuncDef* funcTableFind(const FuncTable* t, Str name)
{
  for (int i = 0; i < t->count; i++)
    if (t->items[i].name.len == name.len && memcmp(t->items[i].name.p, name.p, (size_t)name.len) == 0)
      return &t->items[i];

  return NULL;
}

/* --------------------------------------------------- pass A: definition scan ----- */

/** Count the values a `return` yields. */
static int countReturnValues(const Node* r)
{
  if (r->kids.n == 0)
    return 0;

  /* `return (a, b);` is a list, one value per element. */
  if (r->lhsKind == LH_LIST)
    return r->kids.n;

  return 1;
}

/** @brief Find the return value count that the reference records for a function.
 *
 *  `returnCount` is whatever the *last visited* `return` produced, and the reference's
 *  traversal descends into a trailing control statement. A `return` inside a nested
 *  `if` therefore still sets it, which is what keeps p14 from also reporting mismatched
 *  return sizes; a `return` that is merely the last statement of the body is found by
 *  the plain tail check.
 */
static int lastReturnValues(const Node* b, int depth)
{
  if (!b || b->kind != N_BLOCK || b->kids.n == 0 || depth > 16)
    return -1;

  const Node* last = b->kids.v[b->kids.n - 1];

  if (last->kind == N_RETURN)
    return countReturnValues(last);

  if (last->kind == N_IF && last->kids.n >= 2)
    return lastReturnValues(last->kids.v[1], depth + 1);

  if ((last->kind == N_WHILE || last->kind == N_DOWHILE) && last->kids.n >= 1)
  {
    const int bi = (last->kind == N_DOWHILE) ? 0 : 1;

    return lastReturnValues(last->kids.v[bi], depth + 1);
  }

  return -1;
}

void emit_scan(Emit* e, const Source* src, int unitIndex, const char* prefix)
{
  (void)prefix;

  for (int i = 0; i < src->itemCount && !e->aborted; i++)
  {
    const TopItem* it = &src->items[i];

    if (it->kind != TOP_FUNC || it->fn == NULL)
      continue;

    const Func* fn = it->fn;

    /* `missing return` inspects only the tail of the body's statement list, which is
     * why p14 reports it even though its only `return` is inside the `if`. */
    const Node* last = (fn->body && fn->body->kids.n > 0) ? fn->body->kids.v[fn->body->kids.n - 1] : NULL;

    if (last == NULL || last->kind != N_RETURN)
    {
      char msg[256];
      snprintf(msg, sizeof(msg), "function \"%.*s\" missing return", fn->name.len, fn->name.p);
      diag_add(e->diag, fn->line, msg);
    }

    /* A function named like an ASM op is rejected: `OpType.byName` would win the name. */
    for (int k = 0; k < kOpNameCount; k++)
    {
      if (str_ieq_cstr(fn->name, kOpNames[k].name))
      {
        char msg[256];
        snprintf(msg, sizeof(msg), "function \"%.*s\" is not a legal name", fn->name.len, fn->name.p);
        diag_add(e->diag, fn->line, msg);
        break;
      }
    }

    const int returns = lastReturnValues(fn->body, 0);

    Str params[16];
    int nParams = (fn->argc < 16) ? fn->argc : 16;

    for (int k = 0; k < nParams; k++)
      params[k] = fn->args[k];

    (void)funcTableAdd(&e->table, e->arena, fn->name, params, nParams, returns < 0 ? 0 : returns, unitIndex, fn->line);
  }
}

/* ------------------------------------------------------- pass B: emission -------- */

static Val* emitExpr(Emit* e, const Node* n);
static void emitStmt(Emit* e, const Node* n);

static void pushScope(Emit* e)
{
  if (e->markCount < (int)(sizeof(e->marks) / sizeof(e->marks[0])))
    e->marks[e->markCount++] = e->bindCount;
}

static void popScope(Emit* e)
{
  if (e->markCount > 0)
    e->bindCount = e->marks[--e->markCount];
}

static void emitBlock(Emit* e, const Node* b)
{
  if (!b || b->kind != N_BLOCK || e->aborted)
    return;

  pushScope(e);

  for (int i = 0; i < b->kids.n && !e->aborted; i++)
    emitStmt(e, b->kids.v[i]);

  popScope(e);
}

/** Lower an identifier used as a value. */
static Val* emitVar(Emit* e, const Node* n)
{
  const Str name = str_of(n->name, strlen(n->name));

  /* An enum constant prints verbatim instead of resolving as a variable. */
  if (meta_is_enum_value(e->meta, name))
    return mkImm(e, n->name);

  /* `true` and `false` are lexer keywords with no parser rule that mentions them; the
   * reference reaches a NullPointerException when one appears in a value position.
   * Ruling R1 does not require reproducing the crash, only what is observable around
   * it: compilation stops here and no output is produced. */
  if (str_eq_cstr(name, "true") || str_eq_cstr(name, "false"))
  {
    char msg[64];
    snprintf(msg, sizeof(msg), "unimplemented op %.*s", name.len, name.p);
    diag_add(e->diag, n->line, msg);
    e->aborted = true;
    return NULL;
  }

  VarDecl* var = lookupVar(e, name);

  if (!var)
  {
    char msg[256];
    snprintf(msg, sizeof(msg), "variable \"%.*s\" is not defined in the current scope", name.len, name.p);
    diag_add(e->diag, n->line, msg);
    return mkUndefined(e);
  }

  return mkVar(e, var);
}

/** The bare first identifier of a call name, for the ASM-op lookup. */
static Str bareCallName(const Node* c)
{
  const char* sep = strstr(c->name, "::");

  return sep ? str_of(c->name, (size_t)(sep - c->name)) : str_of(c->name, strlen(c->name));
}

/** Look an ASM op up the way `OpType.byName` does: ASCII-insensitive, first match in
 *  declaration order wins, which matters for the duplicated display names. */
static const OpName* findOp(Str name)
{
  for (int i = 0; i < kOpNameCount; i++)
    if (str_ieq_cstr(name, kOpNames[i].name))
      return &kOpNames[i];

  return NULL;
}

/** @brief Lower a call, in statement or expression position.
 *
 *  @return The produced value in expression position, or NULL when the call produced
 *  none (ASM ops and engine calls), which the caller reports and renders as
 *  `undefined`. */
static Val* emitCall(Emit* e, const Node* c, bool inExpr)
{
  const int argc = c->kids.n;

  if (!c->isEngineCall)
  {
    const OpName* op = findOp(bareCallName(c));

    if (op != NULL)
    {
      Val* params[EMIT_MAX_PARAMS];
      int n = 0;

      if (op->paramCount != argc)
      {
        char msg[256];
        snprintf(msg, sizeof(msg), "expected %d params, got %d", op->paramCount, argc);
        diag_add(e->diag, c->line, msg);
      }

      for (int i = 0; i < argc && i < EMIT_MAX_PARAMS; i++)
      {
        if (c->kids.v[i]->kind == E_STR)
        {
          diag_add(e->diag, c->line, "strings cannot be passed to ASM ops");
          params[n++] = mkUndefined(e);
          continue;
        }

        params[n++] = emitExpr(e, c->kids.v[i]);
      }

      /* `wait` clones its operand first. The op counts down inside the slot it is
       * given, so waiting twice on a shared value would read 0 the second time; the
       * reference emits `mov <arg>, <fresh slot>` and waits on the copy instead. The
       * match is on the opcode, not the display name: `wait` is shared between two
       * entries here, and `wait_cmp` (opcodes 3 and 4) is a different operation that
       * takes no such copy. */
      if (op->opcode == 2 && n == 1 && !e->aborted)
      {
        Val* const slot = allocSlot(e);

        if (!slot)
          return NULL;

        Val* const copy[2] = {params[0], slot};
        putOp(e, "mov", copy, 2);
        params[0] = slot;
      }

      if (!e->aborted)
        putOp(e, kOpNames[op - kOpNames].name, params, n);

      if (inExpr)
      {
        diag_add(e->diag, c->line, "ASM ops and engine calls cannot be used in expressions");
        return mkUndefined(e);
      }

      return NULL;
    }
  }
  else
  {
    const MetaMethod* m = meta_find_method(e->meta, c->name);

    if (!m)
    {
      /* The whole statement disappears: no placeholder op is emitted. */
      char msg[288];
      snprintf(msg, sizeof(msg), "unknown engine call %s", c->name);
      diag_add(e->diag, c->line, msg);
      return NULL;
    }

    Val* params[EMIT_MAX_PARAMS + 1];
    int n = 0;

    params[n++] = mkImm(e, c->name);

    if (m->paramCount != argc)
    {
      char msg[256];
      snprintf(msg, sizeof(msg), "expected %d params, got %d", m->paramCount, argc);
      diag_add(e->diag, c->line, msg);
    }

    for (int i = 0; i < argc && n <= EMIT_MAX_PARAMS; i++)
    {
      /* A string is a legal engine-call parameter and becomes a `str[...]` slot. The
       * rejection and the `INVALID` placeholder belong to the user-function path
       * below, not here; routing both call forms through it dropped every argument
       * after the method name and printed three `INVALID`s for three real ones. */
      if (c->kids.v[i]->kind == E_STR)
      {
        params[n++] = allocStrSlot(e, c->kids.v[i]->text);
        continue;
      }

      params[n++] = emitExpr(e, c->kids.v[i]);
    }

    if (!e->aborted)
      putOp(e, "call", params, n);

    if (inExpr)
    {
      diag_add(e->diag, c->line, "ASM ops and engine calls cannot be used in expressions");
      return mkUndefined(e);
    }

    return NULL;
  }

  /* User-function call. An unknown name still emits the gosub; the reference only
   * records the diagnostic. */
  const FuncDef* def = funcTableFind(&e->table, bareCallName(c));

  if (def)
  {
    if (def->paramCount != argc)
    {
      char msg[256];
      snprintf(msg, sizeof(msg), "expected %d params, got %d", def->paramCount, argc);
      diag_add(e->diag, c->line, msg);
    }
  }
  else
  {
    char msg[256];
    snprintf(msg, sizeof(msg), "unknown function %s", c->name);
    diag_add(e->diag, c->line, msg);
  }

  for (int i = 0; i < argc; i++)
  {
    Val* v;

    if (c->kids.v[i]->kind == E_STR)
    {
      diag_add(e->diag, c->line, "strings cannot be passed as parameters");
      v = mkUndefined(e);
    }
    else
    {
      v = emitExpr(e, c->kids.v[i]);
    }

    Val* const push[1] = {v};
    putOp(e, "push", push, 1);
  }

  char gosubName[288];
  snprintf(gosubName, sizeof(gosubName), "inl[:%s]", c->name);

  Val* ref = mkVal(e, VK_IMM);

  if (ref)
    ref->text = arena_dup(e->arena, gosubName, strlen(gosubName));

  if (e->aborted)
    return NULL;

  /* The `gosub` belongs to the call itself, so it is emitted in both positions. Used as
   * an expression, the return values are then popped off the stack, which makes the
   * emitted order push / gosub / pop. Skipping it here dropped the `gosub` line from
   * every expression-position user call. */
  {
    Val* const op[1] = {ref};
    putOp(e, "gosub", op, 1);
  }

  if (!inExpr)
    return NULL;

  /* In expression position the return values are popped out of the stack. An unknown
   * function has no recorded arity here; the reference dereferences its missing
   * definition and aborts the file. Emitting one slot keeps the tool running, which is
   * the deliberate p10-class deviation from the reference (Ruling R1). */
  const int want = def ? def->returns : 1;

  if (want <= 0)
  {
    diag_add(e->diag, c->line, "ASM ops and engine calls cannot be used in expressions");
    return mkUndefined(e);
  }

  Val** items = NULL;

  if (want > 1)
  {
    items = (Val**)arena_alloc(e->arena, sizeof(Val*) * (size_t)want);
    if (!items)
      return NULL;
  }

  for (int i = want - 1; i >= 0; i--)
  {
    Val* slot = allocSlot(e);

    Val* const pop[1] = {slot};
    putOp(e, "pop", pop, 1);

    if (want > 1)
      items[i] = slot;
    else
      return slot;
  }

  Val* list = mkVal(e, VK_LIST);

  if (list)
  {
    list->items = items;
    list->n = want;
  }

  return list;
}

/** Lower a binary or unary operator. */
static Val* emitBinary(Emit* e, const Node* n)
{
  /* The result slot is allocated before the operands are lowered. Reversing the two
   * shifts every later `_expr_N` number in the file. */
  Val* out = allocSlot(e);

  if (!out)
    return NULL;

  const bool unary = (n->bin >= UNY_NOT);

  if (unary)
  {
    Val* src = emitExpr(e, n->kids.v[0]);

    if (!src)
      return NULL;

    if (n->bin == UNY_NOT)
    {
      Val* const p[4] = {mkImm(e, "=="), src, mkImm(e, "0"), out};
      putOp(e, "cmp", p, 4);
      return out;
    }

    {
      Val* const p2[2] = {src, out};
      putOp(e, "mov", p2, 2);

      if (n->bin == UNY_SUB)
      {
        Val* const p1[1] = {out};
        putOp(e, "neg", p1, 1);
      }
      else if (n->bin == UNY_NOTA)
      {
        Val* const p1[1] = {out};
        putOp(e, "not", p1, 1);
      }

      return out;
    }
  }

  Val* lhs = emitExpr(e, n->kids.v[0]);
  Val* rhs = emitExpr(e, n->kids.v[1]);

  if (!lhs || !rhs)
    return NULL;

  switch (n->bin)
  {
    case BIN_MUL:
    case BIN_DIV:
    case BIN_MOD:
    case BIN_ADD:
    case BIN_SUB:
    case BIN_SHR:
    case BIN_SHL:
    case BIN_ANDA:
    case BIN_ORA:
    case BIN_XORA:
    {
      Val* const p2[2] = {lhs, out};
      putOp(e, "mov", p2, 2);

      const char* op = "add";

      switch (n->bin)
      {
        case BIN_MUL:
          op = "mul";
          break;
        case BIN_DIV:
          op = "div";
          break;
        case BIN_MOD:
          op = "mod";
          break;
        case BIN_ADD:
          op = "add";
          break;
        case BIN_SUB:
          op = "sub";
          break;
        case BIN_SHR:
          op = "shr";
          break;
        case BIN_SHL:
          op = "shl";
          break;
        case BIN_ANDA:
          op = "and";
          break;
        case BIN_ORA:
          op = "or";
          break;
        case BIN_XORA:
          op = "xor";
          break;
        default:
          break;
      }

      Val* const p3[2] = {rhs, out};
      putOp(e, op, p3, 2);
      return out;
    }

    case BIN_GT:
    {
      Val* const p[4] = {mkImm(e, ">"), lhs, rhs, out};
      putOp(e, "cmp", p, 4);
      return out;
    }

    case BIN_LT:
    {
      Val* const p[4] = {mkImm(e, "<"), lhs, rhs, out};
      putOp(e, "cmp", p, 4);
      return out;
    }

    case BIN_GTE:
    {
      Val* const p[4] = {mkImm(e, ">="), lhs, rhs, out};
      putOp(e, "cmp", p, 4);
      return out;
    }

    case BIN_LTE:
    {
      Val* const p[4] = {mkImm(e, "<="), lhs, rhs, out};
      putOp(e, "cmp", p, 4);
      return out;
    }

    case BIN_EQ:
    {
      Val* const p[4] = {mkImm(e, "=="), lhs, rhs, out};
      putOp(e, "cmp", p, 4);
      return out;
    }

    case BIN_NEQ:
    {
      Val* const p[4] = {mkImm(e, "!="), lhs, rhs, out};
      putOp(e, "cmp", p, 4);
      return out;
    }

    case BIN_ANDC:
    {
      Val* const p[4] = {mkImm(e, "&&"), lhs, rhs, out};
      putOp(e, "cmp", p, 4);
      return out;
    }

    case BIN_ORC:
    {
      Val* const p[4] = {mkImm(e, "||"), lhs, rhs, out};
      putOp(e, "cmp", p, 4);
      return out;
    }

    default:
      break;
  }

  char msg[256];
  snprintf(msg, sizeof(msg), "unimplemented op binary");
  diag_add(e->diag, n->line, msg);

  return out;
}

static Val* emitExpr(Emit* e, const Node* n)
{
  if (!n)
    return NULL;

  switch (n->kind)
  {
    case E_NUM:
      return mkImmStr(e, n->text);
    case E_STR:
      return mkImm(e, "INVALID");
    case E_VAR:
      return emitVar(e, n);

    case E_ARRAY:
    {
      Val* base = emitVar(e, n);

      if (!base)
        return NULL;

      Val* idx = toSlot(e, emitExpr(e, n->kids.v[0]));

      return mkBinaryForm(e, VK_ARRAY, base, idx);
    }

    case E_STOR:
    {
      if (n->kids.n == 1)
        return mkBinaryForm(e, VK_STOR1, toSlot(e, emitExpr(e, n->kids.v[0])), NULL);

      return mkBinaryForm(e, VK_STOR2, toSlot(e, emitExpr(e, n->kids.v[0])), toSlot(e, emitExpr(e, n->kids.v[1])));
    }

    case E_GAMEVAR:
    {
      if (n->kids.n == 1)
        return mkBinaryForm(e, VK_GV1, toSlot(e, emitExpr(e, n->kids.v[0])), NULL);

      return mkBinaryForm(e, VK_GV2, toSlot(e, emitExpr(e, n->kids.v[0])), toSlot(e, emitExpr(e, n->kids.v[1])));
    }

    case E_REG:
      /* The register index prints verbatim: no slot is allocated for it. */
      return mkBinaryForm(e, VK_REG, emitExpr(e, n->kids.v[0]), NULL);

    case E_UNARY:
    case E_BINARY:
      return emitBinary(e, n);

    case E_CALL:
      return emitCall(e, n, true);

    default:
      break;
  }

  char msg[256];
  snprintf(msg, sizeof(msg), "unimplemented value");
  diag_add(e->diag, n->line, msg);

  return mkUndefined(e);
}

/** Lower the destination of an assignment or postfix op. */
static Val* emitAssignable(Emit* e, const Node* n)
{
  switch (n->kind)
  {
    case E_VAR:
      return emitVar(e, n);
    case E_CALL:
      return NULL; /* a call target is not a value position */

    case E_STOR:
    {
      if (n->kids.n == 1)
        return mkBinaryForm(e, VK_STOR1, toSlot(e, emitExpr(e, n->kids.v[0])), NULL);

      return mkBinaryForm(e, VK_STOR2, toSlot(e, emitExpr(e, n->kids.v[0])), toSlot(e, emitExpr(e, n->kids.v[1])));
    }

    case E_GAMEVAR:
    {
      if (n->kids.n == 1)
        return mkBinaryForm(e, VK_GV1, toSlot(e, emitExpr(e, n->kids.v[0])), NULL);

      return mkBinaryForm(e, VK_GV2, toSlot(e, emitExpr(e, n->kids.v[0])), toSlot(e, emitExpr(e, n->kids.v[1])));
    }

    case E_REG:
      return mkBinaryForm(e, VK_REG, emitExpr(e, n->kids.v[0]), NULL);

    default:
      break;
  }

  return mkUndefined(e);
}

/* --------------------------------------------------------------- statements ----- */

static void emitAssignment(Emit* e, const Node* rhs, Val** targets, int targetCount)
{
  Val* src = emitExpr(e, rhs);

  if (!src)
    return;

  for (int i = 0; i < targetCount; i++)
  {
    Val* const p[2] = {src, targets[i]};
    putOp(e, "mov", p, 2);
  }
}

static void emitDecl(Emit* e, const Node* n)
{
  Val* targets[EMIT_MAX_PARAMS];
  int count = 0;

  for (int i = 0; i < n->names.n && count < EMIT_MAX_PARAMS; i++)
  {
    const Node* nameNode = n->names.v[i];
    const Str name = str_of(nameNode->name, strlen(nameNode->name));

    if (hasVar(e, name))
    {
      char msg[256];
      snprintf(msg, sizeof(msg), "variable %.*s already defined in scope", name.len, name.p);
      diag_add(e->diag, n->line, msg);
    }

    targets[count++] = mkVar(e, declareVar(e, name.p ? name.p : "", 1, "0"));
  }

  if (n->initKind == INIT_EXPR && n->init)
    emitAssignment(e, n->init, targets, count);
  else if (n->initKind == INIT_ARRAY)
  {
    for (int i = 0; i < count; i++)
    {
      for (int k = 0; k < n->kids.n; k++)
      {
        Val* src = emitExpr(e, n->kids.v[k]);

        if (!src)
          continue;

        Val* const p[2] = {src, targets[i]};
        putOp(e, "mov", p, 2);
      }
    }
  }
}

static void emitReturn(Emit* e, const Node* n)
{
  Val* values[EMIT_MAX_PARAMS];
  int count = 0;

  for (int i = 0; i < n->kids.n && count < EMIT_MAX_PARAMS; i++)
  {
    Val* v = emitExpr(e, n->kids.v[i]);

    if (v)
      values[count++] = v;
  }

  if (e->fn && e->fn->returns != count)
  {
    char msg[256];
    snprintf(msg, sizeof(msg), "return expected %d parameter(s), got %d instead", e->fn->returns, count);
    diag_add(e->diag, n->line, msg);
  }

  for (int i = 0; i < count; i++)
  {
    Val* const p[1] = {values[i]};
    putOp(e, "push", p, 1);
  }

  putOp(e, "return", NULL, 0);
}

static void emitIf(Emit* e, const Node* n)
{
  Val* cond = emitExpr(e, n->kids.v[0]);

  if (!cond)
    return;

  const int target = e->labelIndex++;
  const int hasElse = (n->kids.n >= 3);
  int end = -1;

  if (hasElse)
  {
    /* The jump-over label is allocated only after the then-block has been lowered, so
     * a nested `if` inside it claims the lower number. */
    end = -2; /* reserved, numbered below */
  }

  Val* const jumpToTarget[4] = {mkImm(e, "=="), mkImm(e, "0"), cond, mkLabelRef(e, target)};
  putOp(e, "jmp_cmp", jumpToTarget, 4);

  emitBlock(e, n->kids.v[1]);

  if (hasElse)
  {
    end = e->labelIndex++;
    Val* const skip[1] = {mkLabelRef(e, end)};
    putOp(e, "jmp", skip, 1);
  }

  putLabelLine(e, target);

  if (hasElse)
  {
    const Node* alt = n->kids.v[2];

    if (alt->kind == N_IF)
      emitIf(e, alt);
    else
      emitBlock(e, alt);

    putLabelLine(e, end);
  }
}

static void emitWhile(Emit* e, const Node* n)
{
  const int start = e->labelIndex++;
  const int end = e->labelIndex++;

  if (e->loopCount < (int)(sizeof(e->loops) / sizeof(e->loops[0])))
  {
    e->loops[e->loopCount].start = start;
    e->loops[e->loopCount].end = end;
    e->loopCount++;
  }

  putLabelLine(e, start);

  Val* cond = emitExpr(e, n->kids.v[0]);

  if (!cond)
  {
    e->loopCount--;
    return;
  }

  Val* const test[4] = {mkImm(e, "=="), mkImm(e, "0"), cond, mkLabelRef(e, end)};
  putOp(e, "jmp_cmp", test, 4);

  emitBlock(e, n->kids.v[1]);

  Val* const back[1] = {mkLabelRef(e, start)};
  putOp(e, "jmp", back, 1);

  putLabelLine(e, end);

  e->loopCount--;
}

static void emitDoWhile(Emit* e, const Node* n)
{
  const int start = e->labelIndex++;
  const int end = e->labelIndex++;

  if (e->loopCount < (int)(sizeof(e->loops) / sizeof(e->loops[0])))
  {
    e->loops[e->loopCount].start = start;
    e->loops[e->loopCount].end = end;
    e->loopCount++;
  }

  putLabelLine(e, start);
  emitBlock(e, n->kids.v[0]);

  Val* cond = emitExpr(e, n->kids.v[1]);

  if (!cond)
  {
    e->loopCount--;
    return;
  }

  /* A do-while loops *while* the condition holds, hence `!=`, and it falls through to
   * its end label rather than jumping back. */
  Val* const test[4] = {mkImm(e, "!="), mkImm(e, "0"), cond, mkLabelRef(e, end)};
  putOp(e, "jmp_cmp", test, 4);

  putLabelLine(e, end);

  e->loopCount--;
}

static void emitStmt(Emit* e, const Node* n)
{
  switch (n->kind)
  {
    case N_DECL:
      emitDecl(e, n);
      break;

    case N_RETURN:
      emitReturn(e, n);
      break;

    case N_IF:
      emitIf(e, n);
      break;

    case N_WHILE:
      emitWhile(e, n);
      break;

    case N_DOWHILE:
      emitDoWhile(e, n);
      break;

    case N_CALL:
      (void)emitCall(e, n->kids.v[0], false);
      break;

    case N_BREAK:
    {
      if (e->loopCount == 0)
      {
        diag_add(e->diag, n->line, "break is only valid inside of loop");
        break;
      }

      Val* const p[1] = {mkLabelRef(e, e->loops[e->loopCount - 1].end)};
      putOp(e, "jmp", p, 1);
      break;
    }

    case N_CONTINUE:
    {
      if (e->loopCount == 0)
      {
        diag_add(e->diag, n->line, "continue is only valid inside of loop");
        break;
      }

      Val* const p[1] = {mkLabelRef(e, e->loops[e->loopCount - 1].start)};
      putOp(e, "jmp", p, 1);
      break;
    }

    case N_POSTFIX:
    {
      Val* dest = emitAssignable(e, n->names.v[0]);

      if (!dest)
        break;

      Val* const p[1] = {dest};
      putOp(e, n->isUnary ? "decr" : "incr", p, 1);
      break;
    }

    case N_AUGASSIGN:
    {
      Val* dest = emitAssignable(e, n->names.v[0]);

      if (!dest)
        break;

      Val* rhs = emitExpr(e, n->kids.v[0]);

      if (!rhs)
        break;

      if (n->aug == AUG_ANDC)
      {
        Val* const p[4] = {mkImm(e, "&&"), dest, rhs, dest};
        putOp(e, "cmp", p, 4);
      }
      else if (n->aug == AUG_ORC)
      {
        Val* const p[4] = {mkImm(e, "||"), dest, rhs, dest};
        putOp(e, "cmp", p, 4);
      }
      else
      {
        const char* op = "add";

        switch (n->aug)
        {
          case AUG_MUL:
            op = "mul";
            break;
          case AUG_DIV:
            op = "div";
            break;
          case AUG_MOD:
            op = "mod";
            break;
          case AUG_ADD:
            op = "add";
            break;
          case AUG_SUB:
            op = "sub";
            break;
          case AUG_SHR:
            op = "shr";
            break;
          case AUG_SHL:
            op = "shl";
            break;
          case AUG_ANDA:
            op = "and";
            break;
          case AUG_ORA:
            op = "or";
            break;
          case AUG_XORA:
            op = "xor";
            break;
          case AUG_NOTA:
            op = "not";
            break;
          default:
            break;
        }

        Val* const p[2] = {rhs, dest};
        putOp(e, op, p, 2);
      }

      break;
    }

    case N_ASSIGN:
    {
      Val* targets[EMIT_MAX_PARAMS];
      int count = 0;

      for (int i = 0; i < n->names.n && count < EMIT_MAX_PARAMS; i++)
      {
        Val* v = emitAssignable(e, n->names.v[i]);

        if (v)
          targets[count++] = v;
      }

      if (n->initKind == INIT_EXPR && n->init)
        emitAssignment(e, n->init, targets, count);
      break;
    }

    default:
      break;
  }
}

/* ------------------------------------------------------------- whole file ------- */

/** @brief Emit one function: prologue, then header, then op lines.
 *
 *  The three parts are what every golden file shows:
 *  `"\n"`, the deduplicated `name:` / `data 0` block, then `name:` plus its ops. */
static void emitFunction(Emit* e, const Source* src, const Func* fn)
{
  (void)src;

  buf_init(&e->body, e->arena);
  e->varCount = 0;
  /* `_expr_` and `_label_` counters are per translation unit, not per function: the
   * reference hands one FateContext to every file, so a later function continues the
   * numbering the earlier ones started. */
  e->loopCount = 0;
  e->bindCount = e->globalMark;
  e->markCount = 0;

  if (fn->name.len >= (int)sizeof(e->prefix))
    return;

  memcpy(e->prefix, fn->name.p, (size_t)fn->name.len);
  e->prefix[fn->name.len] = '\0';

  e->fn = funcTableFind(&e->table, fn->name);

  pushScope(e);

  for (int i = 0; i < fn->argc; i++)
  {
    /* `Str` is not NUL-terminated and `declareVar` takes a C string, so copy first:
     * passing `.p` straight through made the prologue print run on into the body. */
    char argName[128];
    const int len = (fn->args[i].len < 0)                       ? 0
                    : (fn->args[i].len >= (int)sizeof(argName)) ? (int)sizeof(argName) - 1
                                                                : fn->args[i].len;

    memcpy(argName, fn->args[i].p, (size_t)len);
    argName[len] = '\0';

    (void)declareVar(e, argName, 1, "0");
  }

  /* The body reads its arguments off the stack, last pushed first. Everything below
   * goes into `e->body`, which is appended after the header; `e->out` must not be
   * touched here or the function gains a stray blank line. */
  for (int i = fn->argc - 1; i >= 0; i--)
  {
    VarDecl* v = lookupVar(e, fn->args[i]);

    if (!v)
      continue;

    Val* const p[1] = {mkVar(e, v)};
    putOp(e, "pop", p, 1);
  }

  emitBlock(e, fn->body);

  /* Assemble: leading blank line, prologue, header, body. */
  buf_putc(&e->out, '\n');

  const char* previous = NULL;

  for (int i = 0; i < e->varCount; i++)
  {
    const VarDecl* v = e->vars[i];

    if (previous && strcmp(previous, v->display) == 0)
      continue;

    previous = v->display;
    buf_puts(&e->out, v->display);
    buf_puts(&e->out, ":\n");

    for (int k = 0; k < v->length; k++)
    {
      buf_puts(&e->out, "data ");
      buf_puts(&e->out, v->value);
      buf_putc(&e->out, '\n');
    }
  }

  buf_put_str(&e->out, fn->name);
  buf_puts(&e->out, ":\n");
  buf_put(&e->out, e->body.data, e->body.len);
}

Status emit_source(Emit* e, const Source* src)
{
  buf_puts(&e->out, "\n");

  for (int i = 0; i < src->itemCount; i++)
  {
    const TopItem* it = &src->items[i];

    switch (it->kind)
    {
      case TOP_INCLUDE:
        buf_puts(&e->out, "#include ");
        buf_puts(&e->out, it->name);
        buf_putc(&e->out, '\n');
        break;

      case TOP_GLOBAL:
      {
        const GlobalVar* g = it->globs;

        if (g != NULL)
        {
          buf_puts(&e->out, g->name);
          buf_puts(&e->out, ":\n");

          for (int k = 0; k < g->length; k++)
          {
            buf_puts(&e->out, "data ");
            buf_puts(&e->out, g->values[k]);
            buf_putc(&e->out, '\n');
          }
        }

        buf_putc(&e->out, '\n');
        break;
      }

      case TOP_FUNC:
        if (it->fn != NULL)
          emitFunction(e, src, it->fn);

        break;
    }

    if (e->aborted)
      return ST_COMPILE;
  }

  return e->aborted ? ST_COMPILE : ST_OK;
}
