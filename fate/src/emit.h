/**
 * @file emit.h
 * @brief Code generation: Fate Script syntax tree to assembly text.
 *
 * Two passes mirror the reference compiler's two phases, and the split is observable
 * rather than cosmetic — the reference populates its function table for *all* loaded
 * files before emitting anything, so a call's arity diagnostic depends on definitions
 * that appear later in the file or in an include.
 *
 *   pass A (`emit_scan`)   collects function definitions and reports the two
 *                          definition-level diagnostics;
 *  pass B (`emit_source`)  walks the syntax tree and renders op text.
 *
 * @note Thread: single-threaded; all state lives in the caller-owned arena.
 */
#ifndef FATE_EMIT_H
#define FATE_EMIT_H

#include "ast.h"
#include "fate.h"
#include "meta.h"

#include <stdbool.h>

/** Arity up to five values (4 args + an implicit target, or a target plus 4 values). */
#define EMIT_MAX_PARAMS 5

/** One collected function definition, keyed by its bare name. */
typedef struct func_def_t
{
  str_t name;
  str_t params[16];
  int param_count;
  int returns;    /**< value count of the last `return` in visit order */
  int unit_index; /**< index into `emit_prefix` / the parsed-unit list */
  int line;
} func_def_t;

/** Function definitions gathered across the main file and its includes. */
typedef struct func_table_t
{
  func_def_t* items;
  int count;
  int cap;
} func_table_t;

/** @brief Look up a definition by exact name.
 *  @return NULL when the name is not a known function. */
const func_def_t* func_table_find(const func_table_t* t, str_t name);

/** @brief Register (or overwrite) a definition. Last registration for a name wins,
 *  which matches the reference's `Map.put` after each preprocessed file. */
func_def_t* func_table_add(func_table_t* t, arena_t* arena, str_t name, str_t params[], int param_count, int returns,
                           int unit_index, int line);

/** Scratch state for one compile. Allocated by the caller; fields set in emit_scan. */
typedef struct emit_t
{
  arena_t* arena;
  const meta_t* meta;
  diag_t* diag;
  func_table_t table;

  /* Prefixes that `updateVariableNames` would have applied. One entry per parsed
   * unit, indexed by `FuncDef.unitIndex`. */
  const char** prefixes;
  int prefix_count;

  /* Per-function emission state, rebuilt by `emitBeginFunction`. */
  const func_def_t* fn; /**< definition of the function being emitted, or NULL */
  char prefix[192];     /**< resolved display prefix for the current function */
  buf_t body;           /**< rendered op lines of the current function */
  buf_t out;            /**< the whole rendered translation unit */

  /** Variable slots of the function being emitted, in allocation order. The prologue
   *  walks this and skips repeats, so a slot used twice prints once. */
  struct var_decl_t* vars[1024];
  int var_count;

  /* Scope chain: flat binding list plus one mark per pushed scope. */
  struct binding_t* binds;
  int bind_count;
  int bind_cap;
  int marks[64];
  int mark_count;

  int expr_index;    /**< next `_expr_N` slot number (per translation unit) */
  int label_index;   /**< next `_label_N` number (per translation unit) */
  int global_mark;   /**< binding count of the global scope, restored per function */
  char scratch[288]; /**< shared staging buffer for NUL-terminated name copies */

  struct loop_info_t
  {
    int start;
    int end;
  } loops[32];

  int loop_count;
  bool aborted; /**< set when a construct cannot be lowered at all (see p10) */
} emit_t;

/** @brief Pass A over one parsed unit: collect definitions, report definition errors.
 *  @param unitIndex Index of `src` in the unit list, used to record its prefix.
 *  @param prefix Function-name prefix for that unit ("" for the main file). */
void emit_scan(emit_t* e, const source_t* src, int unit_index, const char* prefix);

/** @brief Pass B: render @p src into `e->out` (already initialised by the caller). */
status_t emit_source(emit_t* e, const source_t* src);

/** @brief Make a top-level `var` visible to every function.
 *  @param name Variable name, NUL-terminated, used verbatim (no function prefix).
 *  @param length Number of `data` lines its prologue entry occupies.
 *  @param value Initialiser text printed after `data `.
 *  @note Must be called after every `emit_scan` and before `emit_source`. */
void emit_bind_global(emit_t* e, const char* name, int length, const char* value);

#endif /* FATE_EMIT_H */
