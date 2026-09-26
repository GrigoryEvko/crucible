// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// The protocol sends a computation that needs IO.  The background
// context holds Bg and Alloc, and no IO.  One context runs the two sides
// of a forked channel, so it must hold each effect that a payload of
// either side carries, and mint_forked_channel refuses the call.  The
// bodies are correct: with a context that holds IO, the file compiles.
//
// Expected diagnostic: CtxAdmitsChannelRow is not satisfied.

#include <fixy/session/Handle.h>

#include <foundation/effects/Computation.h>
#include <foundation/effects/Ctx.h>
#include <foundation/permissions/Permission.h>

#include <optional>
#include <type_traits>
#include <utility>

namespace forked_row_fixture {
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

struct Wire {};
using IoWork = eff::Computation<eff::Row<eff::Effect::IO>, int>;
using Proto = s::Send<IoWork, s::End>;
using BgCtx = eff::detail::ctx_witnesses::BgWitness;
}  // namespace forked_row_fixture

namespace foundation::permissions {
template <>
struct can_split_into_pack<forked_row_fixture::Whole, forked_row_fixture::Left, forked_row_fixture::Right>
    : std::true_type {};
template <>
struct has_split_pack_authoring_witness<forked_row_fixture::Whole, forked_row_fixture::Left,
                                        forked_row_fixture::Right> : std::true_type {};
}  // namespace foundation::permissions

int main() {
    using namespace forked_row_fixture;
    const BgCtx ctx{eff::testing::bg()};
    auto back = s::mint_forked_channel<Proto, Left, Right>(
        ctx, perm::mint_permission_root<Whole>(), Wire{}, Wire{},
        [](auto head, auto const& /*left_view*/, BgCtx const&) noexcept {
            return std::move(head).send(IoWork{7}, [](Wire&, IoWork&) noexcept { return true; });
        },
        [](auto head, auto const& /*right_view*/, BgCtx const&) noexcept {
            auto [work, at_end] = std::move(head).recv([](Wire&) noexcept -> std::optional<IoWork> { return IoWork{7}; });
            (void)work;
            return std::move(at_end);
        });
    perm::permission_drop(std::move(back));
    return 0;
}
