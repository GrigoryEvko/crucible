#pragma once

// Interns recipes so that two kernels with the same semantics hold the same
// pointer. Downstream identity, deduplication and cache keys then compare
// pointers instead of comparing or rehashing fields.
//
// A pool is single-threaded and does no locking on the intern path. It is
// arena-owned, so nothing here is freed until the arena is.

#include <crucible/Arena.h>
#include <crucible/effects/_Capabilities.h>
#include <crucible/effects/_EffectRow.h>
#include <crucible/NumericalRecipe.h>
#include <crucible/Platform.h>
#include <crucible/fixy/Wrap.h>
#include <crucible/safety/Decide.h>
#include <crucible/safety/Post.h>
#include <crucible/safety/Pre.h>

#include <bit>
#include <cstddef>
#include <cstdint>
#include <type_traits>

namespace crucible {

class CRUCIBLE_OWNER RecipePool {
public:
    using ArenaBorrow = fixy::wrap::BorrowedRef<Arena>;
    using Capacity = fixy::wrap::PowerOfTwo<uint32_t>;
    using Size = fixy::wrap::Monotonic<uint32_t>;
    using init_required_row = effects::Row<effects::Effect::Init>;

    static_assert(fixy::wrap::IsBorrowedRef<ArenaBorrow>);

    // The table never passes half full, so the initial capacity holds half
    // that many distinct recipes before the first resize.
    template <typename CallerRow = init_required_row>
        requires effects::Subrow<init_required_row, CallerRow>
    [[gnu::cold]] explicit RecipePool(ArenaBorrow arena, effects::Init init, uint32_t initial_capacity = 32,
                                      std::type_identity<CallerRow> = {}) noexcept
        // The lower bound is a load-factor sanity floor. The power-of-two
        // requirement is structural: the probe below masks instead of
        // dividing.
        pre(initial_capacity >= 8)
            pre(::crucible::decide::is_power_of_two_le<std::uint32_t>(initial_capacity, UINT32_MAX))
        : arena_{arena}, capacity_{initial_capacity}, size_{0} {
        const effects::Alloc a = init.alloc;
        slots_ = arena_->alloc_array_nonzero<Slot>(a, initial_capacity);
        for (uint32_t i = 0; i < initial_capacity; ++i) {
            slots_[i] = Slot{};
        }
        // A post clause whose predicate reads a member through `this` is
        // skipped at consteval, so these route through the macro. The leading
        // 0 is the placeholder return value for a function returning void.
        CRUCIBLE_POST(0, capacity_.value() == initial_capacity);
        CRUCIBLE_POST(0, slots_ != nullptr);
        CRUCIBLE_POST(0, size_.get() == 0);
    }

    RecipePool(const RecipePool&) = delete("RecipePool owns interior pointers into arena_");
    RecipePool& operator=(const RecipePool&) = delete("RecipePool owns interior pointers into arena_");
    RecipePool(RecipePool&&) = delete("interior pointers would dangle");
    RecipePool& operator=(RecipePool&&) = delete("interior pointers would dangle");

    // Two calls return the same pointer exactly when their arguments agree
    // on every field but the hash. The hash of the argument is ignored: the
    // pool computes and owns that field.
    [[nodiscard, gnu::returns_nonnull]] const NumericalRecipe* intern(effects::Alloc a, const NumericalRecipe& fields)
        CRUCIBLE_LIFETIMEBOUND CRUCIBLE_NO_THREAD_SAFETY {
        const RecipeHash h = compute_recipe_hash(fields);
        const uint64_t hv = h.raw();

        const uint32_t cap = capacity_.value();
        const uint32_t mask = cap - 1;
        uint32_t idx = static_cast<uint32_t>(hv) & mask;
        for (uint32_t probe = 0; probe < cap; ++probe) {
            const uint32_t i = (idx + probe) & mask;
            Slot& s = slots_[i];
            if (s.recipe == nullptr) {
                if ((size_.get() + 1) * 2 > cap) [[unlikely]] {
                    grow_(a);
                    // The table this probe walked no longer exists.
                    return intern(a, fields);
                }
                return install_(a, i, fields, h);
            }
            if (s.hash == hv && semantic_equal_(*s.recipe, fields)) {
                return s.recipe;
            }
        }
        // Unreachable while the table stays under half full: the loop either
        // finds an empty slot or crosses the growth threshold first.
        std::abort();
    }

    [[nodiscard, gnu::pure]] uint32_t size() const noexcept { return size_.get(); }
    [[nodiscard, gnu::pure]] uint32_t capacity() const noexcept { return capacity_.value(); }

private:
    struct Slot {
        uint64_t hash = 0;  // meaningful only when the slot is occupied
        const NumericalRecipe* recipe = nullptr;  // null marks an empty slot
    };

    [[nodiscard, gnu::pure]] static constexpr bool semantic_equal_(const NumericalRecipe& a,
                                                                   const NumericalRecipe& b) noexcept {
        // The hash field is deliberately not compared. The stored one is
        // derived from the fields below, and the argument's is ignored.
        return a.accum_dtype == b.accum_dtype && a.out_dtype == b.out_dtype && a.reduction_algo == b.reduction_algo
            && a.rounding == b.rounding && a.scale_policy == b.scale_policy && a.softmax == b.softmax
            && a.determinism == b.determinism && a.flags == b.flags;
    }

    [[nodiscard, gnu::returns_nonnull]] const NumericalRecipe* install_(effects::Alloc a, uint32_t i,
                                                                        const NumericalRecipe& fields, RecipeHash h) {
        NumericalRecipe* r = arena_->alloc_obj<NumericalRecipe>(a);
        *r = fields;
        r->hash = h;

        slots_[i] = Slot{.hash = h.raw(), .recipe = r};
        size_.bump();
        return r;
    }

    [[gnu::cold, gnu::noinline]]
    void grow_(effects::Alloc a) {
        const uint32_t old_cap = capacity_.value();
        const uint32_t old_size = size_.get();
        Slot* old_slots = slots_;
        const uint32_t new_cap = old_cap * 2;

        slots_ = arena_->alloc_array_nonzero<Slot>(a, new_cap);
        for (uint32_t i = 0; i < new_cap; ++i) {
            slots_[i] = Slot{};
        }
        capacity_ = Capacity{new_cap};

        const uint32_t new_mask = new_cap - 1;
        uint32_t reinserted = 0;
        for (uint32_t i = 0; i < old_cap; ++i) {
            Slot& s = old_slots[i];
            if (s.recipe == nullptr) continue;
            uint32_t idx = static_cast<uint32_t>(s.hash) & new_mask;
            for (uint32_t probe = 0; probe < new_cap; ++probe) {
                const uint32_t j = (idx + probe) & new_mask;
                if (slots_[j].recipe == nullptr) {
                    slots_[j] = s;
                    ++reinserted;
                    break;
                }
            }
        }
        CRUCIBLE_POST(0, reinserted == old_size);
        size_.advance(reinserted);
        // The old slot array stays in the arena until the arena dies. Each
        // growth doubles, so a pool leaks at most its own size that way.
    }

    ArenaBorrow arena_;
    Slot* slots_;
    Capacity capacity_;
    Size size_;
};

}  // namespace crucible
