#pragma once

#include <crucible/cog/CogIdentity.h>
#include <crucible/cog/OpcodeLatencyTable.h>
#include <crucible/cog/TargetCaps.h>
#include <crucible/effects/_Capabilities.h>
#include <crucible/effects/_EffectRow.h>
#include <crucible/effects/_ExecCtx.h>
#include <crucible/safety/Decide.h>
#include <crucible/safety/Pre.h>
#include <crucible/safety/Tagged.h>

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

// xxHash final mix.
[[nodiscard]] constexpr std::uint64_t cog_mimic_fmix64(std::uint64_t h) noexcept {
    h ^= h >> 33;
    h *= 0xFF51AFD7ED558CCDULL;
    h ^= h >> 33;
    h *= 0xC4CEB9FE1A85EC53ULL;
    h ^= h >> 33;
    return h;
}

// Seeding the high byte with the kind keeps distinct kinds apart even when
// their per-K folds collide on numeric content.
[[nodiscard]] constexpr std::uint64_t cog_mimic_kind_seed(cog::CogKind K) noexcept {
    return static_cast<std::uint64_t>(K) << 56;
}

template <cog::CogKind K>
struct caps_class_projection;

template <>
struct caps_class_projection<cog::CogKind::Gpu> {
    [[nodiscard]] static constexpr std::uint64_t fold(cog::GpuTargetCaps const& c) noexcept {
        std::uint64_t h = cog_mimic_kind_seed(cog::CogKind::Gpu);
        h = cog_mimic_fmix64(h ^ static_cast<std::uint64_t>(c.sm_version.value()));
        h = cog_mimic_fmix64(h ^ static_cast<std::uint64_t>(c.sm_count.value()));
        h = cog_mimic_fmix64(h ^ c.hbm_bytes.value());
        h = cog_mimic_fmix64(h ^ static_cast<std::uint64_t>(c.features.raw()));
        return h;
    }
};

template <>
struct caps_class_projection<cog::CogKind::CpuCore> {
    [[nodiscard]] static constexpr std::uint64_t fold(cog::CpuCoreTargetCaps const& c) noexcept {
        std::uint64_t h = cog_mimic_kind_seed(cog::CogKind::CpuCore);
        h = cog_mimic_fmix64(h ^ static_cast<std::uint64_t>(c.base_clock_mhz.value()));
        h = cog_mimic_fmix64(h ^ static_cast<std::uint64_t>(c.l2_bytes.value()));
        h = cog_mimic_fmix64(h ^ static_cast<std::uint64_t>(c.features.raw()));
        return h;
    }
};

template <>
struct caps_class_projection<cog::CogKind::CpuSocket> {
    [[nodiscard]] static constexpr std::uint64_t fold(cog::CpuSocketTargetCaps const& c) noexcept {
        std::uint64_t h = cog_mimic_kind_seed(cog::CogKind::CpuSocket);
        h = cog_mimic_fmix64(h ^ static_cast<std::uint64_t>(c.core_count.value()));
        h = cog_mimic_fmix64(h ^ c.l3_bytes.value());
        h = cog_mimic_fmix64(h ^ static_cast<std::uint64_t>(c.features.raw()));
        return h;
    }
};

template <>
struct caps_class_projection<cog::CogKind::NicPort> {
    [[nodiscard]] static constexpr std::uint64_t fold(cog::NicPortTargetCaps const& c) noexcept {
        // Every folded field changes the emitted work-request shape. The
        // link layer selects the emitter. The line rate sets pacing. The
        // queue-pair ceiling sets allocation shape. The MTU sets
        // segmentation policy.
        std::uint64_t h = cog_mimic_kind_seed(cog::CogKind::NicPort);
        h = cog_mimic_fmix64(h ^ static_cast<std::uint64_t>(c.link_layer.value()));
        h = cog_mimic_fmix64(h ^ c.line_rate_bytes_per_sec.value());
        h = cog_mimic_fmix64(h ^ static_cast<std::uint64_t>(c.max_qp_count.value()));
        h = cog_mimic_fmix64(h ^ static_cast<std::uint64_t>(c.mtu_bytes.value()));
        h = cog_mimic_fmix64(h ^ static_cast<std::uint64_t>(c.features.raw()));
        return h;
    }
};

