// The umbrella header promises that including it alone gives a caller the
// framework surface. Every claim below is reached through the umbrella, so
// one missing include inside it reddens this file. Reaching for a second
// project header here would hide exactly the failure this catches.

#include <crucible/sessions/Sessions.h>

#include <type_traits>

namespace {

namespace proto = ::crucible::safety::proto;
namespace eff = ::crucible::effects;

// Naming the type of the mint expression already forces its requires-clause
// to hold and its result to be well formed, so there is nothing to gain
// from calling it.

struct ProbeResource {
    int value = 0;
};

using EmptyMintFromUmbrella = decltype(proto::mint_permissioned_session<proto::End>(
    std::declval<eff::HotFgCtx const&>(), std::declval<ProbeResource>()));

static_assert(std::is_same_v<typename EmptyMintFromUmbrella::protocol, proto::End>,
              "mint_permissioned_session<End>(ctx, res) must produce "
              "a PSH whose ::protocol == End.");

static_assert(std::is_same_v<typename EmptyMintFromUmbrella::perm_set, proto::EmptyPermSet>,
              "mint_permissioned_session<End>(ctx, res) without perms "
              "must produce a PSH with EmptyPermSet.");

using BgComputation = eff::Computation<eff::Row<eff::Effect::Bg>, int>;
using ProjectedRow = proto::payload_row_t<BgComputation>;

static_assert(std::is_same_v<ProjectedRow, eff::Row<eff::Effect::Bg>>,
              "payload_row_t<Computation<Row<Bg>, int>> must project "
              "to Row<Bg>.  If this fails the umbrella missed "
              "SessionRowExtraction.h.");

using ProducerInt = proto::mpmc_channel_session::ProducerProto<int>;
using ConsumerInt = proto::mpmc_channel_session::ConsumerProto<int>;

static_assert(std::is_same_v<ProducerInt, proto::Loop<proto::Send<int, proto::Continue>>>,
              "mpmc_channel_session::ProducerProto<T> must be the canonical "
              "infinite Loop<Send<T, Continue>>.");

static_assert(std::is_same_v<ConsumerInt, proto::Loop<proto::Recv<int, proto::Continue>>>,
              "mpmc_channel_session::ConsumerProto<T> must be the canonical "
              "infinite Loop<Recv<T, Continue>>.");

static_assert(!proto::mpmc_channel_session::MpmcChannelSessionSurface<int>,
              "MpmcChannelSessionSurface must reject `int` — `int` has "
              "no ProducerHandle / ConsumerHandle / etc. nested types.");

}  // namespace

int main() { return 0; }
