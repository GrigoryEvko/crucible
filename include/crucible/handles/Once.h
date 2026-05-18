#pragma once

// ── crucible::safety::{SetOnce, Once, Lazy} ─────────────────────────
//
// Single-set state primitives that complement WriteOnce<T> from
// Mutation.h.  Three siblings with different storage strategies:
//
//   SetOnce<T*>  — sentinel-based single-set for pointer types.
//                  sizeof == sizeof(T*). nullptr is the "not set"
//                  state.  Cheaper than WriteOnce<T*> because no
//                  tag byte is needed.
//
//   Once<F>      — run a callable exactly once, discard after.
//                  Thread-safe via std::call_once analog but without
//                  the pthread_once backing (hot-path weight
//                  mismatch).  Uses atomic<uint8_t> state machine.
//
//   Lazy<T>      — deferred initialization from a function returning
//                  T.  Thread-safe; subsequent gets are the stored
//                  value.  The function runs at most once across all
//                  threads.
//
//   Axiom coverage: InitSafe, MemSafe, ThreadSafe.
//   Runtime cost:   varies by primitive, documented per-type.
//
// vs WriteOnce<T>: WriteOnce uses std::optional (sizeof(T) + 1 byte
// tag + padding). These siblings skip the optional when a sentinel
// fits the semantics, collapsing sizeof.
//
// vs PublishOnce<T*> (PublishOnce.h): PublishOnce is specifically
// for cross-thread pointer handoff with release/acquire memory
// ordering.  SetOnce<T*> is single-thread write-once, zero
// synchronization overhead.

#include <crucible/Platform.h>
#include <crucible/safety/Pinned.h>
#include <crucible/safety/Post.h>
#include <crucible/safety/Pre.h>

#include <atomic>
#include <cstdlib>
#include <new>
#include <type_traits>
#include <utility>

namespace crucible::safety {

// ── SetOnce<T*> ────────────────────────────────────────────────────
//
// Sentinel-based single-set pointer slot for same-thread use.
// nullptr = unset, any non-null = set.  Second set() fires a contract;
// legitimate resets go through reset() explicitly.
//
// Compared to WriteOnce<T*>: saves the optional<> tag byte + padding,
// giving sizeof(SetOnce<T*>) == sizeof(T*).  The tradeoff is that
// nullptr is reserved as the unset sentinel; callers that need to
// distinguish "published nullptr" from "never published" need
// PublishOnce or WriteOnce<optional<T*>>.

template <typename T>
class [[nodiscard]] SetOnce {
    T* ptr_ = nullptr;

public:
    constexpr SetOnce() noexcept = default;
    ~SetOnce() = default;

    SetOnce(const SetOnce&)            = default;
    SetOnce(SetOnce&&)                 = default;
    SetOnce& operator=(const SetOnce&) = default;
    SetOnce& operator=(SetOnce&&)      = default;

    // Set exactly once.  Contract fires on double-set and on null
    // input (null is reserved as the unset sentinel; publishing null
    // is always a caller bug).
    //
    // constexpr-eligible so HS14 negative-compile fixtures can witness
    // CRUCIBLE_PRE consteval-fire under static_assert (fixy-A1-007).
    CRUCIBLE_INLINE constexpr void set(T* p) noexcept
    {
        // fixy-A1-007: pre clauses migrated from P2900 form to body-
        // CRUCIBLE_PRE.  The first clause references the parameter `p`;
        // the second references the class member `ptr_` through
        // implicit `this->`.  GCC 16.1.1 silently bypasses the second
        // shape at consteval for foldable bodies; the patched g++-16p
        // closes the bypass but the macro form fires identically
        // across both compilers (feedback_gcc16_c26_contract_gotchas
        // + feedback_crucible_pre_post_macros).  Pairs with the
        // existing CRUCIBLE_POST below per the dual-side discipline.
        CRUCIBLE_PRE(p != nullptr);
        CRUCIBLE_PRE(ptr_ == nullptr);
        ptr_ = p;
        // CONTRACT-SetOnce-Set-POST: state-mutation post.  Sibling of
        // CONTRACT-WriteOnceNonNull-Set-POST (safety/Mutation.h, commit
        // 98d0ff8) — same "pre rules out double-set and null input,
        // leaving ptr_ = p as the only legal mutation" framing.  The
        // patched g++-16p §13.6 foldable-body bypass would silently
        // pass `post(ptr_ == p)` under P2900 form; CRUCIBLE_POST is
        // the discharge form (cf. feedback_patched_gcc16_toolchain.md
        // 2026-05-08).  Catches a refactor that publishes a different
        // pointer (off-by-one, moved-from local).
        CRUCIBLE_POST(0, ptr_ == p);
    }

