// Sentinel TU for the algebra core: Lattice.h, Modality.h, Graded.h and
// GradedTrait.h.  A header that no translation unit includes is never
// compiled under the project flags, so this file includes each of the
// four.  It also runs each operation on an argument that the optimizer
// cannot see, and it compares each result with the value that the order
// or the substrate defines.  A static_assert reaches only the constant
// path of a constexpr operation.  The run-time path, and each contract
// predicate on it, runs here.

#include <foundation/algebra/Graded.h>
#include <foundation/algebra/GradedTrait.h>
#include <foundation/algebra/Lattice.h>
#include <foundation/algebra/Modality.h>
#include <foundation/reflect/EnumName.h>

#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <meta>
#include <source_location>
#include <string_view>
#include <type_traits>
#include <utility>

namespace {

namespace fa = ::foundation::algebra;

static_assert(fa::SteppingModality<fa::ModalityKind::Stepping>);
static_assert(fa::has_grade_only_v<fa::ModalityKind::Stepping>);

// Row is a refinement of BoundedLattice, and the self-test row is its
// first witness.
static_assert(fa::Row<fa::detail::TrivialRow>);
static_assert(!fa::Row<fa::detail::TrivialBoolLattice>);

// A lattice with no name() is still a lattice.  HasLatticeName refuses it
// although lattice_name gives it the sentinel, so the sentinel is not a
// name.
struct UnnamedLattice {
    using element_type = bool;
    [[nodiscard]] static constexpr element_type bottom() noexcept { return false; }
    [[nodiscard]] static constexpr element_type top() noexcept { return true; }
    [[nodiscard]] static constexpr bool leq(element_type lhs, element_type rhs) noexcept { return !lhs || rhs; }
    [[nodiscard]] static constexpr element_type join(element_type lhs, element_type rhs) noexcept { return lhs || rhs; }
    [[nodiscard]] static constexpr element_type meet(element_type lhs, element_type rhs) noexcept { return lhs && rhs; }
};
struct NamedLattice : UnnamedLattice {
    [[nodiscard]] static consteval std::string_view name() noexcept { return "NamedLattice"; }
};
static_assert(fa::Lattice<UnnamedLattice>);
static_assert(!fa::HasLatticeName<UnnamedLattice>);
static_assert(fa::lattice_name<UnnamedLattice>() == "<unnamed lattice>");
static_assert(fa::HasLatticeName<NamedLattice>);
static_assert(fa::lattice_name<NamedLattice>() == "NamedLattice");

// The primary template over an empty grade still collapses to sizeof(T).
using EmptyGraded = fa::Graded<fa::ModalityKind::Absolute, fa::detail::TrivialEmptyLattice, int>;
static_assert(sizeof(EmptyGraded) == sizeof(int));
static_assert(fa::IsGraded<EmptyGraded>);
static_assert(fa::is_graded_specialization_v<EmptyGraded const&>);

int failed_checks = 0;

void expect(bool holds, std::source_location where = std::source_location::current()) noexcept {
    if (holds) return;
    std::fprintf(stderr, "check failed at %s:%u\n", where.file_name(), where.line());
    ++failed_checks;
}

// The value comes back through a call that no optimization looks into, so
// each operation below runs on a run-time argument, and each contract
// predicate on the way is evaluated and not folded.
template <typename T>
[[nodiscard, gnu::noipa]] T opaque(T value) noexcept {
    return value;
}

void lattice_runs_at_run_time() {
    using namespace fa;
    using namespace fa::detail;
    using L = TrivialBoolLattice;
    const bool truth = opaque(true);
    const bool falsity = opaque(false);

    expect(!L::bottom());
    expect(L::top());
    expect(!L::leq(truth, falsity));
    expect(L::leq(falsity, truth));
    expect(L::leq(truth, truth));
    expect(L::join(truth, falsity));
    expect(!L::join(falsity, falsity));
    expect(!L::meet(truth, falsity));
    expect(L::meet(truth, truth));

    expect(subsumes<L>(falsity, truth));
    expect(!subsumes<L>(truth, falsity));
    expect(equivalent<L>(truth, truth));
    expect(!equivalent<L>(falsity, truth));
    expect(strictly_less<L>(falsity, truth));
    expect(!strictly_less<L>(truth, truth));

    using S = TrivialBoolSemiring;
    expect(!S::zero());
    expect(S::one());
    expect(S::add(truth, falsity));
    expect(!S::add(falsity, falsity));
    expect(!S::mul(truth, falsity));
    expect(S::mul(truth, truth));

    // A row is a set of atoms, one bit for each atom.
    const TrivialAtom write = opaque(TrivialAtom::Write);
    const unsigned char write_row = TrivialRow::single(write);
    expect(write_row == 2);
    expect(TrivialRow::contains(write_row, TrivialAtom::Write));
    expect(!TrivialRow::contains(write_row, TrivialAtom::Read));
    const unsigned char both = TrivialRow::join(write_row, TrivialRow::single(TrivialAtom::Read));
    expect(both == TrivialRow::top());
    expect(TrivialRow::meet(both, write_row) == write_row);
    expect(subsumes<TrivialRow>(write_row, both));
    expect(!subsumes<TrivialRow>(both, write_row));
    expect(subsumes<TrivialRow>(TrivialRow::bottom(), write_row));
}

// modality_name is consteval.  The run-time name read is enum_name, which
// modality_name calls, so a kind that arrives at run time names itself
// and a byte outside the enum reaches the sentinel.
void modality_runs_at_run_time() {
    using namespace fa;
    using ::foundation::reflect::enum_name;
    const ModalityKind comonad = opaque(modality::Comonad_t::kind);
    const ModalityKind stepping = opaque(modality::Stepping_t::kind);
    expect(comonad == ModalityKind::Comonad);
    expect(stepping == ModalityKind::Stepping);
    expect(comonad != stepping);
    expect(enum_name(comonad) == "Comonad");
    expect(enum_name(stepping) == "Stepping");
    expect(enum_name(opaque(static_cast<ModalityKind>(200))) == "<unknown ModalityKind>");

    static_assert(has_unit_v<ModalityKind::RelativeMonad> && !has_unit_v<ModalityKind::Comonad>);
    static_assert(has_counit_v<ModalityKind::Comonad> && !has_counit_v<ModalityKind::Absolute>);
    static_assert(has_grade_only_v<ModalityKind::Absolute> && !has_grade_only_v<ModalityKind::RelativeMonad>);
}

void graded_runs_at_run_time() {
    using namespace fa;
    using namespace fa::detail;
    const auto key = self_test_authority::key;
    const OneByteValue value{opaque('*')};

    // The stored regime: each rebuild keeps the value and moves the grade
    // up by the order.
    GOneByte initial{key(), value, opaque(false)};
    expect(!initial.grade() && initial.peek().c == '*');
    GOneByte widened = initial.weaken(opaque(true));
    expect(widened.grade() && widened.peek().c == '*');
    GOneByte composed = initial.compose(widened);
    expect(composed.grade() && composed.peek().c == '*');
    GOneByte kept_at_bottom = initial.compose(initial);
    expect(!kept_at_bottom.grade());
    GOneByte moved = std::move(widened).weaken(true);
    expect(moved.grade() && moved.peek().c == '*');
    GOneByte moved_composed = std::move(initial).compose(composed);
    expect(moved_composed.grade());
    expect(std::move(moved_composed).consume().c == '*');

    // A swap moves each value with its own grade.
    GOneByte left{key(), OneByteValue{opaque('l')}, false};
    GOneByte right{key(), OneByteValue{opaque('r')}, true};
    swap(left, right);
    expect(left.peek().c == 'r' && left.grade());
    expect(right.peek().c == 'l' && !right.grade());

    // The keyed write in place leaves the grade where it was, and so does
    // the keyless one under a grade that any bytes satisfy.
    GOneByte written{key(), value, false};
    written.peek_mut(key()).c = static_cast<char>(value.c + 1);
    expect(written.peek().c == '+' && !written.grade());
    GSlotOneByte slot = GSlotOneByte::at_bottom();
    expect(!slot.grade() && slot.peek().c == 0);
    slot.peek_mut().c = value.c;
    expect(!slot.grade() && slot.peek().c == '*');

    // The grade is the value.  The keyed form checks its witness with a
    // run-time argument.
    const GBoolElement element_bottom = GBoolElement::at_bottom();
    expect(!element_bottom.grade() && !element_bottom.peek());
    const GBoolElement element_checked{key(), opaque(TrivialBoolLattice::bottom()), TrivialBoolLattice::bottom()};
    expect(!element_checked.grade());
    const GBoolElement element_top{opaque(true)};
    expect(element_top.grade() && element_top.peek());

    // The grade is derived from the value, and it follows a keyed write.
    const GDerivedSeq derived_bottom = GDerivedSeq::at_bottom();
    expect(derived_bottom.grade() == 0);
    const GDerivedSeq derived_checked{key(), MiniContainer{opaque(std::size_t{0})}, MiniDerivedLattice::bottom()};
    expect(derived_checked.grade() == 0);
    GDerivedSeq derived_three{MiniContainer{opaque(std::size_t{3})}};
    expect(derived_three.grade() == 3);
    derived_three.peek_mut(key()) = MiniContainer{opaque(std::size_t{5})};
    expect(derived_three.grade() == 5);

    // A chain whose carrier holds bytes outside the order.  The bounds
    // checks run with a run-time grade.
    const unsigned char chain_grade = opaque(static_cast<unsigned char>(2));
    const GChainOneByte chain_checked{key(), value, chain_grade};
    expect(chain_checked.grade() == 2);
    expect(chain_checked.weaken(static_cast<unsigned char>(3)).grade() == 3);
    expect(chain_checked.compose(GChainOneByte{key(), value, opaque(static_cast<unsigned char>(1))}).grade() == 2);
    const GChainElement chain_element{chain_grade};
    expect(chain_element.compose(chain_element).grade() == 2);
    expect(chain_element.compose(GChainElement{opaque(static_cast<unsigned char>(1))}).grade() == 2);
    const GChainElement chain_raised = chain_element.weaken(opaque(static_cast<unsigned char>(3)));
    expect(chain_raised.grade() == 3 && chain_raised.peek() == 3);

    // Each door of the carrier opens under one modality.  A stored grade
    // that is not empty names the value under every modality but Absolute,
    // so only Absolute opens the keyed write in place.
    static constexpr auto kinds = std::define_static_array(std::meta::enumerators_of(^^ModalityKind));
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wshadow"
    template for (constexpr auto kind_info : kinds) {
        constexpr ModalityKind kind = [:kind_info:];
        using OverBool = Graded<kind, TrivialBoolLattice, EmptyValue>;
        static_assert(CanExtract<OverBool> == (kind == ModalityKind::Comonad));
        static_assert(CanInject<OverBool> == (kind == ModalityKind::RelativeMonad));
        static_assert(CanPeekMutWithKey<OverBool> == (kind == ModalityKind::Absolute));
        static_assert(!CanPeekMut<OverBool>);
    }
#pragma GCC diagnostic pop
}

// The traits have nothing to run.  The function builds a substrate that
// GradedTrait.h names, so its assertions hold for a type that was built.
void graded_trait_runs_at_run_time() {
    using namespace fa;
    using namespace fa::detail;
    const GraderAB raised{opaque(true)};
    expect(raised.grade() && raised.peek());
    const GraderAB lowered = GraderAB::at_bottom();
    expect(!lowered.grade() && !lowered.peek());
    static_assert(is_graded_specialization_v<decltype(raised)> && is_graded_specialization_v<decltype((raised))>);
    static_assert(!is_graded_specialization_v<int>);
    static_assert(graded_modality_v<decltype(raised)> == ModalityKind::Absolute);
}

}  // namespace

int main() {
    lattice_runs_at_run_time();
    modality_runs_at_run_time();
    graded_runs_at_run_time();
    graded_trait_runs_at_run_time();
    return failed_checks == 0 ? 0 : 1;
}
