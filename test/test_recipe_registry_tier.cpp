// Copyright (c) Grigory Evko
// Licensed under the Apache License, Version 2.0

#include <crucible/NumericalRecipe.h>
#include <crucible/RecipePool.h>
#include <crucible/RecipeRegistry.h>
#include <fixy/Bands.h>
#include <fixy/Borrowed.h>
#include <fixy/Core.h>
#include <foundation/effects/Effect.h>

#include "test_assert.h"
#include <cstdio>
#include <type_traits>
#include <utility>

namespace {

using crucible::NumericalRecipe;
using crucible::RecipeError;
using crucible::RecipeHash;
using crucible::RecipePool;
using crucible::RecipeRegistry;
using crucible::ReductionDeterminism;
using crucible::ScalarType;
using crucible::tolerance_for_dtype;
using crucible::tolerance_of;
using ::fixy::BumpArena;
using ::fixy::NumericalTier;
using ::fixy::Tolerance;
using ::fixy::ToleranceLattice;

namespace names = crucible::recipe_names;

auto g_test = ::foundation::effects::testing::test();
auto g_init = ::foundation::effects::testing::init();
inline ::foundation::effects::Alloc alloc_cap() noexcept { return g_test.alloc; }
inline ::foundation::effects::Init init_cap() noexcept { return g_init; }

[[nodiscard]] inline auto entries_view(const RecipeRegistry& reg) noexcept { return reg.entries().value(); }

using RecipeRef = RecipeRegistry::RecipeRef;

template <Tolerance T>
using PinnedLookup = ::fixy::Result<NumericalTier<T, RecipeRef>, RecipeError>;

// The value of a lookup that hits.  A miss ends the test.
template <class Found>
[[nodiscard]] auto hit(Found found) noexcept {
    return std::move(found).expect("the lookup hits");
}

// The error of a lookup that misses.  A hit ends the test.
template <class Found>
[[nodiscard]] RecipeError miss(Found found) noexcept {
    return std::move(found).err().expect("the lookup misses");
}

}  // namespace