    // Try-set — returns true iff this was the first non-null set.
    // Unlike set(), accepts null input as a no-op (returns false).
    [[nodiscard]] CRUCIBLE_INLINE constexpr bool try_set(T* p) noexcept {
        // Refactored to single-return so the post fires on every path.
        // Semantics preserved: claim iff slot was empty AND p is non-
        // null.  Null input and double-set both yield claimed=false.
        bool const claimed = (ptr_ == nullptr) && (p != nullptr);
        if (claimed) {
            ptr_ = p;
        }
        // CONTRACT-SetOnce-TrySet-POST: lifecycle-witness post mirror of
        // CONTRACT-WriteOnceNonNull-TrySet-POST.  Three cases unified:
        //   1. claimed=true            ⇒ ptr_ == p ≠ nullptr ⇒ ptr_ != nullptr
        //   2. claimed=false, p=null   ⇒ no change; lhs holds
        //   3. claimed=false, ptr_ set ⇒ ptr_ remains non-null; rhs holds
        CRUCIBLE_POST(claimed, p == nullptr || ptr_ != nullptr);
        return claimed;
    }

    [[nodiscard]] CRUCIBLE_INLINE constexpr T* get() const noexcept { return ptr_; }
    [[nodiscard]] CRUCIBLE_INLINE constexpr bool has_value() const noexcept { return ptr_ != nullptr; }
    [[nodiscard]] CRUCIBLE_INLINE constexpr explicit operator bool() const noexcept { return ptr_ != nullptr; }

    // Reset to unset state.  Use only when the lifecycle explicitly
    // permits re-setting (e.g. test fixtures, quiescent reconfig).
    // contract_assert on has_value() so accidental reset of an unset
    // slot — which masks a missed initialization — trips.
    //
    // constexpr-eligible per the same fixy-A1-007 rationale as set().
    constexpr void reset() noexcept
    {
        // fixy-A1-007: pre clause migrated to body-CRUCIBLE_PRE.
        // `has_value()` is a member function accessing `ptr_` through
        // implicit `this->`, the canonical consteval-bypass shape on
        // GCC 16.1.1.  Pairs with the existing CRUCIBLE_POST below.
        CRUCIBLE_PRE(has_value());
        ptr_ = nullptr;
        // CONTRACT-SetOnce-Reset-POST: lifecycle-reset post (CRUCIBLE_POST
        // taxonomy class 3).  After reset() returns, the slot must be
        // back in the unset state — failure to clear ptr_ would leave
        // the next set() pre-check `ptr_ == nullptr` immediately false,
        // tripping the contract from a stale value rather than from a
        // legitimate double-set.  Sibling of IterDet::reset 6-post
        // family / CrucibleContext::deactivate post-pair (commit
        // 0cf7c20) — same "lifecycle returns the structure to a
        // documented ground state" framing.
        CRUCIBLE_POST(0, ptr_ == nullptr);
    }
};

static_assert(sizeof(SetOnce<int>)  == sizeof(int*));
static_assert(sizeof(SetOnce<void>) == sizeof(void*));

// ── Once<F> ────────────────────────────────────────────────────────
//
// Thread-safe "run this function exactly once" primitive.  Races
// between threads resolve via atomic compare_exchange; losers spin-
// wait for the winner to complete.  No std::call_once dependency
// (that's pthread_once in disguise on libstdc++ and brings futex
// overhead we don't need on the Vessel startup path, which is the
// primary consumer).
//
// State machine on atomic<uint8_t>:
//   0 = never-run
//   1 = in-progress (one thread is running the function)
//   2 = completed
//
// Loser-threads spin on the state until 2 (runtime is O(body of f));
// typical use case is one-shot registration at startup where the
// winner completes in microseconds.

class Once : Pinned<Once> {
    // state lives alone on its cache line — Once instances are
    // typically global initialization markers; contention between
    // threads on the same Once is the common case (all trying to
    // register the same subsystem), so alignment here prevents the
    // Once from sharing a line with unrelated state.
    alignas(64) std::atomic<uint8_t> state_{0};

public:
    constexpr Once() noexcept = default;

