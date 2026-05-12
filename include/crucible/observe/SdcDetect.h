#pragma once

// GAPS-180. Silent data corruption detection substrate.
//
// This header owns deterministic redundant-compute comparison over an admitted,
// fixed-size Cog set. It does not schedule remote work, write Cipher audit
// records, or quarantine Cogs by itself; those policy/action layers consume the
// bounded events emitted here.

#include <crucible/cog/CogIdentity.h>
#include <crucible/effects/Capabilities.h>
#include <crucible/effects/EffectRow.h>
#include <crucible/effects/ExecCtx.h>
#include <crucible/observe/Observation.h>
#include <crucible/safety/Diagnostic.h>
#include <crucible/safety/Pinned.h>
#include <crucible/safety/Refined.h>
#include <crucible/safety/Tagged.h>

#include <array>
#include <cmath>
#include <concepts>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <expected>
#include <memory>
#include <span>
#include <string_view>
#include <type_traits>
#include <utility>

namespace crucible::observe {

using PositiveSdcReplicaCount = safety::Positive<std::uint8_t>;
using SdcSamplingRatePpm =
    safety::Refined<safety::in_range<1u, 1'000'000u>, std::uint32_t>;
using PositiveSdcMismatchThreshold = safety::Positive<std::uint16_t>;

template <typename T>
using SdcVerified = safety::Tagged<T, safety::source::SdcVerified>;

enum class SdcComparisonStrategy : std::uint8_t {
    BitwiseEqual = 0,
    ArithmeticTolerance = 1,
};

[[nodiscard]] constexpr std::string_view
sdc_comparison_strategy_name(SdcComparisonStrategy strategy) noexcept {
    switch (strategy) {
        case SdcComparisonStrategy::BitwiseEqual: return "BitwiseEqual";
        case SdcComparisonStrategy::ArithmeticTolerance:
            return "ArithmeticTolerance";
        default: return "<unknown SdcComparisonStrategy>";
    }
}

enum class SdcEventKind : std::uint8_t {
    Verified = 0,
    Mismatch = 1,
    InsufficientReplicas = 2,
};

[[nodiscard]] constexpr std::string_view
sdc_event_kind_name(SdcEventKind kind) noexcept {
    switch (kind) {
        case SdcEventKind::Verified: return "Verified";
        case SdcEventKind::Mismatch: return "Mismatch";
        case SdcEventKind::InsufficientReplicas: return "InsufficientReplicas";
        default: return "<unknown SdcEventKind>";
    }
}

struct SdcMismatch : safety::diag::tag_base {
    static constexpr std::string_view name = "SdcMismatch";
    static constexpr std::string_view description =
        "Redundant execution produced non-equivalent results.";
    static constexpr std::string_view remediation =
        "Retry the operation and route repeated mismatches into Warden "
        "quarantine policy for the implicated Cogs.";
};

struct SdcConfig {
    PositiveSdcReplicaCount redundancy_factor{std::uint8_t{2}};
    SdcSamplingRatePpm sampling_rate_ppm{std::uint32_t{10'000}};
    PositiveSdcMismatchThreshold suspect_after_mismatches{std::uint16_t{3}};
    SdcComparisonStrategy strategy = SdcComparisonStrategy::BitwiseEqual;
    std::uint64_t tolerance_units = 0;
    std::uint32_t metric_id_base = 0x5344'0000u;
};

struct SdcEvent {
    SdcEventKind kind = SdcEventKind::Verified;
    SdcComparisonStrategy strategy = SdcComparisonStrategy::BitwiseEqual;
    std::uint16_t primary_slot = 0;
    std::uint16_t comparison_slot = 0;
    std::uint16_t compared_replicas = 0;
    std::uint16_t mismatch_count = 0;
    std::uint64_t sequence = 0;
    std::uint64_t tolerance_units = 0;
    cog::Uuid primary_cog{};
    cog::Uuid comparison_cog{};
};

static_assert(std::is_trivially_copyable_v<SdcConfig>);
static_assert(std::is_trivially_copyable_v<SdcEvent>);

template <class Ctx>
concept CtxFitsSdcMint =
    effects::IsExecCtx<Ctx>
    && effects::CtxAdmits<Ctx, effects::Row<effects::Effect::Init>>;

template <class Ctx>
concept CtxFitsSdcRun =
    effects::IsExecCtx<Ctx>
    && effects::CtxAdmits<Ctx, effects::Row<effects::Effect::Bg>>;

namespace detail {

[[nodiscard]] constexpr std::uint64_t
mix_sequence(std::uint64_t value) noexcept {
    value ^= value >> 30u;
    value *= 0xbf58'476d'1ce4'e5b9ull;
    value ^= value >> 27u;
    value *= 0x94d0'49bb'1331'11ebull;
    value ^= value >> 31u;
    return value;
}

template <typename T>
[[nodiscard]] bool bitwise_equal(T const& a, T const& b) noexcept {
    static_assert(std::is_trivially_copyable_v<T>,
        "SDC bitwise comparison requires trivially copyable results.");
    return std::memcmp(std::addressof(a), std::addressof(b), sizeof(T)) == 0;
}

template <typename T>
[[nodiscard]] bool tolerance_equal(T const& a,
                                   T const& b,
                                   std::uint64_t tolerance) noexcept {
    if constexpr (std::integral<T>) {
        using U = std::make_unsigned_t<T>;
        U const delta = [&] {
            if constexpr (std::signed_integral<T>) {
                return a >= b
                    ? static_cast<U>(a) - static_cast<U>(b)
                    : static_cast<U>(b) - static_cast<U>(a);
            } else {
                return a >= b ? a - b : b - a;
            }
        }();
        return static_cast<std::uint64_t>(delta) <= tolerance;
    } else if constexpr (std::floating_point<T>) {
        auto const delta = std::fabs(a - b);
        return delta <= static_cast<decltype(delta)>(tolerance);
    } else {
        return bitwise_equal(a, b);
    }
}

template <typename T>
[[nodiscard]] bool equivalent(T const& a,
                              T const& b,
                              SdcComparisonStrategy strategy,
                              std::uint64_t tolerance) noexcept {
    switch (strategy) {
        case SdcComparisonStrategy::BitwiseEqual:
            return bitwise_equal(a, b);
        case SdcComparisonStrategy::ArithmeticTolerance:
            return tolerance_equal(a, b, tolerance);
        default:
            return false;
    }
}

}  // namespace detail

template <std::size_t MaxCogs, std::size_t MaxEvents>
class SdcDetector : public safety::Pinned<SdcDetector<MaxCogs, MaxEvents>> {
    static_assert(MaxCogs > 0, "SdcDetector requires at least one Cog slot.");
    static_assert(MaxEvents > 0, "SdcDetector requires an event ring.");

public:
    static constexpr std::size_t max_cogs = MaxCogs;
    static constexpr std::size_t max_events = MaxEvents;

    struct CogSlot {
        cog::CogIdentity cog{};
        std::uint16_t mismatch_count = 0;
        bool active = false;
    };

private:
    SdcConfig config_{};
    std::array<CogSlot, MaxCogs> cogs_{};
    std::array<SdcEvent, MaxEvents> events_{};
    std::uint64_t event_sequence_ = 0;
    std::size_t event_cursor_ = 0;
    std::size_t active_cogs_ = 0;

    [[nodiscard]] std::size_t find_cog(cog::CogIdentity const& cog) const noexcept {
        for (std::size_t i = 0; i < cogs_.size(); ++i) {
            if (cogs_[i].active && cogs_[i].cog.uuid == cog.uuid) {
                return i;
            }
        }
        return cogs_.size();
    }

    [[nodiscard]] SdcEvent record_event(SdcEvent event) noexcept {
        event.sequence = ++event_sequence_;
        events_[event_cursor_] = event;
        event_cursor_ = (event_cursor_ + 1u) % events_.size();
        return event;
    }

public:
    explicit SdcDetector(SdcConfig config = {}) noexcept
        : config_{config} {}

    [[nodiscard]] SdcConfig config() const noexcept { return config_; }

    [[nodiscard]] std::span<const CogSlot, MaxCogs> cogs() const noexcept {
        return std::span<const CogSlot, MaxCogs>{cogs_};
    }

    [[nodiscard]] std::span<const SdcEvent, MaxEvents> events() const noexcept {
        return std::span<const SdcEvent, MaxEvents>{events_};
    }

    [[nodiscard]] std::size_t active_cog_count() const noexcept {
        return active_cogs_;
    }

    [[nodiscard]] constexpr bool should_sample(std::uint64_t sequence) const noexcept {
        std::uint64_t const bucket = detail::mix_sequence(sequence) % 1'000'000ull;
        return bucket < config_.sampling_rate_ppm.value();
    }

    [[nodiscard]] bool register_cog(cog::CogIdentity cog) noexcept {
        if (cog.uuid.is_zero() || find_cog(cog) != cogs_.size()) {
            return false;
        }
        if (active_cogs_ >= cogs_.size()) {
            return false;
        }
        cogs_[active_cogs_] = CogSlot{.cog = cog, .mismatch_count = 0, .active = true};
        ++active_cogs_;
        return true;
    }

    template <class Ctx, class Work>
        requires CtxFitsSdcRun<Ctx>
    [[nodiscard]] auto run_with_redundancy(Ctx const&, Work&& work)
        noexcept(noexcept(std::forward<Work>(work)(cogs_[0].cog)))
        -> std::expected<
            SdcVerified<std::remove_cvref_t<decltype(std::forward<Work>(work)(cogs_[0].cog))>>,
            SdcEvent>
    {
        using Result = std::remove_cvref_t<decltype(std::forward<Work>(work)(cogs_[0].cog))>;
        static_assert(std::is_trivially_copyable_v<Result>,
            "SdcDetector results must be trivially copyable for deterministic comparison.");

        std::size_t const required = config_.redundancy_factor.value();
        if (active_cogs_ < required) {
            SdcEvent event{};
            event.kind = SdcEventKind::InsufficientReplicas;
            event.strategy = config_.strategy;
            event.compared_replicas = static_cast<std::uint16_t>(active_cogs_);
            event.tolerance_units = config_.tolerance_units;
            return std::unexpected(record_event(event));
        }

        Work& body = work;
        Result primary = body(cogs_[0].cog);
        for (std::size_t i = 1; i < required; ++i) {
            Result replica = body(cogs_[i].cog);
            if (!detail::equivalent(primary, replica, config_.strategy,
                                    config_.tolerance_units)) {
                auto& primary_slot = cogs_[0];
                auto& comparison_slot = cogs_[i];
                ++primary_slot.mismatch_count;
                ++comparison_slot.mismatch_count;

                SdcEvent event{};
                event.kind = SdcEventKind::Mismatch;
                event.strategy = config_.strategy;
                event.primary_slot = 0;
                event.comparison_slot = static_cast<std::uint16_t>(i);
                event.compared_replicas = static_cast<std::uint16_t>(i + 1u);
                event.mismatch_count = comparison_slot.mismatch_count;
                event.tolerance_units = config_.tolerance_units;
                event.primary_cog = primary_slot.cog.uuid;
                event.comparison_cog = comparison_slot.cog.uuid;
                return std::unexpected(record_event(event));
            }
        }

        SdcEvent event{};
        event.kind = SdcEventKind::Verified;
        event.strategy = config_.strategy;
        event.compared_replicas = static_cast<std::uint16_t>(required);
        event.tolerance_units = config_.tolerance_units;
        event.primary_cog = cogs_[0].cog.uuid;
        (void)record_event(event);
        return SdcVerified<Result>{primary};
    }

    [[nodiscard]] bool should_quarantine(cog::CogIdentity const& cog) const noexcept {
        std::size_t const index = find_cog(cog);
        if (index == cogs_.size()) {
            return false;
        }
        return cogs_[index].mismatch_count
            >= config_.suspect_after_mismatches.value();
    }

    [[nodiscard]] bool publish_latest(ObservationSnapshot& sink) const noexcept {
        if (event_sequence_ == 0) {
            return false;
        }
        std::size_t const cursor =
            event_cursor_ == 0 ? events_.size() - 1u : event_cursor_ - 1u;
        SdcEvent const& event = events_[cursor];
        record_observation(sink, make_observation(
            ObservationKind::Metric,
            ObservationSource::Runtime,
            config_.metric_id_base,
            static_cast<std::uint64_t>(event.kind),
            event.sequence));
        return true;
    }
};

template <class Ctx, std::size_t MaxCogs, std::size_t MaxEvents>
    requires CtxFitsSdcMint<Ctx>
[[nodiscard]] SdcDetector<MaxCogs, MaxEvents>
mint_sdc_detector(Ctx const&, SdcConfig config = {}) noexcept {
    return SdcDetector<MaxCogs, MaxEvents>{config};
}

static_assert(CtxFitsSdcMint<effects::ColdInitCtx>);
static_assert(!CtxFitsSdcMint<effects::BgDrainCtx>);
static_assert(CtxFitsSdcRun<effects::BgDrainCtx>);
static_assert(!CtxFitsSdcRun<effects::HotFgCtx>);

}  // namespace crucible::observe
