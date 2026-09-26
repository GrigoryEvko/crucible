#pragma once

#include <crucible/cog/CogIdentity.h>
#include <crucible/cog/OpcodeLatencyTable.h>
#include <crucible/cog/TargetCaps.h>
#include <fixy/Ctx.h>
#include <fixy/Tagged.h>
#include <fixy/Tags.h>
#include <foundation/contracts/Decide.h>
#include <foundation/contracts/Pre.h>
#include <foundation/effects/Ctx.h>
#include <foundation/effects/Effect.h>
#include <foundation/effects/Row.h>
#include <foundation/reflect/Hash.h>

#include <concepts>
#include <cstdint>
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
struct CogMimic {
    static constexpr cog::CogKind kind = K;
    static constexpr cog::CogFamily family = cog::cog_family_v<K>;

    using CapsType = cog::caps_for_t<K>;
    using OpcodeTable = cog::OpcodeLatencyTable<K>;

    // Borrowed. The storage holding the identity must outlive every carrier
    // bound to it.
    cog::CogIdentity const* identity = nullptr;

    ::fixy::Tagged<CapsType, ::fixy::tags::source::Calibrated> calibrated_caps =
        ::fixy::mint_tagged<::fixy::tags::source::Calibrated>(CapsType{});

    OpcodeTable opcode_latency_table{};

    // The fold excludes firmware and BIOS revision. A binary built at one
    // Cog still runs at a same-SKU Cog on a later firmware revision, so
    // folding the revision in would split the cache for no gain.
    [[nodiscard]] constexpr std::uint64_t target_caps_class_hash() const noexcept {
        return detail::caps_class_projection<K>::fold(calibrated_caps.value());
    }

    // The identity hash covers firmware and BIOS revision, so this key
    // rotates on firmware drift while the federation key stays stable.
    [[nodiscard]] constexpr std::uint64_t cog_kernel_cache_key() const noexcept {
        // A P2900 pre() clause is silently skipped at consteval when its
        // predicate dereferences a pointer member. The macro form fires at
        // consteval and at runtime.
        CRUCIBLE_PRE(identity != nullptr);
        CRUCIBLE_PRE(::foundation::decide::is_non_zero(identity->uuid));
        return ::foundation::reflect::fmix64(target_caps_class_hash() ^ cog::content_hash(*identity));
    }

    [[nodiscard]] constexpr bool is_uncalibrated() const noexcept {
        return identity == nullptr || opcode_latency_table.empty();
    }
};

// Minting is permitted at calibration time or during background
// recalibration, so the context row must hold Init or Bg and nothing else
// admits it.
template <class Ctx, cog::CogKind K>
concept CtxFitsCogMimic =
    ::foundation::effects::IsExecCtx<Ctx>
    && (::foundation::decide::row_subset<::foundation::effects::Row<::foundation::effects::Effect::Init>,
                                         ::foundation::effects::row_type_of_t<Ctx>>()
        || ::foundation::decide::row_subset<::foundation::effects::Row<::foundation::effects::Effect::Bg>,
                                            ::foundation::effects::row_type_of_t<Ctx>>())
    && cog::IsMimicSubstrate<K> && cog::HasCaps<K> && cog::HasOpcodeTable<K> && detail::HasCogMimicProjection<K>;

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
    return CogMimic<K>{
        &identity,
        ::fixy::mint_tagged<::fixy::tags::source::Calibrated>(std::move(calibrated_caps)),
        std::move(opcodes),
    };
}

