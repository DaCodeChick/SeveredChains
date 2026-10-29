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

  /** Set when an `include` could not be loaded. That is fatal, not a diagnostic to
   *  collect and carry on past: see the abort branch in @ref main. */
  bool includeFailed;

  /** The one line printed instead of `diag` when @ref Job::includeFailed is set. */
  char fatalMsg[600];
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

/** @brief Resolve an include filename against the `-L` directories, in argument order.
 *
 *  The directory of the file that asked for the include is deliberately **not** searched:
 *  the reference searches only `-L` (design doc §5.1), re-measured on 2026-10-28 with the
 *  built oracle — a `.fate` sitting beside the source it is included from still reports
 *  `Could not resolve include … in include paths []` and writes no output. An earlier
 *  version took a `relativeTo` directory and searched it; nothing ever passed anything but
 *  NULL, so that branch was dead *and* a standing invitation to diverge. It is gone rather
 *  than dormant.
 *
 *  @param job  Supplies the include search path (`includeDirs`, `includeDirCount`).
 *  @param name The include name with trailing blanks already removed.
 *  @return A path in this function's static buffer, valid until the next call, or NULL
 *          when the name resolves nowhere.
 *  @note Thread: not thread-safe — the probe buffer is function-static; one compile is
 *        one job. */
