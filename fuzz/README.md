# Crucible fuzz infrastructure

Two flavors of stress testing ship under `fuzz/`:

## `property/` — E2E correctness fuzzers

Deterministic, dependency-free property-based testing.  Each test
runs N iterations with Philox-derived RNG per iteration; failures
report the failing iteration + seed for trivial reproduction.

Targets today:

| Fuzzer | Invariant checked |
|---|---|
| `prop_hash_determinism` | Same input → same hash across 8 repeated calls |
| `prop_hash_distinct` | Recipes differing in 1 field → different hashes |
| `prop_recipe_pool_intern` | `pool.intern(a) == pool.intern(b)` iff semantic-equal |
| `prop_reflect_avalanche` | 10K random bit-flips → mean flip-count 32 ± 2 |
| `prop_diff_reflect_vs_packed` | REFL-2 refactor: reflect and packed agree on structural equality |
| `prop_recipe_hashed_idempotent` | `hashed(hashed(x)) == hashed(x)` + hash-field exclusion |

### Running

```sh
cmake --preset default -DCRUCIBLE_FUZZ=ON
cmake --build --preset default
ctest --preset default -L fuzz_property
```

Default smoke iterations: 1000 (fast, ~seconds total).  For extended
stress, run a specific fuzzer with a higher iteration count:

```sh
./build/fuzz/prop_recipe_pool_intern --iters=100000 --seed=0xDEADBEEF
```

To reproduce a reported failure, re-run with the printed seed:

```sh
./build/fuzz/prop_hash_determinism --seed=0xC0FFEE --iters=1
```

## `boundary/` — adversarial-input fuzz harnesses

libFuzzer-compatible entry point
(`int LLVMFuzzerTestOneInput(const uint8_t*, size_t)`)
runnable two ways:

1. **Standalone replay** (ctest smoke): the built binary walks a
   corpus directory and feeds every regular file through the
   harness.  Sanitizers (ASan + UBSan + bounds-strict) catch crashes.
2. **AFL++ coverage-guided** (extended): same binary driven by
   `afl-fuzz` in QEMU mode — no recompilation required.

### Standalone (ctest)

```sh
ctest --preset default -L fuzz_boundary
```

### AFL++ QEMU mode (extended)

AFL++ 4.35c is packaged on Fedora 43 as `american-fuzzy-lop`
(the URL points to the AFLplusplus upstream, just kept the legacy
package name).  GCC-plugin mode is tied to the system GCC which
mismatches Crucible's GCC 16; use QEMU mode to avoid the recompile
requirement:

```sh
sudo dnf install american-fuzzy-lop

# Tune loopback + corefile (AFL++ warns about these if missing)
sudo sh -c 'echo core > /proc/sys/kernel/core_pattern'
echo performance | sudo tee /sys/devices/system/cpu/cpu*/cpufreq/scaling_governor

# Build the fuzzer binaries (already built by the property flow above).
# Run AFL++ in QEMU mode — instruments at runtime, any compiler toolchain.
afl-fuzz -Q \
  -i fuzz/boundary/corpus \
  -o /tmp/afl_crucible_serialize \
  -- ./build/fuzz/fuzz_serialize @@
```

AFL++ reports its findings under `/tmp/afl_crucible_serialize/default/`:

- `queue/`   — inputs that reached new coverage
- `crashes/` — inputs that crashed the harness (sanitizer fire)
- `hangs/`   — inputs that exceeded the timeout

Any file in `crashes/` is a regression-worthy new seed.  Copy the
interesting ones into `fuzz/boundary/corpus/` to guard against them
forever via the smoke test.

### Harness pattern

Boundary harnesses follow the libFuzzer convention:

```cpp
extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size) {
    // Exercise the code path with `data[0..size)`.  Return 0.
    // Contract: no crash, no sanitizer fire, regardless of input bytes.
    return 0;
}

#define CRUCIBLE_FUZZ_STANDALONE_MAIN
#include "runner_main.h"   // supplies main() for standalone replay
```

The `CRUCIBLE_FUZZ_STANDALONE_MAIN` guard lets the same source file
compile either standalone (supplies `main()`) or as a libFuzzer
target (libFuzzer supplies its own `main()`).

## Why both flavors?

- **Property fuzzers** test the library's logic at scale.  They catch
  determinism regressions, collision bugs, refactor drifts
  (REFL-2/3), miscompilation under `-O3`, etc.  No external fuzzer
  needed; ctest runs them as normal tests.
- **Boundary fuzzers** test the parser/deserializer code paths that
  take untrusted bytes (Cipher load, crtrace import).  AFL++
  coverage-guided mutation is the right driver here — property
  generators can't efficiently explore byte-level wire-format
  corners.

Together they cover the two canonical fuzz goals: "does the code do
what it says" (property) and "does the code crash on bad input"
(boundary).
