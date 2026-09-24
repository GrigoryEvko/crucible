# Crucible fuzz infrastructure

The `fuzz/` tree has two types of stress test.

## `property/` — property tests

Each property test runs N iterations with a Philox-derived random stream.
A failure prints the iteration and the seed, so you can run it again.

```sh
cmake --preset default -DCRUCIBLE_FUZZ=ON
cmake --build --preset default
ctest --preset default -L fuzz_property
```

To run more iterations, or to run a failure again with its seed:

```sh
./build/fuzz/prop_recipe_pool_intern --iters=100000 --seed=0xDEADBEEF
./build/fuzz/prop_hash_determinism --seed=0xC0FFEE --iters=1
```

## `boundary/` — boundary harnesses

A boundary is a place where bytes from outside the process become typed
values: a region or branch image, a Cipher store on disk, a federation
entry, a trace file, the hardware ledger, a session-event log, the
Reed-Solomon and fountain decoders, and the text that the runtime reads
from sysfs, procfs and host tools.  Each boundary has one harness in
`boundary/harnesses/<name>.h`, with two functions:

- `run_<name>(bytes)` drives the boundary with one input.
- `seeds_<name>()` builds the seed inputs with the real writers, and adds
  each crash that a campaign found as a regression input.

A harness reports two types of finding.  A sanitizer report is the first
type.  The second type is an accepted value that breaks the claim of its
type, for example a loaded region whose content hash is not the hash of its
operations.  `CRUCIBLE_FUZZ_CLAIM` aborts on it, so a fuzzer records it as a
crash.

Each harness compiles alone into an object library, because two harness
headers can include headers that do not compile together.  Two types of
binary link it:

- `fuzz_<name>`, the fuzz binary.  The ctest entry, label `fuzz_smoke`,
  replays the seeds and then a fixed stream of mutated copies, in a few
  seconds.
- `prop_boundary_harnesses`, which runs every harness on Philox-edited
  seeds.  Its label is also `fuzz_smoke`.

```sh
ctest --preset default -L fuzz_smoke
./build/fuzz/fuzz_region --mutate=100000 --crash-file=/tmp/region.crash
./build/fuzz/fuzz_region /tmp/region.crash
./build/fuzz/fuzz_region --write-seeds=/tmp/region-seeds
```

`--crash-file` writes the input in flight when a claim, a contract or a
sanitizer stops the process.

To add a boundary, write `boundary/harnesses/<name>.h` and add one
`crucible_boundary_fuzzer(<name> <mutations>)` line to
`fuzz/CMakeLists.txt`.  A harness names no type of the old substrate.  It
reaches an old-tree boundary through `boundary/old_tree.h`, which is the
one file under `fuzz/` on `scripts/flip-list.txt`.

## Coverage-guided campaigns: AFL++ with its GCC plugin

The coverage-guided fuzzer is AFL++ with its GCC plugin (`afl-g++-fast`),
built from source against the patched GCC 16.  We use it for these
reasons:

- libFuzzer and the clang instrumentation cannot compile this tree, which
  needs GCC 16 for contracts and reflection.
- The GCC plugin instruments at compile time, so a campaign runs at native
  speed.  QEMU mode is ten to twenty times slower.
- AFL++ persistent mode feeds 10,000 inputs to one process.  `runner_main.h`
  enters that mode when the binary was built with `afl-g++-fast` and is
  started with no arguments.
- honggfuzz with `-fsanitize-coverage=trace-pc` also works with GCC, but it
  gives no comparison feedback, and the magic numbers and hashes in these
  formats need that feedback.

To build AFL++ 5.03c with the plugin, as a user-local install:

```sh
git clone https://github.com/AFLplusplus/AFLplusplus ~/.local/src/AFLplusplus
cd ~/.local/src/AFLplusplus
make PREFIX=$HOME/.local afl-fuzz afl-showmap afl-tmin afl-analyze
make -f GNUmakefile.gcc_plugin PREFIX=$HOME/.local \
     CC=$HOME/.local/gcc16-patched/usr/bin/gcc-16p \
     CXX=$HOME/.local/gcc16-patched/usr/bin/g++-16p
```

The top makefile builds the `afl-cc` driver only when LLVM is installed.
Without LLVM, compile `src/afl-cc.c` by hand with each `LLVM_*` macro set
to an empty string or to zero, and with `AFL_PATH` set to
`$HOME/.local/lib/afl`.  Copy `afl-compiler-rt.o`, `afl-gcc-rt.o`,
`dynamic_list.txt` and the three `afl-gcc-*-pass.so` files into that
directory, and link `afl-gcc-fast` and `afl-g++-fast` to `afl-cc`.  The
cmplog pass gives the comparison feedback.

Then `fuzz/run-afl.sh` builds the instrumented binaries in a directory you
name, writes the seeds, and starts one `afl-fuzz` instance for each core
that you give it, with a time limit:

```sh
fuzz/run-afl.sh BUILD_DIR OUT_DIR HARNESS CORES SECONDS
fuzz/run-afl.sh /tmp/afl-build /tmp/afl-out region 0-15 3600
```

The script keeps the campaign off the cores that the host reserves.  After
a campaign, minimize each file in `OUT_DIR/*/crashes` with `afl-tmin`, find
the root cause, and fix it at the boundary.  Then add the input to the
seeds of the harness, as a literal or as a patch to an image that a writer
built, in the commit that fixes the boundary.
