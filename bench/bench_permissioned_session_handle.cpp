// The permission set of a session handle is a type, not a member.
//
// fixy/session/Handle.h has one handle type, and the permission set is one
// of its template arguments.  A handle whose set holds a permission must
// cost the same for each operation as a handle whose set is empty.
//
// Two tiers of evidence:
//
//   Structural (the claim): the static_assert below.  The handle with a
//   permission has the size of the handle with the empty set, so the set
//   takes no storage.  The set moves at compile time only.
//
//   Timed (informational): four pairs on one protocol and one Resource.  At
//   the sub-nanosecond scale the layout of the two measured bodies can move
//   the numbers by a cycle, so the program exits 0 and the numbers are for
//   inspection.
//
// The four pairs are four forms of a caller (CallerForm tells each one).  In
// the local form, the body steps a handle that is local to its loop, as a
// real caller does.  In the form of the caller memory, the timed loop reaches
// the handle through the closure, as a loop reaches a handle that is a member
// of an object.  A step can call the cold note of a holder change, and the
// second form pays for that call.  The pinned form pins the handle of the
// caller memory for each body, and the fourth form measures only the pin and
// its turn back.  Each send takes its item through an input sink and writes
// it through an output sink, so the optimizer cannot fold the sends of a body
// into one.
//
// Single-threaded.  Loop<Send<Item, Continue>> lands each send on the loop
// head again, so the measured body can assign the next handle back to the
// same variable.  The loop has no exit branch, so each run ends with a typed
// detach.
//
// The section on the speed of light gives each form in cycles of the core,
// beside a floor row that does the work of the items with no handle, and two
// chain rows that compare a protocol with no loop and a loop.

#include <fixy/session/Entry.h>
#include <fixy/session/Handle.h>
#include <fixy/session/Mint.h>

#include <foundation/effects/Ctx.h>
#include <foundation/permissions/Permission.h>

#include "bench_harness.h"

#include <cstdint>
#include <type_traits>
#include <utility>

