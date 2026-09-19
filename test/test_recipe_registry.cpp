// Copyright (c) Grigory Evko
// Licensed under the Apache License, Version 2.0
//
// The registry interns its starter specifications into a pool at
// construction, so that every lookup of the same recipe yields one
// canonical pointer no matter which way it is reached.
//
// Two things about a recipe are durable and therefore cannot move
// silently.  Its name is what source code pins, and its hash is what
// persisted entries record.  Changing either breaks consumers that have
// already committed to it, so the fields of each starter recipe and the
// hash it produces are both pinned below.

#include <crucible/Arena.h>
#include <crucible/effects/_Capabilities.h>
#include <crucible/effects/_EffectRow.h>
#include <crucible/NumericalRecipe.h>
#include <crucible/RecipePool.h>
#include <crucible/RecipeRegistry.h>

#include "test_assert.h"
#include <cinttypes>
#include <cstdio>
#include <span>
#include <string_view>
#include <type_traits>
#include <unordered_set>

namespace {

using crucible::Arena;
using crucible::NumericalRecipe;
using crucible::RecipeError;
using crucible::RecipePool;
using crucible::RecipeRegistry;
using crucible::ReductionAlgo;
using crucible::ReductionDeterminism;
using crucible::RoundingMode;
using crucible::ScalarType;
using crucible::ScalePolicy;
using crucible::SoftmaxRecurrence;

namespace names = crucible::recipe_names;

auto g_test = crucible::effects::testing::test();
auto g_init = crucible::effects::testing::init();
inline crucible::effects::Alloc alloc_cap() noexcept { return g_test.alloc; }
inline crucible::effects::Init init_cap() noexcept { return g_init; }

[[nodiscard]] inline auto entries_view(const RecipeRegistry& reg) noexcept { return reg.entries().value(); }

}  // namespace

