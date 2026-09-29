// The permission set of a session handle is a type, not a member.
//
// fixy/session/Handle.h has one handle type, and the permission set is one
// of its template arguments.  A handle whose set holds a permission must
// cost the same for each operation as a handle whose set is empty, and the
// context-bound mint must build the same handle as the bare mint.
//
// Two tiers of evidence:
//
//   Structural (the claim): the static_asserts in main.  The handle with a
//   permission has the size of the handle with the empty set, so the set
//   takes no storage, and the context-bound mint returns the type of the
//   bare mint.  The set moves at compile time only.
//
//   Timed (informational): two pairs, each on one protocol and one
//   Resource.  At the sub-nanosecond scale the layout of the two measured
//   bodies can move the numbers by a cycle, so the program exits 0 and the
//   numbers are for inspection.
//
// Single-threaded.  Loop<Send<Item, Continue>> lands each send on the loop
// head again, so the measured body can assign the next handle back to the
// same variable.  The loop has no exit branch, so each run ends with a typed
// detach.

#include <fixy/session/Entry.h>
#include <fixy/session/Handle.h>

#include <foundation/effects/Ctx.h>
#include <foundation/permissions/Permission.h>

#include "bench_harness.h"

#include <array>
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

using EmptySetLoopHandle = decltype(s::mint_session_handle<SendLoop>(std::declval<FakeChannel>()));
using PermittedLoopHandle = decltype(s::mint_permissioned_session<SendLoop>(kForeground, std::declval<FakeChannel>(),
                                                                            perm::mint_permission_root<BenchRegion>())
                                         .first);
using BareEndHandle = decltype(s::mint_session_handle<s::End>(std::declval<FakeChannel>()));
using CtxEndHandle = decltype(s::mint_session<s::End>(kForeground, std::declval<FakeChannel>()));

static_assert(sizeof(PermittedLoopHandle) == sizeof(EmptySetLoopHandle),
              "a permission set must take no storage in the handle");
static_assert(std::is_same_v<CtxEndHandle, BareEndHandle>,
              "the context-bound mint must build the handle of the bare mint");

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
                     s::mint_session_handle<SendLoop>(FakeChannel{}));
}

[[nodiscard]] bench::Report permitted_send() {
    auto [head, hold] =
        s::mint_permissioned_session<SendLoop>(kForeground, FakeChannel{}, perm::mint_permission_root<BenchRegion>());
    auto report = send_loop("send, one permission in the set (Loop<Send<Item, Continue>>)", std::move(head));
    auto [token] = std::move(hold).into_permissions();
    bench::do_not_optimize(token);
    return report;
}

// close() consumes the handle and gives back the Resource, so each
// iteration mints a fresh handle.  The two arms pay the same mint.
[[nodiscard]] bench::Report bare_close() {
    return bench::run("close, bare mint (End)", [] {
        auto channel = s::mint_session_handle<s::End>(FakeChannel{}).close();
        bench::do_not_optimize(channel);
    });
}

[[nodiscard]] bench::Report ctx_close() {
    return bench::run("close, context-bound mint (End)", [] {
        auto channel = s::mint_session<s::End>(kForeground, FakeChannel{}).close();
        bench::do_not_optimize(channel);
    });
}

}  // namespace

int main() {
    std::array reports{empty_set_send(), permitted_send(), bare_close(), ctx_close()};
    bench::emit_reports_text(reports);

    std::printf("\n=== permission set and context-bound mint: pair deltas ===\n");
    const std::array compares{bench::compare(reports[0], reports[1]), bench::compare(reports[2], reports[3])};
    bench::emit_compares(compares);

    std::printf("\n  The static_asserts carry the claim.  The timed pairs are for inspection only.\n");
    bench::emit_reports_json(reports, bench::env_json());
    return 0;
}
