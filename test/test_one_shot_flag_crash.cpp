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

static void test_peek_nothrow_bit_equality() {
    OneShotFlag flag;

    auto initial_pinned = flag.peek_nothrow();
    bool raw = flag.peek();
    assert(raw == false);
    assert(std::move(initial_pinned).consume() == false);

    flag.signal();
    auto after_pinned = flag.peek_nothrow();
    bool raw2 = flag.peek();
    assert(raw2 == true);
    assert(std::move(after_pinned).consume() == true);
}

static void test_peek_nothrow_type_identity() {
    OneShotFlag flag;
    using Got = decltype(flag.peek_nothrow());
    using Want = Crash<CrashClass_v::NoThrow, bool>;
    static_assert(std::is_same_v<Got, Want>, "peek_nothrow must return Crash<NoThrow, bool>");
    static_assert(Got::crash_class == CrashClass_v::NoThrow);
}

static void test_signal_throw_type_identity() {
    OneShotFlag flag;
    using Got = decltype(flag.signal_throw());
    using Want = Crash<CrashClass_v::Throw, OneShotFlag::signal_marker>;
    static_assert(std::is_same_v<Got, Want>, "signal_throw must return Crash<Throw, signal_marker>");
    static_assert(Got::crash_class == CrashClass_v::Throw);
}

static void test_try_acknowledge_type_identity() {
    OneShotFlag flag;
    auto noop = []() noexcept {};
    using Got = decltype(flag.try_acknowledge_error_return(noop));
    using Want = Crash<CrashClass_v::ErrorReturn, bool>;
    static_assert(std::is_same_v<Got, Want>, "try_acknowledge_error_return must return Crash<ErrorReturn, bool>");
    static_assert(Got::crash_class == CrashClass_v::ErrorReturn);
}

// Stand-ins for a consumer gate at each tier, so the admission direction can
// be exercised without a real consumer.
template <typename W>
concept admissible_at_nothrow_fence = W::crash_class == CrashClass_v::NoThrow;

template <typename W>
concept admissible_at_error_return_fence =
    W::crash_class == CrashClass_v::NoThrow || W::crash_class == CrashClass_v::ErrorReturn;

template <typename W>
concept admissible_at_throw_fence =
    W::crash_class == CrashClass_v::NoThrow || W::crash_class == CrashClass_v::ErrorReturn
    || W::crash_class == CrashClass_v::Throw;

static void test_fence_simulation() {
    using NT = Crash<CrashClass_v::NoThrow, bool>;
    using ER = Crash<CrashClass_v::ErrorReturn, bool>;
    using TR = Crash<CrashClass_v::Throw, bool>;
    using AB = Crash<CrashClass_v::Abort, bool>;

    static_assert(admissible_at_nothrow_fence<NT>);
    static_assert(admissible_at_error_return_fence<NT>);
    static_assert(admissible_at_throw_fence<NT>);

    static_assert(!admissible_at_nothrow_fence<ER>);
    static_assert(admissible_at_error_return_fence<ER>);
    static_assert(admissible_at_throw_fence<ER>);

    static_assert(!admissible_at_nothrow_fence<TR>);
    static_assert(!admissible_at_error_return_fence<TR>);
    static_assert(admissible_at_throw_fence<TR>);

    static_assert(!admissible_at_nothrow_fence<AB>);
    static_assert(!admissible_at_error_return_fence<AB>);
    static_assert(!admissible_at_throw_fence<AB>);
}

