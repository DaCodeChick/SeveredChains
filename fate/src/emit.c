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
typedef enum vk_t
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
} vk_t;

/** A variable slot: the unit of the prologue and of `inl[:]` rendering. */
typedef struct var_decl_t
{
  const char* display; /**< already prefixed, exactly as it prints */
  int length;
  const char* value; /**< text after `data ` in the prologue */
} var_decl_t;

/** A value, i.e. one thing printable as an op parameter. */
typedef struct val_t val_t;

struct val_t
{
  vk_t kind;
  const char* text;
  var_decl_t* var;
  struct val_t* a;
  struct val_t* b;
  val_t** items;
  int n;
};

/** One name binding in the scope chain. */
typedef struct binding_t
{
  const char* name;
  var_decl_t* var;
} binding_t;

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
static var_decl_t* declare_var(emit_t* e, const char* base, int length, const char* value)
{
  for (int i = 0; i < e->bind_count; i++)
    if (strcmp(e->binds[i].name, base) == 0)
      return e->binds[i].var;

  if (e->bind_count == e->bind_cap)
  {
    const int new_cap = e->bind_cap ? e->bind_cap * 2 : 128;
    binding_t* grown = (binding_t*)arena_alloc(e->arena, (size_t)new_cap * sizeof(binding_t));

    if (!grown)
      return NULL;

    if (e->bind_count > 0)
      memcpy(grown, e->binds, (size_t)e->bind_count * sizeof(binding_t));

    e->binds = grown;
    e->bind_cap = new_cap;
  }

  if (e->bind_count == 0)
    e->bind_cap = (e->bind_cap < 128) ? 128 : e->bind_cap;

  var_decl_t* var = (var_decl_t*)arena_alloc(e->arena, sizeof(var_decl_t));

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

  if (e->var_count < (int)(sizeof(e->vars) / sizeof(e->vars[0])))
    e->vars[e->var_count++] = var;

  e->binds[e->bind_count].name = arena_dup(e->arena, base, strlen(base));
  e->binds[e->bind_count].var = var;
  e->bind_count++;

  return var;
}

/** Find a binding by bare name, innermost scope first. */
static var_decl_t* lookup_var(emit_t* e, str_t name)
{
  for (int i = e->bind_count - 1; i >= 0; i--)
    if ((int)strlen(e->binds[i].name) == name.len && memcmp(e->binds[i].name, name.p, (size_t)name.len) == 0)
      return e->binds[i].var;

  return NULL;
}

static bool has_var(emit_t* e, str_t name)
{
  return lookup_var(e, name) != NULL;
}

void emit_bind_global(emit_t* e, const char* name, int length, const char* value)
{
  const char* saved = e->prefix;
  char empty[1] = {0};

  e->prefix[0] = '\0';
  (void)saved;
  (void)empty;

  /* Declared outside any function, so it takes no prefix and joins no function's
   * prologue; the prologue for a `var` block is rendered from the TopItem instead. */
  (void)declare_var(e, name, length, value);

  e->global_mark = e->bind_count;
}

/* ------------------------------------------------------------- op rendering ------ */

static void render_val(buf_t* out, const val_t* v);

/** One rendered op line, including its terminating newline. */
static void put_op(emit_t* e, const char* name, val_t* const* params, int n)
{
  buf_puts(&e->body, name);
  buf_putc(&e->body, ' ');

  for (int i = 0; i < n; i++)
  {
    if (i > 0)
      buf_puts(&e->body, ", ");

    render_val(&e->body, params[i]);
  }

  buf_putc(&e->body, '\n');
}

static void put_label_line(emit_t* e, int label)
{
  buf_printf(&e->body, "_label_%d:\n", label);
}

/* ---------------------------------------------------------------- value helpers -- */

static val_t* mk_val(emit_t* e, vk_t kind)
{
  val_t* v = (val_t*)arena_alloc(e->arena, sizeof(val_t));

  if (!v)
  {
    e->aborted = true;
    return NULL;
  }

  v->kind = kind;
  return v;
}

static val_t* mk_imm(emit_t* e, const char* text)
{
  val_t* v = mk_val(e, VK_IMM);

  if (v)
    v->text = text;

  return v;
}

static val_t* mk_var(emit_t* e, var_decl_t* var)
{
  val_t* v = mk_val(e, VK_VAR);

  if (v)
    v->var = var;

  return v;
}

static val_t* mk_label_ref(emit_t* e, int label)
{
  char name[32];
  snprintf(name, sizeof(name), "_label_%d", label);

  val_t* v = mk_val(e, VK_LABEL);

  if (v)
    v->text = arena_dup(e->arena, name, strlen(name));

  return v;
}

