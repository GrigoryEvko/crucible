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
// The writer door of the channel: the body gets the writer handle of one
// frame (with_writer of fixy/channels/SwmrSession.h), and the door gives the
// result of the body.  The handle ends with the frame.
template <::foundation::brand::IsBrand ReaderBrand, ::foundation::brand::IsBrand WriterBrand, class Body>
auto with_metrics_writer(RuntimeMetricsChannel<ReaderBrand, WriterBrand>& channel,
                         ::foundation::permissions::Permission<RuntimeMetricsWriterTag, WriterBrand>&& permission,
                         Body&& body) noexcept {
    return ::fixy::channels::swmr_session::with_writer(channel, std::move(permission), std::forward<Body>(body));
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
// distinct types and do not convert.  Handle is the reader handle of one
// frame of the reader door, so the role reader holds the scope brand of that
// frame and ends with it.
template <typename Role, class Handle>
class RuntimeMetricsRoleReader final {
public:
    using role_type = Role;
    using value_type = RuntimeMetricsSample;

    explicit RuntimeMetricsRoleReader(Handle&& handle) noexcept : handle_{std::move(handle)} {}

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
    Handle handle_;
};

struct KeeperMetricsRole {};
struct CanopyMetricsRole {};

template <class Handle>
using KeeperMetricsReader = RuntimeMetricsRoleReader<KeeperMetricsRole, Handle>;
template <class Handle>
using CanopyMetricsReader = RuntimeMetricsRoleReader<CanopyMetricsRole, Handle>;

// The gate of a reader door of the channel: the context fits the lend of the
// reader pool (PoolCtx), so it holds Nondet.
template <class Ctx>
concept CtxFitsMetricsReaderDoor = ::foundation::permissions::PoolCtx<RuntimeMetricsReaderTag, Ctx>;

// Each reader door lends a share of the reader pool of the channel, and the
// body gets the reader of its role over the reader handle of one frame
// (with_reader of fixy/channels/SwmrSession.h).  The lend fails while the
// reader root is out, and the schedule decides when the root comes back, so
// the door takes a context that holds Nondet, and it gives an Option of the
// result of the body.  The door owns the body, as with_reader does: the body
// moves into the closure that the door gives to with_reader, and that closure
// calls it one time.  When the lend fails, with_reader ends that closure, and
// the body with it.
template <::foundation::brand::IsBrand ReaderBrand, ::foundation::brand::IsBrand WriterBrand, class Ctx, class Body>
    requires CtxFitsMetricsReaderDoor<Ctx> && (!std::is_reference_v<Body>)
[[nodiscard]] auto with_keeper_metrics_reader(Ctx const& ctx, RuntimeMetricsChannel<ReaderBrand, WriterBrand>& channel,
                                              Body&& body) noexcept {
    return ::fixy::channels::swmr_session::with_reader(
        ctx, channel, [owned = std::move(body)](auto handle) mutable noexcept {
            return ::foundation::linear::call_once(std::move(owned),
                                                   KeeperMetricsReader<decltype(handle)>{std::move(handle)});
        });
}

template <::foundation::brand::IsBrand ReaderBrand, ::foundation::brand::IsBrand WriterBrand, class Ctx, class Body>
    requires CtxFitsMetricsReaderDoor<Ctx> && (!std::is_reference_v<Body>)
[[nodiscard]] auto with_canopy_metrics_reader(Ctx const& ctx, RuntimeMetricsChannel<ReaderBrand, WriterBrand>& channel,
                                              Body&& body) noexcept {
    return ::fixy::channels::swmr_session::with_reader(
        ctx, channel, [owned = std::move(body)](auto handle) mutable noexcept {
            return ::foundation::linear::call_once(std::move(owned),
                                                   CanopyMetricsReader<decltype(handle)>{std::move(handle)});
        });
}

}  // namespace crucible::observe
