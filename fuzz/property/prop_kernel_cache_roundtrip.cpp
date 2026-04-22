// ═══════════════════════════════════════════════════════════════════
// prop_kernel_cache_roundtrip — KernelCache insert + lookup invariant.
//
// KernelCache is the lock-free open-addressing Swiss-table that maps
// content_hash → CompiledKernel*.  The load-bearing invariant:
//
//   For every (h, k) pair inserted, lookup(h) returns k (or fails
//   on table-full, but never returns a different kernel).
//
// Strategy: per iteration, generate N distinct (content_hash, fake
// kernel*) pairs.  Insert all N; lookup each; verify the original
// kernel comes back.  Random hash distribution exercises probe
// chains.  Run with N up to KERNEL_CACHE_CAP/2 (50% load).
//
// Catches:
//   - Probe-order bugs (linear probe vs Robin Hood inconsistency)
//   - CAS retry loop logic errors (wrong slot claim semantics)
//   - Hash-comparison shortcuts that skip equality verification
//   - Insert returning std::expected error class regressions
// ═══════════════════════════════════════════════════════════════════

#include "property_runner.h"
#include "random_input.h"

#include <crucible/MerkleDag.h>

#include <array>

int main(int argc, char** argv) {
    using namespace crucible;
    using namespace crucible::fuzz::prop;
    Config cfg = parse_args(argc, argv);
    if (cfg.iterations > 5'000) cfg.iterations = 5'000;  // O(N) per iter

    return run("KernelCache insert + lookup round-trip", cfg,
        [](Rng& rng) {
            // 64 distinct (hash, fake_kernel*) pairs per iteration.
            // KERNEL_CACHE_CAP is typically 1024+; 64 is well under
            // the 50% load threshold so insert won't OOM the table.
            constexpr unsigned N = 64;
            struct Batch {
                std::array<ContentHash, N> hashes;
                std::array<uintptr_t, N>   tags;
            };
            Batch b{};
            for (unsigned i = 0; i < N; ++i) {
                // Avoid the zero hash (KernelCache may reserve it
                // as a slot-empty sentinel).  Add 1 to guarantee
                // non-zero.
                b.hashes[i] = ContentHash{rng.next64() | 1ULL};
                b.tags[i]   = rng.next64();
            }
            return b;
        },
        [](const auto& b) {
            KernelCache cache;
            constexpr unsigned N = 64;

            // Insert all N — synthesize a "fake" CompiledKernel*
            // by reinterpreting the random tag as a pointer.  We
            // never DEREFERENCE this pointer, only compare it.
            for (unsigned i = 0; i < N; ++i) {
                auto* fake = reinterpret_cast<CompiledKernel*>(b.tags[i]);
                auto ins = cache.insert(b.hashes[i], fake);
                if (!ins.has_value()) return false;
            }

            // Lookup each — must return the SAME pointer.
            for (unsigned i = 0; i < N; ++i) {
                auto* expected =
                    reinterpret_cast<CompiledKernel*>(b.tags[i]);
                auto* actual = cache.lookup(b.hashes[i]);
                // Two paths the lookup can return a "wrong" pointer:
                //   1. Returned someone else's kernel (probe bug)
                //   2. Returned nullptr (table-full or lost insert)
                // Either fails the round-trip.
                //
                // Note: if two of our random hashes collide (after
                // the |1 mask, probability still ~2^-64 per pair),
                // the second insert overwrites and the first lookup
                // legitimately returns the SECOND tag.  Skip the
                // check in that rare case.
                bool collision_with_later = false;
                for (unsigned j = i + 1; j < N; ++j) {
                    if (b.hashes[j] == b.hashes[i]) {
                        collision_with_later = true;
                        break;
                    }
                }
                if (collision_with_later) continue;

                if (actual != expected) return false;
            }
            return true;
        });
}
