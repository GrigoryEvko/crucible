// An adversarial campaign against the implication closure of
// fixy/Refined.h.  Each attack uses only what a caller can write: new
// predicates, new namespaces, derived types, aliases, bounds of mixed
// signedness, and declarations added after the header.  The file
// asserts in place each attack that fails, and it pins each attack that
// succeeds on the known-limitation ledger at the end.  The ledger only
// shrinks: a pin fails when its attack stops working, and the entry and
// its pin go together.

#include <fixy/Refined.h>
#include <fixy/session/Subtype.h>

#include <array>
#include <cstdio>
#include <iterator>
#include <string_view>
#include <utility>

namespace {

namespace rel = ::fixy::refined;
namespace ffc = ::foundation::fail_closed;

// A weakening constrained on the relation, the shape a caller writes.
template <auto P, auto Q, class T>
concept Weakens = fixy::implies_v<P, Q> && requires(fixy::Refined<P, T> refined) {
    fixy::mint_refined_trusted<Q>(std::move(refined).into());
};

// ── Attack 1: a rule family outside the namespace ────────────────────
//
// A class outside admitted_implications that derives rule_family and
// admits non_negative ⇒ positive decides nothing.

namespace elsewhere {
struct non_negative_is_positive : rel::rule_family<non_negative_is_positive> {
    static consteval bool holds_(rel::predicate_t<fixy::non_negative>*, rel::predicate_t<fixy::positive>*) noexcept {
        return true;
    }
    static consteval auto next_(rel::predicate_t<fixy::non_negative>*) noexcept
        -> rel::predicate_t<fixy::positive>* {
        return nullptr;
    }
};
}  // namespace elsewhere

static_assert(elsewhere::non_negative_is_positive::admits<rel::predicate_t<fixy::non_negative>,
                                                          rel::predicate_t<fixy::positive>>(),
              "the family itself decides the pair");
static_assert(!fixy::implies_v<fixy::non_negative, fixy::positive>, "a family outside the namespace is inert");

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

static_assert(!fixy::implies_v<accepts_everything, fixy::bounded_above<20>>, "a derived premise borrows nothing");
static_assert(!fixy::implies_v<fixy::in_range<5, 9>, accepts_nothing>, "a derived conclusion borrows nothing");
static_assert(!fixy::implies_v<fixy::bounded_above<9>, accepts_nothing>, "a derived conclusion borrows nothing");
static_assert(!fixy::implies_v<loose_conjunction, fixy::non_zero>, "a derived conjunction borrows nothing");
static_assert(!fixy::implies_v<narrow_range, fixy::positive>, "a derived range reaches no chain");
static_assert(!fixy::implies_v<fixy::all_of<accepts_everything>, fixy::bounded_above<20>>,
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

static_assert(!fixy::implies_v<impostor::bounded_above<9>, fixy::bounded_above<20>>);
static_assert(!fixy::implies_v<fixy::in_range<5, 9>, impostor::bounded_above<20>>);
static_assert(!fixy::implies_v<impostor::bounded_above<9>, impostor::bounded_above<20>>,
              "the impostor families were never admitted");

// ── Attack 4: aliases ────────────────────────────────────────────────
//
// A second name for one predicate value is the same predicate, with the
// same answers.  A second lambda with the same body is a different
// predicate, with no answers at all.

inline constexpr auto positive_again = fixy::positive;
inline constexpr auto positive_rewritten = [](auto x) constexpr noexcept { return x > decltype(x){0}; };
using ceiling_alias = rel::predicate_t<fixy::bounded_above<9>>;

static_assert(fixy::implies_v<positive_again, fixy::non_zero> && fixy::implies_v<positive_again, fixy::non_null>);
static_assert(!fixy::implies_v<positive_rewritten, fixy::non_zero> && !fixy::implies_v<positive_rewritten, fixy::positive>);
static_assert(rel::implies_types<ceiling_alias, rel::predicate_t<fixy::bounded_above<20>>>());

// ── Attack 5: bounds of mixed signedness ─────────────────────────────

static_assert(!fixy::implies_v<fixy::bounded_above<9u>, fixy::bounded_above<-1>>);
static_assert(!fixy::implies_v<fixy::in_range<5u, 9u>, fixy::bounded_above<-1>>, "no chain turns -1 into a ceiling");
static_assert(!fixy::implies_v<fixy::bounded_below<-1>, fixy::bounded_below<9u>>);
static_assert(!fixy::implies_v<fixy::in_range<-1, 9>, fixy::non_negative>, "a negative floor reaches nothing");
static_assert(!fixy::implies_v<fixy::in_range<-1, 9>, fixy::in_range<0u, 9u>>);
static_assert(!fixy::implies_v<fixy::divisible_by<-8>, fixy::divisible_by<4u>>);
static_assert(fixy::implies_v<fixy::in_range<0u, 9u>, fixy::in_range<-1, 9>>, "the sound direction still holds");

// ── Attack 6: a chain past the narrowing edge ────────────────────────
//
// non_zero ⇒ non_null narrows the domain, and a chain ends there.
// non_null reaches non_zero and nothing past it.

static_assert(fixy::implies_v<fixy::non_null, fixy::non_zero>);
static_assert(!fixy::implies_v<fixy::non_null, fixy::non_negative> && !fixy::implies_v<fixy::non_null, fixy::positive>);
static_assert(!fixy::implies_v<fixy::non_zero, fixy::non_zero>, "the cycle through non_null adds no reflexive answer");
static_assert(fixy::implies_v<fixy::in_range<1, 9>, fixy::non_null>,
              "a range ends on the narrowing edge after three steps that keep the domain");

// ── Attack 7: declarations after the header ──────────────────────────
//
// The relation reads the namespace as it stands at the foot of
// fixy/Refined.h.  A family or an edge added later is inert for it.

}  // namespace

namespace fixy::refined::admitted_implications {

struct late_family : rule_family<late_family> {
    static consteval bool holds_(predicate_t<::fixy::non_negative>*, predicate_t<::fixy::non_zero>*) noexcept {
        return true;
    }
};

// Unsound: zero is non-negative and not positive.
inline constexpr ::foundation::fail_closed::edge<predicate_t<::fixy::non_negative>, predicate_t<::fixy::positive>>
    late_edge{};

}  // namespace fixy::refined::admitted_implications

namespace {

static_assert(!fixy::implies_v<fixy::non_negative, fixy::non_zero>, "a family added after the header is inert");
static_assert(!fixy::implies_v<fixy::non_negative, fixy::positive>, "an edge added after the header is inert");
static_assert(ffc::edge_count<^^rel::admitted_implications>() == 5,
              "a walk here sees the late edge, which is the property the relation avoids");

// ── The known-limitation ledger ──────────────────────────────────────

struct limitation {
    std::string_view attack;
    std::string_view breaks;
};

inline constexpr limitation known_limitations[] = {
    {"fixy/session/Subtype.h walks admitted_implications at query time, and the late edge above then makes a "
     "non-negative payload a subtype of a positive one",
     "the payload order must be the implication relation of fixy/Refined.h, which is closed at the foot of "
     "that header"},
};
static_assert(std::size(known_limitations) == 1, "the ledger only shrinks: lower this count when an entry goes");

// Pin 1: the subtype walk reads the late edge.
using NonNegative = fixy::Refined<fixy::non_negative, int>;
using Positive = fixy::Refined<fixy::positive, int>;
static_assert(fixy::session::is_subtype_sync_v<fixy::session::Send<NonNegative, fixy::session::End>,
                                               fixy::session::Send<Positive, fixy::session::End>>,
              "the pinned attack of ledger entry 1 did not succeed, and the entry can be stale");

// A value crosses a chained weakening unchanged, and a value that the
// late edge admits is exactly the one the relation must keep out.
[[nodiscard]] int check_runtime() noexcept {
    int volatile vol = 6;  // defeats constant folding
    fixy::Refined<fixy::in_range<5, 9>, int> narrow = fixy::mint_refined<fixy::in_range<5, 9>>(int{vol});
    static_assert(Weakens<fixy::in_range<5, 9>, fixy::bounded_above<20>, int>);
    static_assert(!Weakens<fixy::non_negative, fixy::positive, int>);
    fixy::Refined<fixy::bounded_above<20>, int> wide = fixy::mint_refined_trusted<fixy::bounded_above<20>>(
        std::move(narrow).into());
    if (wide.value() != 6) return 1;
    int volatile zero = 0;
    if (fixy::positive(int{zero})) return 2;
    return 0;
}

}  // namespace

int main() {
    const int failure = check_runtime();
    std::printf("test_refined_chain_attack: %zu known limitation(s), runtime check %s\n", std::size(known_limitations),
                failure == 0 ? "passed" : "failed");
    return failure;
}
