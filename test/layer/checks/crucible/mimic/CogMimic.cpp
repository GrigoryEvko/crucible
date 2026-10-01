// The compile-time checks of crucible/mimic/CogMimic.h.

#include <crucible/mimic/CogMimic.h>

namespace crucible::mimic {

namespace detail::cog_mimic_self_test {

// A kind has a CogMimic when it is a Mimic substrate with caps, an opcode
// table and a projection.  A substrate may still lack caps or a table,
// and then it has no carrier yet.
template <cog::CogKind K>
inline constexpr bool has_carrier_v =
    cog::IsMimicSubstrate<K> && cog::HasCaps<K> && cog::HasOpcodeTable<K> && HasCogMimicProjection<K>;

// The carrier comes only from the mint: there is no default constructor,
// no constructor over the three parts that a caller can reach, no
// aggregate form and no build from bytes.  A minted value still copies,
// and its destruction stays trivial because it owns no heap.
template <cog::CogKind K>
inline constexpr bool carrier_is_closed_v =
    CogMimic<K>::kind == K
    && CogMimic<K>::family == cog::cog_family_v<K> && std::is_trivially_destructible_v<CogMimic<K>>
    && !std::is_default_constructible_v<CogMimic<K>> && !std::is_aggregate_v<CogMimic<K>>
    && !std::is_constructible_v<CogMimic<K>, cog::CogIdentity const&, typename CogMimic<K>::CalibratedCaps,
                                typename CogMimic<K>::OpcodeTable>
    && !std::is_trivially_copyable_v<CogMimic<K>> && !::foundation::lifetime::ImplicitLifetimeThroughout<CogMimic<K>>
    && std::is_nothrow_copy_constructible_v<CogMimic<K>> && std::is_nothrow_copy_assignable_v<CogMimic<K>>;

// The walk reads every CogKind.  A substrate that has caps and an opcode
// table but no projection fails, because it would lose its carrier
// without a word.  Each carrier must be closed, and the default caps of
// two carriers must fold apart, because the kind seeds the high byte of
// each fold.  Complexity: quadratic in the number of kinds.
[[nodiscard]] consteval bool every_carrier_is_closed_and_apart() {
    static constexpr auto kinds = std::define_static_array(std::meta::enumerators_of(^^cog::CogKind));
    std::array<std::uint64_t, ::foundation::reflect::enum_count<cog::CogKind>> default_folds{};
    std::size_t carrier_count = 0;
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wshadow"
    template for (constexpr auto kind : kinds) {
        constexpr cog::CogKind K = [:kind:];
        if constexpr (cog::IsMimicSubstrate<K> && cog::HasCaps<K> && cog::HasOpcodeTable<K>) {
            if (!HasCogMimicProjection<K>) return false;
        }
        if constexpr (has_carrier_v<K>) {
            if (!carrier_is_closed_v<K>) return false;
            default_folds[carrier_count++] = caps_class_projection<K>::fold(cog::caps_for_t<K>{});
        }
    }
#pragma GCC diagnostic pop
    for (std::size_t i = 0; i < carrier_count; ++i) {
        for (std::size_t j = i + 1; j < carrier_count; ++j) {
            if (default_folds[i] == default_folds[j]) return false;
        }
    }
    return carrier_count != 0;
}
static_assert(every_carrier_is_closed_and_apart(),
              "A Mimic substrate with caps and an opcode table lacks a projection, a carrier has an open door, or two "
              "carriers collide on their default caps.  The federation cache would alias kernels across substrates.");

static_assert(CogMimic<cog::CogKind::Gpu>::family == cog::CogFamily::Compute);
static_assert(CogMimic<cog::CogKind::CpuCore>::family == cog::CogFamily::Compute);
static_assert(CogMimic<cog::CogKind::CpuSocket>::family == cog::CogFamily::Compute);
static_assert(CogMimic<cog::CogKind::NicPort>::family == cog::CogFamily::Network);
static_assert(CogMimic<cog::CogKind::NvSwitch>::family == cog::CogFamily::Network);
static_assert(CogMimic<cog::CogKind::DramChannel>::family == cog::CogFamily::Memory);

static_assert(!cog::IsMimicSubstrate<cog::CogKind::PsuRail>);
static_assert(!cog::IsMimicSubstrate<cog::CogKind::RackPsu>);
static_assert(!cog::IsMimicSubstrate<cog::CogKind::BmcSensor>);
static_assert(!cog::IsMimicSubstrate<cog::CogKind::Datacenter>);
static_assert(!cog::IsMimicSubstrate<cog::CogKind::Server>);
static_assert(!cog::IsMimicSubstrate<cog::CogKind::Rack>);

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
        ethernet.link_layer =
            ::fixy::mint_tagged<::fixy::tags::source::Vendor, cog::LinkLayer>(cog::LinkLayer::Ethernet);
        return caps_class_projection<cog::CogKind::NicPort>::fold(infiniband)
            != caps_class_projection<cog::CogKind::NicPort>::fold(ethernet);
    }(),
    "Link-layer drift collapsed in NIC target_caps_class_hash. IB and Ethernet kernels would silently alias.");

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
