/* model.c — implementation of the shared data model helpers in `fate.h`. */
#include "fate.h"

#include <stdio.h>
#include <string.h>

void diag_add(diag_t* d, int line, const char* message)
{
  if (line > 0)
    buf_printf(d->buf.arena ? &d->buf : NULL, "%d: %s\n", line, message);
  else
    buf_printf(d->buf.arena ? &d->buf : NULL, "%s\n", message);

  d->count++;
}

const func_t* src_find_func(const source_t* src, str_t name)
{
  for (int i = 0; i < src->func_count; i++)
  {
    const func_t* fn = src->funcs[i];

    if (fn->name.len == name.len && memcmp(fn->name.p, name.p, (size_t)name.len) == 0)
      return fn;
  }

  return NULL;
}

top_item_t* src_add_item(source_t* src, arena_t* a, top_kind_t kind)
{
  if (src->item_count == src->item_cap)
  {
    const int new_cap = src->item_cap ? src->item_cap * 2 : 16;
    top_item_t* grown = (top_item_t*)arena_alloc(a, (size_t)new_cap * sizeof(top_item_t));

    if (!grown)
      return NULL;

    if (src->item_count > 0)
      memcpy(grown, src->items, (size_t)src->item_count * sizeof(top_item_t));

    src->items = grown;
    src->item_cap = new_cap;
  }

  top_item_t* it = &src->items[src->item_count++];
  it->kind = kind;
  it->name = NULL;
  it->fn = NULL;
  it->globs = NULL;
  return it;
}

func_t* add_function(source_t* src, arena_t* a, str_t name)
{
  if (src->func_count == src->func_cap)
  {
    const int new_cap = src->func_cap ? src->func_cap * 2 : 16;
    func_t** grown = (func_t**)arena_alloc(a, (size_t)new_cap * sizeof(func_t*));

    if (!grown)
      return NULL;

    if (src->func_count > 0)
      memcpy(grown, src->funcs, (size_t)src->func_count * sizeof(func_t*));

    src->funcs = grown;
    src->func_cap = new_cap;
  }

  /* One allocation per function, so the pointer table can grow without moving any
   * function that a TopItem already points at. */
  func_t* fn = (func_t*)arena_alloc(a, sizeof(func_t));

  if (!fn)
    return NULL;

  fn->name = name;
  fn->argc = 0;
  fn->body = NULL;
  src->funcs[src->func_count++] = fn;

  return fn;
}

bool add_argument(func_t* fn, arena_t* a, str_t name)
{
  (void)a;

  if (fn->argc >= (int)(sizeof fn->args / sizeof fn->args[0]))
    return false;

  fn->args[fn->argc++] = name;
  return true;
}

bool add_body(source_t* src, arena_t* a, const node_t* body)
{
  (void)a;

  top_item_t* it = src_add_item(src, a, TOP_FUNC);

  if (!it)
    return false;

  it->name = body->name;
  it->fn = src_find_func(src, str_of(body->name, strlen(body->name)));
  return it->fn != NULL;
}
