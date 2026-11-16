# Citation Exception Register

**Status:** living register, opened 2026-11-15. Companion to `scripts/citation_audit.py`.
**Reference revision checked against:** `d47ac8637346f60b73736793642731999885960a` (`Severed-Chains`
`main`, 2026-11-15). Baseline for comparison: `0c08c679b6b54e167b00851f1f1458d4dea3eade`.

This file exists so that **unverified citations are declared rather than implied**. Research
documents cite reference code by `file.java:123`. Those pointers were correct when written. The
reference repository has since moved, and a pointer that no longer lands on the code it quotes is
worse than no citation: it reads like evidence.

Nothing here changes a claim about observed behaviour. Every entry is either a *pointer* problem, a
*resolution* problem the tool cannot settle, or a small number of places where a document describes
a construct that has since changed — that third kind is the only one that can hide a factual error,
and it is called out separately in §4.

---

## 1. What was checked, and by what

`scripts/citation_audit.py` — run from the SC repo root:

```
SEVERED_CHAINS_REPOS=/path/to/parent-of-reference-repos \
  python3 scripts/citation_audit.py --revision HEAD --from 0c08c679b --from-repo Severed-Chains
```

Also available as `make citations`. **Opt-in by construction:** the script needs a reference
checkout supplied via `--repos-root` or `SEVERED_CHAINS_REPOS`. With neither, it prints one line
and exits 0, so the default `make check` gate never touches a reference repository (TD-25).

How a citation is judged, in order:

1. **Locate the file.** A cited path containing `/` must match exactly one file. A bare file name
   is resolved against every repository the document's prose names; a bare name that matches more
   than one path is **not guessed**, it is reported `AMBIGUOUS-REPO`. (A document that read only one
   repository cannot be citing another repository's same-named file; that rule is applied before the
   ambiguity report.)
2. **Compare the quoted block.** The text at the cited lines is compared *exactly* between the two
   revisions. Identical and unique → `RESOLVED` (the pointer is right, or drifted to an equally
   correct place). Found byte-identical at exactly one other location → `MOVED`, a pointer problem.
   Otherwise → `CHANGED`, which is *never* auto-fixed.

This replaces an earlier prototype that scanned quoted identifiers near the cited line. That
heuristic produced 324 mostly-spurious reports — it was measuring prose, not code — and its counts
are superseded by the numbers below.

## 2. Census at `d47ac8637`

1 330 citations across 58 documents, counted 2026-11-15 against a `Severed-Chains` checkout at
`d47ac8637`. `research/audits/**` and this file are excluded from the corpus: the first are
transcripts of past verification passes, the second quotes citations in order to describe them.

| Class | Count | Meaning | Action |
|---|---|---|---|
| `RESOLVED` | 925 | Cited block matches the reference byte-for-byte | none |
| `MOVED` | 178 | Code relocated; block intact elsewhere | pointer nudge, see §3 |
| `UNCHECKED-NO-BASELINE` | 73 | No baseline revision for that repo; movement undetectable | none available |
| `BLANK-LINES` | 56 | Cited lines are blank/comment context, nothing to compare | treat as `RESOLVED`-weak |
| `AMBIGUOUS-REPO` | 58 | File name matches >1 path | **disambiguate, see §4** |
| `CHANGED` | 22 | Quoted block differs at `d47ac8637` | **needs re-read, see §5** |
| `IMPLAUSIBLE-RANGE` | 12 | Citation ranges > 60 lines | formatting; trim to the anchor lines |
| `PAST-EOF` | 4 | Cited line beyond end of file | likely a range-start misparse |
| `NO-SUCH-FILE` | 2 | Path unresolvable under a named repo | formatting, see §5 |

**Headline: 925 citations confirmed correct at `d47ac8637`, 154 open in 22 documents, 178 pointers
that a script *could* rewrite and deliberately will not.**

Of the 22 `CHANGED`, 20 are pre-existing; the other 2 are `LodMod.java:312` in `modding/02` §4.3,
written by hand on 2026-11-15 after reading the source at `d47ac8637`. They point at real code and are
flagged only because the two-line block around them differs from the baseline. The audit counts, it
does not judge — which is why §5 exists.

