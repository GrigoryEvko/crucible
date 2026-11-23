#pragma once

// The channel that publishes the runtime metrics of observe/MetricsSample.h.
//
// The channel is a template over the brand of its reader root.  The site
// that builds a channel mints that root and names its brand in the channel
// type, so the readers of one channel cannot present a share of another.

#include <crucible/observe/MetricsSample.h>
#include <fixy/Core.h>
#include <fixy/channels/SwmrSession.h>
#include <fixy/concurrent/AtomicSnapshot.h>
#include <foundation/Brand.h>
#include <foundation/effects/Computation.h>
#include <foundation/effects/Row.h>
#include <foundation/permissions/Permission.h>

#include <cstddef>
#include <cstdint>
#include <type_traits>
#include <utility>

namespace crucible::observe {

// The channel lives in process memory, so neither role incurs an effect
// through it and each tag declares the empty row.
struct RuntimeMetricsWriterTag {
    using permission_row = ::foundation::effects::Row<>;
};
struct RuntimeMetricsReaderTag {
    using permission_row = ::foundation::effects::Row<>;
};

using RuntimeMetricsComputation =
    ::foundation::effects::Computation<::foundation::effects::Row<::foundation::effects::Effect::Bg>, RuntimeMetrics>;

template <::foundation::brand::IsBrand ReaderBrand, ::foundation::brand::IsBrand WriterBrand>
using RuntimeMetricsChannel =
    ::fixy::channels::swmr_session::SwmrSession<RuntimeMetricsSample, RuntimeMetricsWriterTag, RuntimeMetricsReaderTag,
                                                ReaderBrand, WriterBrand>;
template <::foundation::brand::IsBrand ReaderBrand, ::foundation::brand::IsBrand WriterBrand>
using RuntimeMetricsWriter = typename RuntimeMetricsChannel<ReaderBrand, WriterBrand>::WriterHandle;
template <::foundation::brand::IsBrand ReaderBrand, ::foundation::brand::IsBrand WriterBrand>
using RuntimeMetricsReader = typename RuntimeMetricsChannel<ReaderBrand, WriterBrand>::ReaderHandle;

template <::foundation::brand::IsBrand ReaderBrand, ::foundation::brand::IsBrand WriterBrand>
[[nodiscard]] RuntimeMetricsWriter<ReaderBrand, WriterBrand>
mint_metrics_writer(RuntimeMetricsChannel<ReaderBrand, WriterBrand>& channel,
                    ::foundation::permissions::Permission<RuntimeMetricsWriterTag, WriterBrand>&& permission) noexcept {
    return ::fixy::channels::swmr_session::mint_swmr_writer<RuntimeMetricsChannel<ReaderBrand, WriterBrand>>(
        channel, std::move(permission));
}

// Two consumers read this channel. The Keeper acts on a sample: it
// feeds the sample into a decision it then applies locally. The Canopy
// only forwards one: it gossips the sample to peers and never acts on
// it. That is a real difference in what a reader is for, and it was
// carried by nothing but the two factory names — both returned the same
// type from the same call, so a function written for one accepted the
// other and the names amounted to a comment.
//
// The role now rides in the type. Both roles read the same channel and
// hold the same share, so the wrapper adds no state and no work; what
// it adds is that KeeperMetricsReader and CanopyMetricsReader are
// distinct types and do not convert.
template <typename Role, ::foundation::brand::IsBrand ReaderBrand, ::foundation::brand::IsBrand WriterBrand>
class RuntimeMetricsRoleReader final {
public:
    using role_type = Role;
    using value_type = RuntimeMetricsSample;

    explicit RuntimeMetricsRoleReader(RuntimeMetricsReader<ReaderBrand, WriterBrand>&& handle) noexcept
        : handle_{std::move(handle)} {}

    RuntimeMetricsRoleReader(RuntimeMetricsRoleReader const&) =
        delete("a metrics reader owns one SharedPermissionPool share");
    RuntimeMetricsRoleReader&
    operator=(RuntimeMetricsRoleReader const&) = delete("a metrics reader owns one SharedPermissionPool share");
    RuntimeMetricsRoleReader(RuntimeMetricsRoleReader&&) noexcept = default;
    RuntimeMetricsRoleReader&
    operator=(RuntimeMetricsRoleReader&&) = delete("the share lifetime is fixed at construction");

