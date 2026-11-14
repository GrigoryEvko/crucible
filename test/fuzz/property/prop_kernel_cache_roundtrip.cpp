// ═══════════════════════════════════════════════════════════════════
// prop_kernel_cache_roundtrip — KernelCache insert + lookup invariant.
//
// KernelCache is the lock-free open-addressing Swiss-table that maps
// content_hash → CompiledKernel.  The load-bearing invariant:
//
//   For every (h, k) pair inserted, lookup(h) returns k (or fails
//   on table-full, but never returns a different kernel).
//
// Strategy: per iteration, generate N random content hashes, and pair
// each with one of N fake kernels.  Insert all N; lookup each; verify the
// original kernel comes back.  Random hash distribution exercises probe
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

#include <crucible/KernelCache.h>

#include <array>

int main(int argc, char** argv) {
    using namespace crucible;
    using namespace crucible::fuzz::prop;
    const Config cfg = parse_args(argc, argv, 5000);  // O(N) per iter

    return run(
        "KernelCache insert + lookup round-trip", cfg,
        [](Rng& rng) {
            // 64 random content hashes per iteration.
            // KERNEL_CACHE_CAP is typically 1024+; 64 is well under
            // the 50% load threshold so insert won't OOM the table.
            constexpr unsigned N = 64;
            struct Batch {
                std::array<ContentHash, N> hashes;
            };
            Batch b{};
            for (unsigned i = 0; i < N; ++i) {
                // Avoid the zero hash (KernelCache may reserve it
                // as a slot-empty sentinel).  Add 1 to guarantee
                // non-zero.
                b.hashes[i] = ContentHash{rng.next64() | 1ULL};
            }
            return b;
        },
        [](const auto& b) {
            KernelCache cache;
            constexpr unsigned N = 64;

            // The cache never reads a kernel, so the address of a fake
            // object stands for one.  The N fakes have N addresses.
            struct FakeKernel {
                unsigned tag = 0;
            };
            ::foundation::core::FixedArray<FakeKernel, N> fakes{};
            const auto kernel_of = [&fakes](::fixy::Index<N> index) noexcept -> KernelCache::KernelRef {
                return ::fixy::to(*static_cast<CompiledKernel*>(static_cast<void*>(&fakes.at(index))));
            };

            for (const ::fixy::Index<N> index : fakes.indices()) {
                // RowHash{} is the bare-type / no-row baseline (raw 0):
                // this fuzzer keys purely on content_hash, so it pins a
                // single row slot of the row-keyed KernelCache for every
                // insert.  insert() returns
                // KernelCache::InsertResult, and table-full is the only
                // error.
                auto ins = cache.insert(b.hashes[index.raw()], RowHash{}, kernel_of(index));
                if (ins.is_err()) return false;
            }

            // Lookup each — must return the SAME kernel.
            for (const ::fixy::Index<N> index : fakes.indices()) {
                const std::size_t i = index.raw();
                const KernelCache::FoundKernel actual = cache.lookup(b.hashes[i], RowHash{});
                // Two paths the lookup can return a "wrong" kernel:
                //   1. Returned someone else's kernel (probe bug)
                //   2. Returned no kernel (table-full or lost insert)
                // Either fails the round-trip.
                //
                // Note: if two of our random hashes collide (after
                // the |1 mask, probability still ~2^-64 per pair),
                // the second insert overwrites and the first lookup
                // legitimately returns the SECOND kernel.  Skip the
                // check in that rare case.
                bool collision_with_later = false;
                for (std::size_t j = i + 1; j < N; ++j) {
                    if (b.hashes[j] == b.hashes[i]) {
                        collision_with_later = true;
                        break;
                    }
                }
                if (collision_with_later) continue;

                const bool is_expected = actual.match(
                    [&](KernelCache::KernelRef const& kernel) noexcept {
                        return ::fixy::same_object(kernel, kernel_of(index));
                    },
                    []() noexcept { return false; });
                if (!is_expected) return false;
            }
            return true;
        });
}
