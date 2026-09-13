// Copyright (c) Grigory Evko
// Licensed under the Apache License, Version 2.0
//
// The two admission axes compose differently: tolerance is a chain, and
// recipe family is a partial order with a bottom and a wildcard top. Most of
// what follows probes the places where that difference shows.

#include <crucible/Arena.h>
#include <crucible/effects/Capabilities.h>
#include <crucible/NumericalRecipe.h>
#include <crucible/RecipePool.h>
#include <crucible/RecipeRegistry.h>
#include <crucible/safety/RecipeSpec.h>

#include "test_assert.h"
#include <cassert>
#include <cstdio>
#include <type_traits>
#include <utility>

namespace {

using crucible::Arena;
using crucible::NumericalRecipe;
using crucible::RecipeError;
using crucible::RecipeHash;
using crucible::RecipePool;
using crucible::RecipeRegistry;
using crucible::ReductionAlgo;
using crucible::ReductionDeterminism;
using crucible::ScalarType;
using crucible::recipe_family_of;
using crucible::tolerance_of;
using safety_Tolerance = crucible::safety::Tolerance;
using safety_RecipeFamily = crucible::safety::RecipeFamily;
using crucible::safety::RecipeSpec;

namespace names = crucible::recipe_names;

auto g_test = crucible::effects::testing::test();
auto g_init = crucible::effects::testing::init();
inline crucible::effects::Alloc alloc_cap() noexcept { return g_test.alloc; }
inline crucible::effects::Init init_cap() noexcept { return g_init; }

[[nodiscard]] inline auto entries_view(const RecipeRegistry& reg) noexcept { return reg.entries().value(); }

}  // namespace