namespace {

namespace s = ::fixy::session;
namespace perm = ::foundation::permissions;

using Item = std::uint64_t;

// A value Resource, so each handle owns its own copy and a send writes
// handle-local state.
struct FakeChannel {
    Item last = 0;
};

// The region whose permission the second handle of each pair holds.
struct BenchRegion {
    using permission_row = ::foundation::effects::Row<>;
};

constexpr auto kForeground = ::foundation::effects::testing::foreground();

using SendLoop = s::Loop<s::Send<Item, s::Continue>>;

using EmptySetLoopHandle = decltype(s::mint_session<SendLoop>(kForeground, std::declval<FakeChannel>()));
using PermittedLoopHandle = decltype(s::mint_permissioned_session<SendLoop>(kForeground, std::declval<FakeChannel>(),
                                                                            perm::mint_permission_root<BenchRegion>()));

static_assert(sizeof(PermittedLoopHandle) == sizeof(EmptySetLoopHandle),
              "a permission set must take no storage in the handle");

// An input sink: the asm sink of the harness holds the item in a register
// and gives it back changed.  So the optimizer cannot know the item of a
// send.  The sink emits no instruction and has no memory clobber, so the
// other values of the loop stay in registers.
[[gnu::always_inline]] inline void make_item_opaque(Item& item) noexcept { bench::do_not_optimize(item); }

// An output sink: the sink of an object in memory reads and writes a copy of
// the item in memory.  So each send stores its item, as a send into a channel
// that another thread reads does, and the optimizer cannot move the stores
// out of the loop.  The sink has no memory clobber.  But with it in the loop,
// GCC 16 keeps the handle of the local and the pinned forms in memory (the
// disassembly of this bench shows it), so the count of the steps makes a
// chain of a load and a store in each send.  Each form pays that chain, so the
// forms still compare.
[[gnu::always_inline]] inline void keep_item_written(Item copy) noexcept { bench::mark_changed(copy); }

// The trying write: it always has room, and the output sink keeps each write.
constexpr auto send_item = [](FakeChannel& channel, Item& value) noexcept {
    channel.last = value;
    keep_item_written(channel.last);
    return true;
};

// The sends of one body of the local and the pinned forms.  The moves of the
// body and the pin then cost a small part of each send.
constexpr std::uint32_t kSendsPerBody = 64;

// The four forms of a caller of the timed loop.
enum class CallerForm : std::uint8_t {
    // A handle that is local to the loop, as a real caller keeps its handle
    // on its own stack.  Each body moves the handle into a local, sends
    // kSendsPerBody items, and gives it back.  No call can reach the local,
    // so the cold note of a holder change (the check of
    // fixy/session/Stepping.h) does not make the handle go through memory.
    // The output sink does, as keep_item_written tells.
    LocalHandle,
    // A handle in the memory of the caller: the timed loop reaches the handle
    // and the item through the closure, and each body sends one item.  The
    // step can call the cold note of a holder change, which writes the watch
    // with a release store.  The compiler takes that call as a write of each
    // value that the caller can reach through a pointer, so the loop reloads
    // the closure and keeps the handle in memory.
    CallerMemory,
    // A handle in the memory of the caller that each body pins
    // (HandleFactory::pin of fixy/session/Handle.h): the pin makes one
    // holder check, the body sends kSendsPerBody items through the pinned
    // handle, and the pin gives it back.  A step under the pinned policy
    // reads no slot of the thread and calls no note.
    PinnedHandle,
    // The pin of the pinned form with a body that sends nothing: the row
    // gives the cost of one pin and its turn back, which each body of the
    // pinned form pays one time.
    PinAlone,
};

// The sends of one body of a form.
[[nodiscard]] constexpr std::uint32_t sends_of(CallerForm form) noexcept {
    switch (form) {
        case CallerForm::LocalHandle:
        case CallerForm::PinnedHandle:
            return kSendsPerBody;
        case CallerForm::CallerMemory:
            return 1;
        case CallerForm::PinAlone:
            return 0;
        default:
            ::fixy::unreachable();
    }
}

// One body of the timed loop: it takes the handle of the caller, and it
// gives the handle that the caller keeps.  The local form builds the result
// from the local at the end.  A return of the local itself would make the
// local the object of the result, which the caller reaches through a pointer,
// and the cold note would then keep it in memory.  In the loop of sends, the
// output sink keeps the handle in memory in each form (keep_item_written).
template <CallerForm Form, typename Handle>
[[nodiscard]] Handle send_body(Handle&& handle, Item& item) noexcept {
    if constexpr (Form == CallerForm::LocalHandle) {
        Handle local = std::move(handle);
        Item value = item;
        for (std::uint32_t sent = 0; sent < kSendsPerBody; ++sent) {
            ++value;
            make_item_opaque(value);
            local = std::move(local).send(value, send_item);
        }
        item = value;
        return Handle{std::move(local)};
    } else if constexpr (Form == CallerForm::PinnedHandle) {
        return s::HandleFactory::pin(kForeground, std::move(handle), [&item](auto pinned) noexcept {
            Item value = item;
            for (std::uint32_t sent = 0; sent < kSendsPerBody; ++sent) {
                ++value;
                make_item_opaque(value);
                pinned = std::move(pinned).send(value, send_item);
            }
            item = value;
            return pinned;
        });
    } else if constexpr (Form == CallerForm::PinAlone) {
        return s::HandleFactory::pin(kForeground, std::move(handle), [](auto pinned) noexcept { return pinned; });
    } else {
        Item value = item + 1;
        make_item_opaque(value);
        item = value;
        return std::move(handle).send(value, send_item);
    }
}

// Sends Item after Item in one form, and detaches the handle at the end.
template <CallerForm Form, typename Handle>
[[nodiscard]] bench::Report send_loop(::fixy::TextView name, Handle handle) {
    Item item = 0;
    auto report = bench::run(name, [&] { handle = send_body<Form>(std::move(handle), item); });
    std::move(handle).detach(s::detach_reason::TestInstrumentation{});
    return report;
}

[[nodiscard]] bench::Report empty_set_send_local() {
    return send_loop<CallerForm::LocalHandle>("send, empty permission set, local handle (64 sends a body)",
                                              s::mint_session<SendLoop>(kForeground, FakeChannel{}));
}

[[nodiscard]] bench::Report permitted_send_local() {
    return send_loop<CallerForm::LocalHandle>(
        "send, one permission in the set, local handle (64 sends a body)",
        s::mint_permissioned_session<SendLoop>(kForeground, FakeChannel{}, perm::mint_permission_root<BenchRegion>()));
}

[[nodiscard]] bench::Report empty_set_send_in_caller_memory() {
    return send_loop<CallerForm::CallerMemory>("send, empty permission set, handle in caller memory",
                                               s::mint_session<SendLoop>(kForeground, FakeChannel{}));
}

[[nodiscard]] bench::Report permitted_send_in_caller_memory() {
    return send_loop<CallerForm::CallerMemory>(
        "send, one permission in the set, handle in caller memory",
        s::mint_permissioned_session<SendLoop>(kForeground, FakeChannel{}, perm::mint_permission_root<BenchRegion>()));
}

[[nodiscard]] bench::Report empty_set_send_pinned() {
    return send_loop<CallerForm::PinnedHandle>("send, empty permission set, pinned handle (64 sends a body)",
                                               s::mint_session<SendLoop>(kForeground, FakeChannel{}));
}

[[nodiscard]] bench::Report permitted_send_pinned() {
    return send_loop<CallerForm::PinnedHandle>(
        "send, one permission in the set, pinned handle (64 sends a body)",
        s::mint_permissioned_session<SendLoop>(kForeground, FakeChannel{}, perm::mint_permission_root<BenchRegion>()));
}

[[nodiscard]] bench::Report empty_set_pin_alone() {
    return send_loop<CallerForm::PinAlone>("pin and turn back, empty permission set, no send",
                                           s::mint_session<SendLoop>(kForeground, FakeChannel{}));
}

[[nodiscard]] bench::Report permitted_pin_alone() {
    return send_loop<CallerForm::PinAlone>(
        "pin and turn back, one permission in the set, no send",
        s::mint_permissioned_session<SendLoop>(kForeground, FakeChannel{}, perm::mint_permission_root<BenchRegion>()));
}

// Prints the time of one send of a row: the time of a body over the sends of
// one body of its form.
void print_one_send(bench::Report const& report, CallerForm form) {
    CRUCIBLE_PRE(sends_of(form) > 0);
    const double sends = sends_of(form);
    bench::print("  {}  one send: p50 {} ns  p99 {} ns  max {} ns\n", report.name.view(),
                 ::fixy::Fixed<3>{report.pct.p50 / sends}, ::fixy::Fixed<3>{report.pct.p99 / sends},
                 ::fixy::Fixed<3>{report.pct.max / sends});
}

// ── The speed of light ───────────────────────────────────────────────
//
// THE FLOOR ROWS.  The floor of a send is the work of its item with no
// handle: the add of the item, the input sink, and the write of the item
// into the channel through the output sink.  The local floor runs that work
// kSendsPerBody times on a channel that is local to the body, as the local
// and the pinned forms do.  The floor of the caller memory writes one item
// into the channel of the caller, through the closure, as that form does.
// The gap of a row to its floor is the cost of the handle: the liveness
// flag, the holder check, the count of the steps and the moves.
//
// THE MODEL.  A send of the local floor is one add, one store of the output
// sink and the branch of the loop: three macro-ops, below the dispatch of six
// a cycle.  The core takes one taken branch a cycle, so the model floor of a
// send of a local form is one cycle (the Software Optimization Guide for the
// AMD Zen4 Microarchitecture, AMD 57647 revision 1.00, sections 2.8.1.2 and
// 2.9.8).  The floor of the caller memory adds the load of the item and the
// store of the item of the caller.
//
// THE CLOCK.  kClockAdds dependent adds take one cycle each, so their time
// gives the cycles of one nanosecond of the core in this run.  The core can
// run at a clock that is not the clock of the TSC.
//
// THE CHAIN ROWS.  A body mints a session, sends kChainSends items and ends
// it.  The finite chain is a protocol of kChainSends sends and End, so no
// handle of it keeps a count of its steps.  The looped chain sends the same
// items through Loop<Send<Item, Continue>> and detaches, so each of its
// handles keeps the count.  The two pay one claim and one release of a
// record of the watch.

constexpr std::uint32_t kClockAdds = 64;
constexpr std::size_t kClockBatch = 256;
constexpr std::uint32_t kChainSends = 16;
constexpr double kLocalFloorCycles = 1.0;

CRUCIBLE_I_KNOW_WHAT_IM_DOING("MEASURE: a chain of dependent adds gives the clock of the core")
// kClockAdds dependent adds to the value in one block of code, which the
// compiler cannot fold.
[[gnu::always_inline]] inline void add_chain(std::uint64_t& value) noexcept {
#if defined(__x86_64__)
    asm volatile(".rept %c1\n\tadd $1, %0\n\t.endr" : "+r"(value) : "i"(kClockAdds));
#else
    asm volatile(".rept %c1\n\tadd %0, %0, #1\n\t.endr" : "+r"(value) : "i"(kClockAdds));
#endif
}
CRUCIBLE_END_I_KNOW_WHAT_IM_DOING

// kClockAdds dependent adds: one cycle each on the core (THE CLOCK).  The chain
// runs on a local, so the value stays in a register.
struct ClockChain {
    std::uint64_t value = 0;