## 3. What was deliberately *not* auto-fixed

`--apply` can rewrite a `MOVED` pointer to the unique location where the quoted block now lives. It
was tried, and **the result was reverted**. Reason, measured: applying 131 rewrites did not drain the
`MOVED` set — it produced 101 *new* ones and moved 29 citations from *matching* to *not matching*.
A pass that manufactures more work than it removes is not a checker, it is a randomiser with good
optics.

The consequence for anyone using `--apply` later: the uniqueness requirement means a rewrite only
happens when a block appears exactly once at the target revision, but uniqueness is not the same as
*relevance*, and nothing in the script reads the prose around the citation. Do not run it without
reviewing the diff line by line. Everything in §4 and §5 is safe to read as *nothing has been
rewritten by tooling*.

## 4. `AMBIGUOUS-REPO` — the largest class, and the least alarming

58 citations name a bare file that matches more than one path. Two sub-kinds:

- **Genuinely cross-repo:** `Spu.java`, `Sequencer.java`, `Sssq.java`, `Main.java`, `FileData.java`,
  `Random.java`. Several exist more than once *inside* `Severed-Chains` too
  (`legend/core/spu/Spu.java` and `legend/core/audio/xa/`; `legend/core/audio/sequencer/Sequencer.java`
  and `legend/game/sound/Sequencer.java`), plus `Dig-Dabas` and `Mod-Loader` carry their own copies.
- **Concentrated:** 22 of the 58 are in `research/audio/01-audio-architecture.md`, which cites
  engine, sequencer and SPU code in one document — exactly where name collisions are expected.

Most of these are almost certainly correct: `audio/01` §2 says in prose that it is describing the
engine's sequencer, not the minigame's. The tool cannot prove that, so it declines to guess.
**Fix, when a milestone next touches a document:** qualify the path (`legend/core/spu/Spu.java:85`,
not `Spu.java:85`). That costs nothing, removes the ambiguity permanently, and makes the next audit
run definitive. Do not mass-qualify by script — the whole point is that the correct repository is a
judgement about the sentence, not a filename lookup.

## 5. `CHANGED` / `NO-SUCH-FILE` — what to re-read, by area

These are the citations where the *referent* may have changed. Count is small; concentration matters.

| Document | Cluster | What to check |
|---|---|---|
| `game/05b-battle-entities-and-formulas.md` | `Battle.java:9326`, `Battle.java:8568`, plus 7 `BLANK-LINES` in §3.3/§5.1 | §3.3 cites `Battle.loadEnemyDropsAndScript` (`@ 0x80109808`) at `:9326`. Verified by hand: that method still exists at `d47ac8637` and **starts at 9331** — the citation is ~5 lines off, the described content (`EnemyRewardsEvent`, `LoadEnemyEvent` with its replaceable `scriptFuture`, drops, xp/gold) is intact. Pointer fix, not a fact fix. §5.1's `calculateMagicDamage` was not re-checked; same treatment. |
| `game/03-submap.md` | 5 `CHANGED` + 6 `BLANK-LINES`, all `SMap.java` | `SMap.java` shifted by a handful of lines in #2915/#2907. Re-anchor each against `d47ac8637`; none of the four commits touched submap traversal, so expect offsets, not contradictions. |
| `game/05c-battle-additions-and-ui.md`, `game/06-characters-and-progression.md`, `game/07-inventory-and-items.md` | `LodMod.java:768` ×2, `Battle.java:9319–9345`, `LodMod.java:679–688`, `ShopScreen.java:732–787` | These sit in the regions the four drift commits changed — #2915 quantity buy/sell (`ShopScreen`), #2908 equipment filtering (`LodMod`), #2907 addition practice (`LodTrainingHandler`, `Battle`). Highest chance of a real behaviour difference; re-read before any of these areas is implemented. |
| `modding/02-registries-and-events.md` | `lodmod/LodMod.java:114` | **A genuine content change — see §6.** |
| `platform/02-execution-model.md` | `WMap.java:681`, `WMap.java:707`, `Battle.java:2040` | `WMap.java:681` is cited by two documents (`04` and `platform/02`). `WMap.java` is 17 lines **longer** at `d47ac8637` than at the baseline (6 262 vs 6 245), `Battle.java` 5 longer (9 397 vs 9 392), `SMap.java` 6 longer (5 797 vs 5 791) — growth, not deletion, so expect offsets rather than contradictions. |
| `psx/02-gte.md` | `SMap.java:2140` | GTE-adjacent submap call site; re-anchor. |
| `scripting/01-fate-language.md` | 2 `NO-SUCH-FILE` (`Severed-Chains/.../scripting/ScriptManager.java:423-468`, `:452-468`) | Written with a repo-prefixed path and a path ellipsis, which the tool cannot resolve. `ScriptManager.java` does exist under `Severed-Chains` — **this is formatting, not a missing file.** Rewrite the citations as `src/main/java/legend/game/scripting/ScriptManager.java:452` and they will check. |