[[gnu::cold]] int main() {
    {
        static_assert(tolerance_for_dtype(ScalarType::Double) == Tolerance::ULP_FP64);
        static_assert(tolerance_for_dtype(ScalarType::Float) == Tolerance::ULP_FP32);
        static_assert(tolerance_for_dtype(ScalarType::Half) == Tolerance::ULP_FP16);
        static_assert(tolerance_for_dtype(ScalarType::BFloat16) == Tolerance::ULP_FP16);
        static_assert(tolerance_for_dtype(ScalarType::Float8_e4m3fn) == Tolerance::ULP_FP8);
        static_assert(tolerance_for_dtype(ScalarType::Float8_e5m2) == Tolerance::ULP_FP8);
        static_assert(tolerance_for_dtype(ScalarType::Char) == Tolerance::ULP_INT8);
        static_assert(tolerance_for_dtype(ScalarType::Byte) == Tolerance::ULP_INT8);
        static_assert(tolerance_for_dtype(ScalarType::Int) == Tolerance::RELAXED);
        static_assert(tolerance_for_dtype(ScalarType::Long) == Tolerance::RELAXED);
        static_assert(tolerance_for_dtype(ScalarType::Bool) == Tolerance::RELAXED);
        static_assert(tolerance_for_dtype(ScalarType::Undefined) == Tolerance::RELAXED);
    }

    // An ordered reduction can land up to 4 ULP away, which is coarser
    // than any of the 1-ULP-at-precision classes, so it maps to the
    // weakest class rather than to the one its output dtype suggests.
    {
        NumericalRecipe r_strict{};
        r_strict.determinism = ReductionDeterminism::BITEXACT_STRICT;
        r_strict.out_dtype = ScalarType::Float;
        assert(tolerance_of(r_strict) == Tolerance::BITEXACT);

        NumericalRecipe r_tc_fp16{};
        r_tc_fp16.determinism = ReductionDeterminism::BITEXACT_TC;
        r_tc_fp16.out_dtype = ScalarType::Half;
        assert(tolerance_of(r_tc_fp16) == Tolerance::ULP_FP16);

        NumericalRecipe r_tc_bf16{};
        r_tc_bf16.determinism = ReductionDeterminism::BITEXACT_TC;
        r_tc_bf16.out_dtype = ScalarType::BFloat16;
        assert(tolerance_of(r_tc_bf16) == Tolerance::ULP_FP16);

        NumericalRecipe r_ordered{};
        r_ordered.determinism = ReductionDeterminism::ORDERED;
        r_ordered.out_dtype = ScalarType::Float;
        assert(tolerance_of(r_ordered) == Tolerance::RELAXED);

        NumericalRecipe r_unordered{};
        r_unordered.determinism = ReductionDeterminism::UNORDERED;
        r_unordered.out_dtype = ScalarType::Float;
        assert(tolerance_of(r_unordered) == Tolerance::RELAXED);
    }

    BumpArena arena{};
    RecipePool pool{::fixy::mint_borrowed_ref(arena), init_cap()};
    RecipeRegistry reg{::fixy::mint_borrowed_ref(pool), alloc_cap()};

    {
        auto pinned = reg.by_name_pinned<Tolerance::BITEXACT>(names::kF32Strict);
        static_assert(std::is_same_v<decltype(pinned), PinnedLookup<Tolerance::BITEXACT>>);
        auto const tier = hit(std::move(pinned));
        assert(tier.peek()->determinism == ReductionDeterminism::BITEXACT_STRICT);
    }

    // A recipe admits at any pin weaker than its own class.
    {
        auto pinned = reg.by_name_pinned<Tolerance::RELAXED>(names::kF32Strict);
        assert(pinned.is_ok());
        static_assert(std::is_same_v<decltype(pinned), PinnedLookup<Tolerance::RELAXED>>);
    }

    // The reverse direction is the bug this gate exists for: an ordered
    // recipe, several ULP wide, reaching a consumer that was written
    // against bit-exact results.
    { assert(miss(reg.by_name_pinned<Tolerance::BITEXACT>(names::kF32Ordered)) == RecipeError::ToleranceMismatch); }

    // The name is the primary key and the tolerance is a second gate
    // behind it, so an unknown name reports as an unknown name at every
    // pin strength.  Pinning only adds a gate, it never changes what
    // the unpinned lookup would have reported.
    {
        assert(miss(reg.by_name_pinned<Tolerance::RELAXED>("nonexistent_recipe")) == RecipeError::NameNotFound);
        assert(miss(reg.by_name_pinned<Tolerance::BITEXACT>("does_not_exist")) == RecipeError::NameNotFound);
    }

    {
        const RecipeHash hash = hit(reg.by_name(names::kF32Strict))->hash;
        auto const tier = hit(reg.by_hash_pinned<Tolerance::BITEXACT>(hash));
        assert(tier.peek()->hash == hash);
    }

    // This recipe is ordered, so its class is the weakest one, not the
    // one its half-precision storage would suggest.  A half-precision
    // pin is therefore up the lattice and rejects.
    {
        const RecipeHash hash = hit(reg.by_name(names::kF16F32AccumOrdered))->hash;
        assert(miss(reg.by_hash_pinned<Tolerance::ULP_FP16>(hash)) == RecipeError::ToleranceMismatch);
    }

    // A hash no recipe carries reports as a missing hash, never as a
    // tolerance mismatch: there is no recipe to compare a class against.
    {
        const RecipeHash bogus{0xDEADBEEFCAFEBABEULL};
        assert(miss(reg.by_hash_pinned<Tolerance::RELAXED>(bogus)) == RecipeError::HashNotFound);
    }

    {
        for (const auto& entry : entries_view(reg)) {
            // RELAXED is the bottom of the lattice, so every recipe
            // satisfies it.
            auto relaxed_pin = reg.by_name_pinned<Tolerance::RELAXED>(entry.name);
            assert(relaxed_pin.is_ok());

            // BITEXACT is the top, so only a strictly bit-exact recipe
            // satisfies it.
            auto bitexact_pin = reg.by_name_pinned<Tolerance::BITEXACT>(entry.name);
            const bool is_strict = entry.recipe->determinism == ReductionDeterminism::BITEXACT_STRICT;
            assert(bitexact_pin.is_ok() == is_strict);
        }
    }

    {
        int admits = 0;
        for (const auto& entry : entries_view(reg)) {
            auto pinned = reg.by_name_pinned<Tolerance::RELAXED>(entry.name);
            if (pinned.is_ok()) ++admits;
        }
        assert(admits == int{RecipeRegistry::STARTER_COUNT});
    }

    // Exactly one starter recipe is strictly bit-exact, which is where
    // the expected count of one comes from.
    {
        int strict_admits = 0;
        for (const auto& entry : entries_view(reg)) {
            auto pinned = reg.by_name_pinned<Tolerance::BITEXACT>(entry.name);
            if (pinned.is_ok()) ++strict_admits;
        }
        assert(strict_admits == 1);
    }

    {
        using PinnedBitexact = NumericalTier<Tolerance::BITEXACT, RecipeRef>;
        using PinnedRelaxed = NumericalTier<Tolerance::RELAXED, RecipeRef>;
        using PinnedFp16 = NumericalTier<Tolerance::ULP_FP16, RecipeRef>;
        static_assert(sizeof(PinnedBitexact) == sizeof(RecipeRef));
        static_assert(sizeof(PinnedRelaxed) == sizeof(RecipeRef));
        static_assert(sizeof(PinnedFp16) == sizeof(RecipeRef));
    }

    {
        RecipeRef const expected_ref = hit(reg.by_name(names::kF32Strict));

        auto pinned = hit(reg.by_name_pinned<Tolerance::BITEXACT>(names::kF32Strict));
        assert(::fixy::same_object(pinned.peek(), expected_ref));

        RecipeRef const moved = std::move(pinned).consume();
        assert(::fixy::same_object(moved, expected_ref));
    }

    // The runtime rejection above has a compile-time counterpart: a
    // consumer that demands the top class cannot be handed a wrapper
    // pinned at the bottom one.
    {
        using PinnedBitexact = NumericalTier<Tolerance::BITEXACT, RecipeRef>;
        using PinnedRelaxed = NumericalTier<Tolerance::RELAXED, RecipeRef>;
        static_assert(!std::is_same_v<PinnedBitexact, PinnedRelaxed>);

        static_assert(::fixy::satisfies_v<PinnedBitexact, Tolerance::RELAXED>);
        static_assert(!::fixy::satisfies_v<PinnedRelaxed, Tolerance::BITEXACT>);
    }

    // The lattice runs RELAXED, ULP_INT8, ULP_FP8, ULP_FP16, ULP_FP32,
    // ULP_FP64, BITEXACT from bottom to top, which is the order the
    // chain below walks.  Each step weakens the pin by one class and
    // stays admissible at the next consumer.
    {
        auto bitexact = ::fixy::mint_band<NumericalTier<Tolerance::BITEXACT, int>>(42);
        auto fp64 = ::fixy::relax<Tolerance::ULP_FP64>(std::move(bitexact));
        auto fp32 = ::fixy::relax<Tolerance::ULP_FP32>(std::move(fp64));
        auto fp16 = ::fixy::relax<Tolerance::ULP_FP16>(std::move(fp32));
        auto fp8 = ::fixy::relax<Tolerance::ULP_FP8>(std::move(fp16));
        auto int8 = ::fixy::relax<Tolerance::ULP_INT8>(std::move(fp8));
        auto relaxed = ::fixy::relax<Tolerance::RELAXED>(std::move(int8));

        static_assert(std::is_same_v<decltype(fp64), NumericalTier<Tolerance::ULP_FP64, int>>);
        static_assert(std::is_same_v<decltype(fp32), NumericalTier<Tolerance::ULP_FP32, int>>);
        static_assert(std::is_same_v<decltype(fp16), NumericalTier<Tolerance::ULP_FP16, int>>);
        static_assert(std::is_same_v<decltype(fp8), NumericalTier<Tolerance::ULP_FP8, int>>);
        static_assert(std::is_same_v<decltype(int8), NumericalTier<Tolerance::ULP_INT8, int>>);
        static_assert(std::is_same_v<decltype(relaxed), NumericalTier<Tolerance::RELAXED, int>>);

        int v = std::move(relaxed).consume();
        assert(v == 42);
    }

    // The band traits must report the tier that the grade of the type
    // pins, through every cv-ref qualification.  Diagnostic printing and
    // dispatcher introspection read the traits, so a drift between the
    // two surfaces silently as a wrong tier in a message.
    {
        using ::fixy::band_tier_v;
        using ::fixy::is_band_of_v;

        using NT_BX = NumericalTier<Tolerance::BITEXACT, RecipeRef>;
        using NT_RX = NumericalTier<Tolerance::RELAXED, RecipeRef>;
        using NT_F16 = NumericalTier<Tolerance::ULP_FP16, RecipeRef>;

        static_assert(band_tier_v<NT_BX> == NT_BX::lattice_type::pinned);
        static_assert(band_tier_v<NT_RX> == NT_RX::lattice_type::pinned);
        static_assert(band_tier_v<NT_F16> == NT_F16::lattice_type::pinned);
        static_assert(band_tier_v<NT_BX> == Tolerance::BITEXACT);
        static_assert(band_tier_v<NT_BX&> == Tolerance::BITEXACT);
        static_assert(band_tier_v<NT_BX const&> == Tolerance::BITEXACT);
        static_assert(band_tier_v<NT_BX&&> == Tolerance::BITEXACT);

        static_assert(!is_band_of_v<ToleranceLattice, int>);
        static_assert(!is_band_of_v<ToleranceLattice, RecipeRef>);
        static_assert(is_band_of_v<ToleranceLattice, NT_BX>);
    }

    // The two lookup paths are separate code, so they can drift: one
    // could skip the tolerance gate, or hand back a second copy of the
    // recipe.  Resolving one recipe both ways and comparing closes
    // that.
    {
        RecipeRef const base = hit(reg.by_name(names::kF32Strict));
        const RecipeHash hash = base->hash;

        auto by_name_pin = reg.by_name_pinned<Tolerance::BITEXACT>(names::kF32Strict);
        auto by_hash_pin = reg.by_hash_pinned<Tolerance::BITEXACT>(hash);
        static_assert(std::is_same_v<decltype(by_name_pin), decltype(by_hash_pin)>);
        auto const name_tier = hit(std::move(by_name_pin));
        auto const hash_tier = hit(std::move(by_hash_pin));
        assert(::fixy::same_object(name_tier.peek(), hash_tier.peek()));
        assert(::fixy::same_object(name_tier.peek(), base));
    }

    {
        using T = Tolerance;
        using ::fixy::satisfies_v;

        using BX = NumericalTier<T::BITEXACT, int>;
        static_assert(satisfies_v<BX, T::BITEXACT>);
        static_assert(satisfies_v<BX, T::ULP_FP64>);
        static_assert(satisfies_v<BX, T::ULP_FP32>);
        static_assert(satisfies_v<BX, T::ULP_FP16>);
        static_assert(satisfies_v<BX, T::ULP_FP8>);
        static_assert(satisfies_v<BX, T::ULP_INT8>);
        static_assert(satisfies_v<BX, T::RELAXED>);

        using F64 = NumericalTier<T::ULP_FP64, int>;
        static_assert(!satisfies_v<F64, T::BITEXACT>);
        static_assert(satisfies_v<F64, T::ULP_FP64>);
        static_assert(satisfies_v<F64, T::ULP_FP32>);
        static_assert(satisfies_v<F64, T::ULP_FP16>);
        static_assert(satisfies_v<F64, T::ULP_FP8>);
        static_assert(satisfies_v<F64, T::ULP_INT8>);
        static_assert(satisfies_v<F64, T::RELAXED>);

        using F32 = NumericalTier<T::ULP_FP32, int>;
        static_assert(!satisfies_v<F32, T::BITEXACT>);
        static_assert(!satisfies_v<F32, T::ULP_FP64>);
        static_assert(satisfies_v<F32, T::ULP_FP32>);
        static_assert(satisfies_v<F32, T::ULP_FP16>);
        static_assert(satisfies_v<F32, T::ULP_FP8>);
        static_assert(satisfies_v<F32, T::ULP_INT8>);
        static_assert(satisfies_v<F32, T::RELAXED>);

        using F16 = NumericalTier<T::ULP_FP16, int>;
        static_assert(!satisfies_v<F16, T::BITEXACT>);
        static_assert(!satisfies_v<F16, T::ULP_FP64>);
        static_assert(!satisfies_v<F16, T::ULP_FP32>);
        static_assert(satisfies_v<F16, T::ULP_FP16>);
        static_assert(satisfies_v<F16, T::ULP_FP8>);
        static_assert(satisfies_v<F16, T::ULP_INT8>);
        static_assert(satisfies_v<F16, T::RELAXED>);

        using F8 = NumericalTier<T::ULP_FP8, int>;
        static_assert(!satisfies_v<F8, T::BITEXACT>);
        static_assert(!satisfies_v<F8, T::ULP_FP64>);
        static_assert(!satisfies_v<F8, T::ULP_FP32>);
        static_assert(!satisfies_v<F8, T::ULP_FP16>);
        static_assert(satisfies_v<F8, T::ULP_FP8>);
        static_assert(satisfies_v<F8, T::ULP_INT8>);
        static_assert(satisfies_v<F8, T::RELAXED>);

        using I8 = NumericalTier<T::ULP_INT8, int>;
        static_assert(!satisfies_v<I8, T::BITEXACT>);
        static_assert(!satisfies_v<I8, T::ULP_FP64>);
        static_assert(!satisfies_v<I8, T::ULP_FP32>);
        static_assert(!satisfies_v<I8, T::ULP_FP16>);
        static_assert(!satisfies_v<I8, T::ULP_FP8>);
        static_assert(satisfies_v<I8, T::ULP_INT8>);
        static_assert(satisfies_v<I8, T::RELAXED>);

        using RX = NumericalTier<T::RELAXED, int>;
        static_assert(!satisfies_v<RX, T::BITEXACT>);
        static_assert(!satisfies_v<RX, T::ULP_FP64>);
        static_assert(!satisfies_v<RX, T::ULP_FP32>);
        static_assert(!satisfies_v<RX, T::ULP_FP16>);
        static_assert(!satisfies_v<RX, T::ULP_FP8>);
        static_assert(!satisfies_v<RX, T::ULP_INT8>);
        static_assert(satisfies_v<RX, T::RELAXED>);
    }

    // The payload the registry pins is a Ref, which copies, so nothing else
    // in this file exercises a move-only payload.  The wrapper has to
    // carry one for the sake of everything else that composes with it.
    {
        struct MoveOnlyT {
            int v{0};
            constexpr MoveOnlyT() = default;
            constexpr explicit MoveOnlyT(int x) : v{x} {}
            constexpr MoveOnlyT(MoveOnlyT&&) = default;
            constexpr MoveOnlyT& operator=(MoveOnlyT&&) = default;
            MoveOnlyT(MoveOnlyT const&) = delete;
            MoveOnlyT& operator=(MoveOnlyT const&) = delete;
        };

        using NT_MO = NumericalTier<Tolerance::BITEXACT, MoveOnlyT>;
        static_assert(!std::is_copy_constructible_v<NT_MO>);
        static_assert(std::is_move_constructible_v<NT_MO>);

        NT_MO src = ::fixy::mint_band<NT_MO>(MoveOnlyT{77});
        auto relaxed = ::fixy::relax<Tolerance::RELAXED>(std::move(src));
        static_assert(std::is_same_v<decltype(relaxed), NumericalTier<Tolerance::RELAXED, MoveOnlyT>>);
        MoveOnlyT v = std::move(relaxed).consume();
        assert(v.v == 77);
    }

    // The pool interns recipes, so the same name always resolves to the
    // same address.  Two identical lookups that disagree would mean the
    // pinned path carries state of its own.
    {
        auto const pin1 = hit(reg.by_name_pinned<Tolerance::BITEXACT>(names::kF32Strict));
        auto const pin2 = hit(reg.by_name_pinned<Tolerance::BITEXACT>(names::kF32Strict));
        assert(::fixy::same_object(pin1.peek(), pin2.peek()));

        auto const pin3 = hit(reg.by_name_pinned<Tolerance::ULP_FP32>(names::kF32Strict));
        assert(::fixy::same_object(pin3.peek(), pin1.peek()));
        static_assert(!std::is_same_v<decltype(pin1), decltype(pin3)>);

        // The rejecting path is stateless too, so a repeat rejects again
        // rather than returning a wrapper left over from an earlier call.
        assert(miss(reg.by_name_pinned<Tolerance::BITEXACT>(names::kF32Ordered)) == RecipeError::ToleranceMismatch);
        assert(miss(reg.by_name_pinned<Tolerance::BITEXACT>(names::kF32Ordered)) == RecipeError::ToleranceMismatch);
    }

    std::puts("ok");
    return 0;
}
