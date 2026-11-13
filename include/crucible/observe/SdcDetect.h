#pragma once

// SDC is silent data corruption.

#include <crucible/cog/CogIdentity.h>
#include <crucible/observe/Observation.h>
#include <fixy/Core.h>
#include <fixy/Ctx.h>
#include <fixy/Refined.h>
#include <fixy/Tagged.h>
#include <fixy/Tags.h>
#include <foundation/Pinned.h>
#include <foundation/Saturate.h>
#include <foundation/diag/Tag.h>
#include <foundation/effects/Ctx.h>
#include <foundation/effects/Row.h>

#include <cstddef>
#include <cstdint>
#include <string_view>
#include <type_traits>

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

// The bytes of a result, which the bitwise compare reads.
template <typename T>
using SdcResultBytes = ::foundation::core::FixedArray<std::byte, sizeof(T)>;

// A result that the detector can compare bit for bit: a plain value with no
// padding bit.  No replica writes a padding bit, so two equal results can
// differ there.
template <typename T>
concept SdcComparableResult = ::fixy::DecodableFrom<SdcResultBytes<T>, T>;

// The result of one replica of a work.
template <class Work>
using SdcWorkResult = std::remove_cvref_t<std::invoke_result_t<Work&, cog::CogIdentity&>>;

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

template <SdcComparableResult T>
[[nodiscard]] constexpr bool bitwise_equal(T const& a, T const& b) noexcept {
    return ::fixy::bit_cast<SdcResultBytes<T>>(a) == ::fixy::bit_cast<SdcResultBytes<T>>(b);
}

// An integer compares by its true distance, which abs_diff gives with no
// overflow at each width.  A float compares by the magnitude of its
// difference, so a NaN operand is never within the tolerance.  Each other
// result compares bit for bit.
template <SdcComparableResult T>
[[nodiscard]] constexpr bool tolerance_equal(T const& a, T const& b, std::uint64_t tolerance) noexcept {
    if constexpr (::fixy::Integer<T>) {
        return ::fixy::widen<std::uint64_t>(::fixy::abs_diff(a, b)) <= tolerance;
    } else if constexpr (::fixy::Float<T>) {
        T const delta = ::fixy::fp::abs(a - b);
        return delta <= static_cast<T>(tolerance);
    } else {
        return bitwise_equal(a, b);
    }
}

