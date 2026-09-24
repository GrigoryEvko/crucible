// What fixy/handle/PublishOnce.h and fixy/handle/Once.h claim, checked.
//
// Four handles, one question each.  PublishOnce takes exactly one
// publisher ever.  PublishSlot takes one again and again and lets a
// consumer take the pointer back.  SetOnce is the non-atomic slot that
// refuses a second set.  Once gates a body so it runs once across
// threads, and Lazy is a value built on that gate.
//
// Two of these claims are runtime-only.  PublishOnce's double-publish
// abort is deliberately NOT a contract clause — the header says why: a
// hot-path translation unit builds with the contract semantic set to
// ignore, where a clause elides and a second publish quietly no-ops
// while the first pointer stays visible.  A helper that aborts
// regardless of the semantic can only be observed by running it, so
// that case runs in a child process and reads WIFSIGNALED.  Once's
// run-exactly-once needs threads for the same reason: a single-threaded
// call cannot distinguish the gate from an if.

#include <fixy/handle/Once.h>
#include <fixy/handle/PublishOnce.h>

#include <sys/wait.h>
#include <unistd.h>

#include <atomic>
#include <cstdio>
#include <cstdlib>
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
// The guard this concept replaced read `is_pointer_v<T*> || is_same_v<T,
// T>`, whose second disjunct is true for every T, so it admitted
// PublishOnce<int*> and enforced nothing.  These cells are the witness
// from outside the header that the replacement still discriminates.
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

static_assert(sizeof(h::SetOnce<Payload>) == sizeof(Payload*));

// Every one of these is a channel identity: an address other threads
// reach, or a slot whose duplication would split ownership.
static_assert(!std::is_copy_constructible_v<h::PublishOnce<Payload>>);
static_assert(!std::is_move_constructible_v<h::PublishOnce<Payload>>);
static_assert(!std::is_copy_constructible_v<h::PublishSlot<Payload>>);
static_assert(!std::is_move_constructible_v<h::PublishSlot<Payload>>);
static_assert(!std::is_copy_constructible_v<h::Once>);
static_assert(!std::is_move_constructible_v<h::Once>);
static_assert(!std::is_copy_constructible_v<h::Lazy<int>>);

// SetOnce is the exception: it is a plain pointer slot with no atomic
// and no cross-thread role, so it copies.
static_assert(std::is_copy_constructible_v<h::SetOnce<Payload>>);

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

[[nodiscard]] int set_once_runs() {
    Payload first{1, 1.0};
    Payload second{2, 2.0};
    h::SetOnce<Payload> slot{};

    if (slot.has_value() || static_cast<bool>(slot) || slot.get() != nullptr) {
        std::fprintf(stderr, "a fresh SetOnce held a pointer\n");
        return 1;
    }

    if (!slot.try_set(&first)) {
        std::fprintf(stderr, "try_set refused the first claim\n");
        return 1;
    }
    // A second claim is refused, and the first survives it.
    if (slot.try_set(&second)) {
        std::fprintf(stderr, "try_set accepted a second claim\n");
        return 1;
    }
    if (slot.get() != &first) {
        std::fprintf(stderr, "the refused claim overwrote the slot\n");
        return 1;
    }

    // Null is the unset state, so it can never be stored: a slot that
    // accepted it could not report it back.
    h::SetOnce<Payload> other{};
    if (other.try_set(nullptr)) {
        std::fprintf(stderr, "try_set stored a null\n");
        return 1;
    }
    if (other.has_value()) {
        std::fprintf(stderr, "the refused null left the slot occupied\n");
        return 1;
    }

    slot.reset();
    if (slot.has_value()) {
        std::fprintf(stderr, "reset left the slot occupied\n");
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

// The single-threaded path, which used to run as an inline smoke test
// compiled into every translation unit that included the header.  What
// it checks is behaviour under a contrived sequence of calls, not a
// property of Lazy as shipped, so it belongs here.
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

template <typename Body>
[[nodiscard]] bool child_aborted(Body body) {
    // SPAWN-PROCESS-OK: the child exists to die.  The double-publish abort is a
    // runtime behaviour of a destructor, and std::abort ends the process
    // that runs it, so the only way to observe it is from a parent.
    const pid_t pid = ::fork();  // SPAWN-PROCESS-OK: death test, see above
    if (pid < 0) {
        std::fprintf(stderr, "fork failed\n");
        std::_Exit(2);
    }
    if (pid == 0) {
        body();
        std::_Exit(0);
    }
    int status = 0;
    // SPAWN-PROCESS-OK: the wait belongs to the fork above; reading
    // WIFSIGNALED from the status is what makes the abort observable,
    // and is stricter than ctest's WILL_FAIL, which accepts any exit.
    if (::waitpid(pid, &status, 0) != pid) {  // SPAWN-PROCESS-OK: death test, see above
        std::fprintf(stderr, "waitpid failed\n");
        std::_Exit(2);
    }
    return WIFSIGNALED(status) != 0;
}

[[nodiscard]] int double_publish_aborts() {
    std::fprintf(stderr, "[expected] the PublishOnceDoublePublish banner below comes from the child process "
                         "that proves the second publish aborts\n");

    const bool aborted = child_aborted([] {
        Payload first{1, 1.0};
        Payload second{2, 2.0};
        h::PublishOnce<Payload> slot{};
        slot.publish(&first);
        slot.publish(&second);
    });
    if (!aborted) {
        std::fprintf(stderr, "a second publish did not abort — the slot silently kept one of the two pointers, and "
                             "observers cannot tell which\n");
        return 1;
    }

    // One publish must NOT abort, or the case above would pass for a
    // handle that aborts on every call.
    const bool single_aborted = child_aborted([] {
        Payload only{1, 1.0};
        h::PublishOnce<Payload> slot{};
        slot.publish(&only);
    });
    if (single_aborted) {
        std::fprintf(stderr, "a single publish aborted\n");
        return 1;
    }
    return 0;
}

}  // namespace

int main() {
    if (const int rc = publish_once_runs(); rc != 0) return rc;
    if (const int rc = publish_slot_runs(); rc != 0) return rc;
    if (const int rc = set_once_runs(); rc != 0) return rc;
    if (const int rc = once_runs_exactly_once(); rc != 0) return rc;
    if (const int rc = lazy_initializes_once_single_threaded(); rc != 0) return rc;
    if (const int rc = lazy_initializes_once(); rc != 0) return rc;
    if (const int rc = double_publish_aborts(); rc != 0) return rc;
    return 0;
}
