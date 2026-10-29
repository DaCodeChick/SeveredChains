/* main.c — `fate` command-line entry point.
 *
 * Only `compile` exists in this milestone; the other planned subcommands are
 * deliberately absent rather than stubbed, so an unimplemented operation reads as an
 * unknown command instead of silently succeeding.
 *
 * Exit codes (the contract approved for this tool, which deliberately differs from the
 * reference's):
 *   0 success, 1 usage error, 2 I/O or allocation failure, 3 compile diagnostics.
 *
 * The compile pipeline, in order:
 *   read main -> lex/parse -> scan definitions -> load includes (breadth first,
 *   scanning each as it is parsed) -> seed the global scope -> emit.
 *
 * Definition scanning completes for *every* file before any emission starts, because a
 * call's arity diagnostic is answered from the whole function table, including
 * definitions that appear later in the same file or inside an include. */

#include "emit.h"
#include "fate.h"
#include "lexer.h"
#include "meta.h"
#include "parse.h"
#include "util.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define FATE_MAX_INCLUDE_DIRS 32
#define FATE_MAX_UNITS 32

typedef struct Job
{
  Arena arena;
  Meta meta;
  Emit emit;
  Diag diag;

  /** Rendered lexer complaints, printed before @ref Job::diag. Kept apart because the
   *  reference does not count them as errors: a tab-indented file still exits 0. */
  Buf warn;
  const char* includeDirs[FATE_MAX_INCLUDE_DIRS];
  int includeDirCount;
} Job;

/** Options after the `compile` subcommand. */
typedef struct Options
{
  const char* metaDir;
  const char* output;
  const char* input;
} Options;

static char* readFileToArena(Arena* a, const char* path, Str* out)
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
  *out = str_of(buf, got);

  return buf;
}

/** @brief Resolve an include filename against the -L directories, then the directory of
 *  the file that asked for it.
 *
 *  @return A path owned by the job, or NULL when the file is nowhere to be found. */
static const char* resolveInclude(Job* job, const char* name, const char* relativeTo)
{
  static char path[1024];
  static char tried[FATE_MAX_INCLUDE_DIRS + 1][1100];
  int triedCount = 0;

  for (int i = 0; i < job->includeDirCount; i++)
  {
    snprintf(path, sizeof(path), "%s/%s", job->includeDirs[i], name);
    FILE* f = fopen(path, "rb");

    if (f)
    {
      fclose(f);
      return path;
    }

    if (triedCount < (int)(sizeof(tried) / sizeof(tried[0])))
      snprintf(tried[triedCount++], sizeof(tried[0]), "%s", path);
  }

  if (relativeTo && relativeTo[0] != '\0')
  {
    snprintf(path, sizeof(path), "%s/%s", relativeTo, name);
    FILE* f = fopen(path, "rb");

    if (f)
    {
      fclose(f);
      return path;
    }
  }

  return NULL;
}

/** Parse one translation unit from a file.
 *
 *  Lexical complaints are recorded while the unit is read. They go to `job->warn`
 *  rather than to the diagnostic list: the reference prints them from its tokenizer,
 *  they do not enter the errors list, and a file that only has them still compiles and
 *  exits 0. The recorded text is already escaped for display (a tab prints as `\t`). */
static Status parseUnit(Job* job, Source* src, const char* displayName)
{
  Str text;

  if (!readFileToArena(&job->arena, displayName, &text))
    return ST_IO;

  Lexer lx;

  if (lex_run(&job->arena, text, &lx) != ST_OK)
    return ST_IO;

  for (int i = 0; i < lx.errorCount; i++)
  {
    const LexerError* le = &lx.errors[i];

    buf_printf(&job->warn, "line %d:%d token recognition error at: '%.*s'\n", le->line, le->col, le->text.len,
               le->text.p ? le->text.p : "");
  }

  if (parse_translationUnit(&job->arena, &lx, src, &job->diag) != ST_OK)
    return ST_COMPILE;

  return ST_OK;
}

