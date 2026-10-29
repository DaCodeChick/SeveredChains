#pragma once
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

#include "lexer.h"
#include "util.h"

#include <stdbool.h>

/** Syntax-tree node classes. */
typedef enum node_kind_t
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
} node_kind_t;

/**
 * @brief Operator identity for unary and binary operators.
 *
 * Bitwise AND (`BIN_ANDA`) and logical AND (`BIN_ANDC`) must stay distinct: they lower
 * to different ops (`and` versus `cmp &&`).
 */
typedef enum bin_op_t
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
} bin_op_t;

/** Augmented-assignment operators. */
typedef enum aug_op_t
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
} aug_op_t;

/** What the left-hand side of an assignment/postfix was, after ANTLR's rule choice. */
typedef enum lhs_kind_t
{
  LH_SINGLE = 0, /**< one `assignable` */
  LH_LIST        /**< parenthesized `assignable_list` */
} lhs_kind_t;

/** Initialiser form on the right of `=` in a declaration or assignment. */
typedef enum init_kind_t
{
  INIT_NONE = 0,
  INIT_EXPR, /**< a single expression */
  INIT_ARRAY /**< `[e, e, ...]` */
} init_kind_t;

/** One syntax-tree node. */
typedef struct node_t node_t;

/** Child pointer list. */
typedef struct
{
  node_t** v;
  int n;
  int cap;
} node_vec_t;

struct node_t
{
  node_kind_t kind;

  int line; /**< 1-based source line used for diagnostics. */
  int col;  /**< 0-based column, kept for parity with ANTLR's reported position. */

  bin_op_t bin;
  aug_op_t aug;
  bool is_unary;

  /** Primary identifier: function or call name, array name, declared variable name. */
  const char* name;
  /** Trailing identifier of a scoped call (`SMap::scriptScaleUniform`), or the second
   *  half of `id[a:b]`. NUL-terminated arena string. */
  const char* name2;
  int scope_depth;     /**< Count of `::` seen, so `SMap::x` differs from `x::SMap::y`. */
  bool is_engine_call; /**< At least one `::` was present. */
  bool call_in_expr;   /**< Call appeared in expression position (stricter errors). */

  str_t text; /**< literal text: numeric source form, or string body without quotes */

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
  node_vec_t kids;

  /** N_DECL / N_POSTFIX / N_AUGASSIGN / N_RETURN: declared names, or the assignment
   *  targets. Names are stored as E_VAR nodes so codegen has one path. */
  node_vec_t names;

  lhs_kind_t lhs_kind;
  init_kind_t init_kind;
  node_t* init; /**< declaration initialiser when initKind == INIT_EXPR */
};

/** @brief Append a child to a node's kid list. */
void node_add_child(arena_t* a, node_t* parent, node_t* child);

/** @brief Append a name/target to a node's names list. */
void node_add_name(arena_t* a, node_t* parent, node_t* child);

/** @brief Allocate a zeroed node of `kind` at a source position. */
node_t* node_new(arena_t* a, node_kind_t kind, int line, int col);
