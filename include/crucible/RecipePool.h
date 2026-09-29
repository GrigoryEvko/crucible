#pragma once

// Interns recipes so that two kernels with the same semantics hold the same
// pointer. Downstream identity, deduplication and cache keys then compare
// pointers instead of comparing or rehashing fields.
//
// A pool is single-threaded and does no locking on the intern path. It is
// arena-owned, so nothing here is freed until the arena is.

#include <crucible/Arena.h>
#include <crucible/NumericalRecipe.h>
#include <fixy/Borrowed.h>
#include <fixy/Mutation.h>
#include <fixy/Refined.h>
#include <foundation/Brand.h>
#include <foundation/Platform.h>
#include <foundation/contracts/Decide.h>
#include <foundation/contracts/Post.h>
#include <foundation/effects/Effect.h>
#include <foundation/effects/Row.h>

#include <bit>
#include <cstddef>
#include <cstdint>
#include <type_traits>

namespace crucible {

class CRUCIBLE_OWNER RecipePool {
public:
    using ArenaBorrow = ::fixy::BorrowedRef<Arena>;
    using Capacity = ::fixy::PowerOfTwo<uint32_t>;
    using Size = ::fixy::Monotonic<uint32_t>;
    using init_required_row = ::foundation::effects::Row<::foundation::effects::Effect::Init>;

    static_assert(::fixy::IsBorrowedRef<ArenaBorrow>);

    // The table never passes half full, so the initial capacity holds half
    // that many distinct recipes before the first resize.
    //
    // The arena comes through ::fixy::mint_borrowed_ref, and the constructor
    // refuses a borrow that names no brand.  The pool keeps the erased
    // pointer, so the layout does not change.  Each mint site is a new
    // brand, so this constructor only checks and forwards.  The one body
    // below does the work out of line.
    template <typename Brand, typename CallerRow = init_required_row>
        requires ::foundation::brand::IsFreshBrand<Brand> && ::foundation::effects::Subrow<init_required_row, CallerRow>
    [[gnu::cold]] explicit RecipePool(::fixy::BorrowedRef<Arena, Brand> arena, ::foundation::effects::Init init,
                                      uint32_t initial_capacity = 32, std::type_identity<CallerRow> = {}) noexcept
        // The lower bound is a load-factor sanity floor. The power-of-two
        // requirement is structural: the probe below masks instead of
        // dividing.
        pre(initial_capacity >= 8)
            pre(::foundation::decide::is_power_of_two_le<std::uint32_t>(initial_capacity, UINT32_MAX))
        : RecipePool{erased_door_{}, ArenaBorrow{arena}, init, initial_capacity} {}

    RecipePool(const RecipePool&) = delete("RecipePool owns interior pointers into arena_");
    RecipePool& operator=(const RecipePool&) = delete("RecipePool owns interior pointers into arena_");
    RecipePool(RecipePool&&) = delete("interior pointers would dangle");
    RecipePool& operator=(RecipePool&&) = delete("interior pointers would dangle");

    // Two calls return the same pointer exactly when their arguments agree
    // on every field but the hash. The hash of the argument is ignored: the
    // pool computes and owns that field.
    [[nodiscard, gnu::returns_nonnull]] const NumericalRecipe* intern(::foundation::effects::Alloc a,
                                                                      const NumericalRecipe& fields)
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

    // Only the public constructor names this tag, so no caller reaches the
    // erased borrow below.
    struct erased_door_ {};

    [[gnu::cold, gnu::noinline]] RecipePool(erased_door_, ArenaBorrow arena, ::foundation::effects::Init init,
                                            uint32_t initial_capacity) noexcept
        : arena_{arena},
          capacity_{::fixy::mint_refined<::fixy::power_of_two>(initial_capacity)},
          size_{::fixy::mint_monotonic<uint32_t>(0)} {
        const ::foundation::effects::Alloc a = init.alloc;
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

    [[nodiscard, gnu::pure]] static constexpr bool semantic_equal_(const NumericalRecipe& a,
                                                                   const NumericalRecipe& b) noexcept {
        // The hash field is deliberately not compared. The stored one is
        // derived from the fields below, and the argument's is ignored.
        return a.accum_dtype == b.accum_dtype && a.out_dtype == b.out_dtype && a.reduction_algo == b.reduction_algo
            && a.rounding == b.rounding && a.scale_policy == b.scale_policy && a.softmax == b.softmax
            && a.determinism == b.determinism && a.flags == b.flags;
    }

    [[nodiscard, gnu::returns_nonnull]] const NumericalRecipe* install_(::foundation::effects::Alloc a, uint32_t i,
                                                                        const NumericalRecipe& fields, RecipeHash h) {
        NumericalRecipe* r = arena_->alloc_obj<NumericalRecipe>(a);
        *r = fields;
        r->hash = h;

        slots_[i] = Slot{.hash = h.raw(), .recipe = r};
        size_.bump();
        return r;
    }

    [[gnu::cold, gnu::noinline]]
    void grow_(::foundation::effects::Alloc a) {
        const uint32_t old_cap = capacity_.value();
        const uint32_t old_size = size_.get();
        Slot* old_slots = slots_;
        const uint32_t new_cap = old_cap * 2;

        slots_ = arena_->alloc_array_nonzero<Slot>(a, new_cap);
        for (uint32_t i = 0; i < new_cap; ++i) {
            slots_[i] = Slot{};
        }
        capacity_ = ::fixy::mint_refined<::fixy::power_of_two>(new_cap);

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
    Slot* slots_ = nullptr;
    Capacity capacity_;
    Size size_;
};

}  // namespace crucible
