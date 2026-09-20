#pragma once

// Three initialize-once handles: a non-atomic pointer slot that refuses
// a second set, a cross-thread once-gate that spins rather than parks,
// and a lazily-constructed value built on top of it.
//
// Old spelling: include/crucible/handles/Once.h.

#include <foundation/Pinned.h>
#include <foundation/Platform.h>
#include <foundation/contracts/Post.h>
#include <foundation/contracts/Pre.h>

#include <atomic>
#include <cstdint>
#include <cstdlib>
#include <new>
#include <type_traits>
#include <utility>

namespace fixy::handle {

// A null pointer is the unset state.  A slot that carries its own tag
// byte could tell a published null from an unset slot, and this one
// pays one pointer instead and cannot.  set therefore rejects null
// rather than store a value it could never report back.

template <typename T>
class [[nodiscard]] SetOnce {
    T* ptr_ = nullptr;

public:
    constexpr SetOnce() noexcept = default;
    ~SetOnce() = default;

    SetOnce(const SetOnce&) = default;
    SetOnce(SetOnce&&) = default;
    SetOnce& operator=(const SetOnce&) = default;
    SetOnce& operator=(SetOnce&&) = default;

    // The conditions are spelled in the body rather than as contract
    // clauses.  A clause that reads a member through the implicit this
    // is skipped during constant evaluation of a foldable body, so a
    // negative-compile fixture could never witness it fire.  constexpr
    // is what lets such a fixture force the check at compile time.
    CRUCIBLE_INLINE constexpr void set(T* p) noexcept {
        CRUCIBLE_PRE(p != nullptr);
        CRUCIBLE_PRE(ptr_ == nullptr);
        ptr_ = p;
        CRUCIBLE_POST(0, ptr_ == p);
    }

    [[nodiscard]] CRUCIBLE_INLINE constexpr bool try_set(T* p) noexcept {
        bool const claimed = (ptr_ == nullptr) && (p != nullptr);
        if (claimed) {
            ptr_ = p;
        }
        // The disjunction covers all three exits.  A claim stores a
        // non-null p.  A refused null leaves the slot as it stood.  A
        // refused double-set leaves the slot already non-null.
        CRUCIBLE_POST(claimed, p == nullptr || ptr_ != nullptr);
        return claimed;
    }

    [[nodiscard]] CRUCIBLE_INLINE constexpr T* get() const noexcept { return ptr_; }
    [[nodiscard]] CRUCIBLE_INLINE constexpr bool has_value() const noexcept { return ptr_ != nullptr; }
    [[nodiscard]] CRUCIBLE_INLINE constexpr explicit operator bool() const noexcept { return ptr_ != nullptr; }

    // Resetting a slot that was never set masks a missed
    // initialization, so the precondition refuses it.
    constexpr void reset() noexcept {
        CRUCIBLE_PRE(has_value());
        ptr_ = nullptr;
        CRUCIBLE_POST(0, ptr_ == nullptr);
    }
};

static_assert(sizeof(SetOnce<int>) == sizeof(int*));
static_assert(sizeof(SetOnce<void>) == sizeof(void*));

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
        // completed state.  The non-template members do take posts.
    }

    [[nodiscard]] bool done() const noexcept { return state_.load(std::memory_order_acquire) == 2; }
};

template <typename T>
class Lazy : ::foundation::Pinned<Lazy<T>> {
    alignas(T) unsigned char storage_[sizeof(T)]{};
    Once once_{};

    // These bytes hold a T whose lifetime begins at the placement new
    // inside get_or_init.  launder re-establishes that fact, which the
    // optimizer cannot see: under strict aliasing it is otherwise free
    // to assume the character array is still the live object.
    //
    // start_lifetime_as is the wrong tool here.  It would create a
    // fresh T at the address, ending the placement-new object's
    // lifetime and leaving the destructor below to run against a
    // different object.  It suits an implicit-lifetime T that is never
    // placement-new'd, and T here is arbitrary.
    [[nodiscard]] T* storage_ptr_() noexcept { return std::launder(static_cast<T*>(static_cast<void*>(&storage_))); }
    [[nodiscard]] const T* storage_ptr_() const noexcept {
        return std::launder(static_cast<const T*>(static_cast<const void*>(&storage_)));
    }

public:
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
        once_.call([&]() noexcept { ::new(&storage_) T(std::forward<F>(f)()); });
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

namespace detail::lazy_self_test {

// The checks below run rather than fold, so a refactor that made the
// stored initializer re-runnable fails here instead of compiling.
inline void runtime_smoke_test() {
    const int seed = 0xA5C3;
    int invocations = 0;
    Lazy<int> lazy{};

    if (lazy.initialized()) std::abort();

    int& first = lazy.get_or_init([&] {
        ++invocations;
        return seed + 1;
    });
    if (first != seed + 1) std::abort();
    if (invocations != 1) std::abort();
    if (!lazy.initialized()) std::abort();

    int& second = lazy.get_or_init([&] {
        ++invocations;
        return seed + 99999;
    });
    if (invocations != 1) std::abort();
    if (&second != &first) std::abort();
    if (second != seed + 1) std::abort();

    int& third = lazy.get();
    if (&third != &first) std::abort();
    if (third != seed + 1) std::abort();

    const Lazy<int>& clazy = lazy;
    const int& fourth = clazy.get();
    if (&fourth != &first) std::abort();
}

}  // namespace detail::lazy_self_test

}  // namespace fixy::handle
