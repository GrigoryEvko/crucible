// What fixy/handle/PublishOnce.h and fixy/handle/Once.h claim, checked.
//
// Four handles, one question each.  PublishOnce takes exactly one
// publisher ever.  PublishSlot takes one again and again and lets a
// consumer take the pointer back.  Once gates a body so it runs once
// across threads, and Lazy is a value built on that gate.
//
// Two of these claims are runtime-only.  PublishOnce's double-publish
// abort is deliberately NOT a contract clause — the header says why: a
// hot-path translation unit builds with the contract semantic set to
// ignore, where a clause elides and a second publish quietly no-ops
// while the first pointer stays visible.  A helper that aborts
// regardless of the semantic can only be observed by running it, so
// that case runs in a child process through expect_dies.  Once's
// run-exactly-once needs threads for the same reason: a single-threaded
// call cannot distinguish the gate from an if.

#include <fixy/Core.h>
#include <fixy/Ctx.h>
#include <fixy/check/Check.h>
#include <fixy/handle/Once.h>
#include <fixy/handle/PublishOnce.h>
#include <foundation/effects/Effect.h>

#include <atomic>
#include <cstdio>
#include <thread>
#include <type_traits>
#include <vector>

namespace h = fixy::handle;

namespace {

struct Payload {
    int a = 0;
    double b = 0.0;
};

// ── The pointee-not-pointer guard ────────────────────────────────────
//
// A guard that read `is_pointer_v<T*> || is_same_v<T, T>` would admit
// PublishOnce<int*> and enforce nothing, because its second disjunct is
// true for every T.  These cells are the witness from outside the header
// that the concept discriminates.
static_assert(h::PublishOncePointee<Payload>);
static_assert(h::PublishOncePointee<void>);
static_assert(h::PublishOncePointee<const Payload>);
static_assert(!h::PublishOncePointee<Payload*>);
static_assert(!h::PublishOncePointee<void*>);
static_assert(!h::PublishOncePointee<Payload&>);

// ── Shape ────────────────────────────────────────────────────────────

// PublishOnce keeps the natural alignment of an atomic pointer.  It is
// written once per instance, so its line is invalidated once; dense
// structures hold thousands of them under a size-locked layout that
// padding would break.
static_assert(sizeof(h::PublishOnce<Payload>) == sizeof(std::atomic<Payload*>));
static_assert(alignof(h::PublishOnce<Payload>) == alignof(std::atomic<Payload*>));

// PublishSlot pads to a whole line instead, because it is written again
// and again and must not share one with anything.
static_assert(sizeof(h::PublishSlot<Payload>) >= 64);
static_assert(alignof(h::PublishSlot<Payload>) >= 64);
static_assert(alignof(h::Once) >= 64);

// Every one of these is a channel identity: an address other threads
// reach, or a slot whose duplication would split ownership.
static_assert(!std::is_copy_constructible_v<h::PublishOnce<Payload>>);
static_assert(!std::is_move_constructible_v<h::PublishOnce<Payload>>);
static_assert(!std::is_copy_constructible_v<h::PublishSlot<Payload>>);
static_assert(!std::is_move_constructible_v<h::PublishSlot<Payload>>);
static_assert(!std::is_copy_constructible_v<h::Once>);
static_assert(!std::is_move_constructible_v<h::Once>);
static_assert(!std::is_copy_constructible_v<h::Lazy<int>>);

// ── Runtime: the two publication slots ───────────────────────────────

[[nodiscard]] int publish_once_runs() {
    Payload value{7, 2.5};
    h::PublishOnce<Payload> slot{};

    if (slot.is_published() || slot.observe() != nullptr) {
        std::fprintf(stderr, "a fresh PublishOnce read as published\n");
        return 1;
    }

    slot.publish(&value);
    if (!slot.is_published() || slot.observe() != &value) {
        std::fprintf(stderr, "publish did not reach the slot\n");
        return 1;
    }
    return 0;
}

[[nodiscard]] int publish_slot_runs() {
    Payload first{1, 1.0};
    Payload second{2, 2.0};
    h::PublishSlot<Payload> slot{};

    if (slot.has_pending() || slot.consume() != nullptr) {
        std::fprintf(stderr, "a fresh PublishSlot held a pointer\n");
        return 1;
    }

    slot.publish(&first);
    if (!slot.has_pending() || slot.observe() != &first) {
        std::fprintf(stderr, "PublishSlot did not take the first publish\n");
        return 1;
    }

    // Unlike PublishOnce, republishing is the point: the slot is a
    // handoff point, not a one-shot claim.
    slot.publish(&second);
    if (slot.observe() != &second) {
        std::fprintf(stderr, "PublishSlot did not take the second publish\n");
        return 1;
    }

    if (slot.consume() != &second) {
        std::fprintf(stderr, "consume returned the wrong pointer\n");
        return 1;
    }
    if (slot.has_pending() || slot.consume() != nullptr) {
        std::fprintf(stderr, "consume left the slot occupied\n");
        return 1;
    }
    return 0;
}

// ── Runtime: the once-gate, under threads ────────────────────────────

[[nodiscard]] int once_runs_exactly_once() {
    constexpr int kThreads = 8;

    h::Once gate{};
    std::atomic<int> bodies{0};
    std::atomic<int> observed_done{0};

    {
        std::vector<std::jthread> racers;
        for (int i = 0; i < kThreads; ++i) {
            racers.emplace_back([&gate, &bodies, &observed_done] {
                // The lambda is declared noexcept deliberately.
                // Once::call derives its own specifier from the
                // callable, and a lambda whose call operator is not
                // noexcept makes that derivation read false — which
                // -Werror=noexcept then flags against a body that
                // provably does not throw.  Lazy::get_or_init carries
                // the same note in the header.
                gate.call([&bodies]() noexcept { bodies.fetch_add(1, std::memory_order_acq_rel); });
                // Every loser leaves call() only after the winner's
                // release store, so done() is true for all of them.
                if (gate.done()) observed_done.fetch_add(1, std::memory_order_acq_rel);
            });
        }
    }

    if (bodies.load(std::memory_order_acquire) != 1) {
        std::fprintf(stderr, "the once body ran %d times\n", bodies.load(std::memory_order_acquire));
        return 1;
    }
    if (observed_done.load(std::memory_order_acquire) != kThreads) {
        std::fprintf(stderr, "a thread left call() before the gate read as done\n");
        return 1;
    }
    if (!gate.done()) {
        std::fprintf(stderr, "the gate did not end up done\n");
        return 1;
    }
    return 0;
}

// The single-threaded path.  What it checks is behaviour under a
// contrived sequence of calls, not a property of Lazy as shipped, so it
// lives in a test and not in the header.
//
// These checks run rather than fold: a refactor that made the stored
// initializer re-runnable would fail here instead of compiling.
[[nodiscard]] int lazy_initializes_once_single_threaded() {
    const int seed = 0xA5C3;
    int invocations = 0;
    h::Lazy<int> lazy{};

    if (lazy.initialized()) {
        std::fprintf(stderr, "a fresh Lazy read as initialized\n");
        return 1;
    }

    int& first = lazy.get_or_init([&]() noexcept {
        ++invocations;
        return seed + 1;
    });
    if (first != seed + 1 || invocations != 1 || !lazy.initialized()) {
        std::fprintf(stderr, "the first get_or_init did not initialize once\n");
        return 1;
    }

    // The second initializer must never run, and the reference must be
    // the same object rather than a second one holding the same value.
    int& second = lazy.get_or_init([&]() noexcept {
        ++invocations;
        return seed + 99999;
    });
    if (invocations != 1 || &second != &first || second != seed + 1) {
        std::fprintf(stderr, "the second get_or_init re-ran the initializer\n");
        return 1;
    }

    int& third = lazy.get();
    if (&third != &first || third != seed + 1) {
        std::fprintf(stderr, "get() returned a different object than get_or_init\n");
        return 1;
    }

    const h::Lazy<int>& const_view = lazy;
    const int& fourth = const_view.get();
    if (&fourth != &first) {
        std::fprintf(stderr, "the const get() returned a different object\n");
        return 1;
    }
    return 0;
}

[[nodiscard]] int lazy_initializes_once() {
    constexpr int kThreads = 8;
    h::Lazy<int> lazy{};
    std::atomic<int> initializers{0};
    std::atomic<int> wrong_value{0};

    {
        std::vector<std::jthread> racers;
        for (int i = 0; i < kThreads; ++i) {
            racers.emplace_back([&lazy, &initializers, &wrong_value] {
                const int& value = lazy.get_or_init([&initializers]() noexcept {
                    initializers.fetch_add(1, std::memory_order_acq_rel);
                    return 4242;
                });
                if (value != 4242) wrong_value.fetch_add(1, std::memory_order_acq_rel);
            });
        }
    }

    if (initializers.load(std::memory_order_acquire) != 1) {
        std::fprintf(stderr, "the lazy initializer ran %d times\n", initializers.load(std::memory_order_acquire));
        return 1;
    }
    if (wrong_value.load(std::memory_order_acquire) != 0) {
        std::fprintf(stderr, "a racer read a value the initializer never produced\n");
        return 1;
    }
    if (!lazy.initialized() || lazy.get() != 4242) {
        std::fprintf(stderr, "the lazy value did not settle\n");
        return 1;
    }
    return 0;
}

// ── Runtime: the double-publish abort ────────────────────────────────
//
// The double-publish abort is a runtime behaviour of publish, and the abort
// ends the process that runs it.  So the case runs in a child process
// through expect_dies of the Check family, which requires SIGABRT and the
// name of the catalog entry on the standard error of the child.

void double_publish_aborts(::fixy::TestRunnerCtx const& test) {
    ::fixy::check::expect_dies(
        test,
        [] noexcept {
            Payload first{1, 1.0};
            Payload second{2, 2.0};
            h::PublishOnce<Payload> slot{};
            slot.publish(&first);
            slot.publish(&second);
        },
        ::fixy::TextView{"crucible: fatal: PublishOnceDoublePublish"});

    // One publish must NOT abort, or the case above would pass for a
    // handle that aborts on every call.  It runs in the test process.
    Payload only{1, 1.0};
    h::PublishOnce<Payload> slot{};
    slot.publish(&only);
    CRUCIBLE_TEST_ASSERT(test, slot.observe() == &only);
    ::fixy::check::pass(test, "  a second publish aborts, and one publish does not\n");
}

// ── Runtime: the preconditions no constant evaluation reaches ────────
//
// publish and Lazy::get read atomics, so neither is constexpr, and a
// negative-compile fixture cannot reach their preconditions.  Each
// refusal is observed in a child process through expect_dies instead.  The
// runs above are the positive controls: a non-null publish and a get after
// the value is built do not abort.
void preconditions_abort(::fixy::TestRunnerCtx const& test) {
    constexpr ::fixy::TextView kNullPublish{"foundation: contract violation: ptr != nullptr"};
    constexpr ::fixy::TextView kNeverBuilt{"foundation: fatal invariant failed: initialized()"};
    ::fixy::check::expect_dies(
        test,
        [] noexcept {
            h::PublishOnce<Payload> slot{};
            slot.publish(nullptr);
        },
        kNullPublish);
    ::fixy::check::expect_dies(
        test,
        [] noexcept {
            h::PublishSlot<Payload> slot{};
            slot.publish(nullptr);
        },
        kNullPublish);
    ::fixy::check::expect_dies(
        test,
        [] noexcept {
            h::Lazy<int> lazy{};
            (void)lazy.get();
        },
        kNeverBuilt);
    ::fixy::check::expect_dies(
        test,
        [] noexcept {
            const h::Lazy<int> lazy{};
            (void)lazy.get();
        },
        kNeverBuilt);
    ::fixy::check::pass(test, "  each precondition that no constant evaluation reaches aborts\n");
}

}  // namespace

int main() {
    if (const int rc = publish_once_runs(); rc != 0) return rc;
    if (const int rc = publish_slot_runs(); rc != 0) return rc;
    if (const int rc = once_runs_exactly_once(); rc != 0) return rc;
    if (const int rc = lazy_initializes_once_single_threaded(); rc != 0) return rc;
    if (const int rc = lazy_initializes_once(); rc != 0) return rc;
    ::fixy::TestRunnerCtx const test{::foundation::effects::testing::test()};
    double_publish_aborts(test);
    preconditions_abort(test);
    return 0;
}
