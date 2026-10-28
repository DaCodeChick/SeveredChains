/**
 * @file util.h
 * @brief Arena allocation, growable byte buffer, and string helpers shared by every
 * stage of the `fate` compiler.
 *
 * The whole compile runs out of one arena: the token stream, the parse tree, the op
 * list and every rendered string are bump-allocated and released together when the
 * compile ends. That mirrors the reference implementation's garbage-collected object
 * model and keeps manual `free()` off the hot path.
 *
 * @note Thread: single-threaded. No lock is taken anywhere in this file.
 */
#ifndef FATE_UTIL_H
#define FATE_UTIL_H

#include <stdbool.h>
#include <stddef.h>

/** Completion status shared by every fallible function in the tool. */
typedef enum Status
{
  ST_OK = 0,  /**< Succeeded. */
  ST_USAGE,   /**< Bad command line; maps to process exit 1. */
  ST_IO,      /**< Input/output failure; maps to process exit 2. */
  ST_COMPILE, /**< Compilation produced diagnostics; maps to process exit 3. */
  ST_OOM,     /**< Allocation failed; maps to process exit 2. */
} Status;

/** A borrowed, not-necessarily-NUL-terminated slice of a buffer. */
typedef struct Str
{
  const char* p;
  int len;
} Str;

/** @brief Make a slice from a literal's first `n` bytes.
 *
 * The length narrows to `int` because every consumer prints it through `%.*s`, which
 * takes an `int`. Sources larger than 2 GiB are out of scope for this tool; a slice
 * that would overflow is clamped rather than wrapped, so the failure mode is a
 * truncated token rather than undefined behaviour.
 *
 * @param p Start of the slice; may be NULL for an empty slice.
 * @param n Byte length.
 */
static inline Str str_of(const char* p, size_t n)
{
  Str s = {p, n > 2147483647u ? 2147483647 : (int)n};
  return s;
}

/** Slice over a string literal, length known at compile time. */
#define STR_LITERAL(s) str_of((s), sizeof(s) - 1)

/** Compare a slice against a NUL-terminated literal for exact equality. */
bool str_eq_cstr(Str s, const char* lit);

/** Case-insensitive counterpart of #str_eq_cstr (ASCII only). */
bool str_ieq_cstr(Str s, const char* lit);

/** True when the slice ends with the given NUL-terminated suffix. */
bool str_ends_with(Str s, const char* suffix);

/** One block in the arena's chunk list. */
typedef struct ArenaChunk
{
  struct ArenaChunk* next;
  size_t used;
  size_t cap;
  char* data;
} ArenaChunk;

/** Bump allocator; `arena_reset` releases everything at once. */
typedef struct Arena
{
  ArenaChunk* head;
  size_t total;
  bool failed;
} Arena;

/** @brief Allocate `n` zeroed bytes from `a`.
 *  @return Pointer, or NULL when the allocation failed (also latches `a->failed`). */
void* arena_alloc(Arena* a, size_t n);

/** @brief Copy `n` bytes from `p` into the arena and NUL-terminate the copy. */
char* arena_dup(Arena* a, const char* p, size_t n);

/** @brief Release every chunk owned by `a` and reset it to empty. */
void arena_reset(Arena* a);

/** A byte buffer that grows through the arena (never realloc, to keep it simple). */
typedef struct Buf
{
  Arena* arena;
  char* data;
  size_t len;
  size_t cap;
  bool failed;
} Buf;

/** @brief Create an empty buffer drawing its storage from `a`. */
void buf_init(Buf* b, Arena* a);

/** @brief Append `n` bytes from `p` (a NULL `p` or zero `n` is a no-op). */
void buf_put(Buf* b, const char* p, size_t n);

/** @brief Append a NUL-terminated literal. */
void buf_puts(Buf* b, const char* s);

/** @brief Append one byte. */
void buf_putc(Buf* b, char c);

/** @brief Append a slice. */
void buf_put_str(Buf* b, Str s);

/** @brief Append a formatted string (snprintf-based, bounded). */
void buf_printf(Buf* b, const char* fmt, ...);

/** ASCII lower-case a slice into `dst`, truncating safely. */
void buf_put_lower(Buf* b, Str s);

#endif /* FATE_UTIL_H */