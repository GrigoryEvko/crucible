// Sentinel TU for the resource axes and the concurrent rows.  The two
// headers carry their own static_asserts.  This file includes them and
// drives every accessor with values the optimizer cannot fold, which is
// the run-time half that the old inline smoke tests held.

#include <foundation/effects/Concurrent.h>
#include <foundation/effects/Resources.h>
#include <foundation/reflect/EnumName.h>

#include <cstddef>
#include <cstdint>
#include <string_view>
#include <type_traits>

// Attacks on the tag gate that are legal C++.  Each explicit
// specialization lives in this translation unit only.  A type that
// inherits a real tag, a template of the same shape in another
// namespace, and a specialization of a real tag template that states
// another axis, another budget or another name are each refused.
namespace tag_gate_attacks {

namespace fe = ::foundation::effects;

struct DerivedSm : fe::resource::SmBudget<5> {};

template <std::uint64_t N>
struct SmBudget {
    static constexpr fe::ResourceKind kind = fe::ResourceKind::Sm;
    static constexpr std::uint64_t value = N;
    static constexpr std::string_view name = "SmBudget";
};

}  // namespace tag_gate_attacks

template <>
struct foundation::effects::resource::SmBudget<7> {
    static constexpr ResourceKind kind = ResourceKind::NicQp;
    static constexpr std::uint64_t value = 7;
    static constexpr std::string_view name = "SmBudget";
};

template <>
struct foundation::effects::resource::SmBudget<9> {
    static constexpr ResourceKind kind = ResourceKind::Sm;
    static constexpr std::uint64_t value = 0;
    static constexpr std::string_view name = "SmBudget";
};

template <>
struct foundation::effects::resource::SmBudget<11> {
    static constexpr ResourceKind kind = ResourceKind::Sm;
    static constexpr std::uint64_t value = 11;
    static constexpr std::string_view name = "HbmBytes";
};

namespace tag_gate_attacks {

static_assert(!fe::ResourceTag<DerivedSm>);
static_assert(!fe::ResourceTag<SmBudget<5>>);
static_assert(!fe::ResourceTag<fe::resource::SmBudget<7>>);
static_assert(!fe::ResourceTag<fe::resource::SmBudget<9>>);
static_assert(!fe::ResourceTag<fe::resource::SmBudget<11>>);
static_assert(!fe::ResourceTag<const fe::resource::SmBudget<5>>);
static_assert(!fe::ResourceTag<fe::resource::SmBudget<5>&>);

// The controls: the unspecialized neighbours are tags.
static_assert(fe::ResourceTag<fe::resource::SmBudget<5>>);
static_assert(fe::ResourceTag<fe::resource::SmBudget<12>>);

}  // namespace tag_gate_attacks