namespace detail::cog_mimic_self_test {

// Every Mimic substrate has caps, an opcode table and a projection, and
// its family is the one the cog layer assigns.
template <cog::CogKind K, cog::CogFamily Family>
inline constexpr bool substrate_is_complete_v = cog::IsMimicSubstrate<K> && cog::HasCaps<K> && cog::HasOpcodeTable<K>
                                             && HasCogMimicProjection<K> && CogMimic<K>::family == Family
                                             && CogMimic<K>::kind == K
                                             && std::is_trivially_destructible_v<CogMimic<K>>;

static_assert(substrate_is_complete_v<cog::CogKind::Gpu, cog::CogFamily::Compute>);
static_assert(substrate_is_complete_v<cog::CogKind::CpuCore, cog::CogFamily::Compute>);
static_assert(substrate_is_complete_v<cog::CogKind::CpuSocket, cog::CogFamily::Compute>);
static_assert(substrate_is_complete_v<cog::CogKind::NicPort, cog::CogFamily::Network>);
static_assert(substrate_is_complete_v<cog::CogKind::NvSwitch, cog::CogFamily::Network>);
static_assert(substrate_is_complete_v<cog::CogKind::DramChannel, cog::CogFamily::Memory>);

static_assert(!cog::IsMimicSubstrate<cog::CogKind::PsuRail>);
static_assert(!cog::IsMimicSubstrate<cog::CogKind::RackPsu>);
static_assert(!cog::IsMimicSubstrate<cog::CogKind::BmcSensor>);
static_assert(!cog::IsMimicSubstrate<cog::CogKind::Datacenter>);
static_assert(!cog::IsMimicSubstrate<cog::CogKind::Server>);
static_assert(!cog::IsMimicSubstrate<cog::CogKind::Rack>);

static_assert(
    [] {
        CogMimic<cog::CogKind::Gpu> m{};
        return m.identity == nullptr && m.opcode_latency_table.empty() && m.is_uncalibrated();
    }(),
    "Default CogMimic<Gpu> must be uncalibrated and identity-unbound.");

static_assert(
    [] {
        CogMimic<cog::CogKind::NicPort> a{};
        CogMimic<cog::CogKind::NicPort> b{};
        return a.is_uncalibrated() && a.target_caps_class_hash() == b.target_caps_class_hash();
    }(),
    "CogMimic<NicPort>::target_caps_class_hash diverged for identical default caps. DetSafe is violated.");

static_assert(
    [] {
        std::uint64_t const hashes[] = {
            CogMimic<cog::CogKind::Gpu>{}.target_caps_class_hash(),
            CogMimic<cog::CogKind::CpuCore>{}.target_caps_class_hash(),
            CogMimic<cog::CogKind::CpuSocket>{}.target_caps_class_hash(),
            CogMimic<cog::CogKind::NicPort>{}.target_caps_class_hash(),
            CogMimic<cog::CogKind::NvSwitch>{}.target_caps_class_hash(),
            CogMimic<cog::CogKind::DramChannel>{}.target_caps_class_hash(),
        };
        for (std::size_t i = 0; i < std::size(hashes); ++i) {
            for (std::size_t j = i + 1; j < std::size(hashes); ++j) {
                if (hashes[i] == hashes[j]) {
                    return false;
                }
            }
        }
        return true;
    }(),
    "Distinct CogKinds collided in target_caps_class_hash. The federation cache would alias kernels across "
    "substrates.");

static_assert(
    [] {
        cog::GpuTargetCaps hopper_caps{};
        hopper_caps.sm_version = ::fixy::mint_tagged<::fixy::tags::source::Vendor, std::uint16_t>(90);
        cog::GpuTargetCaps blackwell_caps{};
        blackwell_caps.sm_version = ::fixy::mint_tagged<::fixy::tags::source::Vendor, std::uint16_t>(100);
        return caps_class_projection<cog::CogKind::Gpu>::fold(hopper_caps)
            != caps_class_projection<cog::CogKind::Gpu>::fold(blackwell_caps);
    }(),
    "SM-version drift collapsed in target_caps_class_hash. The kernel cache would silently reuse Hopper kernels "
    "at Blackwell.");

static_assert(
    [] {
        cog::NicPortTargetCaps infiniband{};
        infiniband.link_layer =
            ::fixy::mint_tagged<::fixy::tags::source::Vendor, cog::LinkLayer>(cog::LinkLayer::Infiniband);
        cog::NicPortTargetCaps ethernet{};
        ethernet.link_layer = ::fixy::mint_tagged<::fixy::tags::source::Vendor, cog::LinkLayer>(cog::LinkLayer::Ethernet);
        return caps_class_projection<cog::CogKind::NicPort>::fold(infiniband)
            != caps_class_projection<cog::CogKind::NicPort>::fold(ethernet);
    }(),
    "Link-layer drift collapsed in NIC target_caps_class_hash. IB and Ethernet kernels would silently alias.");

static_assert(
    [] {
        cog::CogIdentity id_a{};
        id_a.uuid = cog::Uuid{0xDEAD0001ULL, 0xCAFE0002ULL};
        id_a.kind = cog::CogKind::Gpu;
        id_a.firmware_revision = ::fixy::mint_tagged<::fixy::tags::source::Vendor, std::uint64_t>(1);

        cog::CogIdentity id_b = id_a;
        id_b.firmware_revision = ::fixy::mint_tagged<::fixy::tags::source::Vendor, std::uint64_t>(2);

        CogMimic<cog::CogKind::Gpu> a{};
        a.identity = &id_a;
        CogMimic<cog::CogKind::Gpu> b{};
        b.identity = &id_b;
        return a.cog_kernel_cache_key() != b.cog_kernel_cache_key()
            && a.target_caps_class_hash() == b.target_caps_class_hash();
    }(),
    "Firmware drift must rotate cog_kernel_cache_key and must leave target_caps_class_hash stable.");

// Minting admits a context that owns Init or Bg.  A foreground or test
// context owns neither, and a type that is not a context never fits.
static_assert(CtxFitsCogMimic<::fixy::ColdInitCtx, cog::CogKind::Gpu>);
static_assert(CtxFitsCogMimic<::fixy::InitLoadCtx, cog::CogKind::DramChannel>);
static_assert(CtxFitsCogMimic<::fixy::BgDrainCtx, cog::CogKind::NicPort>);
static_assert(CtxFitsCogMimic<::fixy::BgLoadCtx, cog::CogKind::NvSwitch>);
static_assert(!CtxFitsCogMimic<::fixy::HotFgCtx, cog::CogKind::Gpu>);
static_assert(!CtxFitsCogMimic<::fixy::TestRunnerCtx, cog::CogKind::Gpu>);
static_assert(!CtxFitsCogMimic<::fixy::ColdInitCtx, cog::CogKind::PsuRail>);
static_assert(!CtxFitsCogMimic<::fixy::ColdInitCtx, cog::CogKind::BmcSensor>);
static_assert(!CtxFitsCogMimic<::fixy::ColdInitCtx, cog::CogKind::Datacenter>);
static_assert(!CtxFitsCogMimic<int, cog::CogKind::Gpu>);

}  // namespace detail::cog_mimic_self_test

}  // namespace crucible::mimic
