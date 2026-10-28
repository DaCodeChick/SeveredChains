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
typedef struct FuncDef
{
  Str name;
  Str params[16];
  int paramCount;
  int returns;   /**< value count of the last `return` in visit order */
  int unitIndex; /**< index into `emit_prefix` / the parsed-unit list */
  int line;
} FuncDef;

/** Function definitions gathered across the main file and its includes. */
typedef struct FuncTable
{
  FuncDef* items;
  int count;
  int cap;
} FuncTable;

/** @brief Look up a definition by exact name.
 *  @return NULL when the name is not a known function. */
const FuncDef* funcTableFind(const FuncTable* t, Str name);

/** @brief Register (or overwrite) a definition. Last registration for a name wins,
 *  which matches the reference's `Map.put` after each preprocessed file. */
FuncDef* funcTableAdd(FuncTable* t, Arena* arena, Str name, Str params[], int paramCount, int returns, int unitIndex,
                      int line);

/** Scratch state for one compile. Allocated by the caller; fields set in emit_scan. */
typedef struct Emit
{
  Arena* arena;
  const Meta* meta;
  Diag* diag;
  FuncTable table;

  /* Prefixes that `updateVariableNames` would have applied. One entry per parsed
   * unit, indexed by `FuncDef.unitIndex`. */
  const char** prefixes;
  int prefixCount;

  /* Per-function emission state, rebuilt by `emitBeginFunction`. */
  const FuncDef* fn; /**< definition of the function being emitted, or NULL */
  char prefix[192];  /**< resolved display prefix for the current function */
  Buf body;          /**< rendered op lines of the current function */
  Buf out;           /**< the whole rendered translation unit */

  /** Variable slots of the function being emitted, in allocation order. The prologue
   *  walks this and skips repeats, so a slot used twice prints once. */
  struct VarDecl* vars[1024];
  int varCount;

  /* Scope chain: flat binding list plus one mark per pushed scope. */
  struct Binding* binds;
  int bindCount;
  int bindCap;
  int marks[64];
  int markCount;

  int exprIndex;     /**< next `_expr_N` slot number (per translation unit) */
  int labelIndex;    /**< next `_label_N` number (per translation unit) */
  int globalMark;    /**< binding count of the global scope, restored per function */
  char scratch[288]; /**< shared staging buffer for NUL-terminated name copies */

  struct LoopInfo
  {
    int start;
    int end;
  } loops[32];

  int loopCount;
  bool aborted; /**< set when a construct cannot be lowered at all (see p10) */
} Emit;

/** @brief Pass A over one parsed unit: collect definitions, report definition errors.
 *  @param unitIndex Index of `src` in the unit list, used to record its prefix.
 *  @param prefix Function-name prefix for that unit ("" for the main file). */
void emit_scan(Emit* e, const Source* src, int unitIndex, const char* prefix);

/** @brief Pass B: render @p src into `e->out` (already initialised by the caller). */
Status emit_source(Emit* e, const Source* src);

/** @brief Make a top-level `var` visible to every function.
 *  @param name Variable name, NUL-terminated, used verbatim (no function prefix).
 *  @param length Number of `data` lines its prologue entry occupies.
 *  @param value Initialiser text printed after `data `.
 *  @note Must be called after every `emit_scan` and before `emit_source`. */
void emit_bindGlobal(Emit* e, const char* name, int length, const char* value);

#endif /* FATE_EMIT_H */
