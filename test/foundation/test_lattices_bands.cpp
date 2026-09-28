// Sentinel TU for the band, hardware-scope and clock lattices and the
// enum value pins.  Each header carries its own static_asserts and an
// inline runtime_smoke_test; this file includes each and calls each
// once.  EnumValuePins.h is included so its pins are compiled at all.
//
// The tiers are walked by reflection rather than listed by hand, so a
// new enumerator is covered the moment it is declared.

#include <foundation/algebra/Graded.h>
#include <foundation/algebra/lattices/AffinityLattice.h>
#include <foundation/algebra/lattices/AllocClassLattice.h>
#include <foundation/algebra/lattices/BarrierStrengthLattice.h>
#include <foundation/algebra/lattices/ChainLattice.h>
#include <foundation/algebra/lattices/CipherTierLattice.h>
#include <foundation/algebra/lattices/ClockSourceLattice.h>
#include <foundation/algebra/lattices/DetSafeLattice.h>
#include <foundation/algebra/lattices/DualLattice.h>
#include <foundation/algebra/lattices/EnumValuePins.h>
#include <foundation/algebra/lattices/HotPathLattice.h>
#include <foundation/algebra/lattices/LifetimeLattice.h>
#include <foundation/algebra/lattices/MemoryScopeLattice.h>
#include <foundation/algebra/lattices/NumaNodeLattice.h>
#include <foundation/algebra/lattices/PinningRequirementLattice.h>
#include <foundation/algebra/lattices/ProductLattice.h>
#include <foundation/algebra/lattices/RecipeFamilyLattice.h>
#include <foundation/algebra/lattices/ResidencyHeatLattice.h>
#include <foundation/algebra/lattices/SchedulerPolicyLattice.h>
#include <foundation/algebra/lattices/SuspendBehaviorLattice.h>
#include <foundation/algebra/lattices/ToleranceLattice.h>
#include <foundation/algebra/lattices/VendorLattice.h>
#include <foundation/algebra/lattices/WaitLattice.h>
#include <foundation/reflect/Enumerate.h>

#include <array>
#include <cstddef>
#include <cstdio>
#include <cstdlib>
#include <iterator>
#include <meta>
#include <string_view>
#include <type_traits>
#include <utility>
#include <vector>