    [[gnu::always_inline]] void operator()() noexcept {
        std::uint64_t local = value;
        add_chain(local);
        value = local;
    }
};

using CoreClock = ::fixy::Quantity<::fixy::unit::Quotient<::fixy::unit::Cycle, ::fixy::unit::Nanosecond>, double>;

// The cycles of one nanosecond of the core in this run (THE CLOCK).
[[nodiscard]] CoreClock measure_clock() {
    ClockChain chain{};
    bench::Report const report = bench::Run("clock").batch(kClockBatch).measure(chain);
    bench::do_not_optimize(chain.value);
    return CoreClock{report.pct.p50 > 0.0 ? kClockAdds / report.pct.p50 : 0.0};
}

// The sends of one body of a floor row, with no handle (THE FLOOR ROWS).
template <CallerForm Form>
void floor_body(FakeChannel& channel, Item& item) noexcept {
    if constexpr (Form == CallerForm::LocalHandle) {
        FakeChannel local = channel;
        Item value = item;
        for (std::uint32_t sent = 0; sent < kSendsPerBody; ++sent) {
            ++value;
            make_item_opaque(value);
            static_cast<void>(send_item(local, value));
        }
        item = value;
        channel = local;
    } else {
        Item value = item + 1;
        make_item_opaque(value);
        item = value;
        static_cast<void>(send_item(channel, value));
    }
}

template <CallerForm Form>
[[nodiscard]] bench::Report floor_loop(::fixy::TextView name) {
    FakeChannel channel{};
    Item item = 0;
    auto report = bench::run(name, [&] { floor_body<Form>(channel, item); });
    bench::do_not_optimize(item);
    return report;
}

// The protocol of the finite chain: kChainSends sends and End (THE CHAIN ROWS).
template <std::uint32_t Count>
struct send_chain_of {
    using type = s::Send<Item, typename send_chain_of<Count - 1>::type>;
};

template <>
struct send_chain_of<0> {
    using type = s::End;
};

using SendChain = typename send_chain_of<kChainSends>::type;

// Sends one item for each Send of the handle, and gives the handle at End.
// The chain rows force each body inline, so the inliner of GCC cannot make a
// call of one form and inline the other: an outlined body of the finite chain
// cost two cycles a send more than the inline body.
template <typename Handle>
[[nodiscard, gnu::always_inline]] inline auto send_each(Handle handle, Item& value) noexcept {
    if constexpr (Handle::is_terminal()) {
        return handle;
    } else {
        ++value;
        make_item_opaque(value);
        return send_each(std::move(handle).send(value, send_item), value);
    }
}

[[nodiscard]] bench::Report finite_chain() {
    Item item = 0;
    auto report = bench::run("chain of 16 sends, finite protocol (mint, 16 sends, close)", [&] [[gnu::always_inline]] {
        auto at_end = send_each(s::mint_session<SendChain>(kForeground, FakeChannel{}), item);
        FakeChannel const channel = std::move(at_end).close();
        bench::do_not_optimize(channel.last);
    });
    return report;
}

[[nodiscard]] bench::Report looped_chain() {
    Item item = 0;
    auto report = bench::run("chain of 16 sends, looped protocol (mint, 16 sends, detach)", [&] [[gnu::always_inline]] {
        auto handle = s::mint_session<SendLoop>(kForeground, FakeChannel{});
        for (std::uint32_t sent = 0; sent < kChainSends; ++sent) {
            ++item;
            make_item_opaque(item);
            handle = std::move(handle).send(item, send_item);
        }
        std::move(handle).detach(s::detach_reason::TestInstrumentation{});
    });
    return report;
}

// Writes one row of the speed of light: the cycles of one send at the p50 and
// the p99, the cycles of one send of its floor row, and the gap to the floor
// row.  The floor row of a form sends as many items in a body as the form.
void print_light(bench::Report const& report, bench::Report const& floor, CallerForm form, CoreClock clock) {
    CRUCIBLE_PRE(sends_of(form) > 0);
    const double sends = sends_of(form);
    const double p50 = report.pct.p50 * clock.raw() / sends;
    const double floor_p50 = floor.pct.p50 * clock.raw() / sends;
    bench::print("  {}  one send: p50 {} cycles  p99 {} cycles  floor row {} cycles  gap {} cycles\n",
                 report.name.view(), ::fixy::Fixed<2>{p50}, ::fixy::Fixed<2>{report.pct.p99 * clock.raw() / sends},
                 ::fixy::Fixed<2>{floor_p50}, ::fixy::Fixed<2>{p50 - floor_p50});
}

// Writes one chain row in cycles of one send: the time of a body, which holds
// one mint and one end, over kChainSends.
void print_chain(bench::Report const& report, CoreClock clock) {
    const double sends = kChainSends;
    bench::print("  {}  one send: p50 {} cycles  p99 {} cycles\n", report.name.view(),
                 ::fixy::Fixed<2>{report.pct.p50 * clock.raw() / sends},
                 ::fixy::Fixed<2>{report.pct.p99 * clock.raw() / sends});
}

}  // namespace

