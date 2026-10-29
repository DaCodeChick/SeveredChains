/**
 * @file lexer.c
 * @brief Fate Script tokenizer.
 *
 * Matching semantics follow ANTLR's lexer: the longest match wins, and a tie is broken
 * by rule declaration order. That is why keyword lookup happens on the *whole*
 * identifier run rather than by prefix test — `iffy` must stay an identifier, `if`
 * must not. Two lexical gaps are reproduced on purpose (see the testdata manifest,
 * rulings `p8_badhex` and `p9_tab`):
 *
 *   - hex literals only accept lowercase digits, so `0xFF` lexes as `0` + `xFF`;
 *   - whitespace is only space and LF, so a tab is an unrecognized character.
 *
 * The last behaviour is a deliberate deviation from the reference: the reference lets
 * a lexer complaint be printed by ANTLR's default console listener and continues. We
 * record the same text and also continue, and `fate compile` still exits 3 because a
 * non-empty diagnostics list is a compilation failure under the agreed exit contract.
 */
#include "lexer.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ------------------------------------------------------------------ helpers ------ */

/* Character classes below are written as explicit ASCII ranges, not with ctype.h, and
 * that is deliberate. Two reasons, one verified on this host:
 *
 * 1. Scope. The reference's lexer rules are ASCII character classes (`[a-zA-Z_]`,
 *    `[a-zA-Z0-9_]`, `[0-9]`, `[a-f0-9]`). ctype's nearest names are *wider*: `isxdigit`
 *    accepts uppercase hex, which would silently change how `0xFF` lexes and break the
 *    p8 golden (measured: `isxdigit('F') == 1`, the rule below says no).
 * 2. Environment. ctype classification follows the active locale. This tool never calls
 *    `setlocale`, so it runs in the "C" locale where the two forms currently agree for
 *    high bytes — but that agreement is incidental. With a UTF-8 locale loaded,
 *    `isalpha` reports 0xC9/0xE9/0xAA/0xFF as letters (measured on macOS and expected
 *    on glibc), which would make identifier recognition depend on the operator's
 *    environment. A tool that reads arbitrary script bytes must not have that
 *    dependency, and `char` is signed here, so the tempting one-line form is also
 *    outside the domain that C17 7.4.1 defines for these functions.
 *
 * Do not "simplify" these into ctype calls; the width is the behaviour. */

static bool is_ident_start(char c)
{
  return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_';
}

static bool is_ident_char(char c)
{
  return is_ident_start(c) || (c >= '0' && c <= '9');
}

static bool is_digit(char c)
{
  return c >= '0' && c <= '9';
}

/* `[a-f0-9]` exactly, per the NUMBER rule's character class. Uppercase is excluded
 * and that exclusion is observable (`0xFF` lexes as `0` + `xFF`), so do not "fix"
 * this to isxdigit — see the note above. */
static bool is_hex_digit_lower(char c)
{
  return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f');
}

/** Keyword table. Order is irrelevant for lookup; membership is decided by the full
 *  identifier run, which already implements ANTLR's longest-match preference. */
typedef struct kw_entry_t
{
  const char* text;
  tok_kind_t kind;
} kw_entry_t;

static const kw_entry_t KEYWORDS[] = {
  {"entrypoint", TK_ENTRYPOINT},
  {"def", TK_DEF},
  {"var", TK_VAR},
  {"true", TK_TRUE},
  {"false", TK_FALSE},
  {"return", TK_RETURN},
  {"continue", TK_CONTINUE},
  {"break", TK_BREAK},
  {"if", TK_IF},
  {"else", TK_ELSE},
  {"while", TK_WHILE},
  {"do", TK_DO},
  {"stor", TK_STOR},
  {"reg", TK_REG},
};

static tok_kind_t lookup_keyword(str_t s)
{
  const size_t n = sizeof(KEYWORDS) / sizeof(KEYWORDS[0]);

  for (size_t i = 0; i < n; ++i)
    if (str_eq_cstr(s, KEYWORDS[i].text))
      return KEYWORDS[i].kind;

  return TK_IDENTIFIER;
}

/** Operator table, longest-first within a length class so a 3-character operator is
 *  preferred over its 2- and 1-character prefixes, as ANTLR's longest match requires. */
typedef struct op_entry_t
{
  const char* text;
  tok_kind_t kind;
} op_entry_t;

