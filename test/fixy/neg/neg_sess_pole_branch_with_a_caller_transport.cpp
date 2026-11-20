// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// A unit plants a pole table, in a probe region that the plugin rules admit,
// whose pole offers two branches.  The session reads the label word of the
// peer with a read of its own.  A select over a pole of a channel puts no
// label on the wire, and the pole table moves no label word, so a branch over
// a pole has no word to read, and the step refuses a read of the caller.
//
// Expected diagnostic: [Pole_Owns_Its_Transport] at the branch of the
// handle.

#include <fixy/Ctx.h>
#include <fixy/session/Handle.h>

#include <foundation/Quarantine.h>
#include <foundation/effects/Ctx.h>

#include <concepts>
#include <cstddef>
#include <utility>

namespace pole_branch_fixture {
namespace s = ::fixy::session;
struct OfferChannel {
    struct ProducerHandle {
        using channel_type = OfferChannel;
        [[no_unique_address]] s::MoveOnlyResource move_only{};
    };
    struct ConsumerHandle {
        using channel_type = OfferChannel;
        [[no_unique_address]] s::MoveOnlyResource move_only{};
    };
};
}  // namespace pole_branch_fixture

CRUCIBLE_I_KNOW_WHAT_IM_DOING("PROBE: the fixture plants a pole table whose pole offers two branches")
namespace fixy::session::pole_tables::branch_probe {
template <class Pole>
    requires std::same_as<Pole, pole_branch_fixture::OfferChannel::ConsumerHandle>
struct offer_protocols {
    using self_protocol = ::fixy::session::Loop<
        ::fixy::session::Offer<::fixy::session::Recv<int, ::fixy::session::Continue>, ::fixy::session::End>>;
    using peer_protocol = ::fixy::session::dual_of_t<self_protocol>;
};
inline constexpr ::fixy::session::pole_table offer_table{.table = ^^offer_protocols};
inline constexpr ::fixy::session::transition::seal table_seal{.kind = ^^::fixy::session::pole_table, .count = 1};
}  // namespace fixy::session::pole_tables::branch_probe
CRUCIBLE_END_I_KNOW_WHAT_IM_DOING

int main() {
    using namespace pole_branch_fixture;
    const ::fixy::TestRunnerCtx ctx{::foundation::effects::testing::test()};
    auto head = s::SessionMintDoor::open_pole<s::DefaultAbandonmentPolicy>(ctx, OfferChannel::ConsumerHandle{});
    std::move(head).branch(
        [](OfferChannel::ConsumerHandle&) noexcept -> ::fixy::Option<std::size_t> {
            return ::fixy::Option<std::size_t>{};
        },
        [](auto next) noexcept { std::move(next).detach(s::detach_reason::TestInstrumentation{}); });
    return 0;
}
