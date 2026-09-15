#pragma once

// The payload is fixed-size and trivially copyable so a snapshot can be
// published by value. A growable or span-backed payload would need heap
// ownership or leave the reader holding a borrow.

#include <crucible/concurrent/AtomicSnapshot.h>
#include <crucible/effects/Computation.h>
#include <crucible/permissions/Permission.h>
#include <crucible/safety/Stale.h>
#include <crucible/sessions/SwmrSession.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <type_traits>
#include <utility>

namespace crucible::observe {

struct RuntimeMetricsWriterTag {};
struct RuntimeMetricsReaderTag {};

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

using RuntimeMetricsSample = ::crucible::safety::Stale<RuntimeMetrics>;
using RuntimeMetricsComputation =
    ::crucible::effects::Computation<::crucible::effects::Row<::crucible::effects::Effect::Bg>, RuntimeMetrics>;
using RuntimeMetricsChannel =
    ::crucible::safety::proto::swmr_session::SwmrSession<RuntimeMetricsSample, RuntimeMetricsWriterTag,
                                                         RuntimeMetricsReaderTag>;
using RuntimeMetricsWriter = typename RuntimeMetricsChannel::WriterHandle;
using RuntimeMetricsReader = typename RuntimeMetricsChannel::ReaderHandle;

static_assert(std::is_trivially_copyable_v<RuntimeMetrics>);
static_assert(std::is_trivially_destructible_v<RuntimeMetrics>);
static_assert(::crucible::concurrent::SnapshotValue<RuntimeMetricsSample>);

[[nodiscard]] inline RuntimeMetricsSample fresh_metrics_sample(RuntimeMetrics metrics) noexcept {
    return RuntimeMetricsSample::fresh(metrics);
}

[[nodiscard]] inline RuntimeMetricsSample metrics_sample_at(RuntimeMetrics metrics, std::uint64_t staleness) noexcept {
    return RuntimeMetricsSample::at(metrics, staleness);
}

[[nodiscard]] inline RuntimeMetricsWriter
mint_metrics_writer(RuntimeMetricsChannel& channel,
                    ::crucible::safety::Permission<RuntimeMetricsWriterTag>&& permission) noexcept {
    return ::crucible::safety::proto::swmr_session::mint_swmr_writer<RuntimeMetricsChannel>(channel,
                                                                                            std::move(permission));
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
template <typename Role>
class RuntimeMetricsRoleReader final {
public:
    using role_type = Role;
    using value_type = RuntimeMetricsSample;

    explicit RuntimeMetricsRoleReader(RuntimeMetricsReader&& handle) noexcept : handle_{std::move(handle)} {}

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
    RuntimeMetricsReader handle_;
};

struct KeeperMetricsRole {};
struct CanopyMetricsRole {};

using KeeperMetricsReader = RuntimeMetricsRoleReader<KeeperMetricsRole>;
using CanopyMetricsReader = RuntimeMetricsRoleReader<CanopyMetricsRole>;

// The whole point of the split: neither role converts to the other, so
// the two mint names below now differ in what they hand back.
static_assert(!std::is_same_v<KeeperMetricsReader, CanopyMetricsReader>);
static_assert(!std::is_convertible_v<KeeperMetricsReader, CanopyMetricsReader>);
static_assert(!std::is_convertible_v<CanopyMetricsReader, KeeperMetricsReader>);
static_assert(sizeof(KeeperMetricsReader) == sizeof(RuntimeMetricsReader),
              "the role is a type-level marker; it must not cost a byte.");

[[nodiscard]] inline std::optional<KeeperMetricsReader>
mint_keeper_metrics_reader(RuntimeMetricsChannel& channel) noexcept {
    auto handle = ::crucible::safety::proto::swmr_session::mint_swmr_reader<RuntimeMetricsChannel>(channel);
    if (!handle) return std::nullopt;
    return KeeperMetricsReader{std::move(*handle)};
}

[[nodiscard]] inline std::optional<CanopyMetricsReader>
mint_canopy_metrics_reader(RuntimeMetricsChannel& channel) noexcept {
    auto handle = ::crucible::safety::proto::swmr_session::mint_swmr_reader<RuntimeMetricsChannel>(channel);
    if (!handle) return std::nullopt;
    return CanopyMetricsReader{std::move(*handle)};
}

}  // namespace crucible::observe
