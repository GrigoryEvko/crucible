#pragma once

#include <crucible/cog/CogIdentity.h>
#include <crucible/cog/OpcodeLatencyTable.h>
#include <crucible/cog/TargetCaps.h>
#include <fixy/Ctx.h>
#include <fixy/Tagged.h>
#include <fixy/Tags.h>
#include <foundation/Lifetime.h>
#include <foundation/Platform.h>
#include <foundation/contracts/Decide.h>
#include <foundation/contracts/Pre.h>
#include <foundation/effects/Ctx.h>
#include <foundation/effects/Effect.h>
#include <foundation/effects/Row.h>
#include <foundation/reflect/EnumName.h>
#include <foundation/reflect/Hash.h>

#include <array>
#include <concepts>
#include <cstddef>
#include <cstdint>
#include <meta>
#include <type_traits>
#include <utility>

namespace crucible::mimic {

// Two Cogs whose projections agree may share compiled binaries.
// Disagreement forces a per-Cog recompile.
//
// Calibrated throughput measurements are deliberately not folded. They vary
// across same-SKU Cogs because of manufacturing spread, yet a binary built
// for one such Cog still runs on another. Folding them would split the cache
// on noise.

namespace detail {

// Seeding the high byte with the kind keeps distinct kinds apart even when
// their per-K folds collide on numeric content.  Each word then passes
// through the one fmix64 finalizer of the tree.
template <class... Words>
[[nodiscard]] constexpr std::uint64_t fold_caps_words(cog::CogKind kind, Words... words) noexcept {
    std::uint64_t h = static_cast<std::uint64_t>(kind) << 56;
    ((h = ::foundation::reflect::fmix64(h ^ static_cast<std::uint64_t>(words))), ...);
    return h;
}

// The primary has no body, so a kind with no projection below has no
// fold, and HasCogMimicProjection refuses it.
template <cog::CogKind K>
struct caps_class_projection;

template <>
struct caps_class_projection<cog::CogKind::Gpu> {
    [[nodiscard]] static constexpr std::uint64_t fold(cog::GpuTargetCaps const& c) noexcept {
        return fold_caps_words(cog::CogKind::Gpu, c.sm_version.value(), c.sm_count.value(), c.hbm_bytes.value(),
                               c.features.raw());
    }
};

template <>
struct caps_class_projection<cog::CogKind::CpuCore> {
    [[nodiscard]] static constexpr std::uint64_t fold(cog::CpuCoreTargetCaps const& c) noexcept {
        return fold_caps_words(cog::CogKind::CpuCore, c.base_clock_mhz.value(), c.l2_bytes.value(), c.features.raw());
    }
};

template <>
struct caps_class_projection<cog::CogKind::CpuSocket> {
    [[nodiscard]] static constexpr std::uint64_t fold(cog::CpuSocketTargetCaps const& c) noexcept {
        return fold_caps_words(cog::CogKind::CpuSocket, c.core_count.value(), c.l3_bytes.value(), c.features.raw());
    }
};

template <>
struct caps_class_projection<cog::CogKind::NicPort> {
    // Every folded field changes the emitted work-request shape. The link
    // layer selects the emitter. The line rate sets pacing. The queue-pair
    // ceiling sets allocation shape. The MTU sets segmentation policy.
    [[nodiscard]] static constexpr std::uint64_t fold(cog::NicPortTargetCaps const& c) noexcept {
        return fold_caps_words(cog::CogKind::NicPort, c.link_layer.value(), c.line_rate_bytes_per_sec.value(),
                               c.max_qp_count.value(), c.mtu_bytes.value(), c.features.raw());
    }
};

template <>
struct caps_class_projection<cog::CogKind::NvSwitch> {
    // The port count fixes which fabric topology can be realised. Per-port
    // bandwidth sets pacing. The TCAM entry count bounds what the
    // access-control program can express.
    [[nodiscard]] static constexpr std::uint64_t fold(cog::NvSwitchTargetCaps const& c) noexcept {
        return fold_caps_words(cog::CogKind::NvSwitch, c.port_count.value(), c.per_port_bandwidth_bytes_per_sec.value(),
                               c.tcam_entries.value(), c.features.raw());
    }
};

template <>
struct caps_class_projection<cog::CogKind::DramChannel> {
    // Channel width sets the interleaving strategy. Speed sets refresh and
    // activate timing. Capacity sets the page-allocation shape.
    [[nodiscard]] static constexpr std::uint64_t fold(cog::DramChannelTargetCaps const& c) noexcept {
        return fold_caps_words(cog::CogKind::DramChannel, c.channel_width_bits.value(), c.speed_mts.value(),
                               c.capacity_bytes.value(), c.features.raw());
    }
};

template <cog::CogKind K>
concept HasCogMimicProjection = requires(cog::caps_for_t<K> const& caps) {
    { caps_class_projection<K>::fold(caps) } -> std::same_as<std::uint64_t>;
};

}  // namespace detail

template <cog::CogKind K>
    requires cog::IsMimicSubstrate<K> && cog::HasCaps<K> && cog::HasOpcodeTable<K> && detail::HasCogMimicProjection<K>
class CogMimic;

// Minting is permitted at calibration time or during background
// recalibration, so the context row must hold Init or Bg and nothing else
// admits it.
template <class Ctx, cog::CogKind K>
concept CtxFitsCogMimic =
    ::foundation::effects::IsExecCtx<Ctx>
    && (::foundation::decide::row_subset(^^::foundation::effects::Row<::foundation::effects::Effect::Init>,
                                         ^^::foundation::effects::row_type_of_t<Ctx>)
        || ::foundation::decide::row_subset(^^::foundation::effects::Row<::foundation::effects::Effect::Bg>,
                                            ^^::foundation::effects::row_type_of_t<Ctx>))
    && cog::IsMimicSubstrate<K> && cog::HasCaps<K> && cog::HasOpcodeTable<K> && detail::HasCogMimicProjection<K>;

// The one door to a CogMimic.  Declared here so that the class can name it
// as the friend of its only constructor.  Defined after the class.
template <cog::CogKind K, ::foundation::effects::IsExecCtx Ctx>
    requires CtxFitsCogMimic<Ctx, K>
[[nodiscard]] constexpr CogMimic<K>
mint_cog_mimic(Ctx const& ctx, cog::CogIdentity const& identity CRUCIBLE_LIFETIMEBOUND,
               cog::caps_for_t<K> calibrated_caps, cog::OpcodeLatencyTable<K> opcodes) noexcept;

// The CogMimic borrows its identity, so an identity that is a temporary
// would dangle at the end of the full expression.  This form is the better
// match for a temporary, and it is deleted.
template <cog::CogKind K, ::foundation::effects::IsExecCtx Ctx>
    requires CtxFitsCogMimic<Ctx, K>
constexpr CogMimic<K> mint_cog_mimic(Ctx const& ctx, cog::CogIdentity const&& identity,
                                     cog::caps_for_t<K> calibrated_caps, cog::OpcodeLatencyTable<K> opcodes) noexcept =
    delete("the CogMimic borrows its identity, so a temporary dangles at the end of the full expression; "
           "bind the identity to a name first");

// An identity bound to the caps and the opcode table that calibration
// measured for it.  The constructor is private and mint_cog_mimic is its
// one friend, so every CogMimic passed the context gate of the mint, and
// no caller binds an identity to caps that no calibration produced.  There
// is no default constructor, so no CogMimic is unbound.
//
// The two assignments are user-provided, so the class is not trivially
// copyable and std::bit_cast cannot build one from bytes.  The annotation
// refuses the checked lifetime start over bytes.  The copy and move
// constructors stay trivial, so a minted value still travels in registers.
template <cog::CogKind K>
    requires cog::IsMimicSubstrate<K> && cog::HasCaps<K> && cog::HasOpcodeTable<K> && detail::HasCogMimicProjection<K>
class [[nodiscard]][[= ::foundation::lifetime::no_start_over_bytes{}]] CogMimic {
public:
    static constexpr cog::CogKind kind = K;
    static constexpr cog::CogFamily family = cog::cog_family_v<K>;