template <>
struct caps_class_projection<cog::CogKind::NvSwitch> {
    [[nodiscard]] static constexpr std::uint64_t fold(cog::NvSwitchTargetCaps const& c) noexcept {
        // The port count fixes which fabric topology can be realised.
        // Per-port bandwidth sets pacing. The TCAM entry count bounds what
        // the access-control program can express.
        std::uint64_t h = cog_mimic_kind_seed(cog::CogKind::NvSwitch);
        h = cog_mimic_fmix64(h ^ static_cast<std::uint64_t>(c.port_count.value()));
        h = cog_mimic_fmix64(h ^ c.per_port_bandwidth_bytes_per_sec.value());
        h = cog_mimic_fmix64(h ^ static_cast<std::uint64_t>(c.tcam_entries.value()));
        h = cog_mimic_fmix64(h ^ static_cast<std::uint64_t>(c.features.raw()));
        return h;
    }
};

template <>
struct caps_class_projection<cog::CogKind::DramChannel> {
    [[nodiscard]] static constexpr std::uint64_t fold(cog::DramChannelTargetCaps const& c) noexcept {
        // Channel width sets the interleaving strategy. Speed sets refresh
        // and activate timing. Capacity sets the page-allocation shape.
        std::uint64_t h = cog_mimic_kind_seed(cog::CogKind::DramChannel);
        h = cog_mimic_fmix64(h ^ static_cast<std::uint64_t>(c.channel_width_bits.value()));
        h = cog_mimic_fmix64(h ^ static_cast<std::uint64_t>(c.speed_mts.value()));
        h = cog_mimic_fmix64(h ^ c.capacity_bytes.value());
        h = cog_mimic_fmix64(h ^ static_cast<std::uint64_t>(c.features.raw()));
        return h;
    }
};

template <cog::CogKind K, class = void>
struct has_cog_mimic_projection_v_impl : std::false_type {};

template <cog::CogKind K>
struct has_cog_mimic_projection_v_impl<
    K, std::void_t<decltype(caps_class_projection<K>::fold(std::declval<cog::caps_for_t<K> const&>()))>>
    : std::true_type {};

template <cog::CogKind K>
inline constexpr bool has_cog_mimic_projection_v = has_cog_mimic_projection_v_impl<K>::value;

}  // namespace detail

template <cog::CogKind K>
    requires cog::IsMimicSubstrate<K> && cog::HasCaps<K> && cog::HasOpcodeTable<K>
          && detail::has_cog_mimic_projection_v<K>
struct CogMimic {
    static constexpr cog::CogKind kind = K;
    static constexpr cog::CogFamily family = cog::cog_family_v<K>;

    using CapsType = cog::caps_for_t<K>;
    using OpcodeTable = cog::OpcodeLatencyTable<K>;

    // Borrowed. The storage holding the identity must outlive every carrier
    // bound to it.
    cog::CogIdentity const* identity = nullptr;

    safety::Tagged<CapsType, safety::source::Calibrated> calibrated_caps{CapsType{}};

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
        CRUCIBLE_PRE(crucible::decide::is_non_zero(identity->uuid));
        std::uint64_t federation = target_caps_class_hash();
        std::uint64_t cog_local = cog::content_hash(*identity);
        return detail::cog_mimic_fmix64(federation ^ cog_local);
    }

    [[nodiscard]] constexpr bool is_uncalibrated() const noexcept {
        return identity == nullptr || opcode_latency_table.empty();
    }
};

