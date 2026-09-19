// Copyright (c) Grigory Evko
// Licensed under the Apache License, Version 2.0

#include <crucible/Arena.h>
#include <crucible/effects/_Capabilities.h>
#include <crucible/effects/_EffectRow.h>
#include <crucible/NumericalRecipe.h>
#include <crucible/RecipePool.h>

#include "test_assert.h"
#include <cinttypes>
#include <cstdio>
#include <type_traits>
#include <unordered_set>

namespace {

using crucible::Arena;
using crucible::NumericalRecipe;
using crucible::RecipeHash;
using crucible::RecipePool;
using crucible::ReductionAlgo;
using crucible::ReductionDeterminism;
using crucible::RoundingMode;
using crucible::ScalarType;
using crucible::ScalePolicy;
using crucible::SoftmaxRecurrence;

// Other fields stay at their defaults.  These three vary enough to
// avoid hash collisions across the small matrices below.
[[nodiscard]] constexpr NumericalRecipe mk(ScalarType accum, ScalarType out, ReductionDeterminism det,
                                           ScalePolicy scale = ScalePolicy::NONE, uint8_t flags = 0) noexcept {
    NumericalRecipe r{};
    r.accum_dtype = accum;
    r.out_dtype = out;
    r.determinism = det;
    r.scale_policy = scale;
    r.flags = ::crucible::safety::Bits<::crucible::RecipeFlags>::from_raw(flags);
    return r;
}

// A capability is minted through one of the context types rather than
// constructed directly.
auto g_test = crucible::effects::testing::test();
auto g_init = crucible::effects::testing::init();
inline crucible::effects::Alloc alloc_cap() noexcept { return g_test.alloc; }
inline crucible::effects::Init init_cap() noexcept { return g_init; }

}  // namespace

