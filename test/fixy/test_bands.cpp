// Sentinel TU for fixy/Bands.h.  Each band is the Graded carrier over a
// pinned grade, so the pins are the carrier's own: a band costs
// sizeof(T) at every tier, its diagnostic surface is the substrate's,
// at_bottom and weaken are the singleton identity, and relax rebinds
// the type down the chain only.
//
// The tiers are walked by reflection over each band's enum, so a new
// tier is covered the moment it is declared.

#include <fixy/Bands.h>

#include <foundation/algebra/Graded.h>
#include <foundation/algebra/GradedTrait.h>
#include <foundation/algebra/Modality.h>
#include <foundation/reflect/Enumerate.h>

#include <cstdint>
#include <cstdio>
#include <memory>
#include <meta>
#include <string_view>
#include <type_traits>
#include <utility>

namespace {

namespace fa = ::foundation::algebra;
namespace fr = ::foundation::reflect;

struct TwoWords {
    std::uint64_t lo{0};
    std::uint64_t hi{0};
    constexpr bool operator==(TwoWords const&) const noexcept = default;
};

struct MoveOnlyValue {
    int v{0};
    constexpr MoveOnlyValue() = default;
    constexpr explicit MoveOnlyValue(int x) noexcept : v{x} {}
    MoveOnlyValue(MoveOnlyValue const&) = delete;
    MoveOnlyValue& operator=(MoveOnlyValue const&) = delete;
    constexpr MoveOnlyValue(MoveOnlyValue&&) noexcept = default;
    constexpr MoveOnlyValue& operator=(MoveOnlyValue&&) noexcept = default;
};

// A band over every tier of a lattice costs sizeof(T), aligns as T, is
// a Graded specialization with the substrate's diagnostic surface, and
// answers the tier queries with the tier it was named with.
template <typename L, template <auto, class> class Band>
[[nodiscard]] consteval bool every_tier_is_a_band() noexcept {
    using E = typename L::element_type;
    static constexpr auto enumerators = std::define_static_array(std::meta::enumerators_of(^^E));
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wshadow"
    template for (constexpr auto en : enumerators) {
        constexpr E tier = [:en:];
        using BInt = Band<tier, int>;
        using BTwo = Band<tier, TwoWords>;
        using BMove = Band<tier, MoveOnlyValue>;
        if (sizeof(BInt) != sizeof(int)) return false;
        if (sizeof(BTwo) != sizeof(TwoWords)) return false;
        if (alignof(BTwo) != alignof(TwoWords)) return false;
        if (sizeof(BMove) != sizeof(MoveOnlyValue)) return false;
        if (!fa::IsGraded<BInt>) return false;
        if (!fa::is_graded_specialization_v<BInt const&>) return false;
        if (BInt::modality != fa::ModalityKind::Absolute) return false;
        if (BInt::modality_name() != "Absolute") return false;
        if (BInt::lattice_name() != L::template At<tier>::name()) return false;
        if (!BInt::value_type_name().ends_with("int")) return false;
        if (!std::is_same_v<typename BInt::value_type, int>) return false;
        if (!std::is_same_v<typename BInt::lattice_type, typename L::template At<tier>>) return false;
        if (!fixy::IsBand<BInt>) return false;
        if (!fixy::IsBandOf<L, BInt>) return false;
        if (!fixy::is_band_of_v<L, BTwo const&>) return false;
        if (fixy::band_tier_v<BInt> != tier) return false;
        if (!std::is_same_v<fixy::band_lattice_t<BInt>, L>) return false;
        if (!std::is_same_v<fixy::band_tier_t<BInt>, E>) return false;
        if (!std::is_same_v<fixy::band_value_t<BMove>, MoveOnlyValue>) return false;
        if (!std::is_copy_constructible_v<BInt>) return false;
        if (std::is_copy_constructible_v<BMove>) return false;
        if (!std::is_move_constructible_v<BMove>) return false;
        // A band satisfies its own tier and the bottom, and every tier
        // above it is not satisfied.
        if (!fixy::satisfies_v<BInt, tier>) return false;
        if (!fixy::satisfies_v<BInt, L::bottom()>) return false;
        if (tier != L::top() && fixy::satisfies_v<BInt, L::top()>) return false;
    }
#pragma GCC diagnostic pop
    return true;
}

static_assert(every_tier_is_a_band<fixy::DetSafeLattice, fixy::DetSafe>());
static_assert(every_tier_is_a_band<fixy::AllocClassLattice, fixy::AllocClass>());
static_assert(every_tier_is_a_band<fixy::HotPathLattice, fixy::HotPath>());
static_assert(every_tier_is_a_band<fixy::CipherTierLattice, fixy::CipherTier>());
static_assert(every_tier_is_a_band<fixy::WaitLattice, fixy::Wait>());
static_assert(every_tier_is_a_band<fixy::ToleranceLattice, fixy::NumericalTier>());
static_assert(every_tier_is_a_band<fixy::LifetimeLattice, fixy::OpaqueLifetime>());

// The old spellings resolve to the same carriers.
static_assert(std::is_same_v<fixy::det_safe::Pure<int>, fixy::DetSafe<fixy::DetSafeTier_v::Pure, int>>);
static_assert(
    std::is_same_v<fixy::det_safe::NDS<int>, fixy::DetSafe<fixy::DetSafeTier_v::NonDeterministicSyscall, int>>);
static_assert(std::is_same_v<fixy::alloc_class::Arena<void*>, fixy::AllocClass<fixy::AllocClassTag_v::Arena, void*>>);
static_assert(std::is_same_v<fixy::hot_path::Hot<bool>, fixy::HotPath<fixy::HotPathTier_v::Hot, bool>>);
static_assert(std::is_same_v<fixy::cipher_tier::Warm<int>, fixy::CipherTier<fixy::CipherTierTag_v::Warm, int>>);
static_assert(std::is_same_v<fixy::wait::Block<int>, fixy::Wait<fixy::WaitStrategy_v::Block, int>>);
static_assert(std::is_same_v<fixy::numerical_tier::Bitexact<int>, fixy::NumericalTier<fixy::Tolerance::BITEXACT, int>>);
static_assert(
    std::is_same_v<fixy::opaque_lifetime::PerFleet<int>, fixy::OpaqueLifetime<fixy::Lifetime_v::PER_FLEET, int>>);

// Two bands over one payload at different tiers, or over different
// lattices at the same ordinal, are different types.
static_assert(!std::is_same_v<fixy::det_safe::Pure<int>, fixy::det_safe::PhiloxRng<int>>);
static_assert(!std::is_same_v<fixy::hot_path::Hot<int>, fixy::cipher_tier::Hot<int>>);

// The old detector answered the same question per band; one query
// answers it for all of them.
static_assert(fixy::is_band_of_v<fixy::DetSafeLattice, fixy::det_safe::Pure<int>>);
static_assert(!fixy::is_band_of_v<fixy::DetSafeLattice, fixy::hot_path::Hot<int>>);
static_assert(!fixy::is_band_v<int>);
static_assert(!fixy::is_band_v<fixy::RecipeSpec<int>>);

// at_bottom on the pinned singleton is the construction door: bottom
// and top coincide, so the value is held at the one tier the type
// names.
constexpr auto pure_from_bottom = fixy::det_safe::Pure<int>::at_bottom(42);
static_assert(pure_from_bottom.peek() == 42);
static_assert(fixy::tier_of(pure_from_bottom) == fixy::DetSafeTier_v::Pure);
static_assert(fixy::det_safe::Pure<int>::at_bottom().peek() == 0);

// weaken on the pinned singleton is the identity: the only grade it can
// be asked for is the one it holds, so the tier cannot move.
constexpr auto pure_weakened = pure_from_bottom.weaken(fixy::det_safe::Pure<int>::lattice_type::top());
static_assert(std::is_same_v<decltype(pure_weakened), fixy::det_safe::Pure<int> const>);
static_assert(pure_weakened.peek() == 42);
static_assert(fixy::tier_of(pure_weakened) == fixy::DetSafeTier_v::Pure);

// relax walks down the chain one class at a time and stays admissible
// at the next consumer; it never walks up.
[[nodiscard]] consteval bool relax_walks_down_the_tolerance_chain() noexcept {
    fixy::NumericalTier<fixy::Tolerance::BITEXACT, int> bitexact{42, {}};
    auto fp64 = fixy::relax<fixy::Tolerance::ULP_FP64>(std::move(bitexact));
    auto fp32 = fixy::relax<fixy::Tolerance::ULP_FP32>(std::move(fp64));
    auto fp16 = fixy::relax<fixy::Tolerance::ULP_FP16>(std::move(fp32));
    auto fp8 = fixy::relax<fixy::Tolerance::ULP_FP8>(std::move(fp16));
    auto int8 = fixy::relax<fixy::Tolerance::ULP_INT8>(std::move(fp8));
    auto relaxed = fixy::relax<fixy::Tolerance::RELAXED>(std::move(int8));
    static_assert(std::is_same_v<decltype(relaxed), fixy::NumericalTier<fixy::Tolerance::RELAXED, int>>);
    return std::move(relaxed).consume() == 42;
}
static_assert(relax_walks_down_the_tolerance_chain());

template <typename B, auto Target>
concept can_relax_rvalue = requires(B&& b) { fixy::relax<Target>(std::move(b)); };
template <typename B, auto Target>
concept can_relax_lvalue = requires(B const& b) { fixy::relax<Target>(b); };

using PureMoveOnly = fixy::det_safe::Pure<MoveOnlyValue>;
static_assert(can_relax_rvalue<PureMoveOnly, fixy::DetSafeTier_v::PhiloxRng>,
              "relax on an rvalue must accept a move-only T.  The rvalue "
              "overload moves through consume(), so it must not carry a "
              "copy_constructible requirement.");
static_assert(!can_relax_lvalue<PureMoveOnly, fixy::DetSafeTier_v::PhiloxRng>,
              "relax on a const lvalue must reject a move-only T.  Without "
              "the copy_constructible requirement the rejection surfaces as a "
              "deleted-copy error inside the substrate instead.");
static_assert(!can_relax_rvalue<fixy::det_safe::PhiloxRng<int>, fixy::DetSafeTier_v::Pure>);
static_assert(!can_relax_rvalue<fixy::opaque_lifetime::PerRequest<int>, fixy::Lifetime_v::PER_FLEET>,
              "A request-scoped value dies with the request; nothing widens it "
              "to fleet scope.");

// The Cipher consumer's gate, written against the generic surface: a
// generic W is admitted when it is a lifetime band that satisfies the
// required scope.
template <typename W, fixy::Lifetime_v Required>
concept commit_admits = fixy::is_band_of_v<fixy::LifetimeLattice, W> && fixy::satisfies_v<W, Required>;

static_assert(commit_admits<fixy::opaque_lifetime::PerFleet<int>, fixy::Lifetime_v::PER_REQUEST>);
static_assert(commit_admits<fixy::opaque_lifetime::PerFleet<int>, fixy::Lifetime_v::PER_FLEET>);
static_assert(!commit_admits<fixy::opaque_lifetime::PerRequest<int>, fixy::Lifetime_v::PER_FLEET>,
              "A request-scoped value does not satisfy the fleet-scoped "
              "requirement, so request-scoped state cannot leak into durable "
              "fleet-wide storage.");
static_assert(!commit_admits<fixy::det_safe::Pure<int>, fixy::Lifetime_v::PER_REQUEST>);

// A RecipeSpec carries both axes at run time.
static_assert(sizeof(fixy::RecipeSpec<int>) >= sizeof(int) + 2);
constexpr fixy::RecipeSpec<int> kahan_fp16{42, {fixy::Tolerance::ULP_FP16, fixy::RecipeFamily::Kahan}};
static_assert(kahan_fp16.peek() == 42);
static_assert(fixy::tolerance_of(kahan_fp16) == fixy::Tolerance::ULP_FP16);
static_assert(fixy::recipe_family_of(kahan_fp16) == fixy::RecipeFamily::Kahan);
static_assert(fixy::admits(kahan_fp16, fixy::Tolerance::ULP_FP8, fixy::RecipeFamily::Kahan));
static_assert(!fixy::admits(kahan_fp16, fixy::Tolerance::BITEXACT, fixy::RecipeFamily::Kahan));
static_assert(!fixy::admits(kahan_fp16, fixy::Tolerance::ULP_FP16, fixy::RecipeFamily::Pairwise));
static_assert(fixy::admits(fixy::RecipeSpec<int>{7, {fixy::Tolerance::BITEXACT, fixy::RecipeFamily::Any}},
                           fixy::Tolerance::ULP_FP32, fixy::RecipeFamily::Kahan),
              "The wildcard family at the strictest tier admits every request.");

}  // namespace