// Minting is permitted at calibration time or during background
// recalibration, so the row conjunct admits Init or Bg and nothing else.
template <class Ctx, cog::CogKind K>
concept CtxFitsCogMimic =
    effects::IsExecCtx<Ctx> && cog::IsMimicSubstrate<K> && cog::HasCaps<K> && cog::HasOpcodeTable<K>
    && detail::has_cog_mimic_projection_v<K>
    && (crucible::decide::row_subset<effects::Row<effects::Effect::Init>, effects::row_type_of_t<Ctx>>()
        || crucible::decide::row_subset<effects::Row<effects::Effect::Bg>, effects::row_type_of_t<Ctx>>());

// A P2900 pre() clause is silently skipped at consteval when its predicate
// reads through a by-const-reference struct parameter. The macro form fires
// at consteval and at runtime.
template <cog::CogKind K, effects::IsExecCtx Ctx>
    requires CtxFitsCogMimic<Ctx, K>
[[nodiscard]] constexpr CogMimic<K> mint_cog_mimic(Ctx const& /* ctx */, cog::CogIdentity const& identity,
                                                   cog::caps_for_t<K> calibrated_caps,
                                                   cog::OpcodeLatencyTable<K> opcodes) noexcept {
    CRUCIBLE_PRE(crucible::decide::is_non_zero(identity.uuid));
    CRUCIBLE_PRE(identity.kind == K);
    return CogMimic<K>{
        &identity,
        safety::Tagged<cog::caps_for_t<K>, safety::source::Calibrated>{std::move(calibrated_caps)},
        std::move(opcodes),
    };
}

