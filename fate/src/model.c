/* model.c — implementation of the shared data model helpers in `fate.h`. */
#include "fate.h"

#include <stdio.h>
#include <string.h>

void diag_add(Diag* d, int line, const char* message)
{
  if (line > 0)
    buf_printf(d->buf.arena ? &d->buf : NULL, "%d: %s\n", line, message);
  else
    buf_printf(d->buf.arena ? &d->buf : NULL, "%s\n", message);

  d->count++;
}

const Func* src_find_func(const Source* src, Str name)
{
  for (int i = 0; i < src->funcCount; i++)
  {
    const Func* fn = src->funcs[i];

    if (fn->name.len == name.len && memcmp(fn->name.p, name.p, (size_t)name.len) == 0)
      return fn;
  }

  return NULL;
}

TopItem* src_add_item(Source* src, Arena* a, TopKind kind)
{
  if (src->itemCount == src->itemCap)
  {
    const int newCap = src->itemCap ? src->itemCap * 2 : 16;
    TopItem* grown = (TopItem*)arena_alloc(a, (size_t)newCap * sizeof(TopItem));

    if (!grown)
      return NULL;

    if (src->itemCount > 0)
      memcpy(grown, src->items, (size_t)src->itemCount * sizeof(TopItem));

    src->items = grown;
    src->itemCap = newCap;
  }

  TopItem* it = &src->items[src->itemCount++];
  it->kind = kind;
  it->name = NULL;
  it->fn = NULL;
  it->globs = NULL;
  return it;
}

Func* add_function(Source* src, Arena* a, Str name)
{
  if (src->funcCount == src->funcCap)
  {
    const int newCap = src->funcCap ? src->funcCap * 2 : 16;
    Func** grown = (Func**)arena_alloc(a, (size_t)newCap * sizeof(Func*));

    if (!grown)
      return NULL;

    if (src->funcCount > 0)
      memcpy(grown, src->funcs, (size_t)src->funcCount * sizeof(Func*));

    src->funcs = grown;
    src->funcCap = newCap;
  }

  /* One allocation per function, so the pointer table can grow without moving any
   * function that a TopItem already points at. */
  Func* fn = (Func*)arena_alloc(a, sizeof(Func));

  if (!fn)
    return NULL;

  fn->name = name;
  fn->argc = 0;
  fn->body = NULL;
  src->funcs[src->funcCount++] = fn;

  return fn;
}

bool add_argument(Func* fn, Arena* a, Str name)
{
  (void)a;

  if (fn->argc >= (int)(sizeof fn->args / sizeof fn->args[0]))
    return false;

  fn->args[fn->argc++] = name;
  return true;
}

bool add_body(Source* src, Arena* a, const Node* body)
{
  (void)a;

  TopItem* it = src_add_item(src, a, TOP_FUNC);

  if (!it)
    return false;

  it->name = body->name;
  it->fn = src_find_func(src, str_of(body->name, strlen(body->name)));
  return it->fn != NULL;
}
