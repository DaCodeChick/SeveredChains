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
#ifndef FATE_FATE_H
#define FATE_FATE_H

#include "ast.h"
#include "util.h"

/* -------------------------------------------------------------- diagnostics ----- */

typedef struct Diag
{
  Buf buf;   /* rendered "LINE: message\n" text, in emission order */
  int count; /* number of diagnostics recorded */
} Diag;

/** Record one diagnostic.
 *  @param line 1-based source line, or 0 for a file-level message. */
void diag_add(Diag* d, int line, const char* message);

/* --------------------------------------------------------------- model: include - */

/** One `#include` directive as written in the source. */
typedef struct Include
{
  const char* raw;  /* exact INCLUDE_FILE token text, trailing space preserved */
  const char* name; /* same text with trailing blanks removed */
  int line;
} Include;

/* ----------------------------------------------------------- model: definition -- */

/** A function definition, or a table row when `body == NULL`. */
typedef struct Func
{
  Str name;
  Str args[16];
  int argc;
  Node* body; /**< owned by the arena; NULL for a table row */
  int line;
} Func;

/** A constant or scratch array declaration. */
typedef struct GlobalVar
{
  const char* name;
  int length;
  const char* const* values; /* owned by the arena, NUL-terminated strings */
} GlobalVar;

/* ------------------------------------------------------------- model: file ------ */

/** The kind of one top-level item, in source order. */
typedef enum TopKind
{
  TOP_INCLUDE,
  TOP_GLOBAL,
  TOP_FUNC
} TopKind;

/** A top-level item in a translation unit. Order matters: emission follows it. */
typedef struct TopItem
{
  TopKind kind;
  const char* name;       /**< include filename, global name, or function name */
  const Func* fn;         /**< TOP_FUNC */
  const GlobalVar* globs; /**< TOP_GLOBAL: the one declaration this item carries */
} TopItem;

/** One translation unit: the main script, or an included file. */
typedef struct Source
{
  const char* displayName; /**< path as it appeared on the command line */

  TopItem* items;
  int itemCount;
  int itemCap;

  /* Function lookup table. Entries are individually allocated and referenced by
   * pointer from TopItem, so growing this table never invalidates those pointers. */
  Func** funcs;
  int funcCount;
  int funcCap;

  Include* includes;
  int includeCount;
  int includeCap;
} Source;

/** Find a function definition by exact name within one translation unit. */
const Func* src_find_func(const Source* src, Str name);

/** Append a top-level item, growing the arena-backed array. NULL on allocation
 *  failure. */
TopItem* src_add_item(Source* src, Arena* a, TopKind kind);

/** Register a function definition (and its index entry) in one translation unit. */
Func* add_function(Source* src, Arena* a, Str name);

/** Append a parameter name to a function. */
bool add_argument(Func* fn, Arena* a, Str name);

/** Append a parsed function body to a translation unit. */
bool add_body(Source* src, Arena* a, const Node* body);

#endif /* FATE_FATE_H */