template <SdcComparableResult T>
[[nodiscard]] constexpr bool equivalent(T const& a, T const& b, SdcComparisonStrategy strategy,
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
    static_assert(MaxCogs <= ::fixy::max_of<std::uint16_t>,
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
    using CogIndex = ::fixy::Index<MaxCogs>;
    using EventIndex = ::fixy::Index<MaxEvents>;

    SdcConfig config_{};
    ::foundation::core::FixedArray<CogSlot, MaxCogs> cogs_{};
    ::foundation::core::FixedArray<SdcEvent, MaxEvents> events_{};
    std::uint64_t event_sequence_ = 0;
    // The slot of the next event.
    EventIndex event_cursor_ = ::fixy::index<0>();
    std::size_t active_cogs_ = 0;

    template <class Ctx, std::size_t Cogs, std::size_t Events>
        requires CtxFitsSdcMint<Ctx>
    friend constexpr SdcDetector<Cogs, Events> mint_sdc_detector(Ctx const&, SdcConfig config) noexcept;

    constexpr explicit SdcDetector(SdcConfig config) noexcept : config_{config} {}

    // The slot of an active Cog with this identity, or no value.
    [[nodiscard]] constexpr ::fixy::Option<CogIndex> find_cog(cog::CogIdentity const& cog) const noexcept {
        for (CogIndex const slot : cogs_.indices()) {
            CogSlot const& entry = cogs_.at(slot);
            if (entry.active && entry.cog.uuid == cog.uuid) {
                return ::fixy::Option<CogIndex>::some(slot);
            }
        }
        return ::fixy::none;
    }

    // The slot after cursor, and the first slot after the last one.
    [[nodiscard]] static constexpr EventIndex next_event_slot(EventIndex cursor) noexcept {
        return EventIndex::check(cursor.raw() + 1u).value_or(::fixy::index<0>());
    }

    // The slot before cursor, and the last slot before the first one.
    [[nodiscard]] static constexpr EventIndex previous_event_slot(EventIndex cursor) noexcept {
        if (cursor.raw() == 0) {
            return ::fixy::index<MaxEvents - 1>();
        }
        return EventIndex::check(cursor.raw() - 1u).value_or(::fixy::index<0>());
    }

    // Every event carries the strategy and tolerance it was judged under.
    [[nodiscard]] constexpr SdcEvent make_event(SdcEventKind kind, std::size_t compared_replicas) const noexcept {
        return SdcEvent{.kind = kind,
                        .strategy = config_.strategy,
                        .compared_replicas = static_cast<std::uint16_t>(compared_replicas),
                        .tolerance_units = config_.tolerance_units};
    }

    void record_event(SdcEvent event) noexcept {
        event.sequence = ++event_sequence_;
        events_.at(event_cursor_) = event;
        event_cursor_ = next_event_slot(event_cursor_);
    }

public:
    [[nodiscard]] SdcConfig config() const noexcept { return config_; }

    [[nodiscard]] ::fixy::View<CogSlot const, MaxCogs> cogs() const noexcept { return cogs_.view(); }

    [[nodiscard]] ::fixy::View<SdcEvent const, MaxEvents> events() const noexcept { return events_.view(); }

    // The last recorded event, or no value before the first event.
    [[nodiscard]] ::fixy::Option<SdcEvent> latest_event() const noexcept {
        if (event_sequence_ == 0) {
            return ::fixy::none;
        }
        return ::fixy::Option<SdcEvent>::some(events_.at(previous_event_slot(event_cursor_)));
    }

    [[nodiscard]] std::size_t active_cog_count() const noexcept { return active_cogs_; }

    [[nodiscard]] constexpr bool should_sample(std::uint64_t sequence) const noexcept {
        std::uint64_t const bucket = detail::mix_sequence(sequence) % 1000000ull;
        return bucket < config_.sampling_rate_ppm.value();
    }

    [[nodiscard]] bool register_cog(cog::CogIdentity cog) noexcept {
        if (cog.uuid.is_zero() || find_cog(cog).is_some()) {
            return false;
        }
        return CogIndex::check(active_cogs_)
            .match(
                [&](CogIndex slot) noexcept {
                    cogs_.at(slot) = CogSlot{.cog = cog, .mismatch_count = 0, .active = true};
                    ++active_cogs_;
                    return true;
                },
                [] noexcept { return false; });
    }

    // Runs work on the first redundancy_factor Cogs and compares each result
    // with the result of the first Cog.  The ring of events holds the full
    // event of each run, and an error gives its kind.
    template <class Ctx, class Work>
        requires CtxFitsSdcRun<Ctx> && SdcComparableResult<SdcWorkResult<Work>>
    [[nodiscard]] auto run_with_redundancy(Ctx const&,
                                           Work&& work) noexcept(std::is_nothrow_invocable_v<Work&, cog::CogIdentity&>)
        -> ::fixy::Result<SdcVerified<SdcWorkResult<Work>>, SdcEventKind> {
        using Result = SdcWorkResult<Work>;

        std::size_t const required = config_.redundancy_factor.value();
        if (active_cogs_ < required) {
            record_event(make_event(SdcEventKind::InsufficientReplicas, active_cogs_));
            return ::fixy::err(SdcEventKind::InsufficientReplicas);
        }

        Work& body = work;
        CogSlot& primary_slot = cogs_.at(::fixy::index<0>());
        Result primary = body(primary_slot.cog);
        for (CogIndex const slot : cogs_.indices()) {
            if (slot.raw() == 0) {
                continue;
            }
            if (slot.raw() >= required) {
                break;
            }
            CogSlot& comparison_slot = cogs_.at(slot);
            Result replica = body(comparison_slot.cog);
            if (!detail::equivalent(primary, replica, config_.strategy, config_.tolerance_units)) {
                // A count that wrapped to zero would clear a suspect Cog.
                primary_slot.mismatch_count = ::foundation::sat::add_sat(primary_slot.mismatch_count, std::uint16_t{1});
                comparison_slot.mismatch_count =
                    ::foundation::sat::add_sat(comparison_slot.mismatch_count, std::uint16_t{1});

                SdcEvent event = make_event(SdcEventKind::Mismatch, slot.raw() + 1u);
                event.comparison_slot = static_cast<std::uint16_t>(slot.raw());
                event.mismatch_count = comparison_slot.mismatch_count;
                event.primary_cog = primary_slot.cog.uuid;
                event.comparison_cog = comparison_slot.cog.uuid;
                record_event(event);
                return ::fixy::err(SdcEventKind::Mismatch);
            }
        }

        SdcEvent event = make_event(SdcEventKind::Verified, required);
        event.primary_cog = primary_slot.cog.uuid;
        record_event(event);
        return ::fixy::mint_tagged<::fixy::tags::source::SdcVerified>(primary);
    }

    [[nodiscard]] bool should_quarantine(cog::CogIdentity const& cog) const noexcept {
        return find_cog(cog).match(
            [this](CogIndex slot) noexcept {
                return cogs_.at(slot).mismatch_count >= config_.suspect_after_mismatches.value();
            },
            [] noexcept { return false; });
    }

    [[nodiscard]] bool publish_latest(ObservationSnapshot& sink) const noexcept {
        return latest_event().match(
            [&](SdcEvent event) noexcept {
                record_observation(sink, make_observation(ObservationKind::Metric, ObservationSource::Runtime,
                                                          config_.metric_id_base,
                                                          static_cast<std::uint64_t>(event.kind), event.sequence));
                return true;
            },
            [] noexcept { return false; });
    }
};

template <class Ctx, std::size_t MaxCogs, std::size_t MaxEvents>
    requires CtxFitsSdcMint<Ctx>
[[nodiscard]] constexpr SdcDetector<MaxCogs, MaxEvents> mint_sdc_detector(Ctx const&, SdcConfig config) noexcept {
    return SdcDetector<MaxCogs, MaxEvents>{config};
}

}  // namespace crucible::observe
