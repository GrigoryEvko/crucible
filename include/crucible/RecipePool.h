#pragma once

// Interns recipes so that two kernels with the same semantics hold a Ref of
// the same recipe. Downstream identity, deduplication and cache keys then
// compare the identity of the recipes instead of comparing or rehashing
// fields.
//
// A pool is single-threaded and does no locking on the intern path. The
// recipes live in the arena, so no recipe is freed until the arena is. The
// pool owns its slot table on the heap, and a growth frees the old table.

#include <crucible/NumericalRecipe.h>
#include <fixy/Core.h>
#include <fixy/Mutation.h>
#include <fixy/Refined.h>
#include <foundation/Brand.h>
#include <foundation/Platform.h>
#include <foundation/contracts/Decide.h>
#include <foundation/contracts/Post.h>
#include <foundation/contracts/Pre.h>
#include <foundation/effects/Effect.h>
#include <foundation/effects/Row.h>

#include <bit>
#include <cstddef>
#include <cstdint>
#include <type_traits>
#include <utility>

namespace crucible {

class CRUCIBLE_OWNER RecipePool {
public:
    using ArenaBorrow = ::fixy::Ref<::fixy::BumpArena<RecipeArena>, RecipeArena>;
    using Capacity = ::fixy::PowerOfTwo<uint32_t>;
    using Size = ::fixy::Monotonic<uint32_t>;
    using init_required_row = ::foundation::effects::Row<::foundation::effects::Effect::Init>;

    // The table never passes half full, so the initial capacity holds half
    // that many distinct recipes before the first resize.
    //
    // The arena comes as a Ref from ::fixy::to, which gives the brand of the
    // arena, RecipeArena.  The pool keeps that Ref, so each recipe that it
    // makes keeps the brand of its arena.  This constructor only checks and
    // forwards, and the one body below does the work out of line.
    template <typename CallerRow = init_required_row>
        requires ::foundation::effects::Subrow<init_required_row, CallerRow>
    [[gnu::cold]] explicit RecipePool(ArenaBorrow arena, ::foundation::effects::Init init,
                                      uint32_t initial_capacity = 32, std::type_identity<CallerRow> = {}) noexcept
        : RecipePool{body_door_{}, arena, init, admitted_capacity_(initial_capacity)} {}

    RecipePool(const RecipePool&) = delete("RecipePool owns interior pointers into arena_");
    RecipePool& operator=(const RecipePool&) = delete("RecipePool owns interior pointers into arena_");
    RecipePool(RecipePool&&) = delete("interior pointers would dangle");
    RecipePool& operator=(RecipePool&&) = delete("interior pointers would dangle");

    // Two calls return a Ref of the same recipe exactly when their arguments
    // agree on every field but the hash. The hash of the argument is ignored:
    // the pool computes and owns that field.
    //
    // The recipe is in the arena, and not in the pool.
    [[nodiscard]] RecipeRef intern(::foundation::effects::Alloc a, const NumericalRecipe& fields)
        CRUCIBLE_NO_THREAD_SAFETY {
        const RecipeHash h = compute_recipe_hash(fields);
        const uint64_t hv = h.raw();

        const uint32_t cap = capacity_.value();
        for (uint32_t probe = 0; probe < cap; ++probe) {
            const uint64_t key = hv + probe;
            Slot const& s = slots_.at_masked(key);
            if (s.recipe.is_none()) {
                if ((size_.get() + 1) * 2 > cap) [[unlikely]] {
                    grow_(a);
                    // The table this probe walked no longer exists.
                    return intern(a, fields);
                }
                return install_(a, key, fields, h);
            }
            for (const RecipeRef stored : s.recipe) {
                if (s.hash == hv && semantic_equal_(*stored, fields)) {
                    return stored;
                }
            }
        }
        // The table stays under half full, so the loop finds an empty slot
        // or crosses the growth threshold first.
        ::fixy::fatal("a RecipePool probed each slot and found no empty slot: the table is over half full");
    }

    [[nodiscard, gnu::pure]] uint32_t size() const noexcept { return size_.get(); }
    [[nodiscard, gnu::pure]] uint32_t capacity() const noexcept { return capacity_.value(); }

private:
    struct Slot {
        uint64_t hash = 0;  // meaningful only when the slot is occupied
        ::fixy::Option<RecipeRef> recipe = ::fixy::none;  // none marks an empty slot
    };