int main(int argc, char** argv)
{
  if (argc < 2)
  {
    fprintf(stderr, "usage: fate <command> [options] [args]\n");
    fprintf(stderr, "commands:\n  compile  translate Fate Script source into assembly text\n");
    return 1;
  }

  const char* command = argv[1];

  if (strcmp(command, "compile") != 0)
  {
    fprintf(stderr, "unknown command: %s\n", command);
    fprintf(stderr, "commands:\n  compile  translate Fate Script source into assembly text\n");
    return 1;
  }

  Job job;

  memset(&job, 0, sizeof(job));

  Options opt;

  memset(&opt, 0, sizeof(opt));

  for (int i = 2; i < argc; i++)
  {
    const char* a = argv[i];

    if (strcmp(a, "-o") == 0 && i + 1 < argc)
      opt.output = argv[++i];
    else if (strcmp(a, "-m") == 0 && i + 1 < argc)
      opt.metaDir = argv[++i];
    else if (strcmp(a, "-L") == 0 && i + 1 < argc)
    {
      /* A dropped `-L` would silently change where includes resolve, so a full table is
       * a usage error rather than something to ignore. */
      if (job.includeDirCount == FATE_MAX_INCLUDE_DIRS)
      {
        fprintf(stderr, "too many -L directories (limit %d)\n", FATE_MAX_INCLUDE_DIRS);
        return 1;
      }

      job.includeDirs[job.includeDirCount++] = argv[++i];
    }
    else if (a[0] == '-' && a[1] != '\0')
    {
      fprintf(stderr, "unknown option: %s\n", a);
      return 1;
    }
    else if (opt.input == NULL)
      opt.input = a;
    else
    {
      fprintf(stderr, "only one input file is supported\n");
      return 1;
    }
  }

  if (opt.input == NULL)
  {
    fprintf(stderr, "compile needs an input file\n");
    return 1;
  }

  if (opt.metaDir == NULL)
  {
    fprintf(stderr, "compile needs a metadata directory: pass -m <dir>\n");
    return 1;
  }

  /* The metadata bundle is a required input, not an optional extra: an engine call's
   * emitted name comes out of it, so compiling without it would invent output. */
  if (meta_load(&job.meta, &job.arena, opt.metaDir) != ST_OK)
  {
    fprintf(stderr, "could not read metadata from %s\n", opt.metaDir);
    return 2;
  }

  /* An include directory that the command line had to name explicitly is an input, not
   * a hint. Checked here — before any source is read and before anything is emitted —
   * because letting it pass turns an environment mistake into per-function
   * `unknown function` diagnostics and a non-empty program (owner ruling 2026-10-28,
   * decision 1): a supplied `-L` path that cannot be read is exit 2, with no output
   * file, and it is reported before the program text is discussed.
   *
   * The reference aborts with exit 1 in this situation. That status is deliberately NOT
   * reproduced: the native 0/1/2/3 contract maps "a path the command line supplied
   * cannot be read" to 2, and R1 does not override the exit-code contract. The reference
   * makes the same check for the same reason - `Shell.readIncludeDirs` (Shell.java:273-286)
   * throws `IncludeFailedException("Include dir <path> does not exist")`, and it throws
   * outside the compile `try`, so nothing is caught and nothing is written. Pinned by
   * `c1_missing_incdir` / `c2_incdir_is_file`, with `c3_incdir_ok` as the control that a
   * usable `-L` still compiles (fate/testdata/manifest.json, `cli_contract`). */
  for (int i = 0; i < job.includeDirCount; i++)
  {
    if (!path_is_dir(job.includeDirs[i]))
    {
      fprintf(stderr, "no such include directory: %s\n", job.includeDirs[i]);
      return 2;
    }
  }

  job.diag.buf.arena = &job.arena;

  buf_init(&job.warn, &job.arena);

  job.emit.arena = &job.arena;
  job.emit.meta = &job.meta;
  job.emit.diag = &job.diag;

  buf_init(&job.emit.out, &job.arena);
  buf_init(&job.emit.body, &job.arena);

  Source units[FATE_MAX_UNITS];
  int unitCount = 0;

  units[0] = (Source){0};
  units[0].displayName = opt.input;
  unitCount = 1;

  if (parseUnit(&job, &units[0], opt.input) != ST_OK)
  {
    fprintf(stderr, "could not read %s\n", opt.input);
    return 2;
  }

  emit_scan(&job.emit, &units[0], 0, "");

  /* Breadth-first include loading. A filename token that does not end exactly in
   * `.fate` is emitted but never loaded, which is what the reference's
   * `name.endsWith(".fate")` gate does — and p13 depends on it, because its
   * `include char_utils.fate ;` leaves a trailing space inside the token.
   *
   * The gate reads the *verbatim* token for that reason. Waiting until the loop below
   * to trim would open the file, resolve `charIsInActiveParty`, and drop the
   * `5: unknown function charIsInActiveParty` line that p13's golden carries; the
   * emitted `gosub inl[:charIsInActiveParty]` is the same either way, which is exactly
   * why the missing message needed a differential to be seen at all. */
  static char pending[FATE_MAX_UNITS + 1][256];
  static char done[FATE_MAX_UNITS + 1][256];

  int pendingCount = 0;
  int doneCount = 0;

  for (int i = 0; i < units[0].includeCount; i++)
    if (units[0].includes[i].raw && pendingCount < (int)(sizeof(pending) / sizeof(pending[0])))
      snprintf(pending[pendingCount++], sizeof(pending[0]), "%s", units[0].includes[i].raw);

  for (int head = 0; head < pendingCount && unitCount < FATE_MAX_UNITS; head++)
  {
    char token[256];

    snprintf(token, sizeof(token), "%s", pending[head]);

    /* The lookup form: the same token with its trailing blanks removed, used for the
     * directory search and for the "already loaded" check. */
    char name[256];

    snprintf(name, sizeof(name), "%s", token);

    size_t name_len = strlen(name);

    while (name_len > 0 && (name[name_len - 1] == ' ' || name[name_len - 1] == '\t'))
      name[--name_len] = '\0';

    if (name[0] == '\0')
      continue;

    bool already = false;

    for (int i = 0; i < doneCount; i++)
      if (strcmp(done[i], name) == 0)
        already = true;

    if (already)
      continue;

    if (doneCount < (int)(sizeof(done) / sizeof(done[0])))
      snprintf(done[doneCount++], sizeof(done[0]), "%s", name);

    if (!str_ends_with(str_of(token, strlen(token)), ".fate"))
      continue;

    const char* path = resolveInclude(&job, name, NULL);

    if (!path)
    {
      char msg[512];
      snprintf(msg, sizeof(msg), "Failed to load include %s", name);
      diag_add(&job.diag, 0, msg);
      continue;
    }

    Source* unit = &units[unitCount];

    *unit = (Source){0};

    /* Copied into the arena because `name` lives on this iteration's stack and
     * `displayName` outlives the loop (it is the unit's "as it appeared" path). */
    unit->displayName = arena_dup(&job.arena, name, strlen(name));

    if (parseUnit(&job, unit, path) != ST_OK)
    {
      char msg[512];
      snprintf(msg, sizeof(msg), "Failed to load include %s", name);
      diag_add(&job.diag, 0, msg);
      continue;
    }

    emit_scan(&job.emit, unit, unitCount, "");
    unitCount++;

    /* Nested includes are queued in the same verbatim form, so they meet the same
     * gate as the top-level ones. */
    for (int i = 0; i < unit->includeCount && pendingCount < (int)(sizeof(pending) / sizeof(pending[0])); i++)
      if (unit->includes[i].raw)
        snprintf(pending[pendingCount++], sizeof(pending[0]), "%s", unit->includes[i].raw);
  }

  /* Globals live in the scope every function starts from. */
  for (int i = 0; i < units[0].itemCount; i++)
  {
    if (units[0].items[i].kind != TOP_GLOBAL)
      continue;

    /* One GlobalVar per TOP_GLOBAL item: `globs` is a single binding, not a run of
     * them, so walking it as an array would read past the allocation. */
    const GlobalVar* g = units[0].items[i].globs;

    if (g != NULL)
      emit_bindGlobal(&job.emit, g->name, g->length, g->values[0]);
  }

  const Status st = emit_source(&job.emit, &units[0]);

  /* Stream order follows the reference: tokenizer complaints are emitted while the
   * source is read, so they come out above the compile diagnostics. Neither group is
   * separated from the other by a banner, and neither is indented. */
  if (job.warn.len > 0)
    fwrite(job.warn.data, 1, job.warn.len, stderr);

  if (job.diag.count > 0 && job.diag.buf.len > 0)
    fwrite(job.diag.buf.data, 1, job.diag.buf.len, stderr);

  char defaultOut[1024];

  const char* outPath = opt.output;

  if (!outPath)
  {
    /* Default output name: the input path with its extension replaced by `.txt`. */
    snprintf(defaultOut, sizeof(defaultOut), "%s", opt.input);
    char* dot = strrchr(defaultOut, '.');
    char* slash = strrchr(defaultOut, '/');

    if (dot && slash && dot < slash)
      dot = NULL;

    if (dot)
      *dot = '\0';

    strcat(defaultOut, ".txt");
    outPath = defaultOut;
  }

  if (st == ST_OOM)
  {
    fprintf(stderr, "out of memory\n");
    return 2;
  }

  if (job.emit.aborted)
  {
    /* p10 class: the reference aborts before writing anything. Matching it means no
     * output file at all, not an empty one, and nothing further is printed: what the
     * reference prints on its way out is a JVM stack trace, which ruling R1 keeps out
     * of the goldens. The diagnostic that caused the abort is already on stderr, and
     * exit 3 plus the missing output file carry the rest. */
    return 3;
  }

  FILE* out = fopen(outPath, "wb");

  if (!out)
  {
    fprintf(stderr, "could not write %s\n", outPath);
    return 2;
  }

  if (job.emit.out.len > 0)
    fwrite(job.emit.out.data, 1, job.emit.out.len, out);

  fclose(out);

  return (job.diag.count > 0) ? 3 : 0;
}