int main() {
    // The header's own cells hold at compile time; here every operation
    // runs with non-constant operands so the substrate's contract
    // predicates run under runtime semantics.
    volatile int raw = 7;
    const int seed = raw;

    fixy::det_safe::Pure<int> pure{seed, {}};
    if (pure.peek() != seed) {
        std::fprintf(stderr, "test_bands: Pure<int>{seed} lost its value\n");
        return 1;
    }
    if (fixy::tier_of(pure) != fixy::DetSafeTier_v::Pure) {
        std::fprintf(stderr, "test_bands: tier_of(Pure<int>) is not Pure\n");
        return 1;
    }

    auto philox = fixy::relax<fixy::DetSafeTier_v::PhiloxRng>(pure);
    if (philox.peek() != seed || fixy::tier_of(philox) != fixy::DetSafeTier_v::PhiloxRng) {
        std::fprintf(stderr, "test_bands: relax<PhiloxRng>(Pure) is wrong\n");
        return 1;
    }
    auto mono = fixy::relax<fixy::DetSafeTier_v::MonotonicClockRead>(std::move(philox));
    if (std::move(mono).consume() != seed) {
        std::fprintf(stderr, "test_bands: consume after two relax steps lost the value\n");
        return 1;
    }

    auto from_bottom = fixy::hot_path::Hot<TwoWords>::at_bottom(TwoWords{1, 2});
    auto weakened = from_bottom.weaken(fixy::hot_path::Hot<TwoWords>::lattice_type::top());
    if (!(weakened.peek() == TwoWords{1, 2})) {
        std::fprintf(stderr, "test_bands: weaken on the singleton changed the value\n");
        return 1;
    }

    fixy::opaque_lifetime::PerFleet<MoveOnlyValue> fleet{MoveOnlyValue{seed}, {}};
    auto request = fixy::relax<fixy::Lifetime_v::PER_REQUEST>(std::move(fleet));
    if (std::move(request).consume().v != seed) {
        std::fprintf(stderr, "test_bands: relax of a move-only fleet value lost it\n");
        return 1;
    }

    fixy::alloc_class::Arena<std::unique_ptr<int>> arena{std::make_unique<int>(seed), {}};
    if (*arena.peek() != seed) {
        std::fprintf(stderr, "test_bands: Arena<unique_ptr<int>> lost its value\n");
        return 1;
    }
    std::unique_ptr<int> released = std::move(arena).consume();
    if (*released != seed) {
        std::fprintf(stderr, "test_bands: consume of Arena<unique_ptr<int>> lost it\n");
        return 1;
    }

    volatile int raw_tier = 3;
    const auto tol = static_cast<fixy::Tolerance>(raw_tier);
    fixy::RecipeSpec<int> spec{seed, {tol, fixy::RecipeFamily::Kahan}};
    if (fixy::tolerance_of(spec) != fixy::Tolerance::ULP_FP16
        || !fixy::admits(spec, fixy::Tolerance::ULP_FP8, fixy::RecipeFamily::Kahan)) {
        std::fprintf(stderr, "test_bands: RecipeSpec admission is wrong at runtime\n");
        return 1;
    }
    if (fr::enum_name(fixy::tolerance_of(spec)) != "ULP_FP16") {
        std::fprintf(stderr, "test_bands: the reflected tolerance name is wrong\n");
        return 1;
    }

    fixy::wait::Block<int> blocked{seed, {}};
    fixy::cipher_tier::Warm<int> warm{seed, {}};
    fixy::numerical_tier::Fp32<int> fp32{seed, {}};
    if (blocked.peek() + warm.peek() + fp32.peek() != 3 * seed) {
        std::fprintf(stderr, "test_bands: a band lost its value\n");
        return 1;
    }
    return 0;
}