namespace {

namespace fa = ::foundation::algebra;
namespace fl = ::foundation::algebra::lattices;
namespace fr = ::foundation::reflect;

// The authority for the carriers built below.  It hands its key out,
// which an authority in production code never does.
struct test_authority {
    [[nodiscard]] static constexpr fa::grade_key<test_authority> key() noexcept {
        return fa::grade_key<test_authority>{};
    }
};

struct TwoWords {
    unsigned long long lo{0};
    unsigned long long hi{0};
};

// The payload of the pinned carriers below.  A chain header keeps no
// payload type of its own, because ChainLattice.h checks the layout of
// a pinned carrier one time for all of them.
struct OneByteValue {
    char c{0};
};

// The Absolute carrier over a pinned grade.
template <typename PinnedGrade, typename T>
using PinnedGraded = fa::Graded<fa::ModalityKind::Absolute, PinnedGrade, T>;

// Every chain lattice here is built on the shared ChainLatticeOps base,
// so its element type is the scoped enum and its At<> sub-lattice is
// empty; a band wrapper over At<> costs sizeof(T) at every tier, not
// only at the one a hand-written cell happened to name.
template <typename L, typename E = typename L::element_type>
[[nodiscard]] consteval bool every_tier_collapses() noexcept {
    static constexpr auto enumerators = std::define_static_array(std::meta::enumerators_of(^^E));
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wshadow"
    template for (constexpr auto en : enumerators) {
        using At = typename L::template At<([:en:])>;
        if (!std::is_empty_v<typename At::element_type>) return false;
        if (sizeof(fa::Graded<fa::ModalityKind::Absolute, At, int>) != sizeof(int)) return false;
        if (sizeof(fa::Graded<fa::ModalityKind::Absolute, At, TwoWords>) != sizeof(TwoWords)) return false;
        if (alignof(fa::Graded<fa::ModalityKind::Absolute, At, TwoWords>) != alignof(TwoWords)) return false;
    }
#pragma GCC diagnostic pop
    return true;
}

static_assert(std::is_base_of_v<fl::ChainLatticeOps<fl::DetSafeTier>, fl::DetSafeLattice>);
static_assert(every_tier_collapses<fl::DetSafeLattice>());
static_assert(every_tier_collapses<fl::AllocClassLattice>());
static_assert(every_tier_collapses<fl::HotPathLattice>());
static_assert(every_tier_collapses<fl::CipherTierLattice>());
static_assert(every_tier_collapses<fl::ResidencyHeatLattice>());
static_assert(every_tier_collapses<fl::ToleranceLattice>());
static_assert(every_tier_collapses<fl::WaitLattice>());
static_assert(every_tier_collapses<fl::LifetimeLattice>());
static_assert(every_tier_collapses<fl::BarrierStrengthLattice>());
static_assert(every_tier_collapses<fl::SuspendBehaviorLattice>());
static_assert(every_tier_collapses<fl::PinningRequirementLattice>());
static_assert(every_tier_collapses<fl::VendorLattice>());
static_assert(every_tier_collapses<fl::MemoryScopeLattice>());
static_assert(every_tier_collapses<fl::ClockSourceLattice, fl::ClockSource>());

// The reflected At name is the lattice name, "::At<", the enumerator
// identifier and ">", at every tier.
template <typename L, typename E = typename L::element_type>
[[nodiscard]] consteval bool every_at_name_is_reflected() noexcept {
    static constexpr auto enumerators = std::define_static_array(std::meta::enumerators_of(^^E));
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wshadow"
    template for (constexpr auto en : enumerators) {
        using At = typename L::template At<([:en:])>;
        const std::string_view name = At::name();
        if (!name.starts_with(L::name())) return false;
        if (!name.ends_with(">")) return false;
        const std::string_view middle = name.substr(L::name().size());
        if (!middle.starts_with("::At<")) return false;
        if (middle.substr(5, middle.size() - 6) != fr::enum_name([:en:])) return false;
    }
#pragma GCC diagnostic pop
    return true;
}

static_assert(every_at_name_is_reflected<fl::DetSafeLattice>());
static_assert(every_at_name_is_reflected<fl::ToleranceLattice>());
static_assert(every_at_name_is_reflected<fl::LifetimeLattice>());
static_assert(every_at_name_is_reflected<fl::VendorLattice>());
static_assert(every_at_name_is_reflected<fl::ResidencyHeatLattice>());
static_assert(every_at_name_is_reflected<fl::ClockSourceLattice, fl::ClockSource>());

// Each memory scope belongs to one chain or to none.  The table spells out
// the chain of every enumerator, so a scope in the wrong nibble or with no
// chain is refused, and a new scope needs a row before the walk passes.
enum class ScopeChain : unsigned char {
    None,
    Accelerator,
    Host,
};
struct ScopeRow {
    fl::MemoryScope scope = fl::MemoryScope::Thread;
    ScopeChain chain = ScopeChain::None;
};
inline constexpr ScopeRow kScopeChains[] = {
    {fl::MemoryScope::Thread, ScopeChain::None},     {fl::MemoryScope::Warp, ScopeChain::Accelerator},
    {fl::MemoryScope::Cta, ScopeChain::Accelerator}, {fl::MemoryScope::Cluster, ScopeChain::Accelerator},
    {fl::MemoryScope::Gpu, ScopeChain::Accelerator}, {fl::MemoryScope::Inner, ScopeChain::Host},
    {fl::MemoryScope::Outer, ScopeChain::Host},      {fl::MemoryScope::System, ScopeChain::None},
};

// Complexity: O(n^2) in the number of enumerators.
[[nodiscard]] consteval bool every_scope_is_in_its_chain() noexcept {
    static constexpr auto enumerators = std::define_static_array(std::meta::enumerators_of(^^fl::MemoryScope));
    if (std::size(kScopeChains) != enumerators.size()) return false;
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wshadow"
    template for (constexpr auto en : enumerators) {
        bool has_row = false;
        for (const ScopeRow row : kScopeChains)
            has_row = has_row || row.scope == [:en:];
        if (!has_row) return false;
    }
#pragma GCC diagnostic pop
    for (const ScopeRow row : kScopeChains) {
        if (fl::mem_scope_is_accel(row.scope) != (row.chain == ScopeChain::Accelerator)) return false;
        if (fl::mem_scope_is_arm(row.scope) != (row.chain == ScopeChain::Host)) return false;
    }
    return true;
}
static_assert(every_scope_is_in_its_chain());

// The clock lattice is a product of three axes and its At<> pins the
// source only; the projected point is a free function.
static_assert(fl::ClockSourceLattice::arity == 3);
static_assert(fa::BoundedLattice<fl::ClockSourceLattice>);
static_assert(fl::ClockSourceLattice::get<0>(fl::clock_source_project(fl::ClockSource::TscSerialized))
              == fl::DetSafeTier::MonotonicClockRead);

// A variable template and not a concept, so that a reflection can
// substitute it and read the answer.
template <typename L, typename A, typename B>
inline constexpr bool leq_accepts_v = requires(A lhs, B rhs) { L::leq(lhs, rhs); };

template <typename L>
using element_of_t = typename L::element_type;

[[nodiscard]] consteval std::meta::info element_of(std::meta::info lattice) {
    return std::meta::dealias(std::meta::substitute(^^element_of_t, {lattice}));
}

[[nodiscard]] consteval bool leq_accepts(std::meta::info lattice, std::meta::info lhs, std::meta::info rhs) {
    return std::meta::extract<bool>(std::meta::substitute(^^leq_accepts_v, {lattice, lhs, rhs}));
}

// Every pinned grade of L, one reflection for each tier.  E is a
// parameter because the clock lattice pins an enum that is not its
// element type.
template <typename L, typename E = typename L::element_type>
consteval void append_pins(std::vector<std::meta::info>& pins) {
    static constexpr auto enumerators = std::define_static_array(std::meta::enumerators_of(^^E));
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wshadow"
    template for (constexpr auto en : enumerators) {
        pins.push_back(std::meta::dealias(^^typename L::template At<([:en:])>));
    }
#pragma GCC diagnostic pop
}

// Two pinned grades never meet, in one lattice or across two.  The
// element of one does not convert to the element of the other, and the
// leq of one does not take the element of the other.  Each grade takes
// its own element, so the refusals are not vacuous.  O(n^2) in the
// number of pins, at compile time only.
[[nodiscard]] consteval bool pins_stay_apart() {
    std::vector<std::meta::info> pins;
    append_pins<fl::DetSafeLattice>(pins);
    append_pins<fl::AllocClassLattice>(pins);
    append_pins<fl::HotPathLattice>(pins);
    append_pins<fl::CipherTierLattice>(pins);
    append_pins<fl::ResidencyHeatLattice>(pins);
    append_pins<fl::ToleranceLattice>(pins);
    append_pins<fl::WaitLattice>(pins);
    append_pins<fl::LifetimeLattice>(pins);
    append_pins<fl::BarrierStrengthLattice>(pins);
    append_pins<fl::SuspendBehaviorLattice>(pins);
    append_pins<fl::PinningRequirementLattice>(pins);
    append_pins<fl::SchedulerPolicyLattice>(pins);
    append_pins<fl::VendorLattice>(pins);
    append_pins<fl::MemoryScopeLattice>(pins);
    append_pins<fl::ClockSourceLattice, fl::ClockSource>(pins);
    for (const std::meta::info held : pins) {
        const std::meta::info held_element = element_of(held);
        for (const std::meta::info other : pins) {
            const std::meta::info other_element = element_of(other);
            const bool is_same_pin = held == other;
            if (std::meta::is_convertible_type(other_element, held_element) != is_same_pin) return false;
            if (leq_accepts(held, held_element, other_element) != is_same_pin) return false;
        }
    }
    return true;
}
static_assert(pins_stay_apart());

// The leq of one lattice does not take the element of another.  The
// lattices hold pairwise different elements, and each leq takes its
// own, so the walk proves a refusal at every pair.
template <typename... Lattices>
[[nodiscard]] consteval bool leq_refuses_foreign_elements() {
    constexpr std::array<std::meta::info, sizeof...(Lattices)> lattices{^^Lattices...};
    for (const std::meta::info held : lattices) {
        for (const std::meta::info other : lattices) {
            const bool is_same_lattice = held == other;
            if ((element_of(held) == element_of(other)) != is_same_lattice) return false;
            if (leq_accepts(held, element_of(held), element_of(other)) != is_same_lattice) return false;
        }
    }
    return true;
}
static_assert(leq_refuses_foreign_elements<
              fl::DetSafeLattice, fl::AllocClassLattice, fl::HotPathLattice, fl::CipherTierLattice,
              fl::ResidencyHeatLattice, fl::ToleranceLattice, fl::WaitLattice, fl::LifetimeLattice,
              fl::BarrierStrengthLattice, fl::SuspendBehaviorLattice, fl::PinningRequirementLattice,
              fl::SchedulerPolicyLattice, fl::VendorLattice, fl::MemoryScopeLattice, fl::RecipeFamilyLattice,
              fl::NumaNodeLattice, fl::AffinityLattice, fl::ClockSourceLattice>());

// The clock lattice orders projected points, not sources, and each slot
// of a point takes the enum of its own axis only.
static_assert(!leq_accepts_v<fl::ClockSourceLattice, fl::ClockSource, fl::ClockSource>);

template <typename L, std::size_t Slot, typename V>
inline constexpr bool slot_takes_v = requires(element_of_t<L> point, V value) { L::template get<Slot>(point) = value; };

template <typename L, std::size_t Slot, std::size_t... Axes>
inline constexpr bool slot_takes_only_its_axis =
    ((slot_takes_v<L, Slot, element_of_t<typename L::template nth_lattice<Axes>>> == (Slot == Axes)) && ...);

template <typename L, std::size_t... Slots>
[[nodiscard]] consteval bool slots_take_their_own_axis(std::index_sequence<Slots...>) noexcept {
    return (slot_takes_only_its_axis<L, Slots, Slots...> && ...);
}
static_assert(
    slots_take_their_own_axis<fl::ClockSourceLattice>(std::make_index_sequence<fl::ClockSourceLattice::arity>{}));

// Each body below was an inline runtime_smoke_test in its header,
// compiled into every translation unit that included it.  They are
// moved verbatim.  The three function-scope using-directives reproduce
// the name lookup each body had inside its header, where the self-test
// namespace nests in lattices, which nests in algebra, so the
// scaffolding types the bodies name still resolve.
//
// The static_assert walls stayed in the headers.  Those read a shipped
// lattice against its own grades, so they fire wherever the header is
// used, and each header's self-test namespace still holds one.

void chain_lattice_runs_at_run_time() {
    using namespace fa;
    using namespace fl;
    using namespace fl::detail::chain_lattice_self_test;
    volatile std::uint8_t raw_hi = 2;
    const SmokeTier lo = SmokeTier::Lo;
    const SmokeTier hi = static_cast<SmokeTier>(raw_hi);

    [[maybe_unused]] bool le_dir = SmokeChainLattice::leq(lo, hi);
    [[maybe_unused]] bool ge_dir = SmokeChainLattice::leq(hi, lo);
    [[maybe_unused]] SmokeTier mx = SmokeChainLattice::join(lo, hi);
    [[maybe_unused]] SmokeTier mn = SmokeChainLattice::meet(lo, hi);
    [[maybe_unused]] SmokeTier bot = SmokeChainLattice::bottom();
    [[maybe_unused]] SmokeTier top = SmokeChainLattice::top();

    using MidAt = SmokeChainLattice::At<SmokeTier::Mid>;
    MidAt::element_type pin{};
    [[maybe_unused]] SmokeTier recovered = pin;
    [[maybe_unused]] bool pin_leq = MidAt::leq(pin, MidAt::top());
    [[maybe_unused]] MidAt::element_type pin_join = MidAt::join(pin, MidAt::bottom());
}

void det_safe_lattice_runs_at_run_time() {
    using namespace fa;
    using namespace fl;
    using namespace fl::detail::det_safe_lattice_self_test;
    DetSafeTier a = DetSafeTier::NonDeterministicSyscall;
    DetSafeTier b = DetSafeTier::Pure;
    [[maybe_unused]] bool l1 = DetSafeLattice::leq(a, b);
    [[maybe_unused]] DetSafeTier j1 = DetSafeLattice::join(a, b);
    [[maybe_unused]] DetSafeTier m1 = DetSafeLattice::meet(a, b);
    [[maybe_unused]] DetSafeTier bot = DetSafeLattice::bottom();
    [[maybe_unused]] DetSafeTier top = DetSafeLattice::top();

    DetSafeTier mono = DetSafeTier::MonotonicClockRead;
    DetSafeTier philox = DetSafeTier::PhiloxRng;
    [[maybe_unused]] DetSafeTier j2 = DetSafeLattice::join(mono, philox);
    [[maybe_unused]] DetSafeTier m2 = DetSafeLattice::meet(mono, philox);

    OneByteValue v{42};
    PinnedGraded<det_safe_tier::PureTier, OneByteValue> initial{test_authority::key(), v,
                                                               det_safe_tier::PureTier::bottom()};
    auto widened = initial.weaken(det_safe_tier::PureTier::top());
    auto composed = initial.compose(widened);
    auto rv_widen = std::move(widened).weaken(det_safe_tier::PureTier::top());

    [[maybe_unused]] auto g = rv_widen.grade();
    [[maybe_unused]] auto vc = composed.peek().c;

    det_safe_tier::PureTier::element_type e{};
    [[maybe_unused]] DetSafeTier rec = e;
}

void alloc_class_lattice_runs_at_run_time() {
    using namespace fa;
    using namespace fl;
    using namespace fl::detail::alloc_class_lattice_self_test;
    AllocClassTag a = AllocClassTag::HugePage;
    AllocClassTag b = AllocClassTag::Stack;
    [[maybe_unused]] bool l1 = AllocClassLattice::leq(a, b);
    [[maybe_unused]] AllocClassTag j1 = AllocClassLattice::join(a, b);
    [[maybe_unused]] AllocClassTag m1 = AllocClassLattice::meet(a, b);
    [[maybe_unused]] AllocClassTag bot = AllocClassLattice::bottom();
    [[maybe_unused]] AllocClassTag topv = AllocClassLattice::top();

    AllocClassTag heap = AllocClassTag::Heap;
    AllocClassTag arena = AllocClassTag::Arena;
    [[maybe_unused]] AllocClassTag j2 = AllocClassLattice::join(heap, arena);
    [[maybe_unused]] AllocClassTag m2 = AllocClassLattice::meet(heap, arena);

    OneByteValue v{42};
    PinnedGraded<alloc_class_tag::StackAlloc, OneByteValue> initial{test_authority::key(), v,
                                                                   alloc_class_tag::StackAlloc::bottom()};
    auto widened = initial.weaken(alloc_class_tag::StackAlloc::top());
    auto composed = initial.compose(widened);
    auto rv_widen = std::move(widened).weaken(alloc_class_tag::StackAlloc::top());

    [[maybe_unused]] auto g = rv_widen.grade();
    [[maybe_unused]] auto vc = composed.peek().c;

    alloc_class_tag::StackAlloc::element_type e{};
    [[maybe_unused]] AllocClassTag rec = e;
}

void hot_path_lattice_runs_at_run_time() {
    using namespace fa;
    using namespace fl;
    using namespace fl::detail::hot_path_lattice_self_test;
    HotPathTier a = HotPathTier::Cold;
    HotPathTier b = HotPathTier::Hot;
    [[maybe_unused]] bool l1 = HotPathLattice::leq(a, b);
    [[maybe_unused]] HotPathTier j1 = HotPathLattice::join(a, b);
    [[maybe_unused]] HotPathTier m1 = HotPathLattice::meet(a, b);
    [[maybe_unused]] HotPathTier bot = HotPathLattice::bottom();
    [[maybe_unused]] HotPathTier topv = HotPathLattice::top();

    HotPathTier warm = HotPathTier::Warm;
    [[maybe_unused]] HotPathTier j2 = HotPathLattice::join(warm, a);
    [[maybe_unused]] HotPathTier m2 = HotPathLattice::meet(warm, b);

    OneByteValue v{42};
    PinnedGraded<hot_path_tier::HotTier, OneByteValue> initial{test_authority::key(), v,
                                                              hot_path_tier::HotTier::bottom()};
    auto widened = initial.weaken(hot_path_tier::HotTier::top());
    auto composed = initial.compose(widened);
    auto rv_widen = std::move(widened).weaken(hot_path_tier::HotTier::top());

    [[maybe_unused]] auto g = rv_widen.grade();
    [[maybe_unused]] auto vc = composed.peek().c;

    hot_path_tier::HotTier::element_type e{};
    [[maybe_unused]] HotPathTier rec = e;
}

void cipher_tier_lattice_runs_at_run_time() {
    using namespace fa;
    using namespace fl;
    using namespace fl::detail::cipher_tier_lattice_self_test;
    CipherTierTag a = CipherTierTag::Cold;
    CipherTierTag b = CipherTierTag::Hot;
    [[maybe_unused]] bool l1 = CipherTierLattice::leq(a, b);
    [[maybe_unused]] CipherTierTag j1 = CipherTierLattice::join(a, b);
    [[maybe_unused]] CipherTierTag m1 = CipherTierLattice::meet(a, b);
    [[maybe_unused]] CipherTierTag bot = CipherTierLattice::bottom();
    [[maybe_unused]] CipherTierTag topv = CipherTierLattice::top();

    CipherTierTag warm = CipherTierTag::Warm;
    [[maybe_unused]] CipherTierTag j2 = CipherTierLattice::join(warm, a);
    [[maybe_unused]] CipherTierTag m2 = CipherTierLattice::meet(warm, b);

    OneByteValue v{42};
    PinnedGraded<cipher_tier_tag::HotTier, OneByteValue> initial{test_authority::key(), v,
                                                                cipher_tier_tag::HotTier::bottom()};
    auto widened = initial.weaken(cipher_tier_tag::HotTier::top());
    auto composed = initial.compose(widened);
    auto rv_widen = std::move(widened).weaken(cipher_tier_tag::HotTier::top());

    [[maybe_unused]] auto g = rv_widen.grade();
    [[maybe_unused]] auto vc = composed.peek().c;

    cipher_tier_tag::HotTier::element_type e{};
    [[maybe_unused]] CipherTierTag rec = e;
}

void tolerance_lattice_runs_at_run_time() {
    using namespace fa;
    using namespace fl;
    using namespace fl::detail::tolerance_lattice_self_test;
    Tolerance a = Tolerance::RELAXED;
    Tolerance b = Tolerance::BITEXACT;
    [[maybe_unused]] bool l1 = ToleranceLattice::leq(a, b);
    [[maybe_unused]] Tolerance j1 = ToleranceLattice::join(a, b);
    [[maybe_unused]] Tolerance m1 = ToleranceLattice::meet(a, b);
    [[maybe_unused]] Tolerance bot = ToleranceLattice::bottom();
    [[maybe_unused]] Tolerance top = ToleranceLattice::top();

    Tolerance fp16 = Tolerance::ULP_FP16;
    Tolerance fp32 = Tolerance::ULP_FP32;
    [[maybe_unused]] Tolerance j2 = ToleranceLattice::join(fp16, fp32);
    [[maybe_unused]] Tolerance m2 = ToleranceLattice::meet(fp16, fp32);

    OneByteValue v{42};
    PinnedGraded<tolerance::BitexactTier, OneByteValue> initial{test_authority::key(), v,
                                                               tolerance::BitexactTier::bottom()};
    auto widened = initial.weaken(tolerance::BitexactTier::top());
    auto composed = initial.compose(widened);
    auto rv_widen = std::move(widened).weaken(tolerance::BitexactTier::top());

    [[maybe_unused]] auto g = rv_widen.grade();
    [[maybe_unused]] auto vc = composed.peek().c;

    tolerance::BitexactTier::element_type e{};
    [[maybe_unused]] Tolerance rec = e;
}

void wait_lattice_runs_at_run_time() {
    using namespace fa;
    using namespace fl;
    using namespace fl::detail::wait_lattice_self_test;
    WaitStrategy a = WaitStrategy::Block;
    WaitStrategy b = WaitStrategy::SpinPause;
    [[maybe_unused]] bool l1 = WaitLattice::leq(a, b);
    [[maybe_unused]] WaitStrategy j1 = WaitLattice::join(a, b);
    [[maybe_unused]] WaitStrategy m1 = WaitLattice::meet(a, b);
    [[maybe_unused]] WaitStrategy bot = WaitLattice::bottom();
    [[maybe_unused]] WaitStrategy topv = WaitLattice::top();

    WaitStrategy umwait = WaitStrategy::UmwaitC01;
    WaitStrategy futex = WaitStrategy::AcquireWait;
    [[maybe_unused]] WaitStrategy j2 = WaitLattice::join(umwait, futex);
    [[maybe_unused]] WaitStrategy m2 = WaitLattice::meet(umwait, futex);

    OneByteValue v{42};
    PinnedGraded<wait_strategy::SpinPauseStrategy, OneByteValue> initial{test_authority::key(), v,
                                                                        wait_strategy::SpinPauseStrategy::bottom()};
    auto widened = initial.weaken(wait_strategy::SpinPauseStrategy::top());
    auto composed = initial.compose(widened);
    auto rv_widen = std::move(widened).weaken(wait_strategy::SpinPauseStrategy::top());

    [[maybe_unused]] auto g = rv_widen.grade();
    [[maybe_unused]] auto vc = composed.peek().c;

    wait_strategy::SpinPauseStrategy::element_type e{};
    [[maybe_unused]] WaitStrategy rec = e;
}

void lifetime_lattice_runs_at_run_time() {
    using namespace fa;
    using namespace fl;
    using namespace fl::detail::lifetime_lattice_self_test;
    Lifetime a = Lifetime::PER_REQUEST;
    Lifetime b = Lifetime::PER_FLEET;
    [[maybe_unused]] bool l1 = LifetimeLattice::leq(a, b);
    [[maybe_unused]] Lifetime j1 = LifetimeLattice::join(a, b);
    [[maybe_unused]] Lifetime m1 = LifetimeLattice::meet(a, b);
    [[maybe_unused]] Lifetime bot = LifetimeLattice::bottom();
    [[maybe_unused]] Lifetime top = LifetimeLattice::top();

    OneByteValue v{42};
    fa::Graded<fa::ModalityKind::Comonad, lifetime::PerFleetTier, OneByteValue> initial{
        test_authority::key(), v, lifetime::PerFleetTier::bottom()};
    auto widened = initial.weaken(lifetime::PerFleetTier::top());
    auto composed = initial.compose(widened);
    auto rv_widen = std::move(widened).weaken(lifetime::PerFleetTier::top());

    // extract() is reachable only because the modality is Comonad.
    auto extracted = std::move(composed).extract();

    [[maybe_unused]] auto g = rv_widen.grade();
    [[maybe_unused]] auto vc = extracted.c;

    lifetime::PerFleetTier::element_type e{};
    [[maybe_unused]] Lifetime rec = e;
}

void vendor_lattice_runs_at_run_time() {
    using namespace fa;
    using namespace fl;
    using namespace fl::detail::vendor_lattice_self_test;
    VendorBackend a = VendorBackend::NV;
    VendorBackend b = VendorBackend::AMD;
    [[maybe_unused]] bool l1 = VendorLattice::leq(a, b);
    [[maybe_unused]] VendorBackend j1 = VendorLattice::join(a, b);
    [[maybe_unused]] VendorBackend m1 = VendorLattice::meet(a, b);
    [[maybe_unused]] VendorBackend bot = VendorLattice::bottom();
    [[maybe_unused]] VendorBackend topv = VendorLattice::top();

    VendorBackend portable = VendorBackend::Portable;
    [[maybe_unused]] VendorBackend j2 = VendorLattice::join(portable, a);
    [[maybe_unused]] VendorBackend m2 = VendorLattice::meet(portable, b);

    OneByteValue v{42};
    PinnedGraded<vendor_backend::PortableVendor, OneByteValue> initial{test_authority::key(), v,
                                                                      vendor_backend::PortableVendor::bottom()};
    auto widened = initial.weaken(vendor_backend::PortableVendor::top());
    auto composed = initial.compose(widened);
    auto rv_widen = std::move(widened).weaken(vendor_backend::PortableVendor::top());

    [[maybe_unused]] auto g = rv_widen.grade();
    [[maybe_unused]] auto vc = composed.peek().c;

    vendor_backend::PortableVendor::element_type e{};
    [[maybe_unused]] VendorBackend rec = e;
}

void recipe_family_lattice_runs_at_run_time() {
    using namespace fa;
    using namespace fl;
    using namespace fl::detail::recipe_family_lattice_self_test;
    RecipeFamily bot = RecipeFamilyLattice::bottom();
    RecipeFamily topv = RecipeFamilyLattice::top();
    RecipeFamily kahan = RecipeFamily::Kahan;
    [[maybe_unused]] bool l = RecipeFamilyLattice::leq(bot, topv);
    [[maybe_unused]] auto j = RecipeFamilyLattice::join(kahan, topv);
    [[maybe_unused]] auto m = RecipeFamilyLattice::meet(kahan, bot);

    auto sib_join = RecipeFamilyLattice::join(RecipeFamily::Linear, RecipeFamily::Pairwise);
    if (sib_join != RecipeFamily::Any) std::abort();

    auto sib_meet = RecipeFamilyLattice::meet(RecipeFamily::Linear, RecipeFamily::Pairwise);
    if (sib_meet != RecipeFamily::None) std::abort();

    // A wider family is the stronger claim.  A stored family grades
    // through the order dual, and weaken() narrows it toward None.
    using RecipeGraded = Graded<ModalityKind::Absolute, DualLattice<RecipeFamilyLattice>, int>;
    RecipeGraded v{test_authority::key(), 42, RecipeFamily::Kahan};
    if (v.grade() != RecipeFamily::Kahan || v.peek() != 42) std::abort();
    if (v.weaken(RecipeFamily::None).grade() != RecipeFamily::None) std::abort();
}

void barrier_strength_lattice_runs_at_run_time() {
    using namespace fa;
    using namespace fl;
    using namespace fl::detail::barrier_strength_lattice_self_test;
    BarrierStrength a = BarrierStrength::None;
    BarrierStrength b = BarrierStrength::FullFence;
    [[maybe_unused]] bool rl = BarrierStrengthLattice::leq(a, b);
    [[maybe_unused]] BarrierStrength rj = BarrierStrengthLattice::join(a, b);
    [[maybe_unused]] BarrierStrength rm = BarrierStrengthLattice::meet(a, b);
    [[maybe_unused]] BarrierStrength bot = BarrierStrengthLattice::bottom();
    [[maybe_unused]] BarrierStrength topv = BarrierStrengthLattice::top();

    BarrierStrength acqrel = BarrierStrength::AcqRel;
    BarrierStrength seqcst = BarrierStrength::SeqCst;
    [[maybe_unused]] bool seqcst_satisfies_acqrel = BarrierStrengthLattice::leq(acqrel, seqcst);
    [[maybe_unused]] BarrierStrength rj2 = BarrierStrengthLattice::join(acqrel, seqcst);
    [[maybe_unused]] BarrierStrength rm2 = BarrierStrengthLattice::meet(acqrel, seqcst);

    BarrierStrengthLattice::At<BarrierStrength::ReleaseStore>::element_type rel_pin{};
    [[maybe_unused]] BarrierStrength rel_recovered = rel_pin;

    OneByteValue payload{9};
    using NoneFence = BarrierStrengthLattice::At<BarrierStrength::None>;
    PinnedGraded<NoneFence, OneByteValue> initial{test_authority::key(), payload, NoneFence::bottom()};
    auto widened = initial.weaken(BarrierStrengthLattice::At<BarrierStrength::None>::top());
    auto composed = initial.compose(widened);
    [[maybe_unused]] auto grade = widened.grade();
    [[maybe_unused]] auto peeked = composed.peek().c;
}

void memory_scope_lattice_runs_at_run_time() {
    using namespace fa;
    using namespace fl;
    using namespace fl::detail::memory_scope_lattice_self_test;
    MemoryScope a = MemoryScope::Cta;
    MemoryScope b = MemoryScope::Inner;
    [[maybe_unused]] bool l1 = MemoryScopeLattice::leq(a, b);
    [[maybe_unused]] MemoryScope j1 = MemoryScopeLattice::join(a, b);
    [[maybe_unused]] MemoryScope m1 = MemoryScopeLattice::meet(a, b);
    [[maybe_unused]] MemoryScope bot = MemoryScopeLattice::bottom();
    [[maybe_unused]] MemoryScope topv = MemoryScopeLattice::top();

    MemoryScope warp = MemoryScope::Warp;
    [[maybe_unused]] bool within = MemoryScopeLattice::leq(warp, a);  // Warp ⊑ Cta
    [[maybe_unused]] bool xtrunk = mem_scope_same_trunk(a, b);  // false

    OneByteValue v{42};
    PinnedGraded<memory_scope::SystemScope, OneByteValue> initial{test_authority::key(), v,
                                                                 memory_scope::SystemScope::bottom()};
    auto widened = initial.weaken(memory_scope::SystemScope::top());
    auto composed = initial.compose(widened);
    [[maybe_unused]] auto g = widened.grade();
    [[maybe_unused]] auto vc = composed.peek().c;

    memory_scope::SystemScope::element_type e{};
    [[maybe_unused]] MemoryScope rec = e;
}

void product_lattice_runs_at_run_time() {
    using namespace fa;
    using namespace fl;
    using namespace fl::detail::product_lattice_self_test;
    using L = P_u8u8;
    L::element_type lo{1, 2};
    L::element_type hi{5, 7};
    [[maybe_unused]] bool le = L::leq(lo, hi);
    [[maybe_unused]] L::element_type jn = L::join(lo, hi);
    [[maybe_unused]] L::element_type mt = L::meet(lo, hi);
    [[maybe_unused]] L::element_type bt = L::bottom();
    [[maybe_unused]] L::element_type tp = L::top();

    OneByteValue v{42};
    BudgetU8U8<OneByteValue> initial{test_authority::key(), v, lo};
    auto widened = initial.weaken(hi);
    auto composed = initial.compose(widened);
    auto rv_widen = std::move(widened).weaken(L::top());
    auto rv_comp = std::move(initial).compose(composed);

    [[maybe_unused]] auto g1 = composed.grade();
    [[maybe_unused]] auto v1 = composed.peek().c;
    [[maybe_unused]] auto v2 = std::move(rv_comp).consume().c;
    [[maybe_unused]] auto _r = std::move(rv_widen).consume().c;

    using N = P_u8u8u8;
    N::element_type n_lo{};
    N::get<0>(n_lo) = 1;
    N::get<1>(n_lo) = 2;
    N::get<2>(n_lo) = 3;
    N::element_type n_hi{};
    N::get<0>(n_hi) = 4;
    N::get<1>(n_hi) = 5;
    N::get<2>(n_hi) = 6;

    [[maybe_unused]] bool n_le = N::leq(n_lo, n_hi);
    [[maybe_unused]] N::element_type n_jn = N::join(n_lo, n_hi);
    [[maybe_unused]] N::element_type n_mt = N::meet(n_lo, n_hi);
    [[maybe_unused]] N::element_type n_bt = N::bottom();
    [[maybe_unused]] N::element_type n_tp = N::top();

    OneByteValue n_v{17};
    Budgeted3U8<OneByteValue> n_initial{test_authority::key(), n_v, n_lo};
    auto n_widened = n_initial.weaken(n_hi);
    auto n_composed = n_initial.compose(n_widened);
    [[maybe_unused]] auto n_g = n_composed.grade();
    [[maybe_unused]] auto n_vc = n_composed.peek().c;
}

void pinning_requirement_lattice_runs_at_run_time() {
    using namespace fa;
    using namespace fl;
    using namespace fl::detail::pinning_requirement_lattice_self_test;
    PinningRequirement a = PinningRequirement::NotRequired;
    PinningRequirement b = PinningRequirement::CrossSocketSafe;
    [[maybe_unused]] bool l1 = PinningRequirementLattice::leq(a, b);
    [[maybe_unused]] PinningRequirement j1 = PinningRequirementLattice::join(a, b);
    [[maybe_unused]] PinningRequirement m1 = PinningRequirementLattice::meet(a, b);
    [[maybe_unused]] PinningRequirement bot = PinningRequirementLattice::bottom();
    [[maybe_unused]] PinningRequirement top = PinningRequirementLattice::top();

    PinningRequirement core = PinningRequirement::PerCore;
    PinningRequirement socket = PinningRequirement::PerSocket;
    [[maybe_unused]] PinningRequirement j2 = PinningRequirementLattice::join(core, socket);
    [[maybe_unused]] PinningRequirement m2 = PinningRequirementLattice::meet(core, socket);

    OneByteValue v{42};
    PinnedGraded<pinning_requirement::PerCorePin, OneByteValue> initial{test_authority::key(), v,
                                                                       pinning_requirement::PerCorePin::bottom()};
    auto widened = initial.weaken(pinning_requirement::PerCorePin::top());
    auto composed = initial.compose(widened);
    auto rv_widen = std::move(widened).weaken(pinning_requirement::PerCorePin::top());

    [[maybe_unused]] auto g = rv_widen.grade();
    [[maybe_unused]] auto vc = composed.peek().c;

    pinning_requirement::PerCorePin::element_type e{};
    [[maybe_unused]] PinningRequirement rec = e;
}

void suspend_behavior_lattice_runs_at_run_time() {
    using namespace fa;
    using namespace fl;
    using namespace fl::detail::suspend_behavior_lattice_self_test;
    SuspendBehavior a = SuspendBehavior::Unknown;
    SuspendBehavior b = SuspendBehavior::KeepsTicking;
    [[maybe_unused]] bool l1 = SuspendBehaviorLattice::leq(a, b);
    [[maybe_unused]] SuspendBehavior j1 = SuspendBehaviorLattice::join(a, b);
    [[maybe_unused]] SuspendBehavior m1 = SuspendBehaviorLattice::meet(a, b);
    [[maybe_unused]] SuspendBehavior bot = SuspendBehaviorLattice::bottom();
    [[maybe_unused]] SuspendBehavior top = SuspendBehaviorLattice::top();

    SuspendBehavior mono = SuspendBehavior::PausesOnSuspend;
    SuspendBehavior boot = SuspendBehavior::KeepsTicking;
    [[maybe_unused]] SuspendBehavior j2 = SuspendBehaviorLattice::join(mono, boot);
    [[maybe_unused]] SuspendBehavior m2 = SuspendBehaviorLattice::meet(mono, boot);

    OneByteValue v{42};
    PinnedGraded<suspend_behavior::KeepsTickingClock, OneByteValue> initial{
        test_authority::key(), v, suspend_behavior::KeepsTickingClock::bottom()};
    auto widened = initial.weaken(suspend_behavior::KeepsTickingClock::top());
    auto composed = initial.compose(widened);
    auto rv_widen = std::move(widened).weaken(suspend_behavior::KeepsTickingClock::top());

    [[maybe_unused]] auto g = rv_widen.grade();
    [[maybe_unused]] auto vc = composed.peek().c;

    suspend_behavior::KeepsTickingClock::element_type e{};
    [[maybe_unused]] SuspendBehavior rec = e;
}

void clock_source_lattice_runs_at_run_time() {
    using namespace fa;
    using namespace fl;
    using namespace fl::detail::clock_source_lattice_self_test;
    ClockSource source = ClockSource::TscRaw;
    auto tsc_point = clock_source_project(source);
    [[maybe_unused]] DetSafeTier det = ClockSourceLattice::get<0>(tsc_point);
    [[maybe_unused]] SuspendBehavior suspend = ClockSourceLattice::get<1>(tsc_point);
    [[maybe_unused]] PinningRequirement pin = ClockSourceLattice::get<2>(tsc_point);

    auto boot_point = clock_source_project(ClockSource::Boot);
    [[maybe_unused]] bool le = ClockSourceLattice::leq(boot_point, tsc_point);
    [[maybe_unused]] auto jn = ClockSourceLattice::join(boot_point, tsc_point);
    [[maybe_unused]] auto mt = ClockSourceLattice::meet(boot_point, tsc_point);
    [[maybe_unused]] auto bt = ClockSourceLattice::bottom();
    [[maybe_unused]] auto tp = ClockSourceLattice::top();

    [[maybe_unused]] auto built = ClockSourceLattice::make_point(det, suspend, pin);

    // A point stored beside a value grades through the order dual.
    // weaken() moves from the counter read to the weaker boot clock, and
    // then to the weakest point.
    EightByteValue payload{42};
    ClockGraded<EightByteValue> initial{test_authority::key(), payload, tsc_point};
    auto widened = initial.weaken(boot_point);
    auto composed = initial.compose(widened);
    auto rv_widen = std::move(widened).weaken(ClockSourceLattice::bottom());
    if (!(composed.grade() == boot_point) || !(rv_widen.grade() == ClockSourceLattice::bottom())) std::abort();

    [[maybe_unused]] auto grade = rv_widen.grade();
    [[maybe_unused]] auto value = composed.peek().v;
    [[maybe_unused]] auto moved = std::move(composed).consume().v;
}

// These two had no caller at all before this file gained one.  Both
// headers were ported to foundation to unblock the OS wrappers, and the
// port copied each self test across without registering it with a test.
// The old tree's copies ARE called, so the same body passed for
// crucible:: and had never once run for foundation::, which left a
// divergence between the two copies invisible.  Copying a self test
// along with a header is not the same as porting it, and the difference
// is silent.

void affinity_lattice_runs_at_run_time() {
    using namespace fa;
    using namespace fl;
    using namespace fl::detail::affinity_lattice_self_test;
    AffinityMask bot = AffinityLattice::bottom();
    AffinityMask topv = AffinityLattice::top();
    AffinityMask bergamo_full = AffinityMask::range(0, 191);

    [[maybe_unused]] bool l1 = AffinityLattice::leq(bot, topv);
    [[maybe_unused]] bool l2 = AffinityLattice::leq(bergamo_full, topv);
    [[maybe_unused]] AffinityMask j = AffinityLattice::join(bergamo_full, bot);
    [[maybe_unused]] AffinityMask m = AffinityLattice::meet(bergamo_full, topv);

    if (bergamo_full.popcount() != 192) std::abort();

    AffinityMask core_0 = AffinityMask::single(0);
    AffinityMask core_191 = AffinityMask::single(191);
    AffinityMask core_255 = AffinityMask::single(255);
    if (!core_0.contains(0)) std::abort();
    if (!core_191.contains(191)) std::abort();
    if (!core_255.contains(255)) std::abort();
    if (core_0.contains(1)) std::abort();

    AffinityMask joined = AffinityLattice::join(core_0, core_191);
    if (!joined.contains(0)) std::abort();
    if (!joined.contains(191)) std::abort();

    AffinityMask intersected = AffinityLattice::meet(AffinityMask::range(0, 127), AffinityMask::range(64, 191));
    if (intersected.popcount() != 64) std::abort();

    using AffinityGraded = Graded<ModalityKind::Absolute, AffinityLattice, double>;
    AffinityGraded v{test_authority::key(), 3.14, AffinityMask::range(0, 31)};
    [[maybe_unused]] auto g = v.grade();
    [[maybe_unused]] auto vp = v.peek();
}

// The node order at run time: two siblings join to the wildcard and meet
// at None, and a graded value carries a concrete node.
void numa_node_lattice_runs_at_run_time() {
    using namespace fa;
    using namespace fl;
    NumaNodeId const first{0};
    NumaNodeId const second{1};
    if (NumaNodeLattice::join(first, second) != NumaNodeId::Any) std::abort();
    if (NumaNodeLattice::meet(first, second) != NumaNodeId::None) std::abort();
    if (!NumaNodeLattice::leq(NumaNodeLattice::bottom(), first)) std::abort();
    if (NumaNodeLattice::leq(NumaNodeLattice::top(), first)) std::abort();
    if (!is_concrete_numa_node(second) || is_concrete_numa_node(NumaNodeId::Any)) std::abort();

    // A claim that covers more nodes is the stronger claim.  A stored node
    // grades through the order dual, and weaken() narrows it to None.
    using NumaGraded = Graded<ModalityKind::Absolute, DualLattice<NumaNodeLattice>, int>;
    NumaGraded const value{test_authority::key(), 42, NumaNodeId{2}};
    if (value.grade() != NumaNodeId{2} || value.peek() != 42) std::abort();
    if (value.weaken(NumaNodeId::None).grade() != NumaNodeId::None) std::abort();
}

void scheduler_policy_lattice_runs_at_run_time() {
    using namespace fa;
    using namespace fl;
    using namespace fl::detail::scheduler_policy_lattice_self_test;
    SchedulerPolicy a = SchedulerPolicy::Idle;
    SchedulerPolicy b = SchedulerPolicy::Deadline;
    [[maybe_unused]] bool l1 = SchedulerPolicyLattice::leq(a, b);
    [[maybe_unused]] SchedulerPolicy j1 = SchedulerPolicyLattice::join(a, b);
    [[maybe_unused]] SchedulerPolicy m1 = SchedulerPolicyLattice::meet(a, b);
    [[maybe_unused]] SchedulerPolicy bot = SchedulerPolicyLattice::bottom();
    [[maybe_unused]] SchedulerPolicy top = SchedulerPolicyLattice::top();

    SchedulerPolicy rr = SchedulerPolicy::RoundRobin;
    SchedulerPolicy fifo = SchedulerPolicy::Fifo;
    SchedulerPolicy other = SchedulerPolicy::Other;
    [[maybe_unused]] SchedulerPolicy j2 = SchedulerPolicyLattice::join(rr, fifo);
    [[maybe_unused]] SchedulerPolicy m2 = SchedulerPolicyLattice::meet(rr, fifo);
    [[maybe_unused]] bool tsc_ok = SchedulerPolicyLattice::leq(other, fifo);

    OneByteValue v{42};
    PinnedGraded<scheduler_policy::FifoClass, OneByteValue> initial{test_authority::key(), v,
                                                                   scheduler_policy::FifoClass::bottom()};
    auto widened = initial.weaken(scheduler_policy::FifoClass::top());
    auto composed = initial.compose(widened);
    auto rv_widen = std::move(widened).weaken(scheduler_policy::FifoClass::top());

    [[maybe_unused]] auto g = rv_widen.grade();
    [[maybe_unused]] auto vc = composed.peek().c;

    scheduler_policy::FifoClass::element_type e{};
    [[maybe_unused]] SchedulerPolicy rec = e;
}

}  // namespace