[[gnu::cold]] int main() {
    {
        // Interior pointers into the arena would dangle if the pool moved.
        static_assert(!std::is_copy_constructible_v<RecipePool>, "RecipePool must not be copy-constructible");
        static_assert(!std::is_copy_assignable_v<RecipePool>, "RecipePool must not be copy-assignable");
        static_assert(!std::is_move_constructible_v<RecipePool>, "RecipePool must not be move-constructible");
        static_assert(!std::is_move_assignable_v<RecipePool>, "RecipePool must not be move-assignable");
        static_assert(std::is_same_v<RecipePool::Capacity, crucible::safety::PowerOfTwo<uint32_t>>,
                      "RecipePool capacity must carry the power-of-two invariant");
        static_assert(std::is_same_v<RecipePool::Size, crucible::safety::Monotonic<uint32_t>>,
                      "RecipePool size must carry the monotonic-growth invariant");
        static_assert(std::is_same_v<RecipePool::ArenaBorrow, crucible::safety::BorrowedRef<Arena>>,
                      "RecipePool arena dependency must be an explicit borrow");
        static_assert(sizeof(RecipePool::Capacity) == sizeof(uint32_t),
                      "PowerOfTwo capacity wrapper must stay zero-cost");
        static_assert(sizeof(RecipePool::Size) == sizeof(uint32_t), "Monotonic size wrapper must stay zero-cost");
        static_assert(sizeof(RecipePool::ArenaBorrow) == sizeof(Arena*), "BorrowedRef<Arena> must stay pointer-sized");
        static_assert(crucible::effects::Subrow<RecipePool::init_required_row,
                                                crucible::effects::Row<crucible::effects::Effect::Init>>,
                      "RecipePool construction must admit Init callers");
        static_assert(!crucible::effects::Subrow<RecipePool::init_required_row, crucible::effects::Row<>>,
                      "pure callers cannot construct RecipePool");
        static_assert(!crucible::effects::Subrow<RecipePool::init_required_row,
                                                 crucible::effects::Row<crucible::effects::Effect::Alloc>>,
                      "allocation authority alone is not the Init phase");
    }

    {
        Arena arena{};
        RecipePool pool{RecipePool::ArenaBorrow{arena}, init_cap(), 32};
        assert(pool.size() == 0);
        assert(pool.capacity() == 32);
    }

    {
        Arena arena{};
        RecipePool pool{RecipePool::ArenaBorrow{arena}, init_cap(), 32};

        const NumericalRecipe fields = mk(ScalarType::Float, ScalarType::Half, ReductionDeterminism::BITEXACT_TC);

        const auto* a = pool.intern(alloc_cap(), fields);
        const auto* b = pool.intern(alloc_cap(), fields);
        assert(a == b);
        assert(pool.size() == 1);

        assert(a != nullptr);
        assert(a->hash == crucible::compute_recipe_hash(fields));
        assert(a->accum_dtype == ScalarType::Float);
        assert(a->out_dtype == ScalarType::Half);
        assert(a->determinism == ReductionDeterminism::BITEXACT_TC);
    }

    {
        Arena arena{};
        RecipePool pool{RecipePool::ArenaBorrow{arena}, init_cap(), 32};

        const auto* r_f32_strict =
            pool.intern(alloc_cap(), mk(ScalarType::Float, ScalarType::Float, ReductionDeterminism::BITEXACT_STRICT));
        const auto* r_f16_tc =
            pool.intern(alloc_cap(), mk(ScalarType::Float, ScalarType::Half, ReductionDeterminism::BITEXACT_TC));
        const auto* r_bf16_ordered =
            pool.intern(alloc_cap(), mk(ScalarType::Float, ScalarType::BFloat16, ReductionDeterminism::ORDERED));

        assert(r_f32_strict != r_f16_tc);
        assert(r_f32_strict != r_bf16_ordered);
        assert(r_f16_tc != r_bf16_ordered);
        assert(pool.size() == 3);
    }

    {
        Arena arena{};
        RecipePool pool{RecipePool::ArenaBorrow{arena}, init_cap(), 32};

        NumericalRecipe poisoned = mk(ScalarType::Float, ScalarType::Half, ReductionDeterminism::ORDERED);
        poisoned.hash = RecipeHash{0xDEADBEEFCAFEBABEULL};

        const auto* r = pool.intern(alloc_cap(), poisoned);
        // Stored hash is compute_recipe_hash of the semantic fields,
        // NOT the poisoned value supplied by the caller.
        assert(r->hash == crucible::compute_recipe_hash(poisoned));
        assert(r->hash != RecipeHash{0xDEADBEEFCAFEBABEULL});
    }

    // The probe comparison looks at the semantic fields only.  A refactor
    // that folded the stored hash into it would break this.
    {
        Arena arena{};
        RecipePool pool{RecipePool::ArenaBorrow{arena}, init_cap(), 32};

        NumericalRecipe fresh = mk(ScalarType::Float, ScalarType::Float, ReductionDeterminism::BITEXACT_STRICT);
        const auto* a = pool.intern(alloc_cap(), fresh);

        NumericalRecipe poisoned = fresh;
        poisoned.hash = RecipeHash{0xFFFFFFFFFFFFFFFFULL};
        const auto* b = pool.intern(alloc_cap(), poisoned);

        assert(a == b);
        assert(pool.size() == 1);
    }

    // Arena-owned recipes never move, so a pointer captured before a grow
    // stays valid after it.  Capacity 8 at 50% load grows after 4 entries,
    // so 10 insertions force two grows, to 16 and then 32.
    {
        Arena arena{};
        RecipePool pool{RecipePool::ArenaBorrow{arena}, init_cap(), 8};
        assert(pool.capacity() == 8);

        constexpr unsigned N = 10;
        const NumericalRecipe keys[N] = {
            mk(ScalarType::Float, ScalarType::Float, ReductionDeterminism::BITEXACT_STRICT),
            mk(ScalarType::Float, ScalarType::Half, ReductionDeterminism::BITEXACT_TC),
            mk(ScalarType::Float, ScalarType::BFloat16, ReductionDeterminism::BITEXACT_TC),
            mk(ScalarType::Float, ScalarType::Half, ReductionDeterminism::ORDERED),
            mk(ScalarType::Float, ScalarType::BFloat16, ReductionDeterminism::ORDERED),
            mk(ScalarType::Float, ScalarType::Float8_e4m3fn, ReductionDeterminism::ORDERED, ScalePolicy::PER_BLOCK_MX),
            mk(ScalarType::Float, ScalarType::Float8_e5m2, ReductionDeterminism::ORDERED, ScalePolicy::PER_BLOCK_MX),
            mk(ScalarType::Float, ScalarType::Half, ReductionDeterminism::UNORDERED),
            mk(ScalarType::Float, ScalarType::BFloat16, ReductionDeterminism::UNORDERED),
            mk(ScalarType::Float, ScalarType::Float, ReductionDeterminism::ORDERED),
        };

        const NumericalRecipe* ptrs[N]{};

        for (unsigned i = 0; i < N; ++i) {
            ptrs[i] = pool.intern(alloc_cap(), keys[i]);
            assert(ptrs[i] != nullptr);
        }

        assert(pool.capacity() >= 32);
        assert(pool.size() == N);

        for (unsigned i = 0; i < N; ++i) {
            for (unsigned j = i + 1; j < N; ++j) {
                assert(ptrs[i] != ptrs[j]);
            }
        }

        for (unsigned i = 0; i < N; ++i) {
            const auto* re = pool.intern(alloc_cap(), keys[i]);
            assert(re == ptrs[i]);
        }
        assert(pool.size() == N);  // no new entries on hits
    }

    // The 5 × 5 × 4 × 4 grid of dtype, dtype, determinism and scale gives
    // 400 distinct recipes.
    {
        Arena arena{};
        RecipePool pool{RecipePool::ArenaBorrow{arena}, init_cap(), 32};

        const ScalarType dtypes[] = {
            ScalarType::Float,         ScalarType::Half,        ScalarType::BFloat16,
            ScalarType::Float8_e4m3fn, ScalarType::Float8_e5m2,
        };
        const ReductionDeterminism dets[] = {
            ReductionDeterminism::UNORDERED,
            ReductionDeterminism::ORDERED,
            ReductionDeterminism::BITEXACT_TC,
            ReductionDeterminism::BITEXACT_STRICT,
        };
        const ScalePolicy scales[] = {
            ScalePolicy::NONE,
            ScalePolicy::PER_TENSOR_POST,
            ScalePolicy::PER_BLOCK_MX,
            ScalePolicy::PER_BLOCK_NVFP4,
        };

        std::unordered_set<const NumericalRecipe*> seen_ptrs;
        std::unordered_set<uint64_t> seen_hashes;

        for (auto accum : dtypes) {
            for (auto out : dtypes) {
                for (auto det : dets) {
                    for (auto sp : scales) {
                        const NumericalRecipe fields = mk(accum, out, det, sp);
                        const auto* p = pool.intern(alloc_cap(), fields);
                        const auto [it_p, inserted_p] = seen_ptrs.insert(p);
                        assert(inserted_p && "pool aliased two distinct recipes");
                        const auto [it_h, inserted_h] = seen_hashes.insert(p->hash.raw());
                        assert(inserted_h && "two distinct recipes share a hash");
                    }
                }
            }
        }

        assert(pool.size() == 400);
        assert(seen_ptrs.size() == 400);
        assert(pool.capacity() >= 1024);  // 400 @ 50% load requires capacity >= 800 → next pow2
    }

    // Pointer equality is a per-pool invariant, not a global one: each
    // pool owns its own arena storage.
    {
        Arena arena_a{};
        Arena arena_b{};
        RecipePool pool_a{RecipePool::ArenaBorrow{arena_a}, init_cap(), 32};
        RecipePool pool_b{RecipePool::ArenaBorrow{arena_b}, init_cap(), 32};

        const NumericalRecipe fields = mk(ScalarType::Float, ScalarType::Float, ReductionDeterminism::BITEXACT_STRICT);

        const auto* a = pool_a.intern(alloc_cap(), fields);
        const auto* b = pool_b.intern(alloc_cap(), fields);

        assert(a != b);
        assert(a->hash == b->hash);
        assert(a->accum_dtype == b->accum_dtype);
    }

    {
        Arena arena{};
        RecipePool pool{RecipePool::ArenaBorrow{arena}, init_cap(), 32};
        const auto* r =
            pool.intern(alloc_cap(), mk(ScalarType::Float, ScalarType::Float, ReductionDeterminism::ORDERED));
        static_assert(std::is_same_v<decltype(r), const NumericalRecipe*>,
                      "RecipePool::intern must return const NumericalRecipe*");
        assert(r != nullptr);
    }

    std::printf("test_recipe_pool: all tests passed\n");
    return 0;
}