[[gnu::cold]] int main() {
    // A concrete recipe always maps to one of the four named families. A
    // sentinel can only come out of the default arm.
    {
        NumericalRecipe r_pairwise{};
        r_pairwise.reduction_algo = ReductionAlgo::PAIRWISE;
        assert(recipe_family_of(r_pairwise) == safety_RecipeFamily::Pairwise);

        NumericalRecipe r_linear{};
        r_linear.reduction_algo = ReductionAlgo::LINEAR;
        assert(recipe_family_of(r_linear) == safety_RecipeFamily::Linear);

        NumericalRecipe r_kahan{};
        r_kahan.reduction_algo = ReductionAlgo::KAHAN;
        assert(recipe_family_of(r_kahan) == safety_RecipeFamily::Kahan);

        NumericalRecipe r_block{};
        r_block.reduction_algo = ReductionAlgo::BLOCK_STABLE;
        assert(recipe_family_of(r_block) == safety_RecipeFamily::BlockStable);

        static_assert(noexcept(recipe_family_of(std::declval<const NumericalRecipe&>())));
    }

    Arena arena{};
    RecipePool pool{RecipePool::ArenaBorrow{arena}, init_cap()};
    RecipeRegistry reg{RecipeRegistry::PoolBorrow{pool}, alloc_cap()};

    {
        auto spec = reg.by_name_spec(names::kF32Strict);
        assert(spec.has_value());
        static_assert(std::is_same_v<decltype(spec)::value_type, RecipeSpec<const NumericalRecipe*>>);
        assert(spec->peek() != nullptr);
        assert(spec->peek()->determinism == ReductionDeterminism::BITEXACT_STRICT);
        assert(spec->peek()->reduction_algo == ReductionAlgo::PAIRWISE);
        assert(spec->tolerance() == safety_Tolerance::BITEXACT);
        assert(spec->recipe_family() == safety_RecipeFamily::Pairwise);
    }

    // The lookup pairs the recipe with its two axes and transforms neither, so
    // both must equal what the recipe yields on its own.
    {
        for (const auto& entry : entries_view(reg)) {
            auto spec = reg.by_name_spec(entry.name);
            assert(spec.has_value());
            assert(spec->peek() == entry.recipe);
            assert(spec->tolerance() == tolerance_of(*entry.recipe));
            assert(spec->recipe_family() == recipe_family_of(*entry.recipe));
        }
    }

    // The spec is an additive overlay on the plain lookup, so an unknown key
    // surfaces the lookup error before either axis is examined.
    {
        auto spec = reg.by_name_spec("nonexistent_recipe");
        assert(!spec.has_value());
        assert(spec.error() == RecipeError::NameNotFound);
    }

    // The tolerance class follows the storage type, so this recipe lands in the
    // one-ULP class for FP16 rather than in the bit-exact class.
    {
        auto base = reg.by_name(names::kF16F32AccumTc);
        assert(base.has_value());
        const RecipeHash hash = (*base)->hash;

        auto spec = reg.by_hash_spec(hash);
        assert(spec.has_value());
        assert(spec->peek()->hash == hash);
        assert(spec->tolerance() == safety_Tolerance::ULP_FP16);
        assert(spec->recipe_family() == safety_RecipeFamily::Pairwise);
    }

    {
        const RecipeHash bogus{0xFEEDFACEDEADBEEFULL};
        auto spec = reg.by_hash_spec(bogus);
        assert(!spec.has_value());
        assert(spec.error() == RecipeError::HashNotFound);
    }

    // A recovery path resolves by hash while a live registry resolves by name.
    // The two must produce equal specs, or recovery would silently admit a
    // different set of requests.
    {
        for (const auto& entry : entries_view(reg)) {
            auto by_n = reg.by_name_spec(entry.name);
            auto by_h = reg.by_hash_spec(entry.recipe->hash);
            assert(by_n.has_value());
            assert(by_h.has_value());
            assert(by_n->peek() == by_h->peek());
            assert(by_n->tolerance() == by_h->tolerance());
            assert(by_n->recipe_family() == by_h->recipe_family());
            assert(*by_n == *by_h);
        }
    }

    // A request is admitted when it sits at or below the spec on both axes.
    {
        auto spec = reg.by_name_spec(names::kF16F32AccumTc).value();

        // Exact match on both axes.
        assert(spec.admits(safety_Tolerance::ULP_FP16, safety_RecipeFamily::Pairwise));

        // A weaker tolerance request: ULP_FP8 sits below ULP_FP16 on the chain.
        assert(spec.admits(safety_Tolerance::ULP_FP8, safety_RecipeFamily::Pairwise));

        // None is the family bottom, so a consumer that pins no family is
        // admitted by any producer.
        assert(spec.admits(safety_Tolerance::ULP_FP16, safety_RecipeFamily::None));
    }

    // The named families are siblings, incomparable in the partial order, so
    // agreeing on tolerance is not enough to be admitted.
    {
        auto spec = reg.by_name_spec(names::kF32Strict).value();
        assert(spec.tolerance() == safety_Tolerance::BITEXACT);
        assert(spec.recipe_family() == safety_RecipeFamily::Pairwise);

        assert(!spec.admits(safety_Tolerance::BITEXACT, safety_RecipeFamily::Kahan));
        assert(!spec.admits(safety_Tolerance::BITEXACT, safety_RecipeFamily::BlockStable));
        assert(!spec.admits(safety_Tolerance::BITEXACT, safety_RecipeFamily::Linear));
    }

    // The mirror case: a relaxed producer sits below a bit-exact request on the
    // tolerance chain, so agreeing on family is not enough either.
    {
        auto spec = reg.by_name_spec(names::kF32Ordered).value();
        assert(spec.tolerance() == safety_Tolerance::RELAXED);
        assert(spec.recipe_family() == safety_RecipeFamily::Pairwise);

        assert(!spec.admits(safety_Tolerance::BITEXACT, safety_RecipeFamily::Pairwise));
        assert(!spec.admits(safety_Tolerance::ULP_FP16, safety_RecipeFamily::Pairwise));
    }

    {
        // No starter recipe carries a sentinel family, so both specs here are
        // synthesized. This one sits at the family bottom.
        RecipeSpec<const NumericalRecipe*> bottom_spec{nullptr, safety_Tolerance::RELAXED, safety_RecipeFamily::None};
        assert(bottom_spec.admits(safety_Tolerance::RELAXED, safety_RecipeFamily::None));
        assert(!bottom_spec.admits(safety_Tolerance::RELAXED, safety_RecipeFamily::Pairwise));
        assert(!bottom_spec.admits(safety_Tolerance::ULP_FP16, safety_RecipeFamily::None));

        // And this one at the family top.
        RecipeSpec<const NumericalRecipe*> top_spec{nullptr, safety_Tolerance::BITEXACT, safety_RecipeFamily::Any};
        assert(top_spec.admits(safety_Tolerance::BITEXACT, safety_RecipeFamily::Pairwise));
        assert(top_spec.admits(safety_Tolerance::BITEXACT, safety_RecipeFamily::Kahan));
        assert(top_spec.admits(safety_Tolerance::BITEXACT, safety_RecipeFamily::BlockStable));
        assert(top_spec.admits(safety_Tolerance::ULP_FP8, safety_RecipeFamily::Linear));
    }

    // The join is idempotent on both axes.
    {
        auto a = reg.by_name_spec(names::kF16F32AccumTc).value();
        auto b = reg.by_name_spec(names::kF16F32AccumTc).value();
        auto c = a.combine_max(b);
        assert(c.tolerance() == safety_Tolerance::ULP_FP16);
        assert(c.recipe_family() == safety_RecipeFamily::Pairwise);
    }

    // Two siblings have no common family below the wildcard, so their join
    // promotes to it. Every starter recipe is Pairwise, which is why the second
    // spec is synthesized.
    {
        auto a = reg.by_name_spec(names::kF32Strict).value();
        RecipeSpec<const NumericalRecipe*> synth_kahan{a.peek(), safety_Tolerance::ULP_FP16,
                                                       safety_RecipeFamily::Kahan};

        auto joined = a.combine_max(synth_kahan);
        assert(joined.tolerance() == safety_Tolerance::BITEXACT);
        assert(joined.recipe_family() == safety_RecipeFamily::Any);
    }

    // Both axes are runtime data, so neither can collapse away. One byte per
    // axis rides alongside the pointer.
    {
        using SpecPtr = RecipeSpec<const NumericalRecipe*>;
        static_assert(sizeof(SpecPtr) >= sizeof(const NumericalRecipe*) + 2);
        static_assert(!std::is_same_v<SpecPtr, RecipeSpec<int>>);
    }

    // The spec is a passive overlay, so it carries the interned pointer itself
    // rather than a copy of the recipe.
    {
        auto base = reg.by_name(names::kF32Strict);
        assert(base.has_value());
        const NumericalRecipe* expected_ptr = *base;

        auto spec = reg.by_name_spec(names::kF32Strict).value();
        assert(spec.peek() == expected_ptr);

        const NumericalRecipe* moved = std::move(spec).consume();
        assert(moved == expected_ptr);
    }

    // Equality compares the pointer and both axes. Repeated lookups that
    // carried any hidden state would fail here, which replay depends on.
    {
        auto s1 = reg.by_name_spec(names::kBf16F32AccumTc).value();
        auto s2 = reg.by_name_spec(names::kBf16F32AccumTc).value();
        auto s3 = reg.by_hash_spec(s1.peek()->hash).value();
        assert(s1 == s2);
        assert(s1 == s3);

        // A different recipe must compare unequal, or the operator would be
        // vacuous.
        auto s4 = reg.by_name_spec(names::kF32Strict).value();
        assert(!(s1 == s4));
    }

    // Every cell of the grid is checked against the pointwise ordering on each
    // axis, so any drift between the admission body and the two lattices shows
    // up here. The sentinel families are left out and probed on their own.
    {
        using crucible::algebra::lattices::ToleranceLattice;
        using crucible::algebra::lattices::RecipeFamilyLattice;

        constexpr safety_Tolerance kTiers[] = {
            safety_Tolerance::RELAXED,  safety_Tolerance::ULP_INT8, safety_Tolerance::ULP_FP8,
            safety_Tolerance::ULP_FP16, safety_Tolerance::ULP_FP32, safety_Tolerance::ULP_FP64,
            safety_Tolerance::BITEXACT,
        };
        constexpr safety_RecipeFamily kFamilies[] = {
            safety_RecipeFamily::Linear,
            safety_RecipeFamily::Pairwise,
            safety_RecipeFamily::Kahan,
            safety_RecipeFamily::BlockStable,
        };

        int total_decisions = 0;
        for (const auto& entry : entries_view(reg)) {
            auto spec = reg.by_name_spec(entry.name).value();
            const auto spec_tier = spec.tolerance();
            const auto spec_fam = spec.recipe_family();
            for (auto req_tier : kTiers) {
                for (auto req_fam : kFamilies) {
                    const bool tier_ok = ToleranceLattice::leq(req_tier, spec_tier);
                    const bool fam_ok = RecipeFamilyLattice::leq(req_fam, spec_fam);
                    const bool expected = tier_ok && fam_ok;
                    assert(spec.admits(req_tier, req_fam) == expected);
                    ++total_decisions;
                }
            }
        }
        assert(total_decisions == 8 * 7 * 4);
    }

    // Every unordered pair of distinct named families, taken in both
    // directions, must reject. Holding the tolerance tier equal isolates the
    // family axis.
    {
        constexpr safety_RecipeFamily kFamilies[] = {
            safety_RecipeFamily::Linear,
            safety_RecipeFamily::Pairwise,
            safety_RecipeFamily::Kahan,
            safety_RecipeFamily::BlockStable,
        };

        int rejection_count = 0;
        for (size_t i = 0; i < 4; ++i) {
            for (size_t j = i + 1; j < 4; ++j) {
                RecipeSpec<const NumericalRecipe*> spec_i{nullptr, safety_Tolerance::BITEXACT, kFamilies[i]};
                RecipeSpec<const NumericalRecipe*> spec_j{nullptr, safety_Tolerance::BITEXACT, kFamilies[j]};
                assert(!spec_i.admits(safety_Tolerance::BITEXACT, kFamilies[j]));
                assert(!spec_j.admits(safety_Tolerance::BITEXACT, kFamilies[i]));
                rejection_count += 2;
            }
        }
        assert(rejection_count == 12);
    }

    // Every sibling pair joins to the wildcard, and the tolerance axis is left
    // untouched by that promotion.
    {
        constexpr safety_RecipeFamily kFamilies[] = {
            safety_RecipeFamily::Linear,
            safety_RecipeFamily::Pairwise,
            safety_RecipeFamily::Kahan,
            safety_RecipeFamily::BlockStable,
        };

        auto base_recipe = reg.by_name_spec(names::kF32Strict).value().peek();

        int join_count = 0;
        for (size_t i = 0; i < 4; ++i) {
            for (size_t j = i + 1; j < 4; ++j) {
                RecipeSpec<const NumericalRecipe*> spec_i{base_recipe, safety_Tolerance::ULP_FP16, kFamilies[i]};
                RecipeSpec<const NumericalRecipe*> spec_j{base_recipe, safety_Tolerance::ULP_FP16, kFamilies[j]};
                auto joined = spec_i.combine_max(spec_j);
                assert(joined.tolerance() == safety_Tolerance::ULP_FP16);
                assert(joined.recipe_family() == safety_RecipeFamily::Any);
                ++join_count;
            }
        }
        assert(join_count == 6);  // one per unordered pair of four families
    }

    // The two caps of the family axis: the wildcard admits every named family,
    // and the bottom admits nothing but itself.
    {
        constexpr safety_RecipeFamily kFamilies[] = {
            safety_RecipeFamily::Linear,
            safety_RecipeFamily::Pairwise,
            safety_RecipeFamily::Kahan,
            safety_RecipeFamily::BlockStable,
        };

        RecipeSpec<const NumericalRecipe*> any_spec{nullptr, safety_Tolerance::BITEXACT, safety_RecipeFamily::Any};
        for (auto fam : kFamilies) {
            assert(any_spec.admits(safety_Tolerance::BITEXACT, fam));
        }
        assert(any_spec.admits(safety_Tolerance::BITEXACT, safety_RecipeFamily::None));

        RecipeSpec<const NumericalRecipe*> none_spec{nullptr, safety_Tolerance::BITEXACT, safety_RecipeFamily::None};
        for (auto fam : kFamilies) {
            assert(!none_spec.admits(safety_Tolerance::BITEXACT, fam));
        }
        assert(none_spec.admits(safety_Tolerance::BITEXACT, safety_RecipeFamily::None));
    }

    // A spec recovered from nothing but a persisted hash must admit exactly
    // what the live one admits. The second arena and registry below stand in
    // for a process that starts with no registry of its own.
    {
        using crucible::algebra::lattices::ToleranceLattice;
        using crucible::algebra::lattices::RecipeFamilyLattice;

        constexpr safety_Tolerance kTiers[] = {
            safety_Tolerance::RELAXED,
            safety_Tolerance::ULP_FP16,
            safety_Tolerance::BITEXACT,
        };
        constexpr safety_RecipeFamily kFamilies[] = {
            safety_RecipeFamily::None,
            safety_RecipeFamily::Pairwise,
            safety_RecipeFamily::Kahan,
            safety_RecipeFamily::Any,
        };

        std::array<RecipeHash, RecipeRegistry::STARTER_COUNT> persisted{};
        for (std::size_t i = 0; i < RecipeRegistry::STARTER_COUNT; ++i) {
            persisted[i] = entries_view(reg)[i].recipe->hash;
        }

        Arena arena2{};
        RecipePool pool2{RecipePool::ArenaBorrow{arena2}, init_cap()};
        RecipeRegistry reg2{RecipeRegistry::PoolBorrow{pool2}, alloc_cap()};

        for (std::size_t i = 0; i < RecipeRegistry::STARTER_COUNT; ++i) {
            auto live_spec = reg.by_name_spec(entries_view(reg)[i].name).value();
            auto recovered = reg2.by_hash_spec(persisted[i]).value();

            assert(live_spec.tolerance() == recovered.tolerance());
            assert(live_spec.recipe_family() == recovered.recipe_family());

            for (auto t : kTiers) {
                for (auto f : kFamilies) {
                    assert(live_spec.admits(t, f) == recovered.admits(t, f));
                }
            }
        }
    }

    // The registry only ever hands out a raw pointer, which copies freely. A
    // move-only carrier is exercised here so the admission and join surface
    // stays honest for a payload that cannot be copied.
    {
        struct MoveOnlyT {
            int v{0};
            MoveOnlyT() = default;
            explicit MoveOnlyT(int x) : v{x} {}
            MoveOnlyT(MoveOnlyT&&) = default;
            MoveOnlyT& operator=(MoveOnlyT&&) = default;
            MoveOnlyT(MoveOnlyT const&) = delete;
            MoveOnlyT& operator=(MoveOnlyT const&) = delete;
        };

        static_assert(!std::is_copy_constructible_v<RecipeSpec<MoveOnlyT>>);
        static_assert(std::is_move_constructible_v<RecipeSpec<MoveOnlyT>>);

        RecipeSpec<MoveOnlyT> a{MoveOnlyT{42}, safety_Tolerance::ULP_FP16, safety_RecipeFamily::Kahan};
        RecipeSpec<MoveOnlyT> b{MoveOnlyT{99}, safety_Tolerance::BITEXACT, safety_RecipeFamily::Kahan};

        // The join takes the maximum on each axis and keeps the left carrier.
        auto joined = std::move(a).combine_max(b);
        assert(joined.tolerance() == safety_Tolerance::BITEXACT);
        assert(joined.recipe_family() == safety_RecipeFamily::Kahan);
        assert(joined.peek().v == 42);

        assert(joined.admits(safety_Tolerance::ULP_FP8, safety_RecipeFamily::Kahan));
        assert(!joined.admits(safety_Tolerance::ULP_FP16, safety_RecipeFamily::Pairwise));
    }

    // The sentinels on the request side, rather than on the spec side. A None
    // request sits at the bottom, so every spec admits it: that is a consumer
    // pinning no family at all. An Any request sits at the top and is below
    // nothing but itself, so only a wildcard spec admits it.
    {
        auto strict_spec = reg.by_name_spec(names::kF32Strict).value();
        assert(strict_spec.recipe_family() == safety_RecipeFamily::Pairwise);

        assert(strict_spec.admits(safety_Tolerance::BITEXACT, safety_RecipeFamily::None));
        assert(!strict_spec.admits(safety_Tolerance::BITEXACT, safety_RecipeFamily::Any));

        int none_admits = 0;
        int any_rejects = 0;
        for (const auto& entry : entries_view(reg)) {
            auto spec = reg.by_name_spec(entry.name).value();
            // The spec's own tier is the tightest one it admits, so probing at that
            // tier isolates the family axis.
            if (spec.admits(spec.tolerance(), safety_RecipeFamily::None)) ++none_admits;
            if (!spec.admits(spec.tolerance(), safety_RecipeFamily::Any)) ++any_rejects;
        }
        assert(none_admits == int{RecipeRegistry::STARTER_COUNT});
        assert(any_rejects == int{RecipeRegistry::STARTER_COUNT});

        // A wildcard spec admits both sentinel requests.
        RecipeSpec<const NumericalRecipe*> wildcard_spec{nullptr, safety_Tolerance::BITEXACT, safety_RecipeFamily::Any};
        assert(wildcard_spec.admits(safety_Tolerance::BITEXACT, safety_RecipeFamily::None));
        assert(wildcard_spec.admits(safety_Tolerance::BITEXACT, safety_RecipeFamily::Any));

        // A bottom spec admits only the bottom request. An Any request is above
        // it, and the ordering runs from request to spec, so it is rejected.
        RecipeSpec<const NumericalRecipe*> none_spec{nullptr, safety_Tolerance::BITEXACT, safety_RecipeFamily::None};
        assert(none_spec.admits(safety_Tolerance::BITEXACT, safety_RecipeFamily::None));
        assert(!none_spec.admits(safety_Tolerance::BITEXACT, safety_RecipeFamily::Any));
    }

    std::puts("ok");
    return 0;
}
