// Sentinel TU for fixy/Refined.h: the two wrappers collapse to sizeof(T),
// every bare-value constructor is behind its mint, the checked mint runs
// the predicate at consteval and at runtime, the implication relation is
// the admitted namespace and nothing else, and every combinator is driven
// with arguments the compiler cannot fold.
//
// Ported from test/test_smoke_safety_wrappers.cpp, test/test_safety_compile.cpp
// and test/test_is_refined.cpp, whose Refined cells were calls into the old
// headers' self-tests.

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

// The mint overload that names the value type, the trusted door's value
// passthrough, and each named alias built over a predicate.
int check_refined_doors_and_aliases() {
    int volatile vol = 42;
    int seed = vol;

    auto named = fixy::mint_refined<fixy::positive, int>(seed);
    if (named.value() != 42) return 40;
    static_assert(std::is_same_v<decltype(named), Refined<fixy::positive, int>>);

    // The trusted door carries a value the predicate would refuse.
    int sentinel = -seed;
    Refined<fixy::positive, int> trusted = fixy::mint_refined_trusted<fixy::positive>(sentinel);
    if (trusted.value() != -42) return 41;

    Refined<fixy::positive, int> lo = fixy::mint_refined<fixy::positive>(seed);
    Refined<fixy::positive, int> hi = fixy::mint_refined<fixy::positive>(seed + 1);
    if ((lo <=> hi) != std::strong_ordering::less) return 42;

    Refined<fixy::bounded_above<128u>, unsigned int> capped =
        fixy::mint_refined<fixy::bounded_above<128u>>(static_cast<unsigned int>(seed));
    if (capped.value() != 42u) return 43;

    Refined<fixy::in_range<0, 100>, int> ranged = fixy::mint_refined<fixy::in_range<0, 100>>(seed);
    if (ranged.value() != 42) return 44;

    int arr[3] = {1, 2, 3};
    std::span<int> sp{arr};
    Refined<fixy::length_ge<1>, std::span<int>> at_least_one = fixy::mint_refined<fixy::length_ge<1>>(sp);
    if (at_least_one.value().size() != 3) return 45;

    fixy::NonZero<int> non_zero = fixy::mint_refined<fixy::non_zero>(seed);
    if (non_zero.value() != 42) return 46;

    fixy::NonEmpty<std::span<int>> non_empty = fixy::mint_refined<fixy::non_empty>(sp);
    if (non_empty.value().size() != 3) return 47;

    // The refinement rides inside the linear wrapper, so the proof
    // survives the one use the wrapper allows.
    fixy::LinearRefined<fixy::positive, int> linear =
        fixy::mint_linear<Refined<fixy::positive, int>>(fixy::mint_refined<fixy::positive>(seed));
    if (linear.peek().value() != 42) return 48;
    if (std::move(linear).consume().into() != 42) return 49;

    return 0;
}

// The sealed twin has no into(), so its doors are the two mints, the
// promotion from an unsealed Refined, and the value-copy operations.
int check_sealed_doors() {
    int volatile vol = 5;
    int seed = vol;

    SealedRefined<fixy::positive, int> five = fixy::mint_sealed_refined<fixy::positive>(seed);
    if (five.value() != 5) return 60;

    auto named = fixy::mint_sealed_refined<fixy::positive, int>(seed);
    if (named.value() != 5) return 61;
    static_assert(std::is_same_v<decltype(named), SealedRefined<fixy::positive, int>>);

    int sentinel = -3 * seed / 5;
    SealedRefined<fixy::positive, int> trusted = fixy::mint_sealed_refined_trusted<fixy::positive>(sentinel);
    if (trusted.value() != -3) return 62;

    Refined<fixy::positive, int> unsealed = fixy::mint_refined<fixy::positive>(seed * 2);
    SealedRefined<fixy::positive, int> promoted{std::move(unsealed)};
    if (promoted.value() != 10) return 63;

    SealedRefined<fixy::positive, int> same = fixy::mint_sealed_refined<fixy::positive>(seed);
    if (!(five == same)) return 64;
    SealedRefined<fixy::positive, int> lower = fixy::mint_sealed_refined<fixy::positive>(seed - 1);
    if ((lower <=> five) != std::strong_ordering::less) return 65;

    SealedRefined<fixy::positive, int> copied = five;
    if (copied.value() != 5) return 66;
    SealedRefined<fixy::positive, int> moved = std::move(copied);
    if (moved.value() != 5) return 67;

    return 0;
}

