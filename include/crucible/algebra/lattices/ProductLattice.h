#pragma once

// ── crucible::algebra::lattices::ProductLattice<Ls...> ──────────────
//
// Componentwise product of lattices per 25_04_2026.md §2.4.  The
// graded-modal foundation for any value carrying two or more
// independent lattice grades simultaneously — e.g. the §2.4 Budgeted
// primitive `Graded<Absolute, ProductLattice<BitsBudget, PeakBytes>,
// ResultTensor>` whose grade tracks transferred bytes AND peak-
// resident memory together at the type level.
//
// ── Algebraic shape ─────────────────────────────────────────────────
//
// Given lattices L1, ..., Ln with elements e_i ∈ L_i:
//
//     element_type      = (L1::element_type, ..., Ln::element_type)
//     bottom            = (L1::bottom(), ..., Ln::bottom())
//                          [requires every L_i BoundedBelowLattice]
//     top               = (L1::top(),    ..., Ln::top())
//                          [requires every L_i BoundedAboveLattice]
//     leq(a, b)         = ⋀_i L_i::leq(a_i, b_i)         (pointwise AND)
//     join(a, b)        = (L1::join(a_1, b_1), ..., Ln::join(a_n, b_n))
//     meet(a, b)        = (L1::meet(a_1, b_1), ..., Ln::meet(a_n, b_n))
//
// All lattice axioms (idempotent / commutative / associative join+meet,
// absorption, partial-order reflexive/antisymmetric/transitive, and —
// when applicable — bottom/top identity) lift unchanged from the
// component lattices: each axiom of the product is the component-wise
// AND of the per-lattice axiom.  See the self-test block for the law
// verification at representative witnesses.
//
// ── Layout discipline (zero-overhead contract) ──────────────────────
//
// `element_type` uses `[[no_unique_address]]` per component so that
// any pair of empty-element lattices (e.g. `ProductLattice<BoolLattice
// <P>, BoolLattice<Q>>` carrying two refinement predicates with no
// runtime payload) collapses to 1 byte (the C++ unique-address rule's
// minimum).  For the typical case (BitsBudget × PeakBytes — both
// `uint64_t` grades) the layout is the natural sum-of-components.
//
// std::tuple is intentionally NOT used here: libstdc++'s tuple
// implementation does not aggressively EBO empty members, so a
// `tuple<EmptyA, EmptyB>` would consume 2 bytes (one per empty type)
// instead of the C++ minimum.  Crucible's zero-overhead axiom forbids
// that drift; the in-house aggregate honors it.
//
// ── Variadic shape ──────────────────────────────────────────────────
//
// The forward-declaration in algebra/lattices/AllLattices.h is
// variadic (`template <typename... Ls> struct ProductLattice`).  The
// definition here ships:
//
//   - The binary specialization (the practical-deployment case for
//     Budgeted<{BitsBudget, PeakBytes}, T> and similar two-grade
//     wrappers).
//   - The degenerate empty-pack specialization (a one-element trivial
//     lattice that satisfies BoundedLattice, useful as a recursion
//     base if a future caller wants higher-arity products via
//     left-associative nesting `ProductLattice<L1, ProductLattice<L2,
//     ProductLattice<L3, ...>>>`).
//
// N-ary direct support (>2 components without nesting) is left as a
// future extension — the binary case covers every primitive's needs
// per the 25_04 doc, and adding the variadic recursion would risk
// binding the order-of-traversal policy (left-bias vs. right-bias on
// associativity) before any concrete caller demands it.
//
//   Axiom coverage:
//     TypeSafe — Ls are captured at the type level; component
//                lattices are validated via the Lattice / BoundedLattice
//                concept gates at template-substitution time.
//     DetSafe  — every operation is `constexpr` (NOT `consteval`) so
//                Graded's runtime `pre (L::leq(...))` precondition can
//                fire under the `enforce` contract semantic.
//     MemSafe  — element_type is trivially-destructible iff every
//                component element_type is; no per-instance heap.
//   Runtime cost: pointwise component cost + zero overhead for the
//                product wrapper itself (verified by static_asserts in
//                the self-test).
//
// See ALGEBRA-2 (Lattice.h) for the verifier helpers; ALGEBRA-5 / -8
// for the BoolLattice / FractionalLattice patterns the binary case
// follows; 25_04_2026.md §2.4 for the Budgeted use case that motivated
// shipping this lattice.

