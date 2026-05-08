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

#include <atomic>
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
    CRUCIBLE_INLINE void set(T* p) noexcept
        pre (p != nullptr)
        pre (ptr_ == nullptr)
    {
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
    [[nodiscard]] CRUCIBLE_INLINE bool try_set(T* p) noexcept {
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

    [[nodiscard]] CRUCIBLE_INLINE T* get() const noexcept { return ptr_; }
    [[nodiscard]] CRUCIBLE_INLINE bool has_value() const noexcept { return ptr_ != nullptr; }
    [[nodiscard]] CRUCIBLE_INLINE explicit operator bool() const noexcept { return ptr_ != nullptr; }

    // Reset to unset state.  Use only when the lifecycle explicitly
    // permits re-setting (e.g. test fixtures, quiescent reconfig).
    // contract_assert on has_value() so accidental reset of an unset
    // slot — which masks a missed initialization — trips.
    void reset() noexcept
        pre (has_value())
    {
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
// Deferred initialization.  get() returns a reference to the stored
// T; on first call, invokes the initializer.  Thread-safe: if
// multiple threads race on the first get(), exactly one runs the
// init, the others wait.
//
// sizeof(Lazy<T>) == sizeof(T) + sizeof(Once) + padding.  Heavier
// than SetOnce because it stores the T inline rather than behind a
// pointer; use SetOnce for pointer-sized slots and Lazy when you
// want the value co-located.

template <typename T>
class Lazy : Pinned<Lazy<T>> {
    // Storage for T, left uninitialized until first get().
    alignas(T) unsigned char storage_[sizeof(T)]{};
    Once once_{};

public:
    constexpr Lazy() noexcept = default;

    // Destructor calls T::~T only if the Once ran.
    ~Lazy() {
        if (once_.done()) {
            static_cast<T*>(static_cast<void*>(&storage_))->~T();
        }
    }

    // get(f): first call invokes f() and stores the result.
    // Subsequent calls return a reference to the stored value without
    // re-invoking f.  f must be invocable with no args and return T.
    //
    // The placement-new at &storage_ begins T's lifetime in the
    // unsigned char[] backing.  The cast cascade (static_cast<T*>(
    // static_cast<void*>(&storage_))) is the §III-clean equivalent of
    // reinterpret_cast<T*>: it reaches through void* without invoking
    // pointer-type-pun UB.
    template <typename F>
    [[nodiscard]] T& get(F&& f)
        requires std::is_invocable_r_v<T, F>
    {
        once_.call([&] {
            ::new (&storage_) T(std::forward<F>(f)());
        });
        return *static_cast<T*>(static_cast<void*>(&storage_));
    }

    [[nodiscard]] bool initialized() const noexcept {
        return once_.done();
    }
};

} // namespace crucible::safety
