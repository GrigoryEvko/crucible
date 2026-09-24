// Sentinel TU for the algebra core.  Each header carries its own
// static_asserts and an inline runtime_smoke_test; a header no
// translation unit includes is never compiled under the project flags
// and its smoke test never runs.  This file includes each of the four
// and calls each smoke test once.

#include <foundation/algebra/Graded.h>
#include <foundation/algebra/GradedTrait.h>
#include <foundation/algebra/Lattice.h>
#include <foundation/algebra/Modality.h>

#include <cstddef>
#include <type_traits>
#include <utility>

namespace {

namespace fa = ::foundation::algebra;

// The five-kind set is what the extraction decided; the left side is
// derived by reflection, so this fires if an enumerator is added or
// removed.
static_assert(fa::modality_kind_count == 5);
static_assert(fa::SteppingModality<fa::ModalityKind::Stepping>);
static_assert(fa::has_grade_only_v<fa::ModalityKind::Stepping>);

// Row is a refinement of BoundedLattice, and the self-test row is its
// first witness.
static_assert(fa::Row<fa::detail::lattice_self_test::TrivialRow>);
static_assert(!fa::Row<fa::detail::lattice_self_test::TrivialBoolLattice>);

// The primary template over an empty grade still collapses to sizeof(T).
using EmptyGraded = fa::Graded<fa::ModalityKind::Absolute, fa::detail::graded_self_test::TrivialEmptyLattice, int>;
static_assert(sizeof(EmptyGraded) == sizeof(int));
static_assert(fa::IsGraded<EmptyGraded>);
static_assert(fa::is_graded_specialization_v<EmptyGraded const&>);

// Each body below was an inline runtime_smoke_test in its header,
// compiled into every translation unit that included it.  They are
// moved verbatim, and the function-scope using-directives reproduce the
// name lookup each body had inside its header.  The static_assert walls
// stayed behind, because each reads a shipped type against itself.

// Every operation is driven once through a non-constant argument.  A
// static_assert reaches only the constant path, and the whole point of
// the constexpr-not-consteval rule is the other one.
void lattice_runs_at_run_time() {
    using namespace fa;
    using namespace fa::detail::lattice_self_test;
    bool x = true;  // deliberately not constexpr
    bool y = false;  // deliberately not constexpr
    [[maybe_unused]] bool bot = TrivialBoolLattice::bottom();
    [[maybe_unused]] bool top = TrivialBoolLattice::top();
    [[maybe_unused]] bool le = TrivialBoolLattice::leq(x, y);
    [[maybe_unused]] bool jo = TrivialBoolLattice::join(x, y);
    [[maybe_unused]] bool me = TrivialBoolLattice::meet(x, y);

    [[maybe_unused]] bool sub = subsumes<TrivialBoolLattice>(y, x);
    [[maybe_unused]] bool eq = equivalent<TrivialBoolLattice>(x, x);
    [[maybe_unused]] bool sl = strictly_less<TrivialBoolLattice>(y, x);

    [[maybe_unused]] bool zer = TrivialBoolSemiring::zero();
    [[maybe_unused]] bool one = TrivialBoolSemiring::one();
    [[maybe_unused]] bool add = TrivialBoolSemiring::add(x, y);
    [[maybe_unused]] bool mul = TrivialBoolSemiring::mul(x, y);

    TrivialAtom atom = x ? TrivialAtom::Write : TrivialAtom::Read;  // deliberately not constexpr
    unsigned char row = TrivialRow::single(atom);
    [[maybe_unused]] bool holds = TrivialRow::contains(row, atom);
    [[maybe_unused]] unsigned char both = TrivialRow::join(row, TrivialRow::single(TrivialAtom::Read));
    [[maybe_unused]] bool under_top = subsumes<TrivialRow>(both, TrivialRow::top());
}

// modality_name is consteval and so cannot appear here at all.  What
// this checks is the rest of the header against runtime semantics.
void modality_runs_at_run_time() {
    using namespace fa;
    using namespace fa::detail::modality_self_test;
    [[maybe_unused]] modality::Comonad_t co_tag{};
    [[maybe_unused]] modality::RelativeMonad_t rm_tag{};
    [[maybe_unused]] modality::Absolute_t ab_tag{};
    [[maybe_unused]] modality::Relative_t rl_tag{};
    [[maybe_unused]] modality::Stepping_t st_tag{};

    // Reading ::kind into a non-constexpr local pins that it stays
    // usable in a runtime context as well as a constant one.
    ModalityKind k = modality::Comonad_t::kind;
    [[maybe_unused]] bool ok1 = (k == ModalityKind::Comonad);
    k = modality::Stepping_t::kind;
    [[maybe_unused]] bool ok2 = (k == ModalityKind::Stepping);

    // The predicates take a non-type template argument, so they cannot
    // be driven with a runtime value.  Only their results reach here.
    [[maybe_unused]] bool unit_co = has_unit_v<ModalityKind::RelativeMonad>;
    [[maybe_unused]] bool grade_ab = has_grade_only_v<ModalityKind::Absolute>;
    [[maybe_unused]] bool grade_st = has_grade_only_v<ModalityKind::Stepping>;
}

// The contract predicates in the operations below run with non-constant
// arguments, which a static_assert-only test never reaches.
void graded_runs_at_run_time() {
    using namespace fa;
    using namespace fa::detail::graded_self_test;
    OneByteValue value{42};
    GOneByte initial{value, false};
    GOneByte widened = initial.weaken(true);
    GOneByte composed = initial.compose(widened);
    GOneByte moved = std::move(widened).weaken(true);
    GOneByte mcomposed = std::move(initial).compose(composed);

    [[maybe_unused]] bool g1 = composed.grade();
    [[maybe_unused]] bool g2 = moved.grade();
    [[maybe_unused]] bool g3 = mcomposed.grade();
    [[maybe_unused]] auto v1 = composed.peek().c;
    [[maybe_unused]] auto v2 = std::move(mcomposed).consume().c;

    GOneByte prim_noarg = GOneByte::at_bottom();
    GOneByte prim_value = GOneByte::at_bottom(OneByteValue{static_cast<char>(value.c + 1)});
    [[maybe_unused]] bool gb1 = prim_noarg.grade();
    [[maybe_unused]] auto vb1 = prim_value.peek().c;

    // The two specializations publish at_bottom() alone.  The checked
    // form runs here with a non-constant argument, where the witness
    // predicate is evaluated under runtime semantics rather than folded
    // away.
    GBoolElement same_noarg = GBoolElement::at_bottom();
    GBoolElement same_checked{TrivialBoolLattice::bottom(), TrivialBoolLattice::bottom()};
    [[maybe_unused]] bool gs1 = same_noarg.grade();
    [[maybe_unused]] bool gs2 = same_checked.grade();

    GDerivedSeq der_noarg = GDerivedSeq::at_bottom();
    GDerivedSeq der_checked{MiniContainer{static_cast<std::size_t>(0)}, MiniDerivedLattice::bottom()};
    [[maybe_unused]] std::size_t gd1 = der_noarg.grade();
    [[maybe_unused]] std::size_t gd2 = der_checked.grade();

    // The stored-grade bounds check with a non-constant grade, so the
    // predicate runs under runtime semantics rather than being folded.
    unsigned char chain_grade = static_cast<unsigned char>(value.c % 4);  // 42 % 4 == 2, inside [0, 3]
    GChainOneByte chain_checked{value, chain_grade};
    [[maybe_unused]] unsigned char gc1 = chain_checked.grade();
    [[maybe_unused]] unsigned char gc2 = chain_checked.weaken(static_cast<unsigned char>(3)).grade();
}

// The traits here have nothing to run at runtime.  What the function
// buys is a real instantiation of the substrate, so the assertions in
// the header are known to hold for a type that was actually built and
// not only named.
void graded_trait_runs_at_run_time() {
    using namespace fa;
    using namespace fa::detail::is_graded_specialization_self_test;
    GraderAB g{true, true};
    [[maybe_unused]] bool grade_view = g.grade();
    [[maybe_unused]] bool value_view = g.peek();

    [[maybe_unused]] bool t1 = is_graded_specialization_v<decltype(g)>;
    [[maybe_unused]] bool t2 = is_graded_specialization_v<decltype((g))>;
    [[maybe_unused]] bool t3 = is_graded_specialization_v<int>;
}

}  // namespace

int main() {
    lattice_runs_at_run_time();
    modality_runs_at_run_time();
    graded_runs_at_run_time();
    graded_trait_runs_at_run_time();
    return 0;
}
