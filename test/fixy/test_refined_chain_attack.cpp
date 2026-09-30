// An adversarial campaign against the implication closure of
// fixy/Refined.h.  Each attack uses only what a caller can write: new
// predicates, new namespaces, derived types, aliases, bounds of mixed
// signedness, and declarations added after the header.  The file
// asserts in place that each attack fails.

#include <fixy/Refined.h>
#include <fixy/session/Subtype.h>

#include <cstdio>
#include <meta>
#include <utility>

namespace {

namespace rel = ::fixy::refined;
namespace ffc = ::foundation::fail_closed;

// A weakening constrained on the relation, the shape a caller writes.
template <auto P, auto Q, class T>
concept Weakens = fixy::PredicateImplies<P, Q>
               && requires(fixy::Refined<P, T> refined) { fixy::mint_refined_trusted<Q>(std::move(refined).into()); };

// ── Attack 1: a rule outside the namespace ───────────────────────────
//
// A function of the rule type outside admitted_implications that admits
// non_negative ⇒ positive decides nothing.

namespace elsewhere {
consteval rel::rule_verdict non_negative_is_positive(std::meta::info premise, std::meta::info conclusion) {
    const std::meta::info positive_type = std::meta::dealias(^^rel::predicate_t<fixy::positive>);
    return {.admits = std::meta::dealias(premise) == std::meta::dealias(^^rel::predicate_t<fixy::non_negative>)
                   && std::meta::dealias(conclusion) == positive_type,
            .successor = positive_type};
}
}  // namespace elsewhere

static_assert(rel::is_rule(^^elsewhere::non_negative_is_positive), "the function has the type of a rule");
static_assert(elsewhere::non_negative_is_positive(^^rel::predicate_t<fixy::non_negative>,
                                                  ^^rel::predicate_t<fixy::positive>)
                  .admits,
              "the rule itself decides the pair");
static_assert(!fixy::PredicateImplies<fixy::non_negative, fixy::positive>, "a rule outside the namespace is inert");

// ── Attack 2: a derived predicate borrows a base's parameters ────────
//
// Template argument deduction converts a pointer to a derived class into
// a pointer to its base.  A predicate that derives from a parameterised
// predicate, or from a conjunction, must not borrow its base's place in
// the relation.

struct AcceptsEverything : fixy::BoundedAbove<9> {
    constexpr bool operator()(auto) const noexcept { return true; }
};
struct AcceptsNothing : fixy::BoundedAbove<20> {
    constexpr bool operator()(auto) const noexcept { return false; }
};
struct LooseConjunction : fixy::AllOf<fixy::positive> {
    constexpr bool operator()(auto) const noexcept { return true; }
};
struct NarrowRange : fixy::InRange<1, 9> {
    constexpr bool operator()(auto) const noexcept { return true; }
};

inline constexpr AcceptsEverything accepts_everything{};
inline constexpr AcceptsNothing accepts_nothing{};
inline constexpr LooseConjunction loose_conjunction{};
inline constexpr NarrowRange narrow_range{};

static_assert(!fixy::PredicateImplies<accepts_everything, fixy::bounded_above<20>>,
              "a derived premise borrows nothing");
static_assert(!fixy::PredicateImplies<fixy::in_range<5, 9>, accepts_nothing>, "a derived conclusion borrows nothing");
static_assert(!fixy::PredicateImplies<fixy::bounded_above<9>, accepts_nothing>, "a derived conclusion borrows nothing");
static_assert(!fixy::PredicateImplies<loose_conjunction, fixy::non_zero>, "a derived conjunction borrows nothing");
static_assert(!fixy::PredicateImplies<narrow_range, fixy::positive>, "a derived range reaches no chain");
static_assert(!fixy::PredicateImplies<fixy::all_of<accepts_everything>, fixy::bounded_above<20>>,
              "a derived conjunct borrows nothing through a conjunction");

// ── Attack 3: the same name in another namespace ─────────────────────

namespace impostor {
template <auto Max>
struct BoundedAbove {
    constexpr bool operator()(auto) const noexcept { return true; }
};
template <auto Max>
inline constexpr BoundedAbove<Max> bounded_above{};
}  // namespace impostor

static_assert(!fixy::PredicateImplies<impostor::bounded_above<9>, fixy::bounded_above<20>>);
static_assert(!fixy::PredicateImplies<fixy::in_range<5, 9>, impostor::bounded_above<20>>);
static_assert(!fixy::PredicateImplies<impostor::bounded_above<9>, impostor::bounded_above<20>>,
              "the impostor families were never admitted");

// ── Attack 4: aliases ────────────────────────────────────────────────
//
// A second name for one predicate value is the same predicate, with the
// same answers.  A second lambda with the same body is a different
// predicate, with no answers at all.

inline constexpr auto positive_again = fixy::positive;
inline constexpr auto positive_rewritten = [](auto x) constexpr noexcept { return x > decltype(x){0}; };
using ceiling_alias = rel::predicate_t<fixy::bounded_above<9>>;

static_assert(fixy::PredicateImplies<positive_again, fixy::non_zero>
              && fixy::PredicateImplies<positive_again, fixy::non_null>);
static_assert(!fixy::PredicateImplies<positive_rewritten, fixy::non_zero>
              && !fixy::PredicateImplies<positive_rewritten, fixy::positive>);
static_assert(rel::predicate_implies(^^ceiling_alias, ^^rel::predicate_t<fixy::bounded_above<20>>));

// ── Attack 5: bounds of mixed signedness ─────────────────────────────

static_assert(!fixy::PredicateImplies<fixy::bounded_above<9u>, fixy::bounded_above<-1>>);
static_assert(!fixy::PredicateImplies<fixy::in_range<5u, 9u>, fixy::bounded_above<-1>>,
              "no chain turns -1 into a ceiling");
static_assert(!fixy::PredicateImplies<fixy::bounded_below<-1>, fixy::bounded_below<9u>>);
static_assert(!fixy::PredicateImplies<fixy::in_range<-1, 9>, fixy::non_negative>, "a negative floor reaches nothing");
static_assert(!fixy::PredicateImplies<fixy::in_range<-1, 9>, fixy::in_range<0u, 9u>>);
static_assert(!fixy::PredicateImplies<fixy::divisible_by<-8>, fixy::divisible_by<4u>>);
static_assert(fixy::PredicateImplies<fixy::in_range<0u, 9u>, fixy::in_range<-1, 9>>, "the sound direction still holds");

// ── Attack 6: a chain past the narrowing edge ────────────────────────
//
// non_zero ⇒ non_null narrows the domain, and a chain ends there.
// non_null reaches non_zero and nothing past it.

static_assert(fixy::PredicateImplies<fixy::non_null, fixy::non_zero>);
static_assert(!fixy::PredicateImplies<fixy::non_null, fixy::non_negative>
              && !fixy::PredicateImplies<fixy::non_null, fixy::positive>);
static_assert(!fixy::PredicateImplies<fixy::non_zero, fixy::non_zero>,
              "the cycle through non_null adds no reflexive answer");
static_assert(fixy::PredicateImplies<fixy::in_range<1, 9>, fixy::non_null>,
              "a range ends on the narrowing edge after three steps that keep the domain");

// ── Attack 7: declarations after the header ──────────────────────────
//
// The relation holds one seal, which counts its members at the foot of
// fixy/Refined.h.  Each implication query reads the seal first.  A family
// or an edge that a file adds later stops the build at the next query,
// and the fixtures neg_refined_edge_after_the_header and
// neg_refined_family_after_the_header show the refusal.  Here the seal
// holds, and the sealed count is the count the header states.

static_assert(ffc::Sealed<^^rel::admitted_implications>);
static_assert(ffc::read_seal(^^rel::admitted_implications).sealed == 20);
static_assert(ffc::edge_count<^^rel::admitted_implications>() == 4,
              "the header states four plain edges beside its narrowing edges and rule families");

// ── Attack 8: an explicit specialization of a reader ─────────────────
//
// An explicit specialization changes the answer of a template in any
// file.  The closure, each rule and fail_closed::admits are functions at
// namespace scope that are not templates, and the two gates are concepts.
// The fixtures neg_refined_implies_specialized,
// neg_refined_rule_specialized and neg_fail_closed_admits_specialized
// show each specialization refused.

static_assert(std::meta::is_function(^^rel::predicate_implies) && std::meta::is_function(^^ffc::admits));
static_assert(std::meta::is_concept(^^fixy::PredicateImplies) && std::meta::is_concept(^^ffc::Admitted));
static_assert(rel::is_rule(^^rel::admitted_implications::bounded_above_weakens));

// The payload order of fixy/session/Subtype.h is this relation, so the
// late edge does not make a non-negative payload a subtype of a positive
// one.  A chain that the header admits still weakens a payload.
using NonNegative = fixy::Refined<fixy::non_negative, int>;
using Positive = fixy::Refined<fixy::positive, int>;
static_assert(!fixy::session::is_subtype_sync_v<fixy::session::Send<NonNegative, fixy::session::End>,
                                                fixy::session::Send<Positive, fixy::session::End>>);
static_assert(fixy::session::is_subtype_sync_v<
              fixy::session::Send<fixy::Refined<fixy::in_range<5, 9>, int>, fixy::session::End>,
              fixy::session::Send<fixy::Refined<fixy::bounded_above<20>, int>, fixy::session::End>>);

// A value crosses a chained weakening unchanged, and a value that the
// late edge admits is exactly the one the relation must keep out.
[[nodiscard]] int check_runtime() noexcept {
    int volatile vol = 6;  // defeats constant folding
    fixy::Refined<fixy::in_range<5, 9>, int> narrow = fixy::mint_refined<fixy::in_range<5, 9>>(int{vol});
    static_assert(Weakens<fixy::in_range<5, 9>, fixy::bounded_above<20>, int>);
    static_assert(!Weakens<fixy::non_negative, fixy::positive, int>);
    fixy::Refined<fixy::bounded_above<20>, int> wide =
        fixy::mint_refined_trusted<fixy::bounded_above<20>>(std::move(narrow).into());
    if (wide.value() != 6) return 1;
    int volatile zero = 0;
    if (fixy::positive(int{zero})) return 2;
    return 0;
}

}  // namespace

int main() {
    const int failure = check_runtime();
    std::printf("test_refined_chain_attack: runtime check %s\n", failure == 0 ? "passed" : "failed");
    return failure;
}
