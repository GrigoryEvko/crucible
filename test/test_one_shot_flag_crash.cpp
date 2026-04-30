// FOUND-G62 — OneShotFlag Crash-pinned production surface.
//
// Verifies the three pinned variants added to OneShotFlag:
//   - peek_nothrow()                  → Crash<NoThrow, bool>
//   - signal_throw()                  → Crash<Throw, signal_marker>
//   - try_acknowledge_error_return(F) → Crash<ErrorReturn, bool>
//
// Layered against the existing OneShotFlag tests — the raw surface
// (peek, signal, check_and_run) is exercised in test_one_shot_flag;
// this file proves the additive Crash-pinned overlay preserves the
// happy-path semantics while pinning failure-mode classification at
// the type level.

#include <crucible/handles/OneShotFlag.h>
#include <crucible/safety/Crash.h>
#include "test_assert.h"

#include <atomic>
#include <cstdio>
#include <type_traits>
#include <utility>

using crucible::safety::OneShotFlag;
using crucible::safety::Crash;
using crucible::safety::CrashClass_v;

// ── T01 — peek_nothrow bit-equality vs raw peek ──────────────────
static void test_peek_nothrow_bit_equality() {
    OneShotFlag flag;

    // Initial state: both peek() and peek_nothrow().consume() are false.
    auto initial_pinned = flag.peek_nothrow();
    bool raw = flag.peek();
    assert(raw == false);
    assert(std::move(initial_pinned).consume() == false);

    // After signal: both return true (release-ordered store).
    flag.signal();
    auto after_pinned = flag.peek_nothrow();
    bool raw2 = flag.peek();
    assert(raw2 == true);
    assert(std::move(after_pinned).consume() == true);
}

// ── T02 — type-identity for peek_nothrow ─────────────────────────
static void test_peek_nothrow_type_identity() {
    OneShotFlag flag;
    using Got = decltype(flag.peek_nothrow());
    using Want = Crash<CrashClass_v::NoThrow, bool>;
    static_assert(std::is_same_v<Got, Want>,
        "peek_nothrow must return Crash<NoThrow, bool>");
    static_assert(Got::crash_class == CrashClass_v::NoThrow);
}

// ── T03 — type-identity for signal_throw ─────────────────────────
static void test_signal_throw_type_identity() {
    OneShotFlag flag;
    using Got = decltype(flag.signal_throw());
    using Want = Crash<CrashClass_v::Throw, OneShotFlag::signal_marker>;
    static_assert(std::is_same_v<Got, Want>,
        "signal_throw must return Crash<Throw, signal_marker>");
    static_assert(Got::crash_class == CrashClass_v::Throw);
}

// ── T04 — type-identity for try_acknowledge_error_return ─────────
static void test_try_acknowledge_type_identity() {
    OneShotFlag flag;
    auto noop = []() noexcept {};
    using Got = decltype(flag.try_acknowledge_error_return(noop));
    using Want = Crash<CrashClass_v::ErrorReturn, bool>;
    static_assert(std::is_same_v<Got, Want>,
        "try_acknowledge_error_return must return Crash<ErrorReturn, bool>");
    static_assert(Got::crash_class == CrashClass_v::ErrorReturn);
}

// ── T05 — fence-acceptance simulation: NoThrow steady-state
//         admits at any consumer gate; ErrorReturn admits at
//         ErrorReturn-or-weaker; Throw admits at Throw-or-weaker. ──
template <typename W>
concept admissible_at_nothrow_fence =
    W::crash_class == CrashClass_v::NoThrow;

template <typename W>
concept admissible_at_error_return_fence =
    W::crash_class == CrashClass_v::NoThrow ||
    W::crash_class == CrashClass_v::ErrorReturn;

template <typename W>
concept admissible_at_throw_fence =
    W::crash_class == CrashClass_v::NoThrow     ||
    W::crash_class == CrashClass_v::ErrorReturn ||
    W::crash_class == CrashClass_v::Throw;