// Driving the combinators with non-constant arguments and a move-only
// payload catches the consteval, substitution and inline-body bugs that
// a block of compile-time assertions alone would mask.  Nothing here can
// fail at run time; the value is that the bodies are instantiated.
void instantiate_every_combinator_at_runtime() noexcept {
    int volatile vol = 42;  // defeats constant folding
    int x = vol;
    constexpr auto pred = fixy::all_of<fixy::positive, fixy::bounded_above<100>>;
    bool ok = pred(x);
    static_cast<void>(ok);

    Refined<fixy::all_of<fixy::positive, fixy::bounded_above<100>>, int> composed =
        fixy::mint_refined<fixy::all_of<fixy::positive, fixy::bounded_above<100>>>(x);
    static_cast<void>(composed);

    // The wrapper must not demand a copyable payload, which a combinator
    // could reintroduce by accident.  Minting through the trusted door
    // keeps this about type composition rather than about the predicate
    // being callable on a move-only value.
    struct MoveOnly {
        int v_ = 0;
        constexpr MoveOnly() noexcept = default;
        constexpr explicit MoveOnly(int v) noexcept : v_{v} {}
        MoveOnly(const MoveOnly&) = delete;
        MoveOnly(MoveOnly&&) noexcept = default;
        MoveOnly& operator=(const MoveOnly&) = delete;
        MoveOnly& operator=(MoveOnly&&) noexcept = default;
    };
    static_assert(!std::is_copy_constructible_v<MoveOnly>);
    static_assert(std::is_move_constructible_v<MoveOnly>);

    using RmoT = Refined<fixy::positive, MoveOnly>;
    MoveOnly mo{vol};
    RmoT rmo = fixy::mint_refined_trusted<fixy::positive>(std::move(mo));
    static_assert(sizeof(RmoT) == sizeof(MoveOnly), "Refined<P, MoveOnly> must EBO-collapse to sizeof(MoveOnly) "
                                                    "regardless of T's copyability");
    static_cast<void>(rmo);

    alignas(64) int buf[16] = {};
    fixy::AlignedTo<64, int*> ap = fixy::mint_refined<fixy::aligned<64>>(static_cast<int*>(buf));
    static_cast<void>(ap);

    std::array<int, 8> arr8_runtime{};
    fixy::Sized<8, std::array<int, 8>> sized = fixy::mint_refined<fixy::exact_size<8>>(arr8_runtime);
    static_cast<void>(sized);

    fixy::Bounded<0, 100, int> bd = fixy::mint_refined<fixy::in_range<0, 100>>(x);
    static_cast<void>(bd);

    fixy::Capped<255, std::uint32_t> cap =
        fixy::mint_refined<fixy::bounded_above<255>>(static_cast<std::uint32_t>(vol));
    static_cast<void>(cap);

    fixy::Floored<1, int> fl = fixy::mint_refined<fixy::bounded_below<1>>(x);
    static_cast<void>(fl);

    std::size_t volatile big = 1024;
    fixy::DivisibleByN<4, std::size_t> dN = fixy::mint_refined<fixy::divisible_by<4>>(big);
    static_cast<void>(dN);

    // The composed predicate accepts a pointer argument because both of
    // its conjuncts do.
    Refined<fixy::all_of<fixy::non_null, fixy::aligned<64>>, void*> aligned_nonnull_ptr =
        fixy::mint_refined<fixy::all_of<fixy::non_null, fixy::aligned<64>>>(static_cast<void*>(buf));
    static_cast<void>(aligned_nonnull_ptr);
}

}  // namespace

int main() {
    instantiate_every_combinator_at_runtime();

    if (int rc = check_value_paths(); rc != 0) return rc;
    if (int rc = check_refined_doors_and_aliases(); rc != 0) return rc;
    if (int rc = check_sealed_doors(); rc != 0) return rc;
    if (int rc = check_contracts_abort(); rc != 0) return rc;
    return 0;
}
