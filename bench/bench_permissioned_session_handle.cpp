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
//   Timed (informational): one pair on one protocol and one Resource.  At
//   the sub-nanosecond scale the layout of the two measured bodies can move
//   the numbers by a cycle, so the program exits 0 and the numbers are for
//   inspection.
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
#include <cstdio>
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

// Sends Item after Item through the handle, and detaches it at the end.
template <typename Handle>
[[nodiscard]] bench::Report send_loop(const char* name, Handle handle) {
    Item item = 0;
    auto report = bench::run(name, [&] { handle = std::move(handle).send(++item, send_item); });
    std::move(handle).detach(s::detach_reason::TestInstrumentation{});
    return report;
}

[[nodiscard]] bench::Report empty_set_send() {
    return send_loop("send, empty permission set (Loop<Send<Item, Continue>>)",
                     s::mint_session<SendLoop>(kForeground, FakeChannel{}));
}

[[nodiscard]] bench::Report permitted_send() {
    auto head =
        s::mint_permissioned_session<SendLoop>(kForeground, FakeChannel{}, perm::mint_permission_root<BenchRegion>());
    return send_loop("send, one permission in the set (Loop<Send<Item, Continue>>)", std::move(head));
}

}  // namespace

int main() {
    ::fixy::FixedArray<bench::Report, 2> reports{empty_set_send(), permitted_send()};
    bench::emit_reports_text(reports.view());

    std::printf("\n=== permission set: pair delta ===\n");
    ::fixy::FixedArray<bench::Compare, 1> const compares{
        bench::compare(reports.at(::fixy::index<0>()), reports.at(::fixy::index<1>()))};
    bench::emit_compares(compares.view());

    std::printf("\n  The static_assert carries the claim.  The timed pair is for inspection only.\n");
    bench::emit_reports_json(reports.view(), bench::env_json());
    return 0;
}