    // The load spins with no exit while the writer publishes, so the context
    // owns Div (AtomicSnapshot::load).  The schedule of the writer decides
    // which sample the load gives, so the context also owns Nondet.
    template <class Ctx>
        requires ::foundation::effects::CtxOwnsCapability<Ctx, ::foundation::effects::Effect::Div>
              && ::foundation::effects::CtxOwnsCapability<Ctx, ::foundation::effects::Effect::Nondet>
    [[nodiscard]] RuntimeMetricsSample load(Ctx const& ctx) const noexcept {
        return handle_.load(ctx);
    }
    // The sample, or no value while the writer publishes.  The schedule
    // decides the sample and the outcome, so the call takes the tag of
    // Nondet.
    [[nodiscard]] ::fixy::Option<RuntimeMetricsSample>
    try_load(::foundation::effects::cap::Nondet nondet) const noexcept {
        return handle_.try_load(nondet);
    }
    [[nodiscard]] std::uint64_t version() const noexcept { return handle_.version(); }

private:
    RuntimeMetricsReader<ReaderBrand, WriterBrand> handle_;
};

struct KeeperMetricsRole {};
struct CanopyMetricsRole {};

template <::foundation::brand::IsBrand ReaderBrand, ::foundation::brand::IsBrand WriterBrand>
using KeeperMetricsReader = RuntimeMetricsRoleReader<KeeperMetricsRole, ReaderBrand, WriterBrand>;
template <::foundation::brand::IsBrand ReaderBrand, ::foundation::brand::IsBrand WriterBrand>
using CanopyMetricsReader = RuntimeMetricsRoleReader<CanopyMetricsRole, ReaderBrand, WriterBrand>;

// Each reader mint lends a share of the reader pool of the channel.  The lend
// fails while the reader root is out, and the schedule decides when the root
// comes back, so the mint takes a context that holds Nondet (the gate of
// mint_swmr_reader).
template <::foundation::brand::IsBrand ReaderBrand, ::foundation::brand::IsBrand WriterBrand, class Ctx>
    requires ::fixy::channels::swmr_session::CtxFitsSwmrReaderMint<Ctx, RuntimeMetricsChannel<ReaderBrand, WriterBrand>>
[[nodiscard]] ::fixy::Option<KeeperMetricsReader<ReaderBrand, WriterBrand>>
mint_keeper_metrics_reader(Ctx const& ctx, RuntimeMetricsChannel<ReaderBrand, WriterBrand>& channel) noexcept {
    using Reader = KeeperMetricsReader<ReaderBrand, WriterBrand>;
    auto handle =
        ::fixy::channels::swmr_session::mint_swmr_reader<RuntimeMetricsChannel<ReaderBrand, WriterBrand>>(ctx, channel);
    if (handle.is_none()) return ::fixy::none;
    return ::fixy::Option<Reader>::some(Reader{std::move(handle).expect("the reader pool lent a share")});
}

template <::foundation::brand::IsBrand ReaderBrand, ::foundation::brand::IsBrand WriterBrand, class Ctx>
    requires ::fixy::channels::swmr_session::CtxFitsSwmrReaderMint<Ctx, RuntimeMetricsChannel<ReaderBrand, WriterBrand>>
[[nodiscard]] ::fixy::Option<CanopyMetricsReader<ReaderBrand, WriterBrand>>
mint_canopy_metrics_reader(Ctx const& ctx, RuntimeMetricsChannel<ReaderBrand, WriterBrand>& channel) noexcept {
    using Reader = CanopyMetricsReader<ReaderBrand, WriterBrand>;
    auto handle =
        ::fixy::channels::swmr_session::mint_swmr_reader<RuntimeMetricsChannel<ReaderBrand, WriterBrand>>(ctx, channel);
    if (handle.is_none()) return ::fixy::none;
    return ::fixy::Option<Reader>::some(Reader{std::move(handle).expect("the reader pool lent a share")});
}

}  // namespace crucible::observe