    using CapsType = cog::caps_for_t<K>;
    using CalibratedCaps = ::fixy::Tagged<CapsType, ::fixy::tags::source::Calibrated>;
    using OpcodeTable = cog::OpcodeLatencyTable<K>;

    CogMimic() = delete("a CogMimic binds a calibrated identity; take one from mint_cog_mimic");
    constexpr CogMimic(const CogMimic&) noexcept = default;
    constexpr CogMimic(CogMimic&&) noexcept = default;

    constexpr CogMimic& operator=(const CogMimic& other) noexcept {
        identity_ = other.identity_;
        calibrated_caps_ = other.calibrated_caps_;
        opcode_latency_table_ = other.opcode_latency_table_;
        return *this;
    }
    constexpr CogMimic& operator=(CogMimic&& other) noexcept {
        identity_ = other.identity_;
        calibrated_caps_ = std::move(other.calibrated_caps_);
        opcode_latency_table_ = std::move(other.opcode_latency_table_);
        return *this;
    }

    // Borrowed.  The storage that holds the identity must outlive every
    // carrier bound to it.
    [[nodiscard]] constexpr cog::CogIdentity const& identity() const noexcept { return *identity_; }
    [[nodiscard]] constexpr CalibratedCaps const& calibrated_caps() const noexcept { return calibrated_caps_; }
    [[nodiscard]] constexpr OpcodeTable const& opcode_latency_table() const noexcept { return opcode_latency_table_; }