static void test_fence_simulation() {
    using NT = Crash<CrashClass_v::NoThrow, bool>;
    using ER = Crash<CrashClass_v::ErrorReturn, bool>;
    using TR = Crash<CrashClass_v::Throw, bool>;
    using AB = Crash<CrashClass_v::Abort, bool>;

    // NoThrow steady-state passes every gate.
    static_assert( admissible_at_nothrow_fence<NT>);
    static_assert( admissible_at_error_return_fence<NT>);
    static_assert( admissible_at_throw_fence<NT>);

    // ErrorReturn passes ErrorReturn-or-weaker.
    static_assert(!admissible_at_nothrow_fence<ER>);
    static_assert( admissible_at_error_return_fence<ER>);
    static_assert( admissible_at_throw_fence<ER>);

    // Throw passes only Throw-or-weaker.
    static_assert(!admissible_at_nothrow_fence<TR>);
    static_assert(!admissible_at_error_return_fence<TR>);
    static_assert( admissible_at_throw_fence<TR>);

    // Abort fails every gate above Abort.
    static_assert(!admissible_at_nothrow_fence<AB>);
    static_assert(!admissible_at_error_return_fence<AB>);
    static_assert(!admissible_at_throw_fence<AB>);
}

// ── T06 — negative tier witnesses ─────────────────────────────────
static void test_negative_tier_witnesses() {
    using NT = Crash<CrashClass_v::NoThrow, bool>;
    using ER = Crash<CrashClass_v::ErrorReturn, bool>;
    using TR = Crash<CrashClass_v::Throw, bool>;
    using AB = Crash<CrashClass_v::Abort, bool>;

    // Lattice direction (CrashLattice.h):
    //     Abort(weakest) ⊑ Throw ⊑ ErrorReturn ⊑ NoThrow(strongest)
    //
    // satisfies<Required>: Self must be Required-or-stronger.

    // NoThrow satisfies all (top of lattice).
    static_assert( NT::satisfies<CrashClass_v::NoThrow>);
    static_assert( NT::satisfies<CrashClass_v::ErrorReturn>);
    static_assert( NT::satisfies<CrashClass_v::Throw>);
    static_assert( NT::satisfies<CrashClass_v::Abort>);

    // ErrorReturn satisfies ErrorReturn-or-weaker.
    static_assert(!ER::satisfies<CrashClass_v::NoThrow>);
    static_assert( ER::satisfies<CrashClass_v::ErrorReturn>);
    static_assert( ER::satisfies<CrashClass_v::Throw>);
    static_assert( ER::satisfies<CrashClass_v::Abort>);

    // Throw satisfies Throw-or-weaker.
    static_assert(!TR::satisfies<CrashClass_v::NoThrow>);
    static_assert(!TR::satisfies<CrashClass_v::ErrorReturn>);
    static_assert( TR::satisfies<CrashClass_v::Throw>);
    static_assert( TR::satisfies<CrashClass_v::Abort>);

    // Abort satisfies only itself (bottom of lattice).
    static_assert(!AB::satisfies<CrashClass_v::NoThrow>);
    static_assert(!AB::satisfies<CrashClass_v::ErrorReturn>);
    static_assert(!AB::satisfies<CrashClass_v::Throw>);
    static_assert( AB::satisfies<CrashClass_v::Abort>);
}

// ── T07 — layout invariant ───────────────────────────────────────
static void test_layout_invariant() {
    static_assert(sizeof(Crash<CrashClass_v::NoThrow, bool>) == sizeof(bool));
    // signal_marker is empty; Crash<Throw, signal_marker> EBO-collapses
    // to ≤ 1 byte (one byte of address discriminator).  We verify
    // signal_marker is empty here; the wrapper sizeof reflects its
    // empty-class cost (≥1 byte).
    static_assert(std::is_empty_v<OneShotFlag::signal_marker>);
}

// ── T08 — signal_throw effects: raises the flag observably ───────
static void test_signal_throw_raises_flag() {
    OneShotFlag flag;
    assert(flag.peek() == false);

    auto marker = flag.signal_throw();  // wrapper preserves [[nodiscard]]
    (void)std::move(marker).consume();

    // The release-ordered store inside signal_throw is observable.
    assert(flag.peek() == true);
}

// ── T09 — try_acknowledge_error_return runs body on signal ──────
static void test_try_acknowledge_runs_body_on_signal() {
    OneShotFlag flag;
    int run_count = 0;
    auto body = [&]() noexcept { ++run_count; };

    // First call: flag not set → body NOT invoked → returns false.
    auto r1 = flag.try_acknowledge_error_return(body);
    assert(std::move(r1).consume() == false);
    assert(run_count == 0);

    // Set the flag, then call again: body IS invoked, flag clears.
    flag.signal();
    auto r2 = flag.try_acknowledge_error_return(body);
    assert(std::move(r2).consume() == true);
    assert(run_count == 1);

    // Flag should be cleared after acknowledgment.
    assert(flag.peek() == false);

    // Third call: flag cleared again → body NOT invoked.
    auto r3 = flag.try_acknowledge_error_return(body);
    assert(std::move(r3).consume() == false);
    assert(run_count == 1);
}