static const op_entry_t OPERATORS[] = {
  /* 3 characters */
  {"&&=", TK_ANDC_ASSIGN},
  {"||=", TK_ORC_ASSIGN},
  {">>=", TK_SHR_ASSIGN},
  {"<<=", TK_SHL_ASSIGN},
  /* 2 characters */
  {"==", TK_EQ},
  {"!=", TK_NEQ},
  {">=", TK_GTE},
  {"<=", TK_LTE},
  {"&&", TK_ANDC},
  {"||", TK_ORC},
  {">>", TK_SHR},
  {"<<", TK_SHL},
  {"*=", TK_MUL_ASSIGN},
  {"/=", TK_DIV_ASSIGN},
  {"%=", TK_MOD_ASSIGN},
  {"+=", TK_ADD_ASSIGN},
  {"-=", TK_SUB_ASSIGN},
  {"&=", TK_ANDA_ASSIGN},
  {"|=", TK_ORA_ASSIGN},
  {"^=", TK_XORA_ASSIGN},
  {"~=", TK_NOTA_ASSIGN},
  {"++", TK_INCR},
  {"--", TK_DECR},
  {"::", TK_SCOPE},
  /* 1 character */
  {">", TK_GT},
  {"<", TK_LT},
  {"!", TK_NOTC},
  {"*", TK_MUL},
  {"/", TK_DIV},
  {"%", TK_MOD},
  {"+", TK_ADD},
  {"-", TK_SUB},
  {"&", TK_ANDA},
  {"|", TK_ORA},
  {"^", TK_XORA},
  {"~", TK_NOTA},
  {"=", TK_ASSIGN},
  {",", TK_COMMA},
  {"(", TK_LPAREN},
  {")", TK_RPAREN},
  {"[", TK_LBRACKET},
  {"]", TK_RBRACKET},
  {"{", TK_OPENER},
  {"}", TK_CLOSER},
  {";", TK_TERM},
  {":", TK_COLON},
};

static tok_kind_t lookup_operator(str_t s)
{
  const size_t n = sizeof(OPERATORS) / sizeof(OPERATORS[0]);

  for (size_t i = 0; i < n; ++i)
    if (str_eq_cstr(s, OPERATORS[i].text))
      return OPERATORS[i].kind;

  return TK_EOF; /* sentinel: no operator matched */
}

/* Longest operator prefix of the remaining source, at most 3 bytes. ANTLR would take
 * the longest viable match; doing the same here keeps `>>=` from becoming `>>` + `=`. */
static tok_kind_t match_operator(str_t rest, int* out_len)
{
  for (int n = 3; n >= 1; --n)
  {
    if (rest.len < n)
      continue;

    const tok_kind_t k = lookup_operator(str_of(rest.p, (size_t)n));

    if (k != TK_EOF)
    {
      *out_len = n;
      return k;
    }
  }

  *out_len = 0;
  return TK_EOF;
}

/* ------------------------------------------------------------------ tracking ---- */

static void push_error(lexer_t* lx, int line, int col, const char* text)
{
  if (lx->error_count == lx->error_cap)
  {
    const int new_cap = lx->error_cap ? lx->error_cap * 2 : 16;
    lexer_error_t* grown = (lexer_error_t*)arena_alloc(lx->arena, (size_t)new_cap * sizeof(lexer_error_t));

    if (!grown)
    {
      lx->arena->failed = true;
      return;
    }

    if (lx->error_count > 0)
      memcpy(grown, lx->errors, (size_t)lx->error_count * sizeof(lexer_error_t));

    lx->errors = grown;
    lx->error_cap = new_cap;
  }

  /* Escape the three characters the reference prints escaped, so the recorded text
   * matches what ANTLR's console listener would have emitted. */
  char buf[16];
  int n = 0;

  if (*text == '\t')
  {
    buf[n++] = '\\';
    buf[n++] = 't';
  }
  else if (*text == '\n')
  {
    buf[n++] = '\\';
    buf[n++] = 'n';
  }
  else if (*text == '\r')
  {
    buf[n++] = '\\';
    buf[n++] = 'r';
  }
  else
  {
    buf[n++] = *text;
  }

  buf[n] = '\0';

  lx->errors[lx->error_count].line = line;
  lx->errors[lx->error_count].col = col;
  lx->errors[lx->error_count].text = str_of(arena_dup(lx->arena, buf, (size_t)n), (size_t)n);
  lx->error_count++;
}

static void push_token(lexer_t* lx, tok_kind_t kind, int line, int col, str_t text)
{
  if (lx->count == lx->cap)
  {
    const int new_cap = lx->cap ? lx->cap * 2 : 256;
    token_t* grown = (token_t*)arena_alloc(lx->arena, (size_t)new_cap * sizeof(token_t));

    if (!grown)
    {
      lx->arena->failed = true;
      return;
    }

    if (lx->count > 0)
      memcpy(grown, lx->tok, (size_t)lx->count * sizeof(token_t));

    lx->tok = grown;
    lx->cap = new_cap;
  }

  lx->tok[lx->count].kind = kind;
  lx->tok[lx->count].line = line;
  lx->tok[lx->count].col = col;
  lx->tok[lx->count].text = text;
  lx->tok[lx->count].error_id = -1;
  lx->count++;
}