    // The fold excludes firmware and BIOS revision. A binary built at one
    // Cog still runs at a same-SKU Cog on a later firmware revision, so
    // folding the revision in would split the cache for no gain.
    [[nodiscard]] constexpr std::uint64_t target_caps_class_hash() const noexcept {
        return detail::caps_class_projection<K>::fold(calibrated_caps_.value());
    }

    // The identity hash covers firmware and BIOS revision, so this key
    // rotates on firmware drift while the federation key stays stable.
    [[nodiscard]] constexpr std::uint64_t cog_kernel_cache_key() const noexcept {
        // The mint refused a zero uuid, but the owner of the identity can
        // change it later.  A P2900 pre() clause is silently skipped at
        // consteval when its predicate reads through a pointer member, and
        // the macro form fires at consteval and at runtime.
        CRUCIBLE_PRE(::foundation::decide::is_non_zero(identity_->uuid));
        return ::foundation::reflect::fmix64(target_caps_class_hash() ^ cog::content_hash(*identity_));
    }

    // The identity is always bound, so an empty opcode table is the one
    // mark of a CogMimic that calibration has not filled.
    [[nodiscard]] constexpr bool is_uncalibrated() const noexcept { return opcode_latency_table_.empty(); }

private:
    constexpr CogMimic(cog::CogIdentity const& identity, CalibratedCaps calibrated_caps, OpcodeTable opcodes) noexcept
        : identity_{&identity},
          calibrated_caps_{std::move(calibrated_caps)},
          opcode_latency_table_{std::move(opcodes)} {}

    template <cog::CogKind Kind, ::foundation::effects::IsExecCtx Ctx>
        requires CtxFitsCogMimic<Ctx, Kind>
    friend constexpr CogMimic<Kind> mint_cog_mimic(Ctx const& ctx, cog::CogIdentity const& identity,
                                                   cog::caps_for_t<Kind> calibrated_caps,
                                                   cog::OpcodeLatencyTable<Kind> opcodes) noexcept;

    cog::CogIdentity const* identity_;
    CalibratedCaps calibrated_caps_;
    OpcodeTable opcode_latency_table_;
};

// A P2900 pre() clause is silently skipped at consteval when its predicate
// reads through a by-const-reference struct parameter. The macro form fires
// at consteval and at runtime.
template <cog::CogKind K, ::foundation::effects::IsExecCtx Ctx>
    requires CtxFitsCogMimic<Ctx, K>
[[nodiscard]] constexpr CogMimic<K> mint_cog_mimic(Ctx const& /* ctx */, cog::CogIdentity const& identity,
                                                   cog::caps_for_t<K> calibrated_caps,
                                                   cog::OpcodeLatencyTable<K> opcodes) noexcept {
    CRUCIBLE_PRE(::foundation::decide::is_non_zero(identity.uuid));
    CRUCIBLE_PRE(identity.kind == K);
    return CogMimic<K>{identity, ::fixy::mint_tagged<::fixy::tags::source::Calibrated>(std::move(calibrated_caps)),
                       std::move(opcodes)};
}

}  // namespace crucible::mimic
