// Sentinel TU for include/crucible/effects/Resources.h.
//
// Per feedback_header_only_static_assert_blind_spot.md: header-only
// static_asserts inside `Resources.h` are only evaluated under the
// project's full warning + standard flags when SOMEONE includes the
// header from a TU that lands in the build graph.  This sentinel
// makes the inclusion explicit so the resources_self_test block is
// exercised by every default build, not just incidental TUs that
// happen to transitively pull in `Capabilities.h`-adjacent headers.
//
// The runtime portion exercises the constexpr name accessors with
// non-constant arguments (per
// feedback_algebra_runtime_smoke_test_discipline) and verifies the
// reflected `resource_kind_count` cardinality matches the manually
// pinned 23.
//
// GAPS-189.  No data flows out of this TU — its purpose is purely to
// pull the header into the warning-flag-controlled compilation unit
// graph.

#include <crucible/effects/Resources.h>

#include "test_assert.h"

#include <cstdio>
#include <string_view>

namespace eff = crucible::effects;

// ── Runtime smoke test ───────────────────────────────────────────────
//
// Exercises every ResourceKind atom through resource_kind_name() with
// runtime-only inputs.  The constexpr accessor SHOULD demote to a
// regular runtime call here (its body is a switch over uint8_t —
// nothing consteval-only inside).

static void test_resource_kind_name_coverage() {
    // Pull the enum range into runtime.  The volatile barrier prevents
    // the optimizer from constant-folding the entire suite at compile
    // time and silently masking a constexpr-vs-runtime divergence.
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
        // Volatile-load the kind so the compiler must call the accessor
        // at runtime rather than fold via the static_asserts in-header.
        volatile auto vk = k;
        std::string_view name = eff::resource_kind_name(static_cast<eff::ResourceKind>(vk));
        assert(!name.empty());
        assert(name != std::string_view{"<unknown ResourceKind>"});
    }
    std::printf("  test_resource_kind_name_coverage:    PASSED\n");
}

// ── Tag instantiation runtime smoke ─────────────────────────────────
//
// Construct a representative tag from each axis at runtime (default
// constructor, EBO-collapsible) and confirm the canonical triple
// (kind / value / name) is observable through a runtime instance —
// not just at compile time.

static void test_resource_tag_instances() {
    eff::SmBudget<32>           sm{};
    eff::HbmBytes<80'000'000'000ULL> hbm{};
    eff::NicQp<4>               qp{};
    eff::PowerWatts<700>        watts{};
    eff::CarbonGramsPerKwh<400> carbon{};

    // sizeof must be 1 (empty struct floor).  Empty-base optimization
    // collapses this to zero in containing rows / contexts.
    static_assert(sizeof(sm)     == 1);
    static_assert(sizeof(hbm)    == 1);
    static_assert(sizeof(qp)     == 1);
    static_assert(sizeof(watts)  == 1);
    static_assert(sizeof(carbon) == 1);

    // Triple readable via runtime instance.  The volatile barrier here
    // again forces the compiler to evaluate at runtime.
    auto fingerprint = [](auto const& tag) -> std::uint64_t {
        using TagT = std::remove_cvref_t<decltype(tag)>;
        volatile auto k = static_cast<std::uint8_t>(TagT::kind);
        volatile auto v = TagT::value;
        return (static_cast<std::uint64_t>(k) << 56) ^ static_cast<std::uint64_t>(v);
    };

    assert(fingerprint(sm)
        == ((static_cast<std::uint64_t>(eff::ResourceKind::Sm) << 56)
            ^ 32ULL));
    assert(fingerprint(hbm)
        == ((static_cast<std::uint64_t>(eff::ResourceKind::HbmBytes) << 56)
            ^ 80'000'000'000ULL));
    assert(fingerprint(qp)
        == ((static_cast<std::uint64_t>(eff::ResourceKind::NicQp) << 56)
            ^ 4ULL));

    // Concept satisfaction at runtime-instantiation point.  The header
    // already pins this at compile time; this redundant check fires if
    // a future edit accidentally weakens the in-header static_asserts.
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
