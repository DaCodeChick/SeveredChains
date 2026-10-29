/**
 * @file meta.h
 * @brief Engine-call and enum metadata used to resolve `Module::method` calls.
 *
 * The reference loads this bundle from a URL or a local directory
 * (`MetaManager` -> `Meta`). Two lookups matter for compilation:
 *
 *   * an exact, case-sensitive search of the method table for a `Module::method`
 *     name, which decides whether a scoped call emits `call <name>, ...` or is
 *     dropped with an "unknown engine call" diagnostic;
 *   * a scan of every enum value for a bare identifier, which decides whether that
 *     identifier is emitted verbatim as an enum constant instead of resolving as a
 *     variable.
 *
 * The bundle is a required input for `fate compile`; there is no built-in default.
 *
 * @note Thread: read-only after `meta_load`.
 */
#ifndef FATE_META_H
#define FATE_META_H

#include "util.h"

#include <stdbool.h>

/** One engine-call declaration: its qualified name and its parameter count. */
typedef struct meta_method_t
{
  const char* name;
  int param_count;
} meta_method_t;

/** Loaded metadata. All storage belongs to the arena handed to @ref meta_load. */
typedef struct meta_t
{
  arena_t* arena;
  meta_method_t* methods;
  int method_count;
  int method_cap;
  const char** enum_values;
  int enum_value_count;
  int enum_value_cap;
} meta_t;

/** @brief Load `descriptions.csv`, `params.csv`, `enums.csv` and the per-enum files.
 *  @param dir Directory holding the bundle.
 *  @errors ST_IO when the directory or a referenced file is missing or unreadable,
 *  ST_OOM on allocation failure. The reference throws in this situation and writes
 *  no output, so failing here is the matching behaviour. */
status_t meta_load(meta_t* m, arena_t* arena, const char* dir);

/** @brief Find a method by exact qualified name (case-sensitive, as the reference is).
 *  @return NULL when the call is unknown. */
const meta_method_t* meta_find_method(const meta_t* m, const char* name);

/** @brief True when `s` names a value in any loaded enum class. */
bool meta_is_enum_value(const meta_t* m, str_t s);

#endif /* FATE_META_H */