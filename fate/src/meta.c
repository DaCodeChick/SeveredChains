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

static char* readFile(Arena* a, const char* path, size_t* outLen)
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
  *outLen = got;

  return buf;
}

/** Split one CSV line on commas, honouring double-quoted fields. */
static int splitCsv(Str line, Str* fields, int max)
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
static MetaMethod* methodSlot(Meta* m, Str name)
{
  for (int i = 0; i < m->methodCount; i++)
    if ((int)strlen(m->methods[i].name) == name.len && memcmp(m->methods[i].name, name.p, (size_t)name.len) == 0)
      return &m->methods[i];

  if (m->methodCount == m->methodCap)
  {
    const int newCap = m->methodCap ? m->methodCap * 2 : 1024;
    MetaMethod* grown = (MetaMethod*)arena_alloc(m->arena, (size_t)newCap * sizeof(MetaMethod));

    if (!grown)
      return NULL;

    if (m->methodCount > 0)
      memcpy(grown, m->methods, (size_t)m->methodCount * sizeof(MetaMethod));

    m->methods = grown;
    m->methodCap = newCap;
  }

  if (m->methodCount >= MAX_METHODS)
    return NULL;

  MetaMethod* slot = &m->methods[m->methodCount++];

  slot->name = arena_dup(m->arena, name.p, (size_t)name.len);
  slot->paramCount = 0;

  return slot;
}

static void loadParams(Meta* m, const char* dir)
{
  char path[1024];

  snprintf(path, sizeof(path), "%s/params.csv", dir);

  size_t len = 0;
  char* text = readFile(m->arena, path, &len);

  if (!text)
    return;

  const char* line = text;

  while (*line != '\0')
  {
    const char* end = strchr(line, '\n');

    if (!end)
      end = line + strlen(line);

    Str fields[MAX_FIELDS];

    /* Rows shorter than method+direction+type+name are not parameter rows. */
    if (splitCsv(str_of(line, (size_t)(end - line)), fields, MAX_FIELDS) >= 4)
    {
      MetaMethod* slot = methodSlot(m, fields[0]);

      if (slot)
        slot->paramCount++;
    }

    if (*end == '\0')
      break;

    line = end + 1;
  }
}

static void addEnumValue(Meta* m, Str value)
{
  if (value.len <= 0 || value.len >= 128)
    return;

  if (m->enumValueCount == m->enumValueCap)
  {
    const int newCap = m->enumValueCap ? m->enumValueCap * 2 : 1024;
    const char** grown = (const char**)arena_alloc(m->arena, (size_t)newCap * sizeof(char*));

    if (!grown)
      return;

    if (m->enumValueCount > 0)
      memcpy(grown, m->enumValues, (size_t)m->enumValueCount * sizeof(char*));

    m->enumValues = grown;
    m->enumValueCap = newCap;
  }

  if (m->enumValueCount >= MAX_ENUM_VALUES)
    return;

  m->enumValues[m->enumValueCount++] = arena_dup(m->arena, value.p, (size_t)value.len);
}

static void loadEnumClass(Meta* m, const char* dir, Str className)
{
  char path[1024];
  char file[256];

  snprintf(file, sizeof(file), "%.*s.csv", className.len, className.p);
  snprintf(path, sizeof(path), "%s/%s", dir, file);

  (void)path;

  size_t len = 0;
  char* text = readFile(m->arena, path, &len);

  if (!text)
    return;

  const char* line = text;

  while (*line != '\0')
  {
    const char* end = strchr(line, '\n');

    if (!end)
      end = line + strlen(line);

    Str fields[MAX_FIELDS];

    if (splitCsv(str_of(line, (size_t)(end - line)), fields, MAX_FIELDS) >= 1)
      addEnumValue(m, fields[0]);

    if (*end == '\0')
      break;

    line = end + 1;
  }
}

static void loadEnums(Meta* m, const char* dir)
{
  char path[1024];

  snprintf(path, sizeof(path), "%s/enums.csv", dir);

  size_t len = 0;
  char* text = readFile(m->arena, path, &len);

  if (!text)
    return;

  const char* line = text;

  while (*line != '\0')
  {
    const char* end = strchr(line, '\n');

    if (!end)
      end = line + strlen(line);

    Str fields[MAX_FIELDS];

    if (splitCsv(str_of(line, (size_t)(end - line)), fields, MAX_FIELDS) >= 1 && fields[0].len > 0)
      loadEnumClass(m, dir, fields[0]);

    if (*end == '\0')
      break;

    line = end + 1;
  }
}

Status meta_load(Meta* m, Arena* arena, const char* dir)
{
  memset(m, 0, sizeof(*m));

  m->arena = arena;

  /* A missing params.csv is a hard failure: every engine-call arity check and every
   * emitted call name depends on it, so compiling without it would invent output. */
  char params[1024];

  snprintf(params, sizeof(params), "%s/params.csv", dir);

  size_t len = 0;

  if (!readFile(arena, params, &len))
    return ST_IO;

  loadParams(m, dir);
  loadEnums(m, dir);

  return ST_OK;
}

const MetaMethod* meta_find_method(const Meta* m, const char* name)
{
  for (int i = 0; i < m->methodCount; i++)
    if (strcmp(m->methods[i].name, name) == 0)
      return &m->methods[i];

  return NULL;
}

bool meta_is_enum_value(const Meta* m, Str value)
{
  for (int i = 0; i < m->enumValueCount; i++)
    if ((int)strlen(m->enumValues[i]) == value.len && memcmp(m->enumValues[i], value.p, (size_t)value.len) == 0)
      return true;

  return false;
}
