#pragma once
/* parse.h — recursive-descent parser for Fate Script.

   The parser turns a token vector (from `lexer.h`) into the parse model in
   `fate.h`. It is a hand-written recursive-descent parser rather than a
   generated one, because the reference grammar's error-recovery behaviour is part
   of the observable output and has to be reproduced algorithmically.

   Thread safety: a `Parser` is single-threaded and owns no persistent state
   beyond the arena and the lexer it is handed. */

#include "fate.h"
#include "lexer.h"

/** Parser state. Deliberately opaque: only `parse.c` touches the fields. */
typedef struct parser_t parser_t;

/** Parse one translation unit.
 *  (@param arena  arena owning every node, name and string the parser creates.
 *  @param lx     a lexer whose `tok` vector is already populated by `lex_run`.
 *  @param src    translation unit to fill; `src->displayName` is already set.
 *  @param diag   diagnostic sink for parse-level messages (not emitted for the
 *                golden corpus, which is why it is quiet here).
 *  @return #ST_OK, #ST_COMPILE on a structural failure, or #ST_OOM.
 *  @note Thread: single-threaded. */
status_t parse_translation_unit(arena_t* arena, lexer_t* lx, source_t* src, diag_t* diag);
