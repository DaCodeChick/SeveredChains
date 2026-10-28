/* Implementation of the arena/buffer/string helpers declared in util.h. */
#include "util.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define ARENA_CHUNK (256u * 1024u)
#define PRINT_MAX 4096

bool str_eq_cstr(Str s, const char* lit)
{
  const size_t n = strlen(lit);
  if ((size_t)s.len != n)
    return false;
  return memcmp(s.p, lit, n) == 0;
}

bool str_ieq_cstr(Str s, const char* lit)
{
  const size_t n = strlen(lit);
  if ((size_t)s.len != n)
    return false;
  for (size_t i = 0; i < n; i++)
  {
    char a = s.p[i];
    char c = lit[i];
    if (a >= 'A' && a <= 'Z')
      a = (char)(a - 'A' + 'a');
    if (c >= 'A' && c <= 'Z')
      c = (char)(c - 'A' + 'a');
    if (a != c)
      return false;
  }
  return true;
}

bool str_ends_with(Str s, const char* suffix)
{
  const size_t n = strlen(suffix);
  if ((size_t)s.len < n)
    return false;
  return memcmp(s.p + (s.len - (int)n), suffix, n) == 0;
}

void* arena_alloc(Arena* a, size_t n)
{
  if (n == 0)
    n = 1;
  /* Round up so every object starts suitably aligned for any scalar type. */
  const size_t want = (n + 15u) & ~(size_t)15u;

  ArenaChunk* c = a->head;
  if (c == NULL || c->used + want > c->cap)
  {
    size_t cap = want > ARENA_CHUNK ? want : ARENA_CHUNK;
    ArenaChunk* nc = (ArenaChunk*)malloc(sizeof(ArenaChunk));
    if (nc == NULL)
    {
      a->failed = true;
      return NULL;
    }
    nc->data = (char*)malloc(cap);
    if (nc->data == NULL)
    {
      free(nc);
      a->failed = true;
      return NULL;
    }
    nc->cap = cap;
    nc->used = 0;
    nc->next = a->head;
    a->head = nc;
    c = nc;
  }
  void* p = c->data + c->used;
  c->used += want;
  a->total += want;
  memset(p, 0, want);
  return p;
}

char* arena_dup(Arena* a, const char* p, size_t n)
{
  char* d = (char*)arena_alloc(a, n + 1);
  if (d == NULL)
    return NULL;
  if (n > 0)
    memcpy(d, p, n);
  d[n] = '\0';
  return d;
}

void arena_reset(Arena* a)
{
  ArenaChunk* c = a->head;
  while (c != NULL)
  {
    ArenaChunk* next = c->next;
    free(c->data);
    free(c);
    c = next;
  }
  a->head = NULL;
  a->total = 0;
  a->failed = false;
}

static bool buf_reserve(Buf* b, size_t extra)
{
  if (b->failed || b->arena == NULL)
  {
    b->failed = true;
    return false;
  }
  if (b->len + extra <= b->cap)
    return true;

  size_t cap = b->cap == 0 ? 1024 : b->cap;
  while (cap < b->len + extra)
    cap *= 2;

  char* data = (char*)arena_alloc(b->arena, cap);
  if (data == NULL)
  {
    b->failed = true;
    return false;
  }
  if (b->len > 0)
    memcpy(data, b->data, b->len);
  b->data = data;
  b->cap = cap;
  return true;
}

void buf_init(Buf* b, Arena* a)
{
  b->arena = a;
  b->data = NULL;
  b->len = 0;
  b->cap = 0;
  b->failed = false;
}

void buf_put(Buf* b, const char* p, size_t n)
{
  if (p == NULL || n == 0)
    return;
  if (!buf_reserve(b, n + 1))
    return;
  memcpy(b->data + b->len, p, n);
  b->len += n;
  b->data[b->len] = '\0';
}

void buf_puts(Buf* b, const char* s)
{
  buf_put(b, s, strlen(s));
}

void buf_putc(Buf* b, char c)
{
  buf_put(b, &c, 1);
}

void buf_put_str(Buf* b, Str s)
{
  if (s.len > 0)
    buf_put(b, s.p, (size_t)s.len);
}

void buf_printf(Buf* b, const char* fmt, ...)
{
  char tmp[PRINT_MAX];
  va_list ap;
  va_start(ap, fmt);
  const int n = vsnprintf(tmp, sizeof(tmp), fmt, ap);
  va_end(ap);
  if (n < 0)
  {
    b->failed = true;
    return;
  }
  const size_t len = (size_t)n < sizeof(tmp) - 1 ? (size_t)n : sizeof(tmp) - 1;
  buf_put(b, tmp, len);
}

void buf_put_lower(Buf* b, Str s)
{
  for (int i = 0; i < s.len; i++)
  {
    char c = s.p[i];
    if (c >= 'A' && c <= 'Z')
      c = (char)(c - 'A' + 'a');
    buf_putc(b, c);
  }
}