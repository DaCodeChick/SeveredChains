/* meta.c — load the metadata bundle that `compile` needs.
 *
 * The bundle is a directory of CSVs:
 *
 *   params.csv   one row per *parameter*: "Method","in"/"out","type","name",
 *                "comment","enumClass". A method's arity is therefore the number of
 *                consecutive rows sharing its name, not a column.
 *   enums.csv    one quoted class name per line, e.g.
 *                "legend.game.combat.types.BattleSoundUsage".
 *   <Class>.csv  the values of one enum class; column 1 holds the value name.
 *
 * A per-class file named in enums.csv but missing from disk is skipped: the reference
 * behaves the same way and the observable output does not change for the golden corpus.
 *
 * Method lookup is case-sensitive and exact. That matters: the emitted engine-call name
 * is the looked-up name, so a case-insensitive match would invent an entry the reference
 * would have rejected. */

#include "meta.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define MAX_FIELDS 8
#define MAX_METHODS 4096
#define MAX_ENUM_VALUES 8192

static char* read_file(arena_t* a, const char* path, size_t* out_len)
{
  FILE* f = fopen(path, "rb");

  if (!f)
    return NULL;

  if (fseek(f, 0, SEEK_END) != 0)
  {
    fclose(f);
    return NULL;
  }

  const long size = ftell(f);

  if (size < 0)
  {
    fclose(f);
    return NULL;
  }

  rewind(f);

  char* buf = (char*)arena_alloc(a, (size_t)size + 1u);

  if (!buf)
  {
    fclose(f);
    return NULL;
  }

  const size_t got = fread(buf, 1, (size_t)size, f);

  fclose(f);
  buf[got] = '\0';
  *out_len = got;

  return buf;
}

/** Split one CSV line on commas, honouring double-quoted fields. */
static int split_csv(str_t line, str_t* fields, int max)
{
  int n = 0;
  int i = 0;

  while (i < line.len && n < max)
  {
    const char* start = line.p + i;
    int len = 0;

    if (line.p[i] == '"')
    {
      i++;
      start = line.p + i;

      while (i < line.len && line.p[i] != '"')
      {
        i++;
        len++;
      }

      if (i < line.len)
        i++; /* closing quote */
    }
    else
    {
      while (i < line.len && line.p[i] != ',')
      {
        i++;
        len++;
      }
    }

    fields[n++] = str_of(start, (size_t)len);

    /* Step over the separator that ended the field, if there was one. */
    if (i < line.len && line.p[i] == ',')
      i++;
  }

  return n;
}

/** Find the slot for a method, creating it when new. */
static meta_method_t* method_slot(meta_t* m, str_t name)
{
  for (int i = 0; i < m->method_count; i++)
    if ((int)strlen(m->methods[i].name) == name.len && memcmp(m->methods[i].name, name.p, (size_t)name.len) == 0)
      return &m->methods[i];

  if (m->method_count == m->method_cap)
  {
    const int new_cap = m->method_cap ? m->method_cap * 2 : 1024;
    meta_method_t* grown = (meta_method_t*)arena_alloc(m->arena, (size_t)new_cap * sizeof(meta_method_t));

    if (!grown)
      return NULL;

    if (m->method_count > 0)
      memcpy(grown, m->methods, (size_t)m->method_count * sizeof(meta_method_t));

    m->methods = grown;
    m->method_cap = new_cap;
  }

  if (m->method_count >= MAX_METHODS)
    return NULL;

  meta_method_t* slot = &m->methods[m->method_count++];

  slot->name = arena_dup(m->arena, name.p, (size_t)name.len);
  slot->param_count = 0;

  return slot;
}

