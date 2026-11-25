#pragma once

// Two initialize-once handles: a cross-thread once-gate that spins rather
// than parks, and a lazily-constructed value built on top of it.

#include <foundation/Brand.h>
#include <foundation/NoObject.h>
#include <foundation/Pinned.h>
#include <foundation/Platform.h>
#include <foundation/core/Atomic.h>
#include <foundation/core/Choice.h>
#include <foundation/core/Ref.h>
#include <foundation/core/Report.h>
#include <foundation/effects/Ctx.h>
#include <foundation/effects/Effect.h>

#include <array>
#include <cstdint>
#include <new>
#include <type_traits>
#include <utility>

// The claim the carrier in this header makes that no lattice grades.
// foundation/diag/RowHash.h folds the identity, so the carrier takes a
// cache slot of its own rather than the zero a bare payload has.
namespace fixy::row_discipline {
struct lazy;
}  // namespace fixy::row_discipline

namespace fixy::handle {
template <typename T>
class Lazy;
}  // namespace fixy::handle

namespace foundation::core {

// The door of a Lazy to the door of a Ref (the doors of the owners above
// foundation/core/Ref.h).  Its one member is private, and Lazy is its one
// friend.  The invariant: a Lazy lends its value only after the gate reads
// the end of the build, the value lives as long as the pinned Lazy, and the
// brand is the brand of the value or the fresh brand of the call.  A home
// owner row of utils/rule-table.txt keeps this definition in this header.
class LazyRefDoor final : ::foundation::NoObject<LazyRefDoor> {
    template <typename T>
    friend class ::fixy::handle::Lazy;

    // The value of a built Lazy, as a Ref of the brand.
    template <::foundation::brand::IsBrand Brand, RefTarget T>
    [[nodiscard]] CRUCIBLE_INLINE static constexpr Ref<T, Brand> lend_(T& object) noexcept {
        return detail::RefDoor::lend<Brand>(object);
    }
};

}  // namespace foundation::core

namespace fixy::handle {

// std::call_once is rejected here.  The library implementation reaches
// pthread_once, which parks a losing thread in the kernel.  The losers
// below spin instead, because the winner runs a startup registration
// and the wait is short.
//
// A loser spins until the winner ends, with no exit: a winner that never
// ends its body leaves each loser in the spin.  A spin with no exit carries
// Div (foundation/effects/Effect.h), so call and get_or_init take the context
// of the caller, and the context owns Div.  The context of the hot path owns
// no atom.

//
// The states of a gate: 0 before the body, 1 while the body runs, 2 after it, and
// 3 in the end of the Lazy that holds the gate.

namespace detail {

// The fatal exit of a call of a gate whose Lazy ends its value.
[[noreturn]] CRUCIBLE_COLD inline void report_call_of_an_ended_lazy_() noexcept {
    ::foundation::core::fatal("get_or_init of a Lazy runs in the code of its value, while the Lazy ends that value");
}

}  // namespace detail

class Once : ::foundation::Pinned<Once> {
    alignas(64)::foundation::core::Atomic<std::uint8_t> state_{std::uint8_t{0}};

    template <typename T>
    friend class Lazy;

    // Closes the gate for the end of its Lazy: done() is then false, and a call of
    // the Lazy ends the process.  No other thread uses the gate at the end of its
    // Lazy.
    void close_() noexcept { state_.store_release(std::uint8_t{3}); }

    // The body of call.  Only the end of a Lazy closes a gate, so only the call of
    // a Lazy tests the closed state.
    template <bool OfALazy, typename F>
    void run_once_(F&& f) noexcept(noexcept(std::forward<F>(f)())) {
        if (state_.cas_acq_rel(std::uint8_t{0}, std::uint8_t{1}).is_ok()) {
            std::forward<F>(f)();
            state_.store_release(std::uint8_t{2});
            return;
        }
        for (std::uint8_t seen = state_.load_acquire(); seen != 2; seen = state_.load_acquire()) {
            if constexpr (OfALazy) {
                if (seen == 3) [[unlikely]] {
                    detail::report_call_of_an_ended_lazy_();
                }
            }
            CRUCIBLE_SPIN_PAUSE;
        }
        // This is a template, so a postcondition would pull the
        // contract-failure symbol into every instantiating translation
        // unit and break the link for a static library that does not
        // carry the handler.  The two exits above already establish the
        // completed state.
    }

public:
    constexpr Once() noexcept = default;

    template <typename Ctx, typename F>
        requires ::foundation::effects::CtxOwnsCapability<Ctx, ::foundation::effects::Effect::Div>
              && std::is_invocable_v<F>
    void call(Ctx const& /*ctx*/, F&& f) noexcept(noexcept(std::forward<F>(f)())) {
        run_once_<false>(std::forward<F>(f));
    }

