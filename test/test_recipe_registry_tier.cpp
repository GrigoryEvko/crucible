// Copyright (c) Grigory Evko
// Licensed under the Apache License, Version 2.0

#include <crucible/Arena.h>
#include <crucible/effects/_Capabilities.h>
#include <crucible/NumericalRecipe.h>
#include <crucible/RecipePool.h>
#include <crucible/RecipeRegistry.h>
#include <crucible/safety/_IsNumericalTier.h>
#include <crucible/safety/_NumericalTier.h>

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
using crucible::ReductionDeterminism;
using crucible::ScalarType;
using crucible::tolerance_for_dtype;
using crucible::tolerance_of;
using safety_Tolerance = crucible::safety::Tolerance;
using crucible::safety::NumericalTier;

namespace names = crucible::recipe_names;

auto g_test = crucible::effects::testing::test();
auto g_init = crucible::effects::testing::init();
inline crucible::effects::Alloc alloc_cap() noexcept { return g_test.alloc; }
inline crucible::effects::Init init_cap() noexcept { return g_init; }

[[nodiscard]] inline auto entries_view(const RecipeRegistry& reg) noexcept { return reg.entries().value(); }

}  // namespace

[[gnu::cold]] int main() {
    {
        static_assert(tolerance_for_dtype(ScalarType::Double) == safety_Tolerance::ULP_FP64);
        static_assert(tolerance_for_dtype(ScalarType::Float) == safety_Tolerance::ULP_FP32);
        static_assert(tolerance_for_dtype(ScalarType::Half) == safety_Tolerance::ULP_FP16);
        static_assert(tolerance_for_dtype(ScalarType::BFloat16) == safety_Tolerance::ULP_FP16);
        static_assert(tolerance_for_dtype(ScalarType::Float8_e4m3fn) == safety_Tolerance::ULP_FP8);
        static_assert(tolerance_for_dtype(ScalarType::Float8_e5m2) == safety_Tolerance::ULP_FP8);
        static_assert(tolerance_for_dtype(ScalarType::Char) == safety_Tolerance::ULP_INT8);
        static_assert(tolerance_for_dtype(ScalarType::Byte) == safety_Tolerance::ULP_INT8);
        static_assert(tolerance_for_dtype(ScalarType::Int) == safety_Tolerance::RELAXED);
        static_assert(tolerance_for_dtype(ScalarType::Long) == safety_Tolerance::RELAXED);
        static_assert(tolerance_for_dtype(ScalarType::Bool) == safety_Tolerance::RELAXED);
        static_assert(tolerance_for_dtype(ScalarType::Undefined) == safety_Tolerance::RELAXED);
    }

    // An ordered reduction can land up to 4 ULP away, which is coarser
    // than any of the 1-ULP-at-precision classes, so it maps to the
    // weakest class rather than to the one its output dtype suggests.
    {
        NumericalRecipe r_strict{};
        r_strict.determinism = ReductionDeterminism::BITEXACT_STRICT;
        r_strict.out_dtype = ScalarType::Float;
        assert(tolerance_of(r_strict) == safety_Tolerance::BITEXACT);

        NumericalRecipe r_tc_fp16{};
        r_tc_fp16.determinism = ReductionDeterminism::BITEXACT_TC;
        r_tc_fp16.out_dtype = ScalarType::Half;
        assert(tolerance_of(r_tc_fp16) == safety_Tolerance::ULP_FP16);

        NumericalRecipe r_tc_bf16{};
        r_tc_bf16.determinism = ReductionDeterminism::BITEXACT_TC;
        r_tc_bf16.out_dtype = ScalarType::BFloat16;
        assert(tolerance_of(r_tc_bf16) == safety_Tolerance::ULP_FP16);

        NumericalRecipe r_ordered{};
        r_ordered.determinism = ReductionDeterminism::ORDERED;
        r_ordered.out_dtype = ScalarType::Float;
        assert(tolerance_of(r_ordered) == safety_Tolerance::RELAXED);

        NumericalRecipe r_unordered{};
        r_unordered.determinism = ReductionDeterminism::UNORDERED;
        r_unordered.out_dtype = ScalarType::Float;
        assert(tolerance_of(r_unordered) == safety_Tolerance::RELAXED);
    }

    Arena arena{};
    RecipePool pool{RecipePool::ArenaBorrow{arena}, init_cap()};
    RecipeRegistry reg{RecipeRegistry::PoolBorrow{pool}, alloc_cap()};

    {
        auto pinned = reg.by_name_pinned<safety_Tolerance::BITEXACT>(names::kF32Strict);
        assert(pinned.has_value());
        static_assert(std::is_same_v<decltype(pinned)::value_type,
                                     NumericalTier<safety_Tolerance::BITEXACT, const NumericalRecipe*>>);
        assert(pinned->peek() != nullptr);
        assert(pinned->peek()->determinism == ReductionDeterminism::BITEXACT_STRICT);
    }

    // A recipe admits at any pin weaker than its own class.
    {
        auto pinned = reg.by_name_pinned<safety_Tolerance::RELAXED>(names::kF32Strict);
        assert(pinned.has_value());
        static_assert(std::is_same_v<decltype(pinned)::value_type,
                                     NumericalTier<safety_Tolerance::RELAXED, const NumericalRecipe*>>);
    }

    // The reverse direction is the bug this gate exists for: an ordered
    // recipe, several ULP wide, reaching a consumer that was written
    // against bit-exact results.
    {
        auto pinned = reg.by_name_pinned<safety_Tolerance::BITEXACT>(names::kF32Ordered);
        assert(!pinned.has_value());
        assert(pinned.error() == RecipeError::ToleranceMismatch);
    }

    // The name is the primary key and the tolerance is a second gate
    // behind it, so an unknown name reports as an unknown name at every
    // pin strength.  Pinning only adds a gate, it never changes what
    // the unpinned lookup would have reported.
    {
        auto pinned = reg.by_name_pinned<safety_Tolerance::RELAXED>("nonexistent_recipe");
        assert(!pinned.has_value());
        assert(pinned.error() == RecipeError::NameNotFound);

        auto pinned_strict = reg.by_name_pinned<safety_Tolerance::BITEXACT>("does_not_exist");
        assert(!pinned_strict.has_value());
        assert(pinned_strict.error() == RecipeError::NameNotFound);
    }

    {
        auto base = reg.by_name(names::kF32Strict);
        assert(base.has_value());
        const RecipeHash hash = (*base)->hash;

        auto pinned = reg.by_hash_pinned<safety_Tolerance::BITEXACT>(hash);
        assert(pinned.has_value());
        assert(pinned->peek()->hash == hash);
    }

    // This recipe is ordered, so its class is the weakest one, not the
    // one its half-precision storage would suggest.  A half-precision
    // pin is therefore up the lattice and rejects.
    {
        auto base = reg.by_name(names::kF16F32AccumOrdered);
        assert(base.has_value());
        const RecipeHash hash = (*base)->hash;

        auto pinned = reg.by_hash_pinned<safety_Tolerance::ULP_FP16>(hash);
        assert(!pinned.has_value());
        assert(pinned.error() == RecipeError::ToleranceMismatch);
    }

    // A hash no recipe carries reports as a missing hash, never as a
    // tolerance mismatch: there is no recipe to compare a class against.
    {
        const RecipeHash bogus{0xDEADBEEFCAFEBABEULL};
        auto pinned = reg.by_hash_pinned<safety_Tolerance::RELAXED>(bogus);
        assert(!pinned.has_value());
        assert(pinned.error() == RecipeError::HashNotFound);
    }

    {
        for (const auto& entry : entries_view(reg)) {
            // RELAXED is the bottom of the lattice, so every recipe
            // satisfies it.
            auto relaxed_pin = reg.by_name_pinned<safety_Tolerance::RELAXED>(entry.name);
            assert(relaxed_pin.has_value());

            // BITEXACT is the top, so only a strictly bit-exact recipe
            // satisfies it.
            auto bitexact_pin = reg.by_name_pinned<safety_Tolerance::BITEXACT>(entry.name);
            const bool is_strict = entry.recipe->determinism == ReductionDeterminism::BITEXACT_STRICT;
            assert(bitexact_pin.has_value() == is_strict);
        }
    }

    {
        int admits = 0;
        for (const auto& entry : entries_view(reg)) {
            auto pinned = reg.by_name_pinned<safety_Tolerance::RELAXED>(entry.name);
            if (pinned.has_value()) ++admits;
        }
        assert(admits == int{RecipeRegistry::STARTER_COUNT});
    }

    // Exactly one starter recipe is strictly bit-exact, which is where
    // the expected count of one comes from.
    {
        int strict_admits = 0;
        for (const auto& entry : entries_view(reg)) {
            auto pinned = reg.by_name_pinned<safety_Tolerance::BITEXACT>(entry.name);
            if (pinned.has_value()) ++strict_admits;
        }
        assert(strict_admits == 1);
    }

    {
        using PinnedBitexact = NumericalTier<safety_Tolerance::BITEXACT, const NumericalRecipe*>;
        using PinnedRelaxed = NumericalTier<safety_Tolerance::RELAXED, const NumericalRecipe*>;
        using PinnedFp16 = NumericalTier<safety_Tolerance::ULP_FP16, const NumericalRecipe*>;
        static_assert(sizeof(PinnedBitexact) == sizeof(const NumericalRecipe*));
        static_assert(sizeof(PinnedRelaxed) == sizeof(const NumericalRecipe*));
        static_assert(sizeof(PinnedFp16) == sizeof(const NumericalRecipe*));
    }

    {
        auto base = reg.by_name(names::kF32Strict);
        assert(base.has_value());
        const NumericalRecipe* expected_ptr = *base;

        auto pinned = reg.by_name_pinned<safety_Tolerance::BITEXACT>(names::kF32Strict);
        assert(pinned.has_value());
        assert(pinned->peek() == expected_ptr);

        const NumericalRecipe* moved = std::move(*pinned).consume();
        assert(moved == expected_ptr);
    }

    // The runtime rejection above has a compile-time counterpart: a
    // consumer that demands the top class cannot be handed a wrapper
    // pinned at the bottom one.
    {
        using PinnedBitexact = NumericalTier<safety_Tolerance::BITEXACT, const NumericalRecipe*>;
        using PinnedRelaxed = NumericalTier<safety_Tolerance::RELAXED, const NumericalRecipe*>;
        static_assert(!std::is_same_v<PinnedBitexact, PinnedRelaxed>);

        static_assert(PinnedBitexact::satisfies<safety_Tolerance::RELAXED>);
        static_assert(!PinnedRelaxed::satisfies<safety_Tolerance::BITEXACT>);
    }

    // The lattice runs RELAXED, ULP_INT8, ULP_FP8, ULP_FP16, ULP_FP32,
    // ULP_FP64, BITEXACT from bottom to top, which is the order the
    // chain below walks.  Each step weakens the pin by one class and
    // stays admissible at the next consumer.
    {
        using crucible::safety::NumericalTier;

        NumericalTier<safety_Tolerance::BITEXACT, int> bitexact{42};
        auto fp64 = std::move(bitexact).relax<safety_Tolerance::ULP_FP64>();
        auto fp32 = std::move(fp64).relax<safety_Tolerance::ULP_FP32>();
        auto fp16 = std::move(fp32).relax<safety_Tolerance::ULP_FP16>();
        auto fp8 = std::move(fp16).relax<safety_Tolerance::ULP_FP8>();
        auto int8 = std::move(fp8).relax<safety_Tolerance::ULP_INT8>();
        auto relaxed = std::move(int8).relax<safety_Tolerance::RELAXED>();

        static_assert(std::is_same_v<decltype(fp64), NumericalTier<safety_Tolerance::ULP_FP64, int>>);
        static_assert(std::is_same_v<decltype(fp32), NumericalTier<safety_Tolerance::ULP_FP32, int>>);
        static_assert(std::is_same_v<decltype(fp16), NumericalTier<safety_Tolerance::ULP_FP16, int>>);
        static_assert(std::is_same_v<decltype(fp8), NumericalTier<safety_Tolerance::ULP_FP8, int>>);
        static_assert(std::is_same_v<decltype(int8), NumericalTier<safety_Tolerance::ULP_INT8, int>>);
        static_assert(std::is_same_v<decltype(relaxed), NumericalTier<safety_Tolerance::RELAXED, int>>);

        int v = std::move(relaxed).consume();
        assert(v == 42);
    }

    // The reflective traits must report the same tier as the wrapper's
    // own member, through every cv-ref qualification.  Diagnostic
    // printing and dispatcher introspection read the traits, so a drift
    // between the two surfaces silently as a wrong tier in a message.
    {
        using crucible::safety::extract::is_numerical_tier_v;
        using crucible::safety::extract::numerical_tier_v;

        using NT_BX = NumericalTier<safety_Tolerance::BITEXACT, const NumericalRecipe*>;
        using NT_RX = NumericalTier<safety_Tolerance::RELAXED, const NumericalRecipe*>;
        using NT_F16 = NumericalTier<safety_Tolerance::ULP_FP16, const NumericalRecipe*>;

        static_assert(numerical_tier_v<NT_BX> == NT_BX::tier);
        static_assert(numerical_tier_v<NT_RX> == NT_RX::tier);
        static_assert(numerical_tier_v<NT_F16> == NT_F16::tier);
        static_assert(numerical_tier_v<NT_BX&> == NT_BX::tier);
        static_assert(numerical_tier_v<NT_BX const&> == NT_BX::tier);
        static_assert(numerical_tier_v<NT_BX&&> == NT_BX::tier);

        static_assert(!is_numerical_tier_v<int>);
        static_assert(!is_numerical_tier_v<const NumericalRecipe*>);
        static_assert(is_numerical_tier_v<NT_BX>);
    }

    // The two lookup paths are separate code, so they can drift: one
    // could skip the tolerance gate, or hand back a second copy of the
    // recipe.  Resolving one recipe both ways and comparing closes
    // that.
    {
        auto base = reg.by_name(names::kF32Strict);
        assert(base.has_value());
        const RecipeHash hash = (*base)->hash;

        auto by_name_pin = reg.by_name_pinned<safety_Tolerance::BITEXACT>(names::kF32Strict);
        auto by_hash_pin = reg.by_hash_pinned<safety_Tolerance::BITEXACT>(hash);
        assert(by_name_pin.has_value());
        assert(by_hash_pin.has_value());
        assert(by_name_pin->peek() == by_hash_pin->peek());
        assert(by_name_pin->peek() == *base);

        static_assert(std::is_same_v<decltype(by_name_pin)::value_type, decltype(by_hash_pin)::value_type>);
    }

    {
        using T = safety_Tolerance;
        using NT = crucible::safety::NumericalTier<T::BITEXACT, int>;
        (void)NT{};  // an unused local alias warns; one instantiation marks it used

        using BX = NumericalTier<T::BITEXACT, int>;
        static_assert(BX::satisfies<T::BITEXACT>);
        static_assert(BX::satisfies<T::ULP_FP64>);
        static_assert(BX::satisfies<T::ULP_FP32>);
        static_assert(BX::satisfies<T::ULP_FP16>);
        static_assert(BX::satisfies<T::ULP_FP8>);
        static_assert(BX::satisfies<T::ULP_INT8>);
        static_assert(BX::satisfies<T::RELAXED>);

        using F64 = NumericalTier<T::ULP_FP64, int>;
        static_assert(!F64::satisfies<T::BITEXACT>);
        static_assert(F64::satisfies<T::ULP_FP64>);
        static_assert(F64::satisfies<T::ULP_FP32>);
        static_assert(F64::satisfies<T::ULP_FP16>);
        static_assert(F64::satisfies<T::ULP_FP8>);
        static_assert(F64::satisfies<T::ULP_INT8>);
        static_assert(F64::satisfies<T::RELAXED>);

        using F32 = NumericalTier<T::ULP_FP32, int>;
        static_assert(!F32::satisfies<T::BITEXACT>);
        static_assert(!F32::satisfies<T::ULP_FP64>);
        static_assert(F32::satisfies<T::ULP_FP32>);
        static_assert(F32::satisfies<T::ULP_FP16>);
        static_assert(F32::satisfies<T::ULP_FP8>);
        static_assert(F32::satisfies<T::ULP_INT8>);
        static_assert(F32::satisfies<T::RELAXED>);

        using F16 = NumericalTier<T::ULP_FP16, int>;
        static_assert(!F16::satisfies<T::BITEXACT>);
        static_assert(!F16::satisfies<T::ULP_FP64>);
        static_assert(!F16::satisfies<T::ULP_FP32>);
        static_assert(F16::satisfies<T::ULP_FP16>);
        static_assert(F16::satisfies<T::ULP_FP8>);
        static_assert(F16::satisfies<T::ULP_INT8>);
        static_assert(F16::satisfies<T::RELAXED>);

        using F8 = NumericalTier<T::ULP_FP8, int>;
        static_assert(!F8::satisfies<T::BITEXACT>);
        static_assert(!F8::satisfies<T::ULP_FP64>);
        static_assert(!F8::satisfies<T::ULP_FP32>);
        static_assert(!F8::satisfies<T::ULP_FP16>);
        static_assert(F8::satisfies<T::ULP_FP8>);
        static_assert(F8::satisfies<T::ULP_INT8>);
        static_assert(F8::satisfies<T::RELAXED>);

        using I8 = NumericalTier<T::ULP_INT8, int>;
        static_assert(!I8::satisfies<T::BITEXACT>);
        static_assert(!I8::satisfies<T::ULP_FP64>);
        static_assert(!I8::satisfies<T::ULP_FP32>);
        static_assert(!I8::satisfies<T::ULP_FP16>);
        static_assert(!I8::satisfies<T::ULP_FP8>);
        static_assert(I8::satisfies<T::ULP_INT8>);
        static_assert(I8::satisfies<T::RELAXED>);

        using RX = NumericalTier<T::RELAXED, int>;
        static_assert(!RX::satisfies<T::BITEXACT>);
        static_assert(!RX::satisfies<T::ULP_FP64>);
        static_assert(!RX::satisfies<T::ULP_FP32>);
        static_assert(!RX::satisfies<T::ULP_FP16>);
        static_assert(!RX::satisfies<T::ULP_FP8>);
        static_assert(!RX::satisfies<T::ULP_INT8>);
        static_assert(RX::satisfies<T::RELAXED>);
    }

    // The payload the registry pins is a plain pointer, so nothing else
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

        using NT_MO = NumericalTier<safety_Tolerance::BITEXACT, MoveOnlyT>;
        static_assert(!std::is_copy_constructible_v<NT_MO>);
        static_assert(std::is_move_constructible_v<NT_MO>);

        NT_MO src{MoveOnlyT{77}};
        auto relaxed = std::move(src).relax<safety_Tolerance::RELAXED>();
        static_assert(std::is_same_v<decltype(relaxed), NumericalTier<safety_Tolerance::RELAXED, MoveOnlyT>>);
        MoveOnlyT v = std::move(relaxed).consume();
        assert(v.v == 77);
    }

    // The pool interns recipes, so the same name always resolves to the
    // same address.  Two identical lookups that disagree would mean the
    // pinned path carries state of its own.
    {
        auto pin1 = reg.by_name_pinned<safety_Tolerance::BITEXACT>(names::kF32Strict);
        auto pin2 = reg.by_name_pinned<safety_Tolerance::BITEXACT>(names::kF32Strict);
        assert(pin1.has_value() && pin2.has_value());
        assert(pin1->peek() == pin2->peek());

        auto pin3 = reg.by_name_pinned<safety_Tolerance::ULP_FP32>(names::kF32Strict);
        assert(pin3.has_value());
        assert(pin3->peek() == pin1->peek());
        static_assert(!std::is_same_v<decltype(pin1)::value_type, decltype(pin3)::value_type>);

        // The rejecting path is stateless too, so a repeat rejects again
        // rather than returning a wrapper left over from an earlier call.
        auto bad1 = reg.by_name_pinned<safety_Tolerance::BITEXACT>(names::kF32Ordered);
        auto bad2 = reg.by_name_pinned<safety_Tolerance::BITEXACT>(names::kF32Ordered);
        assert(!bad1.has_value() && !bad2.has_value());
        assert(bad1.error() == RecipeError::ToleranceMismatch);
        assert(bad2.error() == RecipeError::ToleranceMismatch);
    }

    std::puts("ok");
    return 0;
}