static void test_negative_tier_witnesses() {
    using NT = Crash<CrashClass_v::NoThrow, bool>;
    using ER = Crash<CrashClass_v::ErrorReturn, bool>;
    using TR = Crash<CrashClass_v::Throw, bool>;
    using AB = Crash<CrashClass_v::Abort, bool>;

    // Abort is the weakest class, then Throw, then ErrorReturn, with NoThrow
    // strongest. satisfies<Required> holds when the wrapper's own class is
    // Required or stronger, so the table below is a staircase.

    static_assert(NT::satisfies<CrashClass_v::NoThrow>);
    static_assert(NT::satisfies<CrashClass_v::ErrorReturn>);
    static_assert(NT::satisfies<CrashClass_v::Throw>);
    static_assert(NT::satisfies<CrashClass_v::Abort>);

    static_assert(!ER::satisfies<CrashClass_v::NoThrow>);
    static_assert(ER::satisfies<CrashClass_v::ErrorReturn>);
    static_assert(ER::satisfies<CrashClass_v::Throw>);
    static_assert(ER::satisfies<CrashClass_v::Abort>);

    static_assert(!TR::satisfies<CrashClass_v::NoThrow>);
    static_assert(!TR::satisfies<CrashClass_v::ErrorReturn>);
    static_assert(TR::satisfies<CrashClass_v::Throw>);
    static_assert(TR::satisfies<CrashClass_v::Abort>);

    static_assert(!AB::satisfies<CrashClass_v::NoThrow>);
    static_assert(!AB::satisfies<CrashClass_v::ErrorReturn>);
    static_assert(!AB::satisfies<CrashClass_v::Throw>);
    static_assert(AB::satisfies<CrashClass_v::Abort>);
}

static void test_layout_invariant() {
    static_assert(sizeof(Crash<CrashClass_v::NoThrow, bool>) == sizeof(bool));
    // Only emptiness is pinned. A Crash over an empty payload collapses under
    // EBO yet still occupies at least one byte, so asserting its sizeof would
    // pin an uninteresting number.
    static_assert(std::is_empty_v<OneShotFlag::signal_marker>);
}

static void test_signal_throw_raises_flag() {
    OneShotFlag flag;
    assert(flag.peek() == false);

    auto marker = flag.signal_throw();
    (void)std::move(marker).consume();

    // The store inside signal_throw is release-ordered and observable here.
    assert(flag.peek() == true);
}

static void test_try_acknowledge_runs_body_on_signal() {
    OneShotFlag flag;
    int run_count = 0;
    auto body = [&]() noexcept { ++run_count; };

    auto r1 = flag.try_acknowledge_error_return(body);
    assert(std::move(r1).consume() == false);
    assert(run_count == 0);

    flag.signal();
    auto r2 = flag.try_acknowledge_error_return(body);
    assert(std::move(r2).consume() == true);
    assert(run_count == 1);

    assert(flag.peek() == false);

    auto r3 = flag.try_acknowledge_error_return(body);
    assert(std::move(r3).consume() == false);
    assert(run_count == 1);
}

static void test_relax_to_weaker() {
    OneShotFlag flag;
    auto pinned = flag.peek_nothrow();
    auto relaxed = std::move(pinned).relax<CrashClass_v::ErrorReturn>();
    static_assert(std::is_same_v<decltype(relaxed), Crash<CrashClass_v::ErrorReturn, bool>>);
    (void)std::move(relaxed).consume();
}

static void test_chain_composition() {
    using NT = Crash<CrashClass_v::NoThrow, bool>;
    static_assert(NT::satisfies<CrashClass_v::NoThrow>);
    static_assert(NT::satisfies<CrashClass_v::ErrorReturn>);
    static_assert(NT::satisfies<CrashClass_v::Throw>);
    static_assert(NT::satisfies<CrashClass_v::Abort>);
}

template <typename W>
    requires(W::template satisfies<CrashClass_v::NoThrow>)
static bool nothrow_steady_state_consumer(W wrapped) noexcept {
    return std::move(wrapped).consume();
}

static void test_e2e_nothrow_consumer() {
    OneShotFlag flag;
    auto pinned = flag.peek_nothrow();
    bool result = nothrow_steady_state_consumer(std::move(pinned));
    assert(result == false);
}

template <typename W>
    requires(W::template satisfies<CrashClass_v::ErrorReturn>)
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

static void test_nothrow_satisfies_error_return() {
    OneShotFlag flag;
    auto pinned = flag.peek_nothrow();
    bool result = error_return_consumer(std::move(pinned));
    assert(result == false);
}

// Dropping the result is a compile-time diagnostic, which no passing test can
// provoke, so only the type chain is pinned here.
static void test_signal_throw_nodiscard_chain() {
    OneShotFlag flag;
    using Got = decltype(flag.signal_throw());
    auto marker = flag.signal_throw();
    (void)std::move(marker).consume();
    static_assert(std::is_same_v<Got, Crash<CrashClass_v::Throw, OneShotFlag::signal_marker>>);
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
