# Crucible

Crucible is a runtime for high-performance computing (HPC) and machine learning (ML) programs. Python describes a model, and Crucible executes it. Crucible records the operations, compiles them for the hardware, and keeps the state when a node fails.

This file is the short guide. It tells you what the project needs, which commands to run, and how to write code here. `misc/GUIDE.md` holds the full reference. Read a section of it only when your task needs that detail.

## 1. What the project needs

### The names

| Name | What it is |
|---|---|
| Vessel | The front-end adapter. PyTorch is the only one (`vessel/torch/`) |
| Vigil | One model in Crucible: its graph, its regions and its state |
| Cipher | The persistent store of a Vigil |
| Relay | One compute node |
| Keeper | The daemon of a Relay (planned) |
| Canopy | The mesh of Keepers (planned) |
| Forge | The optimizer that is the same for each vendor (planned, `misc/FORGE.md`) |
| Mimic | The backend of one vendor, which emits machine code (planned, `misc/MIMIC.md`) |

### What exists

- The base language: `include/foundation/` and `include/fixy/`, with their sources in `src/foundation/` and `src/fixy/`.
- The record path: the Vessel intercepts each PyTorch operation and records it in `TraceRing` and `MetaLog`.
- The background pipeline: it finds iterations, builds a `TraceGraph`, and makes a `RegionNode` of the Merkle DAG with a memory plan.
- Replay: the Vigil follows a compiled region, but PyTorch still does each operation.
- The Cipher: a local store of regions on one disk.
- The transport, topology and observation code in `include/crucible/cntp/`, `include/crucible/topology/` and `include/crucible/observe/`.

### What does not exist

- No Crucible code computes a tensor. The tree has no kernel, no Forge phase and no Mimic backend.
- No shadow handle: compiled mode gives real PyTorch tensors.
- No Keeper, no Canopy, and no Cipher tier other than the local disk.

### The order of work

1. Move the product code onto the base: `include/crucible/`, `src/` and `vessel/`. Correct the known defects of the record path in the same work.
2. Build the first compute path: the portable kernel graph (IR002), a CPU reference backend, and a replay that runs Crucible kernels on the memory plan. The first target is one small model whose result is the same as the result of eager PyTorch, bit for bit.
3. Add shadow handles, so that Python gets metadata and Crucible does the work.
4. Add the first GPU backend, then the kernel search and its calibration.
5. Add the Keeper, the Canopy and the Cipher tiers.

Tests and benches move onto the base after step 1. They do not stop step 2.

### Rules for the scope of work

- Build the base ahead of its callers. It holds the common patterns of advanced programming languages and type systems, each in a clean and safe form.
- Lack of use is not a reason to delete a part or not to build it. Keep each part that is useful for the infrastructure of an ML and HPC framework. If a part is not useful there, it can go, and you can look at it again later.
- Each base type that you add has the full quality: safe, no hidden effect, zero cost on the hot path, with tests.
- Do not add a guard rule, a plant, a ledger or a plugin rule, unless the owner asks for it.
- Do not start a soundness campaign against the base, unless the owner asks for it.
- Keep this file short. Put a detail in a code comment or in `misc/`. Do not put a measurement, a date or a tool manual here.

### The tree

| Path | Contents |
|---|---|
| `include/foundation/`, `src/foundation/` | The lower layer of the base: core types, effects, permissions, OS doors |
| `include/fixy/`, `src/fixy/` | The upper layer of the base: wrappers, sessions, channels, concurrency |
| `include/crucible/`, `src/` | The product code |
| `vessel/torch/` | The PyTorch adapter |
| `test/` | Tests. A directory `neg/` or `*_neg/` holds fixtures that must not compile |
| `bench/` | Benches |
| `utils/` | The build and check tools: `utils/bin/guard`, `utils/bin/loop`, `utils/bin/setup` |
| `misc/` | Design documents: `misc/CRUCIBLE.md`, `misc/FORGE.md`, `misc/MIMIC.md`, `misc/GUIDE.md` |

### The toolchain

- The only compiler is the patched GCC 16. The code uses C++26 contracts and reflection, and no other compiler has them.
- `python3 utils/bin/setup gcc PREFIX` builds the compiler. `python3 utils/bin/setup install` installs the pinned CMake, mold and other tools.
- The platform is Linux on x86_64 or aarch64, 64-bit, little-endian, with 64-byte cache lines.
- A vessel build needs the patched PyTorch (`TORCH_DIR`, the preset `vessel`).

## 2. Commands and checks

### Each day

```bash
cmake --preset default                                   # configure the Debug build with ASan, in build/
python3 utils/bin/loop heavy -j 32 -- cmake --build build     # build; give cmake no -j
python3 utils/bin/loop test build -j 32                  # build, then run each test whose input changed
python3 utils/bin/loop heavy -j 32 -- ctest --test-dir build -R REGEX -j 32 --output-on-failure
```

`loop heavy` and `loop test` take at most 32 slots of the shared slot pool of the host. Then many jobs can run at the same time.

### Landing a change

```bash
git diff --binary -M --output=change.patch main...HEAD
python3 utils/bin/loop land -m "SUBJECT" --patch change.patch --
```

