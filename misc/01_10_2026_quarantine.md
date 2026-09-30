# Quarantine and std removal: the migration plan

Date: 2026-10-01. Main at a6e53b48 when this plan was written.

This document is the plan for the next large migration of Crucible. It has two goals:

1. **Strip std.** The base (`include/foundation` and `include/fixy`) depends only on the compiler: the language, the compiler builtins, and a short audited list of compile-time std names. We vendor close to nothing. Each unsafe or surprising part of std that the tree needs gets a safe replacement in the base.
2. **Quarantine everything behind fixy.** All code outside the base (crucible, src, vessel, test, fuzz, bench, examples, utils/tools) holds only objects of base types. If the base does not support a thing, we extend the base, safely. A GCC plugin turns each violation into a compile error.

The method is the one that Stage A to Stage D used: stages, units in worktrees, patch landings, guard runs, witnesses that go red on the old code, and gates with acceptance checks.

---

## 1. Decisions

The owner approved these decisions on 2026-09-30 and 2026-10-01.

| Id | Decision |
|---|---|
| D1 | The base is `include/foundation` plus `include/fixy`. Quarantined code can use the two. The primitive families live at the bottom of foundation, and fixy re-exports each family name, so quarantined code spells `fixy::Option`, `fixy::View`, and so on. |
| D2 | glibc stays as the process runtime: startup, thread-local storage, thread start, `malloc`, `dlopen` for PyTorch. Only `include/fixy/os` and its source files name a glibc function or a kernel header. |
| D3 | Withdrawn 2026-10-01. The premise was false: the tree has 331 executables, not 2,450, and all links together take 26 s. Unity batches are also unsafe here, because namespace-walk reflection sees every header that a translation unit includes. |
| D4 | Deferred 2026-10-01. The modules spike runs only if the S0 gate misses its targets (decision point DP1). |
| D5 | A differential test can use std as its oracle, inside an opt-out region whose reason is "differential oracle". |
| D6 | The `assert` macro expansion (`__assert_fail`) is its own finding kind. The test-harness change in Stage 5 replaces it with the fixy fatal exit. |
| D7 | The admitted std list gets its own audit (Stage 1, unit 1b). Each admitted name must behave the same under every flag set, be predictable, be safe for every use that type-checks (or carry a restriction that the plugin enforces), and have at least one use in the tree. |
| D8 | The families physically live in `include/foundation/core/`, in namespace `foundation::core`, because the foundation algebra must use them too. `include/fixy/Core.h` re-exports them into namespace `fixy`. The re-export is the public surface, not a shim. |
| D9 | 2026-10-01: the Debug build type drops `-fharden-compares`, `-fharden-conditional-branches` and `-fharden-control-flow-redundancy`. These flags defend a binary against fault injection and find no defect in a test. Every other build type keeps the first two. `-fharden-control-flow-redundancy` was in Debug only, so no build has it after this change. |
| D10 | 2026-10-01: build speed comes from how the tree uses C++, not from a changed compiler. No GCC rebuild in Stage 0. ccache stays at its present limit (one Debug build fills about 0.2 GB). |

---

## 2. The rules

These rules hold for the whole migration and after it.

- **R1. Two regions.** The base is `include/foundation` and `include/fixy`, plus their source files in `src/foundation` and `src/fixy`. Every other C++ file in the repository is quarantined.
- **R2. The base depends only on the compiler.** A base header can include only the headers that the rule table (Stage 1, unit 1a) allows for its layer. The first allowance for every layer is: `<cstddef>`, `<cstdint>`, `<type_traits>`, `<concepts>`, `<utility>` (casts, `declval` and index sequences only), `<compare>`, `<initializer_list>`, `<new>`, `<meta>`. The audit in unit 1b can make this list shorter. It cannot make it longer without an owner decision.
- **R3. Builtins before headers.** When a builtin gives a thing, the base uses the builtin and not a header: `__atomic_*`, `__builtin_memcpy`, `__builtin_memmove`, `__builtin_memset`, `__builtin_add_overflow` and its family, `__builtin_bit_cast`, `__builtin_ia32_pause`, `__builtin_ia32_rdtsc`, `__builtin_thread_pointer`, `__builtin_FILE`, `__builtin_LINE`, `__builtin_FUNCTION`, `__builtin_unreachable` (only inside the fatal exit), vector extensions.
- **R4. One door for each kernel subsystem.** Only `include/fixy/os` and `src/fixy/os` can include a system header, a Linux UAPI header, or a glibc header. Each such header has exactly one owner file (the door). No system type crosses a public signature of a door.
- **R5. Zero libstdc++ runtime use.** No object in `libcrucible.a` and no binary other than the vessel library references a libstdc++ symbol outside a short allowlist. A link-level guard proves this.
- **R6. The quarantine rule.** In a quarantined file, the plugin refuses:
  - an object of a std type
  - a use of a std name that the admitted list does not hold
  - a call to a C library function
  - an object of a C library struct or union
  - a raw object pointer, a raw function pointer, a C array
  - a new-expression or a delete-expression
  - an `assert` expansion
- **R7. The opt-out region.** The only exception to R6 is a region that the macros `CRUCIBLE_I_KNOW_WHAT_IM_DOING("reason")` and `CRUCIBLE_END_I_KNOW_WHAT_IM_DOING` open and close. The reason is mandatory. Each region is a row of a ledger that can only shrink. Section 16 gives the permitted reasons.
- **R8. Extend, do not escape.** When the base does not support a thing, a unit sends a request to the extension lane (section 17). The new primitive joins one of the eight families, uses the shared verbs, and comes from a real call site. An opt-out region is never a way around a missing primitive.
- **R9. A small, uniform surface.** Eight families: Choice, Record, Region, Ref, Atomic, Scalar, Report, Os. Ten shared verbs (section 11.1). We do not copy the std API. Each type gets only the operations that the tree calls.
- **R10. Clean cuts.** Each change moves every producer and every consumer at once. No shim, no alias for an old name, no deprecated path. Each commit builds and passes.
- **R11. The ratchet.** From Stage 1, the count of violations in each directory can only fall. A change that raises a count fails CI.
- **R12. Zero hot-path cost.** A family operation on the hot path compiles to the same instructions as the code it replaces. The proof comes from a type (an index type, a fixed extent, a memory order in the operation name), not from a branch at each access.

The rules R13 to R17 keep compile time low. Stage 0 applies them to the tree, and every later stage obeys them.

- **R13. A header does not check itself in each includer.** A header holds no self-test namespace and no `static_assert` at namespace scope. Those checks live in the check file of the header, which one translation unit compiles one time (section 7.2). A `static_assert` inside a class or a template stays, because it guards an instantiation.
- **R14. Compile-time tables are lazy.** A reflection walk, a roster or a table that not every includer reads is a variable template, or it lives inside a template. Only the translation units that read it evaluate it.
- **R15. No heavy inline body in a header.** A non-template inline function is analyzed in each includer. A body that is cold or large goes to a source file.
- **R16. Builtins before headers.** A base header does not include an intrinsics header, `<thread>` or `<chrono>` for one function. Rule R3 gives the builtins.
- **R17. No giant translation unit and no giant function.** A generated file comes out as shards. A test file that compiles for more than 60 s at `-j1` is split by subject. A function large enough to make the debug-information passes explode is split.

---

## 3. Baseline measurements

All numbers are measured unless marked (E).

| Measurement | Value | Source |
|---|---|---|
| Quarantine findings, unique | 29,667 in 602 files | QPLUG census at a572c4bf |
| Findings by kind | std name 14,765. std object 5,978. C library call 5,643. raw pointer 2,075. C array 920. C struct 115. raw new/delete 91. function pointer 80 | same |
| Findings by tree | include/crucible 6,247. src 879. vessel 102 (partial). test 18,262. test/fuzz 1,272. bench 2,602. examples 38. utils/tools and other 265 | same |
| Largest single finding | `__assert_fail`, 2,009 | same |
| std uses in foundation and fixy | 12,324 qualified names. 78% are compile-time or integer spellings | QLAYER |
| Runtime std uses in the base | about 1,497, plus 465 constexpr data members (upper bound) | QLAYER |
| std types in the base public surface | 415 signatures and 79 data members | QLAYER |
| Clean Debug build of `all`, no ccache, -j48, shared host | 375 s wall, 4,479 s user + 158 s system (77 CPU-minutes). 686 compile steps, 331 executables. All links together: 26 s | ninja log, 2026-10-01 |
| Critical path of the build | `test/session_oracle/generated_fixy_wire.cpp`: 315 s alone (69 s front end, 246 s back end) | ninja log, -fsyntax-only |
| Other slow translation units | test_session_global_attack 122 s, crucible_hwprobe 113 s, test_ledger_probes 102 s, generated_fixy_crash 101 s, test_ledger 91 s. The top 100 files take 60% of the compile time | ninja log |
| Negative fixtures, one ctest run | 2,062 fixtures: 289 s wall, 11,671 s user (194 CPU-minutes). They compile again on each run, and ccache does not store a failed compile | ctest -R neg -j48 |
| Other tests | 438 tests: 194 s wall. Six guards take 125 s to 192 s each and set the wall time | ctest -E neg -j48 |
| Header self-checks | 123 self-test namespaces (14,579 lines) and about 1,700 other namespace-scope `static_assert`s. On a scratch copy without them, front-end CPU falls 50% to 75% per file: test_arena 3.5 s to 0.95 s, Fn.h 11.7 s to 2.8 s, Vigil.h 14.0 s to 5.5 s, six fixtures 43 s to 15 s | scratch-copy experiment |
| `<immintrin.h>` through Platform.h | about 0.5 s front end in every translation unit | scratch-copy experiment |
| Parse cost after the checks move | about 0.4 s for 150,000 preprocessed lines. The rest is template instantiation and constant evaluation that runs eagerly | -ftime-report |
| Back-end outliers | test_ledger 110 s, 17 s without `-ftrivial-auto-var-init=zero` (variable tracking explodes on a giant function). The three `-fharden-*` flags cost 20% to 46% of the back end on heavy files. ASan about doubles the back end | flag experiments |
| std headers | each at most 1 s alone (`<chrono>` 1.0 s, `<thread>` 0.9 s, `<meta>` 0.3 s) | -fsyntax-only |
| ccache | cleared 2026-10-01 (94 GB of history). One full Debug build fills about 0.2 GB | ccache -s |
| Patched GCC against Fedora GCC | 5.2 s against 4.2 s on test_arena (24% slower, `--disable-bootstrap`). Not acted on (D10) | direct run |