    // Only the public constructor names this tag, so no caller reaches the
    // body below with a capacity that the precondition did not read.
    struct body_door_ {};

    // The precondition of the public constructor.  It runs before the
    // constructor that it delegates to reads the capacity.  The lower bound
    // is a load-factor sanity floor.  The power-of-two requirement is
    // structural: the probe masks instead of dividing.
    [[nodiscard]] static constexpr uint32_t admitted_capacity_(uint32_t initial_capacity) noexcept {
        CRUCIBLE_PRE(initial_capacity >= 8);
        CRUCIBLE_PRE(::foundation::decide::is_power_of_two_le<std::uint32_t>(initial_capacity, UINT32_MAX));
        return initial_capacity;
    }

    [[gnu::cold, gnu::noinline]] RecipePool(body_door_, ArenaBorrow arena, ::foundation::effects::Init init,
                                            uint32_t initial_capacity) noexcept
        : arena_{arena},
          capacity_{::fixy::mint_refined<::fixy::power_of_two>(initial_capacity)},
          size_{::fixy::mint_monotonic<uint32_t>(0)} {
        slots_ = ::fixy::mint_masked_array<Slot>(init.alloc, initial_capacity);
        // A post clause whose predicate reads a member through `this` is
        // skipped at consteval, so these route through the macro. The leading
        // 0 is the placeholder return value for a function returning void.
        CRUCIBLE_POST(0, capacity_.value() == initial_capacity);
        CRUCIBLE_POST(0, slots_.size() == initial_capacity);
        CRUCIBLE_POST(0, size_.get() == 0);
    }

    [[nodiscard, gnu::pure]] static constexpr bool semantic_equal_(const NumericalRecipe& a,
                                                                   const NumericalRecipe& b) noexcept {
        // The hash field is deliberately not compared. The stored one is
        // derived from the fields below, and the argument's is ignored.
        return a.accum_dtype == b.accum_dtype && a.out_dtype == b.out_dtype && a.reduction_algo == b.reduction_algo
            && a.rounding == b.rounding && a.scale_policy == b.scale_policy && a.softmax == b.softmax
            && a.determinism == b.determinism && a.flags == b.flags;
    }

    // Installs the recipe in the slot of key.
    [[nodiscard]] RecipeRef install_(::foundation::effects::Alloc a, uint64_t key, const NumericalRecipe& fields,
                                     RecipeHash h) {
        const auto recipe = arena_->make<NumericalRecipe>(a, fields);
        recipe->hash = h;

        slots_.with_masked(key, [installed = Slot{.hash = h.raw(), .recipe = ::fixy::some(RecipeRef{recipe})}](
                                    Slot& slot) noexcept { slot = installed; });
        size_.bump();
        return recipe;
    }

    [[gnu::cold, gnu::noinline]]
    void grow_(::foundation::effects::Alloc a) {
        const uint32_t old_size = size_.get();
        const ::fixy::MaskedArray<Slot> old_slots = std::move(slots_);
        const uint32_t new_cap = capacity_.value() * 2;

        slots_ = ::fixy::mint_masked_array<Slot>(a, new_cap);
        capacity_ = ::fixy::mint_refined<::fixy::power_of_two>(new_cap);

        // The element door of each probe puts the slot into the first empty
        // slot of the new table.
        uint32_t reinserted = 0;
        for (const Slot& s : old_slots.view()) {
            if (s.recipe.is_none()) continue;
            for (uint32_t probe = 0; probe < new_cap; ++probe) {
                const bool is_placed = slots_.with_masked(s.hash + probe, [&s](Slot& target) noexcept {
                    if (target.recipe.is_some()) return false;
                    target = s;
                    return true;
                });
                if (is_placed) {
                    ++reinserted;
                    break;
                }
            }
        }
        CRUCIBLE_POST(0, reinserted == old_size);
        size_.advance(reinserted);
        // The old table ends here, and its destructor frees it.
    }

    ArenaBorrow arena_;
    ::fixy::MaskedArray<Slot> slots_{};
    Capacity capacity_;
    Size size_;
};

}  // namespace crucible