namespace {

namespace fe = ::foundation::effects;

// Every axis in the catalog, listed by hand so that the reflected count
// has a second witness.
constexpr fe::ResourceKind every_kind[] = {
    fe::ResourceKind::Sm,
    fe::ResourceKind::WarpScheduler,
    fe::ResourceKind::RegistersPerWarp,
    fe::ResourceKind::Smem,
    fe::ResourceKind::L2,
    fe::ResourceKind::HbmBytes,
    fe::ResourceKind::HbmBw,
    fe::ResourceKind::NvlinkBw,
    fe::ResourceKind::PcieBw,
    fe::ResourceKind::NicQ,
    fe::ResourceKind::NicRing,
    fe::ResourceKind::NicQp,
    fe::ResourceKind::NicCq,
    fe::ResourceKind::NicMr,
    fe::ResourceKind::SwitchEgressBw,
    fe::ResourceKind::SwitchBuffer,
    fe::ResourceKind::Tcam,
    fe::ResourceKind::CpuCore,
    fe::ResourceKind::Llc,
    fe::ResourceKind::PowerWatts,
    fe::ResourceKind::ThermalCelsius,
    fe::ResourceKind::RackPowerKw,
    fe::ResourceKind::CarbonGramsPerKwh,
};
static_assert(sizeof(every_kind) / sizeof(every_kind[0]) == fe::resource_kind_count,
              "The table of axes disagrees with the reflected count.  Add the new axis to the table.");

// Each name is read through a volatile load, so the call happens at run
// time and cannot fold into the compile-time checks of the header.
[[nodiscard]] bool every_kind_has_a_name_at_run_time() noexcept {
    for (fe::ResourceKind kind : every_kind) {
        volatile fe::ResourceKind loaded = kind;
        std::string_view const name = ::foundation::reflect::enum_name(static_cast<fe::ResourceKind>(loaded));
        if (name.empty() || name == std::string_view{"<unknown ResourceKind>"}) return false;
    }
    volatile std::uint8_t unnamed = 99;
    return ::foundation::reflect::enum_name(static_cast<fe::ResourceKind>(unnamed))
        == std::string_view{"<unknown ResourceKind>"};
}

// A tag is one empty byte on its own, and it carries its kind and its
// budget as static members that a run-time read must reproduce.
[[nodiscard]] bool tags_carry_their_facts_at_run_time() noexcept {
    fe::SmBudget<32> sm{};
    fe::HbmBytes<80000000000ULL> hbm{};
    fe::NicQp<4> qp{};
    static_assert(sizeof(sm) == 1 && sizeof(hbm) == 1 && sizeof(qp) == 1);

    auto fingerprint = [](auto const& tag) -> std::uint64_t {
        using TagT = std::remove_cvref_t<decltype(tag)>;
        volatile auto kind = static_cast<std::uint8_t>(TagT::kind);
        volatile auto value = TagT::value;
        return (static_cast<std::uint64_t>(kind) << 56) ^ static_cast<std::uint64_t>(value);
    };
    return fingerprint(sm) == ((static_cast<std::uint64_t>(fe::ResourceKind::Sm) << 56) ^ 32ULL)
        && fingerprint(hbm) == ((static_cast<std::uint64_t>(fe::ResourceKind::HbmBytes) << 56) ^ 80000000000ULL)
        && fingerprint(qp) == ((static_cast<std::uint64_t>(fe::ResourceKind::NicQp) << 56) ^ 4ULL);
}

// A summed row is a real type: it can be built, sized and read at run
// time, and its per-axis values are the sums of its inputs.
[[nodiscard]] bool summed_rows_reify_at_run_time() noexcept {
    using R1 = fe::ConcurrentRow<fe::resource::SmBudget<32>, fe::resource::NicQp<4>>;
    using R2 = fe::ConcurrentRow<fe::resource::SmBudget<64>, fe::resource::NicQp<2>>;
    using Sum = fe::concurrent_row_sum_t<R1, R2>;
    static_assert(std::is_empty_v<Sum> && std::is_trivially_copyable_v<Sum>);

    using Total = fe::concurrent_row_n_t<fe::ConcurrentRow<fe::resource::SmBudget<10>>,
                                         fe::ConcurrentRow<fe::resource::SmBudget<20>>,
                                         fe::ConcurrentRow<fe::resource::SmBudget<30>>,
                                         fe::ConcurrentRow<fe::resource::SmBudget<40>>>;

    [[maybe_unused]] Sum sum{};
    [[maybe_unused]] Total total{};
    volatile std::uint64_t sm_sum = fe::concurrent_row_value_v<fe::ResourceKind::Sm, Sum>;
    volatile std::uint64_t qp_sum = fe::concurrent_row_value_v<fe::ResourceKind::NicQp, Sum>;
    volatile std::uint64_t sm_total = fe::concurrent_row_value_v<fe::ResourceKind::Sm, Total>;
    volatile std::size_t size = sizeof(sum);
    return sm_sum == 96 && qp_sum == 6 && sm_total == 100 && size == 1;
}

// The descriptors of a row walk in the order the row names its tags.
[[nodiscard]] bool descriptors_walk_at_run_time() noexcept {
    using Row = fe::ConcurrentRow<fe::resource::NicQp<4>, fe::resource::SmBudget<32>>;
    std::uint64_t value_sum = 0;
    std::size_t sm_position = 99;
    std::size_t position = 0;
    for (auto const& descriptor : fe::concurrent_row_descriptors_v<Row>) {
        volatile std::uint64_t value = descriptor.value;
        value_sum += value;
        if (descriptor.kind == fe::ResourceKind::Sm) sm_position = position;
        ++position;
    }
    return value_sum == 36 && sm_position == 1;
}

}  // namespace

int main() {
    if (!every_kind_has_a_name_at_run_time()) return 1;
    if (!tags_carry_their_facts_at_run_time()) return 2;
    if (!summed_rows_reify_at_run_time()) return 3;
    if (!descriptors_walk_at_run_time()) return 4;
    return 0;
}
