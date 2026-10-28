/**
 * @file lexer.h
 * @brief Fate Script tokenizer, deliberately matched to the reference ANTLR lexer.
 *
 * The token classes and their matching rules are those of
 * `src/main/antlr/org/legendofdragoon/scripting/compiler/FateLexer.g4` in the Script
 * Recompiler reference. They are reproduced rather than improved on, because the
 * reference's *lexical* quirks are observable behaviour: `0xFF` does not lex as one
 * number, and a tab is not whitespace. Both are covered by goldens
 * (`p8_badhex`, `p9_tab`).
 *
 * @note Thread: single-threaded.
 */
#ifndef FATE_LEXER_H
#define FATE_LEXER_H

#include "util.h"

#include <stdbool.h>

/**
 * @brief Token classes, in the order the reference grammar declares them.
 *
 * Rule order matters: ANTLR breaks equal-length ties by rule order, so a keyword
 * beats the identifier rule at the same length. Keep this enum's keyword block in
 * the same relative order as FateLexer.g4 if new entries are ever added.
 */
typedef enum TokKind
{
  TK_EOF = 0,

  /* Keywords (FateLexer.g4 rules 1-14). */
  TK_ENTRYPOINT,
  TK_DEF,
  TK_VAR,
  TK_TRUE,
  TK_FALSE,
  TK_RETURN,
  TK_CONTINUE,
  TK_BREAK,
  TK_IF,
  TK_ELSE,
  TK_WHILE,
  TK_DO,

  /* Multi-character operators, longest first within each group. */
  TK_EQ,   /**< `==` */
  TK_NEQ,  /**< `!=` */
  TK_GT,   /**< `>`  */
  TK_LT,   /**< `<`  */
  TK_GTE,  /**< `>=` */
  TK_LTE,  /**< `<=` */
  TK_ANDC, /**< `&&` */
  TK_ORC,  /**< `||` */
  TK_NOTC, /**< `!`  */
  TK_MUL,
  TK_DIV,
  TK_MOD,
  TK_ADD,
  TK_SUB,
  TK_ANDA, /**< `&`  */
  TK_ORA,  /**< `|`  */
  TK_XORA, /**< `^`  */
  TK_NOTA, /**< `~`  */
  TK_SHR,  /**< `>>` */
  TK_SHL,  /**< `<<` */

  TK_ANDC_ASSIGN, /**< `&&=` */
  TK_ORC_ASSIGN,  /**< `||=` */
  TK_MUL_ASSIGN,
  TK_DIV_ASSIGN,
  TK_MOD_ASSIGN,
  TK_ADD_ASSIGN,
  TK_SUB_ASSIGN,
  TK_ANDA_ASSIGN,
  TK_ORA_ASSIGN,
  TK_XORA_ASSIGN,
  TK_NOTA_ASSIGN,
  TK_SHR_ASSIGN,
  TK_SHL_ASSIGN,

  TK_ASSIGN,
  TK_INCR, /**< `++` */
  TK_DECR, /**< `--` */
  TK_COMMA,
  TK_LPAREN,
  TK_RPAREN,
  TK_LBRACKET,
  TK_RBRACKET,
  TK_OPENER, /**< `{` */
  TK_CLOSER, /**< `}` */
  TK_TERM,   /**< `;` */
  TK_SCOPE,  /**< `::` */
  TK_COLON,  /**< `:` */

  TK_INCLUDE,      /**< `include` plus the following run of spaces. */
  TK_INCLUDE_FILE, /**< Raw filename text consumed in INCLUDE_FILE_MODE. */

  TK_STOR,
  TK_REG,
  TK_IDENTIFIER,
  TK_NUMBER,
  TK_STRING,

  TK_ERROR /**< One unrecognized character (see @ref LexerError). */
} TokKind;

/** One token, with the source position the reference would have reported. */
typedef struct Token
{
  TokKind kind;
  int line;     /**< 1-based line, as ANTLR reports it. */
  int col;      /**< 0-based column within the line, as ANTLR reports it. */
  Str text;     /**< Borrowed from the source buffer; includes quotes for TK_STRING. */
  int error_id; /**< Index into Lexer::errors for TK_ERROR, else -1. */
} Token;

/** A lexer-level complaint: `line L:C token recognition error at: '<text>'`. */
typedef struct LexerError
{
  int line; /**< 1-based. */
  int col;  /**< 0-based. */
  Str text; /**< The unrecognized text, escaped for display. */
} LexerError;

/** Token stream plus the diagnostics produced while building it. */
typedef struct Lexer
{
  Arena* arena;
  Token* tok;
  int count;
  int cap;
  LexerError* errors;
  int errorCount;
  int errorCap;
} Lexer;

/** @brief Tokenize `src` into `lx`.
 *  @errors ST_IO if the source could not be read (caller reads it; this never fails
 *  for that reason), ST_OOM if the arena could not serve the token array. A stream
 *  that tokenized with complaints still returns ST_OK: unrecognized characters are
 *  reported through `lx->errors`, not through Status. */
Status lex_run(Arena* arena, Str src, Lexer* lx);

/** @brief Human-readable name of a token class (diagnostics and --dump-ast). */
const char* tok_kind_name(TokKind k);

#endif /* FATE_LEXER_H */