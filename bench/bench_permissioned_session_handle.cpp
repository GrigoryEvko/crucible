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
//   Timed (informational): two pairs on one protocol and one Resource.  At
//   the sub-nanosecond scale the layout of the two measured bodies can move
//   the numbers by a cycle, so the program exits 0 and the numbers are for
//   inspection.
//
// The two pairs are two forms of a caller.  In the local form, the body steps
// a handle that is local to its loop, as a real caller does.  In the form of
// the caller memory, the timed loop reaches the handle through the closure,
// as a loop reaches a handle that is a member of an object.  A step can call
// the cold note of a holder change, and the second form pays for that call
// (send_loop_in_caller_memory tells why).
//
// Single-threaded.  Loop<Send<Item, Continue>> lands each send on the loop
// head again, so the measured body can assign the next handle back to the
// same variable.  The loop has no exit branch, so each run ends with a typed
// detach.

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

// The trying write: it always has room.
constexpr auto send_item = [](FakeChannel& channel, Item& value) noexcept {
    channel.last = value;
    return true;
};

using SendLoop = s::Loop<s::Send<Item, s::Continue>>;

using EmptySetLoopHandle = decltype(s::mint_session<SendLoop>(kForeground, std::declval<FakeChannel>()));
using PermittedLoopHandle = decltype(s::mint_permissioned_session<SendLoop>(kForeground, std::declval<FakeChannel>(),
                                                                            perm::mint_permission_root<BenchRegion>()));

static_assert(sizeof(PermittedLoopHandle) == sizeof(EmptySetLoopHandle),
              "a permission set must take no storage in the handle");

// The sends of one body of the local form.  The two moves of the body then
// cost a small part of each send.
constexpr std::uint32_t kSendsPerBody = 64;

// The two forms of a caller of the timed loop.
enum class CallerForm : std::uint8_t {
    // A handle that is local to the loop, as a real caller keeps its handle
    // on its own stack.  Each body moves the handle into a local, sends
    // kSendsPerBody items, and gives it back.  No call can reach the local,
    // so the cold note of a holder change (the check of
    // fixy/session/Stepping.h) does not make the handle go through memory.
    LocalHandle,
    // A handle in the memory of the caller: the timed loop reaches the handle
    // and the item through the closure, and each body sends one item.  The
    // step can call the cold note of a holder change, which writes the watch
    // with a release store.  The compiler takes that call as a write of each
    // value that the caller can reach through a pointer, so the loop reloads
    // the closure and keeps the handle in memory.
    CallerMemory,
};

// One body of the timed loop: it takes the handle of the caller, and it
// gives the handle that the caller keeps.  The local form builds the result
// from the local at the end.  A return of the local itself would make the
// local the object of the result, which the caller reaches through a pointer,
// and the cold note would then keep it in memory.
template <CallerForm Form, typename Handle>
[[nodiscard]] Handle send_body(Handle&& handle, Item& item) noexcept {
    if constexpr (Form == CallerForm::LocalHandle) {
        Handle local = std::move(handle);
        Item value = item;
        for (std::uint32_t sent = 0; sent < kSendsPerBody; ++sent) {
            local = std::move(local).send(++value, send_item);
        }
        item = value;
        return Handle{std::move(local)};
    } else {
        return std::move(handle).send(++item, send_item);
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

// Prints the time of one send of a row of the local form: the time of a body
// over kSendsPerBody.
void print_one_send(bench::Report const& report) {
    constexpr double sends = kSendsPerBody;
    bench::print("  {}  one send: p50 {} ns  p99 {} ns  max {} ns\n", report.name.view(),
                 ::fixy::Fixed<3>{report.pct.p50 / sends}, ::fixy::Fixed<3>{report.pct.p99 / sends},
                 ::fixy::Fixed<3>{report.pct.max / sends});
}

}  // namespace

int main() {
    ::fixy::FixedArray<bench::Report, 4> reports{empty_set_send_local(), permitted_send_local(),
                                                 empty_set_send_in_caller_memory(), permitted_send_in_caller_memory()};
    bench::emit_reports_text(reports.view());

    bench::print("\n=== local form: the time of one send ===\n");
    print_one_send(reports.at(::fixy::index<0>()));
    print_one_send(reports.at(::fixy::index<1>()));

    bench::print("\n=== permission set: pair delta ===\n");
    ::fixy::FixedArray<bench::Compare, 2> const compares{
        bench::compare(reports.at(::fixy::index<0>()), reports.at(::fixy::index<1>())),
        bench::compare(reports.at(::fixy::index<2>()), reports.at(::fixy::index<3>()))};
    bench::emit_compares(compares.view());

    bench::print("\n  The static_assert carries the claim.  The timed pairs are for inspection only.\n");
    bench::emit_reports_json(reports.view(), bench::env_json());
    return 0;
}
