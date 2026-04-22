// ═══════════════════════════════════════════════════════════════════
// prop_arena_alloc_invariants — Arena allocation invariants under stress.
//
// Properties enforced per random alloc sequence:
//   1. Every returned pointer is non-null (Arena aborts on OOM, not
//      returns null — but the allocation itself must succeed)
//   2. Every returned pointer is properly aligned for its type
//   3. No two allocations alias (different pointer values)
//   4. The aligned pointer respects the requested alignment, even
//      across block-boundary allocations triggering alloc_slow_
//
// Catches:
//   - Alignment math bugs in alloc_slow_ (the cold path that
//     allocates a fresh block when the current one is exhausted)
//   - Saturating-add overflow in size+align padding
//   - Block boundary off-by-one (returning a ptr just past block end)
//
// Strategy: per iteration, run a sequence of N (size, align) pairs.
// Sizes biased toward [1, 4096] with occasional larger requests
// (16KB-1MB) to trigger alloc_slow_'s oversized-request path.
// Alignment biased toward power-of-two in [1, 256].
// ═══════════════════════════════════════════════════════════════════

#include "property_runner.h"
#include "random_input.h"

#include <crucible/Arena.h>
#include <crucible/Effects.h>
#include <crucible/safety/Refined.h>

#include <array>
#include <cstdint>

int main(int argc, char** argv) {
    using namespace crucible;
    using namespace crucible::fuzz::prop;
    Config cfg = parse_args(argc, argv);
    if (cfg.iterations > 10'000) cfg.iterations = 10'000;  // O(N) per iter

    return run("Arena alloc alignment + non-aliasing", cfg,
        [](Rng& rng) {
            constexpr unsigned N = 32;
            struct AllocPlan {
                std::array<size_t, N> sizes;
                std::array<size_t, N> aligns;
                uint8_t               count;
            };
            AllocPlan plan{};
            plan.count = static_cast<uint8_t>(rng.next_below(N) + 1);
            for (uint8_t i = 0; i < plan.count; ++i) {
                // 80% small (1..4096), 15% medium (4K..64K), 5% large (64K..1M).
                const uint32_t bucket = rng.next_below(100);
                if (bucket < 80) {
                    plan.sizes[i] = rng.next_below(4096) + 1;
                } else if (bucket < 95) {
                    plan.sizes[i] = rng.next_below(60u * 1024) + 4096;
                } else {
                    plan.sizes[i] = rng.next_below(960u * 1024) + 64u * 1024;
                }
                // Power-of-2 alignment in [1, 256].
                const uint32_t align_bits = rng.next_below(9);  // 0..8
                plan.aligns[i] = size_t{1} << align_bits;
            }
            return plan;
        },
        [](const auto& plan) {
            // Use a small arena (4 KB blocks) to force frequent
            // alloc_slow_ engagement on the larger requests.
            Arena arena{4 * 1024};
            fx::Test test{};

            std::array<void*, 32>  ptrs{};
            std::array<size_t, 32> sizes{};

            for (uint8_t i = 0; i < plan.count; ++i) {
                void* p = arena.alloc(test.alloc,
                    safety::Positive<size_t>{plan.sizes[i]},
                    safety::PowerOfTwo<size_t>{plan.aligns[i]});
                if (p == nullptr) return false;

                // Property 1: alignment.
                if ((reinterpret_cast<uintptr_t>(p) & (plan.aligns[i] - 1)) != 0)
                    return false;

                ptrs[i] = p;
                sizes[i] = plan.sizes[i];
            }

            // Property 2: pairwise non-aliasing.
            // Two arena allocations must never overlap (their byte
            // ranges must be disjoint).  Quadratic check; bounded
            // at N=32 = ~500 pair checks per iteration.
            for (uint8_t i = 0; i < plan.count; ++i) {
                const auto a_lo = reinterpret_cast<uintptr_t>(ptrs[i]);
                const auto a_hi = a_lo + sizes[i];
                for (uint8_t j = static_cast<uint8_t>(i + 1);
                     j < plan.count;
                     ++j)
                {
                    const auto b_lo = reinterpret_cast<uintptr_t>(ptrs[j]);
                    const auto b_hi = b_lo + sizes[j];
                    // Overlap test: !(a_hi <= b_lo || b_hi <= a_lo).
                    if (!(a_hi <= b_lo || b_hi <= a_lo)) return false;
                }
            }
            return true;
        });
}
