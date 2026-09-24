// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// The left side sends a computation that needs IO, and the right side
// receives it.  The background context holds Bg and Alloc, and no IO.
// One context runs the two sides of the channel, so it must hold each
// effect that a payload of either side carries, and
// mint_forked_async_channel refuses the call.  The two ends state one
// capacity, each side refines the dual of the other, and the bodies are
// correct.
//
// Expected diagnostic: CtxAdmitsChannelRow is not satisfied.

#include <fixy/session/AsyncChannel.h>

#include <foundation/effects/Computation.h>
#include <foundation/effects/Ctx.h>
#include <foundation/permissions/Permission.h>

#include <cstddef>
#include <optional>
#include <type_traits>
#include <utility>

namespace async_row_fixture {
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

struct RingEnd {
    static constexpr std::size_t channel_capacity = 1;
};
using IoWork = eff::Computation<eff::Row<eff::Effect::IO>, int>;
using LeftProto = s::Send<IoWork, s::End>;
using RightProto = s::Recv<IoWork, s::End>;
using BgCtx = eff::detail::ctx_witnesses::BgWitness;
}  // namespace async_row_fixture

namespace foundation::permissions {
template <>
struct can_split_into_pack<async_row_fixture::Whole, async_row_fixture::Left, async_row_fixture::Right>
    : std::true_type {};
template <>
struct has_split_pack_authoring_witness<async_row_fixture::Whole, async_row_fixture::Left, async_row_fixture::Right>
    : std::true_type {};
}  // namespace foundation::permissions

int main() {
    using namespace async_row_fixture;
    const BgCtx ctx{eff::testing::bg()};
    auto back = s::mint_forked_async_channel<LeftProto, RightProto, Left, Right>(
        ctx, perm::mint_permission_root<Whole>(), RingEnd{}, RingEnd{},
        [](auto head, auto /*left_permission*/, BgCtx const&) noexcept {
            return std::move(head).send(IoWork{7}, [](RingEnd&, IoWork&) noexcept { return true; });
        },
        [](auto head, auto /*right_permission*/, BgCtx const&) noexcept {
            auto [work, at_end] =
                std::move(head).recv([](RingEnd&) noexcept -> std::optional<IoWork> { return IoWork{7}; });
            (void)work;
            return std::move(at_end);
        });
    perm::permission_drop(std::move(back));
    return 0;
}
