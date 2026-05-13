# Crucible CI Pipeline — `crucible-lint` and the Composed Tooling Stack

> **Status: planned implementation. Replaces the deferred Fixy DSL (see `fixy.md` deferral header) as the mechanism for enforcing Crucible's dimensional discipline at the source level.**

This document specifies the Crucible CI pipeline: how the substrate's discipline (CLAUDE.md axioms, code_guide.md wrapper rules, fixy.md §24 collision catalog) is mechanically enforced on every PR and during local development. The architecture is a **composed pipeline of mature open-source tools plus a thin Crucible-specific layer plus a unified driver**, not a single new analyzer. Most of the work is rule authorship, policy specification, and integration scripting; the execution engines are off-the-shelf.

---

## 1. Thesis and scope

The substrate is shipped: ~60 safety wrappers, ~17 effect-row primitives, ~11 permission combinators, ~25 session-type headers, the GAPS task list of ~150 `WRAP-*` opportunities. The discipline these enforce is documented across CLAUDE.md, code_guide.md, fixy.md §24. What's missing is a tool that mechanically catches code which doesn't apply the discipline — wraps the right field as the right wrapper, threads the right effect row, names the right collision rule.

`crucible-lint` is that tool. Two design commitments shape it:

1. **Compose mature tools rather than reimplement them.** Infer's Pulse handles C++ memory safety better than anything we'd write. RacerD handles concurrency. Clang static analyzer handles path-sensitive null/uninit. Clang-tidy ships hundreds of stock checks across `bugprone-*`, `cppcoreguidelines-*`, `modernize-*`, `performance-*`, `readability-*`. clangd 17+ ships header-include-cleaner. The Crucible-specific layer adds ~200 ast-grep YAML rules + 5-10 clang-tidy custom plugins + a policy manifest; the rest is integration glue.

2. **The brand `crucible-lint` is the unified driver, not a new analyzer.** Users run `crucible-lint` on their PR; the driver invokes the layered pipeline; the output is aggregated into SARIF + terminal + reviewdog comments. Each layer is replaceable: when clangd adds a feature next year that obsoletes one of our custom checks, drop the custom check and use clangd's. The driver doesn't care which analyzer produced the SARIF.

**What this document is not.** Not a complete rule reference (that lives at the auto-generated `crucible-lint.dev/rules/` website built from the YAML/cpp source). Not a contributor guide for adding individual rules (see `tools/crucible-lint/CONTRIBUTING.md` after Phase 1 lands). Not a comparison against commercial tools beyond what's needed to justify the architecture choices.

---

## 2. Architecture — seven layers

```
┌──────────────────────────────────────────────────────────────────────────────┐
│  Layer 0   Compilation database                                              │
│            cmake --preset → compile_commands.json                            │
│            Foundation for every clang-based tool                              │
└──────────────────────────────────────────────────────────────────────────────┘
┌──────────────────────────────────────────────────────────────────────────────┐
│  Layer 1   General-purpose static analyzers (free coverage)                  │
│            • Infer Pulse              memory safety (UAF, leak, double-free) │
│            • Infer RacerD             static race detection                  │
│            • clang static analyzer    path-sensitive null / uninit           │
│            • clang-tidy stock checks  bugprone / cppcoreguidelines /         │
│                                       modernize / performance / readability  │
│            • header-include-cleaner   IWYU enforcement (clangd 17+)          │
│            • cppguard / Lakos check   custom: L0-L16 hierarchy enforcement   │
└──────────────────────────────────────────────────────────────────────────────┘
┌──────────────────────────────────────────────────────────────────────────────┐
│  Layer 2   Crucible structural rules (ast-grep YAML, ~200 rules)             │
│            Strong-types, linear-ownership, refinements, concurrency          │
│            patterns, banned constructs, naming discipline, hot-path checks,  │
│            all 150 WRAP-* opportunities, session/permission patterns         │
└──────────────────────────────────────────────────────────────────────────────┘
┌──────────────────────────────────────────────────────────────────────────────┐
│  Layer 3   Crucible semantic checks (clang-tidy custom plugins, ~5-10)       │
│            • effect-row-flow         declared row vs body's actual ops       │
│            • linear-flow             double-consume, unconsumed-on-exit       │
│            • permission-discovery    producer/consumer pair w/o Permission   │
│            • cache-line-layout       cross-thread atomic alignment            │
│            • collision-preview       Fn<> compositions that would trigger    │
│                                      §6.8 collision rules at instantiation   │
│            • mint-pattern-discipline audit cross-tier compositions for       │
│                                      mint_X factory + concept gate            │
└──────────────────────────────────────────────────────────────────────────────┘
┌──────────────────────────────────────────────────────────────────────────────┐
│  Layer 4   Policy enforcement (manifest + ripgrep + danger-style hooks)      │
│            • crucible-deny.toml      banned headers, banned types, banned    │
│                                      function calls, license requirements    │
│            • crucible-signatures.    expected substrate signatures; drift    │
│              json                    detection; new public API w/o wrapper   │
│                                      discipline → fail                       │
│            • per-PR policy hooks     "PR touches hot/* and bench/*           │
│                                      unchanged → block"; "PR adds wrapper    │
│                                      but no neg-compile fixture → block      │
│                                      per HS14"                                │
│            • secrets scan            gitleaks / trufflehog                    │
└──────────────────────────────────────────────────────────────────────────────┘
┌──────────────────────────────────────────────────────────────────────────────┐
│  Layer 5   Aggregation + output                                              │
│            • SARIF as primary format → GitHub Code Scanning + GitLab + Sonar │
│            • CodeChecker web UI      triage, baseline tracking, history      │
│            • reviewdog               PR inline review comments                │
│            • Terminal (human)         local development                       │
│            • LSP via efm-langserver  diagnostics in clangd-aware editors     │
│            Deduplication across analyzers; severity from strictest source    │
└──────────────────────────────────────────────────────────────────────────────┘
┌──────────────────────────────────────────────────────────────────────────────┐
│  Layer 6   Migration mode (--fix --batch via Coccinelle/spatch)              │
│            Each WRAP-* refactor → a semantic patch (.cocci or equivalent).   │
│            spatch applies the patch across the codebase, generating a single │
│            reviewable diff. Operator reviews, lands as one PR.               │
│            Per-rule trivial fix-its still go through clang-tidy --fix.       │
└──────────────────────────────────────────────────────────────────────────────┘
┌──────────────────────────────────────────────────────────────────────────────┐
│  Layer 7   Documentation site (clippy-style auto-generation)                  │
│            Per-rule page: stable URL, rationale, examples, counter-examples, │
│            doc anchor (CLAUDE.md / fixy.md / code_guide.md), GAPS task ID,   │
│            severity, fix availability, suppression syntax.                   │
│            Diagnostics include the URL.                                       │
└──────────────────────────────────────────────────────────────────────────────┘
```

Each layer is opt-in via `.crucible-lint.toml`. A small project uses Layers 0+1+2+5. The full Crucible CI uses all seven.

---

## 3. Layer-by-layer specification

### 3.1 Layer 0 — Compilation database

**Tool:** `cmake -DCMAKE_EXPORT_COMPILE_COMMANDS=ON` (already enabled per the existing `default` preset in CMakePresets.json) or `bear --` for Make-based builds.

**Output:** `build/compile_commands.json` — the foundation for every clang-based tool in Layers 1, 3, 6.

**Per-target generation:** for very large projects, generating per-target databases (`build/lint/<target>_compile_commands.json`) reduces the clang-based tools' scan scope. Optional optimization for Crucible scale.

### 3.2 Layer 1 — General-purpose analyzers

**Infer (open source, OCaml binary).** Pulse engine for C++ memory safety: use-after-free, null deref, double-free, leak. RacerD for static race detection: data races between threads accessing shared state without synchronization.

```bash
infer run --pulse --racerd-only -- cmake --build build
```

Crucible-specific config: `infer.config` excludes vendored code, baselines existing diagnostics. Initial run on the full Crucible codebase will produce hundreds of diagnostics — most are existing patterns the team has accepted; baselining tags them as "known noise" and the CI gate fires only on NEW violations.

**Clang static analyzer (scan-build).** Path-sensitive symbolic execution. Catches null derefs and uninitialized reads that escape Infer's coverage.