[[gnu::cold]] int main() {
    {
        // The registry holds interior pointers into the pool's arena, so
        // copying or moving it would leave those pointers behind.
        static_assert(!std::is_copy_constructible_v<RecipeRegistry>);
        static_assert(!std::is_copy_assignable_v<RecipeRegistry>);
        static_assert(!std::is_move_constructible_v<RecipeRegistry>);
        static_assert(!std::is_move_assignable_v<RecipeRegistry>);

        static_assert(RecipeRegistry::STARTER_COUNT == 8);
        static_assert(RecipeRegistry::size() == 8);
        static_assert(std::is_same_v<RecipeRegistry::PoolBorrow, crucible::safety::BorrowedRef<RecipePool>>);
        static_assert(sizeof(RecipeRegistry::PoolBorrow) == sizeof(RecipePool*));
        static_assert(
            std::is_same_v<RecipeRegistry::Entries, crucible::safety::Tagged<std::span<const RecipeRegistry::Entry>,
                                                                             crucible::safety::source::JsonRegistry>>);
        static_assert(sizeof(RecipeRegistry::Entries) == sizeof(std::span<const RecipeRegistry::Entry>));
        static_assert(!std::is_convertible_v<std::span<const RecipeRegistry::Entry>, RecipeRegistry::Entries>);
        static_assert(crucible::effects::Subrow<RecipeRegistry::pure_projection_row, crucible::effects::Row<>>);
        static_assert(!crucible::effects::Subrow<crucible::effects::Row<crucible::effects::Effect::IO>,
                                                 RecipeRegistry::pure_projection_row>);
        static_assert(!crucible::effects::Subrow<crucible::effects::Row<crucible::effects::Effect::Init>,
                                                 RecipeRegistry::pure_projection_row>);

        static_assert(crucible::detail_recipe_registry::kStarterRecipes.size() == RecipeRegistry::STARTER_COUNT);
    }

    {
        Arena arena{};
        RecipePool pool{RecipePool::ArenaBorrow{arena}, init_cap()};
        RecipeRegistry reg{RecipeRegistry::PoolBorrow{pool}, alloc_cap()};

        assert(entries_view(reg).size() == RecipeRegistry::STARTER_COUNT);
        assert(pool.size() == RecipeRegistry::STARTER_COUNT);

        for (const auto& e : entries_view(reg)) {
            assert(!e.name.empty());
            assert(e.recipe != nullptr);
            // The stored hash must be the one its own fields produce, not a
            // value copied in from the specification.
            assert(e.recipe->hash == crucible::compute_recipe_hash(*e.recipe));
            assert(e.recipe->accum_dtype == ScalarType::Float);
        }
    }

    {
        Arena arena{};
        RecipePool pool{RecipePool::ArenaBorrow{arena}, init_cap()};
        RecipeRegistry reg{RecipeRegistry::PoolBorrow{pool}, alloc_cap()};

        std::unordered_set<const NumericalRecipe*> entry_ptrs;
        for (const auto& e : entries_view(reg)) {
            entry_ptrs.insert(e.recipe);
        }
        assert(entry_ptrs.size() == RecipeRegistry::STARTER_COUNT);

        const std::string_view known_names[] = {
            names::kF32Strict,
            names::kF32Ordered,
            names::kF16F32AccumTc,
            names::kF16F32AccumOrdered,
            names::kBf16F32AccumTc,
            names::kBf16F32AccumOrdered,
            names::kFp8E4m3F32AccumMxOrd,
            names::kFp8E5m2F32AccumMxOrd,
        };

        for (auto n : known_names) {
            auto r = reg.by_name(n);
            assert(r.has_value() && "starter recipe lookup should hit");
            assert(*r != nullptr);
            // Lookup by name must land on a pointer the pool already owns,
            // not on a fresh copy of the same recipe.
            assert(entry_ptrs.contains(*r));
        }

        auto first = reg.by_name(names::kF16F32AccumTc);
        auto second = reg.by_name(names::kF16F32AccumTc);
        assert(first.has_value() && second.has_value());
        assert(*first == *second);
    }

    // Lookup by name is exact and case-sensitive, so the near-misses
    // below have to miss just as firmly as the nonsense ones.
    {
        Arena arena{};
        RecipePool pool{RecipePool::ArenaBorrow{arena}, init_cap()};
        RecipeRegistry reg{RecipeRegistry::PoolBorrow{pool}, alloc_cap()};

        const std::string_view missing[] = {
            "F32_STRICT",  // wrong case
            "f32_Strict",  // mixed case
            "f32-strict",  // dash instead of underscore
            "f32_strict ",  // trailing whitespace
            " f32_strict",  // leading whitespace
            "",  // empty
            "this_recipe_does_not_exist",  // bogus
            "f32",  // truncated
            "f32_strict_unknown_suffix",  // extended
        };

        for (auto n : missing) {
            auto r = reg.by_name(n);
            assert(!r.has_value() && "missing recipe must not hit");
            assert(r.error() == RecipeError::NameNotFound);
        }
    }

    // A name is a contract.  Swapping the dtype or the determinism tier
    // behind an existing name changes what every consumer that pinned it
    // receives, without any of them changing a line.
    {
        Arena arena{};
        RecipePool pool{RecipePool::ArenaBorrow{arena}, init_cap()};
        RecipeRegistry reg{RecipeRegistry::PoolBorrow{pool}, alloc_cap()};

        {
            auto r = reg.by_name(names::kF32Strict);
            assert(r.has_value());
            const auto& x = **r;
            assert(x.accum_dtype == ScalarType::Float);
            assert(x.out_dtype == ScalarType::Float);
            assert(x.determinism == ReductionDeterminism::BITEXACT_STRICT);
            assert(x.scale_policy == ScalePolicy::NONE);
            assert(x.softmax == SoftmaxRecurrence::ONLINE_LSE);
            assert(crucible::is_bitexact(x.determinism));
            // The strictest determinism tier rules tensor cores out.
            assert(!crucible::permits_tensor_cores(x.determinism));
        }

        {
            auto r = reg.by_name(names::kF16F32AccumTc);
            assert(r.has_value());
            const auto& x = **r;
            assert(x.accum_dtype == ScalarType::Float);
            assert(x.out_dtype == ScalarType::Half);
            assert(x.determinism == ReductionDeterminism::BITEXACT_TC);
            assert(crucible::is_bitexact(x.determinism));
            assert(crucible::permits_tensor_cores(x.determinism));
        }

        {
            auto r = reg.by_name(names::kBf16F32AccumTc);
            assert(r.has_value());
            const auto& x = **r;
            assert(x.accum_dtype == ScalarType::Float);
            assert(x.out_dtype == ScalarType::BFloat16);
            assert(x.determinism == ReductionDeterminism::BITEXACT_TC);
        }

        {
            auto r = reg.by_name(names::kFp8E4m3F32AccumMxOrd);
            assert(r.has_value());
            const auto& x = **r;
            assert(x.accum_dtype == ScalarType::Float);
            assert(x.out_dtype == ScalarType::Float8_e4m3fn);
            assert(x.scale_policy == ScalePolicy::PER_BLOCK_MX);
            assert(x.softmax == SoftmaxRecurrence::NAIVE);
            assert(x.determinism == ReductionDeterminism::ORDERED);
            // A block-scaled format cannot reach a bit-exact tier, because
            // the scale factor is itself derived per block.
            assert(!crucible::is_bitexact(x.determinism));
            assert(crucible::allows_block_scaled_formats(x.determinism));
        }

        {
            auto r = reg.by_name(names::kFp8E5m2F32AccumMxOrd);
            assert(r.has_value());
            const auto& x = **r;
            assert(x.out_dtype == ScalarType::Float8_e5m2);
            assert(x.scale_policy == ScalePolicy::PER_BLOCK_MX);
            assert(x.determinism == ReductionDeterminism::ORDERED);
        }
    }

    // Lookup by name is a linear scan, so a duplicate name would not be
    // an error: the first match would win and the second recipe would
    // become unreachable without anything reporting it.
    {
        Arena arena{};
        RecipePool pool{RecipePool::ArenaBorrow{arena}, init_cap()};
        RecipeRegistry reg{RecipeRegistry::PoolBorrow{pool}, alloc_cap()};

        std::unordered_set<std::string_view> seen;
        for (const auto& e : entries_view(reg)) {
            const auto [it, inserted] = seen.insert(e.name);
            assert(inserted && "starter recipe name collision");
        }
        assert(seen.size() == RecipeRegistry::STARTER_COUNT);
    }

    // Interning is by content, so two starter specifications that ended
    // up with identical fields would collapse into one pooled recipe and
    // the pool would simply hold fewer entries than there are names.
    {
        Arena arena{};
        RecipePool pool{RecipePool::ArenaBorrow{arena}, init_cap()};
        RecipeRegistry reg{RecipeRegistry::PoolBorrow{pool}, alloc_cap()};

        std::unordered_set<const NumericalRecipe*> ptrs;
        std::unordered_set<uint64_t> hashes;

        for (const auto& e : entries_view(reg)) {
            const auto [it_p, inserted_p] = ptrs.insert(e.recipe);
            assert(inserted_p && "two starter recipes intern to the same pointer");
            const auto [it_h, inserted_h] = hashes.insert(e.recipe->hash.raw());
            assert(inserted_h && "two starter recipes share a Family-A hash");
        }

        assert(ptrs.size() == RecipeRegistry::STARTER_COUNT);
        assert(hashes.size() == RecipeRegistry::STARTER_COUNT);
    }

    // Persisted entries refer to a recipe by its hash, so these values
    // are a wire format.  A change to a recipe's fields moves its hash,
    // and a change to the hash function moves all of them; either way
    // anything already written refers to a recipe nothing can resolve.
    //
    // The failure message below says what to do once such a change has
    // been audited and accepted.
    {
        Arena arena{};
        RecipePool pool{RecipePool::ArenaBorrow{arena}, init_cap()};
        RecipeRegistry reg{RecipeRegistry::PoolBorrow{pool}, alloc_cap()};

        struct Golden {
            std::string_view name;
            uint64_t expected;
        };
        const Golden goldens[] = {
            {names::kF32Strict, 0xce0eb0cd5c376b79ULL},
            {names::kF32Ordered, 0x493a001a4ad235a7ULL},
            {names::kF16F32AccumTc, 0xc737d38ea930d024ULL},
            {names::kF16F32AccumOrdered, 0xcc51530cf248c3e3ULL},
            {names::kBf16F32AccumTc, 0x0acf66d27c444494ULL},
            {names::kBf16F32AccumOrdered, 0xc674007dc8716618ULL},
            {names::kFp8E4m3F32AccumMxOrd, 0x5ba4c6b1bdefc89dULL},
            {names::kFp8E5m2F32AccumMxOrd, 0xb8e80f767e9cfb21ULL},
        };

        bool any_drift = false;
        for (auto g : goldens) {
            auto r = reg.by_name(g.name);
            assert(r.has_value());
            const uint64_t actual = (*r)->hash.raw();
            if (actual != g.expected) {
                std::fprintf(stderr,
                             "recipe-hash golden DRIFT for %.*s: expected 0x%016" PRIx64 ", got 0x%016" PRIx64 "\n",
                             int(g.name.size()), g.name.data(), g.expected, actual);
                any_drift = true;
            }
        }
        if (any_drift) {
            std::fprintf(stderr, "  update the pinned golden hashes\n"
                                 "  audit every persisted consumer of the drifted hashes\n"
                                 "  consider bumping the persisted-format version\n");
            assert(!any_drift && "recipe-hash golden mismatch");
        }
    }

    // The pool is the sole authority on recipe identity.  Interning a
    // starter's fields by hand has to reach the pointer the registry
    // already holds, rather than adding a second copy beside it.
    {
        Arena arena{};
        RecipePool pool{RecipePool::ArenaBorrow{arena}, init_cap()};
        RecipeRegistry reg{RecipeRegistry::PoolBorrow{pool}, alloc_cap()};

        for (const auto& spec : crucible::detail_recipe_registry::kStarterRecipes) {
            // Re-intern the starter spec's fields → must hit the canonical
            // pointer already stored by the registry.
            const auto* via_pool = pool.intern(alloc_cap(), spec.fields);
            auto via_reg = reg.by_name(spec.name);
            assert(via_reg.has_value());
            assert(via_pool == *via_reg);
        }
        // An unchanged pool size is what proves every intern was a hit.
        assert(pool.size() == RecipeRegistry::STARTER_COUNT);
    }

    // Lookup by hash is how a persisted entry finds its recipe again, so
    // it has to reach the same pointer as lookup by name.
    {
        Arena arena{};
        RecipePool pool{RecipePool::ArenaBorrow{arena}, init_cap()};
        RecipeRegistry reg{RecipeRegistry::PoolBorrow{pool}, alloc_cap()};

        for (const auto& entry : entries_view(reg)) {
            auto via_hash = reg.by_hash(entry.recipe->hash);
            assert(via_hash.has_value());
            assert(*via_hash == entry.recipe);

            auto via_name = reg.by_name(entry.name);
            assert(via_name.has_value());
            assert(*via_hash == *via_name);
        }

        // The two misses report different errors on purpose.  A name miss
        // is a caller's typo; a hash miss is a stored entry referring to a
        // recipe this build does not have, and the two want different
        // handling.
        const crucible::RecipeHash bogus_hashes[] = {
            crucible::RecipeHash{0xDEADBEEFCAFEBABEULL},
            crucible::RecipeHash{0x0000000000000000ULL},
            crucible::RecipeHash{0x0000000000000001ULL},
            crucible::RecipeHash{0xFFFFFFFFFFFFFFFEULL},
        };
        for (auto h : bogus_hashes) {
            auto r = reg.by_hash(h);
            assert(!r.has_value());
            assert(r.error() == RecipeError::HashNotFound);
        }

        // The sentinel value marks the end of a region, so a real recipe
        // resolving to it would make a region terminate early.
        auto sentinel_miss = reg.by_hash(crucible::RecipeHash::sentinel());
        assert(!sentinel_miss.has_value());
        assert(sentinel_miss.error() == RecipeError::HashNotFound);
    }

    // Identity is per pool and equality is by content.  Two registries
    // over separate pools therefore hand out different pointers for the
    // same recipe, and those recipes still hash alike.
    {
        Arena arena_a{};
        Arena arena_b{};
        RecipePool pool_a{RecipePool::ArenaBorrow{arena_a}, init_cap()};
        RecipePool pool_b{RecipePool::ArenaBorrow{arena_b}, init_cap()};
        RecipeRegistry reg_a{RecipeRegistry::PoolBorrow{pool_a}, alloc_cap()};
        RecipeRegistry reg_b{RecipeRegistry::PoolBorrow{pool_b}, alloc_cap()};

        auto a = reg_a.by_name(names::kF16F32AccumTc);
        auto b = reg_b.by_name(names::kF16F32AccumTc);
        assert(a.has_value() && b.has_value());
        assert(*a != *b);
        assert((*a)->hash == (*b)->hash);
        assert((*a)->out_dtype == (*b)->out_dtype);
    }

    std::printf("test_recipe_registry: all tests passed\n");
    return 0;
}
