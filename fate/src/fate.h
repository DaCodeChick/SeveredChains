#pragma once
/* fate.h — shared data model for the Fate toolchain.

   Two vocabulary classes live here:

   * The **parse model** (`Node`, `Func`, `Source`) — the shape the parser produces
     and the code generator consumes. A `Source` is one translation unit: a script
     file plus the function definitions recovered from it and from its includes.

   * The **diagnostic sink** (`Diag`) — the equivalent of the reference's `errors`
     list, which `Shell` prints to stderr. Keeping it separate from the output
     buffer lets the CLI reproduce the reference's stream split exactly.

   Thread safety: nothing here is thread-safe; one translation unit is compiled at
   a time on the calling thread. */

#include "ast.h"
#include "util.h"

/* -------------------------------------------------------------- diagnostics ----- */

typedef struct diag_t
{
  buf_t buf; /* rendered "LINE: message\n" text, in emission order */
  int count; /* number of diagnostics recorded */
} diag_t;

/** Record one diagnostic.
 *  @param line 1-based source line, or 0 for a file-level message. */
void diag_add(diag_t* d, int line, const char* message);

/* --------------------------------------------------------------- model: include - */

/** One `#include` directive as written in the source. */
typedef struct include_t
{
  const char* raw;  /* exact INCLUDE_FILE token text, trailing space preserved */
  const char* name; /* same text with trailing blanks removed */
  int line;
} include_t;

/* ----------------------------------------------------------- model: definition -- */

/** A function definition, or a table row when `body == NULL`. */
typedef struct func_t
{
  str_t name;
  str_t args[16];
  int argc;
  node_t* body; /**< owned by the arena; NULL for a table row */
  int line;
} func_t;

/** A constant or scratch array declaration. */
typedef struct global_var_t
{
  const char* name;
  int length;
  const char* const* values; /* owned by the arena, NUL-terminated strings */
} global_var_t;

/* ------------------------------------------------------------- model: file ------ */

/** The kind of one top-level item, in source order. */
typedef enum top_kind_t
{
  TOP_INCLUDE,
  TOP_GLOBAL,
  TOP_FUNC
} top_kind_t;

/** A top-level item in a translation unit. Order matters: emission follows it. */
typedef struct top_item_t
{
  top_kind_t kind;
  const char* name;          /**< include filename, global name, or function name */
  const func_t* fn;          /**< TOP_FUNC */
  const global_var_t* globs; /**< TOP_GLOBAL: the one declaration this item carries */
} top_item_t;

/** One translation unit: the main script, or an included file. */
typedef struct source_t
{
  const char* display_name; /**< path as it appeared on the command line */

  top_item_t* items;
  int item_count;
  int item_cap;

  /* Function lookup table. Entries are individually allocated and referenced by
   * pointer from TopItem, so growing this table never invalidates those pointers. */
  func_t** funcs;
  int func_count;
  int func_cap;

  include_t* includes;
  int include_count;
  int include_cap;
} source_t;

/** Find a function definition by exact name within one translation unit. */
const func_t* src_find_func(const source_t* src, str_t name);

/** Append a top-level item, growing the arena-backed array. NULL on allocation
 *  failure. */
top_item_t* src_add_item(source_t* src, arena_t* a, top_kind_t kind);

/** Register a function definition (and its index entry) in one translation unit. */
func_t* add_function(source_t* src, arena_t* a, str_t name);

/** Append a parameter name to a function. */
bool add_argument(func_t* fn, arena_t* a, str_t name);

/** Append a parsed function body to a translation unit. */
bool add_body(source_t* src, arena_t* a, const node_t* body);