**Two corrections to an earlier draft of this register.** It listed "`game/05a`:
`loadEnemyEvent` / `loadAndRenderSubmapModels`, restructured by #2907" and "`rendering/02`: four
`tmd/Renderer.java` citations lost their context". **Both entries were wrong and are withdrawn.**
Neither identifier appears anywhere in the reference repository at *either* revision, and neither
`05a` nor `rendering/02` contains the citations the draft claimed. They were copied from the noisy
prototype's output without checking the document — which is the exact failure this register exists to
prevent. The real entry in that area is the `05b` §3.3 line above, and the fact that a *5-line
pointer offset* is what the noise had been hiding.

## 6. One confirmed content change: the event-listener census

`research/modding/02-registries-and-events.md` §4.3 records **7 listener classes / 39 handler
methods** in `Severed-Chains`, with the annotation-scan mechanism described as class-level. At
`d47ac8637` (verified, not inferred):

- Listener-bearing files: **8** (was 7). New: `legend/lodmod/LodTrainingHandler.java`, introduced by
  *Addition Practice* (#2907). It is **instance-registered** — `LodMod.java:312` calls
  `EVENTS.register(this.trainingHandler)` — so it is a weak-instance handler, not an annotation-scan
  one. Its `handleLoadEnemy` replaces `LoadEnemyEvent.scriptFuture`, which is the first in-repo use
  of that seam.
- **Class-level `@EventListener` sites dropped 2 → 1.** `LodMod` carried the annotation at line 114
  at the baseline; at `d47ac8637` line 114 is its `@Mod(...)` annotation and the class-level
  `@EventListener` is gone. The annotation-scan path is still exercised by the remaining site, so the
  mechanism is still attested in-repo — but the census sentence that lists *which* classes use it is
  now stale, and `LodMod` is one of the two documents' examples.
- Explicit `priority = ...` usage: **still zero** at both revisions. The "all handlers are `NORMAL`"
  finding survives.

This matters beyond tidiness. `modding/02` is the evidence for the event-cancellation and
mod-surface behaviour in `cross/02-compatibility-checklist.md` (CL-ENG-02), and CL-ENG-02 is
verified by a test that *embeds those two documents*. If the census drifts further, that test is the
thing that will notice — which is the intended design. **Before I4 (Fate runtime) or any
event-surface work, re-run the audit and re-count listeners at whatever revision is then checked
out; do not re-read from this register alone.**

## 7. Honest limits

- **925 `RESOLVED` means "the quoted text is still there", not "the claim about it is right."** Line
  drift and block identity are what a script can see. Behavioural equivalence was not re-verified.
- The 5-line `05b` offset above was found by hand, from a sample of five. Assume other offsets exist
  and are harmless *only* where the four drift commits did not touch the area — the commit list is in
  `planning/07` §8.
- `MOD-Loader` and `Dig-Dabas` citations have no baseline comparison available, hence 73
  `UNCHECKED-NO-BASELINE`: that is the absence of evidence, not evidence of correctness.
- The three reference repositories other than `Severed-Chains` are exactly at their recorded
  revisions, so their citations cannot have drifted.