// ── Includes ────────────────────────────────────────────────────────
//
// We DO NOT include AllLattices.h here — that header forward-declares
// the variadic primary template `template <typename... Ls> struct
// ProductLattice;` and ALSO #includes this file under its "shipped
// lattices" block.  Including AllLattices.h from here would create a
// circular include where ProductLattice.h is parsed before
// AllLattices.h's forward declaration is visible, breaking the
// specializations below.  The bool/conf/etc. lattice headers follow
// the same discipline — they declare their own primary templates
// inline rather than depending on the umbrella's forward decls.
//
// QttSemiring + BoolLattice are pulled in for the self-test witnesses
// that exercise the empty × non-empty and empty × empty layout
// invariants documented further down.

#include <crucible/algebra/Graded.h>
#include <crucible/algebra/Lattice.h>
#include <crucible/algebra/lattices/BoolLattice.h>
#include <crucible/algebra/lattices/QttSemiring.h>

#include <string_view>
#include <type_traits>
#include <utility>

namespace crucible::algebra::lattices {

// ── Primary template (matches AllLattices.h's forward declaration) ──
//
// Variadic Ls so the specializations below can fix the pack length
// at zero or two.  Higher arities are intentionally not provided here
// — see the "Variadic shape" note in the file header.
template <typename... Ls>
struct ProductLattice;

// ── ProductLattice<L1, L2> — binary specialization ──────────────────
//
// The foundation case.  Higher-arity products nest left-associatively
// via `ProductLattice<L1, ProductLattice<L2, ...>>`.
template <typename L1, typename L2>
struct ProductLattice<L1, L2> {
    static_assert(Lattice<L1>,
        "ProductLattice<L1, L2>: L1 must satisfy the Lattice concept.");
    static_assert(Lattice<L2>,
        "ProductLattice<L1, L2>: L2 must satisfy the Lattice concept.");

    // Componentwise element carrier.  `[[no_unique_address]]` on each
    // member preserves the EBO collapse for empty-element lattices
    // (BoolLattice, ConfLattice trivial cases).  Defaulted operator==
    // composes the components' equality — every shipped lattice's
    // element_type publishes operator== per the Semiring concept's
    // requirement, so this defaulting is sound across all current
    // lattice instantiations.
    struct element_type {
        [[no_unique_address]] typename L1::element_type first{};
        [[no_unique_address]] typename L2::element_type second{};

        [[nodiscard]] constexpr bool operator==(const element_type&) const noexcept = default;
    };

    using first_lattice  = L1;
    using second_lattice = L2;

    // ── Bounded structure (concept-gated) ───────────────────────────
    //
    // bottom() is only available when BOTH components are
    // BoundedBelowLattice; symmetric for top().  This is the standard
    // pointwise lifting — a product is bounded-below iff every
    // component is.

    [[nodiscard]] static constexpr element_type bottom() noexcept
        requires BoundedBelowLattice<L1> && BoundedBelowLattice<L2>
    {
        return element_type{L1::bottom(), L2::bottom()};
    }

    [[nodiscard]] static constexpr element_type top() noexcept
        requires BoundedAboveLattice<L1> && BoundedAboveLattice<L2>
    {
        return element_type{L1::top(), L2::top()};
    }

    // ── Order + lattice operations ──────────────────────────────────
    //
    // All three are pointwise.  leq is short-circuiting on the first
    // component's failure (standard && semantics).

    [[nodiscard]] static constexpr bool leq(
        element_type a, element_type b) noexcept
    {
        return L1::leq(a.first, b.first) && L2::leq(a.second, b.second);
    }

    [[nodiscard]] static constexpr element_type join(
        element_type a, element_type b) noexcept
    {
        return element_type{
            L1::join(a.first, b.first),
            L2::join(a.second, b.second)
        };
    }

    [[nodiscard]] static constexpr element_type meet(
        element_type a, element_type b) noexcept
    {
        return element_type{
            L1::meet(a.first, b.first),
            L2::meet(a.second, b.second)
        };
    }