```bash
scan-build -o build/scan-results cmake --build build
```

Output is HTML reports plus machine-readable plist. Plist → SARIF via `scan-build-sarif` converter.

**clang-tidy stock checks.** Run with the full bugprone/cppcoreguidelines/modernize/performance/readability groups plus Crucible-specific custom checks (Layer 3).

```bash
clang-tidy -p build --checks='bugprone-*,cppcoreguidelines-*,modernize-*,performance-*,readability-*,crucible-*' -- $FILES
```

Per-file `.clang-tidy` configs override severity for contextual sensitivity (hot-path TUs are stricter; cold-path TUs allow some patterns the hot path forbids).

**header-include-cleaner.** Built into clangd 17+. Enforces the IWYU discipline documented in CLAUDE.md §XV: every header self-contained, no transitive include reliance, IWYU compliance.

```bash
clang-include-cleaner --print=changes -p build $FILES
```

Surfaces unused includes, missing includes, transitive-include dependencies. Fix-its applied in batch via `--edit`.

**Lakos layer enforcement (custom check).** CLAUDE.md L0-L16 layer hierarchy is documented but currently unenforced. Custom check parses include directives, verifies that L_i headers don't include L_j headers where j > i. Fires on cross-layer dependency violations.

Implementation: ~200 lines of Python parsing `#include` lines + a `crucible-layers.json` manifest mapping headers to layer numbers. Run as part of Layer 4 (policy enforcement) or as a clang-tidy custom check; both work.

### 3.3 Layer 2 — Crucible structural rules (ast-grep)

**Tool:** ast-grep (`sg`) — Rust-implemented Tree-sitter-backed pattern matcher. CLAUDE.md names it as the project's preferred AST tool. YAML rule files, fast incremental scanning, good C++ coverage via Tree-sitter-cpp.

**Rule corpus organization:**

```
tools/crucible-lint/rules/structural/
├── strong-types.yml          ~30 rules: raw uintXX → strong IDs / Refined
├── linear-ownership.yml      ~25 rules: RAII-wrappable resources
├── refinements.yml           ~20 rules: pre()-extractable to Refined<>
├── concurrency.yml           ~15 rules: false sharing, alignas, atomic types
├── banned-patterns.yml       ~30 rules: §IV opt-out + §X footguns
├── naming.yml                ~15 rules: §XVII identifier discipline
├── hot-path.yml              ~10 rules: alloc/sleep/syscall in hot scope
├── wrappers.yml              ~150 rules: every WRAP-* opportunity
└── sessions.yml              ~10 rules: raw SPSC/SWMR → typed channel
```

**Rule format** (each rule is a YAML document):

```yaml
id: CRUC-W-StrongID-001
language: cpp
severity: warning
category: strong-types
doc_anchor: "code_guide.md §II.2 TypeSafe + Types.h strong IDs"
rationale: |
  Strong-ID newtypes prevent silent parameter swap. CRUCIBLE_STRONG_ID
  provides O(1) wrapping with .raw() escape hatch. Mixing OpIndex and
  SlotId is a compile error; mixing two raw uint32_t parameters is a
  silent bug.
rule:
  any:
    - pattern: uint32_t $N{^(op_idx|opIdx|op_index)$}
    - pattern: uint32_t $N{^(schema_hash|schemaHash)$}
    - pattern: uint32_t $N{^(slot_id|slotId)$}
fix_template: "${strong_id_for($N)} $N"
applicable: machine-applicable
gaps_id: null
positive_examples:
  - file: fixtures/strong-types/raw_op_idx.cpp
negative_examples:
  - file: fixtures/strong-types/proper_op_index.cpp
```

**Authoring loop:** prototype the AST matcher in `clang-query` REPL → port working matcher to ast-grep YAML → add positive/negative fixture pair → run `crucible-lint --rule CRUC-W-StrongID-001 --test` → iterate. Faster than direct YAML editing for non-trivial patterns.

**Generated rules:** ~150 of the 200 rules are mechanical conversions from the GAPS WRAP-* task list. A script (`tools/crucible-lint/gen-rules-from-gaps.py`) reads the GAPS task descriptions, extracts (file, current type, target wrapper) tuples, generates the YAML rule. Manual review pass for accuracy. Convert ~80% automatically; hand-author the remaining ~20% that need semantic context.

**Fixture corpus:** every rule ships with at least one positive example (the pattern that should fire) and one negative example (the corrected form). Stored in `tests/fixtures/structural/<category>/`. CI runs `crucible-lint --self-test` to verify each rule fires on its positive and not on its negative; regression discipline.

### 3.4 Layer 3 — Crucible semantic checks (clang-tidy plugins)

**Tool:** clang-tidy with custom check plugins authored in C++ libtooling. Production-stable, integrates with clangd, supports fix-its.

**The 5-10 checks** (each ~500-1500 LOC of C++):

**`crucible-effect-row-flow`** — Walk the function body of every public API. Collect actual operations performed: calls into IO functions, allocations (via known allocator entry points), blocking syscalls (via syscall name list), atomic operations, network calls. Compare against the declared `Computation<Row<...>, T>` row on the function signature. Flag mismatches:
- "Function declares `Row<>` but body calls `malloc` — declare `Row<Alloc>`"
- "Function declares `Row<IO>` but body never performs IO — narrow to `Row<>`"
- "Function declares `Row<Bg>` but is called from a `Row<>` (foreground) context"

Effect-row catalog comes from `effects/Capabilities.h`.

**`crucible-linear-flow`** — Track every `Linear<T>` and `Permission<Tag>` binding through the function via clang's CFG. Detect:
- Double-consume: two `std::move` calls on the same value along any control-flow path
- Unconsumed-on-exit: a Linear binding live at function exit that wasn't moved into a return / parameter / drop
- Capture-by-ref into a closure that outlives the linear (Wood-Atkey 2022 violation per fixy.md §24.5)

This is the structural check that fixyc would have done at template-instantiation time; we move it to clang-tidy's CFG analyzer instead.

**`crucible-permission-discovery`** — Identify producer/consumer pairs structurally. Heuristic: two threads (detected via `std::thread`, `std::jthread`, `permission_fork`, or substrate-spawn factories) accessing the same SPSC ring / shared resource. Flag the absence of `Permission<Tag>` discipline:
- "Two threads access `ring_` without `Permission<RingProducer>` / `Permission<RingConsumer>` split"
- "Bare `std::thread` spawn captures Linear value — use `permission_fork` instead per CLAUDE.md §IX"

**`crucible-cache-line-layout`** — For every struct with multiple `std::atomic` fields, check whether they're aligned to separate cache lines (`alignas(64)`). Cross-reference with threading annotations (`CRUCIBLE_HOT`, `[[gnu::hot]]`, `effects::Bg`). Flag false-sharing risks before they become 40× tail-latency surprises.

**`crucible-collision-preview`** — Parse `Fn<...>` template instantiations and synthetically run the §6.8 collision rules (fixy.md §24.2). When a pattern that WOULD trigger I002/L002/E044/M012/etc. is detected — before `safety/CollisionCatalog.h` is fully activated — warn at the source level with the same diagnostic the catalog would emit. Bridges the gap between today (placeholder ValidComposition) and the future (active catalog) without waiting for GAPS-005..018 to land.

**`crucible-mint-pattern-discipline`** — Per CLAUDE.md §XXI Universal Mint Pattern: every cross-tier composition factory must be named `mint_X` with a single `requires CtxFitsX<...>` concept gate, return `[[nodiscard]] constexpr noexcept`, return a concrete type. Audit every factory function in the substrate; flag deviations. Detect new factories in PR diffs that don't follow the pattern.

**Optional:** `crucible-row-hash-coverage` — verify that every wrapper in the canonical wrapper-nesting order (CLAUDE.md §XVI) has a `row_hash_contribution<W>` specialization. Fires when GAPS-028 (the row_hash sweep) is incomplete.

**CodeQL pilot path.** Several of these checks express more concisely as Datalog queries over a code database than as libtooling visitors. Pilot 2-3 of them as CodeQL queries in Week 12; if Datalog wins on conciseness + maintainability, migrate Layer 3 to CodeQL. GitHub provides CodeQL free for OSS via the github/codeql-action workflow. Decision deferred until pilot data exists.

### 3.5 Layer 4 — Policy enforcement

