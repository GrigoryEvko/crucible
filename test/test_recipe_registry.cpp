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

#include <crucible/NumericalRecipe.h>
#include <crucible/RecipePool.h>
#include <crucible/RecipeRegistry.h>
#include <fixy/Borrowed.h>
#include <fixy/Core.h>
#include <fixy/Tagged.h>
#include <fixy/Tags.h>
#include <foundation/effects/Effect.h>
#include <foundation/effects/Row.h>

#include "test_assert.h"
#include <cinttypes>
#include <cstdio>
#include <span>
#include <string_view>
#include <type_traits>
#include <unordered_set>
#include <utility>

namespace {

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
using fixy::BumpArena;

namespace names = crucible::recipe_names;

namespace eff = ::foundation::effects;

auto g_test = eff::testing::test();
auto g_init = eff::testing::init();
inline eff::Alloc alloc_cap() noexcept { return g_test.alloc; }
inline eff::Init init_cap() noexcept { return g_init; }

// The borrow that the mint door gives.
using MintedPoolBorrow = decltype(::fixy::mint_borrowed_ref(std::declval<RecipePool&>()));

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
        static_assert(std::is_same_v<RecipeRegistry::PoolBorrow, ::fixy::BorrowedRef<RecipePool>>);
        static_assert(sizeof(RecipeRegistry::PoolBorrow) == sizeof(RecipePool*));
        static_assert(std::is_constructible_v<RecipeRegistry, MintedPoolBorrow, eff::Alloc>,
                      "RecipeRegistry takes the borrow that mint_borrowed_ref gives");
        static_assert(!std::is_constructible_v<RecipeRegistry, RecipeRegistry::PoolBorrow, eff::Alloc>,
                      "RecipeRegistry refuses a borrow that names no brand");
        using EntryView = ::fixy::View<RecipeRegistry::Entry const, RecipeRegistry::STARTER_COUNT>;
        static_assert(
            std::is_same_v<RecipeRegistry::Entries, ::fixy::Tagged<EntryView, ::fixy::tags::source::JsonRegistry>>);
        static_assert(sizeof(RecipeRegistry::Entries) == sizeof(EntryView));
        static_assert(!std::is_convertible_v<EntryView, RecipeRegistry::Entries>);
        static_assert(eff::Subrow<RecipeRegistry::pure_projection_row, eff::Row<>>);
        static_assert(!eff::Subrow<eff::Row<eff::Effect::IO>, RecipeRegistry::pure_projection_row>);
        static_assert(!eff::Subrow<eff::Row<eff::Effect::Init>, RecipeRegistry::pure_projection_row>);

