#pragma once

// The payload is fixed-size and trivially copyable so a snapshot can be
// published by value. A growable or span-backed payload would need heap
// ownership or leave the reader holding a borrow.
//
// The channel is a template over the brand of its reader root.  The site
// that builds a channel mints that root and names its brand in the channel
// type, so the readers of one channel cannot present a share of another.

#include <fixy/Stale.h>
#include <fixy/concurrent/AtomicSnapshot.h>
#include <fixy/concurrent/SwmrSession.h>
#include <foundation/Brand.h>
#include <foundation/effects/Computation.h>
#include <foundation/effects/Row.h>
#include <foundation/permissions/Permission.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
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

struct RuntimeMetrics {
    double meb_lambda_max = 0.0;
    double meb_threshold = 0.0;
    double wasserstein_ratio = 0.0;
    double bits_per_step_ratio = 0.0;
    double dmft_tail_fraction = 0.0;
    double ntk_alpha = 0.0;
    double ntk_alpha_drift = 0.0;
    std::uint32_t delta_g_count = 0;
    std::uint32_t reserved = 0;
    std::array<double, 16> delta_g{};
};

using RuntimeMetricsSample = ::fixy::Stale<RuntimeMetrics>;
using RuntimeMetricsComputation =
    ::foundation::effects::Computation<::foundation::effects::Row<::foundation::effects::Effect::Bg>, RuntimeMetrics>;

template <::foundation::brand::IsBrand ReaderBrand, ::foundation::brand::IsBrand WriterBrand>
using RuntimeMetricsChannel =
    ::fixy::concurrent::swmr_session::SwmrSession<RuntimeMetricsSample, RuntimeMetricsWriterTag,
                                                  RuntimeMetricsReaderTag, ReaderBrand, WriterBrand>;
template <::foundation::brand::IsBrand ReaderBrand, ::foundation::brand::IsBrand WriterBrand>
using RuntimeMetricsWriter = typename RuntimeMetricsChannel<ReaderBrand, WriterBrand>::WriterHandle;
template <::foundation::brand::IsBrand ReaderBrand, ::foundation::brand::IsBrand WriterBrand>
using RuntimeMetricsReader = typename RuntimeMetricsChannel<ReaderBrand, WriterBrand>::ReaderHandle;

[[nodiscard]] inline RuntimeMetricsSample fresh_metrics_sample(RuntimeMetrics metrics) noexcept {
    return RuntimeMetricsSample::fresh(metrics);
}

[[nodiscard]] inline RuntimeMetricsSample metrics_sample_at(RuntimeMetrics metrics, std::uint64_t staleness) noexcept {
    return RuntimeMetricsSample::at(metrics, staleness);
}

template <::foundation::brand::IsBrand ReaderBrand, ::foundation::brand::IsBrand WriterBrand>
[[nodiscard]] RuntimeMetricsWriter<ReaderBrand, WriterBrand>
mint_metrics_writer(  // MINT-PATTERN-OK: the writer claim of the session is an atomic exchange
    RuntimeMetricsChannel<ReaderBrand, WriterBrand>& channel,
    ::foundation::permissions::Permission<RuntimeMetricsWriterTag, WriterBrand>&& permission) noexcept {
    return ::fixy::concurrent::swmr_session::mint_swmr_writer<RuntimeMetricsChannel<ReaderBrand, WriterBrand>>(
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

    [[nodiscard]] RuntimeMetricsSample load() const noexcept { return handle_.load(); }
    [[nodiscard]] std::optional<RuntimeMetricsSample> try_load() const noexcept { return handle_.try_load(); }
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

template <::foundation::brand::IsBrand ReaderBrand, ::foundation::brand::IsBrand WriterBrand>
[[nodiscard]] std::optional<KeeperMetricsReader<ReaderBrand, WriterBrand>>
mint_keeper_metrics_reader(RuntimeMetricsChannel<ReaderBrand, WriterBrand>& channel) noexcept {
    auto handle =
        ::fixy::concurrent::swmr_session::mint_swmr_reader<RuntimeMetricsChannel<ReaderBrand, WriterBrand>>(channel);
    if (!handle) return std::nullopt;
    return KeeperMetricsReader<ReaderBrand, WriterBrand>{std::move(*handle)};
}

template <::foundation::brand::IsBrand ReaderBrand, ::foundation::brand::IsBrand WriterBrand>
[[nodiscard]] std::optional<CanopyMetricsReader<ReaderBrand, WriterBrand>>
mint_canopy_metrics_reader(RuntimeMetricsChannel<ReaderBrand, WriterBrand>& channel) noexcept {
    auto handle =
        ::fixy::concurrent::swmr_session::mint_swmr_reader<RuntimeMetricsChannel<ReaderBrand, WriterBrand>>(channel);
    if (!handle) return std::nullopt;
    return CanopyMetricsReader<ReaderBrand, WriterBrand>{std::move(*handle)};
}

}  // namespace crucible::observe