    // Run f() exactly once across all threads.  The thread that
    // transitions state 0→1 executes; others spin until state reaches
    // 2.  f must be idempotent with respect to external state in case
    // a crash happens between 0→1 and 1→2, leaving state stuck at 1;
    // the program is terminated in that case (the contract on set
    // would also fire, but a crash typically pre-empts that).
    template <typename F>
    void call(F&& f) noexcept(noexcept(std::forward<F>(f)()))
        requires std::is_invocable_v<F>
    {
        uint8_t expected = 0;
        if (state_.compare_exchange_strong(
                expected, 1,
                std::memory_order_acq_rel,
                std::memory_order_acquire)) {
            // We won the race — run f().
            std::forward<F>(f)();
            state_.store(2, std::memory_order_release);
            return;
        }
        // Loser: wait for state 2.
        while (state_.load(std::memory_order_acquire) != 2) {
            CRUCIBLE_SPIN_PAUSE;
        }
        // Note: Once::call is a function template instantiated in every
        // TU that uses it (e.g. crucible_perf libbpf-log-cb installation).
        // Adding CRUCIBLE_POST here pulls in crucible::detail::
        // contract_failed at every instantiation site, breaking link in
        // static libs not bundled with libcrucible's contract handler
        // (feedback_loader_tu_contract_semantic.md).  The state-machine
        // post `state_.load(acquire) == 2` is therefore enforced by the
        // class invariant + spin-wait structure itself, not by a post.
        // The non-template SetOnce primitives below DO get posts — they
        // instantiate exactly once per pointer-type, well-bounded.
    }

    [[nodiscard]] bool done() const noexcept {
        return state_.load(std::memory_order_acquire) == 2;
    }
};

// ── Lazy<T> ────────────────────────────────────────────────────────
//
// Deferred initialization.  get_or_init(f) returns a reference to
// the stored T; on first call, invokes f().  Thread-safe: if
// multiple threads race on the first get_or_init(), exactly one
// runs the init, the others wait.  get() returns the stored value
// after init; calling it before initialized() is a contract bug.
//
// sizeof(Lazy<T>) == sizeof(T) + sizeof(Once) + padding.  Heavier
// than SetOnce because it stores the T inline rather than behind a
// pointer; use SetOnce for pointer-sized slots and Lazy when you
// want the value co-located.
//
// fixy-A1-017: the original API exposed a single `get(F&& f)` that
// invoked f only on the FIRST call.  Subsequent calls silently
// ignored f even when the lambda body had been refactored — the
// classic "set-once, ignore-thereafter" footgun.  The split below
// makes the semantic explicit: `get_or_init` says it out loud, and
// a no-arg `get()` exists for fetch-only call sites where the F
// is irrelevant.  Same machinery, narrower call-site contract.

template <typename T>
class Lazy : Pinned<Lazy<T>> {
    // Storage for T, left uninitialized until first get_or_init().
    alignas(T) unsigned char storage_[sizeof(T)]{};
    Once once_{};

    // Internal reach-through: §III-clean cast cascade
    // `static_cast<T*>(static_cast<void*>(&storage_))` reaches through
    // void* without invoking reinterpret_cast.  Wrapped in
    // `std::launder` (fixy-A1-023): post-placement-new, the bytes at
    // `&storage_` host an object of type T whose lifetime started at
    // the `::new (&storage_) T(...)` call site inside get_or_init().
    // `std::launder` is the C++17/23-canonical optimizer barrier that
    // formally re-establishes that aliasing fact for the optimizer,
    // which is otherwise free to assume the original unsigned-char
    // array is still in lifetime under -O3 + strict aliasing.  Sibling
    // pattern in `concurrent/AdaptiveScheduler.h` lines 188/191/196.
    //
    // Why not `std::start_lifetime_as<T>` (CLAUDE.md §II MemSafe entry):
    // start_lifetime_as implicitly CREATES a T at the byte address — it
    // is the right tool for IMPLICIT-LIFETIME T accessed without
    // placement-new (the AtomicSnapshot/memcpy pattern in
    // `concurrent/AtomicSnapshot.h` lines 310/341).  Lazy<T> supports
    // arbitrary T (including non-trivial types whose dtor must be
    // called from `~Lazy()`); calling start_lifetime_as<T> here would
    // re-create the T, leaving the placement-new'd object's lifetime
    // wrong and the destructor's `->~T()` racing a fresh implicit T.
    // The distinction is documented at the cite site for future
    // refactor-safety.
    [[nodiscard]] T* storage_ptr_() noexcept {
        return std::launder(
            static_cast<T*>(static_cast<void*>(&storage_)));
    }
    [[nodiscard]] const T* storage_ptr_() const noexcept {
        return std::launder(
            static_cast<const T*>(static_cast<const void*>(&storage_)));
    }

public:
    constexpr Lazy() noexcept = default;

