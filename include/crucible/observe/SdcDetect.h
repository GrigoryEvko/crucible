#pragma once

// SDC is silent data corruption.

#include <crucible/cog/CogIdentity.h>
#include <crucible/observe/Observation.h>
#include <fixy/Ctx.h>
#include <fixy/Refined.h>
#include <fixy/Tagged.h>
#include <fixy/Tags.h>
#include <foundation/Pinned.h>
#include <foundation/Saturate.h>
#include <foundation/diag/Catalog.h>
#include <foundation/effects/Ctx.h>
#include <foundation/effects/Row.h>

#include <array>
#include <cmath>
#include <concepts>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <expected>
#include <limits>
#include <memory>
#include <span>
#include <string_view>
#include <type_traits>
#include <utility>

namespace crucible::observe {

using PositiveSdcReplicaCount = ::fixy::Positive<std::uint8_t>;
using SdcSamplingRatePpm = ::fixy::Refined<::fixy::in_range<1u, 1000000u>, std::uint32_t>;
using PositiveSdcMismatchThreshold = ::fixy::Positive<std::uint16_t>;

template <typename T>
using SdcVerified = ::fixy::Tagged<T, ::fixy::tags::source::SdcVerified>;

enum class SdcComparisonStrategy : std::uint8_t {
    BitwiseEqual = 0,
    ArithmeticTolerance = 1,
};

enum class SdcEventKind : std::uint8_t {
    Verified = 0,
    Mismatch = 1,
    InsufficientReplicas = 2,
};

struct SdcMismatch : ::foundation::diag::tag_base {
    static constexpr std::string_view name = "SdcMismatch";
    static constexpr std::string_view description = "Redundant execution produced non-equivalent results.";
    static constexpr std::string_view remediation = "Retry the operation and route repeated mismatches into Warden "
                                                    "quarantine policy for the implicated Cogs.";
};

struct SdcConfig {
    PositiveSdcReplicaCount redundancy_factor = ::fixy::mint_refined<::fixy::positive>(std::uint8_t{2});
    SdcSamplingRatePpm sampling_rate_ppm = ::fixy::mint_refined<::fixy::in_range<1u, 1000000u>>(std::uint32_t{10000});
    PositiveSdcMismatchThreshold suspect_after_mismatches = ::fixy::mint_refined<::fixy::positive>(std::uint16_t{3});
    SdcComparisonStrategy strategy = SdcComparisonStrategy::BitwiseEqual;
    std::uint64_t tolerance_units = 0;
    std::uint32_t metric_id_base = 0x53440000u;
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

// A refined field keeps no byte route into it, so the config is not
// trivially copyable.  Its copies stay trivial, so a config still passes
// by value in registers.
static_assert(std::is_trivially_copy_constructible_v<SdcConfig> && std::is_trivially_destructible_v<SdcConfig>);
static_assert(std::is_trivially_copyable_v<SdcEvent>);

template <class Ctx>
concept CtxFitsSdcMint =
    ::foundation::effects::IsExecCtx<Ctx>
    && ::foundation::effects::CtxAdmits<Ctx, ::foundation::effects::Row<::foundation::effects::Effect::Init>>;

template <class Ctx>
concept CtxFitsSdcRun =
    ::foundation::effects::IsExecCtx<Ctx>
    && ::foundation::effects::CtxAdmits<Ctx, ::foundation::effects::Row<::foundation::effects::Effect::Bg>>;

namespace detail {

[[nodiscard]] constexpr std::uint64_t mix_sequence(std::uint64_t value) noexcept {
    value ^= value >> 30u;
    value *= 0xbf58476d1ce4e5b9ull;
    value ^= value >> 27u;
    value *= 0x94d049bb133111ebull;
    value ^= value >> 31u;
    return value;
}

template <typename T>
[[nodiscard]] bool bitwise_equal(T const& a, T const& b) noexcept {
    static_assert(std::is_trivially_copyable_v<T>, "SDC bitwise comparison requires trivially copyable results.");
    return std::memcmp(std::addressof(a), std::addressof(b), sizeof(T)) == 0;
}

template <typename T>
[[nodiscard]] bool tolerance_equal(T const& a, T const& b, std::uint64_t tolerance) noexcept {
    if constexpr (std::integral<T>) {
        using U = std::make_unsigned_t<T>;
        // Sub-int operands promote to int for the subtraction. Casting back
        // to U is the intended magnitude, the absolute difference modulo
        // 2^bits, and keeps the path clean under -Werror=conversion. The
        // template then compiles at every integral width, not only at int
        // and wider.
        U const delta = [&]() -> U {
            if constexpr (std::signed_integral<T>) {
                return a >= b ? static_cast<U>(static_cast<U>(a) - static_cast<U>(b))
                              : static_cast<U>(static_cast<U>(b) - static_cast<U>(a));
            } else {
                return a >= b ? static_cast<U>(a - b) : static_cast<U>(b - a);
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
[[nodiscard]] bool equivalent(T const& a, T const& b, SdcComparisonStrategy strategy,
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
class SdcDetector;

// The only door into a detector.  An Init-row context mints it; a
// background worker may then run checks on it.
template <class Ctx, std::size_t MaxCogs, std::size_t MaxEvents>
    requires CtxFitsSdcMint<Ctx>
[[nodiscard]] constexpr SdcDetector<MaxCogs, MaxEvents> mint_sdc_detector(Ctx const&, SdcConfig config = {}) noexcept;

template <std::size_t MaxCogs, std::size_t MaxEvents>
class SdcDetector : public ::foundation::Pinned<SdcDetector<MaxCogs, MaxEvents>> {
    static_assert(MaxCogs > 0, "SdcDetector requires at least one Cog slot.");
    static_assert(MaxEvents > 0, "SdcDetector requires an event ring.");
    // An event names a slot and a replica count in 16 bits.
    static_assert(MaxCogs <= std::numeric_limits<std::uint16_t>::max(),
                  "SdcDetector slot indices and replica counts must fit the 16-bit event fields.");

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

    template <class Ctx, std::size_t Cogs, std::size_t Events>
        requires CtxFitsSdcMint<Ctx>
    friend constexpr SdcDetector<Cogs, Events> mint_sdc_detector(Ctx const&, SdcConfig config) noexcept;

    constexpr explicit SdcDetector(SdcConfig config) noexcept : config_{config} {}

    // The slot of an active Cog with this identity, or nullptr.
    [[nodiscard]] constexpr CogSlot const* find_cog(cog::CogIdentity const& cog) const noexcept {
        for (CogSlot const& slot : cogs_) {
            if (slot.active && slot.cog.uuid == cog.uuid) {
                return std::addressof(slot);
            }
        }
        return nullptr;
    }

    // Every event carries the strategy and tolerance it was judged under.
    [[nodiscard]] constexpr SdcEvent make_event(SdcEventKind kind, std::size_t compared_replicas) const noexcept {
        return SdcEvent{.kind = kind,
                        .strategy = config_.strategy,
                        .compared_replicas = static_cast<std::uint16_t>(compared_replicas),
                        .tolerance_units = config_.tolerance_units};
    }

    [[nodiscard]] SdcEvent record_event(SdcEvent event) noexcept {
        event.sequence = ++event_sequence_;
        events_[event_cursor_] = event;
        event_cursor_ = (event_cursor_ + 1u) % events_.size();
        return event;
    }

public:
    [[nodiscard]] SdcConfig config() const noexcept { return config_; }

    [[nodiscard]] std::span<const CogSlot, MaxCogs> cogs() const noexcept {
        return std::span<const CogSlot, MaxCogs>{cogs_};
    }

    [[nodiscard]] std::span<const SdcEvent, MaxEvents> events() const noexcept {
        return std::span<const SdcEvent, MaxEvents>{events_};
    }

    [[nodiscard]] std::size_t active_cog_count() const noexcept { return active_cogs_; }

    [[nodiscard]] constexpr bool should_sample(std::uint64_t sequence) const noexcept {
        std::uint64_t const bucket = detail::mix_sequence(sequence) % 1000000ull;
        return bucket < config_.sampling_rate_ppm.value();
    }

    [[nodiscard]] bool register_cog(cog::CogIdentity cog) noexcept {
        if (cog.uuid.is_zero() || find_cog(cog) != nullptr) {
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
    [[nodiscard]] auto run_with_redundancy(Ctx const&,
                                           Work&& work) noexcept(noexcept(std::forward<Work>(work)(cogs_[0].cog)))
        -> std::expected<SdcVerified<std::remove_cvref_t<decltype(std::forward<Work>(work)(cogs_[0].cog))>>, SdcEvent> {
        using Result = std::remove_cvref_t<decltype(std::forward<Work>(work)(cogs_[0].cog))>;
        static_assert(std::is_trivially_copyable_v<Result>,
                      "SdcDetector results must be trivially copyable for deterministic comparison.");

        std::size_t const required = config_.redundancy_factor.value();
        if (active_cogs_ < required) {
            return std::unexpected(record_event(make_event(SdcEventKind::InsufficientReplicas, active_cogs_)));
        }

        Work& body = work;
        Result primary = body(cogs_[0].cog);
        for (std::size_t i = 1; i < required; ++i) {
            Result replica = body(cogs_[i].cog);
            if (!detail::equivalent(primary, replica, config_.strategy, config_.tolerance_units)) {
                auto& primary_slot = cogs_[0];
                auto& comparison_slot = cogs_[i];
                // A count that wrapped to zero would clear a suspect Cog.
                primary_slot.mismatch_count = ::foundation::sat::add_sat(primary_slot.mismatch_count, std::uint16_t{1});
                comparison_slot.mismatch_count =
                    ::foundation::sat::add_sat(comparison_slot.mismatch_count, std::uint16_t{1});

                SdcEvent event = make_event(SdcEventKind::Mismatch, i + 1u);
                event.comparison_slot = static_cast<std::uint16_t>(i);
                event.mismatch_count = comparison_slot.mismatch_count;
                event.primary_cog = primary_slot.cog.uuid;
                event.comparison_cog = comparison_slot.cog.uuid;
                return std::unexpected(record_event(event));
            }
        }

        SdcEvent event = make_event(SdcEventKind::Verified, required);
        event.primary_cog = cogs_[0].cog.uuid;
        (void)record_event(event);
        return ::fixy::mint_tagged<::fixy::tags::source::SdcVerified>(primary);
    }

    [[nodiscard]] bool should_quarantine(cog::CogIdentity const& cog) const noexcept {
        CogSlot const* const slot = find_cog(cog);
        return slot != nullptr && slot->mismatch_count >= config_.suspect_after_mismatches.value();
    }

    [[nodiscard]] bool publish_latest(ObservationSnapshot& sink) const noexcept {
        if (event_sequence_ == 0) {
            return false;
        }
        std::size_t const cursor = event_cursor_ == 0 ? events_.size() - 1u : event_cursor_ - 1u;
        SdcEvent const& event = events_[cursor];
        record_observation(sink,
                           make_observation(ObservationKind::Metric, ObservationSource::Runtime, config_.metric_id_base,
                                            static_cast<std::uint64_t>(event.kind), event.sequence));
        return true;
    }
};

template <class Ctx, std::size_t MaxCogs, std::size_t MaxEvents>
    requires CtxFitsSdcMint<Ctx>
[[nodiscard]] constexpr SdcDetector<MaxCogs, MaxEvents> mint_sdc_detector(Ctx const&, SdcConfig config) noexcept {
    return SdcDetector<MaxCogs, MaxEvents>{config};
}

static_assert(CtxFitsSdcMint<::fixy::ColdInitCtx>);
static_assert(!CtxFitsSdcMint<::fixy::BgDrainCtx>);
static_assert(CtxFitsSdcRun<::fixy::BgDrainCtx>);
static_assert(!CtxFitsSdcRun<::fixy::HotFgCtx>);
static_assert(!std::is_constructible_v<SdcDetector<1, 1>, SdcConfig>);

}  // namespace crucible::observe