static const char* resolveInclude(Job* job, const char* name)
{
  static char path[1024];

  for (int i = 0; i < job->includeDirCount; i++)
  {
    snprintf(path, sizeof(path), "%s/%s", job->includeDirs[i], name);
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

/** One queued `include` directive, with enough context to say where it came from.
 *
 *  The queue used to hold bare filename strings, which was enough while an unresolvable
 *  include was a diagnostic and the compile carried on. It is not enough once the run
 *  stops: the operator needs the file and the line that asked for the missing one. */
typedef struct IncludeReq
{
  char token[256];      /**< verbatim filename token, trailing space preserved */
  int line;             /**< line the directive appeared on, 0 when unknown */
  const char* fromUnit; /**< file that asked for it; NULL for the script named on the command line */
} IncludeReq;

/** @brief Stop the compile because an include could not be loaded.
 *
 *  @param job The job; `includeFailed` is set, which ends the load loop and skips emission.
 *  @param req The queued directive that failed, used for the `line: ` prefix.
 *  @param what The message, already naming the include file.
 *
 *  @note Thread: single-threaded; writes only @ref Job fields. */
static void includeFatal(Job* job, const IncludeReq* req, const char* what)
{
  job->includeFailed = true;

  if (req->line > 0)
    snprintf(job->fatalMsg, sizeof(job->fatalMsg), "%d: %s", req->line, what);
  else
    snprintf(job->fatalMsg, sizeof(job->fatalMsg), "%s", what);
}

/** @brief Name of the file the emitted assembly would go to.
 *
 *  @param opt Parsed options; @ref Options::output wins when given.
 *  @param buf Destination for the derived default name.
 *  @param cap Size of @p buf.
 *  @return The path to write to, either @p opt's output or @p buf.
 *
 *  The default is the input path with its extension replaced by `.txt`, which is what the
 *  reference does and what the goldens were taken with. Kept as one function because the
 *  abort paths need the same name as the write, to decide what to do with a file already
 *  sitting there. */
static const char* resolveOutputPath(const Options* opt, char* buf, size_t cap)
{
  if (opt->output != NULL)
    return opt->output;

  snprintf(buf, cap, "%s", opt->input);

  char* dot = strrchr(buf, '.');
  char* slash = strrchr(buf, '/');

  if (dot && slash && dot < slash)
    dot = NULL;

  if (dot)
    *dot = '\0';

  strncat(buf, ".txt", cap - strlen(buf) - 1);
  return buf;
}

/** @brief Remove a pre-existing output file when this run produces none.
 *
 *  @param outPath Where the assembly text would have been written, or NULL.
 *
 *  A failed compile that leaves the previous run's output behind is worse than one that
 *  removes it: anything packaging whatever `-o` names ships a stale program and reports
 *  success. Deleting the file and saying so keeps the failure visible where it can still
 *  be acted on. Only a regular file is removed: a directory, a device such as
 *  `/dev/null`, or a socket is not "the previous run's output", and deleting one of those
 *  would be a bug rather than a cleanup. The caller guarantees that @p outPath is not the
 *  input file — @ref main settles that before anything is read or written.
 *
 *  @note Thread: single-threaded; touches the filesystem. */
static void discardStaleOutput(const char* outPath)
{
  if (outPath == NULL || !path_is_regular_file(outPath))
    return;

  if (remove(outPath) == 0)
    fprintf(stderr, "removed stale output %s (this run produced no output)\n", outPath);
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

  /* The output name is needed before anything is read: the abort paths below have to
   * decide what to do with a file already sitting at it, and a `.txt` source would
   * otherwise resolve to its own name as the default output. */
  char defaultOut[1024];

  const char* outPath = resolveOutputPath(&opt, defaultOut, sizeof(defaultOut));

  if (strcmp(outPath, opt.input) == 0)
  {
    /* Reached by `-o <the source itself>`, and by a `.txt` source whose default output
     * name is the source itself. Checked before the file is opened for reading, so no
     * run can damage the program text it was asked to compile. */
    fprintf(stderr, "refusing to write the output file: %s is also the input file\n", outPath);
    return 2;
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
   * `c1_missing_incdir` / `c2_incdir_is_file`, with `c3_valid_include_control` as the control that a
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
   * `include char_utils.fate ;` leaves a trailing space inside the token. That is also
   * why the "missing include" rule below does not fire for p13: `char_utils.fate ` is
   * not read as a file include at all, and `char_utils.fate` exists beside the fixture.
   *
   * The gate reads the *verbatim* token for that reason. Waiting until the loop below
   * to trim would open the file, resolve `charIsInActiveParty`, and drop the
   * `5: unknown function charIsInActiveParty` line that p13's golden carries; the
   * emitted `gosub inl[:charIsInActiveParty]` is the same either way, which is exactly
   * why the missing message needed a differential to be seen at all. */
  static IncludeReq pending[FATE_MAX_UNITS + 1];
  static char done[FATE_MAX_UNITS + 1][256];

  int pendingCount = 0;
  int doneCount = 0;

  for (int i = 0; i < units[0].includeCount; i++)
  {
    if (!units[0].includes[i].raw || pendingCount >= (int)(sizeof(pending) / sizeof(pending[0])))
      continue;

    /* The token keeps its trailing space for the gate above; the line and the origin are
     * recorded so a failure can name both. */
    snprintf(pending[pendingCount].token, sizeof(pending[0].token), "%s", units[0].includes[i].raw);
    pending[pendingCount].line = units[0].includes[i].line;
    pending[pendingCount].fromUnit = NULL;
    pendingCount++;
  }

  /* One failed include ends the loop: `includeFailed` is part of the condition, so a
   * queued include behind the failed one is never opened either. */
  for (int head = 0; head < pendingCount && unitCount < FATE_MAX_UNITS && !job.includeFailed; head++)
  {
    const IncludeReq* req = &pending[head];
    char token[256];

    snprintf(token, sizeof(token), "%s", req->token);

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

    const char* path = resolveInclude(&job, name);

    if (!path)
    {
      /* Fatal, and the two clauses of the message are for the two ways to get here: the
       * file is elsewhere (name the asker) or no search path was given at all. */
      char tail[256];
      char what[512];

      tail[0] = '\0';

      if (req->fromUnit != NULL)
        snprintf(tail, sizeof(tail), " asked for by %s", req->fromUnit);

      if (job.includeDirCount == 0)
        snprintf(what, sizeof(what), "include file not found: %s%s - no include path given, pass -L <dir>", name, tail);
      else
        snprintf(what, sizeof(what), "include file not found: %s%s", name, tail);

      includeFatal(&job, req, what);
      break;
    }

    Source* unit = &units[unitCount];

    *unit = (Source){0};

    /* Copied into the arena because `name` lives on this iteration's stack and
     * `displayName` outlives the loop (it is the unit's "as it appeared" path). */
    unit->displayName = arena_dup(&job.arena, name, strlen(name));

    if (parseUnit(&job, unit, path) != ST_OK)
    {
      /* Found but unreadable or unparseable. The file exists, so this is not "not
       * found", and its functions are still missing from the table, which makes the
       * program exactly as unbuildable; the same treatment, with a message that does not
       * tell the operator to look for a file they can see. */
      char what[512];

      snprintf(what, sizeof(what), "include file could not be read: %s", name);
      includeFatal(&job, req, what);
      break;
    }

    emit_scan(&job.emit, unit, unitCount, "");
    unitCount++;

    /* Nested includes are queued in the same verbatim form, so they meet the same
     * gate as the top-level ones. */
    for (int i = 0; i < unit->includeCount && pendingCount < (int)(sizeof(pending) / sizeof(pending[0])); i++)
    {
      if (!unit->includes[i].raw)
        continue;

      snprintf(pending[pendingCount].token, sizeof(pending[0].token), "%s", unit->includes[i].raw);
      pending[pendingCount].line = unit->includes[i].line;
      pending[pendingCount].fromUnit = unit->displayName;
      pendingCount++;
    }
  }

  /* An include the compiler could not load stops the run here, and nothing is emitted: a
   * program whose calls into that file have no definitions is not a lesser program, it is
   * a broken one (owner ruling 2026-10-28).
   *
   * The reference agrees that this aborts. `Include.resolve` throws
   * `IncludeFailedException("Could not resolve include ...")` for a missing file and that
   * throw is outside the `catch (IOException)` which would have logged and continued, so
   * it reaches `Shell.java`'s `catch (Throwable)`, no output is written, and the run then
   * exits 0. Two things are deliberately not copied: the stack trace, which ruling R1
   * keeps out of the goldens, and the exit status, which the 0/1/2/3 contract puts at 2.
   * Pinned by `c4_missing_include_with_path` and `c5_missing_include_no_path`, with
   * `c3_valid_include_control` and `c6_trailing_space_gate` as the controls that a resolvable
   * include and a non-`.fate` token still behave as before (manifest.json `cli_contract`).
   *
   * Only the abort reason is printed. `job.diag` can already hold `missing return` lines
   * from the definition scan, and those describe a program this run refused to build. */
  if (job.includeFailed)
  {
    fprintf(stderr, "%s\n", job.fatalMsg);
    discardStaleOutput(outPath);
    return 2;
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

  if (st == ST_OOM)
  {
    fprintf(stderr, "out of memory\n");
    discardStaleOutput(outPath);
    return 2;
  }

  if (job.emit.aborted)
  {
    /* p10 class: the reference aborts before writing anything. Matching it means no
     * output file at all, not an empty one, and nothing further is printed: what the
     * reference prints on its way out is a JVM stack trace, which ruling R1 keeps out
     * of the goldens. The diagnostic that caused the abort is already on stderr, and
     * exit 3 plus the missing output file carry the rest. */
    discardStaleOutput(outPath);
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