/* -------------------------------------------------------------------- strings ---- */

/**
 * @brief Try to match a STRING token at `i`.
 * @return Length in bytes, or 0 when no string match exists (the caller then treats the
 *         quote as an unrecognized character, which is what ANTLR does).
 *
 * The rule is `'"' ( ESC_SEQ | ~('\\'|'"') )* '"'` with
 * `ESC_SEQ = '\\' [n"\\]`. A backslash that is not part of a legal escape cannot be
 * matched by either alternative, so `"a\q"` is *not* a string: the quote becomes a
 * lexer error. Reproducing that matters for the string fixture being added now.
 */
static int match_string(const char* src, int len, int i)
{
  int j = i + 1;

  while (j < len)
  {
    const char c = src[j];

    if (c == '"')
      return j + 1 - i; /* closing quote, inclusive */

    if (c == '\\')
    {
      if (j + 1 < len && (src[j + 1] == 'n' || src[j + 1] == '"' || src[j + 1] == '\\'))
      {
        j += 2;
        continue;
      }

      return 0; /* lone backslash: the whole match dies */
    }

    ++j;
  }

  return 0; /* unterminated: ANTLR consumes to EOF and errors; we degrade the same way */
}

/* ---------------------------------------------------------------------- entry ---- */

status_t lex_run(arena_t* arena, str_t src, lexer_t* lx)
{
  lx->arena = arena;
  lx->count = 0;
  lx->cap = 0;
  lx->error_count = 0;
  lx->error_cap = 0;
  lx->errors = NULL;
  lx->tok = NULL;

  push_token(lx, TK_EOF, 1, 0, str_of("", 0));

  if (src.len <= 0)
    return lx->arena->failed ? ST_OOM : ST_OK;

  const char* s = src.p;
  const int len = src.len;

  int i = 0;
  int line = 1;
  int col = 0;

  /* Track the current position so tokens and errors carry ANTLR-style coordinates. */
/* ADV(n) advances the cursor n characters, updating line/col. The count is bound to a
   local first: `n` is a macro parameter, so writing it inside the loop condition would
   re-evaluate the expression against a cursor that is itself moving, halving every skip. */
#define ADV(n)                                                                                                         \
  do                                                                                                                   \
  {                                                                                                                    \
    const int adv_n_ = (n);                                                                                            \
    for (int adv_ = 0; adv_ < adv_n_; ++adv_)                                                                          \
    {                                                                                                                  \
      if (s[i] == '\n')                                                                                                \
      {                                                                                                                \
        line++;                                                                                                        \
        col = 0;                                                                                                       \
      }                                                                                                                \
      else                                                                                                             \
      {                                                                                                                \
        col++;                                                                                                         \
      }                                                                                                                \
      i++;                                                                                                             \
    }                                                                                                                  \
  } while (0)

  while (i < len)
  {
    const char c = s[i];
    const int start_line = line;
    const int start_col = col;

    /* Whitespace: space and LF only (`WHITESPACE : [ \n]` in the reference lexer).
     * Tab and CR are not whitespace and fall through to the unrecognized-character
     * path below, which is what the reference does with them. */
    if (c == ' ' || c == '\n')
    {
      ADV(1);
      continue;
    }

    /* Line comment: `//` then everything up to (not including) CR or LF. */
    if (c == '/' && i + 1 < len && s[i + 1] == '/')
    {
      int j = i;

      while (j < len && s[j] != '\n' && s[j] != '\r')
        ++j;

      ADV(j - i);
      continue;
    }

    /* Block comment: open-delimiter ... close-delimiter, non-greedy. An unterminated
     * block comment matches nothing, so the leading `/` falls through to the operator
     * rule. (The delimiters are spelled out to keep this comment from closing itself.) */
    if (c == '/' && i + 1 < len && s[i + 1] == '*')
    {
      const char* end = NULL;

      for (int j = i + 2; j + 1 < len; ++j)
      {
        if (s[j] == '*' && s[j + 1] == '/')
        {
          end = s + j;
          break;
        }
      }

      if (end != NULL)
      {
        ADV((int)(end + 2 - (s + i)));
        continue;
      }
    }

    /* `include ` plus the raw filename, consumed as one lexical unit. The space run is
     * part of the INCLUDE token; the filename keeps any trailing space, which is why
     * `include char_utils.fate ;` resolves the file but not its functions. */
    if (is_ident_start(c) && (len - i >= 7) && strncmp(s + i, "include", 7) == 0)
    {
      int j = i + 7;
      int spaces = 0;

      while (j < len && s[j] == ' ')
      {
        ++j;
        ++spaces;
      }

      if (spaces > 0)
      {
        push_token(lx, TK_INCLUDE, start_line, start_col, str_of(s + i, (size_t)(j - i)));
        ADV(j - i);

        int f = j;

        while (f < len && s[f] != ';' && s[f] != '\r' && s[f] != '\n')
          ++f;

        if (f > j)
        {
          push_token(lx, TK_INCLUDE_FILE, line, col, str_of(s + j, (size_t)(f - j)));
          ADV(f - j);
        }

        continue;
      }
    }

    /* Identifier or keyword: take the whole run first, then decide. */
    if (is_ident_start(c))
    {
      int j = i;

      while (j < len && is_ident_char(s[j]))
        ++j;

      const int n = j - i;
      str_t run = str_of(s + i, (size_t)n);
      tok_kind_t k = lookup_keyword(run);

      if (k == TK_IDENTIFIER)
        push_token(lx, TK_IDENTIFIER, start_line, start_col, run);
      else
        push_token(lx, k, start_line, start_col, run);

      ADV(n);
      continue;
    }

    /* Number: `-`? ( decimal | 0x hex-lowercase ). Uppercase hex digits are outside the
     * class, so `0xFF` becomes `0` + identifier `xFF` and compiles to `mov 0, ...`. */
    if (is_digit(c) ||
        (c == '-' && i + 1 < len &&
         (is_digit(s[i + 1]) || (s[i + 1] == '0' && i + 3 < len && s[i + 2] == 'x' && is_hex_digit_lower(s[i + 3])))))
    {
      int j = i;

      if (s[j] == '-')
        ++j;

      const int digits_start = j;

      if (s[j] == '0' && j + 1 < len && s[j + 1] == 'x' && j + 2 < len && is_hex_digit_lower(s[j + 2]))
      {
        j += 2;

        while (j < len && is_hex_digit_lower(s[j]))
          ++j;
      }
      else
      {
        while (j < len && is_digit(s[j]))
          ++j;

        /* A lone `0` followed by `x` and no lowercase digit is still just `0`. */
        if (j == digits_start)
          ++j;
      }

      push_token(lx, TK_NUMBER, start_line, start_col, str_of(s + i, (size_t)(j - i)));
      ADV(j - i);
      continue;
    }

    /* String literal. */
    if (c == '"')
    {
      const int n = match_string(s, len, i);

      if (n > 0)
      {
        push_token(lx, TK_STRING, start_line, start_col, str_of(s + i, (size_t)n));
        ADV(n);
        continue;
      }
    }

    /* Operators, longest match first. */
    {
      int n = 0;
      const tok_kind_t k = match_operator(str_of(s + i, (len - i) < 3 ? (size_t)(len - i) : 3u), &n);

      if (k != TK_EOF && n > 0)
      {
        push_token(lx, k, start_line, start_col, str_of(s + i, (size_t)n));
        ADV(n);
        continue;
      }
    }

    /* Nothing matched. `WHITESPACE : [ \n]` is the only skip rule in the reference
     * lexer, so a tab or a CR is an unrecognized character: ANTLR reports
     * `token recognition error at: '<c>'` and then drops it, and the surrounding
     * statement still parses. Recording the message and consuming the character without
     * emitting a token is what reproduces that, and it is why the tab-indented fixture
     * still compiles cleanly. */
    push_error(lx, line, col, &s[i]);
    ADV(1);
  }

#undef ADV

  /* Terminate the stream with a real EOF token. `parse.c` clamps out-of-range peeks to
   * the last slot, so without this a recovery loop that scans "until EOF" never sees one
   * and spins forever. The index-0 entry pushed above is padding, not this terminator. */
  push_token(lx, TK_EOF, line, col, str_of("", 0));

  return lx->arena->failed ? ST_OOM : ST_OK;
}

const char* tok_kind_name(tok_kind_t k)
{
  switch (k)
  {
    case TK_EOF:
      return "EOF";
    case TK_ENTRYPOINT:
      return "entrypoint";
    case TK_DEF:
      return "def";
    case TK_VAR:
      return "var";
    case TK_TRUE:
      return "true";
    case TK_FALSE:
      return "false";
    case TK_RETURN:
      return "return";
    case TK_CONTINUE:
      return "continue";
    case TK_BREAK:
      return "break";
    case TK_IF:
      return "if";
    case TK_ELSE:
      return "else";
    case TK_WHILE:
      return "while";
    case TK_DO:
      return "do";
    case TK_STOR:
      return "stor";
    case TK_REG:
      return "reg";
    case TK_INCLUDE:
      return "include";
    case TK_INCLUDE_FILE:
      return "include_file";
    case TK_IDENTIFIER:
      return "identifier";
    case TK_NUMBER:
      return "number";
    case TK_STRING:
      return "string";
    case TK_ERROR:
      return "error";
    default:
      return "punct";
  }
}