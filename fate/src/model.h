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
typedef struct diag_t
{
  arena_t* arena;
  buf_t* buf; /**< owned by the caller; diagnostics are appended here */
  int count;
  bool truncated;
} diag_t;

/** @brief Record a diagnostic as `<line>: <message>`, matching the reference's shape. */
void diag_add(diag_t* d, int line, const char* fmt, ...);

/** @brief True when any diagnostic has been recorded. */
bool diag_failed(const diag_t* d);

/** One `#include` directive found in a source file. */
typedef struct include_t
{
  const char* raw;  /**< verbatim token text, trailing space preserved */
  const char* name; /**< trimmed filename used for lookup */
  int line;
} include_t;

/** A declared function or ASM-op signature. */
typedef struct func_t
{
  const char* name;
  int arity;        /**< declared parameter count; -1 for an ASM op */
  int return_count; /**< declared return count; -1 when unknown */
  int first_line;
  bool is_asm_op;
  const void* asm_op; /**< @ref OpDesc when isAsmOp */
} func_t;

/** Look up or create a function entry. Returns NULL only on allocation failure. */
func_t* add_function(void* owner, arena_t* a, str_t name);

/** Register a parameter on a function (used for the prologue and arity checks). */
void add_argument(func_t* f, arena_t* a, str_t name);

/** Register a parsed top-level body. */
void add_body(void* owner, node_t* body);

/** A declared variable, in the order of declaration within its function. */
typedef struct var_decl_t
{
  const char* name;  /**< fully-qualified display name (function prefix applied) */
  const char* key;   /**< scope lookup key: plain name, or `ns:name` */
  int length;        /**< number of `data` slots to emit */
  const char* value; /**< prologue value text, normally "0" */
  bool is_rel;
} var_decl_t;

/** One emitted op, before rendering. */
typedef struct op_t
{
  const char* name;    /**< op mnemonic, e.g. "mov" */
  const char* prefix;  /**< text emitted between mnemonic and operands, or NULL */
  const char** params; /**< already-rendered operand strings */
  int param_count;
  int line; /**< source line, for --dump-ops */
} op_t;

/** @brief Append an op to the op list.
 *  @param prefix text emitted directly after the mnemonic (used by `cmp`, which emits
 *         `cmp <kind>, ...`); pass NULL for the ordinary single-space separator. */
void emit_op(buf_t* ops, arena_t* a, int line, const char* name, const char* prefix, const char** params,
             int param_count);

/** A lexical scope. Variable lookup searches the chain innermost-first. */
typedef struct scope_t
{
  struct scope_t* parent;
  var_decl_t* vars;
  int count;
  int cap;
} scope_t;

#endif /* FATE_MODEL_H */