static void load_params(meta_t* m, const char* dir)
{
  char path[1024];

  snprintf(path, sizeof(path), "%s/params.csv", dir);

  size_t len = 0;
  char* text = read_file(m->arena, path, &len);

  if (!text)
    return;

  const char* line = text;

  while (*line != '\0')
  {
    const char* end = strchr(line, '\n');

    if (!end)
      end = line + strlen(line);

    str_t fields[MAX_FIELDS];

    /* Rows shorter than method+direction+type+name are not parameter rows. */
    if (split_csv(str_of(line, (size_t)(end - line)), fields, MAX_FIELDS) >= 4)
    {
      meta_method_t* slot = method_slot(m, fields[0]);

      if (slot)
        slot->param_count++;
    }

    if (*end == '\0')
      break;

    line = end + 1;
  }
}

static void add_enum_value(meta_t* m, str_t value)
{
  if (value.len <= 0 || value.len >= 128)
    return;

  if (m->enum_value_count == m->enum_value_cap)
  {
    const int new_cap = m->enum_value_cap ? m->enum_value_cap * 2 : 1024;
    const char** grown = (const char**)arena_alloc(m->arena, (size_t)new_cap * sizeof(char*));

    if (!grown)
      return;

    if (m->enum_value_count > 0)
      memcpy(grown, m->enum_values, (size_t)m->enum_value_count * sizeof(char*));

    m->enum_values = grown;
    m->enum_value_cap = new_cap;
  }

  if (m->enum_value_count >= MAX_ENUM_VALUES)
    return;

  m->enum_values[m->enum_value_count++] = arena_dup(m->arena, value.p, (size_t)value.len);
}

static void load_enum_class(meta_t* m, const char* dir, str_t class_name)
{
  char path[1024];
  char file[256];

  snprintf(file, sizeof(file), "%.*s.csv", class_name.len, class_name.p);
  snprintf(path, sizeof(path), "%s/%s", dir, file);

  (void)path;

  size_t len = 0;
  char* text = read_file(m->arena, path, &len);

  if (!text)
    return;

  const char* line = text;

  while (*line != '\0')
  {
    const char* end = strchr(line, '\n');

    if (!end)
      end = line + strlen(line);

    str_t fields[MAX_FIELDS];

    if (split_csv(str_of(line, (size_t)(end - line)), fields, MAX_FIELDS) >= 1)
      add_enum_value(m, fields[0]);

    if (*end == '\0')
      break;

    line = end + 1;
  }
}

static void load_enums(meta_t* m, const char* dir)
{
  char path[1024];

  snprintf(path, sizeof(path), "%s/enums.csv", dir);

  size_t len = 0;
  char* text = read_file(m->arena, path, &len);

  if (!text)
    return;

  const char* line = text;

  while (*line != '\0')
  {
    const char* end = strchr(line, '\n');

    if (!end)
      end = line + strlen(line);

    str_t fields[MAX_FIELDS];

    if (split_csv(str_of(line, (size_t)(end - line)), fields, MAX_FIELDS) >= 1 && fields[0].len > 0)
      load_enum_class(m, dir, fields[0]);

    if (*end == '\0')
      break;

    line = end + 1;
  }
}

status_t meta_load(meta_t* m, arena_t* arena, const char* dir)
{
  memset(m, 0, sizeof(*m));

  m->arena = arena;

  /* A missing params.csv is a hard failure: every engine-call arity check and every
   * emitted call name depends on it, so compiling without it would invent output. */
  char params[1024];

  snprintf(params, sizeof(params), "%s/params.csv", dir);

  size_t len = 0;

  if (!read_file(arena, params, &len))
    return ST_IO;

  load_params(m, dir);
  load_enums(m, dir);

  return ST_OK;
}

const meta_method_t* meta_find_method(const meta_t* m, const char* name)
{
  for (int i = 0; i < m->method_count; i++)
    if (strcmp(m->methods[i].name, name) == 0)
      return &m->methods[i];

  return NULL;
}

bool meta_is_enum_value(const meta_t* m, str_t value)
{
  for (int i = 0; i < m->enum_value_count; i++)
    if ((int)strlen(m->enum_values[i]) == value.len && memcmp(m->enum_values[i], value.p, (size_t)value.len) == 0)
      return true;

  return false;
}