        static_assert(crucible::detail_recipe_registry::kStarterRecipes.size() == RecipeRegistry::STARTER_COUNT);
    }

    {
        BumpArena arena{};
        RecipePool pool{::fixy::mint_borrowed_ref(arena), init_cap()};
        RecipeRegistry reg{::fixy::mint_borrowed_ref(pool), alloc_cap()};

        assert(entries_view(reg).size() == RecipeRegistry::STARTER_COUNT);
        assert(pool.size() == RecipeRegistry::STARTER_COUNT);

        for (const auto& e : entries_view(reg)) {
            assert(e.name.size() != 0);
            // The stored hash must be the one its own fields produce, not a
            // value copied in from the specification.
            assert(e.recipe->hash == crucible::compute_recipe_hash(*e.recipe));
            assert(e.recipe->accum_dtype == ScalarType::Float);
            // FP16 and BF16 tensor-core fragments are not bit-identical
            // across vendors, so no such recipe claims a bit-exact tier.
            bool const is_half_output =
                e.recipe->out_dtype == ScalarType::Half || e.recipe->out_dtype == ScalarType::BFloat16;
            assert(!is_half_output || !crucible::permits_tensor_cores(e.recipe->determinism)
                   || !crucible::is_bitexact(e.recipe->determinism));
        }
    }

    {
        BumpArena arena{};
        RecipePool pool{::fixy::mint_borrowed_ref(arena), init_cap()};
        RecipeRegistry reg{::fixy::mint_borrowed_ref(pool), alloc_cap()};

        std::unordered_set<const NumericalRecipe*> entry_ptrs;
        for (const auto& e : entries_view(reg)) {
            entry_ptrs.insert(&*e.recipe);
        }
        assert(entry_ptrs.size() == RecipeRegistry::STARTER_COUNT);

        const ::fixy::TextView known_names[] = {
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
            RecipeRegistry::RecipeRef const r = reg.by_name(n).expect("a starter recipe lookup hits");
            // Lookup by name must land on a pointer the pool already owns,
            // not on a fresh copy of the same recipe.
            assert(entry_ptrs.contains(&*r));
        }

        RecipeRegistry::RecipeRef const first = reg.by_name(names::kF16F32AccumTc).expect("the starter is present");
        RecipeRegistry::RecipeRef const second = reg.by_name(names::kF16F32AccumTc).expect("the starter is present");
        assert(::fixy::same_object(first, second));
    }

    // Lookup by name is exact and case-sensitive, so the near-misses
    // below have to miss just as firmly as the nonsense ones.
    {
        BumpArena arena{};
        RecipePool pool{::fixy::mint_borrowed_ref(arena), init_cap()};
        RecipeRegistry reg{::fixy::mint_borrowed_ref(pool), alloc_cap()};

        const ::fixy::TextView missing[] = {
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
            assert(r.is_err() && "missing recipe must not hit");
            assert(std::move(r).err().expect("the lookup missed") == RecipeError::NameNotFound);
        }
    }

    // A name is a contract.  Swapping the dtype or the determinism tier
    // behind an existing name changes what every consumer that pinned it
    // receives, without any of them changing a line.
    {
        BumpArena arena{};
        RecipePool pool{::fixy::mint_borrowed_ref(arena), init_cap()};
        RecipeRegistry reg{::fixy::mint_borrowed_ref(pool), alloc_cap()};

        {
            auto r = reg.by_name(names::kF32Strict);
            RecipeRegistry::RecipeRef const recipe = std::move(r).expect("the starter is present");
            const auto& x = *recipe;
            assert(x.accum_dtype == ScalarType::Float);
            assert(x.out_dtype == ScalarType::Float);
            assert(x.determinism == ReductionDeterminism::BITEXACT_STRICT);
            assert(x.scale_policy == ScalePolicy::NONE);
            assert(x.softmax == SoftmaxRecurrence::ONLINE_LSE);
            assert(crucible::is_bitexact(x.determinism));
            // The strictest determinism tier rules tensor cores out.
            assert(!crucible::permits_tensor_cores(x.determinism));
        }

        // FP16 and BF16 tensor-core fragments give different bits on
        // different vendors, so the two tensor-core recipes are UNORDERED.
        {
            auto r = reg.by_name(names::kF16F32AccumTc);
            RecipeRegistry::RecipeRef const recipe = std::move(r).expect("the starter is present");
            const auto& x = *recipe;
            assert(x.accum_dtype == ScalarType::Float);
            assert(x.out_dtype == ScalarType::Half);
            assert(x.determinism == ReductionDeterminism::UNORDERED);
            assert(!crucible::is_bitexact(x.determinism));
            assert(crucible::permits_tensor_cores(x.determinism));
        }

        {
            auto r = reg.by_name(names::kBf16F32AccumTc);
            RecipeRegistry::RecipeRef const recipe = std::move(r).expect("the starter is present");
            const auto& x = *recipe;
            assert(x.accum_dtype == ScalarType::Float);
            assert(x.out_dtype == ScalarType::BFloat16);
            assert(x.determinism == ReductionDeterminism::UNORDERED);
            assert(!crucible::is_bitexact(x.determinism));
        }

        {
            auto r = reg.by_name(names::kFp8E4m3F32AccumMxOrd);
            RecipeRegistry::RecipeRef const recipe = std::move(r).expect("the starter is present");
            const auto& x = *recipe;
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
            RecipeRegistry::RecipeRef const recipe = std::move(r).expect("the starter is present");
            const auto& x = *recipe;
            assert(x.out_dtype == ScalarType::Float8_e5m2);
            assert(x.scale_policy == ScalePolicy::PER_BLOCK_MX);
            assert(x.determinism == ReductionDeterminism::ORDERED);
        }
    }

    // Lookup by name is a linear scan, so a duplicate name would not be
    // an error: the first match would win and the second recipe would
    // become unreachable without anything reporting it.
    {
        BumpArena arena{};
        RecipePool pool{::fixy::mint_borrowed_ref(arena), init_cap()};
        RecipeRegistry reg{::fixy::mint_borrowed_ref(pool), alloc_cap()};

        // Each name matches exactly one entry: its own.  O(STARTER_COUNT^2).
        for (const auto& e : entries_view(reg)) {
            std::size_t matches = 0;
            for (const auto& other : entries_view(reg)) {
                matches += other.name == e.name ? 1u : 0u;
            }
            assert(matches == 1 && "starter recipe name collision");
        }
    }

    // Interning is by content, so two starter specifications that ended
    // up with identical fields would collapse into one pooled recipe and
    // the pool would simply hold fewer entries than there are names.
    {
        BumpArena arena{};
        RecipePool pool{::fixy::mint_borrowed_ref(arena), init_cap()};
        RecipeRegistry reg{::fixy::mint_borrowed_ref(pool), alloc_cap()};

        std::unordered_set<const NumericalRecipe*> ptrs;
        std::unordered_set<uint64_t> hashes;

        for (const auto& e : entries_view(reg)) {
            const auto [it_p, inserted_p] = ptrs.insert(&*e.recipe);
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
        BumpArena arena{};
        RecipePool pool{::fixy::mint_borrowed_ref(arena), init_cap()};
        RecipeRegistry reg{::fixy::mint_borrowed_ref(pool), alloc_cap()};

        struct Golden {
            ::fixy::TextView name{};
            uint64_t expected = 0;
        };
        const Golden goldens[] = {
            {names::kF32Strict, 0xce0eb0cd5c376b79ULL},
            {names::kF32Ordered, 0x493a001a4ad235a7ULL},
            {names::kF16F32AccumTc, 0x8f46706bfc750adbULL},
            {names::kF16F32AccumOrdered, 0xcc51530cf248c3e3ULL},
            {names::kBf16F32AccumTc, 0x96517ac8a65db41fULL},
            {names::kBf16F32AccumOrdered, 0xc674007dc8716618ULL},
            {names::kFp8E4m3F32AccumMxOrd, 0x5ba4c6b1bdefc89dULL},
            {names::kFp8E5m2F32AccumMxOrd, 0xb8e80f767e9cfb21ULL},
        };

        bool any_drift = false;
        for (auto g : goldens) {
            const uint64_t actual = reg.by_name(g.name).expect("each golden names a starter")->hash.raw();
            if (actual != g.expected) {
                ::fixy::report(::fixy::Sink::Err, "recipe-hash golden DRIFT for {}: expected {}, got {}\n", g.name,
                               g.expected, actual);
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
        BumpArena arena{};
        RecipePool pool{::fixy::mint_borrowed_ref(arena), init_cap()};
        RecipeRegistry reg{::fixy::mint_borrowed_ref(pool), alloc_cap()};

        for (const auto& spec : crucible::detail_recipe_registry::kStarterRecipes) {
            // Re-intern the starter spec's fields → must hit the canonical
            // pointer already stored by the registry.
            const auto* via_pool = pool.intern(alloc_cap(), spec.fields);
            RecipeRegistry::RecipeRef const via_reg = reg.by_name(spec.name).expect("each starter is present");
            assert(via_pool == &*via_reg);
        }
        // An unchanged pool size is what proves every intern was a hit.
        assert(pool.size() == RecipeRegistry::STARTER_COUNT);
    }

    // Lookup by hash is how a persisted entry finds its recipe again, so
    // it has to reach the same pointer as lookup by name.
    {
        BumpArena arena{};
        RecipePool pool{::fixy::mint_borrowed_ref(arena), init_cap()};
        RecipeRegistry reg{::fixy::mint_borrowed_ref(pool), alloc_cap()};

        for (const auto& entry : entries_view(reg)) {
            RecipeRegistry::RecipeRef const via_hash = reg.by_hash(entry.recipe->hash).expect("each hash resolves");
            assert(::fixy::same_object(via_hash, entry.recipe));

            RecipeRegistry::RecipeRef const via_name = reg.by_name(entry.name).expect("each name resolves");
            assert(::fixy::same_object(via_hash, via_name));
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
            assert(reg.by_hash(h).err().expect("a bogus hash misses") == RecipeError::HashNotFound);
        }

        // The sentinel value marks the end of a region, so a real recipe
        // resolving to it would make a region terminate early.
        assert(reg.by_hash(crucible::RecipeHash::sentinel()).err().expect("the sentinel misses")
               == RecipeError::HashNotFound);
    }

    // Identity is per pool and equality is by content.  Two registries
    // over separate pools therefore hand out different pointers for the
    // same recipe, and those recipes still hash alike.
    {
        BumpArena arena_a{};
        BumpArena arena_b{};
        RecipePool pool_a{::fixy::mint_borrowed_ref(arena_a), init_cap()};
        RecipePool pool_b{::fixy::mint_borrowed_ref(arena_b), init_cap()};
        RecipeRegistry reg_a{::fixy::mint_borrowed_ref(pool_a), alloc_cap()};
        RecipeRegistry reg_b{::fixy::mint_borrowed_ref(pool_b), alloc_cap()};

        RecipeRegistry::RecipeRef const a = reg_a.by_name(names::kF16F32AccumTc).expect("the starter is present");
        RecipeRegistry::RecipeRef const b = reg_b.by_name(names::kF16F32AccumTc).expect("the starter is present");
        assert(!::fixy::same_object(a, b));
        assert(a->hash == b->hash);
        assert(a->out_dtype == b->out_dtype);
    }

    crucible::test::pass("test_recipe_registry: all tests passed\n");
    return 0;
}
