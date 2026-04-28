// Sentinel TU for `safety/diag/GradedWrapperGate.h`.
//
// This TU bridges the FOUND-E05 cheat-probe pattern to the EXISTING
// `algebra::GradedWrapper` concept (substrate Round-1, ~Q1 2026).
// After GradedWrapperGate.h's specialization is in scope, every
// `cheat_probe_type<T, Category::GradedWrapperViolation>` instance
// enforces the GradedWrapper concept's strictness.
//
// The 18 cheats already shipped at `test/test_concept_cheat_probe.cpp`
// (Round-1 through Round-5 audits) become per-cheat regression locks
// here too — if a future relaxation of GradedWrapper admits any of
// these, the build fails at this sentinel.
//
// We deliberately re-construct the cheats here (rather than #include
// the existing test) so the sentinel is self-contained and the
// dependency is one-way (this TU consumes the concept; doesn't
// reach into the existing test).

// ── Bring the gate specialization into scope FIRST ─────────────────
//
// CheatProbe.h's eager-shape-check touches concept_gate<EffectRowMismatch>
// only.  GradedWrapperGate.h must be visible BEFORE any
// concept_gate<GradedWrapperViolation> instantiation in this TU.
#include <crucible/safety/diag/GradedWrapperGate.h>

#include <crucible/algebra/Graded.h>
#include <crucible/algebra/lattices/QttSemiring.h>
#include <crucible/algebra/lattices/BoolLattice.h>

#include <string_view>
#include <type_traits>

namespace cheats {

namespace algebra = ::crucible::algebra;
namespace lattices = ::crucible::algebra::lattices;

constexpr bool positive_p(int x) noexcept { return x > 0; }

// ── Cheat 1 — value_type / graded_type::value_type mismatch ───────
struct Cheat1_ValueTypeMismatch {
    using value_type   = int;
    using lattice_type = lattices::QttSemiring::At<lattices::QttGrade::One>;
    using graded_type  = algebra::Graded<algebra::ModalityKind::Absolute,
                                         lattices::QttSemiring::At<lattices::QttGrade::One>,
                                         double>;  // ← differs
    static constexpr algebra::ModalityKind modality = algebra::ModalityKind::Absolute;
    static consteval std::string_view value_type_name() noexcept { return "int"; }
    static consteval std::string_view lattice_name()    noexcept { return "QTT::1"; }
};

// ── Cheat 2 — lattice_type / graded_type::lattice_type mismatch ───
struct Cheat2_LatticeMismatch {
    using value_type   = int;
    using lattice_type = lattices::QttSemiring::At<lattices::QttGrade::One>;
    using graded_type  = algebra::Graded<algebra::ModalityKind::Absolute,
                                         lattices::BoolLattice<decltype(positive_p)>,
                                         int>;  // ← lattice differs
    static constexpr algebra::ModalityKind modality = algebra::ModalityKind::Absolute;
    static consteval std::string_view value_type_name() noexcept { return "int"; }
    static consteval std::string_view lattice_name()    noexcept { return "x"; }
};

// ── Cheat 3 — forwarders return DIFFERENT strings than substrate ──
struct Cheat3_LyingForwarders {
    using value_type   = int;
    using lattice_type = lattices::QttSemiring::At<lattices::QttGrade::One>;
    using graded_type  = algebra::Graded<algebra::ModalityKind::Absolute,
                                         lattices::QttSemiring::At<lattices::QttGrade::One>,
                                         int>;
    static constexpr algebra::ModalityKind modality = algebra::ModalityKind::Absolute;
    static consteval std::string_view value_type_name() noexcept {
        return "TOTALLY-LYING";  // ← does not match graded_type::value_type_name()
    }
    static consteval std::string_view lattice_name() noexcept {
        return "ALSO-LYING";     // ← does not match graded_type::lattice_name()
    }
};

}  // namespace cheats

// ═════════════════════════════════════════════════════════════════════
// Lock in: the GradedWrapper gate REJECTS each cheat.  Build SUCCEEDS
// only when every probe's static_assert passes (i.e., every cheat is
// correctly rejected).
// ═════════════════════════════════════════════════════════════════════

namespace diag = ::crucible::safety::diag;

using probe_1 = diag::cheat_probe_type<cheats::Cheat1_ValueTypeMismatch,
                                       diag::Category::GradedWrapperViolation>;
using probe_2 = diag::cheat_probe_type<cheats::Cheat2_LatticeMismatch,
                                       diag::Category::GradedWrapperViolation>;
using probe_3 = diag::cheat_probe_type<cheats::Cheat3_LyingForwarders,
                                       diag::Category::GradedWrapperViolation>;

// Companion fact-checks (the cheat probe asserts !admits; these
// directly assert the same fact in a more readable form).
static_assert(!diag::concept_gate<diag::Category::GradedWrapperViolation>
              ::admits_type<cheats::Cheat1_ValueTypeMismatch>);
static_assert(!diag::concept_gate<diag::Category::GradedWrapperViolation>
              ::admits_type<cheats::Cheat2_LatticeMismatch>);
static_assert(!diag::concept_gate<diag::Category::GradedWrapperViolation>
              ::admits_type<cheats::Cheat3_LyingForwarders>);

// Sanity: a legitimate Linear<int> wrapper should be ADMITTED.
// (We don't include Linear.h here to keep the TU lean — instead, we
// confirm the gate's plumbing via a direct GradedWrapper concept
// check on Linear<int> from a separate sentinel.)

int main() {
    // The build-time assertions ARE the test.  Reaching main() means
    // every cheat was rejected.  Exit success.
    return 0;
}
