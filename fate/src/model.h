/**
 * @file model.h
 * @brief Shared data model for the Fate compiler: diagnostics, symbols, scopes, ops.
 *
 * Everything here is arena-owned and single-threaded. The shapes exist to mirror the
 * reference compiler's two passes (preprocessor collects definitions, visitor emits
 * ops) so that behaviour differences stay visible rather than being smeared across
 * stages.
 *
 * @note Thread: single-threaded.
 */
#ifndef FATE_MODEL_H
#define FATE_MODEL_H

#include "ast.h"
#include "util.h"

#include <stdbool.h>

/** Maximum nesting depth accepted before the compiler gives up. Guards against a
 *  deeply nested input turning a stack overflow into a crash. */
#define FATE_MAX_DEPTH 64

/** One compilation diagnostic. */
typedef struct Diag
{
  Arena* arena;
  Buf* buf; /**< owned by the caller; diagnostics are appended here */
  int count;
  bool truncated;
} Diag;

/** @brief Record a diagnostic as `<line>: <message>`, matching the reference's shape. */
void diag_add(Diag* d, int line, const char* fmt, ...);

/** @brief True when any diagnostic has been recorded. */
bool diag_failed(const Diag* d);

/** One `#include` directive found in a source file. */
typedef struct Include
{
  const char* raw;  /**< verbatim token text, trailing space preserved */
  const char* name; /**< trimmed filename used for lookup */
  int line;
} Include;

/** A declared function or ASM-op signature. */
typedef struct Func
{
  const char* name;
  int arity;       /**< declared parameter count; -1 for an ASM op */
  int returnCount; /**< declared return count; -1 when unknown */
  int firstLine;
  bool isAsmOp;
  const void* asmOp; /**< @ref OpDesc when isAsmOp */
} Func;

/** Look up or create a function entry. Returns NULL only on allocation failure. */
Func* add_function(void* owner, Arena* a, Str name);

/** Register a parameter on a function (used for the prologue and arity checks). */
void add_argument(Func* f, Arena* a, Str name);

/** Register a parsed top-level body. */
void add_body(void* owner, Node* body);

/** A declared variable, in the order of declaration within its function. */
typedef struct VarDecl
{
  const char* name;  /**< fully-qualified display name (function prefix applied) */
  const char* key;   /**< scope lookup key: plain name, or `ns:name` */
  int length;        /**< number of `data` slots to emit */
  const char* value; /**< prologue value text, normally "0" */
  bool isRel;
} VarDecl;

/** One emitted op, before rendering. */
typedef struct Op
{
  const char* name;    /**< op mnemonic, e.g. "mov" */
  const char* prefix;  /**< text emitted between mnemonic and operands, or NULL */
  const char** params; /**< already-rendered operand strings */
  int paramCount;
  int line; /**< source line, for --dump-ops */
} Op;

/** @brief Append an op to the op list.
 *  @param prefix text emitted directly after the mnemonic (used by `cmp`, which emits
 *         `cmp <kind>, ...`); pass NULL for the ordinary single-space separator. */
void emit_op(Buf* ops, Arena* a, int line, const char* name, const char* prefix, const char** params, int paramCount);

/** A lexical scope. Variable lookup searches the chain innermost-first. */
typedef struct Scope
{
  struct Scope* parent;
  VarDecl* vars;
  int count;
  int cap;
} Scope;

#endif /* FATE_MODEL_H */