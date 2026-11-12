# Quarantine and std removal: the migration plan

Date: 2026-10-01. Main at a6e53b48 when this plan was written.

This document is the plan for the next large migration of Crucible. It has two goals:

1. **Strip std.** The base (`include/foundation` and `include/fixy`) depends only on the compiler: the language, the compiler builtins, and a short audited list of compile-time std names. We vendor close to nothing. Each unsafe or surprising part of std that the tree needs gets a safe replacement in the base.
2. **Quarantine everything behind fixy.** All code outside the base (crucible, src, vessel, test, fuzz, bench, examples, utils/tools) holds only objects of base types. If the base does not support a thing, we extend the base, safely. A GCC plugin turns each violation into a compile error.

The method is the one that Stage A to Stage D used: stages, units in worktrees, patch landings, guard runs, witnesses that go red on the old code, and gates with acceptance checks.

---

## 1. Decisions

The owner approved these decisions on 2026-09-30, 2026-10-01, 2026-10-03 and 2026-10-08.

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
| D11 | 2026-10-03: the local guard run and the local test runs are the only gates. Nobody watches GitHub CI after a landing. Main goes to GitHub in one sync about once a day, and after each sync one person reads the last run. |
| D12 | 2026-10-03: a thin base. This decision makes D2 narrower. glibc keeps only the thread start, signals, `dlopen`, the `mem*` functions that GCC calls, `clock_gettime`, and `malloc` behind one allocation door of the Ref family. The owner decision of 2026-10-11 adds three calls to this list. The exit hook door `src/fixy/os/ExitHooks.cpp` calls `atexit` and `at_quick_exit`, and the thread door `src/fixy/os/ThreadTasks.cpp` calls `pthread_atfork` for its fork hook. Each other door uses Linux UAPI headers and one syscall primitive for each architecture (R4, R5). |
| D13 | 2026-10-03: no replacement of the C runtime inside the process. The base writes no general-purpose `malloc`, no vDSO parser, and no thread or TLS setup of its own, because the vessel shares one process with Python and PyTorch. A binary with no glibc is possible only as a separate daemon, later (section 17). |
| D14 | 2026-10-03: fast hardware access has one shape. The kernel does the control plane one time: mappings, queues and the IOMMU. User space does the data plane: ring writes, doorbell writes and polls, with no syscall for each operation. The IOMMU stays on. A user-space driver through VFIO is only for a device that Crucible owns alone. AF_XDP is out of scope, because the networking scope is the scope of NCCL. |
| D15 | 2026-10-03: the utils system of section 18 is Stage 1B, a part of Stage 1 (section 8.6a). The S1 gate includes the end state of Stage 1B: `utils/` holds only the files of the new system. Stage 1B deletes each source file of the inventory, or moves it to its target path. The owner approved the decisions 0 to 22 of section 18.23. |
| D16 | 2026-10-08: the base floor has a door directory of its own. `include/foundation/os/` holds the declarations, and `src/foundation/os/` holds the door files. The directory holds the raw system call primitive for x86_64 and aarch64. It also holds the two calls of the floor: the write of the Report sink and the read of `/proc` for the debugger probe. Each door of `fixy/os` uses that primitive. Each system header still has exactly one door file, and a header that the floor uses has its door in foundation. Rule R4 then has two door directories: `fixy/os` and `foundation/os`. Unit 2b creates the directory of foundation. |

---

## 2. The rules

These rules hold for the whole migration and after it.

- **R1. Two regions.** The base is `include/foundation` and `include/fixy`, plus their source files in `src/foundation` and `src/fixy`. Every other C++ file in the repository is quarantined.
- **R2. The base depends only on the compiler.** A base header can include only the headers that the rule table (Stage 1, unit 1a) allows for its layer. The first allowance for every layer is: `<cstddef>`, `<cstdint>`, `<type_traits>`, `<concepts>`, `<utility>` (casts, `declval` and index sequences only), `<compare>`, `<initializer_list>`, `<new>`, `<meta>`. Other names of `<utility>` that the base uses on 2026-10-09 are `std::exchange`, `std::pair`, `std::in_place`, `std::to_underlying`, `std::cmp_less`, `std::in_range` and `std::swap`, and no rule counts them outside the files of the language. The audit in unit 1b can make this list shorter. It cannot make it longer without an owner decision.
- **R3. Builtins before headers.** When a builtin gives a thing, the base uses the builtin and not a header: `__atomic_*`, `__builtin_memcpy`, `__builtin_memmove`, `__builtin_memset`, `__builtin_add_overflow` and its family, `__builtin_bit_cast`, `__builtin_ia32_pause`, `__builtin_ia32_rdtsc`, `__builtin_thread_pointer`, `__builtin_FILE`, `__builtin_LINE`, `__builtin_FUNCTION`, `__builtin_unreachable` (only inside the fatal exit), vector extensions.
- **R4. One door for each kernel subsystem.** Only the files of the two door directories can include a system header, a Linux UAPI header, or a glibc header. The directories are `include/fixy/os` with `src/fixy/os`, and `include/foundation/os` with `src/foundation/os` (D16, unit 2b). Each such header has exactly one owner file (the door). No system type crosses a public signature of a door. These parts were added on 2026-10-03 (D12):
  - A door uses the Linux UAPI headers (`<linux/*.h>`, `<asm/unistd.h>`) and the syscall primitive of its architecture (x86_64 and aarch64). It does not use a glibc header, except for the calls that D12 keeps in glibc.
  - One door unit includes UAPI headers or glibc headers, never both, because some of their declarations conflict.
  - A door uses only the system calls that both architectures have: the `*at` and `p*` forms, for example `openat`, `fstatat`, `ppoll` and `dup3`.
  - A deny list in the rule table holds each system call that the base never issues raw, because glibc owns its state: `rt_sigaction` and the other signal calls, `clone` and `clone3`, `set_robust_list`, `set_tid_address`, `arch_prctl` and each call that sets the TLS register, and `exit`. The plugin refuses a raw call of a denied number.
  - The platform floor states a minimum kernel version. A start-up check refuses an older kernel with a clear message, because a raw system call has no glibc fallback.
- **R5. Zero runtime library use.** No object in `libcrucible.a` and no binary other than the vessel library references a libstdc++ symbol outside a short allowlist. A link-level guard proves this. On 2026-10-03 the rule grew to libc (D12). The symbol ledger holds the libc symbols and the libstdc++ symbols of `libfoundation.a`, `libfixy.a` and `libcrucible.a`, with one allowlist for each library:
  - `libfoundation.a` references only the `mem*` functions that GCC calls (`memcpy`, `memmove`, `memset`, `memcmp`), the libgcc helpers (for example `__udivti3`), `__stack_chk_fail` and `__tls_get_addr`. This allowlist is a condition of the S2 gate (section 9.3). On 2026-10-09 the ledger holds these ratchet rows of `libfoundation.a` outside it: nine C runtime symbols (`abort`, `fwrite`, `getenv`, `stderr`, `strcmp`, `strlen`, `__fprintf_chk`, `flockfile` and `funlockfile`), and `std::__glibcxx_assert_fail` in the Debug build.
  - The decision of 2026-10-17: the allowlist of `libfoundation.a` also holds the calls of two doors, each one only from the member of its door. The abort door `foundation/os/Abort.h` calls `abort` in each build. The deny list of R4 refuses `rt_sigprocmask`, `tgkill`, `rt_sigaction` and `exit_group`, and R20 refuses a change of the disposition of `SIGABRT` in a door. The raw primitive then cannot send `SIGABRT` to the thread in the way that the tests and the abort probe need. The C library owns the signals, and its `abort` is the smallest correct exit. In a build with ThreadSanitizer, the read and write functions of R18 in `src/foundation/os/Intercepted.cpp` call `read` and `write` and read `errno` through `__errno_location`. The rule `artifact-symbols` holds the two parts (`FOUNDATION_DOOR_CALLS`). The diagnostic sink and the JSON emitter write through the sink write of the Report family, and the base reads no environment variable for the format of a diagnostic: a program that wants the JSON record installs a sink that calls the emitter.
  - `libfixy.a` adds only the calls that D12 keeps in glibc, and each of them is in its door.
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
- **R11. The ratchet.** From Stage 1, the count of violations can only fall. The ledger counts each file, kind and entity, and the findings that each opt-out region covers. A change that raises a count fails the local test run (D11).
- **R12. Zero hot-path cost.** A family operation on the hot path compiles to the same instructions as the code it replaces. The proof comes from a type (an index type, a fixed extent, a memory order in the operation name), not from a branch at each access.

The rules R13 to R17 keep compile time low. Stage 0 applies them to the tree, and every later stage obeys them.

- **R13. A header does not check itself in each includer.** A header holds no self-test namespace and no `static_assert` at namespace scope. Those checks live in the check file of the header, which one translation unit compiles one time (section 7.2). A `static_assert` inside a class or a template stays, because it guards an instantiation.
- **R14. Compile-time tables are lazy.** A reflection walk, a roster or a table that not every includer reads is a variable template, or it lives inside a template. Only the translation units that read it evaluate it.
- **R15. No heavy inline body in a header.** A non-template inline function is analyzed in each includer. A body that is cold or large goes to a source file.
- **R16. Builtins before headers.** A base header does not include an intrinsics header, `<thread>` or `<chrono>` for one function. Rule R3 gives the builtins.
- **R17. No giant translation unit and no giant function.** A generated file comes out as shards. A test file that compiles for more than 60 s at `-j1` is split by subject. A function large enough to make the debug-information passes explode is split.

The rules R18 to R20 keep the base thin and keep it a good guest in a process that it shares with Python and PyTorch (D12, D13). The owner added them on 2026-10-03.

- **R18. One code path under the sanitizers.** A door has the same code path in each build. A sanitizer cannot see a raw system call, so the door tells it what the call did: `__tsan_acquire` and `__tsan_release` on the futex door, and ASan poisoning of the free space in each arena. There is one exception. A call that can block goes through the intercepted glibc function in a TSan build, because TSan delivers a deferred signal only at an intercepted call, and a raw blocking call can then hang.
- **R19. A static TLS budget.** The base holds only small `thread_local` data. A measured rule of `utils/registry.txt` (section 18.5) holds the size of `.tbss` and `.tdata` of each base object. A hot path takes its context as a parameter and reads no TLS, because in a shared library a TLS read is either a call of `__tls_get_addr` or a use of the static TLS block, which `dlopen` can find full.
- **R20. A door changes no process-wide state.** No door changes signal dispositions, resource limits, the CPU affinity of the whole process, the environment, or the current directory. A door can change the affinity of a thread that the base started. The owner amended the rule on 2026-10-11: a door changes a signal disposition only in a call that takes an Init context, or in its own handler to put back the disposition that such a call replaced. The signal door of `fixy/os/Signal.h` is that door.

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
| Stage 1B | The units 1B.0 to 1B.10 of Stage 1, which build the utils system (section 8.6a). A unit 1B.N belongs to Stage 1B and is not unit 1b |
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
- Land with `python3 utils/bin/loop land -m SUBJECT --patch FILE --`, with a patch and `--replace` pairs for shared files. The command runs the copy of the main checkout, and its gate builds and tests the commit before the swap. Section 5.5 lists the shared files.
- Merge main before each landing. If main moved, rebuild and rerun the tests of the unit.
- After the last landing: remove the worktree and its grun directory, delete the branch and the patch files.

### 5.3 Commands, time and memory

- **Run every command in the foreground.** A command must block until it ends. Use a timeout of up to 600,000 ms, and split a long step into several calls. Never start a Monitor. Never end the turn while a command runs. Each stop sends the owner a notification.
- Report only once, in the final report. Send an interim message only for a question that blocks the unit.
- Run `free -g` before each heavy command. Wait while the available memory is below the level of section 20.2.
- Build with the job count that the stage gives. The default is one compile job for each logical core of the host and half as many link jobs for each unit, and one heavy unit runs at a time (section 20.2).
- Never `nice` a ctest run.

### 5.4 Checks before landing

1. The Debug build of `all` passes.
2. The tests of the unit pass. For a change to a base header, the full Debug suite passes.
3. A Release build of `all` passes, and the Release tests of the unit pass.
4. The gate of `loop land` passes: `loop land --check` with the words of the landing. The landing runs the gate again.
5. `loop derive --check` passes. Regenerate each derived file (mint inventory, row-hash goldens, rosters) in the same commit with `loop derive`. Before unit 1B.5 lands, the command is `refresh-derived.sh --check`.
6. `guard check --rule clang-format` passes on the changed files. Before unit 1B.5 lands, the command is `check-clang-format.sh`.
7. From Stage 1: the quarantine ratchet passes. No directory count rises.
8. Each new test or fixture goes red on the old code, for the defect or the property itself, not only because it names a new symbol.

### 5.5 Shared files

These files get edits from many units. Land each edit as a `--replace` pair:
- `CMakeLists.txt`, `test/CMakeLists.txt`, `test/*/CMakeLists.txt`
- `misc/mint-inventory.md`
- `utils/registry.txt`, `utils/pins.txt`, `utils/ledgers/*.txt` and the plants of `utils/plants/`. Before unit 1B.3 lands, these files are `utils/scripts/*-allowlist.txt`, `utils/scripts/*-roster.txt` and `utils/scripts/*ledger*`
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
                 | Stage 1B: utils system      |
                 | 1B.0 defects (first)        |
                 | 1B.1 1B.2 1B.3 (serial)     |
                 | 1B.4 to 1B.8j               |
                 | 1B.9 1B.10 (last)           |
                 +--------------+--------------+
                                | S1 gate (with the 1B end state)
                 +--------------v--------------+
                 | Stage 2: base hygiene, done |
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

In Stage 1, unit 1B.0 lands first. Unit 1B.1 starts at its sync point after unit 1e lands (section 8.6a). The S1 gate includes the end state of Stage 1B, so Stage 2 starts only after the last unit of Stage 1B.

---

## 7. Stage 0: build speed

**Goal.** Make each build and each test run at least two times faster, before hundreds of guard runs start. Every later stage multiplies the cost of one build. The speed comes from how the tree uses C++ (rules R13 to R17), not from a changed compiler (D10).

**The right tail comes first (owner, 2026-10-01).** Builds and tests run at a high job count (up to `-j192`), so the wall time is the longest single job, not the total CPU divided by the jobs. A change that makes one long job short is worth more than a change that makes the total smaller. A split that adds total CPU to remove a long job is correct. The targets (owner, tightened 2026-10-01): no build job takes more than 15 s in the ninja log of a clean Debug build at `-j64` on this host, and no fixture (cold) and no test takes more than 10 s. On main at e028fb57, a clean build at `-j64` took 82 s wall, with 22 compile jobs above 20 s and 73 above 15 s (the longest 33 s). The full cold ctest at `-j64` took 165 s, with 18 guards above 15 s (the longest 140 s), 6 fixtures above 15 s and 117 above 10 s, and no other test above 4.2 s. At the baseline, the tail was one 315 s translation unit, sixteen more above 40 s, seventeen guards above 20 s (the longest 192 s) and fixtures up to 29 s. Every ordinary test took less than 5 s.

**Entry.** Wave E finished. The QPLUG plugin is on main. The baseline of section 3 is measured.

**What the baseline shows.** A header runs its own compile-time checks again in every translation unit that includes it. That repeated work is about half of all front-end time, and the 2,062 negative fixtures pay it too, on every ctest run. The rest is a small set of outliers: one generated file that alone sets the build wall time, two files whose back end explodes, and six slow guards.

### 7.0 The gauge (part of unit 0a)

1. `utils/scripts/build-gauge.sh` configures a new build directory with no compiler cache, and it builds `all` with the job count of `--jobs`. Then it runs the tests in one ctest run, the negative fixtures together with the other tests. The ctest run does the census checks after the last fixture, because these checks read the records of the fixtures. The script records the wall time, the user and system CPU time and the largest resident set of one process for each step (`/usr/bin/time`). It prints the 20 slowest compile steps from the ninja log and the 10 slowest tests from the ctest log. With `--test-regex`, ctest runs only the tests with a name that agrees with the regular expression.
2. `utils/scripts/tu-sample.py` measures a fixed sample with `-fsyntax-only` and with the full command, and prints the CPU time of each: `test/foundation/test_row_hash.cpp`, `test/test_arena.cpp`, `src/canopy/Lifeguard.cpp`, `test/fixy/test_collision.cpp`, `test/test_region_cache_repeated.cpp` (it replaced `test/test_vigil.cpp` after the split of that file), `test/test_cipher.cpp`, `test/fixy/test_session_handle.cpp`, `test/fixy/test_owned_region_every_total_0.cpp`, `test/test_ledger.cpp`, `utils/tools/crucible_hwprobe.cpp`, `test/fixy/test_session_global_attack_two_roles_1.cpp` (the slowest part of each split file), and five fixtures (`neg_atom_cv_qualified_rejected`, `neg_aligned_buffer_copied`, `neg_permission_brand_not_empty`, `neg_rule_l003_borrow_raw_clone`, `neg_vigil_ring_other_brand`). With `--headers`, it also measures each named header alone. The sample list is part of the script, so each run measures the same files.
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
2. When the header names a type from its own self-test namespace outside that namespace (for example `fixy/channels/StageEndpointBridge.h`), move that type out of the self-test namespace first.
3. When a check depends on the translation unit that includes the header (for example a count of members that a later header can add), it stays in the header, with a reason row in the ledger. Report each such row.
4. Set the ledger rows of the unit to zero, in the same commit.

Each unit reports `tu-sample.py` before and after, and the change of the clean build CPU time from `build-gauge.sh`. The ledger is a shared file: each unit edits only its own rows, through `--replace` pairs.

### 7.4 Unit 0d: shards and splits

**Goal.** No translation unit that this unit owns compiles for more than 20 s at `-j1` in Debug (R17).

1. **The session oracle.** The generated files in `test/session_oracle/` come out of their generator as shards. Choose the shard size so that each shard compiles in at most 20 s. The shards of one family are source files of the one executable of that family, so the ctest names do not change. The number of rows and each answer stay the same.
2. **Heavy test files.** Split `test/fixy/test_session_global_attack.cpp` and each other test file that compiles for more than 60 s mostly in the front end, by subject, into several source files of the same executable. The ctest names and every case stay the same. Files whose back end is the cause go to unit 0e.
3. **The oracle self-test.** `session_oracle_self_test` (`utils/scripts/session-oracle.sh --self-test`) takes 192 s. Make it take at most 20 s with the same verdicts.
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

**Goal.** No guard takes more than 10 s wall on a warm store (tests have a 10 s tail target).

1. Measure each of `no_unchecked_access`, `proof_routes`, `start_lifetime`, `federation_admission`, `atom_roster_joined_self_test` (125 s to 163 s at the baseline). Find where the time goes. Then do the same for each other guard above 20 s: `trait_guard` 45 s, `banned_calls` 41 s, `no_ffast_math` 40 s, `no_combine_ids_duplicate` 40 s, `syscall_capability` 37 s, `host_owners` 36 s, `detail_namespace` 35 s, `no_coordination_refs` 34 s, `parse_clean` 24.5 s, `atom_roster_joined` 22.5 s, `derived_pins` 21.6 s. A shared cache that serves many guards comes first. `session_oracle_self_test` (192 s) belongs to unit 0d, which owns the oracle scripts.
2. `utils/scripts/preprocessed.py`: one shared, content-keyed store in `~/.cache/crucible/preprocessed/`, with a size limit and an age limit, shared by every guard and every worktree. Scan each distinct (file, content hash) chunk one time, not one time for each translation unit that includes it.
3. Keep what each guard covers. Each guard gives the same verdict on the same tree, and each self-test and plant still passes.

### 7.8 Unit 0h: lazy compile-time work (after units 0c)

**Goal.** Apply R14, R15 and R16 to the headers that are still expensive after the check moves. One unit for each of foundation, fixy and crucible, in parallel.

1. List each header whose standalone `-fsyntax-only` cost is above 0.5 s after 0c. For each, read the `-ftime-report` table.
2. Make eager namespace-scope tables lazy (a variable template, or a table inside a template). Move a heavy non-template inline body to a source file.
3. Remove `<thread>`, `<chrono>` and `<functional>` from base headers where a builtin or a lower header gives the same thing. `foundation/effects/Ctx.h` uses `<thread>` for thread identity only. `__builtin_thread_pointer()` compared as an address gives a unique identity for each live thread. Do a test that two live threads never get the same identity, and that one thread always gets the same identity.
4. Each change keeps every answer: the row-hash goldens do not move unless a reflected name changes, and then the unit regenerates them and lists each moved identity.

### 7.8b Unit 0i: the tail splits (after units 0c)

**Goal.** No build job of the tree takes more than 15 s in the ninja log of a clean Debug build at `-j64`.

The first part of 0i started early, on 2026-10-01, for the crucible test files whose back end makes them long (test_vigil_dispatch, test_region_cache, the model tests, test_vigil and others). No check-move unit edits those files. The fixy test files on the list wait for the 0c units, because those units can edit them.

1. Build `all` from clean on main after the 0c units land, and list each translation unit above 20 s from the ninja log. At the baseline the list held, among others: test_vigil_dispatch 66 s, test_region_cache 58 s, test_vigil 47 s, test_mlp_trace 46 s, test_vit 42 s, test_resnet 42 s, test_vigil_deadline_watchdog 38 s, test_transaction_owner 38 s, vessel_api 36 s, rejections_generated 35 s, test_background_thread_run_in_row 33 s, test_owned_region 32 s, test_end_to_end 29 s.
2. Split each test file on the list by subject into several source files of the same executable. The ctest names and every case stay the same. A generated file comes out of its generator as shards.
3. When one header alone costs more than half of the 20 s, the split cannot help. Report that header to the matching 0h unit, with its `-ftime-report` table.
4. **Witnesses.** The ctest list is the same before and after. A clean build at `-j192` (or the highest job count that the memory check allows) shows no translation unit above 20 s.

### 7.8c Unit 0j: the fixture and test tail (after units 0c)

**Goal.** Each negative fixture compiles cold in at most 10 s, and each test runs in at most 10 s (owner, 2026-10-01: tests and fixtures must be fast, with a short right tail).

1. Run every fixture cold (`CRUCIBLE_NEG_CACHE=0`) and every test, and list each one above 10 s. At the baseline, the slowest fixtures took 18 s to 29 s (`neg_row_hash_census_undisposed_carrier` 29 s, the vigil and rule fixtures 18 s to 22 s), mostly for the headers that they include.
2. For each one: include only the header that the fixture attacks, make the tables it does not read lazy (with the 0h units), or split a test into several tests over disjoint cases.
3. `test/neg_compile_driver.py` reads the include directives of each header as text to record the names that a lookup tried. That breaks the rule that no check reads C++ as text. Replace the scan with the compiler's own list (`-E -dI`, about 0.25 s, and only on a miss). The same pass gives a key for the three fixtures that stop with a fatal error and get no dependency file (`neg_platform_unsupported_architecture`, `neg_platform_apple_aarch64`, `neg_layer_foundation_includes_fixy`), so they can use the store too.

### 7.8d Unit 0k: the compile-time checks (owner, 2026-10-01)

**Goal.** Each pattern that made the build or the tests slow gives a warning or an error before it lands again. A warning does not fail. An error fails.

1. **The report format** (on main at 89e31d686). `utils/scripts/check_report.py` prints each finding as `PATH:LINE: warning|error: [CHECK] MESSAGE`. A check writes its warnings to `<build>/check-warnings`, and the CI step "Check warnings" prints them, because ctest hides the output of a test that passes. `utils/scripts/budgets.txt` holds the thresholds of each measured check.
2. **Ratchet.** Each check starts with a ledger of the violations on main. A ledger row gives a warning on each run. A new violation gives an error. An exact count cannot rise above its row, and a row cannot keep slack.
3. **The checks**, in four units:

| Unit | Check | Kind |
|---|---|---|
| chk-header | a `static_assert` in a non-template function body of a header; eager reflection at namespace scope of a header (R13, R14) | parse tree, new finding kinds of `header_checks` |
| chk-static | a heavy standard header included in a header (R16); a unity build or a PCH; a padding byte in the element type of a fixed list of 64 or more elements | parse tree, compile database, compiler |
| chk-launcher | a compile launcher that records CPU, wall time and peak memory per object; the budgets step-memory, object-size, header-alone | measured, with a ledger |
| chk-limits | `-fconstexpr-ops-limit` toward the GCC default with reasoned exceptions; preset timeouts; a re-configure that rebuilds nothing | exact limit, measured, witness |

4. **Thresholds.** An exact count fails at its threshold. A time fails only above the noise that the unit measures on this host, and it warns at the target.
5. chk-header starts after the units that change `check-header-checks.py` and the fixy headers land.

### 7.9 Stage 0 order and parallelism

| Wave | Units | Needs first |
|---|---|---|
| 0-I | 0a, 0b, 0d, 0e, 0f, 0g | entry |
| S0.1 | 0a lands | every running unit merges main and rebuilds |
| S0.2 | 0b lands | the check-file convention is on main |
| 0-II | 0c-F, 0c-W, 0c-S, 0c-C. Then 0c-R | S0.2. 0c-R also needs 0e |
| 0-III | 0h-foundation, 0h-fixy, 0h-crucible, 0i, 0j | every 0c unit |

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
| 0i | the test files on its list and their CMake rows, except the files of 0d and 0e |

Shared files (`CMakeLists.txt`, `test/*/CMakeLists.txt`, CLAUDE.md, the ledger) go in through `--replace` pairs (section 5.5).

### 7.10 The S0 gate

The owner's target (2026-10-01): after an edit of a base header, `cmake --build` uses all 192 cores and gives a full build in about 15 s, and a full test run takes about 15 s more. At 192 cores the wall time is at least the total CPU divided by 192, so this target sets a total-CPU budget (about 2,500 CPU-s for the build and about 2,500 CPU-s for the tests), in addition to the tail. System time counts too: each unit reports the user and system time of its jobs.

All numbers come from `build-gauge.sh` on the same host as the baseline:
- The tail: no build job takes more than 15 s in the ninja log of a clean Debug build at `-j64`. No fixture (cold) and no test takes more than 10 s.
- The clean Debug build of `all` takes at most 60 s wall at `-j192` (or at the highest job count that the memory check allows, with the job count stated).
- `ctest` of the full Debug suite takes at most 60 s wall at `-j192` with a cold fixture store. A second run on an unchanged tree compiles no fixture.
- The total CPU counts too, because the wall time of a step at 192 jobs is at least its total CPU divided by 192. The budget table of CLAUDE.md §XV holds the warning thresholds of the totals.
- The header-checks guard is in error mode with an empty ledger, apart from reasoned rows.
- The full suite passes in the Debug, Release, TSan and UBSan-strict presets, with identical results for every determinism test.

**Decision point DP1.** If the gate misses a target, the owner decides whether the modules spike runs. The owner decided on 2026-10-01: the spike ran, and header units and precompiled headers change results with GCC 16 (`misc/gcc16-modules-pch-defects.md`). The tree tries them again with GCC 17.

**The measurement of 2026-10-02** (commit c78ee01d3, Debug preset, 192 jobs). The host had 376 usable threads and a load of 12 to 20 from a virtual machine and monitoring tools. It had 156 GB of available memory, and another user held a pool of 96 GB of huge pages.

| Case | Build | Tests | Sum |
|---|---|---|---|
| Clean build, no ccache, cold fixture store (median of 3) | 21.3 s, 2,320 CPU-s | fixtures 18.3 s, 2,566 CPU-s; other tests 8.9 s | 48.5 s |
| A code edit of `foundation/Platform.h` in a warm build directory (median of 3) | 19.3 s, 872 compiles, about 3,500 CPU-s | 30.1 s, about 5,460 CPU-s | 49.4 s |
| No edit (median of 3) | 0.18 s | 14.0 s, about 930 CPU-s | 14.2 s |

After the edit, each test group alone:
- The fixtures take 20.1 s and 3,602 CPU-s. With the result store off, they take 14.9 s and 2,642 CPU-s. So on a run where each lookup misses, the store adds about 960 CPU-s and 5 s.
- The guard scripts take 13.1 s and 1,518 CPU-s. The fill of the preprocessed store and the four guards that wait for it make a chain of about 14 s.
- The other tests take 2.3 s.

With ccache off, the edit build takes 18.2 s and 3,213 CPU-s, so ccache adds 10 % to 18 % on that path. System time is 15 % to 25 % of each run. The cause is the host memory settings, which the owner has not decided yet.

The state of each criterion:
- Tail: the slowest compile job takes 11 s to 14 s at 192 jobs, the slowest cold fixture 9.5 s, and the slowest test 7 s to 9 s. Not measured at 64 jobs.
- Clean build: 21.3 s. Met.
- Full test run with a cold fixture store: 27.2 s. A second run compiles no fixture. Met.
- Header-checks ledger: 22 debt rows remain (15 eager evaluations, 2 checks in function bodies, 5 rows in the ledger-probe headers). Not met.
- The four presets: each full suite passes. Two defects that the measurement found are fixed: the seal livelock under ThreadSanitizer (772a45908) and a false array-bounds error in the UBSan-strict build (c78ee01d3). Met.
- The owner's target of 15 s for the edit build and 15 s for its tests: not met (19.3 s and 30.1 s). Both steps are bound by their total CPU. The levers are the store cost on a full miss, the guard chain after a base-header edit, the fixture compile CPU, the build CPU and the system time.

**The second measurement of 2026-10-02** (commit d6ef48653, Debug preset, `-j 192` for each build and each test run). The first measurement gave no job count to `cmake --build`. Ninja then used its default of 378 jobs. Each run had a cache root of its own. Each clean run had a new root, and the runs in the warm build directory had one warm root. The default cache root and the default ccache directory did not change. Before most runs, `vmstat` showed 7 to 40 processes in the run queue, and the CPUs were free for 90 % to 98 % of the time. At three times, a process outside the measurement used 35 % to 43 % of the CPUs. The host had 60 to 117 GB of free memory. Its swap held 127 GB. A different user held a pool of 96 GB of huge pages.

| Case | Build | Tests | Sum |
|---|---|---|---|
| Clean build, no ccache, cold cache root (median of 3) | 15.2 s, 2,017 CPU-s, 1,025 compiles | 23.8 s, 4,044 CPU-s, in one ctest run of 2,616 tests | 39.0 s |
| A code edit of `foundation/Platform.h` in a warm build directory (median of 3) | 16.8 s, 2,285 CPU-s, 880 compiles | 21.8 s, 3,966 CPU-s | 38.6 s |
| No edit (median of 3) | 0.18 s | 10.1 s, 382 CPU-s | 10.3 s |

The edit is the edit of the first measurement: a statement `static_cast<void>(N);` in the body of `breakpoint()`, with a new N in each run. With a new N, ccache and the fixture store hold no result for the edited units. The runs of each case were these:
- Clean case: the build times were 14.8 s, 15.2 s and 15.8 s. The test run times were 22.0 s, 23.8 s and 23.9 s.
- Edit case: the build times were 16.8 s, 16.6 s and 17.2 s. The test run times were 20.9 s, 22.0 s and 21.8 s. Six more edit builds had times of 14.9 s to 20.4 s, and the median of all nine edit builds is 16.8 s.
- No-edit case: the test run times were 29.0 s, 10.0 s and 10.1 s. In the first run, `test_ledger_probes_huge_page` used 24.1 s.

From c78ee01d3 to d6ef48653, these times decreased:
- The clean build, from 21.3 s to 15.2 s
- The edit build, from 19.3 s and approximately 3,500 CPU-s to 16.8 s and 2,285 CPU-s
- The test run after the edit, from 30.1 s and approximately 5,460 CPU-s to 21.8 s and 3,966 CPU-s
- The test run with no edit, from 14.0 s to 10.1 s.

After the edit, each test group alone, in a run with no other load (in parentheses, a run with the load of the outside process):
- The fixtures use 12.7 s and 2,222 CPU-s (13.8 s and 2,320 CPU-s). With the result store off, two runs used 11.5 s and 1,950 CPU-s, and 12.4 s and 2,142 CPU-s. When the store holds a result for no fixture, it adds 0.3 s to 1.2 s and 80 to 270 CPU-s. At c78ee01d3 it added 5 s and 960 CPU-s.
- The guard scripts use 7.7 s and 1,105 CPU-s (9.1 s and 1,215 CPU-s). The fill of the preprocessed store uses 5.0 s and 535 CPU-s. The three parts of the session oracle self-test use 8.0 s and 258 CPU-s. The other guards use 6.4 s and 281 CPU-s.
- The other tests use 2.1 s and 91 CPU-s (6.4 s and 111 CPU-s).

Two pairs of edit builds give a comparison of ccache on and off. With ccache off, the build times were 17.0 s and 18.8 s. With ccache on, they were 18.9 s and 17.8 s. The difference is less than the noise of the host. At 64 jobs, the clean build used 31.0 s and 1,658 CPU-s for the same 1,025 compiles. At 192 jobs, the same work uses 2,017 CPU-s, approximately 22 % more. A possible cause is that two jobs operate on one core. System time is 7 % to 9 % of each build and 14 % to 19 % of each test run.

The records of the build launcher give this sequence of jobs in an edit build:
- Until approximately 13 s, each of the 192 job slots holds a compile.
- Then, for 4 s to 6 s, only 2 to 35 job slots hold a job. The four walk units of `test/layer` start at 11.9 s to 13.5 s, and each uses 5.8 s to 7.4 s. Some shards of the session oracle test start at 7.6 s to 9.2 s, and each uses 8 s to 10 s. The build completes when the last of these compiles completes.
- In one run, 339 of the 357 compiles that start after 9 s are layer sentinels. A layer sentinel and a walk unit are objects of an object library, and no link step waits for them. Ninja starts these objects after the objects that a link step waits for.

Two tests had failures in some runs. The gauge sets `CCACHE_DISABLE=1` for its full run, and ctest gets the variable too. `ccache_relocation_self_test` then has a failure in each gauge run, and it has no failure in the warm build directory. `session_oracle_store_self_test` had a failure in 2 of 16 runs at 192 jobs, in the case "a warm static unit comes from the store, and no compile runs". It had no failure in 32 runs at a lower load.

The state of each criterion at d6ef48653:
- Tail: in a clean run at 64 jobs, the slowest compile used 8.3 s, the slowest cold fixture used 5.8 s and the slowest test used 8.5 s. Met. At 192 jobs, the slowest compile after an edit uses 10.7 s to 11.3 s (`test/fixy/test_session_handle.cpp`). The slowest fixture after an edit uses 8.0 s to 8.7 s (`neg_row_hash_census_undisposed_carrier`). In two warm runs, `test_ledger_probes_huge_page` used 12.0 s and 24.1 s. In the first test run of the warm build directory, four `test_ct_taint_fixy_*` tests used 10.9 s to 11.7 s.
- Clean build: 15.2 s. Met.
- Full test run with a cold cache root: 23.8 s. A second run on the unchanged tree compiled 0 of the 2,080 fixtures. Met.
- Header-checks ledger: no count row stays. The 64 rows are keep rows, each with its reason: 27 eager folds, 25 static_asserts, 9 eager instantiations and 3 eager evaluations. Met.
- The four presets: this measurement built only the Debug preset. At 867246c98, the last commit with a completed CI run, CI built the Debug, Release, TSan and UBSan-strict presets on x86_64 and aarch64. Each full test run had no failure. Met.
- The owner's target of 15 s for the edit build and 15 s for its tests: not met (16.8 s and 21.8 s). At 192 jobs, 15 s of wall time is approximately 2,900 CPU-s. The edit build uses less CPU time than that, and its tail controls its wall time. The tests use 3,966 CPU-s, and their total CPU time controls their wall time.

The levers that remain are these, in the order of their size. For the tests after an edit (21.8 s and 3,966 CPU-s):
1. The fixture compiles: 2,222 CPU-s, 56 % of the run, approximately 1.07 CPU-s for each of the 2,080 fixtures. If each fixture uses 0.3 CPU-s less, the run decreases by approximately 620 CPU-s and 3 s. `neg_row_hash_census_undisposed_carrier` alone uses 7.2 CPU-s and 32.5 G instructions, more than the warning threshold of 28 G.
2. System time: 540 to 730 CPU-s, 14 % to 19 % of the run.
3. The fill of the preprocessed store: 535 CPU-s and 5.0 s. After a base-header edit, it preprocesses each unit again.
4. The other guards: 281 CPU-s. The three parts of the session oracle self-test use 258 CPU-s. With 7.1 s to 7.6 s each, they are the slowest guards.
5. The other tests: 91 CPU-s.

For the edit build (16.8 s and 2,285 CPU-s, a CPU bound of approximately 12 s at 192 jobs):
1. The order of the jobs: the walk units and some shards of the session oracle test start late, and they make the tail of 4 s to 6 s. If these long compiles start first, the build can complete near 13.5 s.
2. The longest compiles: `test/fixy/test_session_handle.cpp` uses 10.7 s to 12.3 s. `test_session_rollback.cpp`, `test_session_recording_decorators.cpp`, `test_session_crash_stop.cpp` and `src/BackgroundThread.cpp` use 10 s to 12 s each. Each of them keeps the build at more than 10 s.
3. The compile CPU: in the clean build at 64 jobs, the 369 layer sentinels use 266 CPU-s (17 %). The 37 shards of the session oracle test use 222 CPU-s (14 %).
4. ccache is not a lever. With ccache on and off, the edit builds differ by less than the noise.

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
4. **CI.** Add a CI job that runs the check on each push. Since D11, the local test runs are the gate, and the CI step runs after the daily sync.
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

Unit 1B.0 of Stage 1B waits for no unit. The other units of Stage 1B come after unit 1e, in the order of section 8.6a.

### 8.6a Stage 1B: the utils system

**Goal.** Stage 1B builds the utils system of section 18. At the S1 gate, `utils/` holds only the files of the new system. These are the package `assay`, the three masters, the data files, the ledgers, the plants and the C++ tools. Stage 1B deletes each source file of the inventory of sections 18.19 and 18.20, or moves it to its target path.

Section 18 is the design reference. Section 18.22 gives the parity protocol, the parity evidence and the deletion handoff, and each unit of this stage obeys them. The units have the names 1B.0 to 1B.10, and some units have sub-units, such as 1B.6a and 1B.8c. A unit 1B.N belongs to Stage 1B, and it is not unit 1b.

**Rules for each unit.** Each unit is a clean cut (R10). One commit moves the code, the CMake registrations and each caller. The same commit changes the paths of the CI files, the paths in CLAUDE.md and the method file of the agents. It also changes the `path` rows of the registry (section 18.13). Each unit obeys the checks of section 5.4 and the local guard run. No unit waits for a CI run (D11), and no unit waits for a subsequent stage.

**The units.** The column "Deletes" gives the files that each unit hands off to the owner (section 18.22). A legacy module is a guard script that unit 1B.1 moved into `assay/guard/legacy/` with no change of behavior (section 18.9).

| Unit | Content | Deletes | Comes after |
|---|---|---|---|
| 1B.0 | The high and medium defects of section 18.21, each as a small correction of its own, at the paths of today | None | No unit. It lands first |
| 1B.1 | The package and the masters, with no change of behavior. Each check script and each audit of section 18.19, `check_plugin.py`, the attack test of use-after-move and the contract test of header units move to `assay/guard/legacy/`. A temporary table in `assay/guard/` gives the legacy module of each rule. Each other Python file moves into `utils/assay/`, with its module name of section 18.20. CMake, ccache, ctest and CI start the masters. `sys.pycache_prefix` goes to the cache root. Each `sys.path` insert, CMake path, CI path and CLAUDE.md path changes | Each Python file of `utils/scripts/`, `utils/tools/session_oracle/`, `utils/tools/mutation/` and `utils/tools/quarantine/test/`, and the Python helpers of `test/`, at their paths of today | Unit 1e, and the sync point 1B.1 |
| 1B.2 | `assay.core.rows` and the schema modules in Python. The registry, from `guard-engines.txt` and `budgets.txt`, with the `path` rows of section 18.5. `assay.core.ledger`, from the ratchet. `guard list`, `write`, `warnings` and `explain`, and `cmake/Guards.cmake`. The meta rule `registry` with its parts shape, modules, witness, layout and tests. The ledger of `registry` holds each rule with only a legacy module or with no plant, and each subsequent unit removes its rows. The registry replaces the temporary table of 1B.1 | `guard-engines.txt`, `budgets.txt`, and the legacy modules of `check-guard-engines.py`, `check-git-isolation.py`, `check-ci-guard-labels.py` and `check-budget-history.py`. The print part of `report-check-warnings.py` goes, and its totals stay in its legacy module until 1B.8i | 1B.1 |
| 1B.3 | Each ledger and allowlist in `utils/ledgers/` with format 1, through `assay.core.ledger`. `process-wide-roster.txt` and `escape-doors.txt` become ledgers, and `compile-first.txt` goes to `cmake/`. Each owner reads its new file in the same commit. The part ledgers of the meta rule `registry` | The 15 empty ledgers, the 19 allowlists and the 23 other ledgers as separate files, and the allowlist part of `check-allowlist-keys.sh` | 1B.2 |
| 1B.4 | The rules of the bar (section 18.14) as ratchets, each with a ledger of the findings of today. The rules `types` and `python-lint` with the rows of mypy and ruff in `utils/pins.txt` (decision 16) | None | 1B.3, and 1B.5 for `utils/pins.txt` |
| 1B.5 | The shell scripts become modules, with no change of behavior: `setup install`, `setup check` and `setup gcc` with `utils/pins.txt`, and `loop pgo`, `loop derive`, `loop gauge --cold`, `loop bench` and `loop fuzz` (decision 21). The shell checks `check-clang-format.sh`, `check-no-throw-no-rtti.sh` and `check-allowlist-keys.sh`, and the check of `session-oracle.sh`, become legacy modules | Each shell script of `utils/`, `test/fuzz/run-afl.sh`, `bench/run-bench.sh`, `utils/toolchain/gcc/BASE`, `utils/toolchain/cmake/requirements.txt`, `utils/tools/mutation/survivors.txt`, and `check-atom-roster-joined.sh` with two fixtures in its place | 1B.2 |
| 1B.6a | `assay.core.proc` and `assay.core.git`. Each process start and each git command of the package moves to them | None | 1B.3 |
| 1B.6b | `assay.readers.cmake` with the queries of the file API, `assay.readers.compdb`, `assay.readers.ninja`, `assay.readers.ctest` and the records of `loop test-step`. Items 2, 3, 5 and 7 of section 18.4 | Each reader of `CMakeCache.txt` outside `assay.readers.cmake`, the CMake lexer, and the reads of the ctest log | 1B.6a |
| 1B.6c | `assay.readers.sarif` and `assay.readers.include_trace`. Item 6 of section 18.4 | The strips of colors, gutters and elisions in the fixture store | 1B.6a |
| 1B.6d | `assay.readers.elf`, `assay.readers.objdump` and `assay.readers.sections`. Items 4 and 10 of section 18.4 | The reads of the text of `nm` and `readelf` | 1B.6a |
| 1B.7 | The plugin. `rows.h` with its host program and the corpus of `rows-readers`. The rule table at `utils/rule-table.txt`, and the audit table at `utils/admitted-audit.txt`. `quarantine.cpp` divided into one source for each family. The section format 2 with rule IDs, and the kinds with hyphens. The plugin runner, the plugin witness and the part kinds of the meta rule `registry`. The fixtures of the plugin test move to `utils/plants/`. The unit writes the quarantine ledger again, with the kind `region` for the regions. No new rule | `layer-rules.txt`, `quarantine-admitted-audit.txt`, and the legacy modules of `check-quarantine-ratchet.py`, `check-quarantine-regions.py` and `check_plugin.py` | 1B.3. It lands at the sync point 1B.7 |
| 1B.8a | The plugin family owner, with the rule `syscalls` and its claims (refer to the table "Rules that a subsequent stage deletes"). The fourth GCC patch gives the family the names of the source: the use of a name, a using form, a member access, a reflection and a splice. Each rule of `check-no-unchecked-access.py` then moves to the plugin, and no parse-tree rule `member-splice` is necessary (section 18.11) | The legacy modules of `check-ctx-init-door.py`, `check-ctx-testing-boundary.py`, `check-detail-namespace.py`, `check-start-lifetime.py`, `check-no-unchecked-access.py` and `check-syscall-capability.py` | 1B.7, and 1B.8c for rule 6 of `check-no-unchecked-access.py` |
| 1B.8b | The plugin family home, with the `home` rows of the host-owner roster | `host-owner-roster.txt`, and the legacy modules of `check-federation-admission.py`, `check-host-owners.py`, `check-splits-orphan.py`, `check-trait-injection.py` and `check-no-combine-ids-duplicate.py` | 1B.7 |
| 1B.8c | The plugin family declaration property, with the mint records and the `witness` rows of the witness roster. The fixture generator of `check-witness-roster.py` becomes `loop derive witness-fixtures` | `witness-roster.txt`, and the legacy modules of `check-no-raw-array-member.py`, `check-permission-storage.py`, `check-process-wide-state.py`, `check-escape-doors.py`, `check-stub-discipline.py`, `check-lifetime-twin.py`, `check-padded-lists.py`, `check-mint-pattern.py` and `check-witness-roster.py` | 1B.7 |
| 1B.8d | The plugin family expression shape, with the shapes union, pointer-cast, allocator and raw-allocation of `check-proof-routes.py` | The legacy modules of `check-refined-pre-subsumption.py` and `check-syscall-grant-coverage.py` | 1B.7 |
| 1B.8e | New plugin code for the three specialization shapes of `check-proof-routes.py`. The shapes are an explicit instantiation, an explicit specialization of a function template, and an explicit specialization with a pointer to a private member. P14 case C13 shows that the walk of today does not read an explicit instantiation. So a probe plant first shows which tree holds one at `PLUGIN_FINISH_UNIT`. Estimate (E): approximately 200 lines in the source of the expression-shape family, and a plant and a control for each shape | The legacy module of `check-proof-routes.py` | 1B.8d |
| 1B.8f | The plugin families include closure and unit fact, with the `heavy` rows of `header-costs.txt`, and the parse-tree rule `layer-residual` | `header-costs.txt`, the legacy modules of `check-detsafe-ledger.py`, `check-heavy-includes.py`, `check-layer-boundary.py` and `check-contract-semantic.py`, and the C++ part of the legacy module of `check-no-ffast-math.py` | 1B.7 |
| 1B.8g | The parse-tree rule modules: `parse-clean`, `banned`, `contract-form`, `header-checks`, `derived-pins`, `use-after-move` and `lock-free-asserts`. The attack test of use-after-move gives the plants of `use-after-move`. The text reads of section 18.1 go | `fixy-only-paths.txt`, `test/guard_attacks/use_after_move_evasions.cpp`, and the legacy modules of `check-banned-calls.py`, `check-parse-clean.py`, `check-contract-form.py`, `audit-pre-callsite-count.py`, `check-walk-units.py`, `check-derived-pins.py`, `check-fixy-hw-discipline.py`, `check-fullness-guard.py`, `check-row-contains-discipline.py`, `check-use-after-move.py`, `check-lock-free-asserts.py` and the attack test of use-after-move. The legacy module of `check-header-checks.py` goes, except its mode `--standalone`, which stays until 1B.8i | 1B.6a |
| 1B.8h | The artifact rules `artifact-symbols` and `codegen-shapes` | The legacy modules of `check-libstdcxx-symbols.py`, `check-no-throw-no-rtti.sh`, `check-core-shapes.py` and `check-ct-disassembly.py` | 1B.5 and 1B.6d |
| 1B.8i | The build rules, with `tool-pins` and `session-oracle` | The legacy modules of `check-compile-cost.py`, `check-parse-cost.py`, `check-build-order.py`, `check-no-unity-pch.py`, `check-header-constexpr-ops.py`, `check-test-time.py`, `check-reconfigure-noop.py`, `check-vessel-acceptance.py`, `check-cache-size.py`, the check of `session-oracle.sh` and the contract test of header units. The parts of `check-no-ffast-math.py`, `check-header-checks.py` and `report-check-warnings.py` that stay in the legacy directory | 1B.6b and 1B.6c, and 1B.8f and 1B.8g for the parts that stay |
| 1B.8j | The meta rules `prose-bans`, `admitted-audit`, `decide-cite-ratio`, `derived-files`, `rows-readers` and `clang-format`, with clang-format in ctest. `loop derive mint-inventory` reads the mint records of the plugin | The legacy modules of `check-fixy-dim-prose.py`, `check-no-coordination-refs.py`, `check-admitted-audit.py`, `audit-decide-cite-ratio.py`, `check-clang-format.sh` and `check-allowlist-keys.sh` | 1B.5, 1B.6a, 1B.7, and 1B.8c for the mint records |
| 1B.9 | The deletions that wait for their users: the preprocessed store, after 1B.8a, 1B.8b and 1B.8e. No file stays in `utils/scripts/` or in `assay/guard/legacy/`, and the ledger of `registry` holds no row of the parts modules and witness | `preprocessed.py`, `assay/guard/legacy/`, and each `path` row of a path of today | 1B.8a to 1B.8j |
| 1B.10 | `loop test` reads the derived inputs of each rule and each test group. The budget table of CLAUDE.md §XV names rule IDs. Each rule of the bar has no finding outside its ledger rows, and each such row has a reason. The end state of this section holds | None | Each other unit |

**The order and the parallelism.**
- 1B.0 lands first, and it waits for no unit. It changes no path.
- 1B.1 starts at the sync point 1B.1, after unit 1e lands. It moves each path while other units land patches.
- 1B.1, 1B.2 and 1B.3 are serial, because each one changes paths or formats that each subsequent unit reads. 1B.5 comes after 1B.2, and it can run beside 1B.3.
- After 1B.3, the units 1B.6a and 1B.7 can run at the same time. 1B.4 also waits for 1B.5, because it adds two rows to `utils/pins.txt`.
- After 1B.6a, the units 1B.6b, 1B.6c and 1B.6d can run at the same time. Each one owns its reader and the callers of that reader.
- After 1B.7, the units 1B.8a to 1B.8d and 1B.8f can run at the same time, because each one owns its own plugin source. 1B.8e comes after 1B.8d, because it adds to the same source.
- 1B.8g to 1B.8j come after the units that the table names, and they can run beside the plugin units.
- Each unit adds rows to the rule table, the registry or the ledgers, so each one lands those files as `--replace` pairs (section 5.5).
- 1B.9 comes after 1B.8a to 1B.8j, and 1B.10 comes last. Section 19 gives the sync points 1B.1, 1B.3 and 1B.7.

**The end state.** After unit 1B.10, these facts hold, and the S1 gate checks them (section 8.7):
- `git ls-files utils` gives the layout of section 18.2 and the targets of section 18.20, and no other file. The part layout of the meta rule `registry` holds this fact with the `path` rows of the registry.
- The tree holds no `utils/scripts/` and no `assay/guard/legacy/`, and `utils/` holds no shell script.
- The helpers of `test/` that section 18.20 names, `bench/run-bench.sh` and `test/fuzz/run-afl.sh` are not in the tree.
- Each rule of the bar (section 18.14) has no finding outside ledger rows with reasons.
- No unit of Stage 1B waits for a subsequent stage. The rules of the next table are rules of the package with their rows of today, and a subsequent stage deletes them or their rows.

**Rules that a subsequent stage deletes.** The first design of section 18 left four parts for a subsequent stage. These parts become rules of the package in Stage 1B, so no source file of today stays at the end of Stage 1B:

| Rule | Engine | Content in Stage 1B | The subsequent step |
|---|---|---|---|
| `lock-free-asserts` | `tsast` | The rule of `check-lock-free-asserts.py`: each `std::atomic` of `include/crucible/{canopy,cntp,topology,warden}/` has a lock-free assert. Its 2 markers become admit rows. The 27 objects of today are in quarantined files | The Stage 5 units cntp, observe and rest move these objects to `fixy::Atomic`. The last of the three deletes the rule, with its module, its plants, its ledger, its registry row and its `path` rows (section 14.3) |
| `banned`, kind `vector-reserve` | `tsast` | The reserve ban of `check-banned-calls.py` on the base files. No base file calls `reserve` today. The rule `quarantine` reads no std name in a base file outside the files of the language, and on 2026-10-09, 47 base files include `<vector>` | The slice of Stage 4 that removes the last base include of `<vector>` deletes the kind. Then the include rules refuse the header (section 13.4) |
| `banned`, kind `process-start` | `tsast` | The spawn ban of `check-banned-calls.py` on the base files and on `vessel/`. No configuration of the ledger compiles `vessel/torch` (decision 5). Its one row names the ATen operator `clone` in a macro table of `vessel/torch/`, and it becomes an admit row with that reason | The base part goes in the last slice of Stage 4 (section 13.4). Then the include allowance and the libc rows of R5 refuse each route to a process start. The vessel part goes in the Stage 5 unit vessel, if a configuration of the ledger then compiles `vessel/torch`. When the two parts go, the kind goes |
| `syscalls` | `plugin`, owner | Each claim as a row of its ledger, and owner rows for the doors. On 2026-10-01 the script held 39 claims: 5 in base files and 34 in quarantined files. On 2026-10-09 the ledger holds 101 rows of the base configuration: 52 of base files (47 in the door directories, 4 in `fixy/session/Watch.h` and 1 in `fixy/concurrent/Pipeline.h`) and 49 of quarantined files. The configuration x86_64-debug-tsan adds 2 rows of `src/foundation/os/Intercepted.cpp`. The rule stays after the migration, because it holds each raw system call outside a door | Each raw call in a door directory keeps its claim row. Unit 2b added the deny list of R4 as rule-table rows and the kind `denied-syscall`. The calls of `Watch.h` and `Pipeline.h` move into doors with their code. A Stage 5 unit that moves a call of a quarantined row into an Os door removes its row |

The kind `file-stream` of the rule `banned` stays, and no subsequent stage deletes it. It reads the runtime directories that `RUNTIME_DIRECTORIES` of `banned.py` names, and each of these directories is quarantined. The plant `plant-file-streams` of the rule `quarantine` shows that the plugin refuses each object of a file stream in a compiled quarantined file. The plugin also refuses an object in the body of a generic lambda. But the rule `quarantine` gives no finding for an `#include <fstream>` that no source code of the unit uses. The kind refuses that include in each file of these directories, also in a source file of `src/` (plant `plant-stream-include`).

In a header of `include/`, the rule `heavy-include` also refuses the include, because `<fstream>` is a heavy header. A macro body that no unit expands is source code that no counted configuration compiles. Decision 5 of section 18.23 accepts that a plugin rule does not see such source code. The kind also reads such a body (plant `plant-stream-unparsed-macro`).

### 8.7 The S1 gate

- The rule table is on main, and the plugin and the layer guard read it.
- The owner approved the audit verdicts. Met: the owner approved each verdict on 2026-10-02.
- The ratchet runs in each local test run and fails on a plant. The test is `quarantine_ratchet` before unit 1B.7 and `guard_plugin` after it. CI is not part of a gate. On 2026-10-03, the owner made the local guard run the only gate, with a sync to GitHub approximately one time each day.
- The pragma macro works with the plugin on and off.
- CLAUDE.md has the quarantine section.
- The end state of Stage 1B holds (section 8.6a). These checks show it:
  - `guard check --rule registry` passes. So `git ls-files utils` gives the files of the `path` rows of the registry. No `path` row names `utils/scripts/`, `utils/assay/guard/legacy/`, a shell script or a helper of today.
  - The tree holds no `utils/scripts/` and no `utils/assay/guard/legacy/`. `guard check --rule no-shell` passes.
  - The helpers of `test/` that section 18.20 names, `bench/run-bench.sh` and `test/fuzz/run-afl.sh` are not tracked.
  - `guard check` passes for each rule of the bar, and each row of their ledgers has a reason.
  - Each rule of the table "Rules that a subsequent stage deletes" of section 8.6a is in the registry, with its plants and its ledger.

---

## 9. Stage 2: base hygiene

**Goal.** A base that obeys its include rules and its layer order, with no new primitive yet.

**State.** Done on 2026-11-12. Section 9.3 gives the check of each condition of the S2 gate.

### 9.1 Units

**2a, the floor.** `include/foundation/Platform.h` keeps the platform checks, attributes and macros. The debugger probe and the body of `fail_invariant` move to `src/foundation/Platform.cpp`, and the header keeps a `[[noreturn]]` declaration. Remove `<fcntl.h>`, `<unistd.h>`, `<cstdio>`, `<cstdlib>` and `<cstring>` from the header. Two more parts (R5, R19):
- The ledger of the rule `artifact-symbols` (section 18.12) also holds the libc symbols of `libfoundation.a`, `libfixy.a` and `libcrucible.a`, with the allowlist of each library from R5. The ledger keeps today's symbols as ratchet rows, so each later unit can only remove them.
- A measured rule of the registry holds the size of `.tbss` and `.tdata` of each base object, read with `assay.readers.elf`. The large thread-local buffer of `src/foundation/DiagRuntime.cpp` moves out of static TLS in this unit, because the vessel library loads only with a glibc tunable while it stays.

**2b, OS code into fixy/os.**
- Move `fixy/OwnedFile.h`, `fixy/OwnedMmap.h`, `fixy/Path.h` and `fixy/concurrent/Topology.h` into `include/fixy/os/`.
- The sysfs probe of Topology moves to `src/fixy/os/Topology.cpp`.
- The signal, `atexit`, `pthread` and clock code of `fixy/session/Watch.h` moves to `src/fixy/session/Watch.cpp`. That file includes no system header. It calls the doors of `fixy/os`, such as the thread door of unit 2c, the time door and the signal door. The layer X5 session is above the layer X3 os, and a layer can include each lower layer (section 12). Watch also uses `::write`, `std::atexit` and `::pthread_atfork`, so the doors must give these calls too. Its `sigaction` handlers change the signal dispositions of the process, and R20 refuses that change in a door. Unit 2b brings this conflict to the owner before it moves the code.
  - The decision of 2026-10-11: the owner amended R20, and the hooks install only through the mint `mint_watch_hooks` of `fixy/session/Watch.h`, which takes an Init context. A claim installs no hook. The cold code is in `src/fixy/session/Watch.cpp`, and it calls four doors: the signal door `fixy/os/Signal.h`, the exit hook door `fixy/os/ExitHooks.h`, the fork hook of the thread door `fixy/os/ThreadTasks.h` and the monotonic clock door `fixy/os/MonotonicClock.h`. The reports write through the Report family. The signal door sets an alternate signal stack on the thread that installs the hooks, and the thread door sets one on each thread that it starts. The memory of each stack comes from the guarded memory door `foundation/os/GuardedRegion.h`, with a guard of no access below it. A thread that the base does not start gets no alternate stack.
- Every caller moves in the same commit.
- The unit creates the door directory of foundation (D16): `include/foundation/os/` for the declarations and `src/foundation/os/` for the door files. The directory holds the raw syscall primitive and the two calls of the floor. The first call is the write of the Report sink, which `src/foundation/ContractHandler.cpp` holds at this time. The second call is the read of `/proc` for the debugger probe, which `include/foundation/Platform.h` holds at this time. Each door of `fixy/os` uses that primitive.
- The syscall primitive lands first: one inline-assembly system call for x86_64 and one for aarch64, in one door file, with the numbers from `<asm/unistd.h>`. The deny list of R4 goes into the rule table. The plugin rule `syscalls` (section 8.6a) gets the kind `denied-syscall`, which refuses a raw call of a denied number. Each claim of the rule for a call that moves into a door goes in the same commit. The platform floor gets the minimum kernel version and its start-up check.
- The Topology probe reads sysfs with raw `openat` and `read` through the file door, and it parses the CPU lists itself. Each error is a `Result`. Its eight `try`/`catch` sites go, and with them the use of `std::filesystem`, `ifstream`, `std::set` and `std::map` (about 40 libstdc++ symbols of `libfixy.a`). This part of slice 4.6 moves here.

**2c, thread start into concurrent.** Move `fixy/os/Spawn.h` to `include/fixy/concurrent/`. Move the thread half of `foundation/permissions/PermissionFork.h` to `include/fixy/concurrent/`. Foundation starts no thread after this unit. Two more parts:
- The thread door of `fixy/os/ThreadTasks.h` calls `pthread_create` and `pthread_join` directly, with the stack size, the guard page, the thread name and the affinity of the new thread. It uses no `std::thread`, so the libstdc++ thread symbols go, and so does the `_State_impl` row of the no-RTTI allowlist. The base uses no mutex, condition variable, cancellation, `detach` or thread key.
- A futex door gives the waits: wait on a word with an optional deadline, and wake one or all waiters. It carries the TSan annotations of R18. The blocking lock `arena_alloc_gate_` of the background pipeline moves onto it.

**2d, the include edges.** Fix the eleven edges that QLAYER found:
1. `foundation/contracts/Decide.h` includes `effects/Row.h` for one predicate. Move `row_subset` into effects.
2. `diag/RowHash.h` and effects include each other. Put the effect fold where it breaks the cycle.
3. `effects/Computation.h` and permissions include each other. Break the cycle.
4. `lattices/HappensBefore.h` and `StrongCounterLattice.h` include `effects/Ctx.h` for their context-bound mints. Move those mints above the lattices.
5. `AlignedBuffer.h` includes `diag/Catalog.h` for two tags, and `ChannelBinding.h` includes `diag/Runtime.h`. Move the tags to the tag vocabulary, or report through the base fatal exit.
6. `fixy/Witnessed.h` includes `session/Handle.h`, and `fixy/OwnedMmap.h` includes `os/AtomPack.h`. Move each to its layer.
7. Five concurrent headers include session headers. Move Endpoint, EndpointMint, SubstrateSessionBridge, SwmrSession and StageEndpointBridge to a new directory `include/fixy/channels/` (layer X6).
8. `concurrent/Pipeline.h` includes `os/Sched.h`, and `os/Spawn.h` includes `concurrent/ParallelismRule.h`. Unit 2c resolves the second. Resolve the first through a door. The affinity read of `caller_allowed_cpus_` goes through that door too, and `Pipeline.h` then includes no `<pthread.h>` and no `<sched.h>`. Done on 2026-10-13: the affinity door of `fixy/os/CpuPinned.h` and the scheduling door of `fixy/os/Sched.h` have the source files `src/fixy/os/CpuPinned.cpp` and `src/fixy/os/Sched.cpp`, which issue their calls through the raw primitive. `Pipeline.h` reads the mask through `read_affinity` of `fixy/os/Sched.h`.
9. `handle/LazyEstablishedChannel.h` includes session. Move it to `include/fixy/channels/`.
10. OS code outside fixy/os. Unit 2b resolves it.
11. Thread start in foundation. Unit 2c resolves it.

**The split of the layer F1 (owner, 2026-10-11).** The layer F1 algebra becomes six ordered layers. From the lowest rank to the highest, they are:
- algebra: `foundation/algebra/`, without `lattices/`
- lattices: `foundation/algebra/lattices/`
- diag: the files of `foundation/diag/` that are not in F0 core
- effects: `foundation/effects/`
- permissions: `foundation/permissions/`
- rest: each other file of `foundation/` and `src/foundation/`.

The split lands in the same commit as items 2 to 4. On the tree of 2026-10-11, the split refuses exactly 5 include edges, and items 2 to 4 remove these 5 edges:
- Item 2: `diag/RowHash.h` includes `effects/Effect.h` and `effects/Row.h`.
- Item 3: `effects/Computation.h` includes `permissions/Fwd.h`.
- Item 4: `lattices/HappensBefore.h` and `lattices/StrongCounterLattice.h` include `effects/Ctx.h`.

The present layers already refuse the edges of items 1 and 5, because the source of each edge is in a layer of F0. F1 algebra has rank 5, the only number between F0 doors (rank 4) and X1 wrappers (rank 6). The six layers then take the ranks 5 to 10, and the layers X1 to X5 move to the ranks 11 to 15. When the split lands with items 2 to 4, or after them, it adds no ledger row. The layer rows are an input of each compile. Each configuration then needs one full rebuild after the split lands.

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
- Each 2e group runs `loop gauge --sample` before and after, and reports the change.

### 9.3 The S2 gate

- The plugin enforces the include rules of the base as errors. The rule table sets `enforce include/foundation/ error include` and `enforce include/fixy/ error include`. The word `include` gives a row the scope of the three include kinds only.
- No base header includes a header outside the allowance of its layer, except the runtime std headers that Stage 4 removes. Those stay on a ledger that can only shrink.
- The layer order has no upward edge.
- The gauge shows no slowdown against the S0 numbers.
- `libfoundation.a` references no symbol outside its R5 allowlist. The ledger of `libfixy.a` and `libcrucible.a` holds ratchet rows only, with no new symbol.
- The static TLS budget holds, and the vessel library loads with the default glibc tunables.
- Each door of 2b and 2c uses UAPI headers and the syscall primitive, except the calls that D12 keeps in glibc. The thread door and the futex door pass their tests under TSan with no suppression.

**The check of 2026-11-12** (commit d36be7daf). The check used the Debug preset on the host luks-a, with 224 vCPU and nine other agents. These are the verdicts:
1. The include rules are errors: holds. The rule table has the row `error include` for `include/foundation/`, `include/fixy/`, `src/foundation/` and `src/fixy/`.
2. The allowance of each layer: holds. The quarantine ledger holds no row of the kinds `layer-header`, `door-header` or `upward-include` in a base directory. Each runtime std header of the base is a row `replace-pending:HEADER` of the ratchet. The row `allow F1_diag <charconv> until Report` had no includer in a preprocessor arm, and commit da020b73d removed it.
3. The layer order: holds. The rules `layer-boundary` and `layer-residual` give no finding. The kind `upward-include` is a compile error in the four base directories. Each configuration of the gate builds or parses `all`, and the landing of da020b73d passed in each of the seven configurations.
4. The gauge: holds. The median of the three edit builds is 10,934 G user instructions. The three runs of 2026-10-02 gave 10,750 G to 10,752 G, and the limit of the rule `edit-build-instructions` is 11,800 G.
5. The symbols of R5: holds. The rule `artifact-symbols` passes. Each row of `libfoundation.a` has the mark `libc-allow`: `__errno_location`, `__stack_chk_fail`, `abort`, `memset`, `read` and `write`. The rows of `libfixy.a` and `libcrucible.a` hold no new symbol.
6. The static TLS budget and the vessel: holds at commit 288f2561b. The rule `static-tls` passes: 3 members hold 20 bytes, and the largest holds 8 bytes. `test_process_wide_across_libraries` passes, and `dlopen` refuses its plant. Commit 4374d025f added the preset `vessel` and moved the schema cache of `vessel/torch/crucible_fallback.cpp` out of static TLS (below).
7. The doors: holds at commit 288f2561b. Each door that units 2b and 2c made obeys the condition. Commit 288f2561b moved `Topology::log_summary` from `fprintf` to the Report sink. `fixy/os/OwnedFile.h` still calls stdio, and the decision below keeps it out of the condition.

The vessel load of condition 6 gives these results. Commit 4374d025f replaced 36 KB of thread-local schema caches with one schema table of the process. The TLS segment of `libcrucible_dispatch.so` went from 36,876 bytes to 8 bytes. The preset `vessel` builds the vessel libraries against the patched PyTorch of `.venv-torch`. Its tier is `nightly`, so the gate does not build it, and a nightly configuration gives this witness.

In the build of the preset `vessel` at 288f2561b, ctest ran `vessel_test_default_tunables` through the test launcher, which gives a test no `GLIBC_TUNABLES`. The test passed. It imported torch 2.15.0a0+git8be2400 first, and then `dlopen` loaded each library of `build-vessel/lib`. Each library has the initial-exec TLS model. `readelf -lW` gives a TLS segment of 8 bytes for `libcrucible_dispatch.so` and 20 bytes for `libcrucible_vessel.so`.

The doors of condition 7 give these results. Each door of `src/foundation/os/` gives no finding of the C library. `Syscall.cpp`, `Errno.cpp` and `Auxiliary.cpp` include only UAPI headers, and `Intercepted.cpp` includes `<errno.h>` and `<unistd.h>` for the routes of R18. In `src/fixy/os/`, the thread door, the signal door, the exit hook door and the monotonic clock door call only the functions of their door headers. D12 names each of these functions, also `atexit`, `at_quick_exit` and `pthread_atfork` (commit 57fb285d7). The futex door calls the syscall primitive, and `test_os_thread_tasks` and `test_os_futex` pass in the `tsan` preset with `halt_on_error=1` and no suppression file.

`guard list` of the Debug build of 288f2561b gives no finding of the C library in `src/fixy/os/`. The door admission hides only a use, in a door file, of a declaration of its own door header. Before 288f2561b, the ledger counted one call of the C library in `src/fixy/os/`: the `std::fprintf` of `src/fixy/os/Topology.cpp`. In `include/fixy/os/`, the list gives the 15 calls and the 11 objects of the ledger. `OwnedFile.h` has 8 calls and 7 objects, `Socket.h` has 2 calls, and `Time.h` has 5 calls and 4 objects. The other headers that units 2b and 2c made or moved have no such finding: `Signal.h`, `ExitHooks.h`, `ThreadTasks.h`, `MonotonicClock.h`, `Futex.h`, `Topology.h`, `OwnedMmap.h` and `Path.h`.

The glibc imports of `libfixy.a` are the calls that GCC makes, the calls of `DOOR_CALLS` of the rule `artifact-symbols`, and the one ratchet row `memchr` of `Topology.cpp.o`. Commit 288f2561b removed the ratchet row `__fprintf_chk`. `memchr` comes from `std::string_view::find` in the parse code of `src/fixy/os/Topology.cpp`, and the allowlist of the rule does not hold it. The probe is a client of the file door and not a door. Slice 4.6 removes `memchr` with the other std uses of the probe, and the S4 gate refuses a ratchet row of `libfixy.a`. `libfixy.a` holds no libstdc++ thread symbol, but the `_State_impl` row stays for `libcrucible_vigil.a`, because `src/BackgroundThread.cpp` starts its thread with `std::jthread`.

The other calls of the C library in the door directories are these. Slice 4.1 removes each of them:
- `fixy/os/OwnedFile.h` opens and closes a file stream through stdio. Unit 2b moved this file.
- `fixy/os/Time.h` calls `clock_gettime` outside its door `src/fixy/os/MonotonicClock.cpp`. It also calls `clock_nanosleep`, which D12 does not keep in glibc.
- `fixy/os/Socket.h` calls `socket` through `<sys/socket.h>`.

The decision of 2026-11-12, for the owner to confirm: `OwnedFile.h` does not block the gate. It is a stdio wrapper that unit 2b moved into the door directory, and not a door that unit 2b or 2c made. `Time.h` and `Socket.h` are not doors of 2b or 2c either, and the check of d36be7daf read them in the same way. Slice 4.1 removes the stdio of `OwnedFile.h` after the Region family is on main. Before that family, code outside the base cannot make a `View`, read the characters of a `TextView`, or hold a buffer with a size from run time.

The gauge measured three edit runs at 96 jobs. The median edit build used 31.4 s, 2,812 CPU-s and 948 compiles. The median edit test run used 53.7 s and 4,248 CPU-s. At d6ef48653, at 192 jobs on the host of section 7.10, these steps used 16.8 s, 2,285 CPU-s and 880 compiles, and 21.8 s and 3,966 CPU-s. The host, the job count and the load are different, so these times do not judge condition 4. The first test run of the copy had a failure: with a cold cache, `guard_clang_format` used 1,575 G instructions, more than the limit of 500 G of the kind `test` of the rule `step-cost`.

Each of the seven conditions holds, so Stage 2 is done on 2026-11-12.

---

## 10. Stage 3: the primitive families

**Goal.** The eight families are on main with the full design of section 11, with tests, fixtures and zero hot-path cost. Seven families live in `include/foundation/core/`, and `include/fixy/Core.h` puts their public names into namespace `fixy`. The Os family lives in the two door directories of D16: its types in `include/foundation/os/`, and its doors in `include/fixy/os/`. No caller outside the base moves yet.

**The state on 2026-11-12** (main at d36be7daf). The first parts of six families are on main: Choice, Scalar, Ref, Atomic, Region with its text types, and Report. CLAUDE.md §XXII "The families on main" gives their rules. Record has no part on main. The Os family has the doors of Stage 2 (section 9) and no part in `foundation/core`.

### 10.1 Groups and order

| Group | Families | Needs first | Parallel inside the group | On main at d36be7daf |
|---|---|---|---|---|
| G1 | Choice | S2 gate | one unit | `Option`, `Result`, `Err`, `Unit`, `none` and `niche` |
| G2 | Scalar, Record | G1 | two units | `decode` and `enum_from` of Scalar |
| G3 | Ref (with the bump arena of `include/crucible/Arena.h`), Atomic | G2 | two units | `Box` and `mint_box`. `Atomic`, `CacheLine` and `Tally` |
| G4 | Region | G3 | one unit, the largest | `View`, `copy`, `fill`, `TextView`, `CText` and `FixedText` |
| G5 | Report | G4 | one unit | `Site`, `Fmt`, `report`, `fatal`, `unreachable` and `format` |
| G6 | Os | G5 | one unit | `Errno` and the doors of section 9 |

The groups are serial, because each family uses the ones before it. A part of a later group can land early when it uses only operations that are on main. The parts of G3, G4 and G5 in the table landed in this way.

The phases of `misc/12_10_2026_linear.md` land with the groups that its section 11 (item 4) names: phase B with G3, and phase C with G3 and G4. A proposal of section 11.11 gets no group until the owner approves it (section 17).

### 10.2 Instructions for each family unit

1. **The API.** Build the full API of the family in section 11, as one part of one language (CLAUDE.md §XXII, "The design bar of the base"). An operation stays in the design also when no call site uses it at this time. The owner made this decision on 2026-10-02. It replaces the rule that a type gets only the operations that the tree calls (the last sentence of R9, and the first rule of section 17). Before the first commit, write the design of the unit against the bar:
   - Each operation, with its contract, its cost and its verb of section 11.1
   - Each place where main, this plan and the bar do not agree, with the side that wins
   - The probes, the tests, the fixtures and the adversarial cases
   - The order of the commits. The operations with call sites in the census come first.

   An operation with no verb of section 11.1 and no name in the list of its family goes to the owner (section 11.11).
2. **The implementation.** Use only the allowance of the layer of the family and the builtins of R3. The layer of the seven families of `foundation/core` is F0 primitives. The Os family uses F0 doors and X3 os. No std runtime type appears in a signature or a data member. Each check of a family calls `fatal()` of the Report family, so it does the same work in each build and with each contract semantic. A check of a family is not a `CRUCIBLE_PRE`, because the ignore semantic removes a contract check.
3. **The layout.** The check file of the family states the size of each type with a `static_assert` (R13). `Option<Ref<T>>` has the size of one pointer. `Option<Id>` has the size of the id. A sum of plain parts of at most 16 bytes returns in registers under the x86-64 System V ABI, and a probe of item 6 shows it.
4. **The tests.**
   - A unit test for each operation, in one test file for each subject (R17).
   - A property test with Philox-seeded inputs for each invariant (for example: `copy` then `equal`, `sort_by_key` stable and ordered, `narrow` round trip).
   - A differential test against std for each operation that has a std counterpart, inside one opt-out region with the reason class `ORACLE:` (section 16).
   - A death test for each path that calls `fatal()`.
   - For Atomic: TSan runs, and a stress test of each operation under contention.
5. **The fixtures.** At least two negative fixtures for each gate and each mint (HS14). Each fixture attacks one gate. Its regex names the constraint expression or the text of the deleted function.
6. **The cost.** For each hot-path operation, add a probe pair to `test/shape/core_shapes.cpp`: the family form, and the raw or std form in an `ORACLE` region. The rule `codegen-shapes` compares the two (R12, CLAUDE.md §XXII). A pair that differs declares the difference with a true claim. For Region and Atomic, run the benches before and after.
7. **The adversarial pass.** A second agent tries legal uses that still dangle, leak, race, forge or misuse. Each finding is fixed, or pinned on a ledger that can only shrink.
8. **The re-export.** Add each public name of a family of `foundation/core` to `include/fixy/Core.h` with a using-declaration, and check it in `test/layer/checks/fixy/Core.cpp`. The doors of the Os family keep their namespaces in `fixy`.
9. **The protocol check.** Each family type gives only the verbs and the language protocols of section 11.1, and the names of the family list of its section. The check file of the family reads the public members of each type by reflection, and a name outside the three lists fails the build.
10. **The rows.** The unit removes no row of the rule table. Stage 4 removes an `allow ... until FAMILY` row when the last base include of its header goes. Stage 5 removes an `admit ... until FAMILY` row when the last use of its name goes. Section 11 names the rows and the R5 imports of each family.

### 10.3 Defects that the families fix

QDESIGN and the designs of 2026-10-17 found these defects. Each one is on main at d36be7daf. The unit of its family fixes it, with a witness that goes red on the old code:
- A Release build has no `_GLIBCXX_ASSERTIONS`, so a subscript of `std::array`, `std::span` and `std::vector` has no check, and `fixy::FixedArray::operator[]` forwards to it. CLAUDE.md §V states this fact. The Region family makes each index checked by its type.
- `fixy/OwnedRegion.h` merges into `View` and the Ref family (Appendix E, slice 4.3). Its arena door `mint_owned_region` takes a type that is not implicit-lifetime. The requirement of the Ref family: `BumpArena::array` takes only a type of `ImplicitLifetimeThroughout`, and no door of Ref or Region gives a run of bytes the type of a proof.
- `AtomicMonotonic` of `fixy/Mutation.h` takes a memory order as an argument, and it has relaxed reads. The Atomic family puts the order in the operation name.
- `foundation/AlignedBuffer.h` merges into `HeapArray` of the Region family (Appendix E, slice 4.3). `AlignedBuffer::allocate` leaves the heap bytes with no initial value. The requirement of the Region family: each mint of a `HeapArray` fills each element with zero or with a fill value. Only an implicit-lifetime element gets a view that only writes.
- CLAUDE.md §II.4 shows an arena that starts a lifetime with `std::start_lifetime_as`, and `include/crucible/Arena.h` does not. The reset is on main as `ArenaEpoch`. The Ref unit corrects the text.
- `TscMode::SteadyClockFallback` of `fixy/os/Time.h` reads the TSC. The Os unit deletes the enumerator.
- `Result` admits the payload `Err<F>` when `F` is not `E`. A `niche<T>` with no complete protocol falls to the plain form with no message. The flag of a trivial `Result` follows the union, so a 4-byte payload puts the flag at bit 32. The Choice unit fixes the three.
- `decode` into a payload with a niche gives the empty bits to `Option::some`, which calls `fatal()`. The Scalar unit gives no value for those bits.

### 10.3a Parts that the thin base adds (2026-10-03)

These parts come from D12 to D14 and the rules R18 to R20. Each one gets its witness in the unit of its group.
- **G3, Ref: one allocation door.** All base allocation goes through one door of the Ref family. At first the door calls glibc `malloc`, so a later change of the allocator is a change of one door. Hot memory and bulk memory come from arenas. Each arena poisons its free space for ASan (R18), so ASan still sees an overflow out of an arena object. The `until` row of `std::nothrow` in `Ref.h` goes when this door lands.
- **G5, Report: a prefix map.** The build gives `-ffile-prefix-map` and `-fmacro-prefix-map`, so `Site` and `__FILE__` hold a path relative to the source root. Two checkouts then give the same report text. gdb needs `set substitute-path` one time.
- **G6, Os: the I/O door on `io_uring`.** Files and sockets go through one I/O door on `io_uring`, with registered buffers and registered files. A plain-syscall fallback serves each host where `io_uring` is off, for example under the default seccomp profile of Docker or with the `io_uring_disabled` sysctl. A start-up probe chooses the path one time. SQPOLL is an option only, because it takes a core.
- **G6, Os: counter reads with `rdpmc`.** A counter door maps the perf page of each event one time and reads the counter with `rdpmc`, with no system call. When `perf_event_paranoid` refuses, the door reports no exact count, as the cost meter does today.
- **G6, Os: time.** The hot path reads `rdtsc` (`cntvct_el0` on aarch64). A wall-clock door calls glibc `clock_gettime`, which uses the vDSO.

### 10.4 The extension lane opens

At the S3 gate, the extension lane opens (section 17). It stays open until Stage 6. A request for a new family or a new verb goes to the owner. Section 11.11 lists the proposals that wait at this time.

### 10.5 The S3 gate

- All eight families are on main with the full API of section 11, with the tests and fixtures of section 10.2. An operation that waits has an owner decision.
- The protocol check of section 10.2 (item 9) passes for each family type.
- The rule `codegen-shapes` holds each hot-path cost claim, and the benches show it for Region and Atomic.
- The adversarial ledger of each family is empty, or each row has an owner decision.
- Each family uses only the allowance of its layer.
- Each proposal of section 11.11 has an owner decision.

---

## 11. The families: the full design and the migration recipes

This section is the design of the base as one language (CLAUDE.md §XXII, "The design bar of the base"). For each family it gives these parts:
- The full API
- The rows of the rule table and the R5 imports that the family removes
- Its load in the tree
- The places where the sources do not agree
- The recipes of a unit that migrates code to the family.

**The sources and their order.** When two sources do not agree, the first source of this list wins:
1. Main at d36be7daf: `include/foundation/core/`, `include/fixy/Core.h`, the doors of `include/foundation/os/` and `include/fixy/os/`, and CLAUDE.md §XXII.
2. `misc/12_10_2026_linear.md`, which the owner accepted on 2026-10-12.
3. The unit designs of G1 Choice, G2 Record and G2 Scalar of 2026-10-17. Their recommendations are a part of this section.
4. QDESIGN, the family report of 2026-10-01 (appendix D).

The work of G1 changes `Choice.h` after d36be7daf, so the state of the Choice family on main can differ from the column "State". Appendix E gives each base header a verdict, a family and a slice of Stage 4, and the merges of this section are its merge rows.

**The column "State"** of each API table has one of three values:
- `main`: the operation is on main at d36be7daf
- `design`: a source of the list designs it
- `new`: this section designs it first
- `P6` or a different proposal: the operation waits for the owner (section 11.11).

**The load** of each family comes from two sources. The first is the quarantine findings of the main build on 2026-11-12 (`guard list --kind std-object` and `--kind std-entity`) and the counts of `utils/ledgers/quarantine.txt` at d36be7daf. The main build can be older than d36be7daf: `guard list` gave 6,954 objects and 14,651 names, and the ledger counts 7,531 and 16,521. So no count of a family is the reason of a decision. The second source is the call sites that the pilot migration of the MetaLog path found missing in the base.

**What a family removes.** A family removes no row of the rule table when it lands (section 10.2, item 10). Stage 4 removes an `allow` row and Stage 5 an `admit` row. For R5, `libfoundation.a` has no ratchet row at d36be7daf. `libfixy.a` has 16 ratchet rows and one throw row, and Stage 4 removes them. Each other ratchet row is a row of a `libcrucible*.a` library, which quarantined code makes. The family makes its removal possible, and Stage 5 removes it. Some imports come from more than one facility of libstdc++, so no one family removes them. The four `libcrucible*.a` libraries of the ledger are `libcrucible.a`, `libcrucible_cipher.a`, `libcrucible_perf.a` and `libcrucible_vigil.a`:

| Import | Libraries at d36be7daf | Goes when |
|---|---|---|
| `__cxa_allocate_exception`, `__cxa_throw`, `__cxa_call_terminate` | `libfixy.a`, `libcrucible_cipher.a`, `libcrucible_perf.a`, `libcrucible_vigil.a` | the last throw path of libstdc++ in the library goes: the containers, the streams and the threads (Region, Ref and Os) |
| `std::terminate` | the four `libcrucible*.a` libraries | the same |
| `__libc_single_threaded` | `libcrucible_vigil.a` | the same |
| `std::__glibcxx_assert_fail` | `libfixy.a` and the four `libcrucible*.a` libraries | the last checked accessor of libstdc++ goes (Choice and Region) |
| `memchr` | `libfixy.a`, `libcrucible.a`, `libcrucible_cipher.a`, `libcrucible_vigil.a` | the last text search of libstdc++ goes (Region and Report) |
| `std::__throw_bad_cast` | `libcrucible_cipher.a` | the last stream with a locale goes (Report and Os) |

**A decision that this section keeps.** The representation kinds (Total, Checkable, Opaque, and a padding map) are not a part of this design, because the owner decided not to start them.

### 11.1 The shared verbs: one protocol set

The ten verbs and the prefix `mint_` are the shared operations of the language. Each verb is one protocol: one receiver form, one result form, one law and one cost, the same in each family. A type that gives a verb obeys its row. No type gives a different meaning under a verb name.

| Verb | Receiver and result | Law | Cost | Types |
|---|---|---|---|---|
| `for (x : c)` | `begin()` and `end()` on `&` and `const&` | Visits each element one time, in a fixed order. The body cannot add or remove an element | the loop bound only | `Option`, `View`, `TextView`, `RingView`, `FixedArray`, `FixedList`, `HeapArray`, the tables in slot order, `Slot`. A growable owner only through `with_view` |
| `match(arms...)` | `&&` and `const&`. One arm for each alternative, in the declared order, and one result type | Total elimination of a closed sum. A missing arm does not compile. The `&&` form consumes the sum and gives no reference | one switch on the tag | `Option`, `Result`, `OneOf`, `Slot`, and a `Ref` of a kind family (section 11.5) |
| `expect(why)` | `&&`, with a `Fmt<>` that holds the reason and the `Site`. Gives the value | The value, or the fatal exit with the reason. A search for `expect(` finds each one | one branch to a cold call | `Option`, `Result`, `OneOf` (`expect<T>`) |
| `get(key)` | `&` and `const&`. Gives `Option<Ref<T>>` or `Option<Ref<T const>>` | Checked lookup by a key that can miss. It never ends the process | one explicit branch | the regions with an index, the tables with a key, `OneOf` with a type (`get<T>()`) |
| `at(proof)` | `&` and `const&`. Gives `T&` or `T const&` | Total lookup with a proof value: `Index<N>`, `BrandedIndex<Brand>` or `StaticIndex<I>` | no branch | `FixedArray`, a `View` of a fixed extent, a `View` in a branded scope, `Record`, and `Pack` as `pack_at_t` |
| `view()` | `&` and `const&`. The `&&` form is deleted | A borrow of the whole owner as a `View` or a `Ref`. The borrow stays in a frame below the frame of its owner (section 11.10) | none | `FixedArray`, `FixedList`, `HeapArray`, `FixedText`, `Box` and `CacheLine` (P6 for the two). A growable owner only through `with_view` |
| `size()` | `const`. Gives `std::size_t`, a static constant for a fixed extent | The count of elements | none | the regions, the tables, `Record`, and `Pack` as `pack_size_v` |
| `push(v)` | `&`. Gives `Result<Ref<T>, Full<T>>` | Adds into a fixed capacity. The element does not move while its owner lives and holds it. A full owner refuses, and `Full<T>` gives a linear value back | one compare | `FixedList`, and the tables as `push(key, value)` |
| `append(alloc, v)` | `&`, with the allocation capability first. The result waits for the owner (P6 of section 11.11) | Adds, and grows if full. A growth can move each element, so the verb gives no reference | a visible O(n) event at each growth | `GrowList`, `GrowText`, `HeapRing` |
| `consume()` | `&&`. Gives `T` | Takes the value out of an owner that always holds one, and ends the owner | a move | `Box`, `Linear<T>` |
| `mint_*` | A free function, `[[nodiscard]]` and `noexcept`, with one concept as its constraint (§XXI) | The only door to a proof, a token, an owner of an allocation or a refined value | the cost of its check | `mint_box`, `mint_heap_array`, `mint_grow_list`, the mints of the Os doors |

`Full<T>` is the `Full` of the approved verb, with the value of a linear `T` in it, so the caller gets the value back (`misc/12_10_2026_linear.md`, section 11, item 10). For each other `T` it is empty, and the value ends with the refusal.

**The language protocols.** These operators and forms are not verbs. The families give them with one meaning:
- Dereference: `*x` and `x->member` are only on a type with no empty state: `Ref`, `Box`, and `CacheLine` after P6. A type that can be empty has no dereference.
- Compare: `==` and `<=>`, defaulted, on two values of one type only, where each part has the operator. Never with a different type and never with `none`. `Unit`, the sums of Choice, `Record`, `Id` and `Digest` give them.
- Structured binding: `Record` only, through the hook `get<I>` that the language names. The plugin kind `binding-hook` refuses a direct call of the hook (section 11.3).
- `template for (auto& part : record)`: each part of a `Record`.

**The laws of the set.**
1. One meaning, one name. No two names have almost the same meaning: an `Option` gives `is_some`, and no `has_value`.
2. The set is closed. Each public member of a family type is a verb, a language protocol or a name of the family list of its section. The check file of each family asserts this by reflection (section 10.2, item 9).
3. A verb that consumes takes `&&`. A verb that borrows from an owner takes `&` or `const&`, and its `&&` form is deleted. A borrowed type such as `View` or `Ref` owns nothing, so it gives its verbs on each value category.
4. Absence is a sum. A verb that can miss gives an `Option` or a `Result`. A verb that cannot miss takes a proof.
5. The row of a verb states its cost, and a probe of the rule `codegen-shapes` holds each hot-path claim.
6. A new verb waits for the owner (section 17). Section 11.11 lists the verbs that wait.

The concepts of the verbs (`Iterable`, `Sum`, `Fallible`, `Lookup`, `ProvenLookup`, `Borrowable`, `Sized`, `BoundedSink`, `GrowingSink`, `Owner`) live in `include/foundation/core/Verbs.h` (new). Generic code takes each type that gives a verb through them, and the protocol check of item 2 reads them.

**Where the sources do not agree about a verb.**
- `consume`: main wins. `Option` and `Result` have no `consume()`, and `expect` and `match` take its place. `consume` stays for an owner that always holds a value.
- `get`: the verb is the checked lookup with a key. `Box::get()` and `CacheLine::get()` of main borrow with no key, and `Ref::get()` of QDESIGN does the same. Main wins until the owner decides P6 of section 11.11, which gives the dereference and `view()` in their place. `Ref` takes the dereference and no `get()`.
- `match`: the `visit_kind` of QDESIGN is the verb `match` on a `Ref` of a kind family.
- `push`: QDESIGN gives `Result<Ref<T>, Full>`, and a refusal then drops a linear value. `Full<T>` gives it back, as the linear design decides. The refusal of a key that a table holds waits for the owner (P6).
- `append`: QDESIGN gives `Ref<T>`, which dangles at the next growth, so `append` gives no reference. Its result form and the maximum of a growable owner wait for the owner (P6).
- The hook `get<I>` of a structured binding is a name that the language fixes, and not the verb.

### 11.2 Choice: `Option`, `Result`, `OneOf`, `CRUCIBLE_TRY`

**The idea.** A value that can be absent, and the result of an operation that can fail, is a closed sum. The roads to a payload are `match`, a loop of zero or one turns (`Option` only), `value_or`, `expect` and `CRUCIBLE_TRY`. No type of the family has `operator*`, `operator->` or `operator bool`. Each type is `[[nodiscard]]`, and each operation is `constexpr` and `noexcept`.

**The headers.** `include/foundation/core/Choice.h` (main), and `include/foundation/core/OneOf.h` (design), because the includers of `Choice.h` do not read `OneOf`. They include `<cstddef>`, `<concepts>`, `<new>`, `<type_traits>`, `foundation/ByteSeal.h`, `foundation/Linear.h` and `foundation/core/Report.h`.

**The gates.**

| Gate | What it admits | State |
|---|---|---|
| `ChoicePayload<T>` | An object type that is not an array, not cv-qualified and not `NoValue`, with a move and a destructor that do not throw. The design adds: not an `Err<F>`, and `WellFormedNiche<T>` | main, design |
| `ErrorValue<E>` | A scoped enum, a trivially copyable class of at most 16 bytes with no reference member, or a linear class. The design adds: not an `Err<F>` | main, design |
| `PlainPayload<T>`, `DroppablePayload<T>` | The plain form, and a payload that a member can drop or copy | main |
| `HasNiche<T>`, `WellFormedNiche<T>` | A payload that declares its empty value. A declared niche that is not complete fails | main, design |
| `OneOfAlternatives<Ts...>` | 2 to 255 alternatives, each a `ChoicePayload`, with no type two times. The tag is one byte | design |
| `ErrorConvertsTo<F, E>` | `F` is not `E`, and `F` converts to `E` with no throw | design |
| `TryOperand<H>` | An rvalue `Result` or `Option` for `CRUCIBLE_TRY` | design |

**The niche protocol.** A payload declares its empty value through `niche<T>`, with `empty()` and `is_empty(value)` (main). A class can also declare the two as members: `static constexpr T niche_empty() noexcept` and `constexpr bool is_niche_empty() const noexcept` (design). The member form serves a class in a namespace that cannot specialize `niche`, such as a strong id (section 11.7).

**The API of `Option<T>`.**

| Operation | Contract | Cost | Verb | State |
|---|---|---|---|---|
| `Option()`, `Option(none)` | Empty | none | - | main |
| `Option<T>::some(v)` | Full. The empty value of a niche ends the process | one test for a niche payload | - | main |
| `some(v)` | A free function that gives `Option<remove_cvref_t<V>>`, with no conversion | as `some` | - | design |
| `is_some()`, `is_none()` | The state | a load | - | main |
| `match(on_some, on_none)` | `&&` consumes, `const&` borrows | one branch | `match` | main |
| `for (x : option)` | Zero or one turn. A linear payload has no mutable loop | one test | `for` | main |
| `value_or(fallback)` | `&&`, a droppable payload only | one select | - | main |
| `expect(why)` | `&&`. An empty `Option` ends the process | one branch to a cold call | `expect` | main |
| `ok_or(error)` | `&&`. Gives `Result<T, E>` for a droppable `E` | one branch | - | design |
| `option = none`, the move assignment | The owning form, a droppable payload only. A self assignment has no effect | destroy, then move | - | design |
| `==` | Two `Option` values of one type only | one test and one compare | compare | design |
| `Option(Err<F>)` | Deleted: "a Result carries an error that an Option drops: write std::move(result).ok()" | - | - | design |

**The API of `Result<T, E>`.**

| Operation | Contract | Cost | Verb | State |
|---|---|---|---|---|
| `Result(value)`, `Result(err(e))` | `return value;` and `return err(code);` convert | a move | - | main |
| `Result(Err<F>)` | `ErrorConvertsTo<F, E>` | a move | - | design |
| `Result(Err<F>)` for each other `F`, `Result(none)` | Deleted, with the fix in the text: a conversion of `E`, `match`, or `ok_or` | - | - | design |
| `is_ok()`, `is_err()` | The state | a load | - | main |
| `ok()` | `&&`. Gives `Option<T>` and drops the error | one branch | - | main |
| `err()` | `const&` copies a droppable `E`. `&&` moves a linear `E` | one branch | - | main |
| `value_or(fallback)` | `&&`, a droppable `T` and `E` only | one select | - | main |
| `expect(why)` | `&&`. Gives `void` for `Unit` | one branch to a cold call | `expect` | main |
| `match(on_ok, on_err)` | `&&` and `const&` | one branch | `match` | main |
| The move assignment, `==` | As for `Option` | as for `Option` | compare | design |

A `Result` has no loop, because a loop over the value drops the error with no trace.

**The API of `OneOf<Ts...>`.**

| Operation | Contract | Cost | Verb | State |
|---|---|---|---|---|
| `OneOf(v)` | Implicit from one of `Ts`, with no conversion, no default constructor and no empty state | a move | - | design |
| `is<T>()` | `T` is one of `Ts` | one compare | - | design |
| `expect<T>(why)` | `&&`. Another alternative ends the process | one branch to a cold call | `expect` | design |
| `match(arms...)` | One arm for each alternative in the order of `Ts`. The last alternative takes the last branch, so `match` has no fatal path | one switch | `match` | design |
| `get<T>()` | `Option<Ref<T>>`, after `Ref` of G3 | one compare | `get` | design |
| `which()` | `Index<sizeof...(Ts)>`, after `Index<N>` of G4 | a load | - | design |
| The move assignment, `==` | As for `Option` | as for `Option` | compare | design |

**`CRUCIBLE_TRY(name, expr);`** (design). The macro evaluates `expr` one time. On an error or on no value, it returns the error or `none` from the function, and `Result(Err<F>)` converts the error. Else it binds the value to `name`. The gate `TryOperand` refuses an lvalue and each type that is not a sum. A `Result` in a function that gives an `Option`, and the reverse, do not compile. The helpers are in `foundation::core::detail`, so the rule `owners` refuses a direct call of them. The macro is a sequence of statements, so the body of an `if` with no braces cannot hold it. The cost is open. The checked take repeats the test, and the probe `result_try` measured 11 instructions against 9 of the raw form. `result_try_convert` measured 12 against 9, with one more branch. The unit makes the build of the `Result` smaller, or it declares the cost with a true claim.

**The layout.** The tag is one byte (`detail::TaggedCells`). It goes before a storage of 4 bytes, so a register return holds it in the low byte, and else after the storage. `Option<std::uint32_t>` has 8 bytes. `Result<std::uint32_t, E>` with a 4-byte `E` has 8 bytes, with the tag at offset 0. A sum of `PlainPayload` parts of at most 16 bytes is trivially copyable and returns in registers. Three types carry the annotation `no_start_over_bytes`: a `Result`, a `OneOf`, and a niche `Option` whose empty value is not all zero bits. So no lifetime start over bytes makes them.

**The family list.** `none`, `NoValue`, `Unit`, `Err`, `err`, `some`, `is_some`, `is_none`, `value_or`, `ok_or`, `is_ok`, `is_err`, `ok`, `is`, `which`, `niche`, `CRUCIBLE_TRY`.

**Removes.** The rows `allow F1_lattices <expected> until Choice`, `allow F1_permissions <optional> until Choice` and `allow X1_wrappers <variant> until Choice`. The family has no R5 import of its own (the table of shared imports holds `std::__glibcxx_assert_fail`).

**The load.** Objects: `std::expected` 1,588 and `std::optional` 494. Names: `std::expected::has_value` 1,145, `std::expected::operator*` 588, `std::expected::error` 550, `std::unexpected` 530, `std::expected::operator->` 509, `std::optional::has_value` 223, `std::optional::operator*` 199 and `std::nullopt` 105. The base: 19 headers include `<expected>`, 22 `<optional>` and one `<variant>`. The pilot found three call sites. A ring and a channel pop into an `Option` (slice 4.2). The mint of a validated MetaLog entry gives a `std::optional`. The trace build of `src/BackgroundThread.cpp` gives a `std::expected`.

**Where the sources do not agree.**
- A move of a niche or a plain `Option` keeps the source (main). QDESIGN empties it, but the payload owns nothing in these two forms, and the ABI wants a trivial copy.
- The fatal unwrap is `expect(Fmt<>)` (main). The `Fmt<>` holds the reason and the `Site`, as the `ExpectWhy` of QDESIGN does.
- An `Option` builds only through `some`, and a `Result` builds from `T` (main). An implicit `Option` from `T` hides the choice of the author in `return false;` of an `Option<bool>`.
- A moved-from owning `Result` or `OneOf` keeps its tag (design). The attack ledger pins it.
- `map`, `and_then` and `or_else` wait for the owner (section 11.11).

**The recipes.**

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
| `opt ? value_of(*opt) : fallback` | `std::move(opt).match(on_some, on_none)` | mechanical |
| `if (!opt) return std::unexpected(e);` before a use of `*opt` | `CRUCIBLE_TRY(v, std::move(opt).ok_or(e));` | mechanical |
| `std::optional<T>` that `emplace` or `reset` changes | `opt = fixy::some(v);` and `opt = fixy::none;` | mechanical |
| `std::variant<A, B>` | `fixy::OneOf<A, B>` | mechanical |
| `std::visit(visitor, v)`, `std::holds_alternative<T>(v)` | `std::move(v).match(arms...)`, `v.is<T>()` | mechanical |

### 11.3 Record: `Record<Ts...>`, `Pack<Ts...>`

**The idea.** A product whose parts have meanings is a named struct. A generic product is `Record<Ts...>`, and a list of types is `Pack<Ts...>`. Neither is `std::tuple`. The family has no part on main.

**The header.** `include/foundation/core/Record.h`, a file of the language. It includes `<cstddef>`, `<compare>`, `<type_traits>`, `<utility>` and `foundation/core/Index.h` of the Scalar family, and no `<tuple>`.

**The gate.** `RecordElement<T>`: an object type that is not an array, not cv-qualified, not a reference and not `void`. A reference part hides a borrow, so a borrow is a `Ref` or a `View`. A const part blocks the move of a linear part. A C array part is a `FixedArray`. A `Pinned` part is valid, and the `Record` is then pinned too.

**The layout.** `Record<Ts...>` derives from one flat base of cells, one cell for each part: `RecordCell<I, T>` with one `[[no_unique_address]]` member. The instantiation depth is two for each count of parts, where a recursive tuple has a depth of N. A `Record` is an aggregate with no converting constructor, and the default member initializer of each cell gives each part its value. It is trivially copyable and structural when each part is. Empty parts collapse: `sizeof(Record<TokenA, TokenB>)` is 1. The parts are in declaration order. When no part gives its tail padding to the next part, each offset equals the offset of a hand-written struct. The check file asserts this by reflection for twelve shapes.

**The API.**

| Operation | Contract | Cost | Verb | State |
|---|---|---|---|---|
| `Record<A, B>{a, b}` | Aggregate initialization. A narrowing conversion does not compile | none | - | design |
| `Record(Ts...) -> Record<Ts...>` | The deduction guide. `return {std::move(value), std::move(next)};` builds a return | none | - | design |
| `at(index<I>())` | `&` and `const&`, for `I` below the count of parts | one member access | `at` | design |
| `at(index<I>())` on `&&` | Deleted: "write auto [first, second] = std::move(record)", because a move of one part drops the others | - | - | design |
| `size()` | `static consteval` | none | `size` | design |
| `types` | `Pack<Ts...>` | none | - | design |
| `auto [a, b] = record;` | The protocol of `std::tuple_size` and `std::tuple_element` (their class forms) and the hidden friend hook `get<I>`. `auto [a, b] = std::move(record)` moves each part one time | none | binding | design |
| `auto [...parts] = record;` | A binding pack, in place of `std::apply` | none | binding | design |
| `template for (auto& part : record)` | Each part, in order | the count of parts | `for` of a product | design |
| `==`, `<=>` | Defaulted and lexicographic, for one `Record` type only. `<=>` only for parts with a strong or a partial order | one compare for each part | compare | design |
| copy, move | Defaulted. A move-only part makes the `Record` move-only | as the parts | - | design |

**The API of `Pack<Ts...>`.** An empty class for deduction only, as `std::index_sequence`. Its operations are variable templates and aliases (R14), each with an instantiation depth of one: `pack_size_v<P>` (verb `size`), `pack_at_t<I, P>` (verb `at`), `pack_contains_v<T, P>`, `pack_index_of_v<T, P>` (one occurrence of `T` only), `pack_concat_t<Ps...>`, `pack_apply_t<Template, P>` and `pack_unique_v<P>`. `std::meta::substitute(^^Pack, args)` and `template_of(type) == ^^Pack` work, because `Pack` is a plain variadic class template.

**The binding hook.** The language finds the hook `get<I>` through argument-dependent lookup, so a quarantined `get<0>(record)` compiles as a second form of `at`. The plugin kind `binding-hook` refuses a call of the annotated hook outside the initializer of a decomposition (design). A binding of a family type gives no finding of the protocol names.

**The family list.** `Record`, `Pack`, `types`, `pack_size_v`, `pack_at_t`, `pack_contains_v`, `pack_index_of_v`, `pack_concat_t`, `pack_apply_t`, `pack_unique_v`.

**Removes.** The row `allow F0_core <tuple> until Record`. The rows `admit std::tuple_size` and `admit std::tuple_element` in `include/foundation/core/` stay, because they hold the protocol of the family. The family has no R5 import.

**The load.** Objects: `std::pair` 330 and `std::tuple` 53. Names: `std::get` 475, `std::pair` 241, `std::tuple` 137, `std::tuple_element_t` 24 and `std::tuple_size_v` 23. The base: 35 headers include `<tuple>`. The products of linear tokens (`mint_permission_split`, `mint_permission_split_n`, `mint_read_loan`), the steps of a session (`recv` gives a value and the next handle), the stages of a pipeline, and the rosters of `fixy/atoms/` (type lists) use it.

**Where the sources do not agree.**
- QDESIGN gives a free `at<I>(record)`. The member `record.at(fixy::index<I>())` is the form of Region, so the families have one form.
- QDESIGN gives `apply(record, fn)`. A binding pack does the same with no name, so `apply` goes.
- R9 cut the API to the calls of the tree. The design bar keeps `pack_unique_v` and the compares.
- `std::weak_ordering` has no admit row. `<=>` takes parts with a strong or a partial order, until a call site makes the admit row necessary.

**The recipes.**

| Pattern A | Pattern B | Kind |
|---|---|---|
| `std::pair<A, B>` in an API | a named struct | judgment (the names) |
| `std::pair<A, B>` in generic code | `fixy::Record<A, B>` | mechanical |
| `std::tuple` for values | `fixy::Record<Ts...>` | mechanical |
| `std::tuple` as a type list | `fixy::Pack<Ts...>` | mechanical |
| `std::tie(a, b) = f();` | `auto [a, b] = f();` | mechanical |
| `std::get<I>(t)` | `t.at(fixy::index<I>())`, or a structured binding | mechanical |
| `std::apply(f, t)` | `auto [...parts] = t; f(parts...);` | mechanical |
| `std::tuple_element_t<I, List>` of a type list | `fixy::pack_at_t<I, Pack>` | mechanical |

### 11.4 Region: `View`, `FixedArray`, `FixedList`, `HeapArray`, `GrowList`, `HeapRing`, `TextView`, `CText`, `FixedText`, `GrowText`, `FixedTable`, `HeapTable`, and the free operations

**The idea.** Each array, buffer, text, run of bytes, pointer with a count, and C array is one contiguous region. A region has an owner kind and an extent kind, and each kind gives one set of verbs and one small set of free operations. An index is checked by its type, or by the one explicit branch of `get`.

**The headers.** `include/foundation/core/Region.h` and `include/foundation/core/Text.h` (main), and one header for each owner kind (design): `Array.h`, `List.h`, `Ring.h`, `Table.h` and `Algorithms.h`. `Text.h` uses no other family, so the Report family can take a `TextView`.

**The merges.** Slice 4.3 merges seven base headers into the family (Appendix E):
- `fixy/FixedArray.h` into `FixedArray`
- `foundation/AlignedBuffer.h` into `HeapArray`, whose `Align` states the alignment
- `foundation/SwissTableBuffer.h` into the storage of the tables
- `fixy/Borrowed.h`, `fixy/ScopedView.h`, `fixy/OwnedRegion.h` and `fixy/SharedRegion.h` into `View` and the Ref family. Their brands go to `Ref<T, Brand>`, `BrandedIndex<Brand>` and the region capability of section 11.10.

**The kinds.**

| | Borrowed | Inline, exactly N | Inline, at most N | Heap, count fixed at the mint | Heap, growable |
|---|---|---|---|---|---|
| Elements | `View<T, Extent>` | `FixedArray<T, N>` | `FixedList<T, N>` | `HeapArray<T, Align>` | `GrowList<T>` |
| Text | `TextView`, `CText` | - | `FixedText<N>` | - | `GrowText` |
| Ring | - | - | `fixy::CyclicBuffer<T, N>` (X1) | - | `HeapRing<T>` |
| Keyed | - | - | `FixedTable<K, V, N>` | `HeapTable<K, V>` | - |

The fixed ring stays `fixy::CyclicBuffer` of `fixy/CyclicBuffer.h`, which keeps the last N elements, and slice 4.3 rebuilds its reads on `Index<N>` (Appendix E). Arena memory comes from `BumpArena::array` of the Ref family as a `View`. A table does not grow: a larger table is a new table, and the caller moves each entry with `take` and `push`.

**The proofs of an index.** `Index<N>` proves `i < N` (design). `Index<N>::check(i)` gives an `Option`, and `index<I>()` of the Scalar family converts when `I < N`. A `View` has no brand (main, and phase D of `misc/12_10_2026_linear.md`). `mint_branded_scope(view, body)` gives the body the view with a fresh brand (design). In the body, `index(i)` gives `Option<BrandedIndex<Brand>>` and `indices()` gives each index, an index of 4 bytes that proves `i` below the count of that view. Only that scope makes such an index, and the count of a view never changes.

**The API of `View<T, Extent>`.**

| Operation | Contract | Cost | Verb | State |
|---|---|---|---|---|
| `size()` | The count. A static constant for a fixed extent | none | `size` | main |
| `for (x : view)` | Each element, with the count as the only bound | the loop bound | `for` | main |
| `window(offset, count)` | `Option<View<T>>`. No value when the window does not fit | one branch | - | main |
| `window<N>(offset)` | `Option<View<T, N>>`. Then N reads with no check | one branch | - | main |
| conversions | `View<T>` to `View<T const>`, and a fixed extent to `View<T>`. No conversion removes a const or adds a fixed extent | none | - | main |
| `Option<View<T>>` | The size of a `View`, through the niche of the view | none | - | main |
| `get(i)` | `Option<Ref<T>>`, after `Ref` of G3 | one branch | `get` | design |
| `at(i)` | `Index<Extent>` for a fixed extent, `BrandedIndex<Brand>` in a branded scope | no branch | `at` | design |
| `indices()` | The proven indexes of a loop: `Index<N>` or `BrandedIndex<Brand>` | the loop bound | `for` | design |
| `split_at(k)` | `Option<Record<View<T>, View<T>>>` | one branch | - | design |
| `mint_branded_scope(view, body)` | The body gets the view with a fresh brand, and the mints of its `BrandedIndex` | none | `mint_*` | design |

The constructor from raw parts is private, and only the doors of the base reach it (`detail::view_over_`, main). The seal of `foundation/ByteSeal.h` refuses `std::bit_cast` into a `View` (main).

**The API of the owners.**

| Operation | Owners | Contract | Cost | Verb | State |
|---|---|---|---|---|---|
| `at(Index<N>)`, `at(index<I>())` | `FixedArray` | Total | no branch | `at` | design |
| `get(i)` | each owner, a growable owner only in `with_view` | `Option<Ref<T>>` | one branch | `get` | design |
| `view()` | `FixedArray`, `FixedList`, `HeapArray`, `FixedText` | `&` and `const&`. The `&&` form is deleted | none | `view` | main for `FixedText`, design for the others |
| `for (x : owner)`, `size()` | each owner, a growable owner only in `with_view` | As for a `View` | the loop bound | `for`, `size` | design |
| `push(v)` | `FixedList` | `Result<Ref<T>, Full<T>>` | one compare | `push` | design |
| `take_last()`, `take_first()` | `FixedList` and `GrowList` (last), `HeapRing` (first and last) | `Option<T>`: the element moves out | one compare | - | new |
| `clear()` | `FixedList`, a droppable element only | Ends each element | the count | - | new |
| `mint_heap_array(alloc, count, fill)` | `HeapArray` | Each element is `fill`. `Align` states the alignment of the storage, as `AlignedBuffer` does | one allocation | `mint_*` | design |
| `mint_heap_array_written(alloc, count, body)` | `HeapArray` of an implicit-lifetime `T` | The body gets a view that only writes, and the mint gives the array after the body | one allocation | `mint_*` | design |
| `mint_grow_list(maximum)`, `mint_heap_ring(minimum, maximum)` | `GrowList`, `GrowText`, `HeapRing` | Empty, with no allocation. The maximum bounds each growth (the principle P5 of the linear design). The maximum of a `GrowList` waits for P6 | none | `mint_*` | new |
| `append(alloc, v)` | `GrowList`, `GrowText`, `HeapRing` | Gives no reference, and its result waits for the owner (P6). A `HeapRing` doubles its capacity from its minimum up to its maximum | a visible O(n) event at each growth | `append` | design |
| `with_view(std::move(owner), body)` | `GrowList`, `GrowText`, `HeapRing` | The body gets a `View`, or a `RingView` that counts from the oldest element, and cannot reach the owner. So no growth happens while the view lives. Gives the owner back | none | - | design |
| `freeze(std::move(list))` | `GrowList` | Gives a `HeapArray<T>` of the elements | none | - | design |
| `push(key, value)` | `FixedTable`, `HeapTable` | `Result<Ref<V>, Full<V>>` for a full table. The refusal of a key that the table holds waits for the owner (P6) | one probe | `push` | design |
| `get(key)`, `take(key)` | `FixedTable`, `HeapTable` | `Option<Ref<V>>`, and `Option<V>` that moves the value out | one probe | `get` | design |
| `for (entry : table)` | `FixedTable`, `HeapTable` | Each entry as `Record<K, Ref<V>>`, in slot order. The order is a function of the keys and a fixed seed | the capacity | `for` | design |
| `mint_heap_table(alloc, capacity)` | `HeapTable` | Empty, with a fixed capacity | one allocation | `mint_*` | design |

Each owner fills each element on its mint, or takes a fill value: no owner reads an indeterminate value. `FixedList`, `GrowList` and `HeapRing` hold trivially destructible elements or end each element in their destructor, and no API reads a slot at or past `size()`. A table key is an integer, an `Id` or a `Digest` of at most 64 bits (section 11.7). The table probe is the SIMD probe of `include/crucible/SwissTable.h`, which moves into the base, over the storage of `foundation/SwissTableBuffer.h`.

**The text types.** `TextView` is a borrowed run of characters (main). Its constructor from a literal is consteval, so the literal has static storage. `CText` is a run with a zero after it, for a call of the kernel (main). It comes from a literal or from `foundation::os::PathText`. `FixedText<N>` is a buffer of N characters with its count (main), and `format` of the Report family writes into it. `GrowText` is the growable text (design). A `TextView` gives `size()`, `for` and `window(offset, count)` as a `View` does, in place of `substr`. The other text operations are free functions: `equal`, `find` of a character or a text, `starts_with`, `ends_with` and `split_once`, which gives `Option<Record<TextView, TextView>>` (design).

**The free operations.**

| Operation | Contract | Cost | State |
|---|---|---|---|
| `copy(dst, src)` | Two views of one fixed extent cannot fail. Two views of a dynamic extent give `Result<Unit, LengthMismatch>`. An overlap is safe, as `__builtin_memmove` | the raw copy | main |
| `fill(dst, value)` | Each element. An element of one byte takes one `memset` | the raw fill | main |
| `equal(a, b)` | Element by element, with no read of padding | the raw compare | design |
| `find(view, x)`, `find_if(view, predicate)` | `Option<std::size_t>` | a scan | design |
| `all_of`, `any_of`, `count_if` | A predicate over each element | a scan | design |
| `sort_by_key(view, key)` | One fixed stable algorithm (a radix sort on the key). The key has a total order, such as an integer or `fp::total_key` | O(n) for each pass of the radix | design |
| `lower_bound_by_key(view, key, probe)` | The first position whose key is not below the probe | O(log n) | design |
| `bytes(object)` | `View<std::byte const, sizeof(T)>` for a `T` with unique object representations, so no padding reaches a hash or a wire | none | design |
| `load<T>(window)`, `store(window, value)` | A `BitValid` value from or into a window of `sizeof(T)` bytes (section 11.7) | one move | design |
| `static_array(range)` | consteval. The run-time `View` of a `define_static_array` | none | design |

**The family list.** The types: `View`, `Index`, `BrandedIndex`, `TextView`, `CText`, `FixedText`, `GrowText`, `FixedArray`, `FixedList`, `HeapArray`, `GrowList`, `HeapRing`, `RingView`, `FixedTable` and `HeapTable`. The errors: `Full` and `LengthMismatch`. The names of operations: `dynamic_extent`, `window`, `split_at`, `indices`, `index`, `check`, `take`, `take_first`, `take_last`, `clear`, `with_view`, `freeze`, the mints and the free operations.

**Removes.** Eight rows: `allow F0_core <array> until Region`, `allow F0_core <span> until Region`, `allow F0_core <string> until Region`, `allow F0_core <string_view> until Region`, `allow F0_core <vector> until Region`, `allow F0_primitives <cstring> until Region`, `allow X1_wrappers <ranges> until Region` and `allow X3_os <algorithm> until Region`. The R5 imports of `libfixy.a`: `std::__throw_length_error` and `std::__throw_logic_error`, with the throw row of `Topology.cpp.o` (Region and the path type of Os). The R5 imports of the `libcrucible*.a` libraries: `std::__throw_length_error`, `std::__throw_logic_error`, `std::__throw_out_of_range_fmt` with their `helper-throw` rows, `std::string::_M_replace_cold`, `strlen`, `strncmp`, `__memcpy_chk` and `__memset_chk`.

**The load.** Objects: `std::span` 1,708, `std::array` 809, `std::basic_string_view` 670, `std::vector` 358, `std::basic_string` 223, `std::inplace_vector` 21, `std::deque` 8, `std::unordered_set` 8, `std::set` 3 and `std::bitset` 2. Names: `std::array::operator[]` 892, `std::vector::operator[]` 286, `std::span::operator[]` 153 and `std::vector::push_back` 98. The ledger holds 1,109 C array objects. C calls: `memcpy` 128, `memset` 44, `strcmp` 28, `memcmp` 21 and `strlen` 14. The pilot found these call sites:
- The counter and the key of `include/crucible/Philox.h` are `std::array`, and they become `FixedArray`.
- A view over an `AlignedBuffer`.
- The history ring of `include/crucible/IterationDetector.h` doubles its capacity up to a maximum. It appends, takes the oldest element and reads by an index from the oldest, each in O(1). It is a `HeapRing`.
- The table of window fingerprints of the same file inserts, finds and removes by a 64-bit key in a fixed capacity. It is a `HeapTable`.
- The callers of `std::vector` take a `GrowList`, and the C arrays become `FixedArray`.

**Where the sources do not agree.**
- QDESIGN gives a type `Text`, and the recipe of a static name used it. On main, `TextView` and `CText` take the literal, and `TextView` is a class of its own and not `View<char const>`. Main wins.
- QDESIGN gives a brand parameter to `View`. Main and phase D of the linear design give a view no brand, and the branded proof is the index. Main wins.
- QDESIGN gives `Ref<T>` from `append` of a `GrowList`, which dangles at the next growth. `append` gives no reference, and a growable owner gives its elements only in `with_view`.
- QDESIGN gives a table that grows through `rehash_into`. The table of the iteration detector has a fixed capacity, and a growth is a new table.
- `FixedArray` is in `include/fixy/FixedArray.h`, in the layer X1. The family gives it in `foundation/core`, in the layer F0 primitives, and slice 4.3 merges the old header.
- Appendix E merges `fixy/OwnedRegion.h` into `View` and the Ref family. Section 4 of the linear design keeps that header in X1 as the CSL view of a region: the region capability and a `Permission`. The owner decides which form slice 4.3 takes.
- QDESIGN keeps `fixy/ScopedView.h` as it is, and Appendix E merges it. A `ScopedView` also proves a state of its carrier, and a `Ref<T, Brand>` holds no state. The owner decides which type holds that proof after slice 4.3.

**The recipes.**

| Pattern A | Pattern B | Kind |
|---|---|---|
| `std::span<T>` parameter | `fixy::View<T>` | mechanical |
| `std::span<T, N>` | `fixy::View<T, N>` | mechanical |
| `static constexpr std::string_view n = "...";` | `static constexpr fixy::TextView n{"..."};` | mechanical |
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
| `std::vector<T>`, unbounded, cold | `GrowList<T>`. `push_back(x)` becomes `list.append(alloc, x)`, with the result form of P6 | judgment (the caller holds an `Alloc`) |
| `std::string` | `FixedText<N>` or `GrowText` | judgment |
| `memcpy(dst, src, n)` | `fixy::copy(dst_view, src_view)` | judgment (n becomes view lengths) |
| `memcpy(&x, p, sizeof x)` | `x = fixy::load<T>(window)` | mechanical when the window exists |
| `memset(p, 0, n)` | `fixy::fill(view, T{})` | mechanical |
| `std::ranges::sort(v, cmp)` | `fixy::sort_by_key(view, key)` | judgment (the key) |
| `std::unordered_map<K, V>` | `HeapTable<K, V>` | judgment (capacity) |
| `std::unordered_set<K>` | `HeapTable<K, fixy::Unit>` | judgment (capacity) |
| `std::inplace_vector<T, N>` | `FixedList<T, N>` | mechanical |
| `std::deque<T>` used as a queue | `HeapRing<T>`, or `fixy::CyclicBuffer<T, N>` that keeps the last N | judgment (the maximum) |
| `v[i]` on a `std::vector` member that grows | `with_view(std::move(list), body)`, and `get(i)` or `at` in the body | judgment |
| `view.subspan(o, c)`, `text.substr(o, c)` | `view.window(o, c)`, and handle the empty case | judgment |
| `text.starts_with(p)`, `text.find(c)` | `fixy::starts_with(text, p)`, `fixy::find(text, c)` | mechanical |
| `std::vector<T>` that a loop fills and then only reads | `GrowList<T>`, then `freeze(std::move(list))` | judgment |

### 11.5 Ref: `Ref`, `Box`, `BumpArena`, `Slot`, `downcast`, `FnRef`, `CallRef`, `Address`, `Hole`

**The idea.** One object is reached through a reference that is never null (`Ref`). It has one owner on the heap (`Box`), lives in an arena (`BumpArena::make`), or lives in storage with an explicit lifetime (`Slot`). A function is reached through `FnRef` or `CallRef`. With Region, this gives one grid of one object or many objects, borrowed, owned, in an arena or inline.

**The header.** `include/foundation/core/Ref.h` (main), with the parts of the linear design in `foundation/core/Credit.h` and `foundation/core/Identity.h` (section 11.10). `Ref.h` and `Region.h` include `Credit.h`, and `Region.h` never includes `Ref.h`.

**The merges.** Slice 4.3 merges `fixy/Borrowed.h`, `fixy/ScopedView.h`, `fixy/OwnedRegion.h` and `fixy/SharedRegion.h` into `View` and the Ref family (Appendix E). A borrow of one object becomes a `Ref<T, Brand>`, and a borrow of a run becomes a `View` (section 11.4).

**The API.**

| Operation | Contract | Cost | Verb | State |
|---|---|---|---|---|
| `Ref<T, Brand>::to(object)` | A reference that is never null. `to` of an rvalue is deleted, because the `Ref` dangles at the end of the full expression | none | - | design |
| `*ref`, `ref->member` | Total: a `Ref` has no empty state | none | dereference | design |
| `same_object(a, b)` | The same object | one compare | - | design |
| conversions | `Ref<T, Brand>` to `Ref<T const, Brand>`, and to the erased brand | none | - | design |
| `Option<Ref<T>>` | One pointer: the null value is the niche | none | - | design |
| `ref.match(arms...)` | A `Ref` of a kind family: one arm for each enumerator of the kind. `KindFamily<Base>` reads the kind enum and a `kind_value` on each derived type by reflection. The build stops when one enumerator has no type | one switch | `match` | design |
| `downcast<Derived>(ref)` | `Option<Ref<Derived>>`, for `KindTagged<Base, Derived>` | one compare | - | design |
| `mint_box<T>(alloc, args...)` | The one door to a `Box`: it allocates, builds the object and gives the `Box` | one allocation | `mint_*` | main |
| `*box`, `box->member` | A borrow of the object. The `&&` forms are deleted | one test of a moved-from `Box` | dereference | main |
| `box.get()` | `T&`, a borrow with no key | one test of a moved-from `Box` | - | main |
| `box.view()` | `Ref<T>`. P6 gives it in place of `get()` | one test of a moved-from `Box` | `view` | P6 |
| `std::move(box).consume()` | Moves the object out and frees the storage | a move | `consume` | main |
| `BumpArena::make<T>(alloc, args...)` | A trivially destructible `T`. Gives `Ref<T, Brand>` of the arena brand | the bump | - | design |
| `BumpArena::array<T>(alloc, count)` | `ImplicitLifetimeThroughout<T>`. Gives `View<T>` | the bump | - | design |
| `BumpArena::copy_text(alloc, text)` | A copy of a text in the arena. Gives `TextView` | the bump and a copy | - | design |
| the epoch of an arena | The end of an epoch frees each byte of the arena at one time, and the arena keeps its blocks. `ArenaEpoch` is on main in `include/crucible/Arena.h`, and it becomes the epoch region of the linear design | O(1) | - | main, design |
| `Slot<T>` | Storage with an explicit lifetime for one object, also a `Pinned` object. `emplace(args...)` gives `Option<Ref<T>>`, with no value when the slot holds an object. `clear()` ends the object | the build | `for`, `match` | design |
| `FnRef<Sig>` | The address of a function, never null. `Option<FnRef<Sig>>` has the size of a pointer | an indirect call | - | design |
| `CallRef<Sig>` | A borrowed callable, for a parameter only. The rule `view-escape` refuses it in a member | an indirect call | - | design |
| `address_of(ref)` | An `Address`: `is_aligned<N>()` and `==`. No hash and no order, because an address changes from run to run | none | - | design |
| `Hole<Size, Align>` | The credit that the consume of a linear `Box` gives, and that a build of a `Box` spends (linear design, section 6) | none | - | design |
| the allocation door | Each allocation of the base goes through one door, which calls glibc `malloc` (section 10.3a). Each arena poisons its free space for ASan (R18) | one call | - | design |

**The family list.** `Ref`, `to`, `same_object`, `downcast`, `KindFamily`, `KindTagged`, `Box`, `mint_box`, `BumpArena`, `make`, `array`, `copy_text`, `Slot`, `emplace`, `clear`, `FnRef`, `CallRef`, `Address`, `address_of`, `is_aligned`, `Hole`.

**Removes.** Three rows: `allow F1_lattices <functional> until Ref`, `allow X1_wrappers <memory> until Ref` and `allow X5_session <any> until Ref`. The text of the row of `<functional>` also names the comparators `std::less`, `std::greater` and `std::identity`. The sort keys of Region replace them, so that row goes after Ref and Region. The row `admit std::nothrow until Ref in include/foundation/core/Ref.h` goes when the allocation door lands. The R5 imports of `libfixy.a`: `operator new` (`_Znwm`), `operator delete` (`_ZdlPvm`), and the typeinfo, the vtable and the destructor of `std::bad_alloc` and of `std::bad_array_new_length`. The R5 imports of the `libcrucible*.a` libraries: the same, and the aligned forms of `operator new` and `operator delete`. The calls of `malloc`, `calloc`, `aligned_alloc` and `free` move into the allocation door.

**The load.** Objects: `std::unique_ptr` 118 and `std::shared_ptr` 4. Names: `std::unique_ptr::operator->` 177, `std::unique_ptr::get` 70 and `std::make_unique` 68. The ledger holds 2,513 raw pointer objects, 102 raw function pointers and 80 raw new-expressions and delete-expressions. The pilot found two call sites. The four payloads of the background stages of `include/crucible/BackgroundThread.h` become a `Box`. The `std::unique_ptr` owners of `include/crucible/Vigil.h` (the ring, the MetaLog, the Cipher, the senses and the watchdog) and of `BackgroundThread.h` (the pipeline) become a `Box`.

**Where the sources do not agree.**
- `Box::get()` of main and `Ref::get()` of QDESIGN borrow with no key, and the verb `get` is the checked lookup. `Ref` takes the dereference only. `Box::get()` stays until the owner decides P6 (section 11.1).
- `visit_kind` of QDESIGN is the verb `match` on a `Ref` of a kind family.
- The base has no counted reference (`misc/12_10_2026_linear.md`, section 11, item 2). A `std::shared_ptr` becomes an owner with a scope, an epoch or a lease.
- A `Box` tests for a moved-from object at each access (main). The rule `must-consume` replaces the test when it becomes an error (linear design, section 6).

**The recipes.**

| Pattern A | Pattern B | Kind |
|---|---|---|
| `T* next = nullptr;` member | `Option<Ref<T>> next = none;` | mechanical |
| a member that is never null after build | `Ref<T>` | judgment |
| `new (arena.alloc_obj<T>(a)) T{}` | `arena.make<T>(a)` | mechanical |
| `arena.alloc_array<T>(a, n)` | `arena.array<T>(a, n)` | mechanical |
| `static_cast<D*>(node)` in `switch (node->kind)` | `node.match(arms...)` | mechanical for each switch |
| one `static_cast` after an if | `downcast<D>(ref)` | judgment |
| `std::make_unique<T>(args)` | `fixy::mint_box<T>(alloc, args)` | judgment (an `Alloc` must reach the site) |
| `new T` / `delete p` | `Box` | mechanical |
| `std::function<Sig>` parameter | `CallRef<Sig>` | mechanical |
| `std::function<Sig>` member | a template parameter | judgment |
| `reinterpret_cast<uintptr_t>(p)` | `fixy::address_of(ref)` | mechanical |
| `std::unique_ptr<T>` member, and `p.get()` | `Box<T>`, and `box.view()` | mechanical |
| `std::reference_wrapper<T>`, `std::addressof(x)` | `Ref<T>`, `Ref<T>::to(x)` | mechanical |
| `std::construct_at` and `std::destroy_at` on raw storage | `Slot<T>`, or `BumpArena::make` | judgment |
| `std::shared_ptr<T>` | an owner with a scope, an epoch or a lease | judgment |

### 11.6 Atomic: `Atomic<T>`, `CacheLine`, `Tally`

**The idea.** A value that two threads touch is an `Atomic<T>`. The name of each operation gives its memory order. Only the sole writer can use the relaxed order, and it shows a proof. Only the lock-free algorithms of the base use the sequentially consistent order. Each operation is one `__atomic` builtin on a plain value. So it compiles to the instructions of the same `std::atomic` operation, and no `<atomic>` include is necessary.

**The header.** `include/foundation/core/Atomic.h` (main).

**The gates.** `AtomicValue<T>` admits a trivially copyable object type that is not an array and not cv-qualified (main). It must have one bit pattern for each value, and lock-free atomic operations for its size. A type with padding and a float with two zeros are refused. `AtomicCount<T>` adds an integer that is not `bool`, and `AtomicBits<T>` an unsigned integer (main).

**The API.**

| Operation | Contract | Cost | State |
|---|---|---|---|
| `Atomic<T>(initial)`, `Atomic<T>()` | A pinned cell | none | main |
| `load_acquire()`, `store_release(v)` | The value with acquire order, and a store with release order | one load, one store | main |
| `exchange_acq_rel(v)` | Stores `v`, and gives the value before | one locked exchange | main |
| `cas_acq_rel(expected, desired)` | Strong. `Result<Unit, CasRefusal<T>>`, and the error holds the value that the cell held | one locked compare-and-swap | main |
| `fetch_add_acq_rel(d)`, `fetch_sub_acq_rel(d)` | For an `AtomicCount` | one locked add | main |
| `fetch_or_acq_rel(b)`, `fetch_and_acq_rel(b)` | For an `AtomicBits` | one locked operation | main |
| `fetch_max_acq_rel(v)`, `fetch_min_acq_rel(v)` | For an `AtomicCount` | one locked operation, or a compare-and-swap loop | main |
| `load_owned(proof)` | The relaxed load of the sole writer. The proof is `SoleWriter<Role, Brand>`, the linear permission of the writer of the cell, so another thread cannot call it | one load | design |
| `Atomic<Option<Ref<T>>>` | A pointer that a thread publishes. `Option<Ref<T>>` is one pointer with a niche, so the cell is lock-free | as `Atomic` | design |
| `CacheLine<T>` | Aligns a cell to 64 bytes and pads it to a multiple of 64 bytes. `get()` borrows the cell (main). P6 gives `*line`, `line->member` and `line.view()` in its place | none | main, P6 |
| `Tally` | A statistics counter. `add(n)` is a fetch-add with acq_rel order, and `read()` is an acquire load. A value of a `Tally` orders no other data | one locked add | main |
| the relaxed and seq_cst operations of `ChaseLevDeque`, and `fence_seq_cst` | `detail` code of the base, with a citation of the algorithm | as the algorithm | design |

A wait on a value is the futex door of the Os family (`fixy/os/Futex.h`, main), and not a member of `Atomic`. The run-time id of a brand of the linear design is an `Atomic` counter (section 11.10). The publication types of `fixy/handle/` (`PublishOnce`, `PublishCommit`, `OneShotFlag` and `Once`) stay in the layer X4, and slice 4.4 rebuilds them on `Atomic` (Appendix E). A pointer that a thread publishes is then a `PublishOnce<Ref<T>>` or an `Atomic<Option<Ref<T>>>`.

**The family list.** The types and the gates: `Atomic`, `AtomicValue`, `AtomicCount`, `AtomicBits`, `CasRefusal`, `SoleWriter`, `CacheLine` and `Tally`. The operations: each operation of the table, and `add` and `read`.

**Removes.** The row `allow F0_primitives <atomic> until Atomic`. The family has no R5 import of its own, because each operation is a builtin. The import `std::__detail::__notify_impl` of `libcrucible_vigil.a` goes when its wait moves to the futex door (section 11.9).

**The load.** Objects: `std::atomic` 212. Names: `std::__atomic_base::load` 184, `std::atomic::load` 106, `std::atomic::store` 101, `std::atomic` 102 and `std::__atomic_base::fetch_add` 94. The quarantine ledger counts 16 base includes of `<atomic>`. The rule `lock-free-asserts` holds 27 objects of four directories, and Stage 5 deletes it when they move (section 8.6a).

**Where the sources do not agree.**
- QDESIGN gives `cas_acq_rel` a `Result<Unit, T>`. Main gives `CasRefusal<T>`, which names the value that the cell held.
- QDESIGN gives `load_owned`. Main has no relaxed operation, because the tree has no proof type `SoleWriter` (CLAUDE.md §XXII). The design keeps `load_owned` for G3, with the proof type.
- `CacheLine::get()` of main borrows with no key. Main wins until the owner decides P6 (section 11.1).
- QDESIGN puts `PublishOnce` and `OneShotFlag` beside `Atomic`. Appendix E rebuilds them in place in `fixy/handle/`, and this section agrees.

**The recipes.**

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
| `x.wait(v)` and `x.notify_one()` | the futex door of the Os family | judgment (the context) |
| `alignas(64) std::atomic<T> x;` | `fixy::CacheLine<fixy::Atomic<T>> x;` | mechanical |

### 11.7 Scalar: numbers, bits, enumerations, strong types and quantities

**The idea.** Each operation on a fundamental value is total: no input is undefined. An input that can fail gives an `Option`. A type that proves the result gives the value with no check. No operation reads `errno`, a locale or a global, and no operation calls `fatal()`. A strong type gives a meaning to a number, and a quantity gives it a unit.

**The headers.** `include/foundation/core/Scalar.h` (main), `include/foundation/core/Index.h` (design), and one header for each group of the API (design). The builtins of R3 replace `<bit>`, `<limits>`, `<numeric>`, `<cmath>` and `<climits>`: `__builtin_clzg`, `__builtin_ctzg`, `__builtin_popcountg`, `__builtin_bswap16` to `64`, the overflow builtins, `__builtin_bit_cast`, `__builtin_sqrt`, `__builtin_fma`, the classification builtins, and the macros `__FLT_*`, `__DBL_*` and `__CHAR_BIT__`. The modular forms use the overflow builtins only, so no result changes with `-fno-strict-overflow`.

**The merges.** Slice 4.10 merges `foundation/Saturate.h`, `fixy/Saturate.h`, `fixy/Saturated.h` and `fixy/Checked.h` into the family (Appendix E). The checked operations then give an `Option`.

**The gates.**

| Gate | What it admits | State |
|---|---|---|
| `Integer<T>` | An integral type that is not cv-qualified, not `bool` and not a character type. Plain `char` is signed on x86_64 and unsigned on aarch64 | design |
| `SignedInteger<T>`, `UnsignedInteger<T>`, `Float<T>`, `Arithmetic<T>` | `Float` is `float` or `double`. `long double` has 80 value bits on x86_64 and 128 on aarch64 | design |
| `Enumerated<E>`, `DecodableFrom<To, From>` | An enumeration with an enumerator, and a decode with no pointer, reference, union, bit-field, empty class or sealed class | main |
| `BitValid<T>` | Each bit pattern of the value bytes is a value. `std::byte` is valid | design |
| `fits_each_value<From, To>` | Each value of `From` is a value of `To`, from the value bits and the signedness | design |

**The API.**

| Group | Operations | Contract | Cost | State |
|---|---|---|---|---|
| Limits | `max_of<T>`, `min_of<T>` (the lowest), `bits_of<T>`, `value_bits_of<T>`. For a `Float`: `min_normal_of`, `min_subnormal_of`, `epsilon_of`, `infinity_of`, `quiet_nan_of`, `signaling_nan_of` | Constants. A type that is not `Arithmetic` does not compile | none | design |
| Compare and select | `min(a, b)`, `max(a, b)` by value, `Bounds<T>::of(lo, hi)`, `bounds<lo, hi>()`, `clamp(v, bounds)`, `compare(a, b)` of two integers of each signedness, `fits<To>(v)`, `unsigned_abs(v)`, `abs_diff(a, b)` | `Bounds` proves `lo <= hi` and no NaN, so `clamp` has no check | one compare and one select | design |
| Conversions | `narrow<To>(v)` from an integer or a float, `wrapping_narrow<To>(v)`, `saturating_narrow<To>(v)`, `widen<To>(v)` | `narrow` gives `Option<To>`. `widen` takes only a pair of `fits_each_value` and has no check | at most two compares | design |
| Enumerations | `enum_from<E>(raw)` | `Option<E>`. A set with no gap is one compare, and a set of 64 values is one compare and one bit test | one compare | main |
| Arithmetic | `checked_add`, `checked_sub`, `checked_mul`, `checked_div`, `checked_rem`, `checked_neg`, `checked_shl`, `checked_shr` | `Option<T>`. A zero divisor and the minimum divided by -1 give no value | one flag test | design |
| | `wrapping_add`, `wrapping_sub`, `wrapping_mul`, `wrapping_neg`, `wrapping_shl`, `wrapping_shr` | Modular. A shift count is modulo `bits_of<T>` | none | design |
| | `saturating_add`, `saturating_sub`, `saturating_mul` | The exact result, or the clamp | one flag test | design |
| | `gcd(a, b)`, `widening_mul(a, b)` | `gcd` gives an unsigned result for each input. `widening_mul` gives a named struct of the high and the low half | a loop of `ctz` steps, one multiply | design |
| Bits | `popcount`, `countl_zero`, `countr_zero`, `countl_one`, `countr_one`, `bit_width`, `has_single_bit`, `bit_floor`, `bit_ceil`, `rotl`, `rotr`, `low_mask<T>(n)`, `byteswap` | `bit_ceil` gives `Option<T>`. `rotl` and `rotr` are total for each count. `low_mask` gives each bit for a count of `bits_of<T>` or more | one instruction in most cases | design |
| Bit copy | `bit_cast<To>(v)`, `decode<To>(v)` | `bit_cast` takes only a `BitValid<To>` with `DecodableFrom`, and has no check. `decode` gives `Option<To>`, and the empty value of a niche gives no value | none, or the checks of the bool and enumeration parts | main for `decode`, design |
| Floats | `fp::is_nan`, `fp::is_inf`, `fp::is_finite`, `fp::is_normal`, `fp::sign_bit`, `fp::abs`, `fp::copy_sign`, `fp::sqrt`, `fp::fma`, `fp::total_key` | `sqrt` of a negative value or a NaN gives the canonical NaN on each target. `total_key` gives the key of the IEEE total order | one instruction, or a call of libm `fma` with no FMA instruction | design |
| Static index | `StaticIndex<I>`, `index<I>()` | The proof of a constant index for `at` | none | design |
| Values | `exchange(slot, value)`, `swap` (a function object) | A type that moves with `noexcept` and is not `Pinned` | a move | design |
| Random | `Philox4x32`, with a `FixedArray` counter and key | The counter-based generator of `include/crucible/Philox.h`, which moves into the base: the same counter and key give the same bits on each target | about ten integer instructions | design |
| Strong ids | `Id<Tag, Word>` | An explicit construction from a `Word`, `raw()`, `==` and `<=>`, and no arithmetic. The value `max_of<Word>` is the empty value of `Option<Id>`, through the member niche protocol of section 11.2 | none | new |
| Digests | `Digest<Tag>` | A 64-bit hash value with a meaning: an explicit construction, `raw()`, `==` and `<=>`, and no arithmetic. Each value is valid, so it has no niche | none | new |
| Quantities | `Quantity<Unit, Rep>` | A number with a unit. A unit is a tag with a dimension and a ratio to the base unit of its dimension. The checked, wrapping and saturating operations take two values of one unit. A product or a quotient with a plain number keeps the unit. `convert<ToUnit>(q)` gives `Option`, and it has no check when the ratio divides | the operation of `Rep` | new |

`Id` and `Digest` replace the macros `CRUCIBLE_STRONG_ID` and `CRUCIBLE_STRONG_HASH` of `include/crucible/Types.h`, in Stage 5. The base units of `Quantity` are bytes, elements, nanoseconds and cycles. `Duration` of the Os family is `Quantity<Nanosecond, std::int64_t>` (section 11.9).

**The family list.** Each name of the API table and of the gates. `parse` belongs to the Report family (section 11.8).

**Removes.** Five rows: `allow F0_core <bit> until Scalar`, `allow F0_primitives <numeric> until Scalar`, `allow F1_lattices <limits> until Scalar`, `allow X1_wrappers <cmath> until Scalar` and `allow X3_os <climits> until Scalar`. Two admit rows: `admit <limits> until Scalar` and `admit std::bit_cast until Scalar plain-result`. The family has no R5 import of its own. A bit builtin can call a helper of libgcc, such as `__popcountdi2` in a Debug build with no `POPCNT`. Such a helper stays a `libc-allow` row.

**The load.** The ledger holds 301 findings of `<limits>` and 213 of `std::bit_cast` (the kinds `replace-pending`). Names: `std::bit_cast` 76, `std::min` 63 and `std::max` 50. The macro `CRUCIBLE_STRONG_ID` makes each id of `include/crucible/Types.h`. The pilot found one call site: `fixy::min`.

**Where the sources do not agree.**
- R9 cut the API to the calls of the tree. The design bar keeps `countl_one`, the float limits and `gcd`.
- QDESIGN gates `enum_from` on a scoped enum. Main gates it on `Enumerated`, because an unscoped enumeration with a fixed type has the same hazard.
- QDESIGN gives `clamp` a `pre` specifier, which the tree refuses (§XII). `Bounds` proves the order of the bounds, so `clamp` has no branch.
- QDESIGN gives `abs`. `unsigned_abs` shows its unsigned result, so a change of `std::abs` cannot give a modular value with no sign.
- `fixy/Checked.h` declares `checked_*`, `wrapping_*` and `saturating_*` with the same parameters, and two declarations of one name are ill-formed. So the re-export of the three sets waits for slice 4.10, which merges `foundation/Saturate.h`, `fixy/Saturate.h`, `fixy/Saturated.h` and `fixy/Checked.h` into the family (Appendix E).
- QDESIGN moves Philox into Scalar, and the G2 Scalar design does not name it. This section keeps the move, because the Check proposal of section 11.11 uses a generator of the base.

**The recipes.**

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
| `std::clamp(v, lo, hi)` | `fixy::clamp(v, fixy::bounds<lo, hi>())`, or `Bounds<T>::of(lo, hi)` for run-time bounds | mechanical for constant bounds |
| `std::cmp_less(a, b)`, `std::in_range<T>(v)` | `fixy::compare(a, b) < 0`, `fixy::fits<T>(v)` | mechanical |
| `std::abs(v)` of an integer | `fixy::unsigned_abs(v)` | judgment (the unsigned result) |
| `CRUCIBLE_STRONG_ID(Name, Word)` | `using Name = fixy::Id<NameTag, Word>;` | mechanical |
| a size or a time as a plain integer | `fixy::Quantity<Unit, Rep>` | judgment (the unit) |

### 11.8 Report: `Site`, `Fmt`, `format`, `report`, `fatal`, `unreachable`, `parse`

**The idea.** Each text that the program writes goes through one checked formatter into a region, and out through one sink with no buffer. Each fatal exit and each point that control never reaches goes through one cold function with a site. Each number that the program reads from a text goes through one parser.

**The headers.** `include/foundation/core/Report.h` and `include/foundation/core/Format.h` (main). The cold functions are in `src/foundation/core/Report.cpp` and `src/foundation/ContractHandler.cpp`. The sink write is in `src/foundation/os/ReportSink.cpp`, and it issues the raw write of `foundation/os/Syscall.h`.

**The API.**

| Operation | Contract | Cost | State |
|---|---|---|---|
| `Site` | The file, the line and the function of a call | none | main |
| `Fmt<Args...>` | A string literal with one `{}` for each argument, and its `Site`. `{{` and `}}` write one brace. The consteval constructor refuses each other text and names the rule | none at run time | main |
| `report(sink, fmt, args...)` | Writes to `Sink::Out` or `Sink::Err` before it returns. A text of 512 bytes or less takes one system call. Safe in a signal handler | the format and one write | main |
| `fatal(fmt, args...)`, `fatal(reason)` | Writes the text and the site to standard error, and ends the process in each build and with each contract semantic | one branch to a cold call | main |
| `unreachable()` | The same, for a point that control never reaches | one branch to a cold call | main |
| `format(out, fmt, args...)` | Writes into a `FixedText`. Gives `Result<TextView, Truncated>`, and on `Truncated` the text holds the first characters | the format | main |
| `Formattable<T>` | An integer, a `bool` or a `TextView` | - | main |
| more argument kinds | A scoped enumeration by its reflected name, an `Id` and a `Digest` by their word, an `Errno` by its name, and a `Site` | the format | design |
| float values | A fixed decimal form with no locale, from an algorithm of the family. The rule table has no `allow` row of `<charconv>`, so no base file includes it | the format | design |
| `parse<T>(text)` | `Result<T, ParseError>`. Decimal and `0x` hex exactly, no white space, no sign on an unsigned type, and no overflow. `ParseError` holds the position in the text. It uses `checked_mul`, `checked_add` and `narrow` of the Scalar family | one pass over the text | design |
| the prefix map | `-ffile-prefix-map` and `-fmacro-prefix-map`, so a `Site` holds a path relative to the source root (section 10.3a) | none | design |

`parse` is the dual of `format`, and it belongs here because its input is a text and its error holds a position in that text.

**The family list.** `Site`, `Fmt`, `report`, `Sink`, `fatal`, `unreachable`, `format`, `Truncated`, `Formattable`, `parse`, `ParseError`.

**Removes.** Three rows: `allow F0_primitives <cstdlib> until Report`, `allow F1_diag <cstdio> until Report` and `allow F1_diag <source_location> until Report`. The text of the row of `<cstdlib>` also names the Ref family (the frees of the two buffers) and the Os family (`std::getenv`). The text of the row of `<cstdio>` names the streams of `fixy/os/OwnedFile.h`. So these two rows go after Report, Ref and Os. The admit row `admit std::source_location until Report`. The R5 import of `libfixy.a`: `__fprintf_chk`. The R5 imports of the `libcrucible*.a` libraries: `__fprintf_chk`, `__vfprintf_chk`, `stderr`, `abort` outside the abort door, and `__isoc23_strtoull`, which `parse` replaces.

**The load.** C calls: `fprintf` 1,097, `stderr` 1,062, `abort` 403, `printf` 230, `_Exit` 80, `snprintf` 21, `fputs` 20, `fflush` 16 and `puts` 14. The ledger holds 31 findings of `std::source_location` in `test/`. Objects: `std::from_chars_result` 12 and `std::to_chars_result` 9. The pilot found one call site: the `PASSED` lines of the tests, which go through `report`.

**Where the sources do not agree.**
- QDESIGN builds `Site` from `std::source_location` in a consteval context. Main builds it from the builtins of R3.
- QDESIGN gives `parse` no family, and the G2 Scalar design puts it in Report. This section agrees with the G2 design.

**The recipes.**

| Pattern A | Pattern B | Kind |
|---|---|---|
| `std::fprintf(stderr, "x=%d\n", x);` | `fixy::report(fixy::Sink::Err, "x={}\n", x);` | mechanical, except width and precision |
| `fprintf(...); std::abort();` | `fixy::fatal("...", args...);` | mechanical |
| bare `std::abort()` | `fixy::fatal("reason")` | judgment (the reason) |
| `std::unreachable()` | `fixy::unreachable()` | mechanical |
| `std::snprintf(buf, n, ...)` | `fixy::format(text, "...", ...)` | mechanical |
| `std::to_string(x)` | `fixy::format` | mechanical |
| `std::strtoull`, `std::from_chars` | `fixy::parse<std::uint64_t>(view)` | mechanical |
| `std::printf("  test_x: PASSED\n");` in a test | `fixy::report(fixy::Sink::Out, "  test_x: PASSED\n");` | mechanical |
| `std::source_location loc = std::source_location::current()` | a `fixy::Site` parameter with its default | mechanical |

### 11.9 Os: the doors to the kernel

**The idea.** Each system call, file, path, directory, clock, thread, wait, socket and read of the environment goes through one door. Each door takes a context that holds its effect atoms, and gives `Result<T, Errno>`. The descriptors, paths and names that a door gives are base types. A door has the same code path in each build (R18), and it changes no state of the whole process (R20).

**The homes.** The family lives in the two door directories of D16 and not in `foundation/core`. `include/foundation/os/` holds the floor and the types. These are the raw system call, the kernel floor, the argument words and `Errno`. They are also the file and directory doors, `PathText`, the auxiliary vector, the guarded memory and the abort door. `include/fixy/os/` holds the doors of the layer X3, and `src/fixy/os/` holds their source files. Each system header has one owner (R4).

**The namespaces.** `Errno` and the floor are in `foundation::os`. The doors keep the namespace of their subject: `fixy::fs`, `fixy::net`, `fixy::time`, `fixy::thread` and `fixy::futex` (main). A new door takes the namespace of its subject. The doors of the state of the process (`env`, `args`) and a re-export of `Errno` take `fixy::os` (design).

**The API on main at d36be7daf.**

| Door | Operations | Header |
|---|---|---|
| The system call | `raw_syscall`, the enumeration `Syscall`, `minimum_kernel()` and the start-up check | `foundation/os/Syscall.h`, `KernelFloor.h` |
| The error | `Errno`, `result_of(word)`, `unit_of`, `error_number` | `foundation/os/Errno.h` |
| Files and directories | `read_file(path, view)`, `read_file(path, text)`, `for_each_entry(path, visitor)`, `PathText<Capacity>` | `foundation/os/File.h`, `Directory.h`, `PathText.h` |
| Memory | `map_guarded_region`, the ASan poison | `foundation/os/GuardedRegion.h`, `Poison.h` |
| The exit | `abort_process()` | `foundation/os/Abort.h` |
| Descriptors and files | `OwnedFd`, the file door, the durable writes of the Cipher | `fixy/os/Fd.h`, `Fs.h`, `CipherDurable.h` |
| Mappings and I/O | `OwnedMmap`, `mint_mmap`, the `io_uring` door | `fixy/os/OwnedMmap.h`, `Mmap.h`, `Io.h` |
| Waits | `wait`, `wake_one`, `wake_all` on a `FutexWord`, `Deadline` | `fixy/os/Futex.h` |
| Threads | the start and the join of the thread door, `ThreadName`, `ThreadOptions` | `fixy/os/ThreadTasks.h` |
| Hooks | the signal door (an Init context only), the exit hooks, the fork hook | `fixy/os/Signal.h`, `ExitHooks.h` |
| Time | `MonotonicClock`, the TSC and PTP readers | `fixy/os/MonotonicClock.h`, `Time.h` |
| Placement | `Sched.h`, `CpuPinned.h`, `NumaPlacement.h`, `Topology.h` | `fixy/os/` |
| Sockets | the netlink route socket | `fixy/os/Socket.h` |

**The design.**

| Operation | Contract | State |
|---|---|---|
| one error type | Each door gives `Result<T, Errno>`, and no `std::error_code` or plain `int` stays (slice 4.1) | design |
| `errno_text(e)` | A static `TextView` for each `Errno` | design |
| `Path<Source>` | A `FixedText` path with its provenance tag, with no `std::filesystem`. A `..`, a zero byte and a path that is too long are refused, as `fixy/os/Path.h` does today | design |
| `join(base, component)` | `Result<Path<Source>, PathError>` | design |
| `OwnedFd`, `Dirfd` | The owners of a descriptor of `fixy/os/Fd.h` and `fixy/os/Fs.h` (main, a keep row of Appendix E). Only the doors of the family can adopt a descriptor | main, design |
| `open_at`, `read_at`, `write_at`, `unlink_at`, `rename_at`, `make_dir_at`, `make_dirs`, `exists`, `temp_file`, `temp_dir` | The calls of the `*at` and `p*` forms that x86_64 and aarch64 have. `EINTR` and a partial transfer are handled inside the door | design |
| `read_all(ctx, alloc, fd)` | `Result<GrowList<std::byte>, Errno>` | design |
| `env(init_ctx, name)` | `Option<FixedText<4096>>`, a copy. An Init context only | design |
| `args(argc, argv)` | `View<CText const>`, the one door from `main` | design |
| `Thread` | A long-lived thread, linear. `mint_thread(bg_ctx, permission, body)` starts it, and `join() &&` gives the permission back. A stop goes through a `OneShotFlag` | design |
| the socket doors | `bind`, `send`, `recv`, `recvmsg`, `setsockopt`, `getsockopt`, `if_nametoindex` and `ioctl` | design |
| the doors of the kernel facilities | `perf_event_open`, `mlock`, `prctl`, `sched_getattr` | design |
| the counter door | Maps the perf page of each event one time, and reads the counter with `rdpmc` (section 10.3a) | design |
| the I/O door | Files and sockets on `io_uring` with registered buffers and files, and a fallback with plain system calls that a start-up probe selects (section 10.3a) | design |
| time | `Instant`, `Duration` (`Quantity<Nanosecond, std::int64_t>`), `MonotonicClock`, `Deadline`. The hot path reads the cycle counter, and the wall clock calls glibc `clock_gettime` (section 10.3a) | main for `MonotonicClock` and `Deadline`, design |
| the BPF owners | `bpf::Object`, `bpf::Map`, `bpf::Link`: linear owners of the libbpf handles | design |

`fixy/os/OwnedFile.h` merges into the file doors of the family in slice 4.1 (Appendix E), so `std::FILE` leaves each public signature. Appendix E asks the owner where `fixy/os/CipherDurable.h` goes.

**The family list.** The names of the two tables.

**Removes.** Six rows: `allow F1_diag <system_error> until Os`, `allow F1_rest <csignal> until Os`, `allow X3_os <cerrno> until Os`, `allow X3_os <ctime> until Os`, `allow X3_os <filesystem> until Os` and `allow X3_os <mutex> until Os`. The text of the row of `<system_error>` also names `std::errc` of the JSON emitter, which Report replaces, so that row goes after Os and Report. The R5 imports of the `libcrucible*.a` libraries:
- The imports of `std::filesystem` (11 names)
- The imports of the file streams: `std::basic_ifstream`, `std::basic_ofstream`, `std::basic_filebuf`, `std::basic_ios`, `std::ios_base`, `std::locale`, `std::ctype<char>`, `std::getline`, and the reads and writes of `std::istream` and `std::ostream`
- The imports of `std::error_code`: `std::system_category`, `std::generic_category` and the typeinfo of `std::error_category`
- The imports of `std::thread`: `_M_start_thread`, `join`, the typeinfo and the vtable of `std::thread::_State`, the vtable of `__si_class_type_info`, and the vtable row of `std::thread::_State_impl`
- `std::__detail::__notify_impl`, with the futex door
- The C calls of files: `__errno_location`, `close`, `faccessat`, `fdatasync`, `fsync`, `openat`, `renameat`, `unlinkat`, `write` and `strerror`
- The C calls of sockets: `recv`, `__recv_chk`, `recvmsg`, `send`, `bind`, `setsockopt`, `getsockopt`, `if_nametoindex` and `ioctl`
- The other C calls: `getenv`, `getpid`, `sysconf`, `syscall`, `pthread_self` and `sched_yield`
- `clock_gettime`, which becomes a call of the time door. D12 keeps it in glibc, so it stays a `libc-allow` row of `libfixy.a`
- `__cxa_begin_catch` and `__cxa_end_catch` of the `try` and `catch` around `std::filesystem`
- The 21 `host-ratchet` imports of libbpf of `libcrucible_perf.a`, which the BPF owners take.

**The load.** Objects: `std::jthread` 96, `std::filesystem::path` 52, `std::chrono::time_point` 26, `std::thread` 15, `std::chrono::duration` 14, `std::stop_token` 13, `std::error_code` 12, `std::basic_ifstream` 12, `std::latch` 6, `std::basic_ofstream` 5, `std::lock_guard` 5 and `std::mutex` 3. Names: `std::jthread::join` 63, `std::chrono::steady_clock::now` 48 and `std::error_code::message` 39. C calls: `__errno_location` 60, `close` 56, `read` 23, `getenv` 16, `setenv` 13, `sched_getaffinity` 11, `mkdtemp` 11 and `syscall` 8. The calls of a child process (`fork` 27, `waitpid` 28, `pipe` 14, `dup2` 14, `_exit` 17 and `_Exit` 80) wait for the process door of section 11.11. The pilot found one call site: the background pipeline takes a thread type of the family in place of `std::jthread`.

**Where the sources do not agree.**
- Section 10 of this plan gave the eight families one home in `foundation/core`. D16 puts the Os family in the two door directories, and main agrees.
- QDESIGN gives `exit_now(code)`. A process exit outside the abort door is a part of the process door, which waits for the owner (section 11.11).

**The recipes.**

| Pattern A | Pattern B | Kind |
|---|---|---|
| `std::filesystem::path{root} / "HEAD"` | `fixy::fs::join(root, "HEAD")` | mechanical |
| `std::filesystem::exists(p)` | `fixy::fs::exists(ctx, p)` | mechanical (the context must reach the site) |
| `::pread(fd, buf, n, off)` | `fixy::fs::read_at(ctx, fd, view, off)` | mechanical |
| `opendir` and `readdir` loop | `for_each_entry` | judgment |
| `std::jthread` | `mint_permission_fork` or `fixy::thread::Thread`, which start through the thread door of unit 2c | judgment |
| `std::mutex`, `std::condition_variable`, a spin with `sleep_for` | the futex door of unit 2c | judgment |
| `::read`, `::write`, `::pread` in a loop | the I/O door of G6 (`io_uring`, or its fallback) | judgment (registered buffers) |
| `std::chrono::steady_clock::now()` | the `MonotonicClock` reader | mechanical |
| `std::getenv("X")` | `fixy::os::env(init_ctx, "X")` | judgment (the context) |
| `std::error_code` | `fixy::os::Errno` | mechanical |
| `std::chrono::nanoseconds`, a duration in a plain integer | `fixy::time::Duration` | mechanical |
| `mkdtemp`, `std::filesystem::remove_all` in a test | `temp_dir` and the directory doors | judgment |
| `std::thread` or `std::jthread` that a class keeps | `fixy::thread::Thread`, with `join() &&` at the end of its owner | judgment |

### 11.10 Ownership, borrows and memory

`misc/12_10_2026_linear.md` holds the design of ownership: linear values, brands, borrows, regions, credits and interaction nets. This section does not state it again. Each family takes these parts of it:

| Family | Part | Section of the linear design | Phase |
|---|---|---|---|
| Choice | A linear payload or error leaves a sum only through a consumer that takes `&&`. A read through `const&` consumes nothing | 11, item 5 | A |
| Choice, Region | The error of `push` and `append` gives a linear value back | 11, item 10 | A |
| Record | A `Record` with a linear part is linear by containment | 2 (P1), 4 | A |
| Ref | `Box` is linear in shape 1, and its consume gives a `Hole` credit | 6 | C |
| Ref | No counted reference. A counted share is the grade `!n` at a named door | 11, item 2 | - |
| Ref, Atomic | The run-time id of a brand | 4 | B |
| Region | The region capability, the epochs, the frame regions, `BrandedIndex` of 4 bytes and the compact views | 6 | C |
| Region | Views with no brand, and the rules `view-escape` and `move-while-borrowed` | 5, 6 | D |
| Os | A handle that holds a `Permission`, such as `Thread`, is linear with a release door | 11, item 6 | A |
| Each family | Each exit from linearity is a named door of the door roster | 2 (P10), 4 | A |

### 11.11 The features with no design, and the proposals that wait for the owner

QDESIGN and the designs of 2026-10-17 give no design to five features. Each one gets a home in a family, or a proposal of a new family or a new verb. A proposal waits for a yes from the owner (section 17), and no unit builds it before that yes.

| Feature | Home | Status |
|---|---|---|
| Equality, order, hash, format and a byte codec, derived by reflection | a new family, Protocol | P1 waits for the owner |
| The test language: death tests, property tests with Philox generators and shrinking, the helper of a std oracle | a new family, Check | P2 waits for the owner |
| The iteration verbs `enumerate`, `zip`, `reverse`, `windows` and `chunks` | new verbs, given by Region and Record | P3 waits for the owner |
| Strong ids, and quantities with units | Scalar (section 11.7): `Id`, `Digest` and `Quantity` | designed in this section. No new family and no new verb |
| A process door | Os | P4 waits for the owner, because it changes D12 |
| One meaning of `get`, the result of `append`, and the refusal of a present key | the verbs of section 11.1 | P6 waits for the owner |

The unit design of G1 also leaves one question to the owner, P5. The protocol set of section 11.1 adds P6, because it changes three approved verb rows or the API of main.

**P1. The Protocol family.**
- What: one reflection walk derives five operations for a type that opts in through an annotation. They are `equal(a, b)`, `order(a, b)`, `hash(a)`, the format of a struct, and a byte codec of `encode(value, out)` and `decode<T>(bytes)`. The walk refuses what `decode` refuses: a pointer, a reference, a union and a read of padding. The codec writes fixed widths in little-endian order. `decode` of a byte view is the form of `decode` of the Scalar family over a run of bytes. Its `Result` holds the position of an error.
- Reason: each type writes these operations by hand today. Examples are `reflect_hash` of `foundation/reflect/Hash.h`, the serializer of `include/crucible/Serialize.h` and the macros of `include/crucible/Types.h`. The design bar asks for a derivation by reflection, and DetSafe asks for a hash and a byte image with no padding.
- Changes outside sections 10 and 11: R9 and section 4 (nine families), a row of section 1, CLAUDE.md §XXII, and a group after G5 in section 10.1.
- Depends on: Scalar (`Digest` and the bit operations), Region (`bytes` and `View`), Record, and Report (`Formattable`). `equal` of Region becomes the form of `equal` over a view.

**P2. The Check family.**
- What: the test language, for a Test context only. `expect_dies(body, text)` runs the body in a child and expects `SIGABRT` with the text on standard error. `property(seed, count, body)` takes Philox generators of the base and shrinks a failure to a minimal input with the same seed. `oracle(...)` runs a family operation and its std form over Philox inputs inside one `ORACLE:` region. `pass(name)` writes the `PASSED` line through `report`, and the assert macro of the tests calls `fatal()` with its text.
- Reason: `test/` holds 11,641 `std-entity` findings and 3,291 `c-library-call` findings. The harness repeats death tests, property loops and oracles by hand in each file, with `fork`, `waitpid`, `fprintf` and `abort`.
- Changes outside sections 10 and 11: R9 and section 4, a row of section 1, the unit `harness` of wave 5A (section 14.1), and CLAUDE.md §XIII and §XXII.
- Depends on: P4, Philox in the Scalar family (section 11.7), and Report.

**P3. The iteration verbs.**
- What: `enumerate()` gives each element with its proven index. `zip(a, b)` gives the pairs of two views: a fixed extent proves the counts equal, and a dynamic extent gives `Result<..., LengthMismatch>`. `reverse()` visits from the last element. `windows<N>()` gives each run of N consecutive elements with a step of one, as a `View<T, N>`. `chunks<N>()` gives each run of N elements with a step of N, and the rest as a separate `View`. Each one is a borrow with no allocation, and its cost is the loop bound.
- Reason: a loop with an index is the largest source of a raw subscript: `std::array::operator[]` 892, `std::vector::operator[]` 286 and `std::span::operator[]` 153. A reverse loop writes `while (i-- > 0)`, and a loop over windows adds an offset by hand. Each verb removes a branch or an index from such a loop.
- Changes outside sections 10 and 11: R9 and section 4 (fifteen verbs), and CLAUDE.md §XXII.

**P4. The process door.**
- What: doors of the Os family for a child process, for a Test context only. `spawn_child(test_ctx, body)` gives `Result<Child, Errno>`, and `wait(child)` gives the exit code or the signal. `pipe` and `redirect` give the output of the child, and `exit_now(code)` ends a child with no exit hooks. The doors call glibc `fork`, `waitpid`, `pipe2`, `dup3` and `_exit`, because glibc owns the fork handlers and the state of its threads.
- Reason: the tests call `fork` 27 times, `waitpid` 28, `pipe` 14, `dup2` 14, `_exit` 17 and `_Exit` 80, and each death test repeats them. P2 uses the door.
- Changes outside sections 10 and 11: the glibc list of D12, the deny list of R4 (`clone`) and the R5 allowlist of `libfixy.a`. It also changes the end state of the kind `process-start` of the rule `banned` (sections 8.6a and 13.4).

**P5. `map`, `and_then` and `or_else` on `Option` and `Result`.**
- What: three new verbs of the Choice family.
- Reason against them, the recommendation of the unit design of G1: `match` and `CRUCIBLE_TRY` cover each pattern of the recipes of section 11.2, so the verbs add a second road to the same result.
- Changes outside sections 10 and 11: R9 and section 4.

**P6. Three changes to the verbs of section 11.1.**
- What, part 1: `Box::get()` and `CacheLine::get()` of main go, and the dereference and `view()` take their place. Then `get` has one meaning, the checked lookup with a key, as law 1 asks.
- What, part 2: `append(alloc, v)` gives `Result<Unit, Full<T>>`, and each growable owner states a maximum at its mint. The principle P5 of the linear design gives each size a budget. A growth past the maximum then refuses, and the process does not end.
- What, part 3: `push(key, value)` of a table refuses a key that the table holds. The refusal gives the reason (full, or a key that the table holds) and a linear value back.
- Reason: the design bar asks for one meaning for each name and for no hidden end of the process. QDESIGN gives `append` a `Ref<T>`, which dangles at the next growth, and gives no refusal for a key that a table holds.
- Changes outside sections 10 and 11: the API of `foundation/core/Ref.h` and `foundation/core/Atomic.h` on main, and CLAUDE.md §XXII "The families on main". Each caller of `get()` changes too.

---

## 12. Layers and the include policy

| Layer | Directories | May include |
|---|---|---|
| F0 floor | `foundation/Platform.h`, `foundation/Pinned.h`, `foundation/Quarantine.h` | the base allowance (R2) |
| F0 core | `foundation/reflect/`, `foundation/contracts/`, `foundation/Brand.h`, `foundation/Lifetime.h`, `foundation/ByteSeal.h`, `foundation/Linear.h`, `foundation/diag/FailClosed.h`, `foundation/NoObject.h` | the base allowance |
| F0 os | `foundation/os/Syscall.h`, `foundation/os/KernelFloor.h`, `foundation/os/OpenFlags.h`, `foundation/os/FutexWords.h`, `foundation/os/MapWords.h`, `foundation/os/SchedWords.h`, `foundation/os/MemoryPolicyWords.h`, `src/foundation/os/Syscall.cpp`, `src/foundation/os/Intercepted.h`, `src/foundation/os/Intercepted.cpp` (D16, unit 2b) | the base allowance, plus each system header that the floor uses, through its one door. The rank is below F0 primitives, so that the families and each higher layer can use the syscall primitive |
| F0 primitives | `foundation/core/` and `src/foundation/core/` (the families), `foundation/Simd.h`, `foundation/ThreadLocalRef.h`, `foundation/ChannelBinding.h`. Until their merge rows of Appendix E land, also `foundation/Saturate.h` (slice 4.10), `foundation/AlignedBuffer.h` and `foundation/SwissTableBuffer.h` (slice 4.3) | the base allowance. No system header in a header. Cold code goes to `src/foundation/` |
| F0 doors | the other files of `foundation/os/` and `src/foundation/os/` | the base allowance, plus each system header through its one door. The rank is above F0 primitives, so that a door can return a `Result` of the families |
| F1 algebra | `foundation/algebra/`, without `lattices/` | the base allowance. Runtime std names only in `consteval` context |
| F1 lattices | `foundation/algebra/lattices/` | the base allowance. Runtime std names only in `consteval` context |
| F1 diag | `foundation/diag/` (other files) | the base allowance. Runtime std names only in `consteval` context |
| F1 effects | `foundation/effects/` | the base allowance. Runtime std names only in `consteval` context |
| F1 permissions | `foundation/permissions/` | the base allowance. Runtime std names only in `consteval` context |
| F1 rest | the other files of `foundation/` and `src/foundation/` | the base allowance. Runtime std names only in `consteval` context |
| X1 wrappers | `fixy/` top-level wrapper headers, `fixy/fp/`, `src/fixy/` | the base allowance |
| X2 binding | `fixy/Fn.h`, `fixy/Axis.h`, `fixy/Atom.h`, `fixy/atoms/`, `fixy/collision/`, Collision, Corpus, Reject, Insights, Role, Throws, Federation | the base allowance |
| X3 os | `fixy/os/`, `src/fixy/os/` | the base allowance, plus each system header through its one door |
| X4 concurrent | `fixy/concurrent/`, `fixy/handle/` | the base allowance. Threads start through X3 |
| X5 session | `fixy/session/`, `src/fixy/session/` | the base allowance |
| X6 channels | `fixy/channels/` | the base allowance |

A layer can include a lower layer and itself. It cannot include a higher layer.

The six F1 layers come from the split of unit 2d (owner, 2026-10-11), and they land with items 2 to 4 of that unit. Before that commit, the rule table has one layer F1 algebra for the six rows.

**Headers that no layer includes at the end of Stage 4:** `<thread>`, `<chrono>`, `<filesystem>`, `<fstream>`, `<sstream>`, `<iostream>`, `<functional>`, `<memory>`, `<ranges>`, `<algorithm>`, `<set>`, `<map>`, `<unordered_map>`, `<mutex>`, `<any>`, `<variant>`, `<expected>`, `<optional>`, `<system_error>`, `<charconv>`, `<format>`, `<regex>`, `<cmath>`, `<source_location>`, any third-party header.

**The admission rule for a new header.** A header joins the allowance of a layer only if all these conditions hold:
1. A member of the layer needs a declaration from it, and no lower layer and no builtin gives the declaration.
2. Exactly one door owns it. No std or C type crosses the door's public signatures, except an integer of a fixed width or a base type. A typedef of the C library, such as `::off_t` or `::mode_t`, is a system type (owner, 2026-10-08).
3. It adds no runtime state: no static initializer, no locale, no stream, no throw path.
4. Its preprocessed cost is measured and recorded in the rule table. More than 5,000 lines needs owner approval.
5. A cold call goes to a source file, not to a header.

---

## 13. Stage 4: move the base onto the families

**Goal.** No std runtime type in the base, and no std type in any base signature. The base includes only its allowance.

### 13.1 Slices

A slice migrates one base API group and every caller in the whole tree, in one clean cut. When a caller is in a quarantined file, the slice changes only the lines that the API change forces. The rest of that file waits for Stage 5.

Appendix E gives each base header a verdict, a family and a slice. The last column of this table names the rebuild, merge and delete rows of each slice. Appendix E gives the keep rows.

| Slice | Content | Callers | Rows of Appendix E |
|---|---|---|---|
| 4.1 Os results | Every `fixy::os` door returns `Result<T, os::Errno>`. One `Errno` type replaces `std::error_code` and the plain `int` of Sched. The slice also removes each system type from the public signatures of the doors (R4). A base record replaces each struct of the kernel or of the C library, and a base type replaces each integer typedef of the C library. On 2026-10-09, seventeen public signatures hold a system type. In `fixy/os/Time.h`, `PtpClockReader::caps()` holds `::ptp_clock_caps`, `nanos_from_timespec` holds `std::timespec`, and `clockid_for` holds `::clockid_t`. `mint_file`, `mint_durable_truncate_file` and `mint_durable_append_file` of `fixy/os/Fs.h` and the three mints of `fixy/os/CipherDurable.h` hold `::mode_t`. `file_size` of `fixy/os/Fs.h`, `mint_mmap` of `fixy/os/Mmap.h`, `OwnedMmap::mint_region` of `fixy/os/OwnedMmap.h` and the two overloads of `zerocopy_transfer` of `fixy/os/Io.h` hold `::off_t`. `OwnedFile::get()` and `OwnedFile::release()` of `fixy/os/OwnedFile.h` and `Topology::log_summary` of `fixy/os/Topology.h` hold `std::FILE*` | about 133 base uses, many callers in crucible and test | Rebuild: `fixy/os/` CpuPinned, Fs, Io, Mmap, NumaPlacement, OwnedMmap, Sched, Socket, Time, CipherDurable. Merge: `fixy/os/OwnedFile.h` |
| 4.2 Choice in concurrent and wrappers | A pop of a ring or a channel, a load of `AtomicSnapshot` and a step of a session decorator give `Option`. `admit_refined` and the version gate of `EpochVersioned` return `Result`. The checked operations of `Checked.h` go to the Scalar family in 4.10 | the rings are on hot paths: check the disassembly | Rebuild: `fixy/` Refined, EpochVersioned. `fixy/channels/` Endpoint, SwmrSession. `fixy/concurrent/` AtomicSnapshot, SpscRing, MpscRing, PermissionedSpscChannel, PermissionedMpscChannel. `fixy/session/` CrashTransport, Recording, Transport, Checkpoint, EventLog |
| 4.3 Region in the wrappers | `fixy/FixedArray.h` merges into the fixed array of the Region family, which has no `operator[](size_t)`, no `data()` and no raw iterator. `Borrowed`, `ScopedView`, `OwnedRegion` and `SharedRegion` merge into `View` and the Ref family, with brands. `AlignedBuffer` and `SwissTableBuffer` merge into the Region family. `foundation::simd` loads and stores take `View<T const, Lanes>`. `CyclicBuffer` indexes through `Index<N>`. `AppendOnly` stores a `GrowList`. Each other `std::span`, `std::string_view` and `std::array` of a base API that runs at run time goes to the Region family | every caller of those accessors | Rebuild: `foundation/` Lifetime, Simd, contracts/Decide, reflect/EnumName, reflect/Enumerate. `fixy/` Mutation, ConstantTime, SipHash, Federation, CyclicBuffer, session/Classified, session/EventLog, session/Witnessed. Merge: `foundation/` AlignedBuffer, SwissTableBuffer. `fixy/` Borrowed, ScopedView, OwnedRegion, SharedRegion, FixedArray |
| 4.4 Atomic in the base | `AtomicMonotonic`, `MpscRing`, `OneShotFlag`, `PublishOnce`, `PublishCommit`, `Once`, `SpinLock` and each other holder of a `std::atomic` move to `Atomic`. `ChaseLevDeque` keeps its relaxed and seq_cst operations as cited `detail` code inside the base. `CpuPinned` and `session/Watch.h` use `Atomic` at this time | few callers outside | Rebuild: `foundation/` ChannelBinding, effects/Ctx, permissions/SharedPermission. `fixy/` Mutation, channels/LazyEstablishedChannel, concurrent/MpscRing, concurrent/ChaseLevDeque, handle/Once, handle/OneShotFlag, handle/PublishCommit, handle/PublishOnce, os/SpinLock, session/CrashTransport, session/VigilModeCell |
| 4.5 Report in the base | `fail_invariant`, the diag runtime sink, `JsonEmitter`, and every `std::source_location` in a public API move to `Site` and the Report sink | few callers outside | Rebuild: `foundation/diag/` Runtime, JsonEmitter. `fixy/session/` Entry, Mint, AsyncChannel, Stepping, Handle, Delegate, Watch, Flow, CrashTransport, Checkpoint |
| 4.6 Os internals | `Path` stops using `std::filesystem::path`. Each `try`/`catch` site that unit 2b did not remove goes. Unit 2b moves Topology off `ifstream`, `filesystem`, `set` and `map`, and unit 2c moves `std::jthread` to the thread door | callers of Path | Rebuild: `fixy/os/` Path, Topology |
| 4.7 Record in the base | `pair` and `tuple` in base APIs and in the stage and pipeline runners become `Record` or `Pack` | few callers outside | Rebuild: `foundation/permissions/` Permission, PermissionFork, ReadView. `fixy/` Federation, concurrent/Pipeline, concurrent/Spawn, concurrent/Stage, session/Flow |
| 4.8 Foundation remainder | the other runtime uses in foundation (233 runtime, 367 constexpr data, 36 in src/foundation) | none outside | Rebuild: `foundation/algebra/lattices/` AffinityLattice, HappensBefore, StrongCounterLattice, MonotoneLattice |
| 4.9 Fixy remainder | the other runtime uses in fixy, one unit for each directory: concurrent, session, os, handle, top level | none outside | Rebuild: `fixy/` SelfContained, os/ThreadName |
| 4.10 Scalar in the base | `foundation/Saturate.h`, `fixy/Saturate.h`, `fixy/Saturated.h` and `fixy/Checked.h` merge into the Scalar family, and the checked operations return `Option`. Each `std::numeric_limits`, `std::bit_cast`, operation on bits and operation on a floating value of the base goes to the Scalar family | 9 production files include `foundation/Saturate.h` | Merge: `foundation/Saturate.h`, `fixy/` Saturate, Saturated, Checked. Rebuild: `fixy/fp/Polynomial.h` |
| 4.11 Deletes | each delete row of Appendix E that the owner approves | tests only | Delete (proposal): `foundation/reflect/RawEscape.h` |

### 13.2 Order and parallelism

| Wave | Slices | Reason |
|---|---|---|
| 4-I | 4.3, 4.4, 4.5, 4.7, 4.10 | the slices change different APIs, and a shared file takes a merge of main. On 2026-11-12, the rows of 4.10 had 66 direct includers outside the base, and 26 of them also include a header of a row of the four other slices |
| 4-II | 4.1, then 4.2 | 4.1 and 4.2 touch many of the same caller files, so they run one after the other |
| 4-III | 4.6 | needs the Os results of 4.1 |
| 4-IV | 4.8 and the five units of 4.9, in parallel | internal only |
| no wave | 4.11 | a delete row becomes work only after the owner approves it. Then it can run in each wave, because only tests include the header |

### 13.3 Special care

- **Determinism.** A type change changes reflected names, so row-hash goldens and federation keys can move. Regenerate the goldens (`utils/tools/dump_row_hashes.cpp` and its golden file) in the same commit, and list each moved identity in the report.
- **Hot paths.** For each slice that touches a hot path (4.2 rings, 4.3 simd and indexing, 4.4 atomics), compare the disassembly of the hot functions before and after, and run the benches.
- **ABI.** A `Result` that returns in memory where the std type returned in registers is a regression. Check the return convention of each hot `Result`.
- **A rule of Stage 1B.** The rule `banned` reads base files (section 8.6a). The slice that removes the last base include of `<vector>` deletes its kind `vector-reserve`. The last slice of Stage 4 deletes the base part of its kind `process-start`. Each deletion also removes the plants and the ledger rows of the part.

### 13.4 The S4 gate

- The plugin enforces the full include allowance of every base layer as errors, with an empty ledger.
- A reflection test walks every base namespace and refuses a std runtime type in any public signature or data member.
- The link guard (R5) passes for `libcrucible.a`. Its first allowlist: `__cxa_guard_acquire`, `__cxa_guard_release`, `__cxa_guard_abort`, `__cxa_atexit`, `__gxx_personality_v0`, and `operator new` and `operator delete` only if the Ref family uses them.
- The libc rows of the symbol ledger (R5) hold no ratchet row for `libfixy.a`. Each libc symbol of `libfixy.a` is a call that D12 keeps in glibc, in its door.
- The kinds `vector-reserve` and `process-start` of the rule `banned` have no base part. The include rules refuse `<vector>` and each header of a process start in a base file outside its door.
- Each rebuild row and each merge row of Appendix E is on main, and each delete row that the owner approved.
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
| 5B | rest | the remaining `include/crucible` files | part | |
| 5C | bench | `bench/` | 2,602 | `bench_harness.h` first (541 findings) |
| 5C | fuzz | `test/fuzz/` | 1,272 | the libFuzzer entry keeps an opt-out region |
| 5C | tests | `test/` split by directory: fixy, foundation, core tests in three or four groups | 18,262 less the harness share | attack tests and raw-probe fixtures get reasoned regions |
| 5C | fixtures | the negative-compile fixtures | part of test | a fixture that probes raw behavior on purpose keeps a region |
| 5D | vessel | `vessel/` | 102, partial | needs `TORCH_DIR`. The PyTorch ABI stays behind regions. If a configuration of the ledger then compiles `vessel/torch`, the unit deletes the vessel part of the kind `process-start` of the rule `banned` (section 8.6a) |
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
- The rule `lock-free-asserts` of Stage 1B reads `include/crucible/{canopy,cntp,topology,warden}/` (section 8.6a). The units cntp, observe and rest move the atomics of these directories to `fixy::Atomic`. The last of the three deletes the rule, with its module, its plants, its ledger, its registry row and its `path` rows.
- A unit that moves a raw system call into an Os door removes the claim of that call from the ledger of the rule `syscalls`, in the same commit.

### 14.4 The S5 gate

- Every quarantined directory has `enforce error` in the rule table.
- The opt-out ledger holds only the permitted reasons of section 16.
- The full suite passes in every preset, and the benches show no regression against the S0 numbers.

---

## 15. Stage 6: close

1. **Error mode everywhere.** Set the plugin default to ERROR in every preset. REPORT stays available for census runs.
2. **The opt-out review.** Read each region of the ledger again. Remove the regions that a primitive now covers. Each remaining region names its reason and its owner.
3. **CLAUDE.md.** Rewrite the layer rules, replace the std tables of §III and §IV with the families, and describe the quarantine, the doors and the extension lane. Remove every rule that the plugin now enforces by construction, and name the plugin in its place.
4. **Guards.** Stage 1B built the utils system (section 8.6a). Make sure that the subsequent steps of the table "Rules that a subsequent stage deletes" of section 8.6a are done: the rule `lock-free-asserts` and the base parts of the kinds `vector-reserve` and `process-start` of the rule `banned` are gone, and the rule `syscalls` holds no claim of a base file. Then make sure that the part witness of the meta rule `registry` finds no rule without a plant.
5. **The adversarial campaign.** A fresh agent tries to get a std object, a raw pointer or a C call past the plugin: through templates, `auto`, aliases, macros, ADL, lambda captures, structured bindings, default arguments, `decltype`, `requires`, `sizeof`, and a header that a quarantined file includes from a base directory. Each finding is fixed, or pinned on a ledger that can only shrink.
6. **Decision point DP3.** With no `try`/`catch` left in the tree, decide whether to build with `-fno-exceptions` and `-fno-rtti`.
7. **Decision point DP4.** Decide whether foundation stays public to quarantined code or goes behind fixy re-exports.

**The S6 gate** (the final gate):
- Four presets (Debug, Release, TSan, UBSan-strict) with zero failures.
- The plugin in error mode in every tree.
- The link guard passes for every binary except the vessel library.
- The gauge numbers at or below the S0 numbers.
- The tests `vessel_test_NAME` of the vessel pass in Release.

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
- Each region is a row of the kind `region` of `utils/ledgers/quarantine.txt`: file, reason class, reason text. Before unit 1B.7 lands, the rows are in `utils/scripts/quarantine-region-ledger.txt`. The ledger can only shrink after Stage 5.
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

### 17.1 Notes for later extensions (2026-10-03)

These two items are not units of this plan. They are recorded here, so that no change of this plan closes them.

- **The device-queue family.** Each fast device has one shape (D14): a submission ring, a completion ring and a doorbell in mapped memory. The family would hold `DmaRegion` (pinned memory with a device address, opaque and linear), `Mmio<T>` (register access with explicit fences that the compiler cannot remove or reorder), `Doorbell` (an MMIO write with its write barrier) and typed rings. The ownership of a DMA buffer is a session: a submission consumes the permission of the buffer, and its completion gives the permission back. A use after submit, or a lost completion, then fails to compile. Design the family only when its first two real users exist: the RDMA data path of CNTP and the pushbuffer of the Mimic runtime.
- **A daemon with no glibc.** Each access to the kernel goes through a door, and no door holds glibc state. A separate daemon, for example the Keeper, can then have its own `_start`, threads from `clone`, its own TLS set-up, its own static constructors, `__cxa_guard_*`, `__cxa_atexit` and `__dso_handle`, as a second implementation of the doors. The rest of the base does not change. The vessel can never use this, because it shares a process with Python and PyTorch (D13).

---

## 18. The utils system

**Status.** This section is a design of 2026-10-03, at main 5317d9bcd. The tree holds no part of it at this time. The owner approved the design on 2026-10-03 (D15). The owner asked for two things. The first request was "i want it ast based and plugin based and typed and systematic". The second request was "my goal here is to refactor all utils scripts so it's like post optimal packaging with ast backing of all targets and 2-4 master scripts that go through them and scripts should be not junky ass but on the contrary elite tier engineering". The design has these properties:
- Each rule about C++ reads a syntax tree, and no module reads C++ as text.
- Each rule about code that the build compiles goes into the GCC plugin, which reads the tree after name lookup and templates.
- Each rule has an identity and a kind, and each finding is one typed record. Each ledger has one schema that one module reads and writes.
- One registry lists the rules, one place holds the ledgers, one runner operates each engine, and one report format gives the findings.
- All the Python of `utils/` is one package, and three master commands start it. No module of the package is a command, and no shell script stays.
- Each input format has one structured reader, and no module reads a structured input as text.
- Each rule of the engineering bar is a check that can fail, and the check runs in the local guard run.

The scope is all of `utils/`: `utils/scripts/`, `utils/toolchain/` and the Python drivers and data of `utils/tools/`. The C++ tools of `utils/tools/` stay C++. The scope also holds nine files of `test/` and `bench/` that are parts of the tools. They are the fixture driver, its store and its test, the test of the build launcher and the contract test of header units. They are also the attack test of use-after-move with its plant, the AFL++ driver and the bench driver. The design comes from an inventory of each of these files (sections 18.19 and 18.20). Stage 1B builds the system (section 8.6a), and this section is its design reference. Section 8.6a gives the units, their order and the end state that the S1 gate checks.

### 18.1 The tree today

Measured on 2026-10-03 in a work tree at 5317d9bcd, with the Debug preset, 96 jobs and a warm tree cache:
- `utils/` holds 263 files. `utils/scripts/` holds 159 of them in one directory: 65 check scripts, 38 other Python and shell files, and 56 data files. `utils/tools/` holds 99 files. The quarantine plugin and its test have 69, the session oracle has 16 and the mutation tool has 4. The hardware probe and the row-hash witness have 8, and the LSP plugin has 2. `utils/toolchain/` holds 5.
- The scope holds 114 Python files with 83,903 lines and 16 shell scripts with 4,678 lines. `utils/` has 108 of the Python files (78,496 lines) and 14 of the shell scripts (4,413 lines).
- 108 files of the scope are commands. They are 89 Python files of `utils/` with a `__main__` block, 12 shell scripts of `utils/`, 5 Python helpers and 2 shell drivers. Shell scripts source `repo_root.sh` and `tools_root.sh`, and these two files are not commands.
- The 56 data files use 18 row formats and 8 separators. With the data files of `utils/toolchain/` and `utils/tools/`, the scope has 22 formats. Only `quarantine-ledger.txt` has a format line. Fifteen ledgers and allowlists hold no row. Code names two allowlists that are not in the tree.
- ctest holds 226 tests with the label `ci_guard`. One call of `ctest -L ci_guard -j 96` used 9.0 s of wall time and 543 CPU-s (443 s user, 100 s system). The sum of the wall times of the 226 tests is 299 s.
- After an edit of a base header (section 7.10, d6ef48653), the guard scripts used 7.7 s and 1,105 CPU-s. The fill of the preprocessed store used 535 CPU-s of that time, for four guards.
- The CI job `guards` names 35 scripts again by hand, each with its self-test. The local guard run is the only gate (D11), so no landing waits for this list.
- `guard-engines.txt` gives the engine of 114 scripts. Its check refuses a regex on the text of a tsast node, and a text tool on C++ in a shell script. It does not find a read of raw bytes, a read through a variable or a read of preprocessed text.

Fifteen scripts read C++ as text in a small part at this time. Each read is a prefilter, a key or a scan of a macro body that the kit cannot parse. Section 18.4 gives the text reads of the other formats.

| Script | The text read |
|---|---|
| `check-banned-calls.py`, `check-fullness-guard.py`, `check-syscall-capability.py` | The key of an allowlist row is the text of a source line |
| `check-ctx-init-door.py`, `check-quarantine-regions.py`, `check-ct-disassembly.py`, `check-federation-admission.py` | A byte search in each file selects the files to parse |
| `check-splits-orphan.py`, `check-row-contains-discipline.py` | A regex on the raw bytes selects the files to parse |
| `check-federation-admission.py`, `check-start-lifetime.py` | A substring test on preprocessed text |
| `check-syscall-grant-coverage.py`, `check-escape-doors.py` | String methods on the text of a node, through a variable |
| `check-detsafe-ledger.py` | The script puts the lexemes of a macro value together as text, and a prefix test reads the text |
| `check-header-checks.py` (`--fix` only) | A regex on the raw bytes of each tracked file |
| `check-no-coordination-refs.py` | A regex on each line of the files of `tsast.UNPARSEABLE` |

### 18.2 The package and the masters

The utils system is one Python package and three commands. The package is `assay`, in `utils/assay/` (decision 14). Each file of the package is a module, and no module is a command. The three commands are the masters `guard`, `loop` and `setup` in `utils/bin/`. Each other file of `utils/` is data, a plant, or a source file of a C++ tool.

```
utils/
  bin/              guard, loop and setup: the only commands (no suffix, mode 755)
  assay/            the package (each file has mode 644)
    core/           the shared modules (section 18.15)
    readers/        one reader for each input format (section 18.4)
    guard/          the runner, explain, warnings, selftest, and engines/{plugin,tsast,elf,buildsys,meta}/
    loop/           the step meters, fixture/, build, affected, gauge, history, pgo, bench, fuzz, derive/
    setup/          install, pins, gcc, oracle
    oracle/         the session oracle
    mutation/       the gate mutants
    tests/          one test module for each module with logic, in the same shape of tree
  registry.txt      the rules, the jobs and the permitted paths (section 18.5)
  pins.txt          the pinned tools (section 18.6)
  rule-table.txt    the input of the plugin (today utils/scripts/layer-rules.txt)
  admitted-audit.txt
  ledgers/          RULE.txt for each rule with rows
  plants/           RULE/ with the plants and the controls of each rule
  tools/            the C++ tools: the quarantine plugin, the hardware probe, dump_row_hashes, the LSP plugin
  toolchain/        gcc/patches/
```

`utils/scripts/` goes in Stage 1B. The pattern `build/` of `.gitignore` matches a directory with that name at each depth, so no directory of `utils/` has the name `build` (decision 12). The engine directory of the build rules is `buildsys`.

**A master.** Each master is a short Python file. It does these steps, and no other step:
1. It refuses a Python that is older than its pin (decision 17), with a message that names the pin.
2. It sets `sys.pycache_prefix` to the directory `pycache` in the cache root of `assay.core.cache`. So no `__pycache__` directory goes into the tree. The fixture driver does this today (`test/neg_compile_driver.py`).
3. It puts `utils/` at the front of `sys.path`.
4. It finds the subcommand in a table of names and module paths. It imports only that module, and it calls the function `main` of that module.

The table holds no import, so the start of a subcommand does only its own imports. Only a subcommand that a person starts parses its arguments with `argparse`, through `assay.core.cli`. A step meter reads its arguments without `argparse`, because the import of `argparse` costs 6.3 ms (measured with `-X importtime`).

| Master | Subcommand | Job | Today |
|---|---|---|---|
| `guard` | `check` | Give the findings of the rules, and compare them with the ledgers | The 65 check scripts, the two audits, `check_plugin.py` |
| | `witness` | Compile or parse each plant. A plant must give its finding, and its control must give no finding | The self-tests of the check scripts |
| | `write` | Write a ledger with the checks of its policy | The `--write` mode of each check script |
| | `list` | Print the registry. With `--path`, print the ledger rows of each rule for one file | None |
| | `warnings` | Print the warnings directory of a build | `report-check-warnings.py` |
| | `explain` | Print the rule, its CLAUDE.md section, its action, its policy, its plants and its inputs | None |
| | `selftest` | Run the test modules of one group of the package | The `--self-test` mode of each script |
| `loop` | `build` | Build the tree, and append one line to the loop history | `cmake --build`, `loop_history.py` |
| | `test` | Build, and then run each test with an input that changed | `run-affected-tests.py` |
| | `gauge` | Measure the edit loop in a clone, warm or cold, and measure a sample of units | `edit-loop-gauge.py`, `build-gauge.sh`, `tu-sample.py` |
| | `history` | Print the loop history | `loop_history.py` |
| | `pgo` | Collect the profile of the tier of the host | `pgo-bootstrap.sh` |
| | `derive` | Write each generated file of the tree. With `--check`, compare each one with its generator | `refresh-derived.sh`, `gen-mint-inventory.py`, `gen-aten-op-table.py`, the write modes of three guards, `session-oracle.sh --regenerate` |
| | `bench` | Build the benches, give them their capabilities, and run them on an isolated core | `bench/run-bench.sh` |
| | `fuzz` | Run one AFL++ campaign on one harness | `test/fuzz/run-afl.sh` |
| | `step` | The meter of each compile and each link. CMake starts it | `build-launcher.py` |
| | `meter-compiler` | The meter of the compiler run of a ccache miss. ccache starts it | `compile-meter.py` |
| | `test-step` | The meter of each test. ctest starts it | `test-launcher.py` |
| | `fixture` | The driver of one negative fixture. ctest starts it | `test/neg_compile_driver.py` |
| | `stamps` | Write the stamps of the plugin at configure time. CMake starts it | `quarantine_stamps.py` |
| `setup` | `install` | Install each pinned tool, or the named tools | The three installers, `session_oracle/toolchain.py` |
| | `check` | Make sure that each installed tool agrees with its pin | `cmake_pin.py`, the `--check` mode of each installer |
| | `gcc` | Build the patched GCC from the pinned base commit and the patches | `utils/toolchain/gcc/build.sh` |
| | `list` | Print the pin table | None |

The help text of a master lists only the subcommands that a person starts. It does not list the five hidden subcommands that CMake, ccache and ctest start.

**The start cost.** CMake starts `loop step` for each compile and each link, and ctest starts `loop test-step` for each test. So the start cost of `loop` is a cost of each step. Today `build-launcher.py` adds approximately 10 ms to each step (§V). A measurement with a scratch master, outside the tree, gives the cost of the dispatch. The scratch master has the design of this section, with copies of `build-launcher.py` and `cost_meter.py` as modules of a package. Each command ran 20 times, in rounds, after 3 warm runs. The host load was 17.6:

| Command | p50 | max |
|---|---|---|
| `/usr/bin/true -o X -c Y` with no launcher | 0.45 ms | 0.55 ms |
| `python3 -S -c pass` | 5.81 ms | 6.74 ms |
| `python3 -c pass` | 10.27 ms | 11.43 ms |
| `build-launcher.py true`, with no `-o`, so it starts the command at once | 8.42 ms | 10.35 ms |
| `build-launcher.py true -o X -c Y`, which measures the command and writes the record | 11.51 ms | 12.60 ms |
| `loop step true -o X -c Y` with lazy dispatch and `-S` | 10.33 ms | 11.39 ms |
| The same with `-X pycache_prefix` | 10.31 ms | 11.18 ms |
| The same with no `-S` | 13.02 ms | 40.35 ms |
| A master that imports each module first and parses with `argparse`, with `-S` | 57.92 ms | 91.16 ms |

A first round of 20 runs gave the same p50 values, with a difference of 0.6 ms or less. The lazy master costs 1.2 ms less than the launcher of today. Python compiles the script that it starts on each start, and the master script is short. Python reads an imported module from its compiled form. The eager master costs 46 ms more on each step. `-X importtime` gives 29 imported modules for the launcher of today, 33 for the lazy master and 171 for the eager master. The 171 modules use 52.3 ms in their own import code. In that process, most of that time is in `tsast` (18.4 ms) and `build_census` (14.3 ms), because the modules share their imports.

So the step meters stay in `loop`, and no fourth master is necessary (decision 18).

**The naming rule.** A module has a name of lowercase words with underscores, and mode 644. A master has a name of one lowercase word with no suffix, a shebang and mode 755. No other file of `utils/` has a shebang or mode 755. A data file has a name of lowercase words with hyphens. The part layout of the meta rule `registry` holds the naming rule and the layout of this section, with the `path` rows of the registry (sections 18.5 and 18.13). Today 80 Python files have a shebang and no mode 755 (rule EXE001 of `ruff`).

### 18.3 The engines

Each rule uses one engine only. The first design had four engines. The inventory shows a fifth engine, `meta`. Nine checks read no C++ and no build output. They read the registry, the ledgers, the scripts, git history and prose. So the design has five engines:

| Engine | Input | Rules about | Reader today | Reader after |
|---|---|---|---|---|
| `plugin` | The semantic tree of each unit that a build compiles, after macro expansion, name lookup and template instantiation | C++ that a build compiles | `utils/tools/quarantine/`, and the section `.crucible.quarantine` of each object | The plugin, and `assay.readers.sections` |
| `tsast` | The parse tree of the fork kit | C++ that no compile keeps. Examples are arms that no build selects, files that no build compiles, macro bodies, the constructs that GCC folds, and the source layout | `utils/scripts/tsast.py` | `assay.readers.tsast` |
| `elf` | Objects and static libraries | Symbols, sections and machine code | `utils/scripts/elf_file.py`, `nm`, `objdump` | `assay.readers.elf` and `assay.readers.objdump` |
| `build` | The compile database, `build.ninja`, the Ninja logs, the launcher records, the CMake file API, the ctest list and the records of the tests | The build graph, the build configuration and the costs | `ninja_files.py`, `build_census.py`, `cost_meter.py` | `assay.readers.census`, built on the readers of section 18.4 |
| `meta` | The Python `ast` module, ast-grep with the bash grammar, git history, the registry, the ledgers and prose | The utils system and the repository, and the engineering bar | `check-guard-engines.py`, `check-budget-history.py` | `assay.readers.python`, `assay.readers.shell`, `assay.core.git`, `assay.core.rows` |

The directory of an engine in the package has the name of the engine. The one exception is the engine `build`, with the directory `buildsys` (section 18.2).

A rule with parts on two engines becomes two rules. For example, the contract rule is a plugin rule for each compiled file. The parse-tree rule `contract-form` holds the remaining part: a specifier in an arm or a file that no build compiles.

These facts limit the plugin. The inventory and the plants of section 18.18 measured them:
- GCC folds four constructs while it parses, and it keeps no tree of them (`quarantine.cpp:184-188`). They are a `static_assert` with a condition that is not dependent, a constant initializer, a splice and an expansion statement. So the header checks, the walk units and the derived pins stay on the parse tree.
- A `reinterpret_cast` keeps its mark `REINTERPRET_CAST_P` only on a `NOP_EXPR` from one pointer type to another. A probe plugin found no mark on five shapes. Two shapes are casts between a pointer and an integer, one in each direction. The other three are a cast to a reference, a cast to the same type and a cast in a macro. The ban of the keyword is a fact of the spelling, so it stays on the parse tree.
- A splice of a member that is not dependent becomes a `COMPONENT_REF` with no mark, because GCC sets `COMPONENT_REF_SPLICE_P` only on a dependent reference.
- GCC reads no comment.
- No configuration of the ledger sets `TORCH_DIR`, so no plugin rule reads `vessel/torch`.
- The plugin reads the pattern of a template, and not the body of an instantiation (`quarantine.cpp:98-109`). Plant P14 shows what the walk of a pattern finds. The walk finds each qualified name that the definition resolves, also with dependent arguments. Examples are a call, a template argument, a nested name, a member type, a cast target, a partial specialization and a `contract_assert`. It does not find three shapes. These are a member call on an object of a dependent type, a call that only argument-dependent lookup resolves, and an explicit instantiation. No kind of today reads an operator.
- At this time the plugin walks only the quarantined files and the files of the language. A rule with a base scope walks a base header only in the sentinel unit of that header. The next paragraph gives the mark of a sentinel unit.

**The sentinel unit.** `test/layer/CMakeLists.txt` gives one unit of the target `all` this plugin argument for each header of `include/foundation/` and `include/fixy/`:

```
-fplugin-arg-crucible_quarantine-sentinel=include/fixy/Refined.h
```

When the header has a check file, the unit is that check file. When the header has no check file, the unit is its one-line unit. CMake appends the argument to the property `COMPILE_OPTIONS` of that source file, beside the operation limit of line 267. The one-line units of `layer_alone` and the walk units of `walks_across_headers.cpp` do not get it. So each header has one sentinel unit. The value is a path relative to the source root, so each work tree gives ccache the same text.

A rule with a base scope gives a finding in a base file only in the unit with an argument that names that file. In that unit, the rule reads the header as the unit parses it:
- Each body of a function that is not a template
- Each pattern of a template
- The instantiations that this one unit makes.

A one-line unit makes only the instantiations that the header itself makes, and a check file adds those of its checks. The rule does not read an instantiation that a different unit makes. It does not find a name that only an instantiation resolves (P2, and cases C4 and C5 of P14). Each base header then costs one walk in each build, and not one walk in each includer.

### 18.4 A structured reader for each input

Each input of a module has one format, and each format has one reader in `assay/readers/` or `assay/core/`. No other module reads an input of that format, and no module reads a structured input as text. Prose is the one input that is text by nature: a comment, a docstring, a Markdown page and a commit message.

| Input | Its one reader | How the reader reads it |
|---|---|---|
| C++ | The plugin for code that a build compiles, and `assay.readers.tsast` for each other read | The semantic tree of GCC, or the parse tree of the fork kit (section 18.3) |
| Python | `assay.readers.python` | The `ast` module, and `tokenize` for the comments |
| Shell | `assay.readers.shell` | ast-grep with the bash grammar, as JSON |
| CMake | `assay.readers.cmake` | The replies of the CMake file API in the build directory: `codemodel-v2`, `cache-v2`, `cmakeFiles-v1` and `toolchains-v1`. `CMakePresets.json` through `json` |
| Ninja | `assay.readers.ninja` | `build.ninja` through the lexical syntax of the Ninja manual, and `.ninja_log` through its version line and its fields. `ninja -t deps` gives the dependency log |
| The compile database | `assay.readers.compdb` | `json`, and `shlex.split` for the field `command` |
| ctest | `assay.readers.ctest` | `ctest --show-only=json-v1` for the list of tests, and the records of `loop test-step` for the cost of each test |
| Compiler diagnostics | `assay.readers.sarif` | The SARIF file that `-fdiagnostics-add-output=sarif:file=PATH` writes. GCC 16.2.1 writes it, with the level, the file, the line, the column and the message of each result (measured on 2026-10-03) |
| The include trace of `-H` | `assay.readers.include_trace` | The documented lines of `-H`, where `!` marks a precompiled header that the compile used. A depfile does not name the `.gch` file (measured with GCC 16.2.1), so the trace stays |
| ELF objects and archives | `assay.readers.elf` | The ELF and `ar` formats: the sections, `.symtab`, `.dynsym` with the versions, and the `.rela` sections. One batch call of `c++filt` |
| Machine code | `assay.readers.objdump` | The line grammar of `objdump -d -r`. A disassembly has no structured form, so this one reader holds the grammar and its tests |
| The findings of the plugin | `assay.readers.sections` | The section `.crucible.quarantine` of each object, through the ELF reader |
| Cost records | `assay.core.record` | The record format of `cost_meter.py` today |
| The rule table, the registry, the ledgers, the pins and the audit table | `assay.core.rows` and one schema module for each | The rows grammar (section 18.6) |
| git | `assay.core.git` | `git` with a constant format argument, through `assay.core.proc`, with no `GIT_DIR` and no other repository variable in its environment |
| `/proc`, `wait4` and the hardware counters | `assay.core.host` and `assay.core.meter` | `/proc/loadavg` and `/proc/meminfo` by field, `os.wait4`, and `perf_event_open` through `ctypes` |
| The statistics log of ccache | `assay.core.meter` | One counter name on each line, as ccache documents it |
| The reports of mypy, ruff and clang-format | `assay.readers.tool_reports` | The JSON of `mypy --output json` and of `ruff check --output-format json`, and the XML of `clang-format --output-replacements-xml` through `xml.etree` |

These text reads of today go. The unit of Stage 1B that brings the reader also removes the read (section 8.6a):
1. The C++ text reads of the 15 scripts of section 18.1.
2. `CMakeCache.txt` as lines or by regex, in 16 files. An example is `test/neg_compile_store.py:415-416`. The file API reply `cache-v2` replaces them.
3. The ctest log by regex, in `check-test-time.py:128-131`, and the output of ctest by regex, in `build-gauge.sh:278`. The records of `loop test-step` replace them. CMake gives no launcher to a script test (§XV, rule 11). So the registration of each script test puts `loop test-step` in front of its command.
4. The output of `nm` by regex, in `check-libstdcxx-symbols.py:177`, and the text of `nm`, `readelf` and `objdump` in `check-no-throw-no-rtti.sh`. The ELF reader replaces them.
5. The output of `ninja -t deps` by regex, in `check-compile-cost.py:177`, and the second parser of the Ninja log in `build-gauge.sh`. `assay.readers.ninja` replaces them.
6. Compiler diagnostics by regex: `test/neg_compile_store.py:352-406` (the ANSI codes, the gutter, the elision, the error head and the context), `check-header-constexpr-ops.py:92`, `utils/tools/quarantine/test/check_plugin.py:266`, `utils/tools/mutation/run.py:427-438` and `test/contract_header_unit_test.py:72`. The SARIF reader replaces them. The pattern of a negative fixture stays a regular expression. It reads the message of one SARIF result, with no strip of colors and gutters.
7. The CMake lexer of `check-no-unity-pch.py`. The reply `codemodel-v2` gives the sources and the precompiled headers of each target.
8. The line markers of preprocessed text, in `preprocessed.py:273`. The store goes (section 18.17).
9. A report line of a check that a self-test reads again by regex: `check_report.py:82`, `check-start-lifetime.py:802-815`, `check-witness-roster.py:843-849` and `check-syscall-grant-coverage.py:427`. A test compares `Finding` records in the process.
10. The plugin section, which `check_plugin.py` parses again. `assay.readers.sections` replaces it.
11. `requirements.txt` by regex, in `cmake_pin.py:79`. A row of `utils/pins.txt` replaces it.

The regular expressions of `check-core-shapes.py:209-214` and `check-ct-disassembly.py:214-241` go into the one objdump reader, because a disassembly has no structured form. The scope has 145 regex calls in 52 files today.

### 18.5 The registry

`utils/registry.txt` is the one list of the rules. It comes from `utils/scripts/guard-engines.txt`, and it gets the 37 rows of `utils/scripts/budgets.txt`. The rule table stays a separate file, because it is an input of each compile (decision 2 of section 18.23). The registry uses the rows grammar of section 18.6. It has a head comment, a format line and three row kinds:

```
format 1
rule ID engine=ENGINE scope=SCOPE[,SCOPE...] ledger=POLICY [kinds=KIND[,KIND...]] [warn=N error=N unit=UNIT] [inputs=PATH[,PATH...]] doc=REF | WHAT THE RULE REFUSES | THE ACTION
job ENTITY module=MODULE[,MODULE...] | WHY THE JOB HAS ONE OWNER
path GLOB | WHAT THE FILES ARE
```

| Field | Values | Rule |
|---|---|---|
| `ID` | Lowercase words with hyphens, as `CHECK_NAME` of `check_report.py` gives them | Unique. It is also the name of the finding, of the ledger file, of the plants directory, of the warnings file and of the ctest test |
| `engine` | `plugin`, `tsast`, `elf`, `build` or `meta` | One engine for each rule |
| `scope` | A path relative to the source root (a path with a final `/` is a directory), or one of the words `base`, `quarantined`, `tracked`, `builddir` and `package` | The files that the rule reads |
| `ledger` | `none`, `count`, `admit`, `max` or `baseline` | Section 18.8 gives each policy |
| `kinds` | The finding kinds of the rule | A plugin rule names each kind that the plugin gives for it |
| `warn`, `error`, `unit` | The thresholds of a measured rule | The rows of `budgets.txt` move here |
| `inputs` | Paths of the source tree or the build tree | Only the inputs that the import graph of the rule module does not show (section 18.16) |
| `doc` | The CLAUDE.md section that states the rule, for example `XV.4` or `XXII.R11` | The part shape of the meta rule `registry` finds the section |
| The first cell | One sentence | What the rule refuses. The message of a finding starts with it |
| The second cell | One sentence in the imperative form | The default action. The message of a finding has it at its end, and `guard explain` prints it |

A `job` row names a Python entity, such as `subprocess` or `argparse`, and the one module that can use it. The row kind `owner` of the rule table is a different row: it names a C++ entity for the plugin (section 18.10). The `script` rows of `guard-engines.txt` go. The import graph of the package gives the reader of each module.

A `path` row gives a glob of tracked paths and what those files are. The part layout of the meta rule `registry` reads the `path` rows (section 18.13). During Stage 1B, `path` rows also hold the paths of today. The cell of such a row names the unit that deletes the files, and that unit removes the row.

These rows are examples:

```
rule quarantine engine=plugin scope=quarantined ledger=count kinds=std-entity,std-object,raw-pointer-object,c-array-object,c-library-call,opted-out,region doc=XXII.R6 | a quarantined file holds only objects of base types | use the base type of the family of section 11, or ask the extension lane for it
rule owner-use engine=plugin scope=base,quarantined ledger=count doc=XXI | code uses an entity with owner rows only at its owner sites | call the door of the entity
rule banned engine=tsast scope=include/,vessel/ ledger=admit doc=III | no code uses the keyword reinterpret_cast | use std::bit_cast with the limits of section IV, or std::start_lifetime_as
job subprocess module=assay.core.proc | each process start has a timeout and a known environment
path utils/assay/**/*.py | the modules of the package
path utils/assay/guard/legacy/*.py | the legacy modules. Unit 1B.9 removes this row
```

`assay.core.registry` reads the registry for Python. `cmake/Guards.cmake` reads it for CMake with `file(STRINGS)` and a match of each key, as `cmake/Budgets.cmake` reads `budgets.txt` today. The plugin does not read the registry. The data of the plugin rules stays in the rule table, which gets the row kinds of section 18.10. The plugin witness makes sure that the kinds of the plugin are the kinds of the plugin rows of the registry.

### 18.6 The rows grammar

The rule table, the registry, the ledgers, the pin table and the audit table use one grammar. `assay.core.rows` is its reader and its writer in Python. `utils/tools/quarantine/rows.h` is its reader in C++, and it includes no GCC header, so a small host program can include it too. Each file has a schema module that tells what its rows hold: `assay.core.registry`, `assay.core.ledger`, `assay.core.ruletable`, `assay.core.pins` and `assay.guard.engines.meta.admitted_audit`.

```
file    = { line "\n" }
line    = blank | comment | row
blank   = { " " | "\t" }
comment = { " " | "\t" } "#" { each character except "\n" }
row     = head { " | " cell }
head    = word { ( " " | "\t" ) { " " | "\t" } word }
word    = one or more characters except " ", "\t", "|" and "\n"
cell    = { each character except "\n" and a "|" with no "\" before it }
```

These rules apply to each file:
- Only the space and the horizontal tab divide words. Python `str.split()` also divides at `\v`, `\f` and the Unicode spaces, so the Python reader does not use it (defect of section 18.21).
- A word of the form `KEY=VALUE` is a field. A schema gives the keys that it accepts, and a second field with the same key is an error.
- In a cell, `\|` is a bar and `\\` is a backslash. The cell `-` is empty. A writer escapes each bar and each backslash.
- A number is a run of decimal digits. Each reader refuses a number that is too large for its type. So the C++ reader adds no digit to an `int` with no overflow check (defect of section 18.21).
- A schema gives the key of each row kind. Each reader refuses a second row with the same key, so the order of the rows cannot change a result (defect of section 18.21).
- A writer keeps the head comment, sorts the rows by their keys, and writes the file only when its bytes change.

The meta rule `rows-readers` makes sure that the two readers agree. The Python reader reads a corpus of valid and invalid files in `utils/plants/rows-readers/`. A host program that includes `rows.h` reads the same corpus, and it prints each parsed row as JSON. The two outputs must be equal. Each invalid file must fail in the two readers, at the same line.

The rule table keeps its row kinds and its words, and it gets the row kinds of section 18.10. Its rows obey the grammar today: no row holds a backslash, a tab or a bar outside ` | ` (measured on 2026-10-03). So the change of its reader changes no row. The stamp of the plugin changes with the source of the plugin, so each object compiles again one time (sync point 1B.7 of section 19).

The pin table has one row kind:

```
pin NAME via=pip|archive|git version=V source=URL sha256=DIGEST|commit=SHA [platform=P] | WHAT IT IS
```

It holds a row for each pinned download:
- The fork kit of tree-sitter: the CLI and the grammar
- ast-grep
- cmake and ctest
- mypy and ruff (decision 16)
- clang-format: the binary of the wheel of clang-format on PyPI, for each platform of the table, with `via=archive`. The rule `clang-format` runs only this binary, and with no install of the pin the rule does not apply
- The base commit of GCC (today `utils/toolchain/gcc/BASE`)
- The toolchain of the session oracle: Java, sbt, mCRL2, the sources of Sprout and the Rocq developments
- The version of Python (decision 17).

`cmake/CMakePin.cmake` reads the `cmake` row with `file(STRINGS)`. For a `via=pip` row, `setup install` writes a requirements file with `--hash` into a temporary directory, and it removes the directory after the install.

### 18.7 The finding record

Each engine gives findings of one type: `Finding` of `assay.core.report`, which is `check_report.py` with three more fields. It is a frozen dataclass with slots. Its fields sort in the sequence of the table, so a sort gives each output one sequence.

| Field | Type | Meaning |
|---|---|---|
| `rule` | A rule ID | The row of the registry |
| `kind` | A kind of the rule, or `-` | A part of a rule, such as `std-object` of the rule `quarantine` |
| `level` | `error` or `warning` | An error fails the test, and a warning does not |
| `path` | A path relative to the source root, or `@build/PATH` | The file of the finding |
| `line`, `column` | Integers, and 0 when no place applies | The place of the finding |
| `entity` | Text | What the rule refuses, with no line number. The ledger key holds it |
| `message` | Text | What is incorrect, and then the action. The two cells of the registry row give the default parts |

The text form is the line of `check_report.py` with the column, the kind and the entity: `PATH:LINE:COLUMN: LEVEL: [RULE/KIND] ENTITY: MESSAGE`. The warnings directory and the GitHub annotations do not change. A self-test compares records, and it does not read the text form again (item 9 of section 18.4).

The plugin writes the same fields into the section `.crucible.quarantine`. The head line becomes `# crucible-guard 2 stamp=TEXT`, and each line becomes `RULE KIND PATH:LINE:COLUMN ENTITY`. The kinds of the plugin get names with hyphens, such as `std-object`, so that each identity has one spelling (decision 3). The stamp of the plugin changes with its source, so each object compiles again, and no object of the previous format stays.

### 18.8 The ledger schema and its module

`utils/ledgers/RULE.txt` is the ledger of the rule `RULE`. A rule with the policy `none` has no ledger. A ledger with no rows has no file, so the fifteen empty files of today go. Each ledger uses the rows grammar of section 18.6, and it has this form:

```
# The head comment.  The module keeps it on each write.
format 1
rule RULE
configuration NAME KEY VALUE
PATH | KIND | VALUE | ENTITY | REASON
@NAME | PATH | KIND | DELTA | ENTITY | REASON
```

- The cells obey section 18.6: the separator is ` | `, `\|` and `\\` escape a bar and a backslash, and `-` is an empty cell.
- The key of a row is `PATH`, `KIND` and `ENTITY`, and a key has one row.
- A `configuration` row is necessary only for a rule with findings that change with the build, such as a plugin rule or an artifact rule. The first configuration is the base. A row that starts with `@NAME` holds the difference of the configuration `NAME` from the base, as the quarantine ledger does today.
- `VALUE` has the type of the policy of the rule:

| Policy | `VALUE` | A finding fails when | A row fails when | Today's files |
|---|---|---|---|---|
| `count` | A positive integer | Its count is more than its row, or its file has no row | Its count is less than the row, until `--write` writes it again | quarantine, header-checks, brand-drain, heavy-includes, padded-lists, open-gate-reads, use-after-move, proof-routes, detail-namespace, the two context doors |
| `admit` | The word `admit` | No row admits it | No finding matches it | contract-form, raw-array-member, process-wide, unchecked-access, mint-pattern, mint-hs14-floor, no-throw-no-rtti, escape-doors, syscall-capability, fullness-guard, derived-pins |
| `max` | A number in the unit of the rule | Its value is more than the error threshold and more than its row | The value of its item is not more than the error threshold | the eight cost ledgers of `check-compile-cost.py`, test-time, test-memory, the three fixture ledgers |
| `baseline` | A number, or `BYTES/READERS` | The sum is outside the levels of the rule | The sum falls more than the warning level, until `--write` writes it again | parse-total, instruction-total, header-fanout, contracts-baseline |

A row of the policy `admit` or `max` must have a reason. A row of the policy `count` can have one.

The module `assay.core.ledger` is the one reader and writer of the ledgers. It gets the ledger code of `check-quarantine-ratchet.py`: the configurations, the difference rows, the moves of `git mv`, `--write`, `--raise`, `--admit` and `--import` of a CI log. Its functions are `read`, `write`, `compare`, which gives findings, and `history`, which gives the rows of a ledger at each commit (section 18.15).

The admitted items of a rule are its ledger rows, with their reasons. So no rule keeps an allowlist file, and the rule table holds no allowlist rows. The comment markers of today become ledger rows too: `PERMISSION-STORAGE-OK`, `ROW-CONTAINS-OK`, `REFINED-PRE-OK`, `FULLNESS-OK`, `LOCK-FREE-OK`, `MINT-PATTERN-OK`, `SYSCALL-CAP-OK` and the `NO-*-OK` markers of the banned calls. Four of these markers have no use in the tree at this time.

**The entity of a rule that admits one site.** The key of a row holds no line. So a change that only moves code changes no row. For a rule that admits one expression or one declaration, `ENTITY` names the site by the function that holds it and by its shape. `FUNCTION` is the qualified name of that function with its parameter types. An operand has the spelling of the source, with no white space:

| Rule | Engine | `ENTITY` | Example | Today's key |
|---|---|---|---|---|
| `refined-pre-subsumption` | plugin | `FUNCTION: PARAMETER: decide::NAME` | No site today | A marker beside the code |
| `proof-routes` | plugin | `ROUTE: KEY`, where `KEY` is the union, the proof type or the specialized template | `union: EitherPointer` | `path:rule:key`, as today |
| `escape-doors` | plugin | The qualified name and the parameter types of the function, with its qualifiers | `fixy::concurrent::Topology::instance()` | `path:name` |
| `mint` | plugin | The qualified name and the parameter types of the mint. The missing property is the kind, such as `mint-not-noexcept` | `crucible::Vigil::mint_producer_context()` | `path:mint:property`, and markers |
| `process-wide` | plugin | The qualified name of the variable. A static local is `FUNCTION::NAME` | `crucible::ledger::probes::cache_tier_detail::g_sweep_memo` | The same qualified name |
| `use-after-move` | tsast | `FUNCTION: LOCAL` | `(anonymous namespace)::test_owning_payload(): source` | `path:function:local` |

When one function has two sites with the same entity, the row has the value 2. A third site fails the rule. When one site goes, the count is less than its row, and the rule fails until `write` decreases the row. Two sites of different shapes in one function are two rows. The ledger cannot tell which of two equal sites a row means. This is not a problem, because the two sites have the same shape and the same reason.

A marker sits beside its code, and a row does not. The design accepts this difference for these reasons:
- The plugin reads no comment (section 18.3), so a marker cannot admit a site of a plugin rule.
- A row of the policy `count` or `admit` fails when its site goes, so an admission cannot stay after its code. A marker stays when its code changes.
- Today each guard has its own marker syntax and reason rule. Three of the reason rules do not agree, and four markers have no use. One ledger has one reason rule, and the part ledgers of the meta rule `registry` holds it.

A reader at the code does not see the admission. `guard list --path FILE` prints the rows of each rule for one file. A code comment can give the intent, but it admits no site.

The other data files of `utils/scripts/` and `utils/` go to these places, in the units of section 8.6a:

| Today | After | Unit |
|---|---|---|
| `quarantine-ledger.txt`, `quarantine-region-ledger.txt` | `utils/ledgers/quarantine.txt`, with the kind `region` for the regions | 1B.3, and 1B.7 for the kind `region` |
| The 19 allowlists | The rows of the ledgers of their rules | 1B.3 |
| The 23 other ledgers | `utils/ledgers/RULE.txt`, one file for each rule with rows | 1B.3 |
| `budgets.txt`, `guard-engines.txt` | Rows of `utils/registry.txt`. The `script` rows go (section 18.5) | 1B.2 |
| `host-owner-roster.txt`, `witness-roster.txt` | Rows of the rule table (`home` and `witness`), because the plugin reads them | 1B.8b and 1B.8c |
| `header-costs.txt` | `heavy` rows of the rule table, which `loop derive header-costs` writes | 1B.8f |
| `layer-rules.txt`, `quarantine-admitted-audit.txt` | `utils/rule-table.txt` and `utils/admitted-audit.txt` | 1B.7 |
| `compile-first.txt` | `cmake/compile-first.txt`, beside `cmake/CompileFirst.cmake`, which reads it at configure time | 1B.3 |
| `fixy-only-paths.txt` | Deleted. Its directories are the constant `RUNTIME_DIRECTORIES` of the rule `banned`, and the kind `file-stream` stays (section 8.6a) | 1B.8g |
| `contracts-baseline.json` | none | 1B.3 |
| `utils/toolchain/cmake/requirements.txt`, `utils/toolchain/gcc/BASE` | Rows of `utils/pins.txt` | 1B.5 |
| `utils/tools/mutation/invalid-exemptions.txt` | `utils/assay/loop/evidence/admitted-gates.txt`, with the policy `admit` | 1B.3 |
| `utils/tools/mutation/survivors.txt` | Deleted, because no code reads it | 1B.5 |

The lists of `test/layer/` and `test/fixy/neg/witness-fixtures.json` stay where they are, because CMake reads them at configure time.

### 18.9 The guard master and its runners

The master `guard` operates each engine (section 18.2):

```
guard check    [--engine E]... [--rule ID]... [--part K/N] [--build-dir DIR] [--warnings-dir DIR]
guard witness  [--engine E]... [--rule ID]...
guard write    --rule ID [--raise | --admit | --import LOG] [--reason TEXT] [--build-dir DIR]
guard list     [--engine E] [--path FILE]
guard warnings BUILD_DIR
guard explain  RULE
guard selftest GROUP [--part K/N]
```

- `check` gives the findings of the rules and compares them with the ledgers.
- `witness` compiles or parses each plant of each rule. A plant must give its finding, and its control must give no finding.
- `write` writes a ledger through `assay.core.ledger`, with the checks of its policy.
- `list` prints the registry. With `--path`, it prints the ledger rows of each rule for one file.
- `warnings` prints the warnings directory of a build, which `report-check-warnings.py` does today.
- `explain` prints the registry row of a rule and the text of its CLAUDE.md section. It also prints the action, the policy, the ledger command, the plants and the derived inputs of the rule.
- `selftest` runs the test modules of one group of the package (section 18.14).

Each engine has one runner module in `assay/guard/engines/ENGINE/`, and the runner reads the input of the engine one time for the rules of one call:
- `plugin` reads the section of each object of `all`, as the ratchet does today. It divides the findings by rule and compares each rule with its ledger. Its witness compiles each plant in `ERROR` mode, as `check_plugin.py` does today.
- `tsast` loads from the tree cache the trees that the rules of the call read. One call for each rule is as fast as one call for many rules (section 18.16). The runner gives each rule trees that no rule changes.
- `elf` reads each archive and each object one time.
- `buildsys` reads the census of `assay.readers.census` one time.
- `meta` reads the registry, the ledgers, the modules of the package and the git history.

A rule is a function in a module of its engine directory. It gets the context of the engine as a parameter, and it gives findings. One module holds one family of rules. The module of the rule `ID` is `assay/guard/engines/ENGINE/ID.py`, with underscores in place of the hyphens, unless the module holds a family of rules. A family module holds a table of the rules that it gives findings for. The part modules of the meta rule `registry` matches the modules and these tables with the registry, and it finds one function for each rule.

**The legacy modules.** Unit 1B.1 moves each guard script of section 18.19 into `assay/guard/legacy/`, with underscores in its name and with no change of behavior. Unit 1B.5 adds the shell checks. A temporary table of 1B.1, and then the registry of 1B.2, give the legacy module of each rule of a script, so `guard check --rule ID` runs it. Each subsequent unit replaces legacy modules with rule modules through the parity protocol of section 18.22. The directory can only become smaller, and unit 1B.9 removes it. Section 8.6a gives the unit that deletes each legacy module.

### 18.10 The plugin rule families

Thirty of the 65 check scripts move into the plugin. They become six families. Each family is one mechanism in the plugin source, with row kinds in the rule table for its data. The source of each family is a file of its own in `utils/tools/quarantine/`, such as `owner.cpp`. The admitted items of each rule go to its ledger (section 18.8).

| Family | Hook and tree codes | Rule-table rows | Scripts that it replaces |
|---|---|---|---|
| Owner: a use of an entity only at its owner sites | The walk of named references at `PLUGIN_PRE_GENERICIZE` and `PLUGIN_FINISH_UNIT`, which `std_entity` uses today. A reference to a decl with a context chain that holds the gated namespace, class or function | `owner ENTITY PATH...` | `check-ctx-init-door.py`, `check-ctx-testing-boundary.py`, `check-detail-namespace.py`, `check-start-lifetime.py` (base files), `check-no-unchecked-access.py` (the qualified form of rule 1, rule 2, and rule 5 through the expansion), `check-syscall-capability.py` (the door of the affinity calls, and each claim as an admit row) |
| Home: a definition or a specialization of an entity only in its home | `PLUGIN_FINISH_UNIT`: `DECL_TEMPLATE_SPECIALIZATIONS`, `DECL_TEMPLATE_INSTANTIATIONS`, `CLASSTYPE_USE_TEMPLATE`, `COMPLETE_TYPE_P` and the file of `DECL_SOURCE_LOCATION` | `home ENTITY PATH...`, and `home-args TEMPLATE` for a specialization beside its arguments | `check-federation-admission.py`, `check-host-owners.py`, `check-splits-orphan.py`, `check-trait-injection.py`, `check-no-combine-ids-duplicate.py` |
| Declaration property | The class and namespace walk at `PLUGIN_FINISH_UNIT`, and `PLUGIN_PRE_GENERICIZE` for locals: `FIELD_DECL`, `VAR_DECL`, `FUNCTION_DECL`, `DECL_ATTRIBUTES`, `DECL_VISIBILITY`, the type sizes | None. Each rule is one predicate in the plugin | `check-no-raw-array-member.py`, `check-permission-storage.py`, `check-process-wide-state.py`, `check-escape-doors.py`, `check-mint-pattern.py`, `check-lifetime-twin.py`, `check-stub-discipline.py`, `check-padded-lists.py`, `check-witness-roster.py`, and rule 6 of `check-no-unchecked-access.py` |
| Expression shape | The body walk at `PLUGIN_PRE_GENERICIZE`, and the walk of template patterns at `PLUGIN_FINISH_UNIT`: `CALL_EXPR`, the cast codes, the node of a `contract_assert`, the statement codes | Data rows only, such as `cite PREDICATE NAME...` and `grant FILE ALIAS ENUM` | `check-refined-pre-subsumption.py`, `check-proof-routes.py`, `check-syscall-grant-coverage.py` |
| Include closure | The include callback of libcpp and `PLUGIN_INCLUDE_FILE`, which the include rules use today | `seal ROOT PREFIX`, `heavy HEADER` | `check-detsafe-ledger.py`, `check-heavy-includes.py` (quarantined headers), `check-layer-boundary.py` (names) |
| Unit fact | `PLUGIN_FINISH_UNIT` and `DECL_FUNCTION_SPECIFIC_OPTIMIZATION`. An error in each mode, as the contract rule is | None | `check-contract-semantic.py`, `check-no-ffast-math.py` (attributes and pragmas) |

**What a family reads in a template.** Most base code is templates, and no unit instantiates most templates of a base header. So a family must hold its rule on the pattern of each template. Plant P14 of section 18.18 shows what the walk of a pattern finds today, in the sentinel unit of a base header:
- Owner: each rule keys on a qualified name or a type. Cases C1 to C3 and C6 show that the walk finds such a name, also with dependent arguments. The walk does not find a member call on an object of a dependent type (C4). It does not find an unqualified call that only argument-dependent lookup resolves (C5). The scripts of today read a name as the source spells it, so they also do not find an ADL call.
- Home: a definition, a specialization and a qualified read of a trait. Cases C6 and C7 show that the walk finds them.
- Declaration property: case C10 shows that the walk finds a member of a class template, with a type that is a dependent specialization.
- Expression shape: the walk finds a qualified name in a comparison (C8), in a `contract_assert` (C11) and in the target of a cast (C12). No kind of today reads an operator or a dependent operand (C9). The walk does not read an explicit instantiation (C13).

One owner rule has a member form: rule 1 of `check-no-unchecked-access.py` counts `object.unchecked()`. Rules 3, 4 and 7 of that script read a reflection operand or a splice. The fourth GCC patch gives the plugin an event for each of these forms: the member of a member access, and the operand of a reflection or a splice. The owner family holds the four rules with the kinds `unchecked-use`, `access-reflection`, `member-splice` and `walked-return` of the rule `owners`. In a pattern, the family reads a member named `unchecked` of a dependent object (P14, C4).

Three scripts of the expression-shape family do not move. `check-fullness-guard.py` reads member chains and `size()` calls on objects of a dependent type. `check-row-contains-discipline.py` reads the operand of a reflection, which the plugin does not read by design (`quarantine.cpp:172-177`). `check-use-after-move.py` stays for decision 13.

The shapes union, pointer-cast, allocator and raw-allocation of `check-proof-routes.py` move. For its three specialization shapes, new plugin code is necessary, and unit 1B.8e of section 8.6a writes it with a plant for each shape. The script goes only when the plants of its eight shapes are red.

Three plugin rules are in the tree today, and they stay: the quarantine rule (R6), the include rules and the contract rule. The rule `quarantine` also replaces `check-quarantine-regions.py`, because the plugin writes one `region` line for each region that a unit opens today.

### 18.11 The rules that stay on the parse tree

| Rule | Node types of the fork grammar | Why the plugin cannot hold it |
|---|---|---|
| `banned`, kind `reinterpret-cast` | `identifier` with the lexeme `reinterpret_cast`, and the macro bodies of `tsast.macro_bodies` | The tree of a cast does not keep the keyword (the probe of section 18.18) |
| `banned`, kinds `vector-reserve` and `process-start` | `call_expression` with a `field_expression` callee named `reserve`, the `identifier` or `qualified_identifier` of a process-start function or a syscall number, and the macro bodies | The rule `quarantine` reads no std name in a base file, and no configuration compiles `vessel/torch`. A subsequent stage deletes the two kinds (section 8.6a) |
| `lock-free-asserts` | `template_type` named `atomic` or `atomic_ref`, `static_assert_declaration`, the `qualified_identifier` of `is_always_lock_free`, and the macro bodies | GCC folds a `static_assert` that is not dependent, and the rule reads a header together with its check file. A unit of Stage 5 deletes the rule (section 8.6a) |
| `contract-form` | `function_contract_specifier`. An `attribute` named `assume` and a `call_expression` of `CRUCIBLE_PRE` in one `compound_statement` | An arm or a file that no build compiles. The expansion of `CRUCIBLE_PRE` changes with the contract semantic |
| `header-checks` | `static_assert_declaration`, `namespace_definition`, `declaration`, `field_declaration`, `alias_declaration`, `expansion_statement`, `call_expression`, `reflect_expression`, `splice_specifier`, `preproc_def` | GCC folds these constructs and keeps no tree of them. Only the kind "eager instantiation" can move to the plugin |
| `derived-files`, target `walk-units` | `call_expression` of `members_of`, `reflect_expression`, `template_function`, `for_range_loop`, `expansion_statement` | The reflection evaluator folds `members_of`, and CMake reads the list at configure time |
| `derived-pins` | `static_assert_declaration`, `binary_expression` with `==`, `number_literal` | GCC folds a `static_assert` that is not dependent |
| `erased-brand` | `template_type` and `template_function` with their argument lists, `template_declaration` with a parameter `Brand` or a default `DefaultBrand`, and the macro bodies | GCC folds a `static_assert` that is not dependent and a `sizeof`, and keeps no use location of a type in them. On 2026-10-08, 209 of the 345 compiled sites were in such a construct: 189 in a `static_assert` and 20 in a `sizeof`. The 83 sites of the negative-compile fixtures compile in no build |
| `use-after-move` | `call_expression` of `std::move`, the statements and the lambda captures of a function body | A path analysis in the plugin is a new analyzer (decision 13) |
| `layer-residual` | `preproc_include` in each arm, and the files of a base directory that are not C++ | Arms that no build compiles, base files that no target compiles, and the layout |
| `parse-clean` | Each node, and the `ERROR` and `MISSING` nodes | The fill of the tree cache (section 18.16) |

The table has no rule `member-splice`. The fourth GCC patch gives the plugin the events of a member access, a reflection and a splice, and the rule `owners` of the owner family holds rules 1, 3, 4 and 7 of `check-no-unchecked-access.py` (section 18.10).

The rule `parse-clean` (`tsast/parse_clean.py`) replaces `check-parse-clean.py`. Its check is the fill of the tree cache, because the fill parses each file. The rule `prose-bans` of the engine `meta` replaces the two scripts that banned a pattern in prose, `check-no-coordination-refs.py` and `check-fixy-dim-prose.py`. It is in `meta` because it also reads files that are not C++.

Two grammar changes for the fork help the rules that stay:
1. A node for the paste operator `##` in a macro body, so that a body with a paste parses. Seven scripts had a token fallback for such a body. `check-no-unchecked-access.py` keeps its fallback until its rules move. The change helps it, `contract-form` and `layer-residual`.
2. For `use-after-move`: a named node for each lambda capture with a field for `&`, and a field `operator` on `reference_declarator` (`grammar.js:707`, `710`, `1267-1274`).

### 18.12 The artifact rules and the build rules

| Rule | Engine | Today | Shape |
|---|---|---|---|
| `artifact-symbols` | `elf` | `check-libstdcxx-symbols.py`, `check-no-throw-no-rtti.sh` | `assay.readers.elf`, which gets the members of an archive, `.symtab` with the binding, `.dynsym` with the version, and the `.rela` sections. One batch call of `c++filt`. The throw and vtable rows get a part of the ledger that no configuration owns |
| `codegen-shapes` | `elf` | `check-core-shapes.py`, `check-ct-disassembly.py` | `assay.readers.objdump`. The constant-time probes get the Release flags from the compile database, as the core probes do |
| `step-cost`, `step-memory`, `object-size` and `header-alone` | `build` | `check-compile-cost.py`, the record half of `--check compile-first`, the totals of `report-check-warnings.py` | The family module `buildsys/cost_budgets.py`, on `assay.readers.census`. Each budget is one registry row. `guard warnings` reads the totals of the module |
| `instruction-total` | `build` | `check-parse-cost.py` | The family module `buildsys/cost_totals.py`, on `assay.readers.census` |
| `build-graph` | `build` | `check-build-order.py`, the dependency step of `check-reconfigure-noop.py` | The family module `buildsys/build_graph.py`. `build.ninja` and the dependency log of Ninja, through `assay.readers.ninja` |
| `no-unity-pch` and `no-ffast-math` | `build` | `check-no-unity-pch.py`, the link and preset half of `check-no-ffast-math.py` | The family module `buildsys/build_config.py`. The compile database, the replies of the CMake file API and `CMakePresets.json` |
| `header-constexpr-ops` and `header-standalone` | `build` | `check-header-constexpr-ops.py`, the `--standalone` mode of `check-header-checks.py` | The family module `buildsys/header_constexpr.py`. Two syntax-only compiles of each listed header, with SARIF diagnostics |
| `reconfigure-noop` | `build` | `check-reconfigure-noop.py` | Its self-test is a local test. The full check configures a build again, so `loop gauge` runs it before each stage gate |
| `tool-pins` | `build` | `cmake_pin.py --check` | `assay.setup.pins`, for each row of `utils/pins.txt`. `setup check` calls the same function |
| `session-oracle` | `build` | `session-oracle.sh --check` | `assay.oracle.runner`. The self-test of the oracle is the group `oracle` of `guard selftest` |

`check-atom-roster-joined.sh` goes. The walk unit of `test/layer/checks/fixy/Collision.cpp` instantiates the same two templates after each fixy header, in each build. Two negative fixtures replace it as the witness.

### 18.13 The meta rules

The engine `meta` holds these rules about the utils system and the repository:
1. The part shape of `registry`: each row parses, each ID is unique and each engine is one of the five engines. Each measured rule has `warn`, `error` and `unit`, each rule has its two cells, and each `doc` names a heading of CLAUDE.md.
2. The part modules of `registry`: each registry row has one function, and each function has one row. A measured rule with `held-by` has no module of its own, and its field `holder` names the function of its holder that judges it. The part refuses a held rule with no field `holder`, a function that does not exist, and a function that the witness of the rule does not run. No ledger row admits these three kinds. During Stage 1B, the ledger of `registry` holds each rule that has only a legacy module.
3. The part witness of `registry`: each rule has a plants directory with a minimum of one plant and one control. The command `witness` shows that each plant is red and each control is green. This part shows that the plants are in the tree, so a rule with no plant fails. During Stage 1B, the ledger of `registry` holds each rule that has no plant.
4. The part ledgers of `registry`: each file of `utils/ledgers/` names a rule with a ledger policy, and each row has the cell types of that policy. Each `PATH` cell names a file of the tree, and each row that its policy makes necessary has a reason. The module `assay/guard/engines/meta/registry_ledgers.py` holds the part. Its kind `path` refuses a `PATH` cell that names no file of the tree.
6. The part tests of `registry`: each registry rule and each test group has its ctest test with the label `ci_guard`. The module `assay/guard/engines/meta/registry_tests.py` holds the part. It reads the tests of a configured build, and it refuses a test of a master with no label `ci_guard` and a rule that no test runs with `guard check --rule ID`.
7. The part kinds of `registry`: the kinds of the plugin are the kinds of the plugin rows of the registry. The plugin witness does this check, because it builds the plugin.
8. The part layout of `registry`: each tracked file of `utils/`, and each tracked Python file or shell script outside `utils/`, matches one `path` row of the registry. Each `path` row matches a minimum of one file. The `path` rows give the layout of section 18.2 and the targets of section 18.20, and the files obey the naming rule of section 18.2. The Python of `vessel/torch/` and `test/harness/check_output.py` have `path` rows too, because they are outside the scope (section 18.20). So a guard script, a shell script or a helper of today fails the rule when its `path` row goes.
10. `rows-readers`: the two readers of the rows grammar give the same rows (section 18.6).
11. `derived-files`: each target of `loop derive` gives the bytes that the tree holds. The module `assay/guard/engines/meta/derived_files.py` holds the rule. It compares the files of the targets `witness-fixtures`, `header-costs` and `mint-inventory` of `loop derive` with the files of the tree. The rule also compares `test/layer/walk-checks.txt` with the bytes that the target `walk-units` gives from the parse trees.
12. The rules of the engineering bar (section 18.14).

### 18.14 The engineering bar

Each rule of the bar is a meta rule with a check that can fail. Unit 1B.4 lands each rule as a ratchet, and unit 1B.2 lands the part layout of the rule `registry`. The ledger of a ratchet holds the findings of today with the policy `count`, so a count can only fall. A new module has no finding. At the end of Stage 1B, each rule of the bar has no finding outside ledger rows with reasons (unit 1B.10). The column "Today" gives measurements of 2026-10-03 for 115 Python files with 2,773 functions. They are the 108 files of `utils/` and 7 files of `test/`, with `test/harness/check_output.py`.

| Rule | What must be true | The check that holds it | Today |
|---|---|---|---|
| `types` | A type hint on each parameter and each return value, and a clean run of the type checker in strict mode | The pinned mypy with `--strict --output json` on the package and the masters, through `assay.core.proc` (decision 16) | 2,735 of the 2,773 functions have full hints. mypy 2.4.0 with `--strict` gives 629 errors in 88 of the 115 files: 153 `type-arg`, 130 `arg-type` and 110 `attr-defined`. A cold run uses 3.6 s, and a warm run uses 0.11 s |
| `python-lint` | A clean run of the pinned ruff, and no change from `ruff format` | The pinned ruff, with its rules in the registry row (decision 16) | ruff 0.16.4 with its default rules gives 527 findings in 0.08 s. Seven of them are `B023`, a closure that reads the variable of a loop. `ruff format --line-length 120` changes 105 of the 108 files of `utils/` |
| `determinism` | Sorted output, and no result that changes with the time, or with the sequence of a directory listing or of a set | Python `ast`: each listing (`iterdir`, `glob`, `rglob`, `listdir`, `scandir`, `os.walk`) is the argument of `sorted()`, or a call of `assay.core.paths.listing()`. A read of the wall clock only in `assay.core.meter`, `assay.core.host` and `assay.loop.history`. `guard selftest` runs each golden test with two values of `PYTHONHASHSEED`, and the outputs must be equal | 113 of the 140 listing calls are not the direct argument of `sorted()`. 39 reads of the wall clock |
| `no-shell` | No shell script in `utils/` | A file with the suffix `.sh`, or a shell shebang, in `utils/` is a finding | 14 shell scripts with 4,413 lines |
| `registry`, part layout | The layout and the naming rule of section 18.2 | The `path` rows of the registry, the file system and Python `ast` (section 18.13) | 80 files with a shebang and no mode 755 |

**Errors.** `ExitCode` has seven members: `OK` 0, `FINDINGS` 1, `USAGE` 2, `MISSING_INPUT` 3, `TOOL_FAILED` 4, `SKIPPED` 77 and `CANNOT_START` 127. `SKIPPED` is the `SKIP_RETURN_CODE` of a ctest test, and `CANNOT_START` is the shell code that the step meters give today. The message of a finding has two parts: what is incorrect, and the action (section 18.7). A step meter keeps one exception to the bar. A failure in a measurement does not change the status of the step, as in `build-launcher.py` today. So its `except BaseException` blocks stay, with a ledger row for each block. The scope has 6 `except BaseException` blocks and 4 `except Exception` blocks today.

**Tests.** `guard selftest GROUP` runs the test modules of one group in one Python process. The groups are `core`, `readers`, `guard`, `loop`, `setup`, `oracle` and `mutation`. Each group is one ctest test with the labels `ci_guard` and `selftest`. A group gets parts with `--part K/N` when its wall time is more than 5 s. `fixture_store_self_test` has such parts today. The rules have no test module, because their plants are their tests. The test `guard_RULE` runs the check and the witness of the rule (section 18.16).

The groups come from these measurements of 2026-10-03. In `CTestCostData.txt` of the build of section 18.1, the 126 self-test tests have a sum of 183.6 s. The 226 `ci_guard` tests have a sum of 278.8 s. 62 of the self-tests use less than 1 s each, and their sum is 15.7 s. A small self-test uses approximately 108 ms (p50 of 10 runs of each of four self-tests). The start of Python with the import of `tsast` and `check_report` uses approximately 55 ms of that time (p50 of 10 runs). The fixture store keeps its 31 parts and the session oracle its 3 parts. So the groups and the witnesses of the rules remove approximately 80 starts of Python from each run (E). The parts keep the wall time of the longest test less than that of the plugin witness, which sets the wall time of the run (section 18.17).

**Cost.** Each engine reads its input one time for each call (section 18.9). The test times of section 18.19 give the cost of each engine today, added for the scripts that stay on the engine. The costs are `tsast` 20.5 s with the fill, `elf` 14.4 s, `build` 25.7 s and `meta` 17.6 s. The plugin runner, the regions and the witness use 14.1 s, and the plugin adds 0.07 % to 0.35 % to the instructions of each compile (§V). Section 18.2 gives the start cost of the dispatch, which is the same in the three masters. Then each subcommand imports its own modules. With `-S`, the import of `tsast.py` alone used 39.7 ms, and the import of `check_report.py` alone used 22.3 ms. `concurrent.futures` and `multiprocessing` use 16.5 ms of the 39.7 ms.

**Shell.** No shell script stays. Each shell script of today goes to a module, because Python can run at each place where a shell script runs today. CMake finds `python3` at configure time, and the build launcher of today uses it (§V). The CI files keep their own shell lines in YAML, and those lines start the masters. Section 18.20 gives the target of each shell script.

### 18.15 The core API

The shared modules of the package have these public names. Each record is a frozen dataclass with slots, and each closed set of words is an enumeration.

```
assay.core.paths
    REPO_ROOT: Path
    def shown(path: Path, build_dir: Path | None = None) -> str        # "include/x.h" or "@build/x.h"
    def listing(directory: Path, pattern: str = "*") -> tuple[Path, ...]   # sorted
    def build_dir_of(path: Path) -> Path | None

assay.core.cache
    def root() -> Path | None                       # CRUCIBLE_CACHE_DIR, or None for "off", or $XDG_CACHE_HOME/crucible
    def tools_root() -> Path
    def write_if_changed(path: Path, data: bytes) -> bool    # an atomic rename, False when the bytes are equal
    def locked(path: Path) -> AbstractContextManager[None]
    class ResultCache: get(key: bytes) -> bytes | None; put(key: bytes, value: bytes) -> None

assay.core.proc
    class Command: argv: tuple[str, ...]; cwd: Path | None; env: Mapping[str, str] | None; timeout_s: float; stdin: bytes
    class Completed: command: Command; exit: int; stdout: bytes; stderr: bytes; wall_s: float
    class ToolFailed(Exception)
    def run(command: Command) -> Completed          # no shell, and always a timeout
    def require(completed: Completed) -> Completed  # ToolFailed when the exit is not 0

assay.core.git
    class Commit: sha: str; time: int; subject: str; paths: tuple[str, ...]
    def tracked_files(root: Path, pathspec: Sequence[str] = ()) -> tuple[str, ...]   # sorted
    def head(root: Path) -> str
    def is_clean(root: Path) -> bool
    def show(root: Path, revision: str, path: str) -> bytes | None
    def commits(root: Path, paths: Sequence[str], since: str | None = None) -> tuple[Commit, ...]
    def moves(root: Path, base: str) -> Mapping[str, str]
    def scratch_repository(files: Mapping[str, bytes]) -> AbstractContextManager[Path]

assay.core.rows
    class Row: path: str; line: int; words: tuple[str, ...]; cells: tuple[str, ...]
    class RowsError(Exception): path: str; line: int
    def parse(text: str, path: str) -> tuple[Row, ...]
    def fields(row: Row, allowed: frozenset[str]) -> Mapping[str, str]
    def render(words: Sequence[str], cells: Sequence[str]) -> str
    def write(path: Path, comment: str, rows: Iterable[str]) -> bool

assay.core.registry
    class Engine(StrEnum): PLUGIN, TSAST, ELF, BUILD, META
    class Policy(StrEnum): NONE, COUNT, ADMIT, MAX, BASELINE
    class Threshold: warn: float; error: float; unit: str
    class Rule: id: RuleId; engine: Engine; scope: tuple[Scope, ...]; policy: Policy; kinds: tuple[str, ...];
                threshold: Threshold | None; inputs: tuple[str, ...]; doc: str; refuses: str; action: str
    class Job: entity: str; modules: tuple[str, ...]; reason: str
    class PathRow: glob: str; text: str
    class Registry: rules: Mapping[RuleId, Rule]; jobs: tuple[Job, ...]; paths: tuple[PathRow, ...]
    def load(path: Path = REGISTRY) -> Registry

assay.core.ledger
    class Key: path: str; kind: str; entity: str
    class Entry: key: Key; value: int | float | Admitted | Baseline; reason: str; line: int
    class Configuration: name: str; facts: tuple[tuple[str, str], ...]
    class Ledger: rule: RuleId; comment: str; configurations: tuple[Configuration, ...];
                  base: Mapping[Key, Entry]; deltas: Mapping[str, Mapping[Key, int]]
    class WriteMode(StrEnum): LOWER, RAISE, ADMIT, IMPORT
    def read(rule: Rule) -> Ledger
    def compare(rule: Rule, ledger: Ledger, findings: Sequence[Finding], configuration: str) -> tuple[Finding, ...]
    def write(rule: Rule, ledger: Ledger, findings: Sequence[Finding], configuration: str,
              mode: WriteMode, reason: str | None) -> Ledger
    def history(rule: Rule, since: str | None = None) -> tuple[LedgerVersion, ...]

assay.core.ruletable
    class RuleTable: layers, allows, doors, admits, quarantines, enforces, languages, homes, owners, seals, heavies, witnesses
    def load(path: Path = RULE_TABLE) -> RuleTable

assay.core.pins
    class Via(StrEnum): PIP, ARCHIVE, GIT
    class Pin: name: str; via: Via; version: str; source: str; digest: str; platform: str | None; text: str
    def load(path: Path = PINS) -> tuple[Pin, ...]

assay.core.report
    class Level(StrEnum): ERROR, WARNING
    class Finding: rule: RuleId; kind: str; level: Level; path: str; line: int; column: int; entity: str; message: str
    def text(finding: Finding) -> str
    def emit(findings: Iterable[Finding], warnings_dir: Path | None) -> ExitCode
    def read_warnings(build_dir: Path) -> tuple[Finding, ...]

assay.core.exit
    class ExitCode(IntEnum): OK = 0, FINDINGS = 1, USAGE = 2, MISSING_INPUT = 3, TOOL_FAILED = 4,
                             SKIPPED = 77, CANNOT_START = 127

assay.core.cli
    class Subcommand: name: str; module: str; summary: str; is_hidden: bool
    def dispatch(master: str, table: Sequence[Subcommand], argv: Sequence[str]) -> NoReturn
    def parser(prog: str, summary: str) -> argparse.ArgumentParser   # imports argparse on the first call

assay.core.meter
    class Measurement: exit_code: int; cpu_s: float; user_s: float; system_s: float; wall_s: float; peak_kb: int;
                       instructions: int | None; cpu_limit_s: int | None; reached_cpu_limit: bool
    def measure(argv: Sequence[str], env: Mapping[str, str] | None, cpu_limit_s: float | None,
                count_instructions: bool) -> Measurement
    def meter_compiler(argv: Sequence[str]) -> NoReturn
    def ccache_hit(stats_log: Path, outer_log: Path | None) -> bool
    def end_like(exit_code: int) -> NoReturn

assay.core.record
    class Step(StrEnum): COMPILE, LINK, TEST, FIXTURE
    class Cost: cpu_s: float; user_s: float; system_s: float; wall_s: float; peak_kb: int; load: float; end: float;
                instructions: Mapping[str, int]
    class CostRecord: step: Step; result: str; source: str | None; output: str; output_bytes: int | None; exit: int;
                      cost: Cost | None; last_cost: Cost | None
    def read(path: Path) -> CostRecord
    def write(path: Path, record: CostRecord) -> None

assay.core.budget
    def judge(rule: Rule, item: str, value: float, admitted: Entry | None, is_ci: bool) -> Finding | None

assay.core.host
    def load_average() -> float
    def available_memory_gb() -> float
    def usable_cpus() -> int
    def is_github_actions() -> bool

assay.core.target
    def build_kind(build_dir: Path) -> str | None
    def target_tier(entry: CompileEntry) -> str

assay.core.testing
    def expect(name: str, condition: bool, detail: str = "") -> None
    def captured(call: Callable[[], ExitCode]) -> tuple[ExitCode, str, str]
    def plant_tree(files: Mapping[str, str]) -> AbstractContextManager[Path]
```

The readers of section 18.4 have these public names:

```
assay.readers.tsast           the API of tsast.py today: Node, Tree, parse, parse_texts, fill_trees, cpp_files and the node helpers
assay.readers.python          def module(path: Path) -> ast.Module; def comments(path: Path) -> tuple[Comment, ...];
                              def imports(path: Path) -> tuple[str, ...]
assay.readers.shell           def functions(path: Path) -> tuple[ShellFunction, ...]; def commands(path: Path) -> tuple[ShellCommand, ...]
assay.readers.cmake           QUERIES: tuple[str, ...]; def codemodel(build_dir: Path) -> CodeModel;
                              def cache(build_dir: Path) -> Mapping[str, CacheEntry]; def cmake_files(build_dir: Path) -> tuple[str, ...];
                              def presets(root: Path) -> Presets
assay.readers.ninja           def graph(build_dir: Path) -> Graph; def log(build_dir: Path) -> Mapping[str, LogRecord];
                              def deps(build_dir: Path) -> Mapping[str, tuple[str, ...]];
                              def ran_outputs(before: Mapping[str, LogRecord], after: Mapping[str, LogRecord]) -> tuple[str, ...]
assay.readers.compdb          class CompileEntry: file: Path; directory: Path; argv: tuple[str, ...]; output: Path | None;
                              def entries(build_dir: Path) -> tuple[CompileEntry, ...]
assay.readers.ctest           def tests(build_dir: Path, label: str | None = None) -> tuple[TestEntry, ...];
                              def costs(build_dir: Path) -> Mapping[str, CostRecord]
assay.readers.sarif           SARIF_FLAG: str; class Diagnostic: level: Level; path: str; line: int; column: int; message: str;
                              children: tuple[Diagnostic, ...]; def read(path: Path) -> tuple[Diagnostic, ...]
assay.readers.include_trace   def read(stderr: str) -> tuple[IncludeStep, ...]
assay.readers.elf             def members(path: Path) -> tuple[Member, ...]; def symbols(member: Member) -> tuple[Symbol, ...];
                              def sections(member: Member) -> tuple[Section, ...]; def relocations(member: Member) -> tuple[Relocation, ...];
                              def demangle(names: Sequence[str]) -> tuple[str, ...]
assay.readers.objdump         def functions(path: Path) -> tuple[Function, ...]       # each with its instructions and relocations
assay.readers.sections        def findings(path: Path) -> tuple[Finding, ...]; def stamp(path: Path) -> str
assay.readers.census          def census(build_dir: Path) -> Census   # the records, the compile database, the tests and the deps, read one time
assay.readers.tool_reports    def mypy(stdout: bytes) -> tuple[Finding, ...]; def ruff(stdout: bytes) -> tuple[Finding, ...];
                              def clang_format(path: str, stdout: bytes) -> tuple[Finding, ...]
```

A reader that reads a file of the build directory names the file in the message of each error, with the command that writes it.

### 18.16 The ctest registration and the inner loop

`cmake/Guards.cmake` reads the registry and registers these tests. Each test gets the label `ci_guard` and the label of its engine:

| Test | Command | Notes |
|---|---|---|
| `guard_parse_clean`, in 8 parts | `guard check --rule parse-clean --part K/8` | The fill of the tree cache, as `tsast_tree_fill` is today. It is the fixture `tsast_trees` |
| `guard_plugin` | `guard check --engine plugin --build-dir BUILD` | The ratchet of today, for each plugin rule. One read of the sections is sufficient for each plugin rule |
| `guard_plugin_witness` | `guard witness --engine plugin` | It replaces `quarantine_plugin` |
| `guard_RULE`, one test for each rule of the engines `tsast`, `elf`, `build` and `meta` | `guard check --rule RULE`, and then `guard witness --rule RULE` | The test name has underscores in place of the hyphens. Only the tests of the `tsast` rules get `FIXTURES_REQUIRED tsast_trees`. Today all 226 tests get it, also 12 tests that read no tree. The three census rules wait for each fixture, as their tests do today |
| `selftest_GROUP`, with parts where section 18.14 gives them | `guard selftest GROUP [--part K/N]` | The groups `core`, `readers`, `guard`, `loop`, `setup`, `oracle` and `mutation`. The fixture store keeps its 31 parts and the session oracle its 3 parts, in their groups |

The registry holds approximately 57 rules of the four engines other than `plugin`. They are 15 `tsast` rules, 2 `elf` rules, 13 `build` rules with a ctest test and 27 `meta` rules. 12 of the `meta` rules are the rules of the bar. So the label `ci_guard` holds approximately 105 tests (E), not 226. The 8 parts of `preprocessed_store_fill` go with the store. The script tests of the fixture driver, the test meter and the edit-loop gauge become parts of the group `loop`. The CI files get the new paths and no other change, and no landing waits for a CI run (D11).

**One test for each rule, and no shards.** These measurements give the shape of the tests. They are from 2026-10-03, in the work tree at 5317d9bcd, with 96 jobs and a warm tree cache. Each number is the second of two runs, and the two runs had a difference of 0.1 s or less:

| Run | Wall time | Sum or peak |
|---|---|---|
| The 53 check tests of today with a script that imports `tsast.py`, with `ctest -R ... -j 96` | 4.4 s | The sum of the 69 tests, with their 16 fixture tests, is 52.2 s |
| The 11 of those checks that stay on the parse tree, with `ctest -R ... -j 96` | 2.8 s, set by `walk_units` (2.6 s) | The sum of the 11 is 10.6 s, and the sum of their 8 fixture tests is 1.2 s |
| The same 11 checks in one Python process of a scratch runner, with one read of each tree from the tree cache (3,658 trees) | 7.9 s | Peak memory 885 MB |
| The same runner in four processes of approximately equal work: walk-units, contract-form with two small checks, banned-calls with derived-pins and fullness-guard, and the other four | 2.6, 2.4, 3.6 and 4.0 s | Peak memory 250 to 795 MB for each process |

The 11 checks are banned-calls, contract-form, header-checks, walk-units, derived-pins, fixy-hw-discipline, use-after-move, fullness-guard, row-contains, audit-pre-callsite-count and parse-clean. The parts `member-splice` and `layer-residual` are not in one script today, so no run holds them. The work tree held the plants of section 18.18, so 5 of the 53 tests failed. Each check that failed read each file too.

In the scratch runner, use-after-move gave 4 findings less, because it got tree objects that an earlier check used. So each rule of a shared runner must get trees that no rule changes. A division of the files into four parts gave 1.9 s and 2.2 s for two of the parts. But each part gave incorrect verdicts, because walk-units, header-checks and derived-pins put together the facts of all files.

A read from the warm tree cache costs little, and the cost is in the rules. A shard costs the sum of its rules, but a test of today costs one rule. So each rule gets its own test, and the engine `tsast` has no shards.

**The inner loop.** `assay.loop.inputs` gives the inputs of each rule and of each test group. They are the modules of the import closure of the rule module, which Python `ast` gives. They also include the ledger, the plants, the registry row, the rule table and the scope of the rule. The field `inputs` of the registry adds only the files that the import graph does not show, such as a data file of a rule. The inputs of a test group are the import closure of its test modules. Today `run-affected-tests.py` runs each script test on each call, because it cannot know the inputs of a script.

After unit 1B.10, `loop test` reads these inputs. It runs the test `guard_RULE` or `selftest_GROUP` only when an input changed after the last pass of that test. It records a fingerprint of the inputs, as `run-affected-tests.py` does today for an executable test. Then:
- A change of one rule module, one ledger or one plant runs one test.
- A change of a module of `assay/core/` runs each rule and each group that imports it.
- A change of a header of `include/` runs each rule with a scope that holds the header. These are approximately 15 parse-tree tests, `guard_plugin` because the objects change, and each build rule that reads the changed records.
- A change of the plugin source, the rule table or a plugin plant runs `guard_plugin_witness`.

### 18.17 The cost before and after

Sections 18.1, 18.2 and 18.14 give the measurements of the column "Before". The column "After" is an estimate (E), from the measured parts, except where the row says that the value is a rule.

| Quantity | Before | After (E) | Reason |
|---|---|---|---|
| Commands | 108: 89 Python files of `utils/` with a `__main__` block, 12 shell scripts of `utils/`, 5 Python helpers and 2 shell drivers | 3 masters. This is a rule (the part layout of `registry`) | Section 18.2 |
| Start cost of the step meter of each compile and link | 11.51 ms p50, 12.60 ms max | 10.33 ms p50, 11.39 ms max (measured with the scratch master) | Section 18.2 |
| Guard scripts and rule modules | 65 check scripts and 3 other guard scripts | Approximately 30 rule modules and 5 engine runners in the package, and 6 families in the plugin | Sections 18.9 to 18.13 |
| Python files and lines | 114 files, 83,903 lines | Approximately 120 modules, 60 test modules and 65,000 lines | The 30 scripts that move into the plugin hold 20,185 lines. The deletion of `preprocessed.py` removes 2,229 lines of Python. The copies of function bodies hold 965 lines. The new readers and rules add approximately 4,000 lines, and the shell scripts that become modules add approximately 1,300 lines |
| Shell files and lines | 16 files, 4,678 lines | 0. This is a rule (`no-shell`) | Section 18.20 |
| C++ lines of the plugin | 4,456 in `quarantine.cpp` and `plugin_core.h` | Approximately 7,000 | The six families of section 18.10 and `rows.h` |
| Data formats | 22 | 1 grammar with 5 schemas, and the two goldens of tools | Section 18.6 |
| Groups of equal or almost equal function bodies | 33 groups, 149 functions, 965 lines | 0 outside ledger rows | Section 18.14 |
| Files that do a shared job by themselves | `subprocess` 56, `argparse` 43, the compile database 25, git 18, `CMakeCache.txt` 16 | 1 owner module for each job | Section 18.14 |
| Errors of mypy in strict mode | 629 in 88 of 115 files | 0 outside ledger rows. This is a rule (`types`) | Section 18.14 |
| Tests with the label `ci_guard` | 226 | Approximately 105 | Section 18.16 |
| Wall times of the guard scripts and modules, added | 174 s (of 299 s for all 226 tests) | Approximately 115 s or less | The tests of the 30 scripts that move into the plugin use 61 s today, and they go. The tree has the plugin check and its witness today. Each other rule keeps its cost. Section 18.16 shows that a shared read of the trees decreases the cost by a small quantity only |
| Wall time of `ctest -L ci_guard -j 96` | 9.0 s, set by `quarantine_plugin` (8.6 s) | Approximately 9 s | The plugin witness stays the slowest test, unless its 19 parts become separate tests |
| CPU time of the guards after an edit of a base header | 1,105 CPU-s | Approximately 550 CPU-s | The preprocessed store and its fill of 535 CPU-s go, with approximately 170 starts of Python |
| CPU time of the build after an edit of a base header | 2,285 CPU-s (section 7.10) | at most 1 % more | The new plugin rules use the walks that the plugin does today. A base header gets one walk in its sentinel unit. On 2026-10-02 the plugin used 0.07 % to 0.35 % of the instructions of a compile |
| Files of `utils/` | 263: 159 in `utils/scripts/`, 99 in `utils/tools/` and 5 in `utils/toolchain/` | 3 masters and approximately 180 Python files of the package. 4 data files at the top, approximately 25 ledgers and the plants of each rule. Approximately 20 files of C++ tools, the row-hash golden and the two patches. The C++ tools have 12 files today, and `rows.h`, its host program and the six family sources come after | Sections 18.2 and 18.20. The plants of the plugin test move into the plants directories |

### 18.18 The plants

Each claim of table 18.19 that the plugin holds a rule has a plant. The plants of each rule are in `utils/plants/RULE/`. Each unit of Stage 1B that brings a rule module also brings its plants. The plants of this section become the first plants of their rules. Unit 1B.7 moves the fixtures of the plugin test there. A plant is a small file in a quarantined or a base directory of a work tree, and the plugin compiles it in `ERROR` mode. P1 to P13 use the compile command of `test/test_arena.cpp`. P14 uses the compile command of `test/layer/checks/foundation/core/Choice.cpp`, the sentinel unit of a header of the language. A claim holds when the plant fails and its control passes. The date of the plants is 2026-10-03, at 5317d9bcd.

| Plant | Violation | Result | Control | Result |
|---|---|---|---|---|
| P1 | `values.reserve(4U)` on a `std::vector<int>&` parameter, in `test/` | Exit 1: `std_entity std::vector::reserve` and `std_object std::vector` | The same function with no vector | Exit 0 |
| P2 | The same call through a template parameter, instantiated with a `std::vector` | Exit 1, for the `std::vector` object only. The plugin does not read the dependent member call | (a known gap) | |
| P3 | `std::start_lifetime_as<Pair>(&storage)` in `test/` | Exit 1: `std_entity std::start_lifetime_as` | A plain member read | Exit 0 |
| P4 | `::sysconf(_SC_PAGESIZE)` in `test/` | Exit 1: `c_library_call sysconf` | The same file with no call | Exit 0 |
| P5 | A data member `std::uint8_t bytes[16]{}` in `test/` | Exit 1: `c_array_object` | Two named members | Exit 0 |
| P6 | `std::ifstream stream(path)` in `test/` | Exit 1: `std_object std::basic_ifstream` and `std_entity is_open` | The same file with no stream | Exit 0 |
| P7 | `std::system("true")` in `test/` | Exit 1: `c_library_call system` | The same file with no call | Exit 0 |
| P8 | `int positive(int value) pre(value > 0);` in `test/` | Exit 1 in `ERROR` mode and in `REPORT` mode | `CRUCIBLE_PRE(value > 0)` in the body | Exit 0 in `REPORT` mode |
| P9 | A `std::atomic<std::uint64_t>&` parameter and `fetch_add` in `test/` | Exit 1: `std_object std::atomic` | The same file with no atomic | Exit 0 |
| P10 | `#include <chrono>` in a header of `include/fixy/` | `layer_header <chrono> in the layer X1_wrappers`, an error in `ERROR` mode | A header that includes `foundation/Platform.h` | No finding on the lines of the plant |
| P11 | `#include <crucible/Arena.h>` in a header of `include/fixy/` | `upward_include include/crucible/Arena.h (no layer) from the layer X1_wrappers` | The same header with a lower include | No finding on the lines of the plant |
| P12 | A C array member and a `reinterpret_cast` in a header of `include/fixy/` | Exit 0: the plugin does not read a base header for these kinds | | |
| P13 | `reinterpret_cast<std::uintptr_t>(&value)` in `test/` | Exit 0: no kind of the plugin reads the cast | | |
| P14 | Fourteen cases in the templates of a base header, `include/foundation/core/guardsys_plant/Patterns.h`, which no unit instantiates. The next table gives each case | Exit 1, with 10 findings | C0, a function that is not a template | Red, so the walk runs |

P14 is in a file of the language. So the name rule of the language files gives an error for each library name that no admit row admits there. The cases name `std::exchange`, `std::in_range` and `std::pair`, which no admit row admits. The unit is one line, `#include <foundation/core/guardsys_plant/Patterns.h>`, as a sentinel unit is. The tree at 5317d9bcd has no family of the design. So P14 does a test of the walk that each family will use, with the one rule that applies to a base file today:

| Case | Family | The use in the pattern | Result |
|---|---|---|---|
| C0 | Control | `std::exchange(value, 0)` in an inline function that is not a template | Red |
| C1 | Owner | The same call in a function template, with no dependent argument | Red |
| C2 | Owner | `std::exchange(value, T{})`, a qualified call with dependent arguments | Red |
| C3 | Owner | `std::in_range<T>(raw)`, a dependent template argument | Red |
| C4 | Owner | `container.reserve(4U)`, a member call on an object of a dependent type | No finding |
| C5 | Owner | `swap(first, second)`, which only argument-dependent lookup resolves | No finding |
| C6 | Home | `typename std::pair<T, T>::first_type` in an alias template | Red |
| C7 | Home | A partial specialization of `std::common_type` for a class template of the header | Red, as `std::common_type::operator=` |
| C8 | Expression shape | `value == std::exchange(other, T{})` | Red, for the name. No kind reads the operator |
| C9 | Expression shape | `ring.count == ring.capacity`, two dependent members | No finding. No kind reads it |
| C10 | Declaration property | A data member `std::pair<T, T> member` of a class template | Red |
| C11 | Expression shape | `contract_assert(std::in_range<T>(raw))` | Red |
| C12 | Expression shape | `reinterpret_cast<std::pair<T, T>*>(raw)` | Red, for the target type |
| C13 | Expression shape | `template struct c7_box<std::pair<int, int>>;`, an explicit instantiation | No finding |

A scratch probe plugin read `REINTERPRET_CAST_P` at `PLUGIN_PRE_GENERICIZE` and `REINTERPRET_CAST_EXPR` in template patterns, on nine casts. It found the mark on a cast between two unrelated pointer types and on a cast to a derived pointer type. It found the expression in the pattern of a template. It found no mark on these six shapes:
- A cast from a pointer to an integer
- A cast from an integer to a pointer
- A cast to a reference
- A cast to the same type
- A cast in a macro
- A cast of the address of a constant.

In the original tree, GCC prints the cast as `(uintptr_t) value`.

P12 and P13 show that the plugin does not read the base scope or the cast today. So a claim on a base file is "move into the plugin", with a new rule and a new plant, and not "delete".

### 18.19 Inventory of the guard scripts

The column "Test s" gives the sum of the wall times of the tests of the script, with its self-tests, in one call of `ctest -L ci_guard -j 96`. The actions are delete, move into the plugin, merge with another script or mechanism, and keep on its engine. A script gets the action delete when the plugin or the build holds its rule today. No script moves to the parse tree, because each script that reads C++ reads it through `tsast.py` today. The text reads of sections 18.1 and 18.4 go when their scripts move or merge.

The column "Target" names a rule, and its module follows from the rule: `assay/guard/engines/ENGINE/RULE.py`, with underscores, or the family module that the registry names (section 18.9). The engine `build` has the directory `buildsys`. For a target "plugin `RULE`, FAMILY", the source of the family in `utils/tools/quarantine/` holds the check (section 18.10). The module `assay/guard/engines/plugin/RULE.py` judges the records that the plugin writes, and the ledger is `utils/ledgers/RULE.txt`. In unit 1B.1, each script first moves to `assay/guard/legacy/` with underscores in its name (section 18.9).

| Script | The rule | Input today | Ledger (rows) | Plugin overlap | Target | Action | Test s |
|---|---|---|---|---|---|---|---|
| `check-admitted-audit.py` | The admit rows, the audit table and the flag matrix agree | Text, and comments through `tsast` | audit table (68) | None | meta `admitted-audit` | Keep | 0.5 |
| `check-allowlist-keys.sh` | Each allowlist key and each Markdown path names a file | Text | the 19 allowlists | None | meta `registry` and `prose-bans` | Merge | 1.2 |
| `check-atom-roster-joined.sh` | The collision roster includes each atom roster | Compiler run of a sentinel | none | None. The walk unit of `Collision.cpp` holds the same rule in each build | none | Delete, with two fixtures | 6.9 |
| `check-banned-calls.py` | No `reinterpret_cast`, vector `reserve`, file stream or process start | `tsast`, line-text keys | 4 lists (0, 3, 0, 1), fixy-only-paths (8) | Full for reserve, stream and spawn in compiled quarantined files (P1, P6, P7). None for the cast (P13), for a base file or for `vessel/torch` | tsast `banned`, for the cast, and for the base files and `vessel/` (section 8.6a) | Keep, as one rule. Delete the parts of the reserve ban and the spawn ban in quarantined files. The stream ban stays as the kind `file-stream` (section 8.6a) | 2.5 |
| `check-brand-drain.py` | Erased brand spellings only decrease | `tsast` | brand-drain (101) | None. GCC folds the `static_assert` or the `sizeof` of most sites, and no build compiles the fixtures (section 18.11) | tsast `erased-brand` | Keep | 2.0 |
| `check-budget-history.py` | Thresholds and ledgers do not rise without a measurement | git | budget-history (1) | None | none | Keep | 2.4 |
| `check-build-order.py` | No order-only wait, compile-first, custom dependencies | `build.ninja`, Ninja deps, records | compile-first (88) | None | build `build-graph` | Keep | 2.8 |
| `check-cache-size.py` | The size of each cache is not more than its limit | File sizes | none | None | The eviction of `assay.core.cache` | Merge | 1.0 |
| `check-ci-guard-labels.py` | Each guard test has the label `ci_guard` | ctest JSON | none | None | meta `registry` | Keep | 1.9 |
| `check-clang-format.sh` | Each C++ file is clang-format clean | clang-format | none | None | meta `clang-format`, with the XML replacements of clang-format, in ctest | Keep | Not in ctest |
| `check-compile-cost.py` | The compile, link and object budgets | Records, ELF sizes, Ninja deps, file API | 8 cost ledgers (0) | None | build `step-cost`, `step-memory`, `object-size` and `header-alone` | Keep | 5.9 |
| `check-contract-form.py` | No `pre` or `post` specifier. No `assume` of a fact that a PRE holds | `tsast` | contract-form (6) | Full for the specifier in compiled files (P8) | tsast `contract-form` | Keep, for the remaining part | 1.8 |
| `check-contract-semantic.py` | The last semantic flag agrees with the ignore define | Compile database | none | None | plugin `contract-semantic`, unit fact | Move | 0.2 |
| `check-core-shapes.py` | Each family operation compiles to the raw instructions | Compiler run, `objdump` | none (21 probes in code) | None | elf `codegen-shapes` | Keep | 3.0 |
| `check-ct-disassembly.py` | Each constant-time wrapper has no branch, call or division | Compiler run, `objdump`, byte prefilter | none | None | elf `codegen-shapes` | Merge | 4.6 |
| `check-ctx-init-door.py` | Each use of the init and background owners is at a listed scope | `tsast`, byte prefilter | 2 and 5 | None | plugin, owner | Move | 0.8 |
| `check-ctx-testing-boundary.py` | Each use of the testing door is in a listed file | `tsast`, a token scan of its own | 2 | None | plugin, owner | Merge | 1.5 |
| `check-derived-pins.py` | No `static_assert` pins a constant to its own literal | `tsast` | derived-pins (0) | None | tsast `derived-pins` | Keep | 2.5 |
| `check-detail-namespace.py` | No use of a `detail` namespace of the base outside the base | `tsast`, git, result cache | 59 | None | plugin, owner | Merge | 3.0 |
| `check-detsafe-ledger.py` | The hashing path includes no ledger code | `tsast` | none (17 roots in code) | None | plugin `detsafe-ledger`, include closure `seal` | Move | 0.9 |
| `check-escape-doors.py` | A public function that returns a reference is an accessor, a mint or a hatch | `tsast`, string methods | escape-doors (3) | None | plugin `escape-doors`, declaration property | Move | 0.5 |
| `check-federation-admission.py` | Only `fixy/Federation.h` defines `FederationAdmission` | `tsast`, preprocessed store, byte prefilter | none | None | plugin `home`, home | Move | 3.1 |
| `check-fixy-dim-prose.py` | No retired axis number in the prose of fixy | `tsast` prose and a regex | none | None | meta `prose-bans` | Merge | 0.5 |
| `check-fixy-hw-discipline.py` | Each `_hw` block has a live definition and a pin row | `tsast` | 2 rows in a test file | None | The reflection rows of `test/test_hardware_axis_pins.cpp` | Keep | 0.7 |
| `check-fullness-guard.py` | No `==` between a counter and a capacity | `tsast`, line-text keys | 3 | None. No kind reads an operator, and the operands in a pattern are names (P14, C4 and C9) | none | Keep | 1.2 |
| `check-git-isolation.py` | A script that writes with git isolates the repository | Python `ast`, ast-grep | none | None | none | Merge | 1.8 |
| `check-guard-engines.py` | Each script names its engine and reads no C++ as text | Python `ast`, ast-grep | guard-engines (114) | None | The registry | Keep, with more shapes | 1.6 |
| `check-header-checks.py` | A header does no compile-time work in each includer | `tsast`, a compiler for `--standalone` | header-checks (64) | None. The plugin can find only the eager instantiation | tsast `header-checks`. `--standalone` goes to build `header-standalone` | Keep | 3.9 |
| `check-header-constexpr-ops.py` | Each one-line unit meets its operation limit | Compile database, compiler | 0, and not-standalone (14) | None | build `header-constexpr-ops` | Keep | 1.0 |
| `check-heavy-includes.py` | A header includes no heavy standard header | `tsast` | heavy-includes (49), header-costs (165) | Full for base headers (P10) | plugin `heavy-include`, include closure `heavy` | Move | 0.8 |
| `check-host-owners.py` | Each class of a `host` namespace is in its listed file | `tsast`, token scan | host-owner roster (4) | None | plugin `home`, home | Merge | 2.6 |
| `check-layer-boundary.py` | foundation names only foundation, and fixy names only foundation and fixy | `tsast` | the layer rows of the rule table | Full for an include in a compiled arm (P11). The ledger holds 19 `upward_include` rows that the script does not find, because its three layers are coarser | plugin `layer-boundary`, and tsast `layer-residual` | Move | 1.0 |
| `check-libstdcxx-symbols.py` | No new libstdc++ symbol in three libraries | `nm`, `c++filt` | libstdcxx-symbols (135) | Partial: the source names in quarantined files | elf `artifact-symbols` | Keep | 2.3 |
| `check-lifetime-twin.py` | Each lifetime-bound parameter has a deleted rvalue twin | `tsast` | none | None | plugin `lifetime-twin`, declaration property | Move | 0.4 |
| `check-lock-free-asserts.py` | Each `std::atomic` of four directories has a lock-free assert | `tsast`, a substring search of comments | 2 markers | Full for a new `std::atomic` (P9). The quarantine ledger holds the 27 objects of today in 6 rows | tsast `lock-free-asserts` | Keep. A unit of Stage 5 deletes the rule (section 8.6a) | 0.6 |
| `check-mint-pattern.py` | Each mint is `[[nodiscard]] constexpr noexcept` with a constraint | `tsast` through `mintmodel.py` | mint-pattern (18), 50 markers | None | plugin `mint`, declaration property, with a mint record for the inventory | Move | 1.0 |
| `check-no-combine-ids-duplicate.py` | `combine_ids` has one body | `tsast`, result cache | 1 in code | None | plugin `home`, home | Merge | 1.1 |
| `check-no-coordination-refs.py` | No task or stage reference in prose | `tsast` prose, Python tokens, text | none | None | meta `prose-bans` | Keep | 1.3 |
| `check-no-ffast-math.py` | No fast-math option in a flag, a preset, an attribute or a pragma | Compile database, file API, presets, `tsast` | 0 (the file is missing) | None | plugin `fast-math-override`, unit fact, and build `no-ffast-math` | Move | 6.1 |
| `check-no-raw-array-member.py` | No C array data member in a base header | `tsast` | 3 | Quarantined files only (P5). None for a base header (P12) | none | Move | 0.6 |
| `check-no-throw-no-rtti.sh` | No throw, downcast, typeinfo or vtable in a library | `nm`, `readelf`, `objdump` | 3 | None | elf `artifact-symbols` | Merge | 4.5 |
| `check-no-unchecked-access.py` | `access_context::unchecked` only in listed files, no open static member, no walked return | `tsast`, preprocessed store | 33 | None | plugin, owner (rules 1 to 7, with the events of the fourth GCC patch). Rule 6 reads the class walk of the declaration-property family | Move | 3.8 |
| `check-no-unity-pch.py` | No unity build and no precompiled header | Compile database, a CMake lexer | none | None | build `no-unity-pch` | Keep | 0.6 |
| `check-padded-lists.py` | No padding bit in the element of a large fixed list | `tsast`, probe compiles | 2 | None | plugin `padded-list`, declaration property | Move | 7.6 |
| `check-parse-clean.py` | Each C++ file parses with the kit | `tsast` | UNPARSEABLE (1) | None | tsast `parse-clean`, the tree fill | Merge | 1.5 |
| `check-parse-cost.py` | The totals of bytes and instructions stay near the baseline | The census | parse-total (293), header-fanout (30), instruction-total (1) | None | build `instruction-total` | Merge | 5.6 |
| `check-permission-storage.py` | A `Permission` member has `[[no_unique_address]]` and is not static | `tsast` | 0 markers | None | plugin `permission-storage`, declaration property | Move | 0.8 |
| `check-process-wide-state.py` | Each mutable static object has a marker or a roster row | `tsast` | 13 | None | plugin `process-wide-state`, declaration property | Move | 0.8 |
| `check-proof-routes.py` | No route builds a proof type without its door | `tsast`, preprocessed store | 21 | Partial: allocators and `malloc` in quarantined files | plugin `proof-routes`, expression shape. For the three specialization shapes, new plugin code is necessary (P14, C13) | Move, after the plants of its eight shapes are red | 3.8 |
| `check-quarantine-ratchet.py` | The findings of the plugin only decrease | ELF sections, census, git | quarantine (8,984) | It is the runner of the plugin | plugin `quarantine`, in the runner of the plugin | Keep | 4.9 |
| `check-quarantine-regions.py` | Each opt-out region has a ledger row | `tsast`, byte prefilter | 8 | Partial: the plugin writes a `region` line for each compiled region | plugin `quarantine`, kind `region` | Merge | 0.6 |
| `check-reconfigure-noop.py` | A second configure compiles no unit | cmake, the Ninja log | none | None | build `reconfigure-noop` | Keep | 2.1 |
| `check-refined-pre-subsumption.py` | No PRE tests again the predicate of a `Refined` parameter | `tsast` | 0 markers | None | plugin `refined-pre-subsumption`, expression shape | Move | 0.9 |
| `check-row-contains-discipline.py` | A row test on a context row uses the lift | `tsast`, a regex on raw bytes | 0 markers | None. The plugin reads no reflection operand | none | Keep | 0.4 |
| `check-splits-orphan.py` | Code specializes a split trait only beside its tags | `tsast`, a regex on raw bytes | 2 in code | None | plugin `home`, home | Merge | 0.9 |
| `check-start-lifetime.py` | `std::start_lifetime_as` only in `Lifetime.h` | `tsast`, preprocessed store | 16, all fixtures | Full in compiled quarantined files (P3) | plugin `owners`, owner, for the base | Move | 4.0 |
| `check-stub-discipline.py` | A stub marker and its deprecation come together | `tsast` | none | None | plugin `stub-discipline`, declaration property | Move | 0.8 |
| `check-syscall-capability.py` | Each raw system call has a claim, and the affinity calls stay in their door | `tsast`, line-text keys | 39 | Full for the 34 quarantined rows (P4, and section 18.22) | plugin `syscalls`, owner, with each of the 39 claims as an admit row (section 8.6a) | Move | 3.3 |
| `check-syscall-grant-coverage.py` | The hardening grant set equals its system calls | `tsast`, string methods | none | Partial | plugin `syscalls`, expression shape | Move | 0.4 |
| `check-test-time.py` | Each script test meets `test-time` | The ctest log | test-time (0) | None | The timeout of the test preset | Keep | 1.8 |
| `check-trait-injection.py` | A trait specialization or an open gate read is only where a row admits it | `tsast` | open-gate-reads (142) | None | plugin `home`, home | Move | 5.8 |
| `check-use-after-move.py` | No use of a local after a move, on each path | `tsast` | 64 | None, because the admitted list holds `std::move` | tsast `use-after-move` (decision 13) | Keep | 1.2 |
| `check-vessel-acceptance.py` | The vessel builds, and its Python tests pass | cmake, ELF, Python | none | None | The tests `vessel_test_NAME` of a build with `TORCH_DIR` | Keep | 0.1 |
| `check-walk-units.py` | `walk-checks.txt` lists each check file that walks a namespace | `tsast` | walk-checks (105) | None | meta `derived-files`, with the target `walk-units` of `loop derive` | Keep | 4.0 |
| `check-witness-roster.py` | Each door class has a roster row and a fixture | `tsast` | witness-roster (120) | None | plugin `witness-roster`, declaration property. The fixture generator becomes `loop derive witness-fixtures` | Move | 1.8 |

The other guard scripts:

| Script | The rule | Input today | Ledger (rows) | Target | Action | Test s |
|---|---|---|---|---|---|---|
| `audit-pre-callsite-count.py` | The counts of contract cites do not fall | `tsast` | contracts-baseline (1) | `tsast/contract_cites.py`, which counts the cites for the rule `decide-cite-ratio` | Keep, one module with the ratio | 0.8 |
| `audit-decide-cite-ratio.py` | Each `decide` procedure gets two cites in 180 days | `tsast`, git | none | meta `decide-cite-ratio` | Merge | 3.1 |
| `gen-mint-inventory.py` | The mint inventory is current, and the HS14 floor holds | `tsast` through `mintmodel.py` | mint-hs14-floor (14) | `loop derive mint-inventory` (`assay/loop/derive/mint_inventory.py`) from the mint records of the plugin, and the plugin rule `mint` | Keep | 3.3 |
| `cmake_pin.py --check` | cmake and ctest are the pinned version | `/proc`, `CMakeCache.txt` | `requirements.txt` | build `tool-pins`, for each row of `utils/pins.txt` | Keep | 1.8 |
| `session-oracle.sh --check` | The golden table of the oracle is current | The oracle, opam | `golden.csv` | build `session-oracle` (`assay/oracle/runner.py`), and the group `oracle` of `guard selftest` | Keep | 21.4 with the self-tests |
| `utils/tools/quarantine/test/check_plugin.py` | The plugin gives each expected finding | Compiler runs | test tables | `guard witness --engine plugin` (`assay/guard/engines/plugin/witness.py`) | Merge | 8.6 |
| `test/guard_attacks/test_use_after_move_attacks.py` | The misses of the use-after-move check | `tsast` | 5 rows in the test | The witness of `use-after-move`, with `utils/plants/use-after-move/` | Merge | 0.2 |
| `test/neg_compile_store.py` (budgets) | The fixture budgets and the rule of the reason | Fixture records | 3 ledgers (0) | `loop fixture` (`assay/loop/fixture/store.py`), with the build rules `step-cost` and `fixture-own-reason` | Keep | in the fixtures |
| `build-launcher.py`, `test-launcher.py` (budgets) | The budgets of each step | `wait4`, a hardware counter | 4 ledgers (0) | The judge of each step in `loop step` and `loop test-step`. The rules of `buildsys/cost_budgets.py` judge the records through `assay.core.budget` | Keep, in `loop step` and `loop test-step` | 2.9 and 1.1 |
| `report-check-warnings.py` | Seven totals, and the print of the warnings | Records, census | none | `guard warnings`, with the totals of `buildsys/cost_budgets.py` | Merge | 0.1 |

The 65 check scripts get these actions:
- 1 delete: `check-atom-roster-joined.sh`
- 23 move into the plugin
- 14 merge: 6 into a plugin family, 1 on the parse tree, 3 into meta, 2 into elf and 2 into build
- 27 keep: 11 on the parse tree, 6 in meta, 2 in elf, 7 in build, and the plugin runner
- 0 move to the parse tree.

`check-brand-drain.py` was a move into the plugin, and it became a keep on the parse tree on 2026-10-08 (section 18.11). Of the 27 kept scripts, most become one rule module of their engine. `check-lock-free-asserts.py` becomes a rule that a unit of Stage 5 deletes (section 8.6a). These parts of scripts go in Stage 1B:
- The parts of the reserve ban and the spawn ban of `check-banned-calls.py` that read quarantined files. Its stream ban stays as the kind `file-stream` (section 8.6a)
- The include half of `check-layer-boundary.py`.

### 18.20 Inventory of the other files

Each file of `utils/scripts/` has a user: a registered test, a CI step, a CMake file or the plan. Two files have no test and no CI user: `build-gauge.sh` and `gen-aten-op-table.py` are manual tools. Code names two files that are not in the tree, `no-ffast-math-allowlist.txt` and `no-file-stream-allowlist.txt`, and reads each one as an empty list. `utils/tools/mutation/survivors.txt` has no reader. Each file of the scope gets one target. The counts at the end of this section give the number of files of each target.

| File | Role | Users | Duplicates and notes | Target |
|---|---|---|---|---|
| `build_census.py` | Module | 5 importers | A reader of `CMakeCache.txt`, of the ctest list, of the cost records and of `ninja -t deps`, each with copies in other files | `assay/readers/census.py`, built on the readers of section 18.4 |
| `build-gauge.sh` | Gauge | The plan only | The step meter, the Ninja log parser and the git probe have copies | `loop gauge --cold`, in `assay/loop/gauge.py` |
| `build-launcher.py` | Launcher | `cmake/BuildLauncher.cmake` | `test-launcher.py` has copies of its ledger lookup and its judge | `loop step`, in `assay/loop/step.py` |
| `build_target.py` | Module | 2 guards | `kind_of` only calls `cost_meter.read_kind` | `assay/core/target.py` |
| `cache_dir.py` | Module with a CLI | 13 importers | Six staged writers do not use its `write_atomic` | `assay/core/cache.py` |
| `check_report.py` | Module with a CLI | Approximately 32 importers | Its budget parser has 2 copies, and its `classify` has 4 | `assay/core/report.py`, and the judge in `assay/core/budget.py` |
| `cmake_pin.py` | Module with a CLI | 8 importers, 4 shell users | A copy of the `CMakeCache.txt` reader, and a repository root of its own | `assay/setup/pins.py`. Its `--check` is the build rule `tool-pins` and `setup check` |
| `compile-meter.py` | Meter | ccache, through `cost_meter.py` | None | `loop meter-compiler`. ccache gets the master and the subcommand as the words of `CCACHE_PREFIX` |
| `cost_meter.py` | Module | 17 importers | Six jobs in one file, and an import cycle with `check_report.py` that only a lazy import breaks | `assay/core/meter.py`, `assay/core/record.py` and `assay/core/budget.py` |
| `decide_catalog.py` | Module | 2 audits | Its declarator unwrap is a copy of one in `gen-aten-op-table.py` | `assay/guard/engines/tsast/decide_catalog.py`, for `decide-cite-ratio` |
| `edit-loop-gauge.py` | Gauge | The owner, before each gate | The table, the median and the git probe have copies | `loop gauge`, in `assay/loop/gauge.py` |
| `elf_file.py` | Module | 4 importers | It gets the archive members, the symbol tables and the relocations | `assay/readers/elf.py` |
| `gen-aten-op-table.py` | Generator | None in ctest or CI | A copy of the string decoder of `tsast.py`, and a default path of one user | `loop derive aten-ops`, in `assay/loop/derive/aten_ops.py` |
| `install-ast-grep.sh`, `install-cmake.sh`, `install-tree-sitter.sh` | Installers | CI, `tsast.py`, two guards, the CMake action | Each of the three has copies of seven functions, and `sha256_of` has three different copies | `setup install`, in `assay/setup/install.py`, with the rows of `utils/pins.txt` |
| `layer_rules.py` | Module | 5 users | The second reader of the rule table. The plugin has the C++ reader (section 18.21) | `assay/core/ruletable.py`, built on `assay.core.rows` |
| `loop_history.py` | Module with a CLI | 3 importers | The step timer, the git probe and the table have copies | `loop history`, in `assay/loop/history.py` |
| `mintmodel.py` | Module | 2 users | The model moves into the plugin. The count of fixtures stays | `assay/loop/derive/mint_inventory.py`, smaller |
| `ninja_files.py` | Module | 4 importers | `build-gauge.sh` parses the Ninja log again | `assay/readers/ninja.py` |
| `pgo-bootstrap.sh` | Runner | `CMakeLists.txt`, CLAUDE.md | The git probe and the reader of the file API have copies | `loop pgo`, in `assay/loop/pgo.py` |
| `preprocessed.py` | Module and fill | 4 store users, 3 cache users, 1 user of `split_command` | Its fill, store, pool and identity hash copy the patterns of `tsast.py` | Delete. Its result cache goes to `assay.core.cache` |
| `quarantine_sections.py` | Module with a CLI | The ratchet, the plugin test | `check_plugin.py` parses the same line again | `assay/readers/sections.py` |
| `quarantine_stamps.py` | Generator | `Quarantine.cmake` | A copy of `write_if_changed` | `loop stamps`, in `assay/loop/stamps.py` |
| `refresh-derived.sh` | Runner | The checklist of the plan | A git probe of its own | `loop derive`, in `assay/loop/derive/__init__.py`, and the meta rule `derived-files` |
| `repo_root.py`, `repo_root.sh` | Modules | 28 importers, 6 shell users | Seven files calculate the root again | `assay/core/paths.py`. Unit 1B.5 deletes `repo_root.sh` with the shell scripts |
| `report-check-warnings.py` | Report | The CI step "Check warnings" | The print loop copies `check_report.emit` | `guard warnings` (`assay/guard/warnings.py`), with the totals of `buildsys/cost_budgets.py` |
| `run-affected-tests.py` | Runner | CLAUDE.md §XV rule 14 | Copies of the ctest list reader, the `CMakeCache.txt` reader and the launcher prefix test | `loop test`, in `assay/loop/affected.py` |
| `session-oracle.sh` | Runner | 4 tests, CI | None | The build rule `session-oracle`, `loop derive session-oracle` and the group `oracle` of `guard selftest` |
| `test-launcher.py` | Launcher | `cmake/TestLauncher.cmake` | Its judge and its ledger lookup copy `build-launcher.py` | `loop test-step`, in `assay/loop/teststep.py` |
| `throwaway_repo.py` | Module | 17 importers | None | `assay.core.git.scratch_repository` and `assay/core/testing.py` |
| `tools_root.sh` | Module | 3 installers | Four more places calculate the cache root of XDG | `assay.core.cache.tools_root`. Unit 1B.5 deletes the file |
| `tsast.py` | Module and fill | 56 importers | Two policies for the file list: `cpp_files` reads untracked files, and `tracked_files` does not | `assay/readers/tsast.py`. Its fill is the rule `parse-clean`, and `tracked_files` moves to `assay.core.git` |
| `tu-sample.py` | Gauge | `check-heavy-includes.py` loads it through `importlib` | A copy of the meter and of the compile database reader. A file with hyphens that another file imports | `loop gauge --sample`, in `assay/loop/gauge.py` |

The audits, `gen-mint-inventory.py` and `session-oracle.sh` are in the second table of section 18.19.

The files of `utils/tools/`, `utils/toolchain/`, `test/` and `bench/` that tables 18.19 and 18.20 do not hold:

| Files | Role | Target |
|---|---|---|
| `utils/tools/quarantine/quarantine.cpp`, `plugin_core.h`, `Quarantine.cmake` | The quarantine plugin and its build | They stay. The reader of the rule table moves into `rows.h`, and each family of section 18.10 gets a source file of its own |
| `utils/tools/quarantine/test/check_plugin.py` | The plugin test (19 parts) | `assay/guard/engines/plugin/witness.py` |
| The 62 C++ files and the 3 tables of `utils/tools/quarantine/test/` | The fixtures of the plugin test, with a planted tree of `include/` and `src/` | `utils/plants/quarantine/`, and the plants directory of each other plugin rule |
| The 6 files of `crucible_hwprobe*` and `dump_row_hashes.cpp` in `utils/tools/` | C++ tools | They stay |
| `utils/tools/row_hash_golden.txt` | The golden of `dump_row_hashes`, which `loop derive row-hashes` writes and the rule `derived-files` compares | It stays |
| The 2 files of `utils/tools/lsp_code_action_plugin/` | A C++ tool | They stay |
| `utils/tools/mutation/gates.py`, `run.py` | The gate mutants | `assay/mutation/gates.py` and `assay/mutation/run.py`, behind `loop evidence` |
| `utils/tools/mutation/invalid-exemptions.txt` | The admitted invalid mutants | `utils/assay/loop/evidence/admitted-gates.txt` |
| `utils/tools/mutation/survivors.txt` | No reader | Deleted |
| `utils/tools/session_oracle/session_oracle.py` | The driver of the oracle | `assay/oracle/runner.py` |
| The 13 other Python files of `utils/tools/session_oracle/`, except `toolchain.py` | The oracle modules | `assay/oracle/`, with the same names |
| `utils/tools/session_oracle/toolchain.py` | The install of Java, sbt and mCRL2 | `assay/setup/oracle.py`, behind `setup install`, with its pins in `utils/pins.txt` |
| `utils/tools/session_oracle/coq/LiveOracle.v` | Rocq source of the liveness certificate | `assay/oracle/coq/LiveOracle.v`, as data beside its reader |
| `utils/toolchain/gcc/build.sh` | The build of the patched GCC | `setup gcc`, in `assay/setup/gcc.py` |
| `utils/toolchain/gcc/BASE`, `utils/toolchain/cmake/requirements.txt` | Pins | Rows of `utils/pins.txt` |
| The 3 files of `utils/toolchain/gcc/patches/` | The GCC patches | They stay |
| `test/neg_compile_driver.py` | The fixture driver | `loop fixture`. The master sets the cache of the compiled modules, as the driver does today |
| `test/neg_compile_store.py` | The fixture store, its cost and its matcher | `assay/loop/fixture/store.py`, `cost.py` and `driver.py` |
| `test/neg_compile_driver_test.py`, `test/build_launcher_test.py` | The tests of the fixture store and of the build launcher | `assay/tests/loop/test_fixture.py` and `assay/tests/loop/test_step.py` |
| `test/contract_header_unit_test.py` | The probe of the contract defect of header units | none |
| `test/guard_attacks/test_use_after_move_attacks.py`, `use_after_move_evasions.cpp` | The attack test of use-after-move | The witness of `use-after-move`, with the plants in `utils/plants/use-after-move/` |
| `test/fuzz/run-afl.sh` | The AFL++ driver | `loop fuzz`, in `assay/loop/fuzz.py` (decision 21) |
| `bench/run-bench.sh` | The bench driver | `loop bench`, in `assay/loop/bench.py` (decision 21) |

`test/harness/check_output.py` is a test of two C++ probes of the product, as a C++ test executable is. It stays in `test/harness/`, outside the scope. The Python of `vessel/torch/` is product code, and it stays too. These files have `path` rows in the registry, so the rule `registry` permits them (section 18.13).

**The counts.** The scope holds 263 files of `utils/` and 9 files of `test/` and `bench/`, so 272 files. Each file has one target:

| Target | Files |
|---|---|
| A module, a test module or a subcommand of a master. `check-lock-free-asserts.py` is one of them: it becomes the rule module `lock-free-asserts`, which a unit of Stage 5 deletes | 96 |
| A family of the plugin, so the Python goes (section 18.10) | 30 |
| A data file: the registry, the pins, the rule table, the audit table, a ledger or `cmake/compile-first.txt` | 43 |
| A plant | 66: the 65 fixture files of the plugin test and the attack plant |
| A C++ tool, its build file, the row-hash golden or a patch, which stay | 15 |
| Data beside its reader (`LiveOracle.v`) | 1 |
| Deleted code: `check-atom-roster-joined.sh`, `preprocessed.py`, `repo_root.sh` and `tools_root.sh` | 4 |
| Deleted data: the 15 empty ledgers and allowlists, `fixy-only-paths.txt` and `survivors.txt` | 17 |
| Total | 272 |

Before Stage 1B, the scope has 108 commands, 83,903 lines of Python, 4,678 lines of shell, 22 data formats and 33 groups of copies. Section 18.17 gives the values after Stage 1B.

The 56 data files of `utils/scripts/`. A target `ledgers/NAME.txt` is the file `utils/ledgers/NAME.txt`:

| Files | Owner | Rows | Format today | Target |
|---|---|---|---|---|
| `compile-cpu`, `compile-instructions`, `compile-memory`, `function-size`, `header-alone`, `link-memory`, `link-time`, `object-text`, `test-memory`, `test-time` ledgers | `check-compile-cost.py`, the launchers, `check-test-time.py` | 0 each | `kind \| item \| value \| reason` | Deleted, because they hold no rows. The rules keep the policy `max` |
| `fixture-cpu`, `fixture-instructions`, `fixture-own-reason` ledgers | `test/neg_compile_store.py` | 0 each | `fixture \| note` | Deleted |
| `brand-drain.txt` | `check-brand-drain.py` | 101 | A path, a TAB and a count | `ledgers/erased-brand.txt` |
| `header-checks-ledger.txt` | `check-header-checks.py` | 64 keep rows | Count rows and keep rows | `ledgers/header-checks.txt` |
| `heavy-includes-ledger.txt` | `check-heavy-includes.py` | 49 | Count rows and keep rows | `ledgers/heavy-include.txt` |
| `padded-lists-ledger.txt` | `check-padded-lists.py` | 2 keep rows | Count rows and keep rows | `ledgers/padded-list.txt` |
| `libstdcxx-symbols-ledger.txt` | `check-libstdcxx-symbols.py` | 135 | Kind rows and symbol rows | `ledgers/artifact-symbols.txt` |
| `quarantine-ledger.txt` | `check-quarantine-ratchet.py` | 8,984 | Format 4, with configurations | `ledgers/quarantine.txt` |
| `quarantine-region-ledger.txt` | `check-quarantine-regions.py` | 8 | `FILE \| REASON` | The kind `region` of `ledgers/quarantine.txt` |
| `open-gate-reads.txt` | `check-trait-injection.py` | 142 | `template \| count [\| reason]` | `ledgers/open-gate-read.txt` |
| `parse-total`, `instruction-total`, `header-fanout` ledgers | `check-parse-cost.py` | 293, 1, 30 | Total rows, file rows, header rows | `ledgers/instruction-total.txt` |
| `budget-history-ledger.txt` | `check-budget-history.py` | 1 | `COMMIT \| ROW \| COLUMN \| REASON` | none |
| `contracts-baseline.json` | `audit-pre-callsite-count.py` | 1 object | JSON | none |
| The 19 allowlists | Their guards | 0 to 64 each, 295 in total | 9 formats with `:`, ` — `, `\|` or spaces | The rows of the ledgers of their rules. The 2 rows of `no-reserve` name `std::vector::reserve` calls of `bench/bench_mpsc_contention.cpp`, and they become admit rows of `banned`. Unit 1B.0 deleted the false row of `ExprPool::reserve`. The row of `no-spawn-process` names the ATen operator `clone` in a macro table of `vessel/torch/`. It becomes an admit row of `banned`. The 16 rows of `start-lifetime` count negative fixtures, and they go (decision 9) |
| `host-owner-roster.txt`, `witness-roster.txt` | Their guards | 4, 120 | ` — ` and `\|` | `home` and `witness` rows of the rule table |
| `process-wide-roster.txt`, `escape-doors.txt` | Their guards | 13, 3 | `\|`, two spaces | Ledger rows of their rules |
| `guard-engines.txt`, `budgets.txt` | `check-guard-engines.py`, `check_report.py`, `cmake/Budgets.cmake` | 114, 37 | `\|` | `utils/registry.txt`. The 114 `script` rows go (section 18.5) |
| `layer-rules.txt`, `quarantine-admitted-audit.txt` | The plugin, `layer_rules.py`, `check-admitted-audit.py` | 92, 68 | Keyword rows, `\|` | `utils/rule-table.txt`, `utils/admitted-audit.txt` |
| `header-costs.txt` | `check-heavy-includes.py` | 165 | `<header> \| seconds \| lines` | `heavy` rows of the rule table |
| `compile-first.txt` | `check-build-order.py`, `cmake/CompileFirst.cmake` | 88 | One name for each line | `cmake/compile-first.txt` |
| `fixy-only-paths.txt` | `check-banned-calls.py` | 8 | One directory for each line | Deleted. The constant `RUNTIME_DIRECTORIES` of the rule `banned` holds the 8 directories |

### 18.21 Defects that the inventory found

The inventory read each file, and it found these defects. The severities are high, medium and low. Unit 1B.0 lands each high and medium defect first, as a small correction of its own (decision 0 of section 18.23). A unit of Stage 1B corrects each low defect in the change that touches its file. The rows grammar of section 18.6 removes the class of the second defect from each file that it reads.

| File | Defect | Severity |
|---|---|---|
| `utils/tools/quarantine/test/check_plugin.py` | The plugin half of the test of the two table readers cannot fail. It asserts only `error: quarantine:`, and each malformed table also gives the error of an unclassified file | High |
| `plugin_core.h`, `layer_rules.py` | The two readers of the rule table divide words at different characters (Python divides at `\v`, `\f` and Unicode spaces). The C++ reader adds digits of a rank or an arity in an `int` with no overflow check, which is undefined behavior. The two readers do not refuse a second admit row for one entry. `facts.txt` sorts the rows, so a swap of two such rows changes the findings and not the stamp | Medium |
| `check_plugin.py` | The 19 parts start more than 130 compiler processes at one time | Medium |
| `check-banned-calls.py` | The reserve ban matches by name, so `ExprPool::reserve` is a false find. `src/` is not in the scope of the cast and reserve bans. Approximately 30 `SPAWN-PROCESS-OK` markers in `test/` have no effect, because the ban does not read `test/` | Medium |
| `check-lifetime-twin.py` | The scope does not include `include/crucible/`, which holds approximately 80 annotations | Medium |
| `check-ct-disassembly.py` | It compiles without the Release flags, and `-fharden-compares` can add a branch to a wrapper | Medium |
| `check-syscall-grant-coverage.py` | It discards the problems of `door_issues`, so an incorrect `_sys` helper in a door header gives no finding | Medium |
| `check-derived-pins.py` | It finds a name only in its own file, so a pin in a check file against a constant of its header passes | Medium |
| `check-detail-namespace.py` | A second allowlist row for one key replaces the first row with no error | Low |
| `check-host-owners.py` | A macro body with a class key at its end causes an `IndexError` | Low |
| `check-brand-drain.py` | The mode `--templates` causes an `AttributeError` | Low |
| `check-splits-orphan.py` | Its test for a `build*` directory also reads the file name, so the script does not read `build_plant.cpp` | Low |
| `check-no-throw-no-rtti.sh` | An exit of 2 becomes an exit of 1 in its loop on the libraries, and a weak undefined downcast passes | Low |
| `check-parse-cost.py` | A total row of 0 divides by zero | Low |
| `check-mint-pattern.py`, `mintmodel.py` | A bare `MINT-PATTERN-OK` with no reason admits a mint | Low |
| `check-no-ffast-math.py` | It does not refuse `-ffp-contract=on`, `-fno-trapping-math`, `-fno-signaling-nans` or `-fno-rounding-math`, which cancel the FP floor | Low |
| `audit-pre-callsite-count.py` | Four counters are 0 and no file uses their macros | Low |
| `check-clang-format.sh` | It is not in ctest, so no local test run fails on a format drift | Low |
| `check-vessel-acceptance.py` | No configuration of the local runs sets `TORCH_DIR`, so the full gate does not run | Low |
| `tsast.py` | `tracked_files` does not remove an exported `GIT_DIR` | Low |
| `test/CMakeLists.txt` | Each `ci_guard` test waits for the tree fill, also 12 tests that read no tree. `check_ct_disassembly` reads trees and does not get the fill | Low |

### 18.22 The migration

Stage 1B does the migration. Section 8.6a gives its units, their order, their parallelism and the end state that the S1 gate checks. This section gives the protocol that each unit obeys, and the evidence for the deletions.

**The parity protocol.** A unit that replaces a legacy module with a rule of the new system does these steps in one commit:
1. Write the plants of the new rule in `utils/plants/RULE/`: each shape that the legacy module refuses today, and a control.
2. Show that each plant is red on the legacy module and red on the new rule, and that each control is green on the two. Give the two runs in the report.
3. Run the legacy module and the new rule on the tree, and compare their verdicts. A difference is a finding of the legacy module that the new rule does not give, or the reverse. The report names each difference and its reason, such as a false find of the legacy module or a finer rule of the plugin.
4. Move the admitted items of the legacy module into the ledger of the new rule, through `assay.core.ledger`.
5. Delete the legacy module and its data files, and remove their `path` rows. Write the row of the deletion handoff.

A unit that moves code with no change of behavior (1B.1, 1B.5, and the reader units 1B.6a to 1B.6d) has a parity of its own. The same ctest tests pass before and after the unit, with the same names or with the names of a rename table in the report. Each check gives the same findings on the same tree. The report compares the warnings directory and the exit status of each test from the two runs.

**The parity today, for the deletions.** These facts hold at 5317d9bcd. They give the evidence for the deletions of table 18.19 in Stage 1B, and for the subsequent deletions of section 8.6a:

| Legacy guard or part | Replaced by | Parity evidence |
|---|---|---|
| The parts of the reserve ban and the spawn ban of `check-banned-calls.py` that read quarantined files | The rule `quarantine` (R6) | P1 and P7 are red. P6 is red too, but the stream ban stays as the kind `file-stream`, because the rule `quarantine` gives no finding for an `#include <fstream>` (section 8.6a). Of the 3 reserve rows, one names `ExprPool::reserve` and is a false find. The quarantine ledger holds the other two as rows `std::vector::reserve` of `bench/bench_mpsc_contention.cpp`. The parts that read base files and `vessel/` stay in the rule `banned` |
| In a subsequent stage: the kind `vector-reserve` of the rule `banned` | The include rules | No base file calls `reserve`. On 2026-10-09, the 47 base files that include `<vector>` are 47 `layer-header` rows of the quarantine ledger. When Stage 4 removes the last of them, the include rules refuse the header in each base file |
| In a subsequent stage: the rule `lock-free-asserts` | The rule `quarantine` and `foundation::core::Atomic` | P9 is red. The quarantine ledger holds the 27 objects of today in 6 rows. `Atomic<T>` refuses a type that is not lock-free. Stage 5 deletes the rule when the 27 objects move to `fixy::Atomic` |
| `check-atom-roster-joined.sh` | The walk unit of `test/layer/checks/fixy/Collision.cpp` | `walk-checks.txt` row 69 lists the file, and its lines 24 and 25 hold the two checks of the script. Two negative fixtures are the new witness |
| In a subsequent stage: the claims of the 34 quarantined rows of the rule `syscalls` | The rule `quarantine` (`c_library_call`) | P4 is red. Each of the 34 quarantined rows has a `c_library_call` row for the same function in the same file in the quarantine ledger. So a unit of Stage 5 that moves such a call into an Os door removes the two rows. In Stage 1B, the rule keeps each claim |
| The include half of `check-layer-boundary.py` | The include rules of the plugin | P11 is red. The plugin is stricter: the quarantine ledger holds 19 `upward_include` rows, and the script, with three coarse layers, gives no finding |
| The specifier half of `check-contract-form.py` in compiled files | The contract rule of the plugin | P8 is red in each mode. The 6 rows of `contract-form-allowlist.txt` name a probe that `execute_process` compiles, three fixtures and two plugin tests, which no counted compile reads. So the parse-tree rule keeps only the remaining part |
| The quarantined part of `check-start-lifetime.py` | The rule `quarantine` | P3 is red. The quarantine ledger holds no `std::start_lifetime_as` row, and the 16 allowlist rows are negative fixtures |

**The deletion handoff.** At its end, each unit gives a list to the owner. The list gives each file that the unit deleted, the rule that replaced it, the plants and the parity runs. The column "Deletes" of the table of section 8.6a gives the deletions that each unit hands off.

### 18.23 Decisions

The owner approved each decision of this table on 2026-10-03 (D15). The first column gives the subject, and the second column gives the decision.

| Subject | Decision |
|---|---|
| 0. The schedule of the units | Stage 1B, a part of Stage 1 (section 8.6a). Unit 1B.0 lands the high and medium defects of section 18.21 first, each as a small correction of its own. Unit 1B.1 starts at its sync point after unit 1e lands, because it moves each path while other units land patches. 1B.1, 1B.2 and 1B.3 are serial, and the other units obey the order of section 8.6a. The S1 gate includes the end state of Stage 1B, so Stage 2 starts only after the last unit of Stage 1B |
| 1. A fifth engine, `meta`, for the rules about the utils system and the repository | Yes. Nine checks read no C++ and no build output |
| 2. The rule table stays a separate file from the registry | Yes. The rule table is an input of each compile, with a stamp that compiles each object again when a fact changes. A change of a threshold or a text of the registry must compile no unit |
| 3. The kinds of the plugin get names with hyphens, and unit 1B.7 writes the quarantine ledger again | Yes. Then one spelling names each rule in the registry, the findings, the ledgers and the tests |
| 4. A plugin rule with a base scope walks a base header only in its sentinel unit, which the plugin argument `sentinel=PATH` identifies (section 18.3) | Yes. Each header then costs one walk in each build |
| 5. Code that no counted configuration compiles: `vessel/torch` with no `TORCH_DIR`, and the arms that no build selects | Keep a parse-tree rule only where such code can cause damage: `contract-form`, `layer-residual` and the vessel part of `banned`. Accept the gap for the other plugin rules, or add a configuration with `TORCH_DIR` to the ledger |
| 6. `reinterpret_cast`: a ban of the keyword on the parse tree, or a patch of the toolchain that marks each conversion of the cast | The parse tree at this time. The probe shows that GCC puts its mark only on a cast from one pointer type to another |
| 7. The comment markers (`*-OK`) become ledger rows with reasons | Yes. Four markers have no use, and the three reason rules of today do not agree |
| 8. The regions in code that no build compiles, such as the fixtures of the plugin test | The ledger counts only the regions of compiled code. The plugin test holds the regions of its fixtures |
| 9. The 16 allowlist rows of negative fixtures in `start-lifetime-allowlist.txt` | Delete them. A fixture must fail to compile, so the rows give no protection |
| 10. GCC patches that give the plugin a hook in `finish_static_assert` and in the reflection evaluator. The header checks and the walk units can then move into the plugin | Not at this time. First, measure the part of their work that the parse tree cannot do |
| 11. One ledger file for each rule, or one for each engine | One for each rule. A merge conflict then touches one rule |
| 12. No directory of `utils/` has the name `build`, because `.gitignore` matches `build/` at each depth. The engine directory of the build rules is `assay/guard/engines/buildsys/` | Yes |
| 13. The rule `use-after-move` in the plugin, on GENERIC at `PLUGIN_PRE_GENERICIZE`, before `cp_fold_function` removes the calls of `std::move` | Not at this time. GENERIC has no control-flow graph at that hook, so the rule is a new path analyzer. It must model the branches, the loops with `break` and `continue`, `goto`, `return` and the cleanup points. It must also model the statement expressions, the lambdas, and the assignment that gives a moved local a value again. The parse-tree rule holds that logic in 1,801 lines today, and its attack test pins 5 misses. Keep the parse-tree rule until a measured miss makes the work necessary |
| 14. The name of the package | `assay`, in `utils/assay/`. An assay is the test of the purity of a metal, which is the job of a crucible in a laboratory. The package does the tests and the measurements of the tree. The word is short, and the tree uses it for no other item. The alternative is `crucible_utils`, which is longer and has the same meaning in two words |
| 15. The number of masters | Three: `guard`, `loop` and `setup`. A fourth master only for the step meters is not necessary. The lazy dispatch of `loop step` costs 10.33 ms at p50, and `build-launcher.py` costs 11.51 ms today (section 18.2) |
| 16. The type checker and the linter | mypy in strict mode, and ruff for the lint and the format. The rules `types` and `python-lint` are in the registry from unit 1B.4. In the same unit, each tool gets a row of `utils/pins.txt` with its version and its SHA-256, which pip checks with `--require-hashes`. mypy is a Python package with compiled wheels, and Node.js is not necessary for it. Node.js is necessary for pyright, and no pin holds it, so this design has no measurement of pyright. On the scope, mypy 2.4.0 with `--strict` used 3.6 s cold and 0.11 s warm, and it gave 629 errors. ruff 0.16.4 used 0.08 s, and it gave 527 findings |
| 17. The pinned version of Python | 3.14. The host has 3.14.7, and the build container `fedora:44` of CI has 3.14. The package uses deferred annotations, `type` aliases, `StrEnum` and dataclasses with slots, so no `from __future__` import is necessary. Each master refuses an older Python with a message. The CI job `guards` runs on `ubuntu-latest`, which has an older Python, so its YAML gets a step that installs 3.14. No landing waits for that job (D11). No module uses the free-threaded build, and the fill of the tree cache keeps its processes |
| 18. The place of the step meters | In `loop`, as the hidden subcommands `step`, `meter-compiler`, `test-step`, `fixture` and `stamps`. They use lazy dispatch, `-S`, no `argparse` and a constant import closure. The measurement of section 18.2 gives the reason |
| 19. One grammar for the rule table, the registry, the ledgers, the pins and the audit table | Yes, with the C++ reader in `rows.h`, which holds no GCC header, and the meta rule `rows-readers` on a shared corpus. The grammar removes the three faults of the medium defect of the two readers in section 18.21 |
| 20. The tests of the package in `assay/tests/`, separate from the modules, and the plants of the rules as data in `utils/plants/` | Yes. The test groups remove approximately 80 starts of Python from each run (E). Each rule keeps its plants in one directory with the name of the rule |
| 21. No shell script in `utils/`, and the bench and AFL++ drivers in `loop` | Yes. Python can run at each place where a shell script runs today. `loop bench` and `loop fuzz` replace `bench/run-bench.sh` and `test/fuzz/run-afl.sh`, so each driver of a tool uses the step meter, the pins and the reports of the package. Unit 1B.5 deletes the two drivers |
| 22. The legacy modules during the migration | Yes. Unit 1B.1 moves each guard script with no change of behavior, so the tree has the three masters from the first unit. Each subsequent unit replaces legacy modules through the parity protocol, and unit 1B.9 removes the empty directory |

---

## 19. Sync points

| Id | When | What every unit does |
|---|---|---|
| S0.1 | unit 0a lands the Debug flag change | Merge main. Rebuild before the next landing (every Debug object compiles again one time) |
| S0.2 | unit 0b lands the check-file convention | The 0c units start |
| S0 | Stage 0 gate | All Stage 0 units have landed or stopped. The gauge runs again |
| 1B.1 | unit 1B.1 moves each Python file of the scope into the package `assay`, after unit 1e lands | Every running unit merges main before its next landing, because each path of a guard, a ledger reader, a shared module and a launcher moves. A command of the previous paths becomes a subcommand of `guard`, `loop` or `setup` |
| 1B.3 | unit 1B.3 changes the ledger format | Every unit that writes a ledger merges main, and writes the ledger again with `guard write` |
| 1B.7 | unit 1B.7 changes the reader of the rule table, the section format and the kind names of the plugin | Merge main. Rebuild before the next landing (the stamp of the plugin changes, so every object compiles again one time) |
| S1 | Stage 1 gate, with the end state of Stage 1B | The ratchet is live. From here each landing must pass it. The utils system of section 18 is complete, and Stage 2 starts |
| S2 | Stage 2 gate | The include policy of the base is an error |
| S2.x | a 2e split lands | Units that include the split header merge main before their next landing |
| S3 | Stage 3 gate | The families are on main. The extension lane opens |
| S4.x | slice 4.1 or 4.2 lands | Every running unit merges main before its next landing, because these slices touch many files |
| S4 | Stage 4 gate | No std type in the base surface |
| S5.x | a directory switches to error | Units that touch that directory merge main first |
| S5 | Stage 5 gate | Error mode in every quarantined directory |
| S6 | final gate | The migration is closed |

---

## 20. Parallelism and resources

### 20.1 The host

From 2026-10-16 the host is a virtual machine on an EPYC 9754: 256 vCPUs and 127 GB of memory. The vCPUs 224 to 255 are a reserved partition for benches, so normal work has 224 vCPUs. The owner does not share this machine. Until 2026-10-16 the host was a shared 2 x EPYC 9655 (384 threads) with 503 GB of memory. One heavy translation unit uses 0.6 GB to 2 GB of compiler memory, and one generated oracle file used 15 GB before unit 0d shards it. A guard run builds a subset of the tree two times.

### 20.2 Budgets

| Situation | Units at the same time | Jobs for each unit |
|---|---|---|
| one unit alone on the host | 1 | one compile job for each logical core of the host, and half as many link jobs |
| a wave of heavy units | 1 at a time, under one shared lock | one compile job for each logical core of the host and half as many link jobs for a build, and one job for each processor for ctest. The owner set 224 compile jobs on 2026-10-17. The default was 128 from 2026-10-16, on the new host, 16 from 2026-10-09, when large virtual machines shared the old host, and 96 from 2026-10-02 |
| a wave with read-only units | the heavy limit plus any number of read-only units | read-only units run no build |

The host gives each job count. `cmake/JobPools.cmake` gives each build a Ninja pool compile with one job for each logical core of the host, and a pool link with half of them. Ninja runs no more edges of a pool at one time, also with a larger `-j`. `DEFAULT_JOBS` of `utils/assay/core/proc.py` is the CPU count of the process, and the presets of `CMakePresets.json` take the host count. From 2026-10-17, each link of the tree uses mold 3.0.0 with the threads of the rule `build-graph`, in place of GNU ld. Before each heavy command, run `free -g`. Wait while the "available" column is below 64 GB. On 2026-10-16 a cold Debug build at 128 jobs used at most 56.4 GB at one time. On 2026-10-17 the cold builds at 224 compile jobs used at most 60.7 GB at one time.

### 20.3 What can run together

| Stage | Parallel | Serial |
|---|---|---|
| 0 | 0a, 0b, 0d, 0e, 0f, 0g. Then the 0c units. Then the 0h units | 0c after 0b (S0.2). 0c-R after 0e. 0h after every 0c |
| 1 | 1a, 1b, 1d. Then 1c, 1e. Then the units of Stage 1B | 1c after 1a. 1e after 1b. 1B.1 after 1e |
| 1B (section 8.6a) | 1B.5 beside 1B.3. 1B.6a and 1B.7 after 1B.3. 1B.6b to 1B.6d after 1B.6a. 1B.8a to 1B.8d and 1B.8f after 1B.7. 1B.8g to 1B.8j after the units that section 8.6a names | 1B.0 first. 1B.1 after 1e, at its sync point. 1B.1, 1B.2, 1B.3 in order. 1B.4 after 1B.3 and 1B.5. 1B.8e after 1B.8d. 1B.9 after 1B.8a to 1B.8j. 1B.10 last |
| 2 | 2a, 2b, 2c. Then the 2e groups | 2d after 2a, 2b, 2c. 2e after 2d |
| 3 | inside G2 and G3 | G1, G2, G3, G4, G5, G6 in order |
| 4 | 4.3, 4.4, 4.5, 4.7. Then 4.8 and the 4.9 units | 4.1 then 4.2. 4.6 after 4.1 |
| 5 | the 5B units. 5C with 5B | 5A first. 5D last |

### 20.4 Conflicts

- A unit owns its directories. It edits a file outside them only through a `--replace` pair, or after the owner of that file lands.
- When two units need the same file, the coordinator decides the order before the second unit starts.
- A unit that finds a file in another unit's open tree stops and asks the coordinator.

---

## 21. Risks

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
| A raw system call is invisible to a sanitizer | TSan reports a false race, a raw blocking call hangs under TSan, ASan misses a bad buffer | R18: annotations in one code path, and the intercepted call only for a blocking call under TSan |
| A host turns `io_uring` off | The I/O door fails on that host | The plain-syscall fallback of G6, chosen by a probe at start-up |
| A raw system call is missing on an old kernel | `ENOSYS`, because glibc gave a fallback and the door gives none | The minimum kernel version of R4, checked at start-up |
| An aarch64 defect stays hidden | No local build compiles or runs an object of aarch64, so a defect of the code shows only after a daily sync | One person reads the run after each sync (D11). The parse build of the preset `syntax-aarch64` gives each aarch64 arm to the plugin with a cross compiler, and the ratchet counts its findings (CLAUDE.md §XXII "The ratchet") |
| A wrong futex protocol | Lost wakeups or wakeups with no cause in the base waits | One futex door, tested under load and under TSan |

### 21.1 Options that the owner decided against (2026-10-03)

| Option | Why not |
|---|---|
| A general-purpose `malloc` of our own on `mmap` | Years of tuning to match glibc or jemalloc, and ASan loses its heap checks. The allocation door of G3 keeps the option open at no cost |
| A vDSO parser of our own | glibc `clock_gettime` already reads the vDSO, and the hot path reads `rdtsc` |
| Threads from `clone`, or a TLS set-up of our own, inside the process | glibc keeps a thread block for each thread, and any glibc call on a thread that `clone` made crashes. The vessel shares the process with Python and PyTorch (D13) |
| `-fno-exceptions` for the base before Stage 4 | libstdc++ headers change their code when exceptions are off. While a base header includes run-time libstdc++ code, one inline function gets two definitions (an ODR risk). The decision point DP3 stays in Stage 6 |
| A different code path for sanitizer builds | The sanitizer builds would then test a path that production does not run. R18 uses annotations |
| User-space drivers through VFIO for shared devices | VFIO takes the device away from its kernel driver. Only a device that Crucible owns alone qualifies (D14). NVMe passthrough through `io_uring` covers most needs |
| AF_XDP | Out of scope: the networking scope is the scope of NCCL (D14) |

**Stop criteria:**
- The S0 gate misses its targets after every Stage 0 unit has landed, and the owner declines the modules spike (DP1).
- A family cannot meet zero hot-path cost for an operation on a hot path.
- The gauge shows a slowdown of more than 10% at a gate, with no fix in the same stage.

In each case, the stage stops and the coordinator reports to the owner with the numbers.

---

## 22. Checklists

### 22.1 Before a unit starts

- [ ] The brief names the unit's directories and files.
- [ ] The brief names the stage, the sync points that affect the unit, and the job budget.
- [ ] The brief repeats sections 5.1 to 5.6.
- [ ] No other open unit owns the same files.

### 22.2 Before a unit lands

- [ ] Main merged, Debug build of `all` passes.
- [ ] The unit's tests pass. For a base header change, the full suite passes.
- [ ] Release build and the unit's Release tests pass.
- [ ] Guard run: "red only with the change: none".
- [ ] `loop derive --check` and `guard check --rule clang-format` pass. Before unit 1B.5 lands, the commands are `refresh-derived.sh --check` and `check-clang-format.sh`.
- [ ] The ratchet passes (from Stage 1).
- [ ] Each new test and fixture goes red on the old code.
- [ ] Shared files go in as `--replace` pairs.

### 22.3 After the last landing

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

---

## Appendix E. The header map

This appendix gives each header of `include/foundation` and `include/fixy` a verdict, a family and a slice of section 13.1. The census reads main at d36be7daf on 2026-11-12. The two directories hold 264 headers and 76,285 lines. The language files (`include/foundation/core/*.h` and `include/fixy/Core.h`) hold 2,382 of these lines.

### E.1 The open question for the owner

The older half of fixy is the graded wrappers, the collision catalog, the sessions and the permissions. Do we rebuild it on the core families, or do we keep it? No delete row becomes work before the owner answers, and each delete row is a proposal.

| Group | Headers | Lines | Keep rows: headers, lines, std findings | Rebuild rows: headers, lines, std findings |
|---|---|---|---|---|
| Graded wrappers | 51 | 10,934 | 43, 7,891, 228 | 8, 3,043, 206 |
| Collision catalog | 27 | 7,992 | 27, 7,992, 394 | 0, 0, 0 |
| Sessions | 36 | 22,367 | 20, 13,309, 360 | 16, 9,058, 481 |
| Permissions | 16 | 4,622 | 7, 908, 76 | 9, 3,714, 152 |
| Total | 130 | 45,915 | 97, 30,100, 1,058 | 33, 15,815, 839 |

A std finding is a finding of a base kind or of a kind `replace-pending:` of the rule `quarantine`. The four groups are these headers:
- The graded wrappers: `foundation/algebra/`, and Qtt, Refined, Tagged, Secret, Stale, Bands, Mutation, EpochVersioned, Budgeted, GradedFacade, Aliases, CanonicalOrder, Predicates and SelfContained of `fixy/`
- The collision catalog: `fixy/atoms/`, `fixy/collision/`, and Fn, Axis, Atom, Collision, Corpus, Reject, Insights, Role and Throws of `fixy/`
- The sessions: `fixy/session/`
- The permissions: `foundation/permissions/`, `fixy/channels/`, and PermissionFork, PermissionedSpscChannel and PermissionedMpscChannel of `fixy/concurrent/`.

The two answers have these costs:
- **Keep.** The older half keeps its design. Stage 4 rebuilds the 33 headers that hold a std type at run time. The remainder slices change the std names of the 97 keep headers to the compile-time forms of Region and Record. The row-hash golden moves only where a reflected name of a type changes (section 13.3).
- **Rebuild.** Each of the 130 headers gets a new implementation on the families. Examples are the graded carriers on Record, the protocols on the type lists of Record, and the permission brands on the brands of the Ref family. The work touches 45,915 lines and each caller. 165 production files reach `fixy/Refined.h` through their includes, and 30 production files reach `foundation/permissions/Permission.h`. The row-hash golden, the session-oracle golden and the fixtures of the four groups change.

The recommendation is to keep the older half, with the rebuild rows of E.3. These are the reasons:
1. The families are the value vocabulary of the base: a choice, a region, an owner, an atomic cell, a scalar, a report and a door. The older half is the type vocabulary: a grade, an effect row, a protocol and a permission tree. No family gives a lattice, a projection or a split manifest.
2. 97 of the 130 headers hold no std type at run time. Their std use is reflection results, the texts of a refusal, type lists, tags and std functions that a slice changes one for one. A remainder slice changes these names, and the design does not change.
3. The 33 headers that hold a std type at run time are rebuild rows in the two answers, because the goal of Stage 4 applies to each base header.
4. Two goldens pin the behavior of the older half. `test/session_oracle/golden.csv` pins the answers of published mechanized proofs of the session relations, and `utils/tools/row_hash_golden.txt` pins the row hash of each carrier. A new implementation must give the same answers again.
5. The brand and borrow design of `misc/12_10_2026_linear.md` moves the brands into the core in its phases B to D. That design changes the permissions one slice at a time, where a family gives a better form. A rebuild of the whole group is then not necessary.

Three smaller questions:
- The collision catalog reaches production through one binding, in `vessel/torch/record_kernel.h`. The recommendation is to keep it. The map gives it remainder work only.
- `SetOnce` of `fixy/handle/Once.h` has the members of `WriteOnceNonNull` of `fixy/Mutation.h`. The proposal is to merge the two in slice 4.4.
- `fixy/os/CipherDurable.h` holds the durable writers of the Cipher, but no production file includes it, because `include/crucible/Cipher.h` writes through `fixy/os/Fs.h`. Does the base keep it, or does it move to `include/crucible/cipher/` when the Cipher uses it?

### E.2 The verdicts

- **Keep.** The header keeps its role, its public API and its data. Its std use is in a constant evaluation only: a reflection result, a name text, a type list or a constant table. A std function that a slice changes one for one is a keep use too, such as `std::exchange`, `std::bit_cast` or `std::popcount`. An empty tag type, such as `std::in_place_t`, is a keep use too. The slice of a keep row changes only these names. A dash in the slice column tells that the header has no std finding.
- **Rebuild.** The header keeps its role, but a std type or a C library call is in a public signature, in a stored member or in code that runs at run time. A header whose API a slice text or a merge row changes is a rebuild row too, such as `fixy/CyclicBuffer.h` and `fixy/session/Witnessed.h`. The slice gives the header a new implementation on the family, and each caller changes with it.
- **Merge.** A family type or a different header gives the same thing. The slice moves each caller to the target and removes the header. The core does not hold the target of a merge row at this time. Each target is a planned part of a Stage 3 group.
- **Delete (proposal).** A second holder gives the same thing. Nothing happens before the owner approves the row, and slice 4.11 has no wave until then (section 13.2).

The family column names the families of section 11. The findings of each header and the rows `until FAMILY` of `utils/rule-table.txt` give them. Os names a door of the Os family.

The run-time test reads the library records of the quarantine plugin (the records `inlined`, `call`, `reference` and `symbol`) and the source of each header. A call that GCC adds itself, such as `memcpy` or `__stack_chk_fail`, does not count. A record that the payload of a caller makes does not count either, as in `fixy/Tagged.h` and `foundation/algebra/Graded.h`. The findings come from `guard list` on the main build, which can be older than d36be7daf. So no finding count is the reason of a row.

### E.3 The map

A group row names its directory, its members and the count of its members.

| Headers | Verdict | Family | Slice | Reason |
|---|---|---|---|---|
| `foundation/core/Atomic.h`, `foundation/core/Choice.h`, `foundation/core/Format.h`, `foundation/core/Ref.h`, `foundation/core/Region.h`, `foundation/core/Report.h`, `foundation/core/Scalar.h`, `foundation/core/Text.h`, `fixy/Core.h` (9) | Keep | the families | – | The language files. Stage 3 adds each part of a family that is not on main. The one finding, `std::nothrow` in `Ref.h`, goes with the allocation door of G3 |
| `foundation/`: Platform, Pinned, Quarantine, Brand, ByteSeal, Linear, NoObject (7) | Keep | – | – | The floor and the markers that each layer reads. No std finding |
| `foundation/Lifetime.h` | Rebuild | Region | 4.3 | `start_as_array` gives a `std::span` at run time |
| `foundation/Saturate.h`, `fixy/Saturate.h`, `fixy/Saturated.h`, `fixy/Checked.h` (4) | Merge | Scalar (G2, planned) | 4.10 | The saturating and the checked operations belong to the Scalar family. The checked operations give an `Option` |
| `foundation/AlignedBuffer.h` | Merge | Region (G4, planned) | 4.3 | An owned allocation at a chosen alignment is a heap array of the Region family. Section 10.3 gives its defect: the heap bytes have no initial value |
| `foundation/SwissTableBuffer.h` | Merge | Region (G4, planned) | 4.3 | The storage of an open-addressing table belongs to the table of the Region family |
| `foundation/Simd.h` | Rebuild | Region, Scalar | 4.3 | The loads and the stores take a `View`. `memcpy` and `std::bit_cast` go to the copy of Region and to Scalar |
| `foundation/ThreadLocalRef.h` | Keep | Region | 4.8 | The std use is the name text of the cell |
| `foundation/ChannelBinding.h` | Rebuild | Atomic, Ref | 4.4 | The binding is a `std::atomic` pointer at run time |
| `foundation/algebra/ClaimOrientation.h` | Keep | – | – | No std finding |
| `foundation/algebra/`: Graded, GradedTrait, Lattice, Modality (4) | Keep | Region | 4.8 | The substrate of each graded wrapper. The std use is the name text, the reflection results of the law checks and `std::swap` |
| `foundation/algebra/lattices/`: AllocClassLattice, CipherTierLattice, ConfLattice, DetSafeLattice, HotPathLattice, LifetimeLattice, PinningRequirementLattice, ResidencyHeatLattice, SchedulerPolicyLattice, SuspendBehaviorLattice, ToleranceLattice, WaitLattice (12) | Keep | – | – | Chain lattices with no std finding |
| `foundation/algebra/lattices/`: BarrierStrengthLattice, BoolLattice, ChainLattice, ClockSourceLattice, DualLattice, MemoryScopeLattice, NumaNodeLattice, ProductLattice, QttSemiring, RecipeFamilyLattice, TrustLattice, VendorLattice (12) | Keep | Region | 4.8 | The std use is the name text of the lattice, or a reflection result in a constant evaluation |
| `foundation/algebra/lattices/`: SeqPrefixLattice, StalenessSemiring, FractionalLattice (3) | Keep | Scalar, Region | 4.10 | The bounds of `std::numeric_limits`, `std::gcd` and the name text |
| `foundation/algebra/lattices/AffinityLattice.h` | Rebuild | Region, Scalar | 4.8 | The mask is a `std::array` of words at run time |
| `foundation/algebra/lattices/`: HappensBefore, StrongCounterLattice (2) | Rebuild | Region, Choice, Scalar | 4.8 | A count or a clock is a `std::array` at run time, and the read of an image gives `std::expected` |
| `foundation/algebra/lattices/MonotoneLattice.h` | Rebuild | Ref, Scalar | 4.8 | `std::less` and `std::greater` are the comparators of the public template, and they run at run time |
| `foundation/algebra/lattices/EnumValuePins.h` | Keep | Region | 4.8 | The pin tables of its check file. The `std::array` tables are constant data |
| `foundation/contracts/`: Armed, Pre, Post (3) | Keep | – | – | No std finding |
| `foundation/contracts/ArmedRoster.h` | Keep | Region | 4.8 | The walk of the check files. The std use is the reflection results of a constant evaluation |
| `foundation/contracts/Decide.h` | Rebuild | Region | 4.3 | `intervals_pairwise_disjoint` takes a `std::span` at run time |
| `foundation/diag/tags/`: Aborts, Rows, Tiers, Wrappers (4) | Keep | – | – | The prose of the tags. No std finding |
| `foundation/diag/`: Catalog, FailClosed, Insights, RowHash, Tag (5) | Keep | Region, Record | 4.8 | The std use is the text, the tables and the reflection results of a constant evaluation |
| `foundation/diag/RowMismatch.h` | Keep | Report, Region | 4.5 | A constant evaluation builds and reads the block. Its `std::source_location` goes to `Site` |
| `foundation/diag/Runtime.h` | Rebuild | Report | 4.5 | The violation sink takes `std::string_view` and `std::source_location` at run time |
| `foundation/diag/JsonEmitter.h` | Rebuild | Report, Region | 4.5 | The emitter reads `std::string_view` at run time |
| `foundation/effects/`: Computation, ImageRead (2) | Keep | – | – | No std finding |
| `foundation/effects/`: Capability, Concurrent, Effect, Lift, Resources, Row (6) | Keep | Region, Scalar | 4.8 | The std use is the name text, the tables and the reflection results of a constant evaluation |
| `foundation/effects/Ctx.h` | Rebuild | Atomic | 4.4 | The host doors hold `std::atomic` flags at run time |
| `foundation/os/`: Abort, Auxiliary, Directory, Errno, File, FutexWords, GuardedRegion, KernelFloor, MapWords, MemoryPolicyWords, OpenFlags, PathText, Poison, SchedWords, Syscall (15) | Keep | Os | – | The floor and the doors of the Os family. No std finding |
| `foundation/permissions/`: FederationDoor, Fwd (2) | Keep | – | – | No std finding |
| `foundation/permissions/PermSet.h` | Keep | Region | 4.8 | The set operations run in a constant evaluation |
| `foundation/permissions/`: Permission, PermissionFork, ReadView (3) | Rebuild | Record | 4.7 | The split, the fork and the loan give or hold a `std::pair` or a `std::tuple` of tokens at run time |
| `foundation/permissions/SharedPermission.h` | Rebuild | Atomic, Choice | 4.4 | The pool counts its shares in a `std::atomic`, and a lend gives `std::optional` at run time |
| `foundation/reflect/Anchor.h` | Keep | – | – | No std finding |
| `foundation/reflect/`: EnumPins, Hash, Instance, Signature, TypeComponents (5) | Keep | Region, Record | 4.8 | The std use is the text and the reflection results of a constant evaluation |
| `foundation/reflect/`: EnumName, Enumerate (2) | Rebuild | Region | 4.3 | The name of an enumerator and `bits_to_string` give `std::string_view` at run time |
| `foundation/reflect/RawEscape.h` | Delete (proposal) | – | 4.11 | The rule `escape-doors` of the quarantine plugin overlaps it: the rule reads each public function of a base header. This header also refuses a public data member, and a member that a public base or a using-declaration gives. First give the rule these two cases with plants, then delete the header and its six fixtures |
| `fixy/`: Aliases, Ctx, Tags, Tagged, Throws (5) | Keep | – | – | No std finding. The library records of `Tagged.h` come from a payload of a caller |
| `fixy/`: Qtt, Secret, Stale, Machine (4) | Keep | Choice | 4.9 | The std use is the tag `std::in_place`, and in `Secret.h` the constant table of the policies |
| `fixy/`: Bands, GradedFacade, CanonicalOrder (3) | Keep | Region, Record | 4.9 | The std use is the name text, the type lists and the reflection results of a constant evaluation |
| `fixy/Mutation.h` | Rebuild | Region, Atomic, Choice, Ref | 4.3, 4.4 | `AppendOnly` stores a `std::vector`, `WriteOnce` a `std::optional` and `AtomicMonotonic` a `std::atomic`. `Monotonic` takes `std::less` by default |
| `fixy/Refined.h` | Rebuild | Choice, Region | 4.2 | `admit_refined` gives `std::expected`, and `NonEmptySpan` refines a `std::span` |
| `fixy/EpochVersioned.h` | Rebuild | Choice, Ref | 4.2 | The version gate and the combine give `std::expected` at run time, and the replace of a payload calls `std::construct_at` |
| `fixy/Budgeted.h` | Keep | Ref, Choice | 4.9 | The std use is `std::construct_at` and `std::destroy_at` in the replace of a payload, and the tag `std::in_place_t` |
| `fixy/Predicates.h` | Keep | Region, Ref | 4.9 | A constant evaluation reads the implication relation. The one std use at run time is a `std::bit_cast` of a pointer, which goes to the address of the Ref family |
| `fixy/SelfContained.h` | Rebuild | Region, Choice, Record, Scalar | 4.9 | The gate runs in a constant evaluation, but `equal_by_members` reads `std::optional`, `std::variant` and a range through `std::ranges` and `std::bit_cast` at run time |
| `fixy/Bits.h` | Keep | Scalar, Region | 4.10 | `std::popcount` and the name text |
| `fixy/`: Borrowed, ScopedView, OwnedRegion, SharedRegion (4) | Merge | Region and Ref (G3, G4, planned) | 4.3 | A borrow with an owner tag and a brand is a `View` or a `Ref` of the core, with brands |
| `fixy/FixedArray.h` | Merge | Region (G4, planned) | 4.3 | The core gives the fixed array |
| `fixy/ConstantTime.h` | Rebuild | Region | 4.3 | The primitives take `std::span` at run time |
| `fixy/SipHash.h` | Rebuild | Region, Scalar | 4.3 | The key and the message are `std::array` and `std::span` at run time, and a round calls `std::rotl` |
| `fixy/Federation.h` | Rebuild | Region, Record | 4.3, 4.7 | The admission holds `std::array` nonces and gives `std::pair` at run time |
| `fixy/Cyclic.h` | Keep | Region | 4.9 | The std use is the name text |
| `fixy/CyclicBuffer.h` | Rebuild | Region | 4.3 | A read indexes through an index type of the Region family |
| `fixy/`: Atom, Axis, Collision, Corpus, Reject, Role, Fn, Insights (8) | Keep | Region, Record, Choice | 4.9 | The gate of `fn` runs in a constant evaluation. The std use is the text of a refusal, the type lists and the reflection results |
| `fixy/atoms/`: Barrier, Ctrl, Dispatch, Fp, Global, Hw, Observe, Os, Regime, Scope, Session, Simd, Stack, Stdio, Sync, Syscall (16) | Keep | Record, Region | 4.9 | The rosters of the atoms are `std::tuple` type lists and constant tables |
| `fixy/collision/`: Codes, Report (2) | Keep | Region | 4.9 | The codes, the corpus and the checks run in a constant evaluation. Only the check files include `Report.h` |
| `fixy/channels/`: EndpointMint, SubstrateSessionBridge (2) | Keep | – | – | No std finding |
| `fixy/channels/StageEndpointBridge.h` | Keep | Record | 4.7 | The `std::tuple` is a type list |
| `fixy/channels/`: Endpoint, SwmrSession (2) | Rebuild | Choice | 4.2 | A receive gives `std::optional` at run time |
| `fixy/channels/LazyEstablishedChannel.h` | Rebuild | Atomic, Choice, Report | 4.4 | The channel holds a `std::atomic`, and its mint gives `std::expected` and takes `std::source_location` |
| `fixy/concurrent/`: ParallelismRule, PermissionFork, RingValue, StageShape (4) | Keep | – | – | No std finding |
| `fixy/concurrent/HandleTraits.h` | Keep | Choice | 4.9 | The `std::optional` is in the detection of a member, in a constant evaluation |
| `fixy/concurrent/PayloadRow.h` | Keep | Region | 4.9 | The payload walk runs in a constant evaluation |
| `fixy/concurrent/WorkingSet.h` | Keep | Scalar | 4.10 | `std::numeric_limits` |
| `fixy/concurrent/`: AtomicSnapshot, SpscRing, PermissionedSpscChannel, PermissionedMpscChannel (4) | Rebuild | Choice, Region | 4.2 | A ring or a channel of the hot path. A pop or a load gives `std::optional`, and the cells of a ring are a `std::array`. Compare the disassembly (13.3) |
| `fixy/concurrent/MpscRing.h` | Rebuild | Atomic, Choice, Region | 4.2, 4.4 | A ring of the hot path. The cells are a `std::array`, the ready bits are a `std::atomic` and a pop gives `std::optional`. Compare the disassembly (13.3) |
| `fixy/concurrent/ChaseLevDeque.h` | Rebuild | Atomic, Choice, Region | 4.4 | The relaxed and the seq_cst operations stay as cited detail code. A steal gives `std::optional` |
| `fixy/concurrent/Pipeline.h` | Rebuild | Region, Record, Atomic | 4.7 | The runner holds `std::vector` lists of CPUs and a `std::tuple` of stages at run time |
| `fixy/concurrent/`: Spawn, Stage (2) | Rebuild | Record, Region, Choice | 4.7 | The fork and the stage hold a `std::tuple` or a `std::array` at run time |
| `fixy/fp/Canonicalize.h` | Keep | Scalar | 4.10 | `std::bit_cast` and `std::isnan` |
| `fixy/fp/Polynomial.h` | Rebuild | Scalar, Region | 4.10 | The coefficients are a `std::array` at run time, and the evaluation calls `std::fma` and `std::bit_cast` |
| `fixy/handle/Once.h` | Rebuild | Atomic | 4.4 | The gate holds a `std::atomic`. `SetOnce` has the members of `WriteOnceNonNull` of `fixy/Mutation.h`. A proposal: merge the two in this slice |
| `fixy/handle/`: OneShotFlag, PublishCommit, PublishOnce (3) | Rebuild | Atomic, Report | 4.4 | A `std::atomic` at run time. `PublishOnce.h` also reports a second publish with `fprintf` and `abort` |
| `fixy/os/`: ClockSource, ExitHooks, Futex, MonotonicClock, NumaCounters, SchedClass, Signal, ThreadTasks (8) | Keep | Os | – | The doors and the tags of fixy/os. No std finding |
| `fixy/os/AtomPack.h` | Keep | Region | 4.9 | The folds of an atom pack run in a constant evaluation |
| `fixy/os/Fd.h` | Keep | Ref | 4.9 | `std::exchange` in the moves of the handle |
| `fixy/os/ThreadName.h` | Rebuild | Region | 4.9 | The name is a `std::array` of characters at run time |
| `fixy/os/`: CpuPinned, Fs, Io, Mmap, NumaPlacement, OwnedMmap, Sched, Socket, Time, CipherDurable (10) | Rebuild | Os, Choice | 4.1 | The door gives `std::expected` with `std::error_code`, or with a plain `int` in `CpuPinned.h` and `Sched.h`. `Fs.h`, `Io.h`, `Mmap.h`, `OwnedMmap.h`, `Time.h` and `CipherDurable.h` also hold a system type in a public signature |
| `fixy/os/OwnedFile.h` | Merge | Os (G6, planned) | 4.1 | The Os family replaces the stdio stream, and `std::FILE` leaves the public signatures |
| `fixy/os/Path.h` | Rebuild | Os, Region | 4.6 | A `Tagged` over `std::filesystem::path` |
| `fixy/os/SpinLock.h` | Rebuild | Atomic, Os | 4.4 | The bare lock holds `std::atomic_flag`, and the guard takes `std::try_to_lock_t` |
| `fixy/os/Topology.h` | Rebuild | Region, Choice, Report | 4.6 | The probe holds `std::vector`, `std::string` and `std::optional` at run time |
| `fixy/session/`: ContentAddressed, CrashAssociation, CrashCause, Liveness, NetworkModel, Projection, Semantics, VigilMode, Wire (9) | Keep | – | – | No std finding |
| `fixy/session/`: Admission, Classification, Crash, Fold, Global, Network, Protocol, Refinement, Registry, Subtype, MachineBridge (11) | Keep | Region, Record | 4.9 | The protocol algebra, the projection, the subtyping and the payload walk run in a constant evaluation. The std use is the text of a refusal, the type lists and the reflection results |
| `fixy/session/Classified.h` | Rebuild | Region | 4.3 | `bytes()` gives a `std::span` at run time |
| `fixy/session/`: Entry, Mint, AsyncChannel, Stepping (4) | Rebuild | Report | 4.5 | A mint takes `std::source_location` at run time |
| `fixy/session/`: Handle, Delegate (2) | Rebuild | Report, Region | 4.5 | A run-time check reports with `fprintf` or `fwrite` and `abort`, and the handle records a `std::source_location` |
| `fixy/session/Watch.h` | Rebuild | Report, Region | 4.5 | The registration of a live session takes a `std::source_location`, and the table of the watch is a `std::array` at run time |
| `fixy/session/Flow.h` | Rebuild | Report, Record | 4.5, 4.7 | A fault reports with `fwrite` and `abort` at run time, and a fact of a flow is a `std::pair` |
| `fixy/session/CrashTransport.h` | Rebuild | Report, Choice, Atomic, Ref, Record | 4.2, 4.4, 4.5 | The decorator gives `std::optional`, reads a `std::atomic` peer cell, calls through `std::invoke` and reports with `fprintf` |
| `fixy/session/`: Recording, Transport (2) | Rebuild | Choice, Record, Ref | 4.2 | A step gives `std::optional` or `std::pair` at run time, and calls through `std::invoke` |
| `fixy/session/Checkpoint.h` | Rebuild | Choice, Record, Report, Region | 4.2, 4.5 | The coordinated rollback gives `std::optional` and stops with `std::abort` |
| `fixy/session/EventLog.h` | Rebuild | Region, Choice, Report, Scalar | 4.2, 4.3 | The log stores a `std::vector` of events, and the decode gives `std::expected` |
| `fixy/session/VigilModeCell.h` | Rebuild | Atomic | 4.4 | The cell holds the mode in a `std::atomic` |
| `fixy/session/Witnessed.h` | Rebuild | Region | 4.3 | A witnessed borrow holds a borrow of `fixy/Borrowed.h`, which merges into `View` |

### E.4 The counts

| Verdict | Rows | Headers | Lines |
|---|---|---|---|
| Keep | 43 | 179 | 45,414 |
| Rebuild | 47 | 72 | 27,663 |
| Merge | 6 | 12 | 2,982 |
| Delete (proposal) | 1 | 1 | 226 |
| Total | 97 | 264 | 76,285 |