int main() {
    ::fixy::FixedArray<bench::Report, 12> reports{empty_set_send_local(),
                                                  permitted_send_local(),
                                                  empty_set_send_in_caller_memory(),
                                                  permitted_send_in_caller_memory(),
                                                  empty_set_send_pinned(),
                                                  permitted_send_pinned(),
                                                  empty_set_pin_alone(),
                                                  permitted_pin_alone(),
                                                  floor_loop<CallerForm::LocalHandle>("floor, local (64 sends a body)"),
                                                  floor_loop<CallerForm::CallerMemory>("floor, caller memory"),
                                                  finite_chain(),
                                                  looped_chain()};
    const CoreClock clock = measure_clock();
    bench::emit_reports_text(reports.view());

    bench::print("\n=== the time of one send ===\n");
    print_one_send(reports.at(::fixy::index<0>()), CallerForm::LocalHandle);
    print_one_send(reports.at(::fixy::index<1>()), CallerForm::LocalHandle);
    print_one_send(reports.at(::fixy::index<2>()), CallerForm::CallerMemory);
    print_one_send(reports.at(::fixy::index<3>()), CallerForm::CallerMemory);
    print_one_send(reports.at(::fixy::index<4>()), CallerForm::PinnedHandle);
    print_one_send(reports.at(::fixy::index<5>()), CallerForm::PinnedHandle);

    bench::print("\n=== the speed of light: {} cycles a ns, the model floor of a local send {} cycle ===\n",
                 ::fixy::Fixed<3>{clock.raw()}, ::fixy::Fixed<1>{kLocalFloorCycles});
    bench::Report const& local_floor = reports.at(::fixy::index<8>());
    bench::Report const& caller_floor = reports.at(::fixy::index<9>());
    print_light(reports.at(::fixy::index<0>()), local_floor, CallerForm::LocalHandle, clock);
    print_light(reports.at(::fixy::index<1>()), local_floor, CallerForm::LocalHandle, clock);
    print_light(reports.at(::fixy::index<2>()), caller_floor, CallerForm::CallerMemory, clock);
    print_light(reports.at(::fixy::index<3>()), caller_floor, CallerForm::CallerMemory, clock);
    print_light(reports.at(::fixy::index<4>()), local_floor, CallerForm::PinnedHandle, clock);
    print_light(reports.at(::fixy::index<5>()), local_floor, CallerForm::PinnedHandle, clock);
    print_light(local_floor, local_floor, CallerForm::LocalHandle, clock);
    print_light(caller_floor, caller_floor, CallerForm::CallerMemory, clock);
    print_chain(reports.at(::fixy::index<10>()), clock);
    print_chain(reports.at(::fixy::index<11>()), clock);

    bench::print("\n=== permission set: pair delta ===\n");
    ::fixy::FixedArray<bench::Compare, 4> const compares{
        bench::compare(reports.at(::fixy::index<0>()), reports.at(::fixy::index<1>())),
        bench::compare(reports.at(::fixy::index<2>()), reports.at(::fixy::index<3>())),
        bench::compare(reports.at(::fixy::index<4>()), reports.at(::fixy::index<5>())),
        bench::compare(reports.at(::fixy::index<6>()), reports.at(::fixy::index<7>()))};
    bench::emit_compares(compares.view());

    bench::print("\n  The static_assert carries the claim.  The timed pairs are for inspection only.\n");
    bench::emit_reports_json(reports.view(), bench::env_json());
    return 0;
}
