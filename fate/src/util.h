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
typedef enum status_t
{
  ST_OK = 0,  /**< Succeeded. */
  ST_USAGE,   /**< Bad command line; maps to process exit 1. */
  ST_IO,      /**< Input/output failure; maps to process exit 2. */
  ST_COMPILE, /**< Compilation produced diagnostics; maps to process exit 3. */
  ST_OOM,     /**< Allocation failed; maps to process exit 2. */
} status_t;

/** A borrowed, not-necessarily-NUL-terminated slice of a buffer. */
typedef struct str_t
{
  const char* p;
  int len;
} str_t;

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
static inline str_t str_of(const char* p, size_t n)
{
  str_t s = {p, n > 2147483647u ? 2147483647 : (int)n};
  return s;
}

/** Slice over a string literal, length known at compile time. */
#define STR_LITERAL(s) str_of((s), sizeof(s) - 1)

/** Compare a slice against a NUL-terminated literal for exact equality. */
bool str_eq_cstr(str_t s, const char* lit);

/** Case-insensitive counterpart of #str_eq_cstr (ASCII only). */
bool str_ieq_cstr(str_t s, const char* lit);

/** True when the slice ends with the given NUL-terminated suffix. */
bool str_ends_with(str_t s, const char* suffix);

/** @brief True when `path` names an existing directory.
 *
 *  Exists for the command line: an include path the user had to type is an input, so
 *  "the filesystem has no such directory" is an I/O failure (exit 2), not a compile
 *  diagnostic (exit 3). A trailing `/` or `\` is not part of the name for this test,
 *  so `/tmp/inc` and `/tmp/inc/` answer the same way; a symlink to a directory counts
 *  as one. Nothing is cached and nothing is allocated.
 *
 *  @param path NUL-terminated path to test; NULL or "" answers false.
 *  @return True only when the path exists, is readable, and is a directory.
 *  @note Platform: `stat` on POSIX, `_stat` on MSVC; no third-party dependency.
 *  @note Thread: single-threaded, no lock, reads the filesystem on every call.
 */
bool path_is_dir(const char* path);

/** @brief True when `path` names an existing regular file.
 *
 *  Used to decide whether a stale output file is worth removing: a directory, a device
 *  such as `/dev/null`, or a socket is never "the previous run's output", and removing
 *  one of those would be a bug rather than a cleanup. Follows symlinks, so a link to a
 *  regular file counts as one.
 *
 *  @param path NUL-terminated path to test; NULL or "" answers false.
 *  @return True only when the path exists and is a regular file.
 *  @note Platform: `stat` on POSIX, `_stat` on MSVC; no third-party dependency.
 */
bool path_is_regular_file(const char* path);

/** One block in the arena's chunk list. */
typedef struct arena_chunk_t
{
  struct arena_chunk_t* next;
  size_t used;
  size_t cap;
  char* data;
} arena_chunk_t;

/** Bump allocator; `arena_reset` releases everything at once. */
typedef struct arena_t
{
  arena_chunk_t* head;
  size_t total;
  bool failed;
} arena_t;

/** @brief Allocate `n` zeroed bytes from `a`.
 *  @return Pointer, or NULL when the allocation failed (also latches `a->failed`). */
void* arena_alloc(arena_t* a, size_t n);

/** @brief Copy `n` bytes from `p` into the arena and NUL-terminate the copy. */
char* arena_dup(arena_t* a, const char* p, size_t n);

/** @brief Release every chunk owned by `a` and reset it to empty. */
void arena_reset(arena_t* a);

/** A byte buffer that grows through the arena (never realloc, to keep it simple). */
typedef struct buf_t
{
  arena_t* arena;
  char* data;
  size_t len;
  size_t cap;
  bool failed;
} buf_t;

/** @brief Create an empty buffer drawing its storage from `a`. */
void buf_init(buf_t* b, arena_t* a);

/** @brief Append `n` bytes from `p` (a NULL `p` or zero `n` is a no-op). */
void buf_put(buf_t* b, const char* p, size_t n);

/** @brief Append a NUL-terminated literal. */
void buf_puts(buf_t* b, const char* s);

/** @brief Append one byte. */
void buf_putc(buf_t* b, char c);

/** @brief Append a slice. */
void buf_put_str(buf_t* b, str_t s);

/** @brief Append a formatted string (snprintf-based, bounded). */
void buf_printf(buf_t* b, const char* fmt, ...);

/** ASCII lower-case a slice into `dst`, truncating safely. */
void buf_put_lower(buf_t* b, str_t s);

#endif /* FATE_UTIL_H */