namespace detail::cog_mimic_self_test {

static_assert(cog::IsMimicSubstrate<cog::CogKind::Gpu>);
static_assert(cog::IsMimicSubstrate<cog::CogKind::CpuCore>);
static_assert(cog::IsMimicSubstrate<cog::CogKind::CpuSocket>);
static_assert(cog::IsMimicSubstrate<cog::CogKind::NicPort>);
static_assert(cog::IsMimicSubstrate<cog::CogKind::NvSwitch>);
static_assert(cog::IsMimicSubstrate<cog::CogKind::DramChannel>);

static_assert(cog::HasCaps<cog::CogKind::Gpu>);
static_assert(cog::HasCaps<cog::CogKind::CpuCore>);
static_assert(cog::HasCaps<cog::CogKind::CpuSocket>);
static_assert(cog::HasCaps<cog::CogKind::NicPort>);
static_assert(cog::HasCaps<cog::CogKind::NvSwitch>);
static_assert(cog::HasCaps<cog::CogKind::DramChannel>);
static_assert(cog::HasOpcodeTable<cog::CogKind::Gpu>);
static_assert(cog::HasOpcodeTable<cog::CogKind::CpuCore>);
static_assert(cog::HasOpcodeTable<cog::CogKind::CpuSocket>);
static_assert(cog::HasOpcodeTable<cog::CogKind::NicPort>);
static_assert(cog::HasOpcodeTable<cog::CogKind::NvSwitch>);
static_assert(cog::HasOpcodeTable<cog::CogKind::DramChannel>);

static_assert(detail::has_cog_mimic_projection_v<cog::CogKind::Gpu>);
static_assert(detail::has_cog_mimic_projection_v<cog::CogKind::CpuCore>);
static_assert(detail::has_cog_mimic_projection_v<cog::CogKind::CpuSocket>);
static_assert(detail::has_cog_mimic_projection_v<cog::CogKind::NicPort>);
static_assert(detail::has_cog_mimic_projection_v<cog::CogKind::NvSwitch>);
static_assert(detail::has_cog_mimic_projection_v<cog::CogKind::DramChannel>);

static_assert(!cog::IsMimicSubstrate<cog::CogKind::PsuRail>);
static_assert(!cog::IsMimicSubstrate<cog::CogKind::RackPsu>);
static_assert(!cog::IsMimicSubstrate<cog::CogKind::BmcSensor>);
static_assert(!cog::IsMimicSubstrate<cog::CogKind::Datacenter>);
static_assert(!cog::IsMimicSubstrate<cog::CogKind::Server>);
static_assert(!cog::IsMimicSubstrate<cog::CogKind::Rack>);

static_assert(CogMimic<cog::CogKind::Gpu>::family == cog::CogFamily::Compute);
static_assert(CogMimic<cog::CogKind::CpuCore>::family == cog::CogFamily::Compute);
static_assert(CogMimic<cog::CogKind::CpuSocket>::family == cog::CogFamily::Compute);
static_assert(CogMimic<cog::CogKind::NicPort>::family == cog::CogFamily::Network);
static_assert(CogMimic<cog::CogKind::NvSwitch>::family == cog::CogFamily::Network);
static_assert(CogMimic<cog::CogKind::DramChannel>::family == cog::CogFamily::Memory);

static_assert(std::is_trivially_destructible_v<CogMimic<cog::CogKind::Gpu>>,
              "CogMimic<Gpu> must be trivially destructible. It owns no heap.");
static_assert(std::is_trivially_destructible_v<CogMimic<cog::CogKind::CpuCore>>);
static_assert(std::is_trivially_destructible_v<CogMimic<cog::CogKind::CpuSocket>>);
static_assert(std::is_trivially_destructible_v<CogMimic<cog::CogKind::NicPort>>);
static_assert(std::is_trivially_destructible_v<CogMimic<cog::CogKind::NvSwitch>>);
static_assert(std::is_trivially_destructible_v<CogMimic<cog::CogKind::DramChannel>>);

static_assert(
    [] {
        CogMimic<cog::CogKind::Gpu> m{};
        return m.identity == nullptr && m.opcode_latency_table.empty() && m.is_uncalibrated();
    }(),
    "Default CogMimic<Gpu> must be uncalibrated and identity-unbound.");

static_assert(
    [] {
        CogMimic<cog::CogKind::NicPort> m{};
        return m.identity == nullptr && m.is_uncalibrated();
    }(),
    "Default CogMimic<NicPort> must be uncalibrated.");

static_assert(
    [] {
        CogMimic<cog::CogKind::Gpu> a{};
        CogMimic<cog::CogKind::Gpu> b{};
        return a.target_caps_class_hash() == b.target_caps_class_hash();
    }(),
    "CogMimic<Gpu>::target_caps_class_hash diverged for identical default "
    "caps. DetSafe is violated.");

static_assert(
    [] {
        CogMimic<cog::CogKind::NicPort> a{};
        CogMimic<cog::CogKind::NicPort> b{};
        return a.target_caps_class_hash() == b.target_caps_class_hash();
    }(),
    "CogMimic<NicPort>::target_caps_class_hash diverged for identical "
    "default caps. DetSafe is violated.");

static_assert(
    [] {
        CogMimic<cog::CogKind::Gpu> g{};
        CogMimic<cog::CogKind::CpuCore> c{};
        CogMimic<cog::CogKind::CpuSocket> s{};
        CogMimic<cog::CogKind::NicPort> n{};
        CogMimic<cog::CogKind::NvSwitch> sw{};
        CogMimic<cog::CogKind::DramChannel> d{};
        auto hg = g.target_caps_class_hash();
        auto hc = c.target_caps_class_hash();
        auto hs = s.target_caps_class_hash();
        auto hn = n.target_caps_class_hash();
        auto hsw = sw.target_caps_class_hash();
        auto hd = d.target_caps_class_hash();
        return hg != hc && hg != hs && hg != hn && hg != hsw && hg != hd && hc != hs && hc != hn && hc != hsw
            && hc != hd && hs != hn && hs != hsw && hs != hd && hn != hsw && hn != hd && hsw != hd;
    }(),
    "Distinct CogKinds collided in target_caps_class_hash. The federation "
    "cache would alias kernels across substrates.");

static_assert(
    [] {
        CogMimic<cog::CogKind::Gpu> hopper{};
        hopper.calibrated_caps.value_mut().sm_version =
            safety::Tagged<std::uint16_t, safety::source::Vendor>{std::uint16_t{90}};

        CogMimic<cog::CogKind::Gpu> blackwell{};
        blackwell.calibrated_caps.value_mut().sm_version =
            safety::Tagged<std::uint16_t, safety::source::Vendor>{std::uint16_t{100}};
        return hopper.target_caps_class_hash() != blackwell.target_caps_class_hash();
    }(),
    "SM-version drift collapsed in target_caps_class_hash. The kernel "
    "cache would silently reuse Hopper kernels at Blackwell.");

static_assert(
    [] {
        CogMimic<cog::CogKind::NicPort> infiniband{};
        infiniband.calibrated_caps.value_mut().link_layer =
            safety::Tagged<cog::LinkLayer, safety::source::Vendor>{cog::LinkLayer::Infiniband};

        CogMimic<cog::CogKind::NicPort> ethernet{};
        ethernet.calibrated_caps.value_mut().link_layer =
            safety::Tagged<cog::LinkLayer, safety::source::Vendor>{cog::LinkLayer::Ethernet};
        return infiniband.target_caps_class_hash() != ethernet.target_caps_class_hash();
    }(),
    "Link-layer drift collapsed in NIC target_caps_class_hash. IB and "
    "Ethernet kernels would silently alias.");

static_assert(
    [] {
        cog::CogIdentity id_a{};
        id_a.uuid = cog::Uuid{0xDEAD0001ULL, 0xCAFE0002ULL};
        id_a.kind = cog::CogKind::Gpu;
        id_a.firmware_revision = safety::Tagged<std::uint64_t, safety::source::Vendor>{1};

        cog::CogIdentity id_b = id_a;
        id_b.firmware_revision = safety::Tagged<std::uint64_t, safety::source::Vendor>{2};

        CogMimic<cog::CogKind::Gpu> a{};
        a.identity = &id_a;
        CogMimic<cog::CogKind::Gpu> b{};
        b.identity = &id_b;
        return a.cog_kernel_cache_key() != b.cog_kernel_cache_key()
            && a.target_caps_class_hash() == b.target_caps_class_hash();
    }(),
    "Firmware drift must rotate cog_kernel_cache_key and must leave "
    "target_caps_class_hash stable.");

using InitCtx =
    effects::ExecCtx<effects::Init, effects::ctx_numa::Any, effects::ctx_alloc::Unbound, effects::ctx_heat::Cold,
                     effects::ctx_resid::DRAM, effects::Row<effects::Effect::Init>, effects::ctx_workload::Unspecified>;

using BgCtx = effects::ExecCtx<effects::Bg, effects::ctx_numa::Any, effects::ctx_alloc::Arena, effects::ctx_heat::Warm,
                               effects::ctx_resid::L3, effects::Row<effects::Effect::Bg, effects::Effect::Alloc>,
                               effects::ctx_workload::Unspecified>;

static_assert(CtxFitsCogMimic<InitCtx, cog::CogKind::Gpu>);
static_assert(CtxFitsCogMimic<InitCtx, cog::CogKind::CpuCore>);
static_assert(CtxFitsCogMimic<InitCtx, cog::CogKind::CpuSocket>);
static_assert(CtxFitsCogMimic<InitCtx, cog::CogKind::NicPort>);
static_assert(CtxFitsCogMimic<InitCtx, cog::CogKind::NvSwitch>);
static_assert(CtxFitsCogMimic<InitCtx, cog::CogKind::DramChannel>);

static_assert(CtxFitsCogMimic<BgCtx, cog::CogKind::Gpu>);
static_assert(CtxFitsCogMimic<BgCtx, cog::CogKind::NicPort>);
static_assert(CtxFitsCogMimic<BgCtx, cog::CogKind::DramChannel>);

// One ctx cannot carry both Init and Bg in its effect row. ExecCtx refuses
// that pairing on its own. The disjunction admits either ctx, never one ctx
// carrying both.

using FgCtx =
    effects::ExecCtx<effects::ctx_cap::Fg, effects::ctx_numa::Any, effects::ctx_alloc::Stack, effects::ctx_heat::Hot,
                     effects::ctx_resid::L1, effects::Row<>, effects::ctx_workload::Unspecified>;
static_assert(!CtxFitsCogMimic<FgCtx, cog::CogKind::Gpu>);

using TestCtx =
    effects::ExecCtx<effects::Test, effects::ctx_numa::Any, effects::ctx_alloc::Stack, effects::ctx_heat::Cold,
                     effects::ctx_resid::DRAM, effects::Row<effects::Effect::Test>, effects::ctx_workload::Unspecified>;
static_assert(!CtxFitsCogMimic<TestCtx, cog::CogKind::Gpu>);

static_assert(!CtxFitsCogMimic<InitCtx, cog::CogKind::PsuRail>);
static_assert(!CtxFitsCogMimic<InitCtx, cog::CogKind::BmcSensor>);
static_assert(!CtxFitsCogMimic<InitCtx, cog::CogKind::Datacenter>);

static_assert(!CtxFitsCogMimic<int, cog::CogKind::Gpu>);

static_assert(
    [] {
        cog::CogIdentity id{};
        id.uuid = cog::Uuid{0xAA0001ULL, 0xBB0002ULL};
        id.kind = cog::CogKind::NicPort;
        id.firmware_revision = safety::Tagged<std::uint64_t, safety::source::Vendor>{42};

        cog::NicPortTargetCaps caps{};
        caps.link_layer = safety::Tagged<cog::LinkLayer, safety::source::Vendor>{cog::LinkLayer::Roce};
        caps.line_rate_bytes_per_sec =
            safety::Tagged<std::uint64_t, safety::source::Vendor>{std::uint64_t{50ULL} * 1024 * 1024 * 1024 / 8};

        cog::OpcodeLatencyTable<cog::CogKind::NicPort> tbl{};

        InitCtx ctx{};
        auto m = mint_cog_mimic<cog::CogKind::NicPort>(ctx, id, caps, tbl);

        return m.identity == &id && m.identity->kind == cog::CogKind::NicPort
            && m.calibrated_caps.value().link_layer.value() == cog::LinkLayer::Roce
            && m.family == cog::CogFamily::Network && m.is_uncalibrated();
    }(),
    "mint_cog_mimic round-trip lost identity, caps or opcodes for the "
    "Network family.");

static_assert(
    [] {
        cog::CogIdentity id{};
        id.uuid = cog::Uuid{0xAA0001ULL, 0xBB0002ULL};
        id.kind = cog::CogKind::Gpu;

        cog::GpuTargetCaps caps{};
        caps.sm_version = safety::Tagged<std::uint16_t, safety::source::Vendor>{std::uint16_t{90}};

        cog::OpcodeLatencyTable<cog::CogKind::Gpu> tbl{};

        InitCtx ctx{};
        auto m = mint_cog_mimic<cog::CogKind::Gpu>(ctx, id, caps, tbl);

        return m.identity == &id && m.calibrated_caps.value().sm_version.value() == 90
            && m.family == cog::CogFamily::Compute;
    }(),
    "mint_cog_mimic round-trip lost identity or caps for the Compute "
    "family.");

static_assert(CogMimic<cog::CogKind::Gpu>::kind == cog::CogKind::Gpu);
static_assert(CogMimic<cog::CogKind::CpuCore>::kind == cog::CogKind::CpuCore);
static_assert(CogMimic<cog::CogKind::CpuSocket>::kind == cog::CogKind::CpuSocket);
static_assert(CogMimic<cog::CogKind::NicPort>::kind == cog::CogKind::NicPort);
static_assert(CogMimic<cog::CogKind::NvSwitch>::kind == cog::CogKind::NvSwitch);
static_assert(CogMimic<cog::CogKind::DramChannel>::kind == cog::CogKind::DramChannel);

}  // namespace detail::cog_mimic_self_test

}  // namespace crucible::mimic
