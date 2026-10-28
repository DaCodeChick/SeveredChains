/**
 * @file ast.h
 * @brief Fate Script syntax tree.
 *
 * The shapes here mirror `FateParser.g4` one-for-one rather than being a generalised
 * expression IR, because the reference compiler's output depends on *which* grammar
 * alternative matched, not merely on operator precedence.
 *
 * Precedence (from the `expression` rule, alternatives listed tightest-first, which is
 * how ANTLR orders left-recursive alts):
 *
 *     paren/unary > mult > add > shift > relational > equality > & > ^ > | > && > ||
 *
 * That is **not** C's table: additive binds tighter than shift, and equality binds
 * tighter than bitwise AND. Reusing a C precedence table mis-groups expressions.
 *
 * @note Thread: single-threaded.
 */
#ifndef FATE_AST_H
#define FATE_AST_H

#include "lexer.h"
#include "util.h"

#include <stdbool.h>

/** Syntax-tree node classes. */
typedef enum NodeKind
{
  N_BLOCK = 1, /**< `{ ... }` */
  N_DECL,      /**< `var a;`, `var a = e;`, `var a = [..]`, and list forms */
  N_ASSIGN,    /**< `lhs = rhs`, `(a,b) = (c,d)`, `(a,b) = [..]` */
  N_POSTFIX,   /**< `a++` / `a--` */
  N_AUGASSIGN, /**< `a += b` and friends */
  N_RETURN,    /**< `return;` / `return e;` / `return (a,b);` */
  N_BREAK,
  N_CONTINUE,
  N_IF,      /**< kids = [cond, then-block, else-stmt?] */
  N_WHILE,   /**< kids = [cond, block] */
  N_DOWHILE, /**< kids = [block, cond] */
  N_CALL,    /**< a call used as a statement */

  E_STR,     /**< string literal, quotes removed */
  E_NUM,     /**< numeric literal, text preserved verbatim */
  E_CALL,    /**< a call in expression position */
  E_UNARY,   /**< `!x` `~x` `-x` `+x`; kids = [operand] */
  E_BINARY,  /**< kids = [lhs, rhs] */
  E_VAR,     /**< bare identifier used as a value */
  E_STOR,    /**< `stor[..]`; kids = 1 or 2 index expressions */
  E_GAMEVAR, /**< `var[..]` / `var[..][..]`; kids = 1 or 2 index expressions */
  E_REG,     /**< `reg[..]`; kids = [index] */
  E_ID,      /**< `id[a:b]` */
  E_ARRAY    /**< `name[..]` — a named array subscript */
} NodeKind;

/**
 * @brief Operator identity for unary and binary operators.
 *
 * Bitwise AND (`BIN_ANDA`) and logical AND (`BIN_ANDC`) must stay distinct: they lower
 * to different ops (`and` versus `cmp &&`).
 */
typedef enum BinOp
{
  BIN_NONE = 0,
  BIN_MUL,
  BIN_DIV,
  BIN_MOD,
  BIN_ADD,
  BIN_SUB,
  BIN_SHR,
  BIN_SHL,
  BIN_GT,
  BIN_LT,
  BIN_GTE,
  BIN_LTE,
  BIN_EQ,
  BIN_NEQ,
  BIN_ANDA,
  BIN_ORA,
  BIN_XORA,
  BIN_ANDC,
  BIN_ORC,
  UNY_NOT,  /**< `!` */
  UNY_NOTA, /**< `~` */
  UNY_SUB,  /**< `-` */
  UNY_ADD   /**< `+` */
} BinOp;

/** Augmented-assignment operators. */
typedef enum AugOp
{
  AUG_NONE = 0,
  AUG_MUL,
  AUG_DIV,
  AUG_MOD,
  AUG_ADD,
  AUG_SUB,
  AUG_SHR,
  AUG_SHL,
  AUG_ANDC,
  AUG_ORC,
  AUG_ANDA,
  AUG_ORA,
  AUG_XORA,
  AUG_NOTA
} AugOp;

/** What the left-hand side of an assignment/postfix was, after ANTLR's rule choice. */
typedef enum LhsKind
{
  LH_SINGLE = 0, /**< one `assignable` */
  LH_LIST        /**< parenthesized `assignable_list` */
} LhsKind;

/** Initialiser form on the right of `=` in a declaration or assignment. */
typedef enum InitKind
{
  INIT_NONE = 0,
  INIT_EXPR, /**< a single expression */
  INIT_ARRAY /**< `[e, e, ...]` */
} InitKind;

/** One syntax-tree node. */
typedef struct Node Node;

/** Child pointer list. */
typedef struct
{
  Node** v;
  int n;
  int cap;
} NodeVec;

struct Node
{
  NodeKind kind;

  int line; /**< 1-based source line used for diagnostics. */
  int col;  /**< 0-based column, kept for parity with ANTLR's reported position. */

  BinOp bin;
  AugOp aug;
  bool isUnary;

  /** Primary identifier: function or call name, array name, declared variable name. */
  const char* name;
  /** Trailing identifier of a scoped call (`SMap::scriptScaleUniform`), or the second
   *  half of `id[a:b]`. NUL-terminated arena string. */
  const char* name2;
  int scopeDepth;    /**< Count of `::` seen, so `SMap::x` differs from `x::SMap::y`. */
  bool isEngineCall; /**< At least one `::` was present. */
  bool callInExpr;   /**< Call appeared in expression position (stricter errors). */

  Str text; /**< literal text: numeric source form, or string body without quotes */

  /** Statement/child payloads.
   *   N_BLOCK    kids = statements and controls
   *   N_IF       kids = [cond, then-block, else-stmt?]
   *   N_WHILE    kids = [cond, block]
   *   N_DOWHILE  kids = [block, cond]
   *   N_RETURN   kids = returned expressions (0, 1, or a parenthesized list)
   *   E_UNARY    kids = [operand]
   *   E_BINARY   kids = [lhs, rhs]
   *   E_STOR / E_GAMEVAR  kids = index expressions
   *   E_CALL     kids = arguments
   *   E_ARRAY    kids = [index]
   */
  NodeVec kids;

  /** N_DECL / N_POSTFIX / N_AUGASSIGN / N_RETURN: declared names, or the assignment
   *  targets. Names are stored as E_VAR nodes so codegen has one path. */
  NodeVec names;

  LhsKind lhsKind;
  InitKind initKind;
  Node* init; /**< declaration initialiser when initKind == INIT_EXPR */
};

/** @brief Append a child to a node's kid list. */
void nodeAddChild(Arena* a, Node* parent, Node* child);

/** @brief Append a name/target to a node's names list. */
void nodeAddName(Arena* a, Node* parent, Node* child);

/** @brief Allocate a zeroed node of `kind` at a source position. */
Node* nodeNew(Arena* a, NodeKind kind, int line, int col);

#endif /* FATE_AST_H */