---

## 4. Vocabulary

| Term | Meaning |
|---|---|
| Base | `include/foundation`, `include/fixy`, `src/foundation`, `src/fixy` |
| Quarantined tree | Every other directory with C++ source |
| Family | One of the eight primitive groups that replace std (section 11) |
| Verb | One of the ten operation names that every family uses with the same meaning |
| Door | The one base file that owns a system header or an unsafe operation |
| Rule table | The one data file that lists layers, allowed headers, doors and admitted std names. The plugin and the layer guard read it |
| Ledger | A committed list of counts or rows that can only shrink |
| Unit | One agent in one worktree, with one owner scope and one final report |
| Lane | A unit that stays open through several stages and serves requests (the extension lane) |
| Slice | A Stage 4 unit that migrates one base API group and all its callers |
| Sync point | A moment when all running units must land or wait, because a change affects every unit |
| Gate | The acceptance checks at the end of a stage |

---

## 5. How units work

Every unit obeys these instructions. The brief of each unit repeats them.

### 5.1 Edit rules

- Read with the Read tool, `rg` and `fd`. Write with Write and Edit only.
- Never `sed`, `awk`, a heredoc into a source file, a shell redirect into a source file, or a Python script that rewrites a source file.
- Never `git checkout`, `git reset`, `git restore`, `git stash`, `git checkout-index`, `--no-verify`, `-c user.name`, `-c user.email`. No commit trailers.
- No task ids, stage names, unit names or step numbers in any file or commit message. This document is the only exception, because it lives in `misc/`.
- All prose (comments, commit messages, reports) is ASD-STE100.

### 5.2 Worktree and landing

- Create the worktree with `git worktree add .worktrees/NAME -b wt/NAME main`. Build only inside the worktree.
- Land through `SCRATCH/commit-block.py` from the main checkout, with a patch and `--replace` pairs for shared files. Section 5.5 lists the shared files.
- Merge main before each landing. If main moved, rebuild and rerun the tests of the unit.
- After the last landing: remove the worktree and its grun directory, delete the branch and the patch files.

### 5.3 Commands, time and memory

- **Run every command in the foreground.** A command must block until it ends. Use a timeout of up to 600,000 ms, and split a long step into several calls. Never start a Monitor. Never end the turn while a command runs. Each stop sends the owner a notification.
- Report only once, in the final report. Send an interim message only for a question that blocks the unit.
- Run `free -g` before each heavy command. Wait while free memory is below 48 GB.
- Build with the job count that the stage gives. The default is -j32 for each unit, and at most six heavy units run at the same time.
- Never `nice` a ctest run.

### 5.4 Checks before landing

1. The Debug build of `all` passes.
2. The tests of the unit pass. For a change to a base header, the full Debug suite passes.
3. A Release build of `all` passes, and the Release tests of the unit pass.
4. The guard run prints "red only with the change: none". Run it in phases, one blocking call for each phase.
5. `refresh-derived.sh --check` passes. Regenerate each derived file (mint inventory, row-hash goldens, rosters) in the same commit.
6. `check-clang-format.sh` passes on the changed files.
7. From Stage 1: the quarantine ratchet passes. No directory count rises.
8. Each new test or fixture goes red on the old code, for the defect or the property itself, not only because it names a new symbol.

### 5.5 Shared files

These files get edits from many units. Land each edit as a `--replace` pair:
- `CMakeLists.txt`, `test/CMakeLists.txt`, `test/*/CMakeLists.txt`
- `misc/mint-inventory.md`
- `utils/scripts/*-allowlist.txt`, `utils/scripts/*-roster.txt`, `utils/scripts/*ledger*`
- the rule table and the admitted list (after Stage 1)
- `CLAUDE.md`
- `.github/workflows/ci.yml`

### 5.6 The final report

The final report gives, in this order:
1. The commit hashes on main.
2. What each test and fixture checks, and how it goes red on the old code.
3. What the unit deleted.
4. What the unit could not do, and why.
5. Each line of CLAUDE.md that the change made false, and the correction.
6. Findings that the unit did not fix.
7. Rule slips.
8. The cleanup.

---

## 6. The stage graph

```
                 +-----------------------------+
                 | Stage 0: build speed        |
                 | 0a floor (sync S0.1)        |
                 | 0b check files (sync S0.2)  |
                 | 0c check moves (5 units)    |
                 | 0d shards  0e back end      |
                 | 0f fixture cache 0g guards  |
                 | 0h lazy tables              |
                 +--------------+--------------+
                                | S0 gate
                 +--------------v--------------+
                 | Stage 1: rules and ratchet  |
                 | 1a rule table  1b audit     |
                 | 1c ratchet  1d pragma/neg   |
                 | 1e CLAUDE.md                |
                 +--------------+--------------+
                                | S1 gate
                 +--------------v--------------+
                 | Stage 2: base hygiene       |
                 | 2a floor  2b os moves       |
                 | 2c thread moves  2d edges   |
                 | 2e header splits            |
                 +--------------+--------------+
                                | S2 gate
                 +--------------v--------------+
                 | Stage 3: families           |
                 | G1 Choice                   |
                 | G2 Scalar, Record           |
                 | G3 Ref, Atomic              |
                 | G4 Region  G5 Report  G6 Os |
                 | extension lane opens        |
                 +--------------+--------------+
                                | S3 gate
                 +--------------v--------------+
                 | Stage 4: base onto families |
                 | slices 4.1 to 4.9           |
                 +--------------+--------------+
                                | S4 gate
                 +--------------v--------------+
                 | Stage 5: quarantine waves   |
                 | 5A harness, src, examples   |
                 | 5B include/crucible modules |
                 | 5C bench, fuzz, tests       |
                 | 5D vessel                   |
                 +--------------+--------------+
                                | S5 gate
                 +--------------v--------------+
                 | Stage 6: close              |
                 +-----------------------------+
```

Stage 1 can start while Stage 0 unit 0h still runs, because unit 0h edits header bodies only. All other stages start only after the gate of the stage before them.

---

## 7. Stage 0: build speed

**Goal.** Make each build and each test run at least two times faster, before hundreds of guard runs start. Every later stage multiplies the cost of one build. The speed comes from how the tree uses C++ (rules R13 to R17), not from a changed compiler (D10).

**Entry.** Wave E finished. The QPLUG plugin is on main. The baseline of section 3 is measured.

**What the baseline shows.** A header runs its own compile-time checks again in every translation unit that includes it. That repeated work is about half of all front-end time, and the 2,062 negative fixtures pay it too, on every ctest run. The rest is a small set of outliers: one generated file that alone sets the build wall time, two files whose back end explodes, and six slow guards.

### 7.0 The gauge (part of unit 0a)

1. `utils/scripts/build-gauge.sh` configures a fresh build directory with `CCACHE_DISABLE=1`, builds `all` at a given job count, and records the wall time, the user plus system CPU time, the peak memory (`/usr/bin/time -v`) and the ninja log. It then runs the negative-fixture tests (`ctest -R neg`) and the other tests (`ctest -E neg`), and records the wall time and CPU time of each. It prints the 20 slowest translation units from the ninja log.
2. `utils/scripts/tu-sample.py` measures a fixed sample with `-fsyntax-only` and with the full command, and prints the CPU time of each: `test/foundation/test_row_hash.cpp`, `test/test_arena.cpp`, `src/canopy/Lifeguard.cpp`, `test/fixy/test_collision.cpp`, `test/test_vigil.cpp`, `test/test_cipher.cpp`, `test/fixy/test_session_handle.cpp`, `test/fixy/test_owned_region.cpp`, `test/test_ledger.cpp`, `utils/tools/crucible_hwprobe.cpp`, `test/fixy/test_session_global_attack.cpp`, and five fixtures (`neg_atom_cv_qualified_rejected`, `neg_aligned_buffer_copied`, `neg_permission_brand_not_empty`, `neg_rule_l003_borrow_raw_clone`, `neg_vigil_ring_other_brand`). With `--headers`, it also measures each named header alone. The sample list is part of the script, so each run measures the same files.
3. Each Stage 0 unit runs `tu-sample.py` before and after its change and puts both tables in its final report. The host is shared, so each number is the median of three runs.

The gauge scripts stay in the repository, because every later stage repeats them.

### 7.1 Unit 0a: the floor

**Goal.** Remove the costs that every translation unit pays for no gain.

1. **Flags (D9).** In `CMakeLists.txt`, the Debug build type drops `-fharden-compares` and `-fharden-conditional-branches` (section 3.13) and `-fharden-control-flow-redundancy` (section 5). Every other build type keeps the first two. Check in `build/compile_commands.json` of each preset that the flags are where this item says. Correct CLAUDE.md §V, which lists `-fharden-control-flow-redundancy` as a flag of every build.
2. **Builtins in Platform.h.** Replace `_mm_pause()` with `__builtin_ia32_pause()` on x86_64 and `__asm__ volatile("yield")` on aarch64. Remove `<immintrin.h>`. A file that used an intrinsic through that include now includes the header it needs itself. The build finds each such file.
3. **Builtins in fixy/os/Time.h.** Replace `__rdtsc()` with `__builtin_ia32_rdtsc()` and `__rdtscp(&aux)` with `__builtin_ia32_rdtscp(&aux)`. Remove `<x86intrin.h>`.
4. **The gauge** of section 7.0.
5. **Witnesses.** `tu-sample.py` before and after. The full suite passes in Debug and in Release, and the determinism tests give identical results.

**Sync point S0.1.** The flag change changes every Debug compile command, so every object compiles again. After the landing, each running unit merges main and rebuilds before its next landing.

### 7.2 Unit 0b: the check files

**Goal.** Give each header one place, compiled one time, for its compile-time checks (R13), and a guard that keeps it so.

1. **The convention.** The check file of `include/<layer>/<path>.h` is `test/layer/checks/<layer>/<path>.cpp`. Its first include is its own header. It holds the self-test namespaces and the namespace-scope `static_assert`s of the header, unchanged, in the same namespaces, so each name resolves as before.
2. **The sentinels.** `test/layer/CMakeLists.txt` already compiles one sentinel translation unit per foundation and fixy header, against the include root of its layer. When a check file exists, it becomes the sentinel of its header, in place of the one-line sentinel. Add a third sentinel set for `include/crucible`, against the full include root. A crucible header that cannot compile alone goes on `test/layer/crucible-not-standalone.txt`, one row per header with its reason. That list can only shrink. At this time it holds the BPF C headers (`*/bpf/*.h`) and the ledger headers that include `bench_harness.h`.
3. **The guard.** `utils/scripts/check-header-checks.py` reads every header under `include/` through the parse-tree machinery that the guard-engine roster requires. It refuses a namespace whose name ends in `_self_test` and a `static_assert` at namespace scope. It compares each header with `utils/scripts/header-checks-ledger.txt` (one row per header: path and count). A count above its row fails. A count below its row also fails, with a message to regenerate the row in the same commit. Register the guard as a ci_guard test with a self-test that plants each kind.
4. **The pilot.** Move the checks of three headers: `foundation/algebra/lattices/ToleranceLattice.h`, `foundation/Lifetime.h`, `fixy/Bands.h`. Measure each with `tu-sample.py --headers`.
5. **CLAUDE.md.** Add rules R13 to R17 to §XV, in STE.
6. **Witnesses.**
   - A planted namespace-scope `static_assert` in a header makes the guard red.
   - A check file whose first include is not its header fails.
   - A check that fails in a check file fails the default build. This shows that the checks still run.

**Sync point S0.2.** Units 0c start when 0b lands.

### 7.3 Unit 0c: the check moves (five units)

**Goal.** Every header obeys R13. The ledger of section 7.2 is empty.

| Unit | Headers |
|---|---|
| 0c-F | `include/foundation/` |
| 0c-W | `include/fixy/` top level, `include/fixy/atoms/`, `include/fixy/fp/` |
| 0c-S | `include/fixy/session/`, `include/fixy/handle/` |
| 0c-C | `include/fixy/concurrent/`, `include/fixy/os/` |
| 0c-R | `include/crucible/`, after unit 0e lands (ledger headers) |

For each header in scope:
1. Move its self-test namespaces and its namespace-scope `static_assert`s to its check file, unchanged. Keep each `static_assert` inside a class or a template.
2. When the header names a type from its own self-test namespace outside that namespace (for example `fixy/concurrent/StageEndpointBridge.h`), move that type out of the self-test namespace first.
3. When a check depends on the translation unit that includes the header (for example a count of members that a later header can add), it stays in the header, with a reason row in the ledger. Report each such row.
4. Set the ledger rows of the unit to zero, in the same commit.

Each unit reports `tu-sample.py` before and after, and the change of the clean build CPU time from `build-gauge.sh`. The ledger is a shared file: each unit edits only its own rows, through `--replace` pairs.

### 7.4 Unit 0d: shards and splits

**Goal.** No translation unit compiles for more than 60 s at `-j1` in Debug (R17), so the build wall time is the total work divided by the job count.

1. **The session oracle.** The generated files in `test/session_oracle/` come out of their generator as shards. Choose the shard size so that each shard compiles in at most 30 s. The shards of one family are source files of the one executable of that family, so the ctest names do not change. The number of rows and each answer stay the same.
2. **Heavy test files.** Split `test/fixy/test_session_global_attack.cpp` and each other test file that compiles for more than 60 s mostly in the front end, by subject, into several source files of the same executable. The ctest names and every case stay the same. Files whose back end is the cause go to unit 0e.
3. **The oracle self-test.** `session_oracle_self_test` (`utils/scripts/session-oracle.sh --self-test`) takes 192 s. Make it take at most 45 s with the same verdicts.
4. **Witnesses.** The ctest list is the same before and after. The row counts of the oracle are the same. The clean build wall time falls, and the ninja log shows no translation unit above 60 s.

### 7.5 Unit 0e: the back-end outliers

**Goal.** `test/test_ledger.cpp`, `test/test_ledger_probes.cpp` and `utils/tools/crucible_hwprobe.cpp` compile in at most 25 s each in Debug (91 s to 113 s at the baseline).

1. Find the construct that makes `-ftrivial-auto-var-init=zero` explode the variable-tracking pass in these files. The largest functions are `bench::Run::measure<...>` (104 KB of machine code in crucible_hwprobe), its `main` (52 KB) and `test_store_round_trips_through_the_filesystem` (43 KB in test_ledger).
2. Fix the code, not the flag: split the giant functions, move cold paths out of line, or put a large aggregate somewhere other than the stack. The timed loop of the bench harness keeps its machine code: compare its disassembly before and after.
3. Report whether a ledger header can stop including `bench_harness.h`. Make that change if it is small, otherwise report it.
4. **Witnesses.** The compile times above. Each test gives the same result.

### 7.6 Unit 0f: the fixture result cache

**Goal.** A ctest run on an unchanged tree compiles no negative fixture again.

1. `test/neg_compile_driver.py` keeps a result store in `~/.cache/crucible/neg/`. The key is the hash of: the compiler identity, the full command line, the driver version, and the content of every file in the dependency list of the fixture (as ccache direct mode does). If GCC gives no dependency list for a failed compile, the key uses the preprocessed text (`-E`) instead.
2. On a hit, the driver replays the stored output and exit code, and evaluates the regexes against them as before. On a miss, it compiles and stores.
3. The store has a size limit and removes the entries used least recently. `CRUCIBLE_NEG_CACHE=0` turns it off. A damaged entry counts as a miss.
4. **Witnesses.**
   - A second run of `ctest -R neg` on an unchanged tree takes at most 10% of the first run.
   - A change to one header compiles again exactly the fixtures whose dependency list names it.
   - A change to a fixture source compiles that fixture again.
   - A stored result that no longer matches its regexes fails, exactly as a fresh compile would.

### 7.7 Unit 0g: the slow guards

**Goal.** The tests other than the fixtures finish in at most 60 s wall at `-j48`, and no guard takes more than 45 s.

1. Measure each of `no_unchecked_access`, `proof_routes`, `start_lifetime`, `federation_admission`, `atom_roster_joined_self_test` (125 s to 163 s at the baseline). Find where the time goes. `session_oracle_self_test` (192 s) belongs to unit 0d, which owns the oracle scripts.
2. `utils/scripts/preprocessed.py`: one shared, content-keyed store in `~/.cache/crucible/preprocessed/`, with a size limit and an age limit, shared by every guard and every worktree. Scan each distinct (file, content hash) chunk one time, not one time for each translation unit that includes it.
3. Keep what each guard covers. Each guard gives the same verdict on the same tree, and each self-test and plant still passes.

### 7.8 Unit 0h: lazy compile-time work (after units 0c)

**Goal.** Apply R14, R15 and R16 to the headers that are still expensive after the check moves. One unit for each of foundation, fixy and crucible, in parallel.

1. List each header whose standalone `-fsyntax-only` cost is above 0.5 s after 0c. For each, read the `-ftime-report` table.
2. Make eager namespace-scope tables lazy (a variable template, or a table inside a template). Move a heavy non-template inline body to a source file.
3. Remove `<thread>`, `<chrono>` and `<functional>` from base headers where a builtin or a lower header gives the same thing. `foundation/effects/Ctx.h` uses `<thread>` for thread identity only. `__builtin_thread_pointer()` compared as an address gives a unique identity for each live thread. Do a test that two live threads never get the same identity, and that one thread always gets the same identity.
4. Each change keeps every answer: the row-hash goldens do not move unless a reflected name changes, and then the unit regenerates them and lists each moved identity.

### 7.9 Stage 0 order and parallelism

| Wave | Units | Needs first |
|---|---|---|
| 0-I | 0a, 0b, 0d, 0e, 0f, 0g | entry |
| S0.1 | 0a lands | every running unit merges main and rebuilds |
| S0.2 | 0b lands | the check-file convention is on main |
| 0-II | 0c-F, 0c-W, 0c-S, 0c-C. Then 0c-R | S0.2. 0c-R also needs 0e |
| 0-III | 0h-foundation, 0h-fixy, 0h-crucible | every 0c unit |

| Unit | Files it owns |
|---|---|
| 0a | `CMakeLists.txt` (sections 3.13 and 5), `include/foundation/Platform.h`, `include/fixy/os/Time.h`, the files that need their own intrinsics include, `utils/scripts/build-gauge.sh`, `utils/scripts/tu-sample.py`, CLAUDE.md §V |
| 0b | `test/layer/`, `utils/scripts/check-header-checks.py`, `utils/scripts/header-checks-ledger.txt`, the guard registration, the three pilot headers, CLAUDE.md §XV |
| 0c-* | the headers of its row in 7.3, their check files, their ledger rows |
| 0d | `test/session_oracle/`, `utils/tools/session_oracle/`, `utils/scripts/session-oracle.sh`, the split test files and their CMake rows |
| 0e | `include/crucible/ledger/`, `bench/bench_harness.h`, `utils/tools/crucible_hwprobe.cpp`, `test/test_ledger.cpp`, `test/test_ledger_probes.cpp` |
| 0f | `test/neg_compile_driver.py` |
| 0g | the five guard scripts of 7.7, `utils/scripts/preprocessed.py` |
| 0h-* | header bodies of its layer, not their checks |

Shared files (`CMakeLists.txt`, `test/*/CMakeLists.txt`, CLAUDE.md, the ledger) go in through `--replace` pairs (section 5.5).

### 7.10 The S0 gate

All numbers come from `build-gauge.sh` on the same host as the baseline:
- The clean Debug build of `all` takes at most 50% of the baseline CPU time (77 CPU-minutes) and at most 150 s wall at `-j48`. No translation unit takes more than 60 s.
- A cold run of the negative fixtures takes at most 40% of the baseline CPU time (194 CPU-minutes). A second run on an unchanged tree takes at most 10% of the first.
- The other tests take at most 90 s wall.
- The header-checks guard is in error mode with an empty ledger, apart from reasoned rows.
- The full suite passes in the Debug, Release, TSan and UBSan-strict presets, with identical results for every determinism test.

**Decision point DP1.** If the gate misses a target, the owner decides whether the modules spike runs.

---

## 8. Stage 1: rules, audit and ratchet

**Goal.** One rule table, an audited admitted list, and a ratchet that stops every new violation from the first day.

### 8.1 Unit 1a: the rule table

1. Write `utils/scripts/layer-rules.txt`, one row for each fact, in a line format that the plugin (C++) and the guards (Python) can both read without a library:
   - `layer <name> <rank> <directory>...`: the layers of section 12 (F0 floor, F0 core, F0 primitives, F1 algebra, X1 wrappers, X2 binding, X3 os, X4 concurrent, X5 session, X6 channels)
   - `allow <layer> <header>`: an allowed header for a layer
   - `door <header> <owner file>`: the one owner of a system header
   - `admit <std name>`: taken from the audited list of unit 1b
   - `quarantine <directory>`: a quarantined root
   - `enforce <directory> report|error`: the mode of each quarantined directory
2. Change the plugin to read the table (a plugin argument gives its path). Remove every fact that the plugin holds in its own source today.
3. Change `utils/scripts/check-layer-boundary.py` to read the same table, only for the `#if` branches that the compiler does not build.
4. **Witness.** A plant for each row kind: a forbidden header in a layer, a door header outside its door, an upward include, a std name that the table does not admit. Each plant fails, and the same file without the plant passes.

### 8.2 Unit 1b: the audit of the admitted list

The owner asked for this audit: each admitted name must act the same under all flags, be predictable, be safe to use, and be used.

For each row of `utils/scripts/quarantine-admitted-std.txt`, and for each candidate in appendix A, do these five checks and record the result in the row:

1. **Flag invariance.** The name gives the same types, values, sizes and code under each preset: Debug, Release, TSan, UBSan-strict, verify, PGO, and under `-D_GLIBCXX_DEBUG`, `-D_GLIBCXX_ASSERTIONS`, `NDEBUG` and each contract semantic. Write `test/layer/admitted_flag_matrix.cpp`. It holds a `static_assert` for each property of each name, and CMake compiles it once for each flag set of the matrix. A name whose meaning changes with a flag fails. Example: `std::unreachable` is undefined behavior with one flag set and a trap with another, so it fails.
2. **Predictability.** The name reads no global state: no locale, no `errno`, no environment, no allocation, no clock, no exception path.
3. **Safe use.** Every use that type-checks is safe, or the name gets a restriction that the plugin enforces. Example: `std::initializer_list` is safe as a parameter and dangles as a data member, so it gets "parameters only".
4. **Use.** The name has at least one use in the tree. A name with no use leaves the list.
5. **Cost.** The preprocessed size that the header adds on top of the allowance of its layer. More than 5,000 lines needs owner approval.

Each row ends with one verdict:
- **KEEP**: all five checks pass.
- **KEEP-RESTRICTED**: the name passes with a restriction that the plugin enforces. The row names the restriction.
- **REPLACE**: a family replaces the name. The row names the family. The name stays admitted only until its slice lands.
- **DROP**: no use, or no safe use.

The plugin admits only KEEP rows, KEEP-RESTRICTED rows, and REPLACE rows until their slice. The audit table is a shared file, so the owner reviews it before the S1 gate. Appendix A gives the first verdicts.

### 8.3 Unit 1c: the ratchet

1. **Reports that ccache can store.** In REPORT mode, the plugin writes its findings to a file next to the object, and QPLUG turned ccache off for that reason. Change the plugin to write the findings into a section of the object file (`.crucible.quarantine`). ccache then stores the findings with the object, and REPORT mode no longer needs a full build. A script reads the section from each object.
2. **The ledger.** `utils/scripts/quarantine-ledger.txt` holds one row for each directory and kind: the count on main. Generate it from a REPORT build.
3. **The check.** A ci_guard test builds in REPORT mode, reads the sections, and compares each count with the ledger. A count above the ledger fails. A count below the ledger fails too, with a message to regenerate the ledger in the same commit, so the ledger never keeps slack.
4. **CI.** Add a CI job that runs the check on each push.
5. **Witness.** A plant of one std object in a quarantined file raises one count and fails the check. The removal of one finding fails until the ledger is regenerated.

### 8.4 Unit 1d: the pragma macro, the fixture rule, the assert kind

1. **The pragma macro.** Add `include/foundation/Quarantine.h` with `CRUCIBLE_I_KNOW_WHAT_IM_DOING(reason)` and `CRUCIBLE_END_I_KNOW_WHAT_IM_DOING`. When the plugin is loaded, CMake defines `CRUCIBLE_QUARANTINE_ACTIVE`, and the macros expand to `_Pragma` forms of the plugin's pragma. Otherwise they expand to nothing. Warnings are errors, and GCC warns about an unknown pragma, so a raw `#pragma crucible` would fail a build without the plugin. The plugin refuses a raw `#pragma crucible` that does not come from the macro.
2. **The fixture rule.** `test/neg_compile_driver.py` removes every `-fplugin` argument from the flags of a fixture, unless the fixture tests the plugin itself. A fixture must fail only for its own reason.
3. **The assert kind.** Add the finding kind `assert_expansion` for `__assert_fail`, so the 2,009 assert rows stop hiding the real C calls.
4. **Witnesses.** A region compiles with the plugin off and with the plugin on. A fixture with a std object fails only for its own reason in ERROR mode. An `assert` in a quarantined file reports as `assert_expansion`.

### 8.5 Unit 1e: CLAUDE.md

Add a section on the quarantine: rules R1 to R12, the layers, the door rule, the opt-out rule, and the extension lane. Mark the std tables of §III and §IV as tables that Stage 6 replaces. Write it in STE.

### 8.6 Stage 1 parallelism

| Unit | Runs with | Needs first |
|---|---|---|
| 1a | 1b, 1d | S0 gate |
| 1b | 1a, 1d | S0 gate |
| 1c | 1d, 1e | 1a (the table) |
| 1d | 1a, 1b, 1c | S0 gate |
| 1e | 1c, 1d | 1b (the verdicts) |

### 8.7 The S1 gate

- The rule table is on main, and the plugin and the layer guard read it.
- The owner approved the audit verdicts.
- The ratchet runs in CI and fails on a plant.
- The pragma macro works with the plugin on and off.
- CLAUDE.md has the quarantine section.

---

## 9. Stage 2: base hygiene

**Goal.** A base that obeys its include rules and its layer order, with no new primitive yet.

### 9.1 Units

**2a, the floor.** `include/foundation/Platform.h` keeps the platform checks, attributes and macros. The debugger probe and the body of `fail_invariant` move to `src/foundation/Platform.cpp`, and the header keeps a `[[noreturn]]` declaration. Remove `<fcntl.h>`, `<unistd.h>`, `<cstdio>`, `<cstdlib>` and `<cstring>` from the header.

**2b, OS code into fixy/os.**
- Move `fixy/OwnedFile.h`, `fixy/OwnedMmap.h`, `fixy/Path.h` and `fixy/concurrent/Topology.h` into `include/fixy/os/`.
- The sysfs probe of Topology moves to `src/fixy/os/Topology.cpp`.
- The signal, `atexit`, `pthread` and clock code of `fixy/session/Watch.h` moves to `src/fixy/session/Watch.cpp`.
- Every caller moves in the same commit.

**2c, thread start into concurrent.** Move `fixy/os/Spawn.h` to `include/fixy/concurrent/`. Move the thread half of `foundation/permissions/PermissionFork.h` to `include/fixy/concurrent/`. Foundation starts no thread after this unit.

**2d, the include edges.** Fix the eleven edges that QLAYER found:
1. `foundation/contracts/Decide.h` includes `effects/Row.h` for one predicate. Move `row_subset` into effects.
2. `diag/RowHash.h` and effects include each other. Put the effect fold where it breaks the cycle.
3. `effects/Computation.h` and permissions include each other. Break the cycle.
4. `lattices/HappensBefore.h` and `StrongCounterLattice.h` include `effects/Ctx.h` for their context-bound mints. Move those mints above the lattices.
5. `AlignedBuffer.h` includes `diag/Catalog.h` for two tags, and `ChannelBinding.h` includes `diag/Runtime.h`. Move the tags to the tag vocabulary, or report through the base fatal exit.
6. `fixy/Witnessed.h` includes `session/Handle.h`, and `fixy/OwnedMmap.h` includes `os/AtomPack.h`. Move each to its layer.
7. Five concurrent headers include session headers. Move Endpoint, EndpointMint, SubstrateSessionBridge, SwmrSession and StageEndpointBridge to a new directory `include/fixy/channels/` (layer X6).
8. `concurrent/Pipeline.h` includes `os/Sched.h`, and `os/Spawn.h` includes `concurrent/ParallelismRule.h`. Unit 2c resolves the second. Resolve the first through a door.
9. `handle/LazyEstablishedChannel.h` includes session. Move it to `include/fixy/channels/`.
10. OS code outside fixy/os. Unit 2b resolves it.
11. Thread start in foundation. Unit 2c resolves it.

**2e, the header splits.** One unit for each group. Each split keeps each closed rule set in one header, so no answer depends on which headers a file included.

| Header | Split |
|---|---|
| `algebra/Transition.h` (3,067) | Moves to `fixy/session/`. Four headers: Registry (vocabulary, registry, coherence, seal), Fold (node, members, fold, algebras), Wire (the wire word that select and branch read), Refinement (payload preorder, type graph, refinement preorder). The 530-line self-test goes to a sentinel file |
| `diag/Catalog.h` (1,921) | A tag vocabulary (`tag_base`, `Severity`, `Category`, name readers) and a prose table. The closed-set check reads one aggregate that includes each domain file. A base header never includes the prose table |
| `permissions/Permission.h` (1,858) | Permission (token, tag tree, split and combine, rows), SharedPermission (share, guard, pool), the federation door, and the separation-logic roster check in a source file |
| `session/Handle.h` (3,023) | Transport (shapes, wire words, keyed values), Admission (admission, effect row of a protocol), Handle (family, factory, builders, rewind, local choices, position), Mint (resource, priority, permissioned start, callback entry, channels, mint doors) |
| `session/Payload.h` (2,004) | Classification (markers, reasons, walk, facts, sealed cache, gates) and Ownership flow (delta, delegation, protocol walk, regions, set after a step). Flow includes Classification |
| `fixy/Collision.h` (2,525) | Codes (`RuleCode`, corpus text, citations), Rules (the `rules_of` resolver), Report (the failure path), and the cells and the self-test in a source file |
| `fixy/Refined.h` (1,867) | Predicates (predicate structs, combinators, the closed implication relation, kept whole) and Refined (`Refinement`, mints, `SealedRefined`), and the test in a source file |

### 9.2 Order and parallelism

- 2a, 2b and 2c run in parallel. Their files do not overlap.
- 2d runs after 2a, 2b and 2c land, because it reads their new paths.
- The 2e groups run in parallel with each other, after 2d lands. Each group owns its header and every include of it. Two groups that edit the same caller include line merge through a `--replace` pair.
- Each 2e group runs `tu-sample.py` before and after, and reports the change.

### 9.3 The S2 gate

- The plugin enforces the include rules of the base as errors. The rule table sets `enforce include/foundation error` and `enforce include/fixy error` for the include rules only.
- No base header includes a header outside the allowance of its layer, except the runtime std headers that Stage 4 removes. Those stay on a ledger that can only shrink.
- The layer order has no upward edge.
- The gauge shows no slowdown against the S0 numbers.

---

## 10. Stage 3: the primitive families

**Goal.** The eight families exist in `include/foundation/core/`, re-exported in `include/fixy/Core.h`, with tests, fixtures and zero hot-path cost. No caller moves yet.

### 10.1 Groups and order

| Group | Families | Needs first | Parallel inside the group |
|---|---|---|---|
| G1 | Choice | S2 gate | one unit |
| G2 | Scalar, Record | G1 | two units |
| G3 | Ref (with the bump arena moved from `include/crucible/Arena.h`), Atomic | G2 | two units |
| G4 | Region | G3 | one unit, the largest |
| G5 | Report | G4 | one unit |
| G6 | Os | G5 | one unit |

The groups are serial, because each family uses the ones before it.

### 10.2 Instructions for each family unit

1. **The API.** Start from the QDESIGN design (section 11). Cut it to the operations that the tree calls. The census in the QPLUG report names them. Each operation that no call site needs stays out.
2. **The implementation.** Use only the allowance of layer F0 and the builtins of R3. No std runtime type appears in any signature or data member. Contracts guard every boundary. `CRUCIBLE_PRE` and `CRUCIBLE_POST` cite a named predicate where one exists.
3. **The layout.** A `static_assert` states the size of each type. `Option<Ref<T>>` has the size of one pointer. `Option<Id>` has the size of the id. `Result<int, E>` with a 4-byte `E` returns in registers under the x86-64 System V ABI: check it in the disassembly.
4. **The tests.**
   - A unit test for each operation.
   - A property test with Philox-seeded inputs for each invariant (for example: `copy` then `equal`, `sort_by_key` stable and ordered, `narrow` round trip).
   - A differential test against std, inside an opt-out region with the reason "differential oracle", for each operation that has a std counterpart.
   - For Atomic: TSan runs, and a stress test of each operation under contention.
5. **The fixtures.** At least two negative fixtures for each gate and each mint (HS14). Each fixture attacks the new door, and each regex names the reason of the refusal.
6. **The cost.** For each hot-path operation, compile a probe with `CRUCIBLE_DUMP_ASM=ON` and compare its instructions with the std or raw form it replaces. They must be the same, or the difference must be explained. For Region and Atomic, run the benches before and after.
7. **The adversarial pass.** A second agent tries legal uses that still dangle, leak, race, forge or misuse. Each finding is fixed, or pinned on a ledger that can only shrink.
8. **The re-export.** Add each public name to `include/fixy/Core.h` with a using-declaration.

### 10.3 Defects that the families fix

These defects from QDESIGN (task list 404) get their fix in the unit of their family, with a witness that goes red on the old code:
- `_GLIBCXX_ASSERTIONS` is on only in Debug, but CLAUDE.md says every build has it. The Region family makes each index checked by its type, and CLAUDE.md gets the truth now.
- The `OwnedRegion` arena door does not require an implicit-lifetime type. The Ref family moves the arena into `BumpArena`, whose `array` requires `ImplicitLifetimeThroughout`.
- `AtomicMonotonic` accepts any memory order. The Atomic family puts the order in the operation name.
- `AlignedBuffer::allocate` leaves heap memory uninitialized. The Region family zero-fills or takes a fill value.
- CLAUDE.md describes an arena with `start_lifetime_as` and a reset. Correct the text in the Ref unit.
- `TscMode::SteadyClockFallback` reads the TSC. Delete the enumerator in the Os unit.

### 10.4 The extension lane opens

At the S3 gate, the extension lane opens (section 17). It stays open until Stage 6.

### 10.5 The S3 gate

- All eight families are on main, with the tests and fixtures above.
- Every hot-path cost claim is checked in the disassembly and in the benches.
- The adversarial ledger of each family is empty, or each row has an owner decision.
- The families use only the allowance of F0.

---

## 11. The families: summary and migration recipes

The full design is in the QDESIGN report. This section gives what a migrating unit needs.

### 11.1 The shared verbs

| Verb | Meaning | Cost |
|---|---|---|
| `for (x : c)` | Visit each element. The type fixes the count | the loop bound only |
| `match(fns...)` | Total elimination of a sum. A missing arm does not compile | one switch on a tag |
| `get(key)` | Checked lookup. Returns `Option<Ref<T>>` | one explicit branch |
| `at(proof)` | Total lookup with a proof value (`Index<N>`, a brand index, a constant) | no branch |
| `view()` | Borrow as a `View` or a `Ref` | none |
| `size()` | Element count | none |
| `push(v)` | Add with no growth. Returns `Result<Ref<T>, Full>` | one compare |
| `append(Alloc, v)` | Add, and grow if full. The capability shows the allocation | a visible O(n) event |
| `consume() &&` | Take the owned value out | a move |
| `expect(Fmt)` | Fatal unwrap at a boundary | one branch to a cold call |

A verb that no family needs is not added. A new operation that no verb covers uses a `mint_*` name, which marks a site that creates authority.

### 11.2 Choice: `Option`, `Result`, `OneOf`, `CRUCIBLE_TRY`

| Pattern A | Pattern B | Kind |
|---|---|---|
| `std::expected<T, E>` | `fixy::Result<T, E>` | mechanical |
| `std::expected<void, E>` | `fixy::Result<fixy::Unit, E>`. `return {};` becomes `return fixy::Unit{};` | mechanical |
| `return std::unexpected(e);` | `return fixy::err(e);` | mechanical |
| `auto r = f(); if (!r) return std::unexpected(r.error()); use(*r);` | `CRUCIBLE_TRY(v, f()); use(v);` | mechanical when the error converts |
| `if (r) {...*r...} else {...r.error()...}` | `std::move(r).match(on_ok, on_err)` | judgment when an arm has `break`, `continue` or `return` |
| `std::optional<T>` | `fixy::Option<T>` | mechanical |
| `std::nullopt` | `fixy::none` | mechanical |
| `opt.has_value()` | `opt.is_some()` | mechanical |
| `if (opt) use(*opt);` with no else | `for (auto& v : opt) use(v);` | mechanical |
| `T* find(...)` that can give null | `Option<Ref<T>> find(...)` | judgment for each caller |
| an error enum with a `None` value | delete `None`. `Result` holds the no-error state | judgment |

### 11.3 Record: `Record<Ts...>`, `Pack<Ts...>`

| Pattern A | Pattern B | Kind |
|---|---|---|
| `std::pair<A, B>` in an API | a named struct | judgment (the names) |
| `std::pair<A, B>` in generic code | `fixy::Record<A, B>` | mechanical |
| `std::tuple` for values | `fixy::Record<Ts...>` | mechanical |
| `std::tuple` as a type list | `fixy::Pack<Ts...>` | mechanical |
| `std::tie(a, b) = f();` | `auto [a, b] = f();` | mechanical |

### 11.4 Region: `View`, `FixedArray`, `FixedList`, `HeapArray`, `GrowList`, `Text`, `TextView`, `CText`, `FixedText`, `FixedTable`, `HeapTable`, and the free operations

| Pattern A | Pattern B | Kind |
|---|---|---|
| `std::span<T>` parameter | `fixy::View<T>` | mechanical |
| `std::span<T, N>` | `fixy::View<T, N>` | mechanical |
| `static constexpr std::string_view n = "...";` | `static constexpr fixy::Text n = "...";` | mechanical |
| `std::string_view` parameter | `fixy::TextView` | mechanical |
| `const char*` text parameter | `TextView`, or `CText` when it goes to the kernel | judgment |
| `std::array<T, N>` | `fixy::FixedArray<T, N>` | mechanical |
| `a[i]`, i a constant | `a.at(fixy::index<i>())` | mechanical |
| `a[i]`, i from a loop | `for (auto& x : a)` or `for (auto i : a.indices())` | mechanical |
| `a[i]`, i from data | `a.get(i)`, and handle the empty case | judgment |
| `T x[N];` | `fixy::FixedArray<T, N> x;` | mechanical |
| `T* p, size_t n` parameters | `fixy::View<T> v` | mechanical signature, judgment body |
| `std::vector<T>`, known maximum | `FixedList<T, N>` | judgment (N) |
| `std::vector<T>`, size known at build | `HeapArray<T>` | judgment |
| `std::vector<T>`, unbounded, cold | `GrowList<T>`. `push_back(x)` becomes `list.append(alloc, x)` | judgment (the caller needs an `Alloc`) |
| `std::string` | `FixedText<N>` or `GrowText` | judgment |
| `memcpy(dst, src, n)` | `fixy::copy(dst_view, src_view)` | judgment (n becomes view lengths) |
| `memcpy(&x, p, sizeof x)` | `x = fixy::load<T>(window)` | mechanical when the window exists |
| `memset(p, 0, n)` | `fixy::fill(view, T{})` | mechanical |
| `std::ranges::sort(v, cmp)` | `fixy::sort_by_key(view, key)` | judgment (the key) |
| `std::unordered_map<K, V>` | `HeapTable<K, V>` | judgment (capacity) |

### 11.5 Ref: `Ref`, `Box`, `BumpArena`, `Slot`, `downcast`, `visit_kind`, `FnRef`, `CallRef`, `Address`

| Pattern A | Pattern B | Kind |
|---|---|---|
| `T* next = nullptr;` member | `Option<Ref<T>> next = none;` | mechanical |
| a member that is never null after build | `Ref<T>` | judgment |
| `new (arena.alloc_obj<T>(a)) T{}` | `arena.make<T>(a)` | mechanical |
| `arena.alloc_array<T>(a, n)` | `arena.array<T>(a, n)` | mechanical |
| `static_cast<D*>(node)` in `switch (node->kind)` | `visit_kind(node, arms...)` | mechanical for each switch |
| one `static_cast` after an if | `downcast<D>(ref)` | judgment |
| `std::make_unique<T>(args)` | `fixy::mint_box<T>(alloc, args)` | judgment (an `Alloc` must reach the site) |
| `new T` / `delete p` | `Box` | mechanical |
| `std::function<Sig>` parameter | `CallRef<Sig>` | mechanical |
| `std::function<Sig>` member | a template parameter | judgment |
| `reinterpret_cast<uintptr_t>(p)` | `fixy::address_of(ref)` | mechanical |

### 11.6 Atomic: `Atomic<T>`, `CacheLine`, `Tally`

| Pattern A | Pattern B | Kind |
|---|---|---|
| `std::atomic<T> x{v};` | `fixy::Atomic<T> x{v};` | mechanical |
| `x.load(std::memory_order_acquire)` | `x.load_acquire()` | mechanical |
| `x.store(v, std::memory_order_release)` | `x.store_release(v)` | mechanical |
| `x.fetch_add(d, std::memory_order_acq_rel)` | `x.fetch_add_acq_rel(d)` | mechanical |
| `x.compare_exchange_strong(e, d, acq_rel, acquire)` | `x.cas_acq_rel(e, d)` | mechanical |
| relaxed statistics counter | `fixy::Tally` | mechanical |
| relaxed read of an owned variable | `load_owned(writer_permission)` | judgment (find the proof) |
| `std::atomic<T*>` | `PublishOnce<Ref<T>>` or `Atomic<Option<Ref<T>>>` | judgment |

### 11.7 Scalar

| Pattern A | Pattern B | Kind |
|---|---|---|
| `std::numeric_limits<uint32_t>::max()` | `fixy::max_of<uint32_t>` | mechanical |
| `numeric_limits<float>::min()` | `min_normal_of` or `min_of` | judgment (which one the author meant) |
| `std::min(a, b)` | `fixy::min(a, b)` | mechanical when the types agree |
| `std::bit_cast<To>(x)` | `fixy::bit_cast<To>(x)` | mechanical when `To` is `BitValid` |
| `std::bit_cast<Enum>(x)`, `std::bit_cast<bool>(x)` | `fixy::decode<To>(x)` | judgment |
| `static_cast<E>(raw)` | `fixy::enum_from<E>(raw)` | judgment (the failure path) |
| narrowing `static_cast<uint32_t>(x)` | `fixy::narrow<uint32_t>(x)`, or `widen` when the build proves it | judgment |
| `std::rotl(x, n)` | `fixy::rotl(x, n)` | mechanical |

### 11.8 Report: `Site`, `Fmt`, `format`, `report`, `fatal`, `unreachable`, `parse`

| Pattern A | Pattern B | Kind |
|---|---|---|
| `std::fprintf(stderr, "x=%d\n", x);` | `fixy::report(fixy::Sink::Err, "x={}\n", x);` | mechanical, except width and precision |
| `fprintf(...); std::abort();` | `fixy::fatal("...", args...);` | mechanical |
| bare `std::abort()` | `fixy::fatal("reason")` | judgment (the reason) |
| `std::unreachable()` | `fixy::unreachable()` | mechanical |
| `std::snprintf(buf, n, ...)` | `fixy::format(text, "...", ...)` | mechanical |
| `std::to_string(x)` | `fixy::format` | mechanical |
| `std::strtoull`, `std::from_chars` | `fixy::parse<std::uint64_t>(view)` | mechanical |

### 11.9 Os

| Pattern A | Pattern B | Kind |
|---|---|---|
| `std::filesystem::path{root} / "HEAD"` | `fixy::os::join(root, "HEAD")` | mechanical |
| `std::filesystem::exists(p)` | `fixy::os::exists(ctx, p)` | mechanical (the context must reach the site) |
| `::pread(fd, buf, n, off)` | `fixy::os::read_at(ctx, fd, view, off)` | mechanical |
| `opendir` and `readdir` loop | `for_each_entry` | judgment |
| `std::jthread` | `mint_permission_fork` or `fixy::os::Thread` | judgment |
| `std::chrono::steady_clock::now()` | the `MonotonicClock` reader | mechanical |
| `std::getenv("X")` | `fixy::os::env(init_ctx, "X")` | judgment (the context) |
| `std::error_code` | `fixy::os::Errno` | mechanical |

---

## 12. Layers and the include policy

| Layer | Directories | May include |
|---|---|---|
| F0 floor | `foundation/Platform.h`, `foundation/Pinned.h`, `foundation/Quarantine.h` | the base allowance (R2) |
| F0 core | `foundation/reflect/`, `foundation/contracts/`, `foundation/Brand.h`, `foundation/Lifetime.h`, `foundation/diag/FailClosed.h` | the base allowance |
| F0 primitives | `foundation/core/` (the families), `foundation/Simd.h`, `foundation/Saturate.h`, `foundation/AlignedBuffer.h`, `foundation/SwissTableBuffer.h`, `foundation/ThreadLocalRef.h` | the base allowance. No system header in a header. Cold code goes to `src/foundation/` |
| F1 algebra | `foundation/algebra/`, `foundation/effects/`, `foundation/diag/` (other files), `foundation/permissions/` | the base allowance. Runtime std names only in `consteval` context |
| X1 wrappers | `fixy/` top-level wrapper headers, `fixy/fp/` | the base allowance |
| X2 binding | `fixy/Fn.h`, `fixy/Axis.h`, `fixy/Atom.h`, `fixy/atoms/`, Collision, Corpus, Reject, Insights, Role, Throws, Federation | the base allowance |
| X3 os | `fixy/os/`, `src/fixy/os/` | the base allowance, plus each system header through its one door |
| X4 concurrent | `fixy/concurrent/`, `fixy/handle/` (without LazyEstablishedChannel) | the base allowance. Threads start through X3 |
| X5 session | `fixy/session/`, `src/fixy/session/` | the base allowance |
| X6 channels | `fixy/channels/` | the base allowance |

A layer can include a lower layer and itself. It cannot include a higher layer.

**Headers that no layer includes:** `<thread>`, `<chrono>`, `<filesystem>`, `<fstream>`, `<sstream>`, `<iostream>`, `<functional>`, `<memory>`, `<ranges>`, `<algorithm>`, `<set>`, `<map>`, `<unordered_map>`, `<mutex>`, `<any>`, `<variant>`, `<expected>`, `<optional>`, `<system_error>`, `<charconv>`, `<format>`, `<regex>`, `<cmath>`, `<source_location>`, any third-party header.

**The admission rule for a new header.** A header joins the allowance of a layer only if all these conditions hold:
1. A member of the layer needs a declaration from it, and no lower layer and no builtin gives the declaration.
2. Exactly one door owns it. No std or C type crosses the door's public signatures, except the integer spellings.
3. It adds no runtime state: no static initializer, no locale, no stream, no throw path.
4. Its preprocessed cost is measured and recorded in the rule table. More than 5,000 lines needs owner approval.
5. A cold call goes to a source file, not to a header.

---

## 13. Stage 4: move the base onto the families

**Goal.** No std runtime type in the base, and no std type in any base signature. The base includes only its allowance.

### 13.1 Slices

A slice migrates one base API group and every caller in the whole tree, in one clean cut. When a caller is in a quarantined file, the slice changes only the lines that the API change forces. The rest of that file waits for Stage 5.

| Slice | Content | Callers |
|---|---|---|
| 4.1 Os results | Every `fixy::os` door returns `Result<T, os::Errno>`. One `Errno` type replaces `std::error_code` and the plain `int` of Sched | about 133 base uses, many callers in crucible and test |
| 4.2 Choice in concurrent and wrappers | `Checked.h` and `Saturate` return `Option`. The rings, channels and `AtomicSnapshot` pop into `Option`. `admit_refined` returns `Result` | the rings are on hot paths: check the disassembly |
| 4.3 Region in the wrappers | `FixedArray` loses `operator[](size_t)`, `data()` and raw iterators. `Borrowed` becomes `View`. `OwnedRegion` and `SharedRegion` give `View`. `foundation::simd` loads and stores take `View<T const, Lanes>`. `CyclicBuffer` indexes through `Index<N>`. `AppendOnly` stores a `GrowList` | every caller of those accessors |
| 4.4 Atomic in the base | `AtomicMonotonic`, `MpscRing`, `OneShotFlag`, `PublishOnce`, `PublishCommit`, `Once`, `SpinLock`, `CpuPinned`, `session/Watch.h` move to `Atomic`. `ChaseLevDeque` keeps its relaxed and seq_cst operations as cited `detail` code inside the base | few callers outside |
| 4.5 Report in the base | `fail_invariant`, the diag runtime sink, `JsonEmitter`, and every `std::source_location` in a public API move to `Site` and the Report sink | few callers outside |
| 4.6 Os internals | Topology stops using `ifstream`, `filesystem`, `set`, `vector`. `Path` stops using `std::filesystem::path`. `std::jthread` in PermissionFork and Spawn moves to the thread-start primitive. All nine `try`/`catch` sites go | callers of Path and Topology |
| 4.7 Record in the base | `pair` and `tuple` in base APIs and in the stage and pipeline runners become `Record` or `Pack` | few callers outside |
| 4.8 Foundation remainder | the other runtime uses in foundation (233 runtime, 367 constexpr data, 36 in src/foundation) | none outside |
| 4.9 Fixy remainder | the other runtime uses in fixy, one unit for each directory: concurrent, session, os, handle, top level | none outside |

### 13.2 Order and parallelism

| Wave | Slices | Reason |
|---|---|---|
| 4-I | 4.3, 4.4, 4.5, 4.7 | few shared caller files |
| 4-II | 4.1, then 4.2 | 4.1 and 4.2 touch many of the same caller files, so they run one after the other |
| 4-III | 4.6 | needs the Os results of 4.1 |
| 4-IV | 4.8 and the five units of 4.9, in parallel | internal only |

### 13.3 Special care

- **Determinism.** A type change changes reflected names, so row-hash goldens and federation keys can move. Regenerate the goldens (`utils/tools/dump_row_hashes.cpp` and its golden file) in the same commit, and list each moved identity in the report.
- **Hot paths.** For each slice that touches a hot path (4.2 rings, 4.3 simd and indexing, 4.4 atomics), compare the disassembly of the hot functions before and after, and run the benches.
- **ABI.** A `Result` that returns in memory where the std type returned in registers is a regression. Check the return convention of each hot `Result`.

### 13.4 The S4 gate

- The plugin enforces the full include allowance of every base layer as errors, with an empty ledger.
- A reflection test walks every base namespace and refuses a std runtime type in any public signature or data member.
- The link guard (R5) passes for `libcrucible.a`. Its first allowlist: `__cxa_guard_acquire`, `__cxa_guard_release`, `__cxa_guard_abort`, `__cxa_atexit`, `__gxx_personality_v0`, and `operator new` and `operator delete` only if the Ref family uses them.
- The full suite passes in every preset.

---

## 14. Stage 5: quarantine migration

**Goal.** Zero findings in every quarantined directory, except rows of the opt-out ledger. Each directory switches to error mode when it reaches zero.

### 14.1 Waves and units

| Wave | Unit | Directory | Findings at census | Notes |
|---|---|---|---|---|
| 5A | harness | `test/test_assert.h`, the pass-print helper, the runner `main` | about 5,000 test findings | `assert` becomes `fixy::fatal("assert: {}", #cond)`. The pass print becomes one harness function. `argv` enters through `fixy::os::args` |
| 5A | small | `examples/`, `src/` | 38 and 879 | |
| 5B | core-ir | `include/crucible/` IR, Expr, ExprPool, Graph, MerkleDag, Arena users | part of 6,247 | ExprPool and MerkleDag hold most raw pointers |
| 5B | vigil | BackgroundThread, Vigil, TraceRing, MetaLog, RegionCache | part | hot paths: benches before and after |
| 5B | cipher | Cipher, Serialize, TraceLoader, ledger | part | fix the Cipher write results (task list 397) here |
| 5B | cntp | `include/crucible/cntp/` | part | |
| 5B | observe | observe, warden, topology | part | |
| 5B | cog-mimic | cog, mimic, forge | part | |
| 5B | perf | perf, the BPF users | part | BPF C headers get whole-file opt-out regions |
| 5B | vis | vis | part | TraceVisualizer, SugiyamaLayout, SvgRenderer are among the largest files |
| 5B | rest | the remaining `include/crucible` files | part | |
| 5C | bench | `bench/` | 2,602 | `bench_harness.h` first (541 findings) |
| 5C | fuzz | `test/fuzz/` | 1,272 | the libFuzzer entry keeps an opt-out region |
| 5C | tests | `test/` split by directory: fixy, foundation, core tests in three or four groups | 18,262 less the harness share | attack tests and raw-probe fixtures get reasoned regions |
| 5C | fixtures | the negative-compile fixtures | part of test | a fixture that probes raw behavior on purpose keeps a region |
| 5D | vessel | `vessel/` | 102, partial | needs `TORCH_DIR`. The PyTorch ABI stays behind regions |
| 5D | tools | `utils/tools/` | about 265 | |

### 14.2 Steps for each unit

1. Build in REPORT mode and read the findings of the directory. Group them by family.
2. Do the mechanical recipes of section 11 first, one family at a time.
3. Do the judgment items. Record each decision in the final report.
4. When the base does not support a thing, send a request to the extension lane (section 17). Leave that site as it is. Continue with the rest of the directory.
5. When the lane lands the primitive, merge main and migrate the waiting sites.
6. When the directory has zero findings apart from ledger rows, set `enforce <directory> error` in the rule table and regenerate the ledger, in the same commit.
7. Do the checks of section 5.4.

### 14.3 Order and parallelism

- 5A runs first. The harness unit changes the test infrastructure, so the 5C test units need it.
- The 5B units run in parallel. Each owns its directories. A header that two units need is owned by the unit that owns its directory, and the other unit waits for its landing.
- 5C starts when 5A lands. It can run while 5B runs, because the files do not overlap.
- 5D runs last. It needs the families in their final form.
- At most six heavy units run at the same time (section 5.3).

### 14.4 The S5 gate

- Every quarantined directory has `enforce error` in the rule table.
- The opt-out ledger holds only the permitted reasons of section 16.
- The full suite passes in every preset, and the benches show no regression against the S0 numbers.

---

## 15. Stage 6: close

1. **Error mode everywhere.** Set the plugin default to ERROR in every preset. REPORT stays available for census runs.
2. **The opt-out review.** Read each region of the ledger again. Remove the regions that a primitive now covers. Each remaining region names its reason and its owner.
3. **CLAUDE.md.** Rewrite the layer rules, replace the std tables of §III and §IV with the families, and describe the quarantine, the doors and the extension lane. Remove every rule that the plugin now enforces by construction, and name the plugin in its place.
4. **Guards.** Remove each guard that the plugin makes redundant, after a plant shows that the plugin catches what the guard caught. Candidates: the parts of `check-banned-calls.py` that ban std calls, `no-reserve-allowlist.txt`, `no-reinterpret-allowlist.txt`, parts of `check-syscall-capability.py`. Keep each guard that sees what the plugin cannot see (dead `#if` branches, file layout, git history).
5. **The adversarial campaign.** A fresh agent tries to get a std object, a raw pointer or a C call past the plugin: through templates, `auto`, aliases, macros, ADL, lambda captures, structured bindings, default arguments, `decltype`, `requires`, `sizeof`, and a header that a quarantined file includes from a base directory. Each finding is fixed, or pinned on a ledger that can only shrink.
6. **Decision point DP3.** With no `try`/`catch` left in the tree, decide whether to build with `-fno-exceptions` and `-fno-rtti`.
7. **Decision point DP4.** Decide whether foundation stays public to quarantined code or goes behind fixy re-exports.

**The S6 gate** (the final gate):
- Four presets (Debug, Release, TSan, UBSan-strict) with zero failures.
- The plugin in error mode in every tree.
- The link guard passes for every binary except the vessel library.
- The gauge numbers at or below the S0 numbers.
- vessel_acceptance passes in Release.

---

## 16. The opt-out policy

A region is permitted only for these reasons. The reason string starts with the reason class.

| Reason class | Where | Example |
|---|---|---|
| `ABI:` | a boundary that a foreign ABI fixes | PyTorch `at::Tensor` and dispatcher function pointers in vessel, `LLVMFuzzerTestOneInput`, `main(int, char**)` |
| `C-HEADER:` | a header that a C compiler also compiles | BPF event structs shared with Clang BPF programs |
| `PROBE:` | a test whose subject is the raw behavior | a negative fixture that forges a proof with `start_lifetime_as`, an attack test |
| `ORACLE:` | a differential test | a family test that compares with std |
| `MEASURE:` | a bench measurement that must see raw timing | `asm volatile` fences, raw cycle buffers in the bench harness |

Rules:
- A region covers the smallest span that the reason needs.
- Each region is a row of `utils/scripts/quarantine-optout-ledger.txt`: file, reason class, reason text. The ledger can only shrink after Stage 5.
- A region with any other reason fails review.
- A region never covers a missing primitive. That is a request to the extension lane.

---

## 17. The extension lane

The extension lane is one unit that stays open from the S3 gate until Stage 6. It owns `include/foundation/core/` and `include/fixy/Core.h` after Stage 3.

**A request** from a migrating unit gives:
- the call site (file and line) and the code that needs the primitive
- what the code must do, and why no existing family operation does it
- the hot-path status of the site

**The lane decides** within one day:
1. An existing operation does it. The lane answers with the spelling.
2. A new operation fits a family and a verb. The lane builds it with tests, fixtures and the cost check, and lands it.
3. The request asks for a new family or a new verb. The lane stops and asks the owner.

**Rules:**
- A new operation needs at least one real call site.
- A new operation obeys section 10.2 in full.
- The lane lands at most one change for each request, so migrating units can merge often.

---

## 18. Sync points

| Id | When | What every unit does |
|---|---|---|
| S0.1 | unit 0a lands the Debug flag change | Merge main. Rebuild before the next landing (every Debug object compiles again one time) |
| S0.2 | unit 0b lands the check-file convention | The 0c units start |
| S0 | Stage 0 gate | All Stage 0 units have landed or stopped. The gauge runs again |
| S1 | Stage 1 gate | The ratchet is live. From here each landing must pass it |
| S2 | Stage 2 gate | The include policy of the base is an error |
| S2.x | a 2e split lands | Units that include the split header merge main before their next landing |
| S3 | Stage 3 gate | The families are on main. The extension lane opens |
| S4.x | slice 4.1 or 4.2 lands | Every running unit merges main before its next landing, because these slices touch many files |
| S4 | Stage 4 gate | No std type in the base surface |
| S5.x | a directory switches to error | Units that touch that directory merge main first |
| S5 | Stage 5 gate | Error mode in every quarantined directory |
| S6 | final gate | The migration is closed |

---

## 19. Parallelism and resources

### 19.1 The host

2 x EPYC 9655 (384 threads), 503 GB of memory. The host is shared: on 2026-10-01 other users kept about 200 of the 384 threads busy, and about 150 GB of memory was available. One heavy translation unit uses 0.6 GB to 2 GB of compiler memory, and one generated oracle file used 15 GB before unit 0d shards it. A guard run builds a subset of the tree two times.

### 19.2 Budgets

| Situation | Units at the same time | Jobs for each unit |
|---|---|---|
| one unit alone on the host | 1 | -j64 |
| a wave of heavy units | at most 6 | -j16, for builds and for ctest |
| a wave with read-only units | the heavy limit plus any number of read-only units | read-only units run no build |

Before each heavy command, run `free -g`. Wait while the "available" column is below 64 GB.

### 19.3 What can run together