static val_t* mk_binary_form(emit_t* e, vk_t kind, val_t* a, val_t* b)
{
  val_t* v = mk_val(e, kind);

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
static val_t* mk_imm_str(emit_t* e, str_t text)
{
  val_t* v = mk_val(e, VK_IMM);

  if (!v)
    return NULL;

  const int len = (text.len < 0) ? 0 : (text.len >= (int)sizeof(e->scratch)) ? (int)sizeof(e->scratch) - 1 : text.len;

  memcpy(e->scratch, text.p, (size_t)len);
  e->scratch[len] = '\0';
  v->text = arena_dup(e->arena, e->scratch, (size_t)len);

  return v;
}

static val_t* mk_undefined(emit_t* e)
{
  return mk_imm(e, "undefined");
}

/** A variable reference is "pure": it prints without first being copied to a slot. */
static bool is_pure(const val_t* v)
{
  return v != NULL && v->kind == VK_VAR;
}

static void render_val(buf_t* out, const val_t* v)
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
      render_val(out, v->a);
      buf_putc(out, '[');
      render_val(out, v->b);
      buf_putc(out, ']');
      break;

    case VK_GV1:
      buf_puts(out, "var[");
      render_val(out, v->a);
      buf_putc(out, ']');
      break;

    case VK_GV2:
      buf_puts(out, "var[");
      render_val(out, v->a);
      buf_puts(out, "][");
      render_val(out, v->b);
      buf_putc(out, ']');
      break;

    case VK_STOR1:
      buf_puts(out, "stor[");
      render_val(out, v->a);
      buf_putc(out, ']');
      break;

    case VK_STOR2:
      buf_puts(out, "stor[");
      render_val(out, v->a);
      buf_puts(out, ", ");
      render_val(out, v->b);
      buf_putc(out, ']');
      break;

    case VK_REG:
      buf_puts(out, "reg[");
      render_val(out, v->a);
      buf_putc(out, ']');
      break;

    case VK_LIST:
      buf_putc(out, '[');

      for (int i = 0; i < v->n; i++)
      {
        if (i > 0)
          buf_puts(out, ", ");

        render_val(out, v->items[i]);
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
static val_t* alloc_slot(emit_t* e)
{
  char name[32];
  snprintf(name, sizeof(name), "_expr_%d", e->expr_index++);

  return mk_var(e, declare_var(e, name, 1, "0"));
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
static val_t* alloc_str_slot(emit_t* e, str_t body)
{
  char name[32];
  snprintf(name, sizeof(name), "_expr_%d", e->expr_index++);

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

  var_decl_t* var = declare_var(e, name, 1, data);

  if (!var)
  {
    e->aborted = true;
    return NULL;
  }

  return mk_var(e, var);
}

/** @brief Give @p v as an operand, copying it to a slot unless it already is one
 *         (`valueToVariable` in the reference). */
static val_t* to_slot(emit_t* e, val_t* v)
{
  if (is_pure(v))
    return v;

  val_t* slot = alloc_slot(e);

  if (!slot)
    return NULL;

  val_t* const params[2] = {v, slot};
  put_op(e, "mov", params, 2);

  return slot;
}

/* ---------------------------------------------------------- function table ------ */

func_def_t* func_table_add(func_table_t* t, arena_t* arena, str_t name, str_t params[], int param_count, int returns,
                           int unit_index, int line)
{
  func_def_t* slot = NULL;

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
      const int new_cap = t->cap ? t->cap * 2 : 32;
      func_def_t* grown = (func_def_t*)arena_alloc(arena, (size_t)new_cap * sizeof(func_def_t));

      if (!grown)
        return NULL;

      if (t->count > 0)
        memcpy(grown, t->items, (size_t)t->count * sizeof(func_def_t));

      t->items = grown;
      t->cap = new_cap;
    }

    slot = &t->items[t->count++];
  }

  slot->name = name;
  slot->param_count = param_count < 16 ? param_count : 16;
  slot->returns = returns;
  slot->unit_index = unit_index;
  slot->line = line;

  for (int i = 0; i < slot->param_count; i++)
    slot->params[i] = params[i];

  return slot;
}

const func_def_t* func_table_find(const func_table_t* t, str_t name)
{
  for (int i = 0; i < t->count; i++)
    if (t->items[i].name.len == name.len && memcmp(t->items[i].name.p, name.p, (size_t)name.len) == 0)
      return &t->items[i];

  return NULL;
}

/* --------------------------------------------------- pass A: definition scan ----- */

/** Count the values a `return` yields. */
static int count_return_values(const node_t* r)
{
  if (r->kids.n == 0)
    return 0;

  /* `return (a, b);` is a list, one value per element. */
  if (r->lhs_kind == LH_LIST)
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
static int last_return_values(const node_t* b, int depth)
{
  if (!b || b->kind != N_BLOCK || b->kids.n == 0 || depth > 16)
    return -1;

  const node_t* last = b->kids.v[b->kids.n - 1];

  if (last->kind == N_RETURN)
    return count_return_values(last);

  if (last->kind == N_IF && last->kids.n >= 2)
    return last_return_values(last->kids.v[1], depth + 1);

  if ((last->kind == N_WHILE || last->kind == N_DOWHILE) && last->kids.n >= 1)
  {
    const int bi = (last->kind == N_DOWHILE) ? 0 : 1;

    return last_return_values(last->kids.v[bi], depth + 1);
  }

  return -1;
}

void emit_scan(emit_t* e, const source_t* src, int unit_index, const char* prefix)
{
  (void)prefix;

  for (int i = 0; i < src->item_count && !e->aborted; i++)
  {
    const top_item_t* it = &src->items[i];

    if (it->kind != TOP_FUNC || it->fn == NULL)
      continue;

    const func_t* fn = it->fn;

    /* `missing return` inspects only the tail of the body's statement list, which is
     * why p14 reports it even though its only `return` is inside the `if`. */
    const node_t* last = (fn->body && fn->body->kids.n > 0) ? fn->body->kids.v[fn->body->kids.n - 1] : NULL;

    if (last == NULL || last->kind != N_RETURN)
    {
      char msg[256];
      snprintf(msg, sizeof(msg), "function \"%.*s\" missing return", fn->name.len, fn->name.p);
      diag_add(e->diag, fn->line, msg);
    }

    /* A function named like an ASM op is rejected: `OpType.byName` would win the name. */
    for (int k = 0; k < OP_NAME_COUNT; k++)
    {
      if (str_ieq_cstr(fn->name, OP_NAMES[k].name))
      {
        char msg[256];
        snprintf(msg, sizeof(msg), "function \"%.*s\" is not a legal name", fn->name.len, fn->name.p);
        diag_add(e->diag, fn->line, msg);
        break;
      }
    }

    const int returns = last_return_values(fn->body, 0);

    str_t params[16];
    int n_params = (fn->argc < 16) ? fn->argc : 16;

    for (int k = 0; k < n_params; k++)
      params[k] = fn->args[k];

    (void)func_table_add(&e->table, e->arena, fn->name, params, n_params, returns < 0 ? 0 : returns, unit_index,
                         fn->line);
  }
}

/* ------------------------------------------------------- pass B: emission -------- */

static val_t* emit_expr(emit_t* e, const node_t* n);
static void emit_stmt(emit_t* e, const node_t* n);

static void push_scope(emit_t* e)
{
  if (e->mark_count < (int)(sizeof(e->marks) / sizeof(e->marks[0])))
    e->marks[e->mark_count++] = e->bind_count;
}

static void pop_scope(emit_t* e)
{
  if (e->mark_count > 0)
    e->bind_count = e->marks[--e->mark_count];
}

static void emit_block(emit_t* e, const node_t* b)
{
  if (!b || b->kind != N_BLOCK || e->aborted)
    return;

  push_scope(e);

  for (int i = 0; i < b->kids.n && !e->aborted; i++)
    emit_stmt(e, b->kids.v[i]);

  pop_scope(e);
}

/** Lower an identifier used as a value. */
static val_t* emit_var(emit_t* e, const node_t* n)
{
  const str_t name = str_of(n->name, strlen(n->name));

  /* An enum constant prints verbatim instead of resolving as a variable. */
  if (meta_is_enum_value(e->meta, name))
    return mk_imm(e, n->name);

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

  var_decl_t* var = lookup_var(e, name);

  if (!var)
  {
    char msg[256];
    snprintf(msg, sizeof(msg), "variable \"%.*s\" is not defined in the current scope", name.len, name.p);
    diag_add(e->diag, n->line, msg);
    return mk_undefined(e);
  }

  return mk_var(e, var);
}

/** The bare first identifier of a call name, for the ASM-op lookup. */
static str_t bare_call_name(const node_t* c)
{
  const char* sep = strstr(c->name, "::");

  return sep ? str_of(c->name, (size_t)(sep - c->name)) : str_of(c->name, strlen(c->name));
}

/** Look an ASM op up the way `OpType.byName` does: ASCII-insensitive, first match in
 *  declaration order wins, which matters for the duplicated display names. */
static const op_name_t* find_op(str_t name)
{
  for (int i = 0; i < OP_NAME_COUNT; i++)
    if (str_ieq_cstr(name, OP_NAMES[i].name))
      return &OP_NAMES[i];

  return NULL;
}

/** @brief Lower a call, in statement or expression position.
 *
 *  @return The produced value in expression position, or NULL when the call produced
 *  none (ASM ops and engine calls), which the caller reports and renders as
 *  `undefined`. */
static val_t* emit_call(emit_t* e, const node_t* c, bool in_expr)
{
  const int argc = c->kids.n;

  if (!c->is_engine_call)
  {
    const op_name_t* op = find_op(bare_call_name(c));

    if (op != NULL)
    {
      val_t* params[EMIT_MAX_PARAMS];
      int n = 0;

      if (op->param_count != argc)
      {
        char msg[256];
        snprintf(msg, sizeof(msg), "expected %d params, got %d", op->param_count, argc);
        diag_add(e->diag, c->line, msg);
      }

      for (int i = 0; i < argc && i < EMIT_MAX_PARAMS; i++)
      {
        if (c->kids.v[i]->kind == E_STR)
        {
          diag_add(e->diag, c->line, "strings cannot be passed to ASM ops");
          params[n++] = mk_undefined(e);
          continue;
        }

        params[n++] = emit_expr(e, c->kids.v[i]);
      }

      /* `wait` clones its operand first. The op counts down inside the slot it is
       * given, so waiting twice on a shared value would read 0 the second time; the
       * reference emits `mov <arg>, <fresh slot>` and waits on the copy instead. The
       * match is on the opcode, not the display name: `wait` is shared between two
       * entries here, and `wait_cmp` (opcodes 3 and 4) is a different operation that
       * takes no such copy. */
      if (op->opcode == 2 && n == 1 && !e->aborted)
      {
        val_t* const slot = alloc_slot(e);

        if (!slot)
          return NULL;

        val_t* const copy[2] = {params[0], slot};
        put_op(e, "mov", copy, 2);
        params[0] = slot;
      }

      if (!e->aborted)
        put_op(e, OP_NAMES[op - OP_NAMES].name, params, n);

      if (in_expr)
      {
        diag_add(e->diag, c->line, "ASM ops and engine calls cannot be used in expressions");
        return mk_undefined(e);
      }

      return NULL;
    }
  }
  else
  {
    const meta_method_t* m = meta_find_method(e->meta, c->name);

    if (!m)
    {
      /* The whole statement disappears: no placeholder op is emitted. */
      char msg[288];
      snprintf(msg, sizeof(msg), "unknown engine call %s", c->name);
      diag_add(e->diag, c->line, msg);
      return NULL;
    }

    val_t* params[EMIT_MAX_PARAMS + 1];
    int n = 0;

    params[n++] = mk_imm(e, c->name);

    if (m->param_count != argc)
    {
      char msg[256];
      snprintf(msg, sizeof(msg), "expected %d params, got %d", m->param_count, argc);
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
        params[n++] = alloc_str_slot(e, c->kids.v[i]->text);
        continue;
      }

      params[n++] = emit_expr(e, c->kids.v[i]);
    }

    if (!e->aborted)
      put_op(e, "call", params, n);

    if (in_expr)
    {
      diag_add(e->diag, c->line, "ASM ops and engine calls cannot be used in expressions");
      return mk_undefined(e);
    }

    return NULL;
  }

  /* User-function call. An unknown name still emits the gosub; the reference only
   * records the diagnostic. */
  const func_def_t* def = func_table_find(&e->table, bare_call_name(c));

  if (def)
  {
    if (def->param_count != argc)
    {
      char msg[256];
      snprintf(msg, sizeof(msg), "expected %d params, got %d", def->param_count, argc);
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
    val_t* v;

    if (c->kids.v[i]->kind == E_STR)
    {
      diag_add(e->diag, c->line, "strings cannot be passed as parameters");
      v = mk_undefined(e);
    }
    else
    {
      v = emit_expr(e, c->kids.v[i]);
    }

    val_t* const push[1] = {v};
    put_op(e, "push", push, 1);
  }

  char gosub_name[288];
  snprintf(gosub_name, sizeof(gosub_name), "inl[:%s]", c->name);

  val_t* ref = mk_val(e, VK_IMM);

  if (ref)
    ref->text = arena_dup(e->arena, gosub_name, strlen(gosub_name));

  if (e->aborted)
    return NULL;

  /* The `gosub` belongs to the call itself, so it is emitted in both positions. Used as
   * an expression, the return values are then popped off the stack, which makes the
   * emitted order push / gosub / pop. Skipping it here dropped the `gosub` line from
   * every expression-position user call. */
  {
    val_t* const op[1] = {ref};
    put_op(e, "gosub", op, 1);
  }

  if (!in_expr)
    return NULL;

  /* In expression position the return values are popped out of the stack. An unknown
   * function has no recorded arity here; the reference dereferences its missing
   * definition and aborts the file. Emitting one slot keeps the tool running, which is
   * the deliberate p10-class deviation from the reference (Ruling R1). */
  const int want = def ? def->returns : 1;

  if (want <= 0)
  {
    diag_add(e->diag, c->line, "ASM ops and engine calls cannot be used in expressions");
    return mk_undefined(e);
  }

  val_t** items = NULL;

  if (want > 1)
  {
    items = (val_t**)arena_alloc(e->arena, sizeof(val_t*) * (size_t)want);
    if (!items)
      return NULL;
  }

  for (int i = want - 1; i >= 0; i--)
  {
    val_t* slot = alloc_slot(e);

    val_t* const pop[1] = {slot};
    put_op(e, "pop", pop, 1);

    if (want > 1)
      items[i] = slot;
    else
      return slot;
  }

  val_t* list = mk_val(e, VK_LIST);

  if (list)
  {
    list->items = items;
    list->n = want;
  }

  return list;
}

/** Lower a binary or unary operator. */
static val_t* emit_binary(emit_t* e, const node_t* n)
{
  /* The result slot is allocated before the operands are lowered. Reversing the two
   * shifts every later `_expr_N` number in the file. */
  val_t* out = alloc_slot(e);

  if (!out)
    return NULL;

  const bool unary = (n->bin >= UNY_NOT);

  if (unary)
  {
    val_t* src = emit_expr(e, n->kids.v[0]);

    if (!src)
      return NULL;

    if (n->bin == UNY_NOT)
    {
      val_t* const p[4] = {mk_imm(e, "=="), src, mk_imm(e, "0"), out};
      put_op(e, "cmp", p, 4);
      return out;
    }

    {
      val_t* const p2[2] = {src, out};
      put_op(e, "mov", p2, 2);

      if (n->bin == UNY_SUB)
      {
        val_t* const p1[1] = {out};
        put_op(e, "neg", p1, 1);
      }
      else if (n->bin == UNY_NOTA)
      {
        val_t* const p1[1] = {out};
        put_op(e, "not", p1, 1);
      }

      return out;
    }
  }

  val_t* lhs = emit_expr(e, n->kids.v[0]);
  val_t* rhs = emit_expr(e, n->kids.v[1]);

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
      val_t* const p2[2] = {lhs, out};
      put_op(e, "mov", p2, 2);

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

      val_t* const p3[2] = {rhs, out};
      put_op(e, op, p3, 2);
      return out;
    }

    case BIN_GT:
    {
      val_t* const p[4] = {mk_imm(e, ">"), lhs, rhs, out};
      put_op(e, "cmp", p, 4);
      return out;
    }

    case BIN_LT:
    {
      val_t* const p[4] = {mk_imm(e, "<"), lhs, rhs, out};
      put_op(e, "cmp", p, 4);
      return out;
    }

    case BIN_GTE:
    {
      val_t* const p[4] = {mk_imm(e, ">="), lhs, rhs, out};
      put_op(e, "cmp", p, 4);
      return out;
    }

    case BIN_LTE:
    {
      val_t* const p[4] = {mk_imm(e, "<="), lhs, rhs, out};
      put_op(e, "cmp", p, 4);
      return out;
    }

    case BIN_EQ:
    {
      val_t* const p[4] = {mk_imm(e, "=="), lhs, rhs, out};
      put_op(e, "cmp", p, 4);
      return out;
    }

    case BIN_NEQ:
    {
      val_t* const p[4] = {mk_imm(e, "!="), lhs, rhs, out};
      put_op(e, "cmp", p, 4);
      return out;
    }

    case BIN_ANDC:
    {
      val_t* const p[4] = {mk_imm(e, "&&"), lhs, rhs, out};
      put_op(e, "cmp", p, 4);
      return out;
    }

    case BIN_ORC:
    {
      val_t* const p[4] = {mk_imm(e, "||"), lhs, rhs, out};
      put_op(e, "cmp", p, 4);
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

static val_t* emit_expr(emit_t* e, const node_t* n)
{
  if (!n)
    return NULL;

  switch (n->kind)
  {
    case E_NUM:
      return mk_imm_str(e, n->text);
    case E_STR:
      return mk_imm(e, "INVALID");
    case E_VAR:
      return emit_var(e, n);

    case E_ARRAY:
    {
      val_t* base = emit_var(e, n);

      if (!base)
        return NULL;

      val_t* idx = to_slot(e, emit_expr(e, n->kids.v[0]));

      return mk_binary_form(e, VK_ARRAY, base, idx);
    }

    case E_STOR:
    {
      if (n->kids.n == 1)
        return mk_binary_form(e, VK_STOR1, to_slot(e, emit_expr(e, n->kids.v[0])), NULL);

      return mk_binary_form(e, VK_STOR2, to_slot(e, emit_expr(e, n->kids.v[0])),
                            to_slot(e, emit_expr(e, n->kids.v[1])));
    }

    case E_GAMEVAR:
    {
      if (n->kids.n == 1)
        return mk_binary_form(e, VK_GV1, to_slot(e, emit_expr(e, n->kids.v[0])), NULL);

      return mk_binary_form(e, VK_GV2, to_slot(e, emit_expr(e, n->kids.v[0])), to_slot(e, emit_expr(e, n->kids.v[1])));
    }

    case E_REG:
      /* The register index prints verbatim: no slot is allocated for it. */
      return mk_binary_form(e, VK_REG, emit_expr(e, n->kids.v[0]), NULL);

    case E_UNARY:
    case E_BINARY:
      return emit_binary(e, n);

    case E_CALL:
      return emit_call(e, n, true);

    default:
      break;
  }

  char msg[256];
  snprintf(msg, sizeof(msg), "unimplemented value");
  diag_add(e->diag, n->line, msg);

  return mk_undefined(e);
}

/** Lower the destination of an assignment or postfix op. */
static val_t* emit_assignable(emit_t* e, const node_t* n)
{
  switch (n->kind)
  {
    case E_VAR:
      return emit_var(e, n);
    case E_CALL:
      return NULL; /* a call target is not a value position */

    case E_STOR:
    {
      if (n->kids.n == 1)
        return mk_binary_form(e, VK_STOR1, to_slot(e, emit_expr(e, n->kids.v[0])), NULL);

      return mk_binary_form(e, VK_STOR2, to_slot(e, emit_expr(e, n->kids.v[0])),
                            to_slot(e, emit_expr(e, n->kids.v[1])));
    }

    case E_GAMEVAR:
    {
      if (n->kids.n == 1)
        return mk_binary_form(e, VK_GV1, to_slot(e, emit_expr(e, n->kids.v[0])), NULL);

      return mk_binary_form(e, VK_GV2, to_slot(e, emit_expr(e, n->kids.v[0])), to_slot(e, emit_expr(e, n->kids.v[1])));
    }

    case E_REG:
      return mk_binary_form(e, VK_REG, emit_expr(e, n->kids.v[0]), NULL);

    default:
      break;
  }

  return mk_undefined(e);
}

/* --------------------------------------------------------------- statements ----- */

static void emit_assignment(emit_t* e, const node_t* rhs, val_t** targets, int target_count)
{
  val_t* src = emit_expr(e, rhs);

  if (!src)
    return;

  for (int i = 0; i < target_count; i++)
  {
    val_t* const p[2] = {src, targets[i]};
    put_op(e, "mov", p, 2);
  }
}

static void emit_decl(emit_t* e, const node_t* n)
{
  val_t* targets[EMIT_MAX_PARAMS];
  int count = 0;

  for (int i = 0; i < n->names.n && count < EMIT_MAX_PARAMS; i++)
  {
    const node_t* name_node = n->names.v[i];
    const str_t name = str_of(name_node->name, strlen(name_node->name));

    if (has_var(e, name))
    {
      char msg[256];
      snprintf(msg, sizeof(msg), "variable %.*s already defined in scope", name.len, name.p);
      diag_add(e->diag, n->line, msg);
    }

    targets[count++] = mk_var(e, declare_var(e, name.p ? name.p : "", 1, "0"));
  }

  if (n->init_kind == INIT_EXPR && n->init)
    emit_assignment(e, n->init, targets, count);
  else if (n->init_kind == INIT_ARRAY)
  {
    for (int i = 0; i < count; i++)
    {
      for (int k = 0; k < n->kids.n; k++)
      {
        val_t* src = emit_expr(e, n->kids.v[k]);

        if (!src)
          continue;

        val_t* const p[2] = {src, targets[i]};
        put_op(e, "mov", p, 2);
      }
    }
  }
}

static void emit_return(emit_t* e, const node_t* n)
{
  val_t* values[EMIT_MAX_PARAMS];
  int count = 0;

  for (int i = 0; i < n->kids.n && count < EMIT_MAX_PARAMS; i++)
  {
    val_t* v = emit_expr(e, n->kids.v[i]);

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
    val_t* const p[1] = {values[i]};
    put_op(e, "push", p, 1);
  }

  put_op(e, "return", NULL, 0);
}

static void emit_if(emit_t* e, const node_t* n)
{
  val_t* cond = emit_expr(e, n->kids.v[0]);

  if (!cond)
    return;

  const int target = e->label_index++;
  const int has_else = (n->kids.n >= 3);
  int end = -1;

  if (has_else)
  {
    /* The jump-over label is allocated only after the then-block has been lowered, so
     * a nested `if` inside it claims the lower number. */
    end = -2; /* reserved, numbered below */
  }

  val_t* const jump_to_target[4] = {mk_imm(e, "=="), mk_imm(e, "0"), cond, mk_label_ref(e, target)};
  put_op(e, "jmp_cmp", jump_to_target, 4);

  emit_block(e, n->kids.v[1]);

  if (has_else)
  {
    end = e->label_index++;
    val_t* const skip[1] = {mk_label_ref(e, end)};
    put_op(e, "jmp", skip, 1);
  }

  put_label_line(e, target);

  if (has_else)
  {
    const node_t* alt = n->kids.v[2];

    if (alt->kind == N_IF)
      emit_if(e, alt);
    else
      emit_block(e, alt);

    put_label_line(e, end);
  }
}

static void emit_while(emit_t* e, const node_t* n)
{
  const int start = e->label_index++;
  const int end = e->label_index++;

  if (e->loop_count < (int)(sizeof(e->loops) / sizeof(e->loops[0])))
  {
    e->loops[e->loop_count].start = start;
    e->loops[e->loop_count].end = end;
    e->loop_count++;
  }

  put_label_line(e, start);

  val_t* cond = emit_expr(e, n->kids.v[0]);

  if (!cond)
  {
    e->loop_count--;
    return;
  }

  val_t* const test[4] = {mk_imm(e, "=="), mk_imm(e, "0"), cond, mk_label_ref(e, end)};
  put_op(e, "jmp_cmp", test, 4);

  emit_block(e, n->kids.v[1]);

  val_t* const back[1] = {mk_label_ref(e, start)};
  put_op(e, "jmp", back, 1);

  put_label_line(e, end);

  e->loop_count--;
}

static void emit_do_while(emit_t* e, const node_t* n)
{
  const int start = e->label_index++;
  const int end = e->label_index++;

  if (e->loop_count < (int)(sizeof(e->loops) / sizeof(e->loops[0])))
  {
    e->loops[e->loop_count].start = start;
    e->loops[e->loop_count].end = end;
    e->loop_count++;
  }

  put_label_line(e, start);
  emit_block(e, n->kids.v[0]);

  val_t* cond = emit_expr(e, n->kids.v[1]);

  if (!cond)
  {
    e->loop_count--;
    return;
  }

  /* A do-while loops *while* the condition holds, hence `!=`, and it falls through to
   * its end label rather than jumping back. */
  val_t* const test[4] = {mk_imm(e, "!="), mk_imm(e, "0"), cond, mk_label_ref(e, end)};
  put_op(e, "jmp_cmp", test, 4);

  put_label_line(e, end);

  e->loop_count--;
}

static void emit_stmt(emit_t* e, const node_t* n)
{
  switch (n->kind)
  {
    case N_DECL:
      emit_decl(e, n);
      break;

    case N_RETURN:
      emit_return(e, n);
      break;

    case N_IF:
      emit_if(e, n);
      break;

    case N_WHILE:
      emit_while(e, n);
      break;

    case N_DOWHILE:
      emit_do_while(e, n);
      break;

    case N_CALL:
      (void)emit_call(e, n->kids.v[0], false);
      break;

    case N_BREAK:
    {
      if (e->loop_count == 0)
      {
        diag_add(e->diag, n->line, "break is only valid inside of loop");
        break;
      }

      val_t* const p[1] = {mk_label_ref(e, e->loops[e->loop_count - 1].end)};
      put_op(e, "jmp", p, 1);
      break;
    }

    case N_CONTINUE:
    {
      if (e->loop_count == 0)
      {
        diag_add(e->diag, n->line, "continue is only valid inside of loop");
        break;
      }

      val_t* const p[1] = {mk_label_ref(e, e->loops[e->loop_count - 1].start)};
      put_op(e, "jmp", p, 1);
      break;
    }

    case N_POSTFIX:
    {
      val_t* dest = emit_assignable(e, n->names.v[0]);

      if (!dest)
        break;

      val_t* const p[1] = {dest};
      put_op(e, n->is_unary ? "decr" : "incr", p, 1);
      break;
    }

    case N_AUGASSIGN:
    {
      val_t* dest = emit_assignable(e, n->names.v[0]);

      if (!dest)
        break;

      val_t* rhs = emit_expr(e, n->kids.v[0]);

      if (!rhs)
        break;

      if (n->aug == AUG_ANDC)
      {
        val_t* const p[4] = {mk_imm(e, "&&"), dest, rhs, dest};
        put_op(e, "cmp", p, 4);
      }
      else if (n->aug == AUG_ORC)
      {
        val_t* const p[4] = {mk_imm(e, "||"), dest, rhs, dest};
        put_op(e, "cmp", p, 4);
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

        val_t* const p[2] = {rhs, dest};
        put_op(e, op, p, 2);
      }

      break;
    }

    case N_ASSIGN:
    {
      val_t* targets[EMIT_MAX_PARAMS];
      int count = 0;

      for (int i = 0; i < n->names.n && count < EMIT_MAX_PARAMS; i++)
      {
        val_t* v = emit_assignable(e, n->names.v[i]);

        if (v)
          targets[count++] = v;
      }

      if (n->init_kind == INIT_EXPR && n->init)
        emit_assignment(e, n->init, targets, count);
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
static void emit_function(emit_t* e, const source_t* src, const func_t* fn)
{
  (void)src;

  buf_init(&e->body, e->arena);
  e->var_count = 0;
  /* `_expr_` and `_label_` counters are per translation unit, not per function: the
   * reference hands one FateContext to every file, so a later function continues the
   * numbering the earlier ones started. */
  e->loop_count = 0;
  e->bind_count = e->global_mark;
  e->mark_count = 0;

  if (fn->name.len >= (int)sizeof(e->prefix))
    return;

  memcpy(e->prefix, fn->name.p, (size_t)fn->name.len);
  e->prefix[fn->name.len] = '\0';

  e->fn = func_table_find(&e->table, fn->name);

  push_scope(e);

  for (int i = 0; i < fn->argc; i++)
  {
    /* `Str` is not NUL-terminated and `declareVar` takes a C string, so copy first:
     * passing `.p` straight through made the prologue print run on into the body. */
    char arg_name[128];
    const int len = (fn->args[i].len < 0)                        ? 0
                    : (fn->args[i].len >= (int)sizeof(arg_name)) ? (int)sizeof(arg_name) - 1
                                                                 : fn->args[i].len;

    memcpy(arg_name, fn->args[i].p, (size_t)len);
    arg_name[len] = '\0';

    (void)declare_var(e, arg_name, 1, "0");
  }

  /* The body reads its arguments off the stack, last pushed first. Everything below
   * goes into `e->body`, which is appended after the header; `e->out` must not be
   * touched here or the function gains a stray blank line. */
  for (int i = fn->argc - 1; i >= 0; i--)
  {
    var_decl_t* v = lookup_var(e, fn->args[i]);

    if (!v)
      continue;

    val_t* const p[1] = {mk_var(e, v)};
    put_op(e, "pop", p, 1);
  }

  emit_block(e, fn->body);

  /* Assemble: leading blank line, prologue, header, body. */
  buf_putc(&e->out, '\n');

  const char* previous = NULL;

  for (int i = 0; i < e->var_count; i++)
  {
    const var_decl_t* v = e->vars[i];

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

status_t emit_source(emit_t* e, const source_t* src)
{
  buf_puts(&e->out, "\n");

  for (int i = 0; i < src->item_count; i++)
  {
    const top_item_t* it = &src->items[i];

    switch (it->kind)
    {
      case TOP_INCLUDE:
        buf_puts(&e->out, "#include ");
        buf_puts(&e->out, it->name);
        buf_putc(&e->out, '\n');
        break;

      case TOP_GLOBAL:
      {
        const global_var_t* g = it->globs;

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
          emit_function(e, src, it->fn);

        break;
    }

    if (e->aborted)
      return ST_COMPILE;
  }

  return e->aborted ? ST_COMPILE : ST_OK;
}