// ── T10 — relax DOWN-the-lattice (admissible) ───────────────────
//         NoThrow → ErrorReturn → Throw → Abort
static void test_relax_to_weaker() {
    OneShotFlag flag;
    auto pinned = flag.peek_nothrow();  // NoThrow
    // Going down the lattice (NoThrow → ErrorReturn) is admissible.
    auto relaxed = std::move(pinned).relax<CrashClass_v::ErrorReturn>();
    static_assert(std::is_same_v<decltype(relaxed),
        Crash<CrashClass_v::ErrorReturn, bool>>);
    (void)std::move(relaxed).consume();
}

// ── T11 — type-level chain composition ────────────────────────────
static void test_chain_composition() {
    using NT = Crash<CrashClass_v::NoThrow, bool>;
    // NoThrow at top — satisfies every weaker class.
    static_assert(NT::satisfies<CrashClass_v::NoThrow>);
    static_assert(NT::satisfies<CrashClass_v::ErrorReturn>);
    static_assert(NT::satisfies<CrashClass_v::Throw>);
    static_assert(NT::satisfies<CrashClass_v::Abort>);
}

// ── T12 — end-to-end fence-checked NoThrow consumer ─────────────
template <typename W>
    requires (W::template satisfies<CrashClass_v::NoThrow>)
static bool nothrow_steady_state_consumer(W wrapped) noexcept {
    return std::move(wrapped).consume();
}

static void test_e2e_nothrow_consumer() {
    OneShotFlag flag;
    auto pinned = flag.peek_nothrow();
    bool result = nothrow_steady_state_consumer(std::move(pinned));
    assert(result == false);
}

// ── T13 — end-to-end fence-checked ErrorReturn consumer ─────────
template <typename W>
    requires (W::template satisfies<CrashClass_v::ErrorReturn>)
static bool error_return_consumer(W wrapped) noexcept {
    return std::move(wrapped).consume();
}

static void test_e2e_error_return_consumer() {
    OneShotFlag flag;
    flag.signal();

    auto ack = flag.try_acknowledge_error_return([]() noexcept {});
    bool ran = error_return_consumer(std::move(ack));
    assert(ran == true);
}

// ── T14 — NoThrow value flows through ErrorReturn consumer
//         (admissible direction; lattice subsumption) ─────────────
static void test_nothrow_satisfies_error_return() {
    OneShotFlag flag;
    auto pinned = flag.peek_nothrow();   // NoThrow
    // NoThrow satisfies the ErrorReturn gate (stronger satisfies weaker).
    bool result = error_return_consumer(std::move(pinned));
    assert(result == false);
}

// ── T15 — signal_throw is [[nodiscard]] — accidental drop is a
//         compile-time diagnostic on Crash's [[nodiscard]] attribute.
//         Here we only verify the type chain.
static void test_signal_throw_nodiscard_chain() {
    OneShotFlag flag;
    using Got = decltype(flag.signal_throw());
    // [[nodiscard]] is on the Crash class; verify via static_assert
    // that the wrapper preserves the discardability discipline.
    auto marker = flag.signal_throw();
    (void)std::move(marker).consume();
    static_assert(std::is_same_v<Got,
        Crash<CrashClass_v::Throw, OneShotFlag::signal_marker>>);
}

int main() {
    test_peek_nothrow_bit_equality();
    test_peek_nothrow_type_identity();
    test_signal_throw_type_identity();
    test_try_acknowledge_type_identity();
    test_fence_simulation();
    test_negative_tier_witnesses();
    test_layout_invariant();
    test_signal_throw_raises_flag();
    test_try_acknowledge_runs_body_on_signal();
    test_relax_to_weaker();
    test_chain_composition();
    test_e2e_nothrow_consumer();
    test_e2e_error_return_consumer();
    test_nothrow_satisfies_error_return();
    test_signal_throw_nodiscard_chain();
    std::puts("ok");
    return 0;
}