    // ── Diagnostic name ─────────────────────────────────────────────
    //
    // Returns a fixed `Product<L1xL2>` token.  Recursive name
    // composition (e.g. extracting and concatenating L1::name() +
    // L2::name() at consteval) requires `define_static_string` glue
    // that adds compile-time cost without diagnostic value the
    // containing Graded<>'s reflection-based display does not already
    // provide.  Downstream debug formatters can introspect the
    // first_lattice / second_lattice typedefs.
    [[nodiscard]] static consteval std::string_view name() noexcept {
        return "Product<L1xL2>";
    }
};

// ── ProductLattice<> — degenerate empty-pack specialization ─────────
//
// One-element trivial lattice.  Useful as a recursion base for any
// future variadic ProductLattice<L1, L2, ..., Ln> implementation that
// wants to fold over an empty residual pack (the standard convention
// in functional pack-folds).  Satisfies BoundedLattice trivially —
// the single element IS bottom AND top.
template <>
struct ProductLattice<> {
    struct element_type {
        [[nodiscard]] constexpr bool operator==(element_type) const noexcept {
            return true;
        }
    };

    [[nodiscard]] static constexpr element_type bottom() noexcept { return {}; }
    [[nodiscard]] static constexpr element_type top()    noexcept { return {}; }
    [[nodiscard]] static constexpr bool         leq(element_type, element_type) noexcept { return true;  }
    [[nodiscard]] static constexpr element_type join(element_type, element_type) noexcept { return {};   }
    [[nodiscard]] static constexpr element_type meet(element_type, element_type) noexcept { return {};   }

