/* parse.h — recursive-descent parser for Fate Script.

   The parser turns a token vector (from `lexer.h`) into the parse model in
   `fate.h`. It is a hand-written recursive-descent parser rather than a
   generated one, because the reference grammar's error-recovery behaviour is part
   of the observable output and has to be reproduced algorithmically.

   Thread safety: a `Parser` is single-threaded and owns no persistent state
   beyond the arena and the lexer it is handed. */
#ifndef FATE_PARSE_H
#define FATE_PARSE_H

#include "fate.h"
#include "lexer.h"

/** Parser state. Deliberately opaque: only `parse.c` touches the fields. */
typedef struct Parser Parser;

/** Parse one translation unit.
 *  (@param arena  arena owning every node, name and string the parser creates.
 *  @param lx     a lexer whose `tok` vector is already populated by `lex_run`.
 *  @param src    translation unit to fill; `src->displayName` is already set.
 *  @param diag   diagnostic sink for parse-level messages (not emitted for the
 *                golden corpus, which is why it is quiet here).
 *  @return #ST_OK, #ST_COMPILE on a structural failure, or #ST_OOM.
 *  @note Thread: single-threaded. */
Status parse_translationUnit(Arena* arena, Lexer* lx, Source* src, Diag* diag);

#endif /* FATE_PARSE_H */
