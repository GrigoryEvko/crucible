#pragma once

// Two initialize-once handles: a cross-thread once-gate that spins rather
// than parks, and a lazily-constructed value built on top of it.

#include <foundation/Pinned.h>
#include <foundation/Platform.h>
#include <foundation/contracts/Pre.h>

#include <array>
#include <atomic>
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

// std::call_once is rejected here.  The library implementation reaches
// pthread_once, which parks a losing thread in the kernel.  The losers
// below spin instead, because the winner runs a startup registration
// and the wait is short.

class Once : ::foundation::Pinned<Once> {
    alignas(64) std::atomic<std::uint8_t> state_{0};

public:
    constexpr Once() noexcept = default;

    template <typename F>
    void call(F&& f) noexcept(noexcept(std::forward<F>(f)()))
        requires std::is_invocable_v<F>
    {
        std::uint8_t expected = 0;
        if (state_.compare_exchange_strong(expected, 1, std::memory_order_acq_rel, std::memory_order_acquire)) {
            std::forward<F>(f)();
            state_.store(2, std::memory_order_release);
            return;
        }
        while (state_.load(std::memory_order_acquire) != 2) {
            CRUCIBLE_SPIN_PAUSE;
        }
        // This is a template, so a postcondition would pull the
        // contract-failure symbol into every instantiating translation
        // unit and break the link for a static library that does not
        // carry the handler.  The two exits above already establish the
        // completed state.
    }

    [[nodiscard]] bool done() const noexcept { return state_.load(std::memory_order_acquire) == 2; }
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

    ~Lazy() {
        if (once_.done()) {
            storage_ptr_()->~T();
        }
    }

    template <typename F>
    [[nodiscard]] T& get_or_init(F&& f) noexcept
        requires std::is_invocable_r_v<T, F>
    {
        // Exceptions are off for the build, so no path here throws.
        // Both specifiers are unconditional rather than derived from F.
        // A derived one reads false for a lambda whose call operator is
        // not itself declared noexcept, and the warning that infers
        // from the body then flags the mismatch.
        once_.call([&]() noexcept { ::new(static_cast<void*>(storage_.data())) T(std::forward<F>(f)()); });
        return *storage_ptr_();
    }

    [[nodiscard]] T& get() & {
        CRUCIBLE_PRE(initialized());
        return *storage_ptr_();
    }

    [[nodiscard]] const T& get() const& {
        CRUCIBLE_PRE(initialized());
        return *storage_ptr_();
    }

    [[nodiscard]] bool initialized() const noexcept { return once_.done(); }
};

}  // namespace fixy::handle