- Main moves only through `loop land`. It applies the patch to the main of that time, runs the gate, and then moves main.
- Add `--check` to run the gate with no landing.
- Add `--all-configurations` for a change of a header that most units include, such as a header of `include/foundation/core/`.
- Work in a git work tree under `.worktrees/`. Do not edit the main work tree.

### The gate

The gate of `loop land` is the check that counts. It builds the configurations that the change can change, and it runs each test and each guard rule there.

| Preset | Build directory | Purpose |
|---|---|---|
| `default` | `build` | Debug, ASan, and UBSan on the tests |
| `release` | `build-release` | Release, `-O3 -march=native` |
| `tsan` | `build-tsan` | ThreadSanitizer |
| `ubsan-strict` | `build-ubsan-strict` | Full UBSan on each target |
| `bench-fuzz` | `build-bench-fuzz` | The benches and the fuzz harnesses |
| `syntax-x86-64-v3` | `build-syntax-x86-64-v3` | Parse only, for the AVX2 code arms |
| `syntax-aarch64` | `build-syntax-aarch64` | Parse only, for the aarch64 code arms |

Other entries:
- `python3 utils/bin/loop nightly` also runs the presets `analyzer`, `bench`, `vessel`, `no-bpf` and `verify`.
- `python3 utils/bin/loop release` also runs the profile-guided presets and the aarch64 tests under qemu.
- `python3 utils/bin/loop bench` runs benches on an isolated core. `python3 utils/bin/loop pgo` collects a profile.
- GitHub Actions has the workflows `ci`, `patched-gcc` and `release`. No person and no agent waits for them. Do not push, unless the owner asks.

### When the gate refuses a change

| The gate says | What to do |
|---|---|
| A test is red | Correct the cause. Do not raise a timeout, and do not run the test again until it passes |
| A quarantine count is above its row | Your change added a std type, a raw pointer or a C call outside the base. Use a base type |
| A `pre` or `post` specifier | Use `CRUCIBLE_PRE` and `CRUCIBLE_POST` as statements in the body |
| A declaration in `foundation` or `fixy` from a file outside the base | Put it in the base, or beside your own type |
| A `static_assert` at namespace scope in a header | Move it to the check file of the header in `test/layer/checks/` |
| A mint with fewer than two fixtures | Add two fixtures that must not compile |
| A ledger or a generated file is different | Run `python3 utils/bin/guard write --rule RULE --build-dir build` or `python3 utils/bin/loop derive`. Do not edit such a file by hand |
| The patch does not apply | Merge main into your branch, and make the patch again |

`python3 utils/bin/guard explain RULE` prints one rule. `python3 utils/bin/guard list --build-dir build --path PATH` prints the findings of a file or a directory.

## 3. How to write code

### The layers

- The dependency goes one way: `foundation`, then `fixy`, then `crucible`. A lower layer does not include a higher layer.
- Each layer has its namespace: `foundation`, `fixy` and `crucible`.
- A file outside the base declares nothing in the namespace `foundation` or `fixy`.
- Only the door files in `include/foundation/os/`, `include/fixy/os/` and their source directories include a system header.

### New code uses the base

Outside the base, each line that you write or change holds only base types. Do not write a std object, a raw pointer, a C array, a C library call, `new`, `delete` or `assert`. The lines that you do not change can stay as they are.

`include/fixy/Core.h` gives the core names in namespace `fixy`:

| You need | Use |
|---|---|
| A value that can be absent | `Option<T>`, `some`, `none` |
| A value or an error | `Result<T, E>`, `err`, `CRUCIBLE_TRY` |
| One of some types | `OneOf<Ts...>` with `match` |
| A view of elements | `View<T>`, `TextView` |
| A fixed or bounded container | `FixedList<T, N>`, `HeapArray<T>`, `FixedText` |
| A checked index | `Index<N>`, `BrandedIndex` |
| A reference or an owner | `Ref<T>`, `Box<T>` with `mint_box` |
| Some values together | `Record<Ts...>` |
| An atomic cell | `Atomic<T>`, `SoleWriter` |
| An integer change | `narrow`, `widen`, `decode`, `enum_from`, `bit_cast` |
| Integer math | `min`, `max`, `clamp`, `popcount`, `bit_width`, and the checked and saturating sets |
| An end of the process | `fatal("reason")`, `unreachable()` |
| A text for a person | `report`, `format`, `Fmt` |

- Read a `Result` or an `Option` with `match`, with one arm for each case.
- If the base has no type for your need, add the type to the base. Do not use a std type in its place.
- An opt-out region is only for these reasons: a foreign ABI, a header that a C compiler reads, a probe of raw behavior, a test against the standard library, or a raw measurement in a bench. Open it with `CRUCIBLE_I_KNOW_WHAT_IM_DOING("CLASS: reason")`.

### The eight safety axioms

Do a check of each axiom on each change.

