// A header-only surface is only checked where a translation unit pulls it
// in. Compiling this file runs the included header's own static_asserts
// under the project warning flags. The body then drives the same accessors
// with non-constant inputs, which is what separates a real runtime call
// from a constant fold of the compile-time checks.

#include <crucible/effects/Resources.h>

#include "test_assert.h"

#include <cstdio>
#include <string_view>

namespace eff = crucible::effects;

static void test_resource_kind_name_coverage() {
    // The volatile barrier below keeps the optimizer from folding the whole
    // suite at compile time, which would mask a divergence between the
    // constexpr path and the runtime one.
    constexpr eff::ResourceKind kinds[] = {
        eff::ResourceKind::Sm,
        eff::ResourceKind::WarpScheduler,
        eff::ResourceKind::RegistersPerWarp,
        eff::ResourceKind::Smem,
        eff::ResourceKind::L2,
        eff::ResourceKind::HbmBytes,
        eff::ResourceKind::HbmBw,
        eff::ResourceKind::NvlinkBw,
        eff::ResourceKind::PcieBw,
        eff::ResourceKind::NicQ,
        eff::ResourceKind::NicRing,
        eff::ResourceKind::NicQp,
        eff::ResourceKind::NicCq,
        eff::ResourceKind::NicMr,
        eff::ResourceKind::SwitchEgressBw,
        eff::ResourceKind::SwitchBuffer,
        eff::ResourceKind::Tcam,
        eff::ResourceKind::CpuCore,
        eff::ResourceKind::Llc,
        eff::ResourceKind::PowerWatts,
        eff::ResourceKind::ThermalCelsius,
        eff::ResourceKind::RackPowerKw,
        eff::ResourceKind::CarbonGramsPerKwh,
    };

    static_assert(sizeof(kinds) / sizeof(kinds[0]) == eff::resource_kind_count,
                  "Manual kinds[] table diverged from reflected resource_kind_count "
                  "— add the new atom to this table when extending the catalog.");

    for (eff::ResourceKind k : kinds) {
        // Loading through volatile forces a real call to the accessor.
        volatile auto vk = k;
        std::string_view name = eff::resource_kind_name(static_cast<eff::ResourceKind>(vk));
        assert(!name.empty());
        assert(name != std::string_view{"<unknown ResourceKind>"});
    }
    std::printf("  test_resource_kind_name_coverage:    PASSED\n");
}

static void test_resource_tag_instances() {
    eff::SmBudget<32> sm{};
    eff::HbmBytes<80000000000ULL> hbm{};
    eff::NicQp<4> qp{};
    eff::PowerWatts<700> watts{};
    eff::CarbonGramsPerKwh<400> carbon{};

    // One byte is the floor for an empty struct. Inside a containing row or
    // context, empty-base optimization takes it back to zero.
    static_assert(sizeof(sm) == 1);
    static_assert(sizeof(hbm) == 1);
    static_assert(sizeof(qp) == 1);
    static_assert(sizeof(watts) == 1);
    static_assert(sizeof(carbon) == 1);

    // The volatile barrier again forces evaluation at runtime.
    auto fingerprint = [](auto const& tag) -> std::uint64_t {
        using TagT = std::remove_cvref_t<decltype(tag)>;
        volatile auto k = static_cast<std::uint8_t>(TagT::kind);
        volatile auto v = TagT::value;
        return (static_cast<std::uint64_t>(k) << 56) ^ static_cast<std::uint64_t>(v);
    };

    assert(fingerprint(sm) == ((static_cast<std::uint64_t>(eff::ResourceKind::Sm) << 56) ^ 32ULL));
    assert(fingerprint(hbm) == ((static_cast<std::uint64_t>(eff::ResourceKind::HbmBytes) << 56) ^ 80000000000ULL));
    assert(fingerprint(qp) == ((static_cast<std::uint64_t>(eff::ResourceKind::NicQp) << 56) ^ 4ULL));

    // The header already pins these at compile time. The repetition is
    // deliberate: it fires if a later edit weakens the checks in the header.
    static_assert(eff::ResourceTag<decltype(sm)>);
    static_assert(eff::ResourceTag<decltype(hbm)>);
    static_assert(eff::ResourceTag<decltype(qp)>);
    static_assert(eff::ResourceTag<decltype(watts)>);
    static_assert(eff::ResourceTag<decltype(carbon)>);

    std::printf("  test_resource_tag_instances:         PASSED\n");
}

int main() {
    std::printf("test_resources: 2 groups\n");
    test_resource_kind_name_coverage();
    test_resource_tag_instances();
    std::printf("test_resources: 2 groups, all passed\n");
    return 0;
}
