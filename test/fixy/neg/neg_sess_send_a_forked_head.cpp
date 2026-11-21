// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// Each side of mint_forked_channel opens its endpoint on its own thread, runs
// its body there and closes the End handle there, so it opens the endpoint
// under check::Pinned.  The pinned policy is thread_bound
// (foundation/Transfer.h), so the head of a side is not Sendable.  A body of
// run_joined runs on another thread, so the door refuses a body that owns
// the head of a side.
//
// Expected diagnostic: the verdict of the door names the field and the cause.

#include <fixy/Core.h>
#include <fixy/Ctx.h>
#include <fixy/os/ThreadTasks.h>
#include <fixy/session/Handle.h>
#include <fixy/session/Mint.h>

#include <foundation/effects/Ctx.h>
#include <foundation/permissions/Permission.h>

#include <type_traits>
#include <utility>

namespace {
namespace s = ::fixy::session;
namespace eff = ::foundation::effects;
namespace perm = ::foundation::permissions;

struct Whole {
    using permission_row = eff::Row<>;
};
struct Left {
    using permission_row = eff::Row<>;
};
struct Right {
    using permission_row = eff::Row<>;
};

struct Msg {};
struct Wire {};
using DrainCtx = eff::ExecCtx<eff::Bg, eff::Row<eff::Effect::Bg, eff::Effect::Alloc>>;
using Proto = s::Send<Msg, s::End>;
constexpr auto write_msg = [](Wire&, Msg&) noexcept { return true; };
constexpr auto read_msg = [](Wire&, s::watch::wait_scope&) noexcept { return Msg{}; };
}  // namespace

namespace foundation::permissions {
template <>
struct can_split_into_pack<Whole, Left, Right> : std::true_type {};
template <>
struct has_split_pack_authoring_witness<Whole, Left, Right> : std::true_type {};
}  // namespace foundation::permissions

int main() {
    const DrainCtx ctx{eff::testing::bg()};
    auto whole = perm::mint_permission_root<Whole>();
    auto back = s::mint_forked_channel<Proto, Left, Right>(
        ctx, std::move(whole), Wire{}, Wire{},
        [](auto head, auto const& /*left_view*/,
           DrainCtx const&) noexcept -> decltype(std::move(head).send(Msg{}, write_msg)) {
            ::fixy::BgLoadCtx const bg{eff::testing::bg()};
            auto body = [moved = std::move(head)]() mutable noexcept {
                static_cast<void>(std::move(moved).send(Msg{}, write_msg).close());
            };
            [[maybe_unused]] auto started = ::fixy::thread::run_joined(bg, ::fixy::thread::ThreadOptions{}, body);
            ::fixy::unreachable();
        },
        [](auto head, auto const& /*right_view*/, DrainCtx const&) noexcept {
            auto [msg, done] = std::move(head).recv(read_msg);
            static_cast<void>(msg);
            return std::move(done);
        });
    ::foundation::linear::drop(std::move(back));
    return 0;
}