int main() {
    affinity_lattice_runs_at_run_time();
    scheduler_policy_lattice_runs_at_run_time();
    chain_lattice_runs_at_run_time();
    det_safe_lattice_runs_at_run_time();
    alloc_class_lattice_runs_at_run_time();
    hot_path_lattice_runs_at_run_time();
    cipher_tier_lattice_runs_at_run_time();
    tolerance_lattice_runs_at_run_time();
    wait_lattice_runs_at_run_time();
    lifetime_lattice_runs_at_run_time();
    vendor_lattice_runs_at_run_time();
    recipe_family_lattice_runs_at_run_time();
    barrier_strength_lattice_runs_at_run_time();
    memory_scope_lattice_runs_at_run_time();
    product_lattice_runs_at_run_time();
    pinning_requirement_lattice_runs_at_run_time();
    suspend_behavior_lattice_runs_at_run_time();
    clock_source_lattice_runs_at_run_time();
    numa_node_lattice_runs_at_run_time();

    // The reflected names reach a runtime context too: the views point
    // at static storage, so a diagnostic can print them.
    volatile int raw_tier = 6;
    const auto tier = static_cast<fl::DetSafeTier>(raw_tier);
    if (fr::enum_name(tier) != "Pure") {
        std::fprintf(stderr, "test_lattices_bands: enum_name(DetSafeTier{6}) is not Pure\n");
        return 1;
    }
    volatile int raw_bad = 200;
    if (fr::enum_name(static_cast<fl::DetSafeTier>(raw_bad)) != "<unknown DetSafeTier>") {
        std::fprintf(stderr, "test_lattices_bands: enum_name(DetSafeTier{200}) is not the sentinel\n");
        return 1;
    }
    return 0;
}