**`crucible-deny.toml`** — Manifest of forbidden constructs, mirrors the `cargo-deny` pattern.

```toml
[banned-headers]
# Headers banned project-wide
"<regex>" = { reason = "use Std.Regex or hand-roll", severity = "error" }
"<filesystem>" = { reason = "filesystem ops via cipher::cold layer", severity = "error" }

# Headers banned in hot-path TUs only
hot-only = ["<map>", "<unordered_map>", "<set>", "<unordered_set>"]

[banned-types]
"std::function<*>" = { in = "hot-path", reason = "use templated callable or function_ref", severity = "error" }
"std::regex" = { reason = "ban project-wide; 10-100x slower than alternatives", severity = "error" }
"std::vector<*>::reserve" = { reason = "code_guide.md §IV opt-out: signals wrong container choice", severity = "warning" }
"volatile" = { in = "concurrency", reason = "use std::atomic; volatile does not order", severity = "error" }
"reinterpret_cast" = { reason = "use std::bit_cast<T>", severity = "error" }

[banned-functions]
"malloc" = { in = "hot-path", severity = "error" }
"free" = { in = "hot-path", severity = "error" }
"new" = { in = "hot-path", severity = "error" }
"delete" = { in = "hot-path", severity = "error" }
"printf" = { in = "hot-path", severity = "error" }
"std::cout" = { in = "hot-path", severity = "error" }

[banned-flags]
"-ffast-math" = "kills BITEXACT discipline"
"-funsafe-math-optimizations" = "kills BITEXACT discipline"
"-fno-strict-aliasing" = "disables TBAA, big perf loss"

[license-requirements]
allow = ["MIT", "Apache-2.0", "BSD-3-Clause", "BSL-1.0"]
deny = ["GPL-*", "AGPL-*", "LGPL-*"]  # incompatible with Crucible licensing
```

**`crucible-signatures.json`** — Expected public-API surface of the substrate. Generated initially via reflection over the current substrate; manually curated thereafter.

```json
{
  "version": "2026-05-05",
  "substrate": {
    "include/crucible/Cipher.h": {
      "Cipher::store": {
        "signature": "auto Cipher::store(Linear<ScopedFile> stream, Refined<non_zero, ContentHash> hash, ...)",
        "expected_wrappers": ["Linear<ScopedFile>", "Refined<non_zero, ContentHash>"],
        "axioms": ["MemSafe", "DetSafe"],
        "doc_anchor": "code_guide.md §XVI"
      }
    }
  }
}
```

The lint tool diffs current source against expected. Drift fails CI with a named diagnostic identifying which wrapper went missing and pointing to the doc anchor. Equivalent to API-stability checks in shared libraries; here it's substrate-discipline stability.

**Per-PR policy hooks** (danger.js style — small Python scripts that read the PR diff):

```python
# .crucible-lint/policies/hot-path-bench-coverage.py
def check(pr_diff):
    hot_changes = pr_diff.files_matching("include/crucible/{TraceRing,MetaLog,Vigil}.h")
    bench_changes = pr_diff.files_matching("bench/bench_{trace_ring,meta_log,vigil}.cpp")
    if hot_changes and not bench_changes:
        return Violation(
            severity="warning",
            message="PR touches hot-path headers without updating bench coverage",
            doc_anchor="code_guide.md §VIII performance discipline"
        )
```

Other policy examples:
- "PR adds new wrapper but no neg-compile fixture per HS14" → block per CLAUDE.md §XVIII
- "PR adds new effect-row tag but no `crucible-deny.toml` update" → warn
- "PR modifies CMakePresets.json release flags" → require human approval
- "PR touches `safety/CollisionCatalog.h` but doesn't update fixy.md §24.2" → warn

Policies live in `.crucible-lint/policies/*.py` as standalone scripts. The driver invokes each in turn with the PR's diff context.

**Secrets scan.** `gitleaks` or `trufflehog` runs on every PR. Catches accidentally committed credentials, API keys, etc. Not Crucible-specific but free coverage.

### 3.6 Layer 5 — Aggregation + output

**SARIF as primary format.** Industry-standard JSON for static-analysis results. GitHub Code Scanning, GitLab, Sonar all consume it. Each layer's output is converted to SARIF and merged.

```bash
# Pseudocode for the driver:
infer-sarif build/infer.json > build/infer.sarif
clang-tidy-sarif build/clang-tidy.txt > build/clang-tidy.sarif
ast-grep-sarif build/ast-grep.json > build/ast-grep.sarif
crucible-policy-sarif build/policy.json > build/policy.sarif
sarif-merge build/*.sarif > build/crucible-lint.sarif
```

**CodeChecker** (Ericsson, open source). Aggregates SARIF, deduplicates diagnostics across analyzers, provides web UI for triage. Stores history of runs, baseline comparison. Worth integrating once 3+ analyzers are running. Optional for small teams.

**reviewdog.** General-purpose CI lint integration. Reads SARIF, posts inline review comments on the PR. Supports severity-based filtering.

```bash
reviewdog -reporter=github-pr-review -f=sarif < build/crucible-lint.sarif
```

**Terminal output.** For local development:

```
crucible-lint: 23 issues found in 8 files

include/crucible/Cipher.h:128:10: warning [CRUC-W-Linear-002]
   FILE* stream parameter — should be Linear<ScopedFile>
   Doc: https://crucible-lint.dev/rules/CRUC-W-Linear-002
   Suggested fix: Linear<ScopedFile> stream
   GAPS task: PROD-WRAP-4 (#533)

include/crucible/MerkleDag.h:336:5: warning [CRUC-W-Refined-001]
   ContentHash field with implicit non-zero invariant
   Doc: https://crucible-lint.dev/rules/CRUC-W-Refined-001
   Suggested fix: Refined<non_zero, ContentHash> merkle_hash
   GAPS task: PROD-WRAP-6 (#535)

...

Summary:
  Errors:    0   (CI gate: pass)
  Warnings: 18
  Info:      5
  Suggestions: 0

Run with --baseline to update lint-baseline.json
Run with --fix to apply machine-applicable fixes (12 of 23)
```

