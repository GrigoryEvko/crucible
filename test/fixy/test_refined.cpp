// Sentinel TU for fixy/Refined.h: the two wrappers collapse to sizeof(T),
// every bare-value constructor is behind its mint, the checked mint runs
// the predicate at consteval and at runtime, the implication relation is
// the admitted namespace and nothing else, and the header's three runtime
// smoke tests run under the test flags.
//
// Ported from test/test_smoke_safety_wrappers.cpp, test/test_safety_compile.cpp
// and test/test_is_refined.cpp, whose Refined cells were calls into the old
// headers' self-tests.  Those self-tests moved with the header; the cells
// here are what the old files never stated.

#include <fixy/Refined.h>

#include <foundation/algebra/GradedTrait.h>
#include <foundation/diag/FailClosed.h>

#include "../foundation/abort_probe.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <meta>
#include <span>
#include <type_traits>
#include <utility>

namespace {

namespace fa = ::foundation::algebra;
using ::fixy::Refined;
using ::fixy::SealedRefined;
using ::foundation::test::aborts;

struct TwoWords {
    std::uint64_t lo = 0;
    std::uint64_t hi = 0;
    [[nodiscard]] constexpr bool operator==(TwoWords const&) const noexcept = default;
};

inline constexpr auto lo_set = [](TwoWords const& w) constexpr noexcept { return w.lo != 0; };

// ── Storage regime 1: the grade lives in the type ────────────────────

static_assert(sizeof(Refined<fixy::positive, int>) == sizeof(int));
static_assert(sizeof(Refined<fixy::positive, double>) == sizeof(double));
static_assert(sizeof(Refined<lo_set, TwoWords>) == sizeof(TwoWords));
static_assert(alignof(Refined<lo_set, TwoWords>) == alignof(TwoWords));
static_assert(sizeof(SealedRefined<lo_set, TwoWords>) == sizeof(TwoWords));
static_assert(alignof(SealedRefined<lo_set, TwoWords>) == alignof(TwoWords));

// ── The diagnostic surface agrees with the substrate ─────────────────

static_assert(fa::GradedWrapper<Refined<fixy::positive, int>>);
static_assert(fa::GradedWrapper<SealedRefined<fixy::positive, int>>);
static_assert(fa::is_graded_wrapper_v<Refined<fixy::positive, int>>);
static_assert(Refined<fixy::positive, int>::modality == fa::ModalityKind::Absolute);
static_assert(SealedRefined<fixy::positive, int>::modality == fa::ModalityKind::Absolute);
static_assert(Refined<fixy::positive, int>::value_type_name().ends_with("int"));
static_assert(Refined<fixy::positive, int>::lattice_name()
              == Refined<fixy::positive, int>::graded_type::lattice_name());
static_assert(SealedRefined<fixy::positive, int>::lattice_name() == Refined<fixy::positive, int>::lattice_name(),
              "sealing changes the wrapper, not the lattice");
static_assert(std::is_same_v<Refined<fixy::positive, int>::lattice_type,
                             fa::lattices::BoolLattice<fixy::refined::predicate_t<fixy::positive>>>);
static_assert(std::is_same_v<Refined<fixy::positive, int>::value_type, int>);

// ── One door ─────────────────────────────────────────────────────────
//
// No public constructor takes a bare value, on either wrapper.  The only
// public constructor with a payload is the proof-carrying transfer from
// a Refined into a SealedRefined.

static_assert(!std::is_constructible_v<Refined<fixy::positive, int>, int>);
static_assert(!std::is_constructible_v<Refined<fixy::positive, int>, int&>);
static_assert(!std::is_default_constructible_v<Refined<fixy::positive, int>>);
static_assert(!std::is_constructible_v<SealedRefined<fixy::positive, int>, int>);
static_assert(!std::is_default_constructible_v<SealedRefined<fixy::positive, int>>);
static_assert(std::is_constructible_v<SealedRefined<fixy::positive, int>, Refined<fixy::positive, int>&&>);
static_assert(!std::is_constructible_v<SealedRefined<fixy::positive, int>, Refined<fixy::positive, int> const&>);
static_assert(!std::is_convertible_v<Refined<fixy::positive, int>, int>);
static_assert(!std::is_convertible_v<int, Refined<fixy::positive, int>>);

// Copy and move preserve the refinement; a move-only payload makes the
// wrapper move-only without disturbing the collapse.
static_assert(std::is_copy_constructible_v<Refined<fixy::positive, int>>);
static_assert(std::is_nothrow_move_constructible_v<Refined<fixy::positive, int>>);
static_assert(std::is_copy_constructible_v<SealedRefined<fixy::positive, int>>);
static_assert(!std::is_copy_constructible_v<fixy::RefinedLinear<fixy::positive, int>>);
static_assert(std::is_move_constructible_v<fixy::RefinedLinear<fixy::positive, int>>);
static_assert(sizeof(fixy::RefinedLinear<fixy::positive, int>) == sizeof(int));

// into takes an rvalue Refined and nothing else, and does not exist on
// the sealed wrapper.
template <typename W>
concept ExtractsLvalue = requires(W& w) { w.into(); };
template <typename W>
concept ExtractsRvalue = requires(W&& w) { std::move(w).into(); };

static_assert(!ExtractsLvalue<Refined<fixy::positive, int>>);
static_assert(ExtractsRvalue<Refined<fixy::positive, int>>);
static_assert(!ExtractsLvalue<SealedRefined<fixy::positive, int>>);
static_assert(!ExtractsRvalue<SealedRefined<fixy::positive, int>>);

// The two refinements are one template with the seal as an argument,
// and the names stay distinct types over it. A parameter that asks for
// one still refuses the other, and neither wrapper grew a byte.
static_assert(std::is_same_v<Refined<fixy::positive, int>, fixy::Refinement<fixy::positive, int, false>>);
static_assert(std::is_same_v<SealedRefined<fixy::positive, int>, fixy::Refinement<fixy::positive, int, true>>);
static_assert(!std::is_same_v<Refined<fixy::positive, int>, SealedRefined<fixy::positive, int>>);
static_assert(!std::is_convertible_v<Refined<fixy::positive, int>, SealedRefined<fixy::positive, int>>);
static_assert(!std::is_convertible_v<SealedRefined<fixy::positive, int>, Refined<fixy::positive, int>>);
static_assert(sizeof(Refined<fixy::positive, int>) == sizeof(int));
static_assert(sizeof(SealedRefined<fixy::positive, int>) == sizeof(int));

// The seal is readable off the type, and the trait is a view of it.
static_assert(!Refined<fixy::positive, int>::is_sealed);
static_assert(SealedRefined<fixy::positive, int>::is_sealed);
static_assert(fixy::refined_is_sealed_v<SealedRefined<fixy::positive, int>>);
static_assert(!fixy::refined_is_sealed_v<Refined<fixy::positive, int>>);

// One reflection query answers for both, through either alias and
// through a reference to one.
static_assert(fixy::is_refined_v<Refined<fixy::positive, int>>);
static_assert(fixy::is_refined_v<SealedRefined<fixy::positive, int>>);
static_assert(fixy::is_refined_v<const SealedRefined<fixy::positive, int>&>);
static_assert(!fixy::is_refined_v<int>);

// Sealing an ordinary refinement is explicit and one-way. There is no
// constructor back.
static_assert(std::is_constructible_v<SealedRefined<fixy::positive, int>, Refined<fixy::positive, int>&&>);
static_assert(!std::is_constructible_v<Refined<fixy::positive, int>, SealedRefined<fixy::positive, int>&&>);
static_assert(!std::is_constructible_v<SealedRefined<fixy::positive, int>, Refined<fixy::positive, int>&>);

// ── The mints in a constant expression ───────────────────────────────

constexpr Refined<fixy::positive, int> minted = fixy::mint_refined<fixy::positive>(7);
static_assert(minted.value() == 7);
static_assert(fixy::mint_refined<fixy::positive, int>(7) == minted);
static_assert(fixy::mint_refined<fixy::positive>(6) < minted);
static_assert((fixy::mint_refined<fixy::positive>(8) <=> minted) == std::strong_ordering::greater);

// The trusted door admits what the predicate refuses, which is its
// whole reason to exist and why it is a separate spelling.
constexpr Refined<fixy::positive, int> taken_on_faith = fixy::mint_refined_trusted<fixy::positive>(-1);
static_assert(taken_on_faith.value() == -1);
static_assert(!fixy::positive(taken_on_faith.value()));

constexpr SealedRefined<fixy::positive, int> sealed = fixy::mint_sealed_refined<fixy::positive>(9);
static_assert(sealed.value() == 9);
static_assert(fixy::mint_sealed_refined_trusted<fixy::positive>(0).value() == 0);

// The trusted mint asks nothing of the predicate, so a predicate the
// payload cannot be fed to is still admissible there.
template <auto Pred, typename T>
concept CheckedMintable = requires(T v) { fixy::mint_refined<Pred>(std::move(v)); };
template <auto Pred, typename T>
concept TrustedMintable = requires(T v) { fixy::mint_refined_trusted<Pred>(std::move(v)); };

static_assert(CheckedMintable<fixy::positive, int>);
static_assert(!CheckedMintable<fixy::non_null, int>, "non_null takes a pointer; the checked door refuses an int");
static_assert(TrustedMintable<fixy::non_null, int>);

// A predicate that returns nothing fails the convertible-to-bool half
// of the gate.
inline constexpr auto returns_void = [](auto) constexpr noexcept -> void {};
static_assert(!CheckedMintable<returns_void, int>);
static_assert(TrustedMintable<returns_void, int>);

// ── The implication relation is the namespace ────────────────────────

namespace ffc = ::foundation::fail_closed;
namespace rel = ::fixy::refined;

// The five atomic edges are members; every other atomic pair is not.
static_assert(ffc::edge_count<^^rel::admitted_implications>() == 5);
static_assert(ffc::Admitted<^^rel::admitted_implications, rel::predicate_t<fixy::positive>,
                            rel::predicate_t<fixy::non_negative>>);
static_assert(!ffc::Admitted<^^rel::admitted_implications, rel::predicate_t<fixy::non_negative>,
                             rel::predicate_t<fixy::positive>>);
static_assert(fixy::implies_v<fixy::positive, fixy::non_negative>);
static_assert(fixy::implies_v<fixy::positive, fixy::non_zero>);
static_assert(fixy::implies_v<fixy::power_of_two, fixy::non_zero>);
static_assert(!fixy::implies_v<fixy::non_negative, fixy::positive>);
static_assert(!fixy::implies_v<fixy::non_zero, fixy::positive>);
static_assert(!fixy::implies_v<fixy::positive, fixy::positive>, "reflexivity is supplied by the subsort machinery");
static_assert(!fixy::implies_v<fixy::is_zero, fixy::non_negative>, "is_zero takes part in no implication");

// A parameterised family answers through its rule, and a rule declared
// anywhere but in the namespace is inert.
static_assert(fixy::implies_v<fixy::aligned<64>, fixy::aligned<8>>);
static_assert(!fixy::implies_v<fixy::aligned<8>, fixy::aligned<64>>);
static_assert(!fixy::implies_v<fixy::aligned<48>, fixy::aligned<32>>, "48 is not a multiple of 32");
static_assert(fixy::implies_v<fixy::bounded_above<10>, fixy::bounded_above<20>>);
static_assert(!fixy::implies_v<fixy::bounded_above<20>, fixy::bounded_above<10>>);
static_assert(fixy::implies_v<fixy::in_range<3, 7>, fixy::bounded_above<7>>);
static_assert(!fixy::implies_v<fixy::in_range<3, 7>, fixy::bounded_above<8>>, "the relation does not chain");

inline constexpr auto even = [](auto x) constexpr noexcept { return x % 2 == 0; };

namespace not_the_relation {
inline constexpr ffc::edge<rel::predicate_t<even>, rel::predicate_t<fixy::non_negative>> even_implies_non_negative{};
}  // namespace not_the_relation

static_assert(ffc::Admitted<^^not_the_relation, rel::predicate_t<even>, rel::predicate_t<fixy::non_negative>>);
static_assert(!fixy::implies_v<even, fixy::non_negative>, "an edge outside admitted_implications is inert");

// Every member of the namespace is an edge or a rule, and the fold the
// header runs over them is reachable from here.
static_assert(fixy::detail::refined_self_test::every_edge_holds());

// A family says it is one by deriving rule_family<itself>, so the
// count reads classes rather than the marker variables that used to sit
// beside them.  A family written without its variable was inert under
// the old shape; under this one it cannot be.
[[nodiscard]] consteval std::size_t rule_count() noexcept {
    std::size_t count = 0;
    for (auto const m : std::meta::members_of(^^rel::admitted_implications, std::meta::access_context::unchecked())) {
        if (!std::meta::is_type(m) || std::meta::is_type_alias(m) || !std::meta::is_class_type(m)) continue;
        ++count;
    }
    return count;
}
static_assert(rule_count() == 19, "one rule per parameterised family: four from Refined.h's own families, "
                                  "the two combinator rules, and thirteen from the algebra");

// ── The extraction traits ────────────────────────────────────────────

static_assert(fixy::IsRefined<Refined<fixy::positive, int>>);
static_assert(fixy::IsRefined<SealedRefined<fixy::positive, int> const&>);
static_assert(!fixy::IsRefined<int>);
static_assert(!fixy::IsRefined<fixy::LinearRefined<fixy::positive, int>>, "a Linear over a Refined is a Linear");
static_assert(std::is_same_v<fixy::refined_value_t<Refined<lo_set, TwoWords>>, TwoWords>);
static_assert(std::is_same_v<fixy::refined_predicate_type_t<Refined<lo_set, TwoWords>>, rel::predicate_t<lo_set>>);
static_assert(fixy::refined_is_sealed_v<SealedRefined<lo_set, TwoWords>>);
static_assert(!fixy::refined_is_sealed_v<Refined<lo_set, TwoWords>>);

// A class derived from a Refined is not a Refined: the traits are strict
// identity, so a wrapper cannot inherit its way into a refinement.
struct DerivedFromRefined : Refined<fixy::positive, int> {};
static_assert(!fixy::IsRefined<DerivedFromRefined>);

// ── Runtime cells ────────────────────────────────────────────────────

int check_value_paths() {
    int volatile vol = 42;
    int seed = vol;

    Refined<fixy::positive, int> r = fixy::mint_refined<fixy::positive>(seed);
    if (r.value() != 42) return 10;

    Refined<fixy::positive, int> copied = r;
    if (!(copied == r)) return 11;
    copied = fixy::mint_refined<fixy::positive>(seed + 1);
    if (!(r < copied)) return 12;

    int out = std::move(copied).into();
    if (out != 43) return 13;

    SealedRefined<fixy::positive, int> from_r{std::move(r)};
    if (from_r.value() != 42) return 14;

    TwoWords words{.lo = static_cast<std::uint64_t>(seed), .hi = 0};
    Refined<lo_set, TwoWords> wrapped = fixy::mint_refined<lo_set>(words);
    if (!(wrapped.value() == words)) return 15;

    std::array<int, 3> arr{1, 2, 3};
    fixy::NonEmptySpan<int> span = fixy::mint_refined<fixy::length_ge<std::size_t{1}>>(std::span<int>{arr});
    if (span.value().size() != 3) return 16;

    int* target = &seed;
    fixy::NonNull<int*> non_null = fixy::mint_refined<fixy::non_null>(target);
    if (non_null.value() != &seed) return 17;

    // The relation the pointer edges state, witnessed on a value.
    if (!fixy::non_zero(non_null.value())) return 18;
    int* null = nullptr;
    if (fixy::non_zero(null)) return 19;
    if (!fixy::is_zero(null)) return 20;
    return 0;
}

// The checked mint aborts at runtime on a value the predicate refuses;
// the trusted mint does not.
int check_contracts_abort() {
    int volatile vol = -1;
    int const refused = vol;

    if (!aborts([&] { (void)fixy::mint_refined<fixy::positive>(refused); })) return 30;
    if (!aborts([&] { (void)fixy::mint_sealed_refined<fixy::positive>(refused); })) return 31;
    if (aborts([&] { (void)fixy::mint_refined_trusted<fixy::positive>(refused); })) return 32;
    if (aborts([&] { (void)fixy::mint_sealed_refined_trusted<fixy::positive>(refused); })) return 33;

    int const admitted = -vol;
    if (aborts([&] { (void)fixy::mint_refined<fixy::positive>(admitted); })) return 34;

    fixy::Bounded<0, 100, int> bounded = fixy::mint_refined<fixy::in_range<0, 100>>(admitted);
    if (bounded.value() != 1) return 35;
    int const beyond = 101;
    if (!aborts([&] { (void)fixy::mint_refined<fixy::in_range<0, 100>>(beyond); })) return 36;
    return 0;
}

}  // namespace

int main() {
    ::fixy::detail::refined_self_test::runtime_smoke_test();
    ::fixy::detail::sealed_refined_self_test::runtime_smoke_test();
    ::fixy::detail::refined_algebra_self_test::runtime_smoke_test();

    if (int rc = check_value_paths(); rc != 0) return rc;
    if (int rc = check_contracts_abort(); rc != 0) return rc;
    return 0;
}