    // Destructor calls T::~T only if the Once ran.
    ~Lazy() {
        if (once_.done()) {
            storage_ptr_()->~T();
        }
    }

    // get_or_init(f): first call invokes f() and stores the result.
    // Subsequent calls return a reference to the stored value
    // WITHOUT re-invoking f.  The name encodes the semantic to
    // prevent the historical "silently ignored f" footgun.
    template <typename F>
    [[nodiscard]] T& get_or_init(F&& f) noexcept
        requires std::is_invocable_r_v<T, F>
    {
        // Crucible compiles with `-fno-exceptions` (CLAUDE.md §III),
        // so throw is impossible across this header.  Both the inner
        // wrapping lambda and `get_or_init` are declared unconditionally
        // `noexcept` to satisfy `-Wnoexcept` body inference — a
        // conditional `noexcept(noexcept(F()))` spec evaluates to
        // false for a user lambda whose `operator()` isn't declared
        // noexcept, while GCC's body analyzer sees the bodies have
        // no throw paths and warns (surfaced by fixy-A1-017's smoke
        // — a latent bug pure static_assert tests miss).
        once_.call([&]() noexcept {
            ::new (&storage_) T(std::forward<F>(f)());
        });
        return *storage_ptr_();
    }

    // get(): fetch the stored value.  Pre: initialized() must hold.
    // Use after at least one successful get_or_init() to acknowledge
    // that no initializer is being provided at this call site.
    [[nodiscard]] T& get() & {
        CRUCIBLE_PRE(initialized());
        return *storage_ptr_();
    }

    [[nodiscard]] const T& get() const& {
        CRUCIBLE_PRE(initialized());
        return *storage_ptr_();
    }

    [[nodiscard]] bool initialized() const noexcept {
        return once_.done();
    }
};

// ── runtime_smoke_test ──────────────────────────────────────────────
//
// fixy-A1-017: sentinel exercises the renamed Lazy<T> surface
// (get_or_init / no-arg get) on non-constant input under project
// warning flags.  Discipline reference: feedback_algebra_runtime_
// smoke_test_discipline + feedback_header_only_static_assert_blind_
// spot.  The "f only fires once" invariant is the load-bearing claim
// behind the rename; without a runtime witness, a refactor that
// broke once_.call into a re-runnable shape would compile silently.

namespace detail::lazy_self_test {

inline void runtime_smoke_test() {
    // Non-constant seed keeps bit-patterns out of the constant
    // folder's view.
    const int seed = 0xA5C3;
    int invocations = 0;
    Lazy<int> lazy{};

    if (lazy.initialized()) std::abort();

    // First call runs f; result is f()'s return value.
    int& first = lazy.get_or_init([&] {
        ++invocations;
        return seed + 1;
    });
    if (first != seed + 1) std::abort();
    if (invocations != 1) std::abort();
    if (!lazy.initialized()) std::abort();

    // Subsequent get_or_init passes a DIFFERENT lambda body — its
    // return value is silently discarded by design.  The witnesses:
    // (a) `invocations` stays at 1, (b) the returned ref still
    // aliases the first stored value.
    int& second = lazy.get_or_init([&] {
        ++invocations;
        return seed + 99999;  // body ignored on second call
    });
    if (invocations != 1) std::abort();
    if (&second != &first) std::abort();
    if (second != seed + 1) std::abort();

    // No-arg get() returns the same reference.
    int& third = lazy.get();
    if (&third != &first) std::abort();
    if (third != seed + 1) std::abort();

    // const get() returns const ref to the same storage.
    const Lazy<int>& clazy = lazy;
    const int& fourth = clazy.get();
    if (&fourth != &first) std::abort();
}

}  // namespace detail::lazy_self_test

} // namespace crucible::safety