1. **InitSafe.** Each field has an initializer. Write `T value{};`, not `T value;`. Each padding byte is a named member.
2. **TypeSafe.** Each value with a meaning has a strong type: `OpIndex`, `SlotId`, `ContentHash`. Use `enum class` with a stated base type. No C cast, no `reinterpret_cast`, no `const_cast`.
3. **NullSafe.** A pointer parameter has a precondition, or better, a type that cannot be null. Each query is `[[nodiscard]]`.
4. **MemSafe.** Graph memory comes from an arena. No `new` and no `delete`. A type that must not copy has `= delete("reason")`.
5. **BorrowSafe.** One writer for each object. State the owner thread of each shared structure.
6. **ThreadSafe.** Cross-thread data goes through atomics with acquire and release. A relaxed read is only for the own variable of the thread.
7. **LeakSafe.** Each resource has one owner and a bounded life. A thread member is the last member, so it joins first.
8. **DetSafe.** The same inputs give the same outputs, bit for bit. No fast-math flag, no order from a hash table, no order from a pointer value. Random numbers come from Philox.

### Errors

| Kind | Mechanism |
|---|---|
| Cannot occur in correct code | `CRUCIBLE_PRE(condition)` at the start of the body, `CRUCIBLE_POST(result, condition)` before a return |
| Can occur, but rarely | Return a `Result<T, E>`. The error is a small enum or a small plain class |
| Cannot continue | `fatal("reason")`, which ends the process in each build |

- No code throws an exception. No code uses RTTI or `virtual`. Dispatch is a `kind` enum.
- A contract check ends the process in Debug and in Release.
- No logging and no formatting on the hot path.

### Types that hold an invariant

- A wrapper holds a rule in the type: `Refined<Pred, T>` for a checked value, `Linear<T>` for a resource, `Tagged<T, Tag>` for input from outside, `Secret<T>` for a key.
- `Permission<Tag>` proves which thread owns a region. `SessionHandle` holds the next step of a protocol in its type.
- A function with the name `mint_*` is the only door to such a type. A mint is `[[nodiscard]] noexcept`, it has one concept as its constraint, and it has two fixtures that must not compile.
- Use a wrapper when it prevents a real error. Do not put a wrapper on each value.

### The hot path

The hot path is the record of one operation and the replay of one operation.

- No allocation, no system call, no lock and no wait that can block.
- Hot code is in headers, so it can inline. A cold body goes to a source file.
- Move each unlikely branch of more than some lines into a `CRUCIBLE_COLD` function.
- Put each atomic that two threads use on its own cache line with `alignas(64)`.
- Use contiguous arrays and indexes, not linked nodes.
- Measure before you change code for speed. Give p50, p99 and the maximum, not a mean. A variance above 5% makes the result invalid.

### Concurrency

- The foreground thread records. One background pipeline of four stages drains, detects, builds and publishes. SPSC channels connect them.
- A wait on the hot path is a spin with `CRUCIBLE_SPIN_PAUSE`. Do not sleep, yield or use a futex there.
- A new producer and consumer pair uses `Permission<Tag>` and a channel of `include/fixy/concurrent/`.
- Start threads through `mint_permission_fork` or the pipeline mints, not with a raw thread.
- Do not run small work in parallel. `ParallelismRule` of `include/fixy/concurrent/ParallelismRule.h` gives the decision from the size of the working set.

### Headers and build flags

- Each header has `#pragma once`, and it compiles alone.
- A header has no `static_assert` at namespace scope and no test namespace. Each includer compiles them again.
- Do not include a heavy std header in a base header for one function. Use the builtin.
- Never add these flags: `-ffast-math`, `-ffp-contract=fast`, `-fno-strict-aliasing`, `-fpermissive`.
- Each warning is an error. Do not turn a warning off to make code compile.
- No vendor library: no cuBLAS, no cuDNN, no NCCL, no libcuda.

### Names

- A function name starts with a verb: `build_trace`, `classify_kernel`.
- A boolean name is a question: `is_compiled`, `has_pending_region`, `should_reject`. Do not use `ok`, `done`, `flag` or `valid` alone.
- No name of one or two characters, apart from a loop index and the names that the code already uses: `op`, `args`, `ndim`, `dtype`, `ctx`, `bg`.
- A name has only ASCII characters.

### Tests

- Each defect fix has a test that fails on the old code and passes on the new code.
- A test is deterministic: a fixed seed, no wall clock, no network.
- A test of a contract violation runs the violation in a child process and reads the abort signal.
- A flaky test is a defect. Correct it.
- `test_bit_exact_replay_invariant` must pass. If it fails, the change added hidden state.

### Changes and text

- A change moves each caller of an API at the same time. Keep no shim, no alias and no old name. The project has no outside users.
- Make small commits, each with one purpose. The subject is a command of at most 20 words.
- A comment tells what the code does. It does not tell what changed.
- Put no task number, stage label or agent name in code, comments, tests or commit messages.
- Write comments, commit messages and documents in short plain sentences: active voice, one idea in each sentence, no semicolon.
- Do not add an `.md` file, unless the owner asks.
- Change files with the edit tools. Do not use `sed` or `awk`.
- Do not use `git checkout`, `git reset`, `git stash`, `git clean` or `--no-verify`.

### If rules conflict

Correctness is first. Then determinism, then safety, then latency, then throughput, then code size.