| Stage | Parallel | Serial |
|---|---|---|
| 0 | 0a, 0b, 0d, 0e, 0f, 0g. Then the 0c units. Then the 0h units | 0c after 0b (S0.2). 0c-R after 0e. 0h after every 0c |
| 1 | 1a, 1b, 1d. Then 1c, 1e | 1c after 1a. 1e after 1b |
| 2 | 2a, 2b, 2c. Then the 2e groups | 2d after 2a, 2b, 2c. 2e after 2d |
| 3 | inside G2 and G3 | G1, G2, G3, G4, G5, G6 in order |
| 4 | 4.3, 4.4, 4.5, 4.7. Then 4.8 and the 4.9 units | 4.1 then 4.2. 4.6 after 4.1 |
| 5 | the 5B units. 5C with 5B | 5A first. 5D last |

### 19.4 Conflicts

- A unit owns its directories. It edits a file outside them only through a `--replace` pair, or after the owner of that file lands.
- When two units need the same file, the coordinator decides the order before the second unit starts.
- A unit that finds a file in another unit's open tree stops and asks the coordinator.

---

## 20. Risks

| Risk | Effect | Mitigation |
|---|---|---|
| Reflection-heavy family code is slow to compile | Builds get slower, not faster | The gauge runs at each gate. A family that raises the S0 numbers by more than 10% stops for a redesign |
| A family costs more on a hot path | Latency regression | The disassembly check and the benches in 10.2, 13.3 and each 5B unit on a hot path |
| The families lose tested std behavior | New bugs | Differential tests against std, property tests, TSan, the adversarial pass |
| Plugin blind spots | A std object gets through | The plugin does not see unevaluated operands, dependent members, using-declarations, unused default arguments, namespace-scope dynamic initializers or contract conditions. A reflection test covers the base surface. The Stage 6 campaign covers the rest |
| Row-hash goldens and federation keys move | Cross-build witness breaks | Regenerate in the same commit and list each moved identity |
| A moved check stops running | A header loses its compile-time proof silently | The check file is the sentinel of its header, and the sentinels are in the default build. Unit 0b shows that a failing check fails the build |
| A check depended on its includer | A property that only a later header can break is no longer checked where it breaks | Unit 0c keeps such a check in the header, with a reason row, and reports each one |
| A split changes a test result | A case moves or disappears | The ctest list and the oracle row counts are compared before and after |
| Agents collide on shared files | Lost work | Ownership tables, `--replace` pairs, merges before landing, compare-and-swap landings |
| Notification floods | The owner gets spam | Blocking commands only, no Monitors, one final report for each unit |
| The migration stops halfway | Two styles in one tree | The ratchet and the directory switch keep each finished directory finished. A stop leaves some directories in report mode, which is safe |

**Stop criteria:**
- The S0 gate misses its targets after every Stage 0 unit has landed, and the owner declines the modules spike (DP1).
- A family cannot meet zero hot-path cost for an operation on a hot path.
- The gauge shows a slowdown of more than 10% at a gate, with no fix in the same stage.

In each case, the stage stops and the coordinator reports to the owner with the numbers.

---

## 21. Checklists

### 21.1 Before a unit starts

- [ ] The brief names the unit's directories and files.
- [ ] The brief names the stage, the sync points that affect the unit, and the job budget.
- [ ] The brief repeats sections 5.1 to 5.6.
- [ ] No other open unit owns the same files.

### 21.2 Before a unit lands

- [ ] Main merged, Debug build of `all` passes.
- [ ] The unit's tests pass. For a base header change, the full suite passes.
- [ ] Release build and the unit's Release tests pass.
- [ ] Guard run: "red only with the change: none".
- [ ] `refresh-derived.sh --check` and `check-clang-format.sh` pass.
- [ ] The ratchet passes (from Stage 1).
- [ ] Each new test and fixture goes red on the old code.
- [ ] Shared files go in as `--replace` pairs.

### 21.3 After the last landing

- [ ] Worktree, branch, grun directory and patches removed.
- [ ] No process of the unit still runs.
- [ ] The final report follows section 5.6.

---

## Appendix A. First audit verdicts for the admitted list

Unit 1b confirms or changes each verdict. "Use" counts come from the QPLUG census or from QDESIGN.

| Name | Current reason | Verdict | Restriction or replacement |
|---|---|---|---|
| `<type_traits>` | compile-time queries | KEEP | none. Many traits are compiler builtins, and the flag matrix confirms that they do not depend on a flag |
| `<concepts>` | compile-time predicates | KEEP | none |
| `<meta>` | reflection | KEEP-RESTRICTED | results only in constant evaluation. `define_static_array` reaches run time only through `fixy::static_array` |
| `<limits>` (`numeric_limits`) | constants of fundamental types | REPLACE | Scalar `max_of`, `min_of`, `min_normal_of`, `epsilon_of`. `numeric_limits<T>` gives silent zeros for a non-arithmetic T, and `min()` of a float is the smallest normal value |
| `std::move` | a cast | KEEP | none |
| `std::forward` | a cast | KEEP | none |
| `std::move_if_noexcept` | a cast | DROP unless used | it copies silently when the move can throw |
| `std::as_const` | a cast | KEEP | none |
| `std::to_underlying` | an enum cast | KEEP | none |
| `std::bit_cast` | a bit copy | REPLACE | Scalar `bit_cast` (requires `BitValid`) and `decode` (returns `Option`). `std::bit_cast` is undefined behavior for a bit pattern that is not a value of the destination |
| `std::declval` | unevaluated only | KEEP | none |
| `std::integer_sequence` and its aliases | compile-time packs | KEEP | none. `__integer_pack` is the builtin behind them |
| `std::tuple_size`, `std::tuple_element` | the tuple protocol | KEEP-RESTRICTED | specializations for `Record` only, for structured bindings |
| `std::unreachable` | an optimizer hint | REPLACE | Report `unreachable`, a cold fatal exit in every build. `std::unreachable` is undefined behavior if the program reaches it |
| candidate: `std::initializer_list` | the language makes it | KEEP-RESTRICTED | parameters only. As a data member it dangles |
| candidate: `std::strong_ordering`, `weak_ordering`, `partial_ordering`, `is_eq` family | `<=>` makes them | KEEP | none |
| candidate: `std::byte` | the language gives it the aliasing right | KEEP | none |
| candidate: integer spellings (`std::size_t`, `std::uint64_t`, and the others) | typedefs of fundamental types | KEEP | none |
| candidate: `std::nullptr_t` | the type of `nullptr` | KEEP | none |
| candidate: `std::meta::info` | the type of `^^` | KEEP | constant evaluation only by the language |
| candidate: `std::source_location` | call-site capture | REPLACE | Report `Site`, built from `__builtin_FILE`, `__builtin_LINE`, `__builtin_FUNCTION` |
| candidate: `std::contracts::contract_violation` | the handler signature | KEEP-RESTRICTED | only in `src/foundation/ContractHandler.cpp` |
| candidate: `std::align_val_t`, `std::nothrow_t` | allocation signatures | KEEP-RESTRICTED | only inside the Ref and Region doors |

---

## Appendix B. Census snapshot

At a572c4bf, from the QPLUG report. Unique findings.

| Kind | include/crucible | src | vessel | test | fuzz | bench | examples | other | total |
|---|---|---|---|---|---|---|---|---|---|
| std name | 3,340 | 528 | 22 | 8,665 | 781 | 1,244 | 7 | 178 | 14,765 |
| std object | 1,639 | 234 | 3 | 3,398 | 300 | 355 | 3 | 46 | 5,978 |
| C struct | 25 | 24 | 1 | 46 | 0 | 19 | 0 | 0 | 115 |
| raw pointer | 717 | 38 | 66 | 830 | 100 | 313 | 7 | 4 | 2,075 |
| function pointer | 5 | 2 | 0 | 70 | 2 | 1 | 0 | 0 | 80 |
| C array | 117 | 9 | 3 | 621 | 30 | 128 | 9 | 3 | 920 |
| raw new/delete | 17 | 0 | 2 | 53 | 1 | 18 | 0 | 0 | 91 |
| C call | 387 | 44 | 5 | 4,579 | 58 | 524 | 12 | 34 | 5,643 |
| **total** | **6,247** | **879** | **102** | **18,262** | **1,272** | **2,602** | **38** | **265** | **29,667** |

Largest rows: `__assert_fail` 2,009. `std::expected` objects 1,567, and its accessors about 2,840. `printf` 1,219. `fprintf` 1,130. `std::array` subscripts 1,079. `std::vector` subscripts 720. `std::basic_string_view` objects 645. `std::optional` objects 446. `std::span` objects 362. `std::atomic` objects 236. `const char*` objects 332.

---

## Appendix C. Size estimates

| Work | Estimate | Source |
|---|---|---|
| New base code (the families) | about 8,000 lines (E) | QDESIGN |
| Base retrofit | about 1,500 runtime uses plus about 600 API sites (E) | QLAYER, QDESIGN |
| Changed lines outside the base | about 11,700 (E) | QDESIGN |
| Opt-out regions at the end | 110 to 170 (E) | QDESIGN |
| Stage 0 | 1 to 2 weeks of unit time (E): 0a, 0b, 0d to 0g a few days each; the five 0c units 1 to 3 days each; 0h open | this plan |
| Stage 0 effect | build CPU about half, fixture CPU about a third on a changed tree, about zero on an unchanged tree (E) | scratch-copy experiments |
| Stages 1 and 2 | 2 to 3 weeks of unit time (E) | QLAYER |
| Stage 3 | 4 to 6 weeks of unit time (E) | QLAYER |
| Stages 4 and 5 | 4 to 6 weeks of unit time, mostly parallel (E) | QLAYER, QDESIGN |

Unit time is the sum of the time of all units. The calendar time is shorter when units run in parallel, and it depends mostly on the speed of one build, which Stage 0 sets.

---

## Appendix D. Source reports

These reports are in the session scratchpad, and they give the full detail behind this plan:
- `stage-cd/qdesign-families.md`: the family design, the measurements outside the base, the migration rules, the admitted list rule.
- `stage-cd/qlayer-imports.md`: the include inventory of the base, the dependency graph, the layer proposal, the import policy, the sanitation plan.
- `stage-cd/qplug/`: the plugin census (731 raw reports, the summary, the census script).
- `stage-cd/open-gate-ledger-plan.md`: the plan for the 30 open templates of the gate ledger.

The scratchpad ends with the session. Copy the reports into `misc/` before the session ends if they must stay.
