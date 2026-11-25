// The observation sink (fixy/os/TimingSink.h): a store of counts of the
// counter of the core.  A body writes it with mark and span, with no context,
// and only a context whose row holds Nondet reads it.
//
// A kernel, whose row is empty and holds no Nondet, marks a slot and times a
// span through the sink.  The test context then reads the two slots with its
// tag.  A mark from a thread that is not the owner ends the process.

#include <fixy/Core.h>
#include <fixy/Ctx.h>
#include <fixy/check/Check.h>
#include <fixy/os/ThreadTasks.h>
#include <fixy/os/TimingSink.h>
#include <foundation/core/FpMode.h>
#include <foundation/effects/Capability.h>
#include <foundation/os/FpControl.h>

#include "../thread_bound_probe.h"

#include <cstdint>
#include <type_traits>

namespace {

namespace core = ::foundation::core;
namespace eff = ::foundation::effects;
namespace os = ::foundation::os;
namespace observe = ::fixy::observe;

using ::fixy::TestRunnerCtx;
using ::fixy::check::expect_dies;

struct KernelBrand {};

using Sink = observe::TimingSink<2>;
using KernelCall = ::fixy::KernelCall<KernelBrand, core::kFpModeIeee>;

constexpr Sink::Slot kMarkSlot = ::fixy::index<0>();
constexpr Sink::Slot kSpanSlot = ::fixy::index<1>();

// The write side takes no context and gives back no count.  The read takes
// the bare tag of Nondet.
static_assert(std::is_void_v<decltype(std::declval<Sink&>().mark(kMarkSlot))>);
static_assert(!std::is_invocable_v<decltype(&Sink::read), Sink const&, Sink::Slot>);
static_assert(std::is_invocable_v<decltype(&Sink::read), Sink const&, eff::cap::Nondet, Sink::Slot>);
static_assert(!std::is_copy_constructible_v<Sink> && !std::is_move_constructible_v<Sink>);
static_assert(!std::is_copy_constructible_v<Sink::Span> && !std::is_move_constructible_v<Sink::Span>);

// The sink belongs to its owner thread: no other thread writes it through a
// reference that is not const, and each thread reads it through a reference
// to const (foundation/Transfer.h).
static_assert(!::foundation::transfer::Sendable<Sink> && !::foundation::transfer::Synchronized<Sink>);
static_assert(::foundation::transfer::Shareable<Sink> && ::foundation::transfer::Sendable<Sink const&>);
static_assert(!::foundation::transfer::Sendable<Sink&>);

// A kernel times itself.  It marks one slot and holds a span over a short
// loop of the other.  It reads no count back.
template <class Brand>
void timed_kernel(KernelCall const&, Sink& sink, ::fixy::ViewMut<std::uint64_t, Brand, 4> out) noexcept {
    sink.mark(kMarkSlot);
    Sink::Span const span = sink.span(kSpanSlot);
    std::uint64_t value = 1;
    for (std::uint64_t& slot : out) {
        value = value * 3 + 1;
        slot = value;
    }
}

void test_a_kernel_writes_and_a_test_reads(TestRunnerCtx const& test) {
    Sink sink = observe::mint_timing_sink<2>();
    eff::cap::Nondet const nondet = eff::extract_bare_from_ctx<eff::Effect::Nondet>(test);
    CRUCIBLE_TEST_ASSERT(test, sink.read(nondet, kMarkSlot) == 0);
    CRUCIBLE_TEST_ASSERT(test, sink.read(nondet, kSpanSlot) == 0);
    os::FpScope<core::kFpModeIeee> const scope = os::mint_fp_scope<core::kFpModeIeee>();
    ::fixy::Kernel<KernelBrand, core::kFpModeIeee> const ctx = ::fixy::mint_kernel_context<KernelBrand>(scope);
    auto [out, out_key] = ::fixy::mint_keyed_owner(::fixy::fixed_array_owner<std::uint64_t, 4>);
    out.with_mut(out_key, [&ctx, &sink](auto run) noexcept {
        ::fixy::run_kernel(ctx, timed_kernel<typename decltype(run)::brand_type>, sink, std::move(run));
    });
    CRUCIBLE_TEST_ASSERT(test, sink.read(nondet, kMarkSlot) != 0);
    CRUCIBLE_TEST_ASSERT(test, out.at(::fixy::index<3>()) == 121u);
    std::uint64_t const first_span = sink.read(nondet, kSpanSlot);
    out.with_mut(out_key, [&ctx, &sink](auto run) noexcept {
        ::fixy::run_kernel(ctx, timed_kernel<typename decltype(run)::brand_type>, sink, std::move(run));
    });
    CRUCIBLE_TEST_ASSERT(test, sink.read(nondet, kSpanSlot) >= first_span);
    ::fixy::check::pass(test, "a kernel marks and spans a sink, and a test context reads it with its tag\n");
}

// A mark from a thread that is not the owner of the sink ends the process.
// The thread doors refuse a body that captures the sink, so the probe gives
// the other thread the address of the sink as an integer
// (test/thread_bound_probe.h).
void a_mark_from_another_thread() {
    Sink sink = observe::mint_timing_sink<2>();
    ::fixy::BgLoadCtx const bg{eff::testing::bg()};
    ::fixy::Atomic<std::uint64_t> address{::crucible::test::address_of_thread_bound(sink)};
    auto body = [&address]() noexcept {
        ::crucible::test::thread_bound_at<Sink>(address.load_acquire()).mark(kMarkSlot);
    };
    static_cast<void>(::fixy::thread::run_joined(bg, ::fixy::thread::ThreadOptions{}, std::move(body)));
}

void test_only_the_owner_writes(TestRunnerCtx const& test) {
    expect_dies(test, a_mark_from_another_thread, ::fixy::TextView{"fatal invariant failed: owner_ == "});
    ::fixy::check::pass(test, "only the owner thread of a sink writes it\n");
}

// A reader on another thread loads a count while the owner marks the slot.  A
// plain count would be a data race, which ThreadSanitizer reports.  The probe
// gives the other thread the address of the sink as an integer.
void test_a_reader_runs_beside_the_owner(TestRunnerCtx const& test) {
    constexpr int kTurns = 1000;
    Sink sink = observe::mint_timing_sink<2>();
    ::fixy::BgLoadCtx const bg{eff::testing::bg()};
    ::fixy::Atomic<std::uint64_t> address{::crucible::test::address_of_thread_bound(sink)};
    ::fixy::Atomic<std::uint64_t> last_seen{0};
    auto reader = [&address, &last_seen]() noexcept {
        Sink const& read = ::crucible::test::thread_bound_at<Sink>(address.load_acquire());
        for (int turn = 0; turn < kTurns; ++turn) {
            last_seen.store_release(read.read(eff::testing::test().nondet, kMarkSlot));
        }
    };
    ::fixy::thread::run_beside(bg, ::fixy::thread::ThreadOptions{}, std::move(reader), [&sink]() noexcept {
        for (int turn = 0; turn < kTurns; ++turn) {
            sink.mark(kMarkSlot);
        }
    }).expect("the door starts the reader with the default options");
    std::uint64_t const at_the_end = sink.read(eff::extract_bare_from_ctx<eff::Effect::Nondet>(test), kMarkSlot);
    static_cast<void>(last_seen.load_acquire());
    CRUCIBLE_TEST_ASSERT(test, at_the_end != 0);
    ::fixy::check::pass(test, "a reader on another thread loads a count while the owner marks it\n");
}

}  // namespace

int main() {
    TestRunnerCtx const test{eff::testing::test()};
    test_a_kernel_writes_and_a_test_reads(test);
    test_only_the_owner_writes(test);
    test_a_reader_runs_beside_the_owner(test);
    return 0;
}