**LSP via efm-langserver.** Wraps any terminal linter as an LSP server. Editors with LSP support (clangd-via-extension, neovim's `vim.lsp`, vscode) consume the diagnostics and render them as squiggles. Hovering shows the rule's rationale + doc URL + suggested fix. No custom LSP server needed; ~30 lines of efm config.

```yaml
# ~/.config/efm-langserver/config.yaml
languages:
  cpp:
    - lint-command: 'crucible-lint --format=efm --stdin'
      lint-stdin: true
      lint-formats:
        - '%f:%l:%c: %trror: %m'
        - '%f:%l:%c: %tarning: %m'
        - '%f:%l:%c: %tnfo: %m'
```

### 3.7 Layer 6 — Migration mode

**Tool:** Coccinelle (`spatch`) — semantic-patch language designed for Linux-kernel-scale refactors. Patterns express "match this construct, transform to that construct" with structural awareness. C++ support is partial but improving; for the patterns Crucible needs (wrap parameter type, rename field, add include, update call sites), C++ coverage is sufficient.

**Per-WRAP-* refactor:** each task in the GAPS WRAP-* corpus becomes a `.cocci` semantic patch.

```cocci
// rules/migration/PROD-WRAP-4.cocci
// Wrap FILE* parameters as Linear<ScopedFile>; update call sites.

@rule1@
identifier fn;
@@
- void fn(FILE* stream)
+ void fn(Linear<ScopedFile> stream)
{
  ...
}

@rule2 depends on rule1@
identifier fn;
expression e;
@@
- fn(fopen(e, "rb"))
+ fn(Linear<ScopedFile>{fopen(e, "rb")})
```

Run via `crucible-lint --fix --batch=PROD-WRAP-4`:

1. spatch applies the patch across the codebase
2. Generates a single unified diff
3. Operator reviews via `crucible-lint --fix --review` (opens an ephemeral PR-style preview)
4. If approved, the diff lands as one commit with the GAPS task ID in the message

For per-rule trivial fix-its (rename one identifier, add one include), use clang-tidy `--fix` directly. Coccinelle is for refactors that touch signatures + call sites + tests atomically.

### 3.8 Layer 7 — Documentation site

**Tool:** any static-site generator (mdbook, hugo, custom Python). Auto-generated from the rule corpus on every release.

**Per-rule page schema:**

```
URL: https://crucible-lint.dev/rules/CRUC-W-Linear-002

Rule: CRUC-W-Linear-002 — FILE* parameter should be Linear<ScopedFile>

Severity: warning
Category: linear-ownership
Doc anchor: code_guide.md §XVI Linear discipline
Related GAPS task: PROD-WRAP-4 (#533)
Applicable: machine-applicable

Rationale:
  RAII-bound file handles prevent leak on early return / exception unwind.
  Linear<ScopedFile> deletes copy, requires explicit consume via std::move.
  CLAUDE.md axiom 4 (MemSafe) and axiom 7 (LeakSafe) both apply.

Bad:
  void store(FILE* stream, ContentHash hash);

Good:
  void store(Linear<ScopedFile> stream, Refined<non_zero, ContentHash> hash);

Suppression:
  // crucible-lint-disable: CRUC-W-Linear-002 — legacy ABI, blocked on PROD-WRAP-4

References:
  - code_guide.md §XVI Linear discipline
  - safety/Linear.h
  - GAPS #533 PROD-WRAP-4 task description
  - CLAUDE.md §II axioms 4, 7
```

The diagnostic emitted by the tool includes the URL. Clicking through goes to the page. Same pattern as Rust's clippy lint reference (`https://rust-lang.github.io/rust-clippy/master/`).

---

## 4. Configuration

**Single config file at repo root:** `.crucible-lint.toml`.

```toml
# Root-level config
version = "1"

[layers]
# Opt-in / opt-out per layer
infer = true
clang-static-analyzer = true
clang-tidy-stock = true
header-include-cleaner = true
ast-grep-rules = true
clang-tidy-custom = true
policy-enforcement = true
codechecker = false  # opt in once 3+ analyzers settled

[paths]
include = ["include/**", "src/**", "test/**", "bench/**"]
exclude = ["vendor/**", "third_party/**", "build/**"]

[severity]
# Project-wide defaults
fail_on = "error"
warn_on = ["warning", "info"]

# Per-rule overrides
[severity.overrides]
"CRUC-W-Linear-002" = "error"  # promote to blocking after PROD-WRAP-4 lands
"CRUC-W-Naming-001" = "info"   # naming sweep deferred to Q3

[baseline]
file = "lint-baseline.json"
mode = "fail-on-new"  # only fail CI on diagnostics not in baseline

[suppressions]
require_justification_for = ["error"]
audit_command = "crucible-lint --report-suppressions"

[output]
formats = ["terminal", "sarif", "github-actions"]
sarif_path = "build/crucible-lint.sarif"

[ci]
provider = "github-actions"
reviewdog_reporter = "github-pr-review"
post_pr_comments = true
```

**Per-file pragma overrides:** in-source.

```cpp
// crucible-lint-disable: CRUC-W-Linear-002 — legacy ABI, blocked on PROD-WRAP-4 (#533)
FILE* legacy_param;

// crucible-lint-disable-next-line: CRUC-W-Naming-001
int x = 0;  // loop induction variable in spec context

// crucible-lint-disable-file: CRUC-W-FalseShare-001 — counter-only struct, no cross-thread access
```

Suppressions of `error`-tier rules **require** justification text after `—`. CI rejects unjustified suppressions of error-tier rules with a meta-diagnostic. Periodic `crucible-lint --report-suppressions` audits the suppression manifest for staleness.

---

## 5. Baselining

**Problem:** initial scan of the 60K-file Crucible codebase by 200 fresh rules produces ~100K diagnostics. Without baselining, the tool causes triage death and gets disabled.

**Solution:** baseline-aware "fail on NEW only" mode from Day 1.

**Mechanism:**

1. **Initial baseline:** run `crucible-lint --baseline-init` once. Snapshots all current diagnostics into `lint-baseline.json`. Commits the baseline file to the repo.

2. **Per-PR check:** `crucible-lint --check` computes the symmetric difference between current diagnostics and baseline. Only NEW diagnostics fail CI. Existing diagnostics ("known noise") don't.

3. **Periodic cleanup:** dedicated PRs reduce the baseline. As GAPS WRAP-* tasks land, the corresponding baseline entries get removed. The baseline shrinks over months as the codebase improves.

4. **Hash-keyed baseline:** entries are keyed by `(file, rule_id, normalized_message)` — line numbers are NOT part of the key, so reformatting the file doesn't invalidate the baseline.

**`lint-baseline.json` schema:**

```json
{
  "version": "1",
  "generated": "2026-05-05T18:00:00Z",
  "snapshots": [
    {
      "file": "include/crucible/Cipher.h",
      "rule": "CRUC-W-Linear-002",
      "message_hash": "a3f2b7c1...",
      "added": "2026-05-05",
      "gaps_task": "PROD-WRAP-4"
    }
  ],
  "summary": {
    "total": 87234,
    "by_severity": { "error": 0, "warning": 65000, "info": 22234 },
    "by_category": { "linear-ownership": 28000, "strong-types": 19000, ... }
  }
}
```

---

## 6. CMake integration

**Module:** `cmake/CrucibleLint.cmake` provides two macros.

```cmake
include(CrucibleLint)

# Development mode — runs in foreground, terminal output
crucible_lint(
  TARGET crucible_core
  CONFIG ${CMAKE_SOURCE_DIR}/.crucible-lint.toml
)

# CI mode — full pipeline, SARIF + reviewdog output, baseline-aware
crucible_lint_ci(
  TARGET crucible_core
  CONFIG ${CMAKE_SOURCE_DIR}/.crucible-lint.toml
  BASELINE ${CMAKE_SOURCE_DIR}/lint-baseline.json
  SARIF_OUTPUT ${CMAKE_BINARY_DIR}/lint.sarif
  REVIEWDOG_REPORTER github-pr-review
  FAIL_ON error
)

# Aggregator target — runs lint on all targets in dependency order
add_custom_target(crucible_lint_all
  DEPENDS lint_crucible_core lint_crucible_test lint_crucible_bench
)
```

**`crucible_lint(TARGET)` behavior:**
1. Reads `compile_commands.json` for the target
2. Filters to source files matching the target's `SOURCES` property
3. Invokes the configured layers in dependency order (Infer first, ast-grep last)
4. Caches per-file results in `.crucible-lint-cache/`
5. Aggregates output, deduplicates, sorts by file:line
6. Emits in the requested format
7. Returns non-zero exit if any rule at `FAIL_ON` severity or above fired

**Caching:** `.crucible-lint-cache/<analyzer>/<file_hash>_<rule_set_hash>.json`. PR-mode runs only on changed files + their direct dependents (computed via the compdb's include graph). Cache TTL: 7 days, auto-cleanup on cache miss.

---

## 7. CI integration (GitHub Actions example)

```yaml
# .github/workflows/crucible-lint.yml
name: crucible-lint
on:
  pull_request:
    branches: [main]
  push:
    branches: [main]

permissions:
  contents: read
  pull-requests: write
  security-events: write  # for SARIF upload

jobs:
  lint:
    runs-on: ubuntu-latest
    steps:
      - uses: actions/checkout@v4
        with:
          fetch-depth: 0  # for baseline diff

      - name: Set up GCC 16
        uses: ./.github/actions/setup-gcc16

      - name: Configure
        run: cmake --preset default

      - name: Install ast-grep
        uses: ast-grep/setup-action@v1

      - name: Install Infer
        run: |
          curl -L https://github.com/facebook/infer/releases/download/v1.1.0/infer-linux64-v1.1.0.tar.xz | tar xJ
          echo "$PWD/infer-linux64-v1.1.0/bin" >> $GITHUB_PATH

      - name: Run crucible-lint
        run: cmake --build build --target crucible_lint_ci_all

      - name: Upload SARIF to Code Scanning
        uses: github/codeql-action/upload-sarif@v3
        with:
          sarif_file: build/lint.sarif
          category: crucible-lint

      - name: Post PR review comments via reviewdog
        if: github.event_name == 'pull_request'
        uses: reviewdog/action-suggester@v1
        with:
          tool_name: crucible-lint
          format: sarif
          input: build/lint.sarif
          fail_on_error: true
          level: error  # only `error` tier blocks merge
```

**Per-PR experience:**
- Review comments materialize inline on the PR diff
- "Files changed" tab shows annotations on lint violations
- "Security" tab shows the SARIF results indexed by category
- Status check shows pass/fail; merge blocked on any new error-tier violation

---

## 8. Implementation roadmap

```
Phase                                       Weeks   Output
──────────────────────────────────────────  ─────   ──────────────────────────────────────
Layer 0: compdb + driver scaffolding        1       Working `crucible-lint` binary;
                                                     terminal output; .crucible-lint.toml
                                                     parser; CMake integration

Layer 1: Infer + clang-tidy stock +         1       Free coverage from mature analyzers
 header-include-cleaner integration                  with baseline-aware "fail on new"

Layer 2: ast-grep rules — initial 50        2       50 highest-impact rules covering
 highest-impact rules                                strong types, banned patterns, naming,
                                                     hot-path

Layer 4: ripgrep banned-strings +           1       crucible-deny.toml + signature manifest
 crucible-deny.toml + Lakos hierarchy +              + per-PR policy hooks
 per-PR policy scripts

Layer 5: SARIF emit + reviewdog +           0.5     Production CI integration
 GitHub Actions wrapper                              fully wired

------ Useful subset complete: Week 5 -----------------------------------------------------

Layer 2: full ast-grep corpus               4       Auto-generate ~150 WRAP-* rules
 (200 rules total)                                   from GAPS task list; manual review;
                                                     hand-author the remaining ~30%

Layer 3: 3 of 5 semantic checks             4       effect-row-flow + linear-flow +
 (1.5 weeks each)                                    cache-line-layout

Layer 7: documentation site                 1       Auto-generated rule reference website

------ Phase 2 complete: Week 14 ----------------------------------------------------------

Layer 3: remaining 2 semantic checks         2       permission-discovery + collision-preview

Layer 5: CodeChecker integration             1       Aggregator with web UI for triage

Layer 6: Coccinelle migration mode           2       Semantic-patch generation for the
                                                     most common WRAP-* refactors; --fix
                                                     --batch mode

CodeQL pilot                                 1       Pilot 10 queries; decide on Layer 3
                                                     migration

Documentation polish + LSP wrapper           1       efm-langserver config + editor docs

------ Full v1 complete: Week 21 ----------------------------------------------------------
```

**Useful subset (Week 5):** catches ~70% of the WRAP-* opportunities, all banned patterns, all Infer/RacerD findings. Triage-scale manageable from PR 1. Production-grade CI integration. This is the deliverable threshold worth targeting first.

**Full v1 (Week 21):** comparable to clippy / clang-tidy / Infer in user experience. Migration mode handles the substrate-wide refactor backlog as reviewable batched PRs. Documentation site auto-generated. Editor integration via standard LSP.

**Compared to fixyc:** ~25-30% the calendar cost (5 months vs 12-24 months), with mature-tool coverage from Day 5 instead of Year 1.

---

## 9. Decision log — tools chosen and rejected

| Tool | Status | Reason |
|---|---|---|
| **Infer (Pulse + RacerD)** | Adopted (Layer 1) | Best-in-class C++ memory safety + race detection; open source; production-tested at Meta scale; SARIF output |
| **clang-tidy (stock + custom)** | Adopted (Layers 1, 3) | Industry standard; integrates with clangd; supports fix-its; mature severity model; custom checks via libtooling |
| **clang static analyzer** | Adopted (Layer 1) | Path-sensitive symbolic execution; complements Infer's coverage; free with clang |
| **header-include-cleaner** | Adopted (Layer 1) | Built into clangd 17+; enforces CLAUDE.md §XV IWYU discipline at zero authoring cost |
| **ast-grep** | Adopted (Layer 2) | YAML rules; CLAUDE.md preferred AST tool; Tree-sitter-cpp coverage sufficient; rapid iteration |
| **clang-query** | Adopted (rule prototyping) | REPL for AST matchers; cuts rule-authoring time in half |
| **ripgrep** | Adopted (Layer 4) | Fastest grep; trivial banned-pattern detection |
| **gitleaks / trufflehog** | Adopted (Layer 4) | Secrets scan; standard CI hygiene |
| **Coccinelle / spatch** | Adopted (Layer 6) | Semantic patches for batch refactors; Linux kernel-scale precedent; better than per-rule fix-it composition for cross-cutting changes |
| **SARIF** | Adopted (Layer 5) | Industry-standard format; consumed by GitHub / GitLab / Sonar / CodeChecker |
| **reviewdog** | Adopted (Layer 5) | Multi-format CI integration; saves writing custom GitHub Actions integration |
| **CodeChecker** | Adopted (Layer 5, optional) | Aggregator with web UI; useful once 3+ analyzers running |
| **efm-langserver** | Adopted (Layer 5) | Wraps terminal linter as LSP server; saves writing custom LSP |
| **CodeQL** | Pilot only | Datalog learning curve; GitHub-flavored workflow integration; may simplify Layer 3 if pilot succeeds; decision deferred to Week 17 |
| **PVS-Studio / Coverity / Klocwork** | Rejected | Commercial, vendor lock-in, license cost |
| **cppcheck** | Rejected | Less precise than clang-tidy; clang-tidy supersedes for any project with compdb |
| **Semgrep** | Rejected | Weaker C++ grammar than ast-grep; managed cloud aspect adds friction |
| **comby** | Rejected | Structural search/replace; ast-grep covers same use cases with better C++ support |
| **Custom GCC plugin** | Rejected | GCC plugin API harder than clang's libtooling for the same outcome |
| **CBMC / SeaHorn / Frama-C** | Rejected | Formal verification; out of scope per the no-SMT decision |

---

## 10. Maintenance discipline

**Rule additions** require:
1. YAML rule (Layer 2) or C++ check (Layer 3)
2. Positive + negative fixture pair in `tests/fixtures/`
3. Documentation page entry in `tools/crucible-lint/rules/<rule-id>.md`
4. Severity assignment + doc anchor + GAPS-task linkage (if applicable)
5. CI passes including the rule's self-test

**Rule deprecation** requires:
1. Marking the rule `deprecated` in the YAML/cpp source
2. Sunset PR removing the rule after 2 release cycles (at minimum)
3. Migration note for users who had the rule active

**Severity escalation** (warning → error) requires:
1. Documented violation count below a threshold (e.g., < 100 violations across the codebase)
2. Period of warning-tier exposure (≥ 1 month) for users to fix
3. PR with the override + audit confirming all current violations are either fixed or explicitly suppressed

**Suppression audit:** monthly automated job runs `crucible-lint --report-suppressions --stale-threshold=180d` and files an issue listing suppressions older than 180 days that should be re-evaluated. Forces periodic review of "known-acceptable" exceptions.

**Rule corpus health metrics:**
- Rule count per category
- Average rule age
- Rule fire rate per file (high fire rate may indicate over-strictness)
- Suppression rate per rule (high suppression rate may indicate the rule is wrong)
- Time-to-fix from first fire to suppression-or-resolution

Tracked in `tools/crucible-lint/health-dashboard.html`, regenerated weekly.

---

## 11. Repository layout and build integration

`crucible-lint` lives **in-tree** at `crucible/tools/crucible-lint/`, not in a separate repo. The decision is decisive for Crucible's situation: single team, monorepo discipline, rule corpus tightly coupled to the substrate's wrapper types, no external library consumers. Splitting into a second repo would add cross-repo coordination overhead and version-skew tax with no proportionate benefit. The reverse path (split now, merge later) is much harder than the forward path (in-tree now, extract later if a need ever emerges).

### 11.1 Why in-tree

1. **Substrate-coupled rules.** The corpus references `Linear<>`, `Refined<>`, `Tagged<>`, `Permission<>`, `Session<>`, the WRAP-* GAPS task IDs, the §6.8 collision codes (I002 / L002 / E044 / etc.). Every substrate API change ripples into the rule corpus. Atomic PRs touching substrate + rules + neg-compile fixtures together are vastly easier in-tree.

2. **No version skew.** With a separate repo, you'd track "rule corpus version 1.4 works against substrate version 0.18-0.21" — a real maintenance tax. In-tree, the rule corpus is exactly compatible with the surrounding substrate by construction.

3. **Discoverable.** A contributor cloning the repo finds the tool immediately. No "btw, also clone X" instruction.

4. **Native CI integration.** No cross-repo workflow wiring. The `crucible_lint()` CMake target is just another build product; the GitHub Actions workflow runs against the same checkout as the substrate build.

5. **Easy to extract later.** Keeping `tools/crucible-lint/` as a self-contained directory with its own CMakeLists, dependencies, and tests preserves the option to extract later if Crucible ever ships as a library with external consumers.

### 11.2 Directory layout

```
crucible/
├── include/, src/, test/, bench/, vis/   ← substrate (existing)
├── misc/                                  ← design docs (existing)
│   ├── fixy.md (deferred design)
│   ├── ci-pipeline.md (this design)
│   └── ...
├── cmake/
│   └── CrucibleLint.cmake                 ← integration macros
└── tools/
    └── crucible-lint/
        ├── CMakeLists.txt                  ← own build target
        ├── CONTRIBUTING.md                 ← rule-authoring guide
        ├── README.md                       ← user-facing intro
        ├── driver/                         ← Python orchestrator (~1500 LOC)
        │   ├── crucible_lint.py            ← entrypoint
        │   ├── layers/                     ← per-layer adapters (Infer,
        │   │                                  clang-tidy, ast-grep, policy)
        │   ├── output/                     ← SARIF / terminal / reviewdog / LSP
        │   ├── cache/                      ← per-file analysis-result cache
        │   └── tests/                      ← driver unit tests
        ├── rules/
        │   ├── structural/                 ← ast-grep YAML (~200 rules)
        │   │   ├── strong-types.yml
        │   │   ├── linear-ownership.yml
        │   │   ├── refinements.yml
        │   │   ├── concurrency.yml
        │   │   ├── banned-patterns.yml
        │   │   ├── naming.yml
        │   │   ├── hot-path.yml
        │   │   ├── wrappers.yml
        │   │   └── sessions.yml
        │   └── catalog.json                ← generated rule index
        ├── checks/                         ← clang-tidy custom plugins (C++)
        │   ├── CMakeLists.txt
        │   ├── crucible-effect-row-flow.cpp
        │   ├── crucible-linear-flow.cpp
        │   ├── crucible-permission-discovery.cpp
        │   ├── crucible-cache-line-layout.cpp
        │   ├── crucible-collision-preview.cpp
        │   └── crucible-mint-pattern-discipline.cpp
        ├── policies/                       ← danger-style PR hooks (Python)
        │   ├── hot-path-bench-coverage.py
        │   ├── new-wrapper-needs-fixture.py
        │   ├── lakos-layer-hierarchy.py
        │   └── signature-manifest-drift.py
        ├── manifests/
        │   ├── crucible-deny.toml          ← banned headers / types / functions
        │   └── crucible-signatures.json    ← expected substrate signatures
        ├── docs-site/                      ← auto-generated rule reference
        │   ├── generator.py
        │   └── templates/
        ├── tests/
        │   ├── fixtures/                   ← per-rule before/after .cpp pairs
        │   │   └── structural/
        │   │       └── strong-types/
        │   │           ├── neg_raw_op_idx.cpp     ← should fire
        │   │           └── pos_proper_op_index.cpp ← should not fire
        │   └── self_test.py                ← runs every rule against its fixtures
        ├── third_party/
        │   ├── infer-version.txt           ← pinned Infer release hash
        │   ├── ast-grep-version.txt
        │   ├── reviewdog-version.txt
        │   └── INSTALL.md                   ← provenance + verification commands
        └── .crucible-lint-self.toml        ← config for self-linting (dogfood)
```

### 11.3 Build behavior

The lint tool is **opt-in** via CMake. Default `cmake --preset default && cmake --build` does NOT build it (substrate builds stay fast). Three opt-in paths:

```bash
# Explicit single-target build:
cmake --preset default -DCRUCIBLE_BUILD_LINT=ON
cmake --build --preset default --target crucible-lint

# Dedicated lint preset (substrate + lint together, for users who want both):
cmake --preset lint
cmake --build --preset lint

# CI builds it as part of the lint workflow:
cmake --preset lint-ci
cmake --build --preset lint-ci --target crucible_lint_ci_all
```

Root `CMakeLists.txt` pulls the tool in conditionally:

```cmake
option(CRUCIBLE_BUILD_LINT "Build crucible-lint static analysis tool" OFF)
if(CRUCIBLE_BUILD_LINT OR DEFINED ENV{CI})
  add_subdirectory(tools/crucible-lint)
endif()
```

Substrate developers don't pay the lint-build cost in their normal cycle. CI always builds it. Pre-built artifacts (the lint binary + cached third-party deps) are uploaded to GitHub Container Registry per release tag for users who want the tool without building from source.

### 11.4 Dogfooding

`tools/crucible-lint/.crucible-lint-self.toml` enables a strict subset of rules; the lint tool's CI runs the lint tool against its own source. If `crucible-lint` cannot pass its own checks, it does not ship. This catches both regressions in the rule corpus (a rule that misfires on its own implementation) and substrate-discipline drift in the tool's own code (the tool is C++26 substrate consumer like everything else).

### 11.5 Release coupling

The lint tool releases when the substrate releases. Tag `v2026.05.0` covers substrate + lint together. Users who want a specific lint rule fixed upgrade Crucible. No separate version compatibility matrix to maintain.

If a future need to extract emerges (e.g., Crucible evolves to be shipped as a library with external consumers), the in-tree `tools/crucible-lint/` directory becomes the source for an external package, with CI publishing both. Future-extraction path is preserved; today-extraction is not done.

---

## 12. Operational considerations

### 12.1 Performance budgets

Stated budgets so the team knows when the tool is regressing:

```
Operation                          Target    Hard ceiling   Action if exceeded
────────────────────────────────   ───────   ────────────   ─────────────────────
Single-file editor scan            ≤200ms    500ms          profile + fix or
 (incremental, on save)                                      reduce rule scope
                                                              for the file's tier
Per-PR scan (changed files +       ≤90s      3min           cache hit rate audit;
 direct dependents, 16-core CI)                               consider sharding
Full-codebase scan (cold cache,    ≤15min    30min          parallelize layer 1
 16-core CI)                                                  + layer 2 across cores
Layer 1 (Infer + clang-tidy        ≤5min     10min          escalate to Infer
 stock + scan-build)                                          team if regression
Layer 2 (200 ast-grep rules)       ≤30s      90s            rule profiling;
                                                              prune slow patterns
Layer 3 (5 clang-tidy custom        ≤2min     5min           per-check profiling
 plugins)
Layer 4 (policy hooks)             ≤10s      30s            policy script profile
Layer 5 (aggregation + SARIF       ≤5s       15s            schema-diff cost;
 emission)                                                    reduce JSON depth
```

A regression in measured latency on the lint suite is investigated like any other regression — root-cause first, then fix. Variance >10% across runs flags throttling / contention; investigate before accepting results.

### 12.2 Inter-rule conflict resolution

Two rules can fire on the same line with overlapping or contradictory fix suggestions. Resolution policy:

Each rule declares a `precedence_class` in its YAML / C++ metadata:

```
security      > correctness > discipline > style
```

When multiple rules fire on the same line range:
1. Highest precedence_class wins; lower-class fixes are reported as informational
2. Within the same class, the rule with the more specific match (longer pattern, narrower scope) wins
3. Within the same class + specificity, rules are reported in alphabetical ID order
4. Fix-its from non-winning rules are reported in diagnostic output but NOT auto-applied in `--fix` mode (operator must resolve manually)

The precedence table is documented in `tools/crucible-lint/CONTRIBUTING.md`. Adding a new rule requires declaring its class; reviewer rejects PRs that don't.

### 12.3 Ownership and governance

**Rule corpus ownership:** any committer may propose a new rule; two-reviewer approval to land. Reviewers verify (a) the rule has positive + negative fixtures, (b) the doc anchor cites a real CLAUDE.md / fixy.md / code_guide.md section, (c) the precedence_class is appropriate, (d) the rule self-test passes, (e) running the rule on the codebase produces a manageable diagnostic count or has an accompanying baseline-update PR.

**False-positive triage:** users file issues with `lint-fp` label citing the rule ID + the source location + why the rule is wrong. Triage owner validates within 5 working days. Outcomes: fix the rule, narrow the rule's scope, accept the case as a documented edge case, or revert the rule.

**Rule disputes:** "rule X is too strict / wrong / harmful" disputes resolve via the standard architectural-decision process. Disputed rules drop to `severity: info` (non-blocking) while the dispute is open. Resolution within 30 days; rule either lands at `warning`/`error`, drops to `info` permanently, or is removed.

**Escalation path:** for blocking disputes affecting a release, the rule is temporarily disabled via the project-wide `severity_overrides` mechanism with a tracking issue.

### 12.4 Migration playbook for new rules

When a new rule lands and produces N violations on the codebase:

| Violations | Procedure |
|---|---|
| ≤10 | Land the rule + fix all violations in the same PR. No baseline entry needed. |
| 11-100 | Land the rule + fix in a follow-up PR within 2 weeks. Auto-baseline current violations as "accepted noise" with a tracking task per file. CI fails only on NEW violations meanwhile. |
| 101-1000 | Land the rule with auto-baseline. File a single tracking task for the sweep. Schedule the sweep within the next quarter. CI fails only on NEW violations. |
| >1000 | Drop the rule's severity to `info` (non-blocking) for 30 days. Run a baseline-population PR. Then re-promote to `warning` after the sweep PR lands. Avoids triage death. |

The auto-baseline mechanism guarantees Day 1 of every new rule is friction-free for the rest of the team. Without this, every new rule causes "everyone's PRs are red" syndrome and the tool gets disabled.

### 12.5 Versioning and deprecation

**Stable IDs forever.** Rule ID `CRUC-W-Linear-002` always means "FILE* parameter should be Linear<ScopedFile>". The semantics never change in place; if the rule's intent changes, it gets a new ID and the old one is deprecated.

**Deprecation procedure:**
1. Mark the rule `deprecated: true` in the YAML/cpp source
2. Diagnostic output includes a deprecation notice + pointer to the replacement rule (if any)
3. Sunset PR removes the rule after 2 release cycles (≥6 months)
4. Migration note in the release CHANGELOG

**Severity escalation (warning → error):** requires (a) violation count < 100 across the codebase, (b) ≥30 days of warning-tier exposure for users to fix, (c) PR with the override + audit confirming all current violations are either fixed or explicitly suppressed.

**Severity demotion (error → warning):** allowed any time as a tactical relief. Requires a tracking issue for the future re-promotion.

### 12.6 Cost model and offline support

**CI compute estimate:** ~5 minutes of 16-core runner time per PR (layered analyzers run in parallel where possible). At GitHub Actions standard runner pricing (~$0.008/min × 16 cores ≈ $0.13/PR). For a Crucible PR volume of ~50 PRs/week, ~$30/month — negligible.

**Full-codebase weekly scan:** ~15 minutes on 16-core. Run nightly to catch baseline drift. ~$2/week.

**Offline / airgapped support:** required for bare-metal Crucible deployments without external network access. All third-party tool versions are pinned in `tools/crucible-lint/third_party/*-version.txt` with SHA256 hashes. CI mirrors download a vendored archive of all dependencies (Infer release tarball, ast-grep binary, reviewdog binary) into an internal artifact registry. Airgapped sites pull from the internal mirror; the lint tool never reaches out to the internet at runtime.

The pinned versions are upgraded via dedicated PRs that update both the version files and the offline-mirror artifacts atomically. CI verifies downloaded hashes match pinned values before accepting an upgrade PR.

### 12.7 Anti-pattern catalog (rules NOT to add)

Things explicitly NOT to do, to keep the tool sane:

1. **Do not add a rule that requires hand-coding the entire AST visit.** If ast-grep cannot express the pattern, escalate to libclang custom check (Layer 3) — but only if it's load-bearing. Adding 50 lines of YAML is fine; adding 500 lines of C++ is a serious commitment that requires reviewer scrutiny.

2. **Do not lower a rule's severity to silence noise.** Either fix the rule (it was wrong), accept the noise via baseline (it was right but inconvenient), or remove the rule (it was wrong AND inconvenient). Severity is the user signal; lowering it because "users complained" is rule capture.

3. **Do not add a rule that depends on the file's name or path heuristically.** Path-based dispatch is fragile and surprising. If a rule needs to fire only on hot-path files, the dispatch should be via the `[[gnu::hot]]` annotation or the `CRUCIBLE_HOT` macro, not via "if path contains TraceRing.h."

4. **Do not add rules whose primary effect is style preference.** Crucible-lint enforces correctness, safety, and discipline. Style choices (brace placement, whitespace) belong in clang-format. Naming discipline (telling-word predicates) is correctness because it affects readability + grep-ability; tab-vs-space is not.

5. **Do not add a rule that requires per-project configuration to be useful.** Rules should be ON by default for the project's policy. Users disable per-rule via suppressions, not by configuring "what does this rule actually mean."

6. **Do not add a rule that fires on auto-generated code.** Generated code (fbs / proto / reflection-emitted) lives outside the substrate-discipline scope. The default `paths.exclude` glob list handles this; rules must not assume they're never run on generated code.

### 12.8 Failure modes and resilience

**Analyzer crash (Infer / clang-tidy / scan-build).** The driver isolates per-analyzer execution; one crash does not poison the others. Crash output is captured to `build/crucible-lint-crash-<analyzer>.log` and surfaced as a meta-diagnostic ("layer N crashed; rerun with `--debug-layer N` for details"). CI does not fail on crash unless `--strict-crash` is set; default is "report and continue."

**Cache corruption.** `.crucible-lint-cache/` is auto-invalidated on cache-key mismatch. Manual nuke: `rm -rf .crucible-lint-cache/`. If repeated corruption: file an issue and run with `--no-cache`.

**Tool version mismatch.** Pinned versions in `third_party/*-version.txt` are checked at startup. Mismatch fails fast with a clear message: "Infer version 1.0.5 found; expected 1.1.0. Run scripts/install-lint-deps.sh to upgrade."

**Network unavailable for fresh download.** If `--offline` is set or network probe fails, the tool requires pre-installed dependencies. No silent partial functionality.

**Flake handling.** Some checks (especially Layer 1 path-sensitive analyzers) can be flaky on parallel runs. The driver retries up to 2 times per crash; persistent crash escalates to meta-diagnostic. CI's GitHub Actions workflow has an outer retry policy of 1 retry on lint-job failure (catches transient runner issues).

### 12.9 Hot-fix exemption

For emergency security fixes that legitimately must land before lint compliance:

```bash
git commit -m "FIX: emergency security patch for CVE-XXXX

crucible-lint-bypass: SEC-2026-001 — see incident #4521 for justification
"
```

The CI workflow recognizes the `crucible-lint-bypass:` trailer in the commit message + the bypass token (must be a real incident ID format) and allows the merge. Bypass usage is logged to a separate audit trail (`tools/crucible-lint/bypass-audit.log` updated by CI on each merge); periodic review verifies bypasses correspond to real incidents.

Bypass does NOT silence the diagnostics — they still appear in PR review comments — it only removes the merge-blocking gate. Post-merge follow-up tasks are auto-filed for each suppressed violation.

---

## 13. Day-1 bootstrap and starter ruleset

### 13.1 Day-1 commands

The exact reproducible recipe to get `crucible-lint` working on a fresh checkout:

```bash
# 1. Clone Crucible (assumes you already have GCC 16 + CMake 3.28+)
git clone https://github.com/crucible/crucible.git
cd crucible

# 2. Install pinned third-party dependencies
./tools/crucible-lint/scripts/install-deps.sh
# Downloads + verifies Infer (pinned), ast-grep (pinned), reviewdog (pinned),
# scan-build (from system clang). Installs to ~/.crucible-lint/bin/.

# 3. Configure the build with lint enabled
cmake --preset lint
# This preset sets CRUCIBLE_BUILD_LINT=ON and configures the layered
# pipeline. Substrate + lint tool both built.

# 4. Build the lint tool
cmake --build --preset lint --target crucible-lint

# 5. Generate the compilation database (already done by the preset, but
#    explicit for clarity)
ls build/lint/compile_commands.json

# 6. Initialize the baseline from current state
./build/lint/crucible-lint --baseline-init --output lint-baseline.json
# Snapshots all current diagnostics. Commit this file to the repo.
git add lint-baseline.json
git commit -m "chore: initial crucible-lint baseline"

# 7. Run the linter on a single file (smoke test)
./build/lint/crucible-lint include/crucible/Cipher.h

# 8. Run the linter on a PR-style diff
./build/lint/crucible-lint --pr-mode --base-ref main

# 9. Run the full suite
cmake --build --preset lint --target crucible_lint_ci_all
```

**Expected first-run output:** ~5-30 thousand diagnostics from Layers 1-2 against the existing 60K-file codebase. All baselined. Future PRs trigger only on NEW violations — typical PR triggers 0-10 diagnostics.

**Expected install-deps.sh duration:** ~5 minutes (downloads ~500MB of analyzer binaries + verifies hashes).

**Expected first-build duration of crucible-lint:** ~3 minutes (clang-tidy custom plugins are the slowest component).

### 13.2 Day-1 starter ruleset (10 rules)

The ten rules to ship in Phase 1, picked because each catches a real bug class already documented in the GAPS task list and each has a clear positive + negative fixture:

| ID | Rule | Severity | Tier | GAPS link |
|---|---|---|---|---|
| **CRUC-E-Banned-001** | `std::function` declared in scope marked `CRUCIBLE_HOT` or `[[gnu::hot]]` | error | banned-patterns | code_guide §IV opt-out |
| **CRUC-E-Banned-002** | `reinterpret_cast` used (use `std::bit_cast<T>`) | error | banned-patterns | code_guide §III opt-out |
| **CRUC-E-Banned-003** | `volatile` declared on a non-MMIO type (use `std::atomic`) | error | banned-patterns | code_guide §III opt-out |
| **CRUC-W-Linear-002** | `FILE*` or `std::FILE*` parameter (use `Linear<ScopedFile>`) | warning | linear-ownership | PROD-WRAP-4 (#533) |
| **CRUC-W-StrongID-001** | `uint32_t` field named `op_idx` / `schema_hash` / `slot_id` / `node_id` / `callsite_hash` (use the corresponding strong ID from Types.h) | warning | strong-types | code_guide §II.2 TypeSafe |
| **CRUC-W-FalseShare-001** | `std::atomic<T>` field declared adjacent to another `std::atomic<T>` field without `alignas(64)` | warning | concurrency | code_guide §IX false-sharing trap |
| **CRUC-W-Refined-001** | `void f(int x)` with `pre(x > 0)` clause (replace with `Refined<positive, int>`) | info | refinements | PROD-AUDIT-2 (#541) |
| **CRUC-W-Naming-001** | Single-letter identifier outside loop induction context | warning | naming | code_guide §XVII |
| **CRUC-W-Naming-002** | `bool` named `ok` / `valid` / `done` / `flag` / `check` / `good` (use telling-word: `is_*` / `has_*` / `should_*` / `must_*` / `can_*` / `will_*` / `was_*` / `needs_*`) | warning | naming | code_guide §XVII telling-word rule |
| **CRUC-W-OneShot-001** | `std::atomic<bool>` field used as a one-time signal flag (use `safety::OneShotFlag`) | warning | linear-ownership | WRAP-BgThread-1 (#872) + WRAP-CKernel-1 (#889) + WRAP-Vigil-1 (#1071) + WRAP-SchemaTab-2 (#1004) |

Three rules are `error`-tier (banned patterns that have no legitimate use). Six are `warning`-tier (substrate-discipline opportunities the user may have missed). One is `info`-tier (pre-clause migration, lower urgency).

Each ships with at least one positive fixture (the pattern that should fire) and one negative fixture (the corrected form). Ten rules × two fixtures = 20 fixture files in `tools/crucible-lint/tests/fixtures/structural/`. Ten rule documentation pages in `tools/crucible-lint/docs-site/rules/`.

**Why these 10 and not others:**
- Each is ALREADY documented as a real opportunity in the GAPS task list or CLAUDE.md
- Each is a single-line pattern (no complex AST flow analysis required) → ast-grep handles them all
- Together they cover the five highest-impact categories (banned patterns, linear ownership, strong types, concurrency, naming)
- Combined estimated initial-baseline diagnostic count: ~3-5K across the existing codebase (manageable; not triage death)
- All ten have unambiguous machine-applicable fix templates → `--fix` mode works on Day 1

### 13.3 Week-1 expected outcome

After running the Day-1 bootstrap and shipping the 10-rule starter:

- `crucible-lint` builds cleanly from `cmake --preset lint`
- `lint-baseline.json` checked in (~3-5K initial baselined diagnostics)
- GitHub Actions workflow runs on every PR
- New PRs trigger only on diagnostics not in the baseline
- First few PRs surface 0-2 NEW violations each (caught by the 10 rules)
- Authors fix the violations or apply the suggested fix-its
- The team experiences "the tool catches real bugs without flooding triage"

If after Week 1 the tool has produced more than 5 false positives across all PRs, drop the offending rule(s) to `info`-tier and triage. If it has produced zero NEW diagnostics across 10+ PRs, the rules are too narrow — consider promoting `info`-tier rules to `warning` or expanding the corpus.

### 13.4 First-PR experience

A typical PR after Day-1 setup:

```
~/crucible $ git push origin feature/wrap-cipher-store

CI pipeline (5 minutes):
  ✓ Build (substrate + lint tool)
  ✗ crucible-lint: 1 new violation

  include/crucible/Cipher.h:128:10
  warning [CRUC-W-Linear-002]: FILE* parameter — should be Linear<ScopedFile>
  Doc: https://crucible-lint.dev/rules/CRUC-W-Linear-002
  Suggested fix: Linear<ScopedFile> stream
  GAPS task: PROD-WRAP-4 (#533)

  Apply fix automatically with: crucible-lint --fix --rule CRUC-W-Linear-002
  Or suppress with: // crucible-lint-disable: CRUC-W-Linear-002 — <reason>

  Status: this rule is at warning severity; merge is allowed but discouraged.
  Promote to error after PROD-WRAP-4 (#533) lands across all call sites.
```

The author either applies the fix, suppresses with justification, or files a follow-up task. Either way, the PR is unblocked from a CI perspective (warning-tier doesn't block); the diagnostic + recommended action are inline on the PR review.

---

## 14. Relationship to fixy.md and CLAUDE.md

**fixy.md §24 is the dimensional-discipline reference.** Each rule in `crucible-lint` corresponds to enforcement of a specific aspect of §24:

- §24.1 grade vector → strong-type rules + Refined<> rules + recipe-pinning rules
- §24.2 collision catalog → `crucible-collision-preview` semantic check + 11 ast-grep preview rules
- §24.3 trust algebra → per-function trust-annotation requirement + escape-hatch policy enforcement
- §24.4 effect lattice → `crucible-effect-row-flow` semantic check + effect-row admission rules
- §24.5 mode semiring → `crucible-linear-flow` semantic check + Linear<>/Permission<> rules
- §24.6 session safety levels → session-typed-channel rules
- §24.10 strong-type catalog → ~25 wrapper-application rules
- §24.12 negative-compile fixture corpus → fixture parity check (every neg-compile fixture has a corresponding lint rule that warns BEFORE the substrate rejects)

**CLAUDE.md / code_guide.md are the substrate-discipline reference.** Each rule cites its CLAUDE.md axiom and code_guide.md section in the doc anchor.

The fixy.md deferral note (top of fixy.md) cross-references this document. This document cross-references fixy.md §24 throughout. Together they form the complete discipline + enforcement story for Crucible:

- **fixy.md** specifies WHAT the discipline is (dimensional algebra, collision rules, wrapper catalog)
- **CLAUDE.md / code_guide.md** specify WHY the discipline matters (axioms, performance discipline, hard stops)
- **`safety/*.h` / `effects/*.h` / `permissions/*.h` / `sessions/*.h`** IMPLEMENT the discipline (the substrate)
- **ci-pipeline.md** (this document) ENFORCES the discipline mechanically (the lint tool + rule corpus + CI integration)

No DSL needed in the loop. The substrate is the language; the lint tool is the compiler-equivalent for discipline enforcement.

---

*End of CI pipeline design. Companion to fixy.md (dimensional reference, deferred), CLAUDE.md (project-level discipline), code_guide.md (substrate-level discipline), CRUCIBLE.md (runtime architecture).*
