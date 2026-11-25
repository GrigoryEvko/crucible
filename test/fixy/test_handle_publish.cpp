// What fixy/handle/PublishOnce.h and fixy/handle/Once.h claim, checked.
//
// Five handles, one question each.  PublishOnce takes exactly one
// publisher ever.  BrandedPublishOnce takes one too, and only a Ref of its
// brand.  PublishSlot takes one again and again and lets a
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
#include <cstddef>
#include <cstdio>
#include <thread>
#include <type_traits>
#include <utility>
#include <vector>

namespace h = fixy::handle;

namespace {

// The context of each Once::call and Lazy::get_or_init.  A loser spins until
// the winner ends, with no exit, so the context owns Div.
constexpr ::fixy::TestRunnerCtx kSpin{::foundation::effects::testing::test()};

template <typename Ctx>
concept CallsOnceIn = requires(h::Once& once, Ctx const& ctx) { once.call(ctx, []() noexcept {}); };
static_assert(CallsOnceIn<::fixy::TestRunnerCtx> && CallsOnceIn<::fixy::InitLoadCtx>);
static_assert(!CallsOnceIn<::fixy::HotFgCtx>, "the hot path does not spin with no exit");
static_assert(!CallsOnceIn<::fixy::ColdInitCtx>, "a context with no Div does not spin with no exit");

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

// ── The publish of a Ref ─────────────────────────────────────────────
//
// Each publish takes a Ref, which is never null.  So no publish stores the
// null pointer of the empty slot, and the publish needs no test.  A raw
// pointer, the null pointer and a Ref of an unrelated type do not reach
// publish.
template <typename Slot, typename Argument>
concept Publishes = requires(Slot& slot, Argument argument) { slot.publish(argument); };

struct Other {
    int c = 0;
};

// The brand of the Refs of the cells.  A publish takes a Ref of each brand.
struct CellBrand final {};

using PayloadRef = ::fixy::Ref<Payload, CellBrand>;
static_assert(Publishes<h::PublishOnce<Payload>, PayloadRef>);
static_assert(Publishes<h::PublishSlot<Payload>, PayloadRef>);
static_assert(Publishes<h::PublishOnce<Payload const>, PayloadRef>, "a slot of a const target takes a Ref of T");
static_assert(Publishes<h::PublishOnce<void>, PayloadRef>, "a slot of void takes a Ref of each object");
static_assert(!Publishes<h::PublishOnce<Payload>, ::fixy::Ref<Payload const, CellBrand>>,
              "a publish never drops a const");
static_assert(!Publishes<h::PublishOnce<Payload>, ::fixy::Ref<Other, CellBrand>>);
static_assert(!Publishes<h::PublishOnce<Payload>, Payload*>, "a raw pointer can be null");
static_assert(!Publishes<h::PublishSlot<Payload>, Payload*>, "a raw pointer can be null");
static_assert(!Publishes<h::PublishOnce<Payload>, decltype(nullptr)>);
static_assert(!Publishes<h::PublishSlot<Payload>, decltype(nullptr)>);

// ── The publish of a Ref of one brand ────────────────────────────────
//
// A BrandedPublishOnce takes a Ref of its brand and of its target or of a
// target with less const.  A Ref of another brand, an erased Ref and a raw
// pointer do not reach publish.  A slot names a brand, so the erased brand
// gives no slot, and void is no target.  An observer gets the Ref, and no raw
// pointer.
struct[[= ::foundation::brand::arena_family_brand{}]] CellArena final {};
using ArenaCell = h::BrandedPublishOnce<Payload const, CellArena>;
using ErasedRef = decltype(::fixy::erase_brand(std::declval<PayloadRef>()));
static_assert(Publishes<ArenaCell, ::fixy::Ref<Payload const, CellArena>>);
static_assert(Publishes<ArenaCell, ::fixy::Ref<Payload, CellArena>>, "a slot of a const target takes a Ref of T");
static_assert(!Publishes<ArenaCell, PayloadRef>, "a Ref of another brand");
static_assert(!Publishes<ArenaCell, ErasedRef>, "a Ref that names no owner");
static_assert(!Publishes<ArenaCell, ::fixy::Ref<Other const, CellArena>>);
static_assert(!Publishes<ArenaCell, Payload const*>, "a raw pointer can be null");
static_assert(!Publishes<ArenaCell, decltype(nullptr)>);
static_assert(!Publishes<h::BrandedPublishOnce<Payload, CellArena>, ::fixy::Ref<Payload const, CellArena>>,
              "a publish never drops a const");
template <typename T, typename Owner>
concept CanNameOwner = requires { typename h::BrandedPublishOnce<T, Owner>; };
static_assert(CanNameOwner<Payload, CellArena> && !CanNameOwner<Payload, ::foundation::brand::DefaultBrand>);
static_assert(!CanNameOwner<void, CellArena> && !CanNameOwner<Payload*, CellArena>);
static_assert(std::is_same_v<decltype(std::declval<ArenaCell const&>().observe()),
                             ::fixy::Option<::fixy::Ref<Payload const, CellArena>>>);

// ── Shape ────────────────────────────────────────────────────────────

// PublishOnce keeps the natural alignment of an atomic pointer.  It is
// written once per instance, so its line is invalidated once; dense
// structures hold thousands of them under a size-locked layout that
// padding would break.
static_assert(sizeof(h::PublishOnce<Payload>) == sizeof(std::atomic<Payload*>));
static_assert(alignof(h::PublishOnce<Payload>) == alignof(std::atomic<Payload*>));
static_assert(sizeof(ArenaCell) == sizeof(h::PublishOnce<Payload>));
static_assert(alignof(ArenaCell) == alignof(h::PublishOnce<Payload>));
static_assert(std::is_trivially_destructible_v<ArenaCell>, "an arena ends no slot");

// PublishSlot pads to a whole line instead, because it is written again
// and again and must not share one with anything.
static_assert(sizeof(h::PublishSlot<Payload>) >= 64);
static_assert(alignof(h::PublishSlot<Payload>) >= 64);
static_assert(alignof(h::Once) >= 64);

// Every one of these is a channel identity: an address other threads
// reach, or a slot whose duplication would split ownership.
static_assert(!std::is_copy_constructible_v<h::PublishOnce<Payload>>);
static_assert(!std::is_move_constructible_v<h::PublishOnce<Payload>>);
static_assert(!std::is_copy_constructible_v<ArenaCell> && !std::is_move_constructible_v<ArenaCell>);
static_assert(!std::is_copy_assignable_v<ArenaCell> && !std::is_move_assignable_v<ArenaCell>);
static_assert(!std::is_copy_constructible_v<h::PublishSlot<Payload>>);
static_assert(!std::is_move_constructible_v<h::PublishSlot<Payload>>);
static_assert(!std::is_copy_constructible_v<h::Once>);
static_assert(!std::is_move_constructible_v<h::Once>);
static_assert(!std::is_copy_constructible_v<h::Lazy<int>>);

// ── Runtime: the three publication slots ─────────────────────────────

// The block of each arena of the cells.
constexpr ::fixy::Quantity<::fixy::unit::Byte, std::size_t> kCellBlock{std::size_t{256}};

void branded_publish_once_runs(::fixy::TestRunnerCtx const& test) {
    ::foundation::effects::Alloc const alloc = ::foundation::effects::testing::test().alloc;
    ::fixy::BumpArena<CellArena> arena = ::fixy::mint_bump_arena<CellArena>(kCellBlock);
    auto const value = arena.make<Payload>(alloc, 7, 2.5);
    ArenaCell slot{};
    CRUCIBLE_TEST_ASSERT(test, !slot.is_published() && slot.observe().is_none());
    slot.publish(value);
    CRUCIBLE_TEST_ASSERT(test, slot.is_published());
    ::fixy::Ref<Payload const, CellArena> const seen = slot.observe().expect("the slot holds the published object");
    CRUCIBLE_TEST_ASSERT(test, ::fixy::same_object(seen, value) && seen->a == 7);
    ::fixy::check::pass(test, "  a branded slot gives the published object\n");
}

[[nodiscard]] int publish_once_runs() {
    Payload value{7, 2.5};
    h::PublishOnce<Payload> slot{};

    if (slot.is_published() || slot.observe() != nullptr) {
        std::fprintf(stderr, "a fresh PublishOnce read as published\n");
        return 1;
    }

    slot.publish(::fixy::to(value));
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

    slot.publish(::fixy::to(first));
    if (!slot.has_pending() || slot.observe() != &first) {
        std::fprintf(stderr, "PublishSlot did not take the first publish\n");
        return 1;
    }

    // Unlike PublishOnce, republishing is the point: the slot is a
    // handoff point, not a one-shot claim.
    slot.publish(::fixy::to(second));
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
                gate.call(kSpin, [&bodies]() noexcept { bodies.fetch_add(1, std::memory_order_acq_rel); });
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

    // A fresh Lazy is not built, and a read of it gives no value, from a
    // mutable Lazy and from a const one.
    const h::Lazy<int>& const_view = lazy;
    if (lazy.initialized() || lazy.get().is_some() || const_view.get().is_some()) {
        std::fprintf(stderr, "a fresh Lazy read as initialized\n");
        return 1;
    }

    int& first = *lazy.get_or_init(kSpin, [&]() noexcept {
        ++invocations;
        return seed + 1;
    });
    if (first != seed + 1 || invocations != 1 || !lazy.initialized()) {
        std::fprintf(stderr, "the first get_or_init did not initialize once\n");
        return 1;
    }

    // The second initializer must never run, and the reference must be
    // the same object rather than a second one holding the same value.
    int& second = *lazy.get_or_init(kSpin, [&]() noexcept {
        ++invocations;
        return seed + 99999;
    });
    if (invocations != 1 || &second != &first || second != seed + 1) {
        std::fprintf(stderr, "the second get_or_init re-ran the initializer\n");
        return 1;
    }

    int& third = *lazy.get().expect("the build made the value");
    if (&third != &first || third != seed + 1) {
        std::fprintf(stderr, "get() returned a different object than get_or_init\n");
        return 1;
    }

    const int& fourth = *const_view.get().expect("the build made the value");
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
                const int& value = *lazy.get_or_init(kSpin, [&initializers]() noexcept {
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
    if (!lazy.initialized() || *lazy.get().expect("the racers built the value") != 4242) {
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
            slot.publish(::fixy::to(first));
            slot.publish(::fixy::to(second));
        },
        ::fixy::TextView{"crucible: fatal: PublishOnceDoublePublish"});
    ::fixy::check::expect_dies(
        test,
        [] noexcept {
            ::foundation::effects::Alloc const alloc = ::foundation::effects::testing::test().alloc;
            ::fixy::BumpArena<CellArena> arena = ::fixy::mint_bump_arena<CellArena>(kCellBlock);
            ArenaCell slot{};
            slot.publish(arena.make<Payload>(alloc, 1, 1.0));
            slot.publish(arena.make<Payload>(alloc, 2, 2.0));
        },
        ::fixy::TextView{"crucible: fatal: PublishOnceDoublePublish"});

    // One publish must NOT abort, or the case above would pass for a
    // handle that aborts on every call.  It runs in the test process.
    Payload only{1, 1.0};
    h::PublishOnce<Payload> slot{};
    slot.publish(::fixy::to(only));
    CRUCIBLE_TEST_ASSERT(test, slot.observe() == &only);
    ::fixy::check::pass(test, "  a second publish aborts, and one publish does not\n");
}

}  // namespace

int main() {
    if (const int rc = publish_once_runs(); rc != 0) return rc;
    if (const int rc = publish_slot_runs(); rc != 0) return rc;
    if (const int rc = once_runs_exactly_once(); rc != 0) return rc;
    if (const int rc = lazy_initializes_once_single_threaded(); rc != 0) return rc;
    if (const int rc = lazy_initializes_once(); rc != 0) return rc;
    ::fixy::TestRunnerCtx const test{::foundation::effects::testing::test()};
    branded_publish_once_runs(test);
    double_publish_aborts(test);
    return 0;
}