    [[nodiscard]] bool done() const noexcept { return state_.load_acquire() == 2; }
};

template <typename T>
class Lazy : ::foundation::Pinned<Lazy<T>> {
    alignas(T) std::array<unsigned char, sizeof(T)> storage_{};
    Once once_{};

    // These bytes hold a T whose lifetime begins at the placement new
    // inside get_or_init.  The character array inside storage_ provides
    // the storage, so every access goes through storage_.data(), never
    // through the address of storage_ itself.  launder re-establishes
    // the new object, which the optimizer cannot see: under strict
    // aliasing it is otherwise free to assume the character array is
    // still the live object.
    //
    // start_lifetime_as is the wrong tool here.  It would create a
    // fresh T at the address, ending the placement-new object's
    // lifetime and leaving the destructor below to run against a
    // different object.  It suits an implicit-lifetime T that is never
    // placement-new'd, and T here is arbitrary.
    [[nodiscard]] T* storage_ptr_() noexcept {
        return std::launder(static_cast<T*>(static_cast<void*>(storage_.data())));
    }
    [[nodiscard]] const T* storage_ptr_() const noexcept {
        return std::launder(static_cast<const T*>(static_cast<const void*>(storage_.data())));
    }

public:
    using row_discipline = ::fixy::row_discipline::lazy;
    using row_payload = T;

    constexpr Lazy() noexcept = default;

    // The gate closes before the value ends, so the code of the value that reaches
    // its Lazy through a second name finds no value, and a get_or_init by it ends
    // the process (the code of an element of foundation/core/Owner.h).
    ~Lazy() {
        if (once_.done()) {
            if constexpr (!std::is_trivially_destructible_v<T>) {
                once_.close_();
            }
            storage_ptr_()->~T();
        }
    }

    // Builds the value one time, and gives it as a Ref with the brand of T,
    // or the fresh brand of the call for a T that states no brand.  The call
    // returns after the build, so the read of the value needs no test.  The
    // pack refuses each explicit argument, so no caller names the brand.
    template <class... Refused, typename Ctx, typename F, ::foundation::brand::IsBrand Fresh = CRUCIBLE_FRESH_BRAND>
        requires(sizeof...(Refused) == 0)
             && ::foundation::effects::CtxOwnsCapability<Ctx, ::foundation::effects::Effect::Div>
             && std::is_invocable_r_v<T, F>
    [[nodiscard]] ::foundation::core::Ref<T, ::foundation::core::carrier_brand_t<T, Fresh>>
    get_or_init(Ctx const& /*ctx*/, F&& f) noexcept {
        // Exceptions are off for the build, so no path here throws.
        // Both specifiers are unconditional rather than derived from F.
        // A derived one reads false for a lambda whose call operator is
        // not itself declared noexcept, and the warning that infers
        // from the body then flags the mismatch.
        once_.template run_once_<true>(
            [&]() noexcept { ::new(static_cast<void*>(storage_.data())) T(std::forward<F>(f)()); });
        return ::foundation::core::LazyRefDoor::lend_<::foundation::core::carrier_brand_t<T, Fresh>>(*storage_ptr_());
    }

    // The value as a Ref, or none before the end of the build.  Another
    // thread can run the build at the same time, so a value can be absent.
    // One acquire load of the gate.
    template <class... Refused, ::foundation::brand::IsBrand Fresh = CRUCIBLE_FRESH_BRAND>
        requires(sizeof...(Refused) == 0)
    [[nodiscard]] ::foundation::core::Option<::foundation::core::Ref<T, ::foundation::core::carrier_brand_t<T, Fresh>>>
    get() & noexcept {
        using Brand = ::foundation::core::carrier_brand_t<T, Fresh>;
        using Held = ::foundation::core::Ref<T, Brand>;
        if (!once_.done()) return ::foundation::core::Option<Held>{::foundation::core::none};
        return ::foundation::core::Option<Held>::some(::foundation::core::LazyRefDoor::lend_<Brand>(*storage_ptr_()));
    }

    template <class... Refused, ::foundation::brand::IsBrand Fresh = CRUCIBLE_FRESH_BRAND>
        requires(sizeof...(Refused) == 0)
    [[nodiscard]] ::foundation::core::Option<
        ::foundation::core::Ref<T const, ::foundation::core::carrier_brand_t<T, Fresh>>>
    get() const& noexcept {
        using Brand = ::foundation::core::carrier_brand_t<T, Fresh>;
        using Held = ::foundation::core::Ref<T const, Brand>;
        if (!once_.done()) return ::foundation::core::Option<Held>{::foundation::core::none};
        return ::foundation::core::Option<Held>::some(::foundation::core::LazyRefDoor::lend_<Brand>(*storage_ptr_()));
    }

    [[nodiscard]] bool initialized() const noexcept { return once_.done(); }
};

}  // namespace fixy::handle