    [[nodiscard]] static consteval std::string_view name() noexcept {
        return "Product<>";
    }
};

// ── Self-test ───────────────────────────────────────────────────────
namespace detail::product_lattice_self_test {

// Witness lattice — total order on a single uint8_t — used as the
// component for both axes of the binary product.  Picked because it
// is BoundedLattice (has bottom = 0 and top = 255), trivially
// constexpr, and the law-verifier helpers from Lattice.h fire over
// any (a, b, c) triple in [0, 255].
struct U8MinMax {
    using element_type = std::uint8_t;
    [[nodiscard]] static constexpr element_type bottom() noexcept { return 0;   }
    [[nodiscard]] static constexpr element_type top()    noexcept { return 255; }
    [[nodiscard]] static constexpr bool         leq(element_type a, element_type b) noexcept {
        return a <= b;
    }
    [[nodiscard]] static constexpr element_type join(element_type a, element_type b) noexcept {
        return a >= b ? a : b;
    }
    [[nodiscard]] static constexpr element_type meet(element_type a, element_type b) noexcept {
        return a <= b ? a : b;
    }
    [[nodiscard]] static consteval std::string_view name() noexcept { return "U8MinMax"; }
};

static_assert(BoundedLattice<U8MinMax>);

using P_u8u8 = ProductLattice<U8MinMax, U8MinMax>;

// Concept conformance — the binary product over two BoundedLattices
// IS a BoundedLattice.
static_assert(Lattice<P_u8u8>);
static_assert(BoundedBelowLattice<P_u8u8>);
static_assert(BoundedAboveLattice<P_u8u8>);
static_assert(BoundedLattice<P_u8u8>);

// Bounds — bottom / top are pointwise lifts.
static_assert(P_u8u8::bottom().first  == 0);
static_assert(P_u8u8::bottom().second == 0);
static_assert(P_u8u8::top().first     == 255);
static_assert(P_u8u8::top().second    == 255);

// Pointwise leq.
static_assert( P_u8u8::leq({1,   2},   {3,   4}));   //  1≤3 ∧ 2≤4
static_assert(!P_u8u8::leq({3,   2},   {1,   4}));   //  3≤1 fails
static_assert(!P_u8u8::leq({1,   4},   {3,   2}));   //  4≤2 fails
static_assert( P_u8u8::leq({0,   0},   {255, 255})); // bottom ≤ top
static_assert(!P_u8u8::leq({255, 255}, {0,   0}));   // top ⊄ bottom

// Pointwise join — supremum on each axis independently.
static_assert(P_u8u8::join({1, 4}, {3, 2}).first  == 3);
static_assert(P_u8u8::join({1, 4}, {3, 2}).second == 4);

// Pointwise meet — infimum on each axis independently.
static_assert(P_u8u8::meet({1, 4}, {3, 2}).first  == 1);
static_assert(P_u8u8::meet({1, 4}, {3, 2}).second == 2);

// Bounded-lattice axioms hold — exhaustive over a representative
// 3-witness span.  verify_bounded_lattice_axioms_at rolls in
// idempotency, commutativity, associativity, absorption, partial
// order, and bottom/top identity in one shot.
static_assert(verify_bounded_lattice_axioms_at<P_u8u8>(
    {0, 0}, {0, 0}, {0, 0}));
static_assert(verify_bounded_lattice_axioms_at<P_u8u8>(
    {0, 0}, {127, 64}, {255, 255}));
static_assert(verify_bounded_lattice_axioms_at<P_u8u8>(
    {1, 4}, {3, 2}, {5, 7}));
static_assert(verify_bounded_lattice_axioms_at<P_u8u8>(
    {255, 0}, {0, 255}, {127, 127}));

// Subsumes / strictly_less helpers — sanity check that the partial-
// order sugar from Lattice.h composes with the product correctly.
static_assert( subsumes<P_u8u8>({1, 2},   {3, 4}));
static_assert(!subsumes<P_u8u8>({3, 4},   {1, 2}));
static_assert( equivalent<P_u8u8>({5, 7}, {5, 7}));
static_assert(!equivalent<P_u8u8>({5, 7}, {7, 5}));
static_assert( strictly_less<P_u8u8>({1, 2}, {3, 4}));
static_assert(!strictly_less<P_u8u8>({3, 4}, {1, 2}));

// Diagnostic name — fixed token; see the "Diagnostic name" comment
// at the binary specialization for why we don't recurse.
static_assert(P_u8u8::name() == "Product<L1xL2>");

// Component-lattice projections — first_lattice / second_lattice
// expose the underlying component types for downstream introspection.
static_assert(std::is_same_v<P_u8u8::first_lattice,  U8MinMax>);
static_assert(std::is_same_v<P_u8u8::second_lattice, U8MinMax>);

// ── Empty-pack degenerate ───────────────────────────────────────────
using P_empty = ProductLattice<>;

static_assert(BoundedLattice<P_empty>);
static_assert(verify_bounded_lattice_axioms_at<P_empty>(
    P_empty::bottom(), P_empty::bottom(), P_empty::bottom()));
static_assert(P_empty::name() == "Product<>");
static_assert(std::is_empty_v<P_empty::element_type>);

// ── Mixing component lattices with different element types ──────────
//
// QttSemiring::At<1> is empty (zero-overhead linear permission grade);
// U8MinMax carries a uint8_t.  Their product's element_type carries
// only the uint8_t component — the empty Qtt slot collapses via EBO.
// This is the EBO-preserving design that motivates shipping
// element_type as a custom struct rather than std::tuple.
using P_qtt_u8 = ProductLattice<QttSemiring::At<QttGrade::One>, U8MinMax>;

static_assert(Lattice<P_qtt_u8>);
static_assert(P_qtt_u8::bottom().second == 0);
static_assert(P_qtt_u8::top().second    == 255);
static_assert( P_qtt_u8::leq({{}, 1}, {{}, 5}));
static_assert(!P_qtt_u8::leq({{}, 5}, {{}, 1}));

// EBO discipline — when L1::element_type is empty, product element_type
// holds only the L2 component.  We assert size ≤ sizeof(L2::element_type)
// + 1 (the +1 admits the C++ unique-address minimum if EBO doesn't
// fully collapse on this compiler/version; the LOAD-BEARING claim is
// the absence of a doubled cost, not bit-exact size).
static_assert(sizeof(P_qtt_u8::element_type) <= sizeof(std::uint8_t) + 1,
    "ProductLattice<Empty, NonEmpty>::element_type must EBO-collapse "
    "the empty component down to ≤ 1 trailing byte; if this fires, "
    "the [[no_unique_address]] discipline drifted.");

// ── Layout invariant on Graded<...,ProductLattice<...>,T> ───────────
//
// Per ALGEBRA-15 #460 sizeof audit: the layout claim depends on the
// component lattices' element-type sizes.
//
//   - Both components empty (e.g. ProductLattice<BoolLattice<P>,
//     BoolLattice<Q>>): element_type is empty, EBO collapses, Graded
//     over T has sizeof(T).  Use CRUCIBLE_GRADED_LAYOUT_INVARIANT.
//
//   - At least one component non-empty (e.g. ProductLattice<U8MinMax,
//     U8MinMax>): element_type carries the non-empty components,
//     Graded over T grows to sizeof(T) + sizeof(element_type) +
//     alignment padding.  This is the natural cost of carrying
//     runtime grade data alongside the value; the macro is NOT
//     applicable.  We assert the bounded shape instead — Graded must
//     not exceed the natural sum-with-alignment.

// Empty-empty product: both component element_types are empty (the
// BoolLattice-pair case).  EBO collapses fully; the layout invariant
// macro applies and Graded over T retains sizeof(T).
struct OneByteValue   { char c{0}; };
struct EightByteValue { unsigned long long v{0}; };

namespace empty_empty_witness {
struct PredA {};
struct PredB {};
}  // namespace empty_empty_witness

using P_empty_empty = ProductLattice<
    BoolLattice<empty_empty_witness::PredA>,
    BoolLattice<empty_empty_witness::PredB>
>;

template <typename T>
using BudgetEmptyEmpty = Graded<ModalityKind::Absolute, P_empty_empty, T>;

// Empty element_type — confirms the EBO collapse works through the
// product wrapper.  Load-bearing for the BoolLattice×BoolLattice
// composition (two refinement predicates carried simultaneously).
static_assert(std::is_empty_v<P_empty_empty::element_type>,
    "ProductLattice<EmptyL1, EmptyL2>::element_type must be empty for "
    "the EBO collapse contract to hold; if this fires the [[no_unique_"
    "address]] discipline drifted.");

CRUCIBLE_GRADED_LAYOUT_INVARIANT(BudgetEmptyEmpty, OneByteValue);
CRUCIBLE_GRADED_LAYOUT_INVARIANT(BudgetEmptyEmpty, EightByteValue);
CRUCIBLE_GRADED_LAYOUT_INVARIANT(BudgetEmptyEmpty, int);
CRUCIBLE_GRADED_LAYOUT_INVARIANT(BudgetEmptyEmpty, double);

// Non-trivial product: U8×U8 has 2-byte element_type.  Graded over
// any T carries sizeof(T) + 2 bytes for the grade + alignment
// padding.  Explicit bounded-shape assertion replaces the layout
// invariant macro.
template <typename T>
using BudgetU8U8 = Graded<ModalityKind::Absolute, P_u8u8, T>;

static_assert(sizeof(BudgetU8U8<int>) <= sizeof(int) + 4,
    "BudgetU8U8<int> exceeded sizeof(int) + 4 — the U8×U8 grade "
    "(2 bytes) plus alignment padding (≤ 2 bytes) should fit in 4 "
    "trailing bytes; if this fires investigate Graded's grade "
    "field placement.");
static_assert(sizeof(BudgetU8U8<double>) <= sizeof(double) + 8,
    "BudgetU8U8<double> exceeded sizeof(double) + 8 — the U8×U8 "
    "grade (2 bytes) plus alignment padding (≤ 6 bytes) should fit "
    "in 8 trailing bytes; if this fires investigate Graded's grade "
    "field placement.");

// ── Runtime smoke test ──────────────────────────────────────────────
//
// Per feedback_algebra_runtime_smoke_test_discipline memory: every
// algebra/lattices/ header must exercise lattice ops AND
// Graded<...,L,T>::weaken / compose with non-constant arguments.  The
// product lattice gets the same treatment — exercising bottom/top/
// leq/join/meet on the U8×U8 product plus the Graded::weaken /
// compose / consume / peek / grade surface on a non-trivial element.
inline void runtime_smoke_test() {
    using L = P_u8u8;
    L::element_type lo{ 1, 2};
    L::element_type hi{ 5, 7};
    [[maybe_unused]] bool             le = L::leq(lo, hi);
    [[maybe_unused]] L::element_type  jn = L::join(lo, hi);
    [[maybe_unused]] L::element_type  mt = L::meet(lo, hi);
    [[maybe_unused]] L::element_type  bt = L::bottom();
    [[maybe_unused]] L::element_type  tp = L::top();

    OneByteValue v{42};
    BudgetU8U8<OneByteValue> initial{v, lo};
    auto widened   = initial.weaken(hi);                 // lo ⊑ hi
    auto composed  = initial.compose(widened);
    auto rv_widen  = std::move(widened).weaken(L::top());
    auto rv_comp   = std::move(initial).compose(composed);

    [[maybe_unused]] auto g1 = composed.grade();
    [[maybe_unused]] auto v1 = composed.peek().c;
    [[maybe_unused]] auto v2 = std::move(rv_comp).consume().c;
    [[maybe_unused]] auto _r = std::move(rv_widen).consume().c;
}

}  // namespace detail::product_lattice_self_test

}  // namespace crucible::algebra::lattices
