// ═══════════════════════════════════════════════════════════════════
// test_algebra_compile — sentinel TU for the algebra/* header tree
//
// Why this exists:
//   The algebra/Modality.h, algebra/Lattice.h, algebra/Graded.h, and
//   algebra/lattices/* headers carry their verification entirely as
//   header-only `static_assert` blocks plus inline runtime_smoke_test
//   functions.  Until MIGRATE-2 (Refined → Graded<>) shipped, NO
//   production .cpp included any of them, which meant the embedded
//   self-tests were never compiled under the project's full
//   -Werror=shadow / -Werror=switch-default / -Wanalyzer-* flag set.
//   Three latent bug families (template-for shadow, missing default
//   arms, display_string_of TU-context-dependent name comparison)
//   sat undetected for the entire ALGEBRA-1..11 shipping window.
//
//   See feedback_header_only_static_assert_blind_spot memory rule.
//
//   This sentinel TU forces every algebra/* header through the test
//   target's full warning matrix.  Adding a new lattice header without
//   updating this file is a CI miss — the sentinel must be the
//   authoritative TU-coverage list.
//
// What it tests:
//   1. Every algebra/* header compiles under the test target's
//      warning matrix (the embedded static_asserts fire on include).
//   2. Every per-header runtime_smoke_test() executes without
//      tripping a contract violation under enforce semantic.
//
// Coverage discipline:
//   When a new ALGEBRA-* header ships under algebra/lattices/, add
//   its include below AND add its runtime_smoke_test() invocation
//   inside the corresponding run_test() block.  Don't rely on the
//   AllLattices.h umbrella alone — a per-header invocation is
//   greppable and makes the discipline explicit.
// ═══════════════════════════════════════════════════════════════════

#include <crucible/algebra/Algebra.h>
#include <crucible/algebra/Graded.h>
#include <crucible/algebra/Lattice.h>
#include <crucible/algebra/Modality.h>
#include <crucible/algebra/lattices/AllLattices.h>
#include <crucible/algebra/lattices/BoolLattice.h>
#include <crucible/algebra/lattices/ConfLattice.h>
#include <crucible/algebra/lattices/ConsistencyLattice.h>
#include <crucible/algebra/lattices/FractionalLattice.h>
#include <crucible/algebra/lattices/HappensBefore.h>
#include <crucible/algebra/lattices/HotPathLattice.h>
#include <crucible/algebra/lattices/LifetimeLattice.h>
#include <crucible/algebra/lattices/MemOrderLattice.h>
#include <crucible/algebra/lattices/MonotoneLattice.h>
#include <crucible/algebra/lattices/ProductLattice.h>
#include <crucible/algebra/lattices/QttSemiring.h>
#include <crucible/algebra/lattices/SeqPrefixLattice.h>
#include <crucible/algebra/lattices/StalenessSemiring.h>
#include <crucible/algebra/lattices/ToleranceLattice.h>
#include <crucible/algebra/lattices/TrustLattice.h>
#include <crucible/algebra/lattices/WaitLattice.h>

#include <cstdio>
#include <cstdlib>

// ── Test harness — pattern matches test_owned_region etc. ───────────

namespace {

struct TestFailure {};

int total_passed = 0;
int total_failed = 0;

template <typename F>
void run_test(const char* name, F&& body) {
    std::fprintf(stderr, "  %s: ", name);
    try {
        body();
        ++total_passed;
        std::fprintf(stderr, "PASSED\n");
    } catch (TestFailure&) {
        ++total_failed;
        std::fprintf(stderr, "FAILED\n");
    }
}

// ── Per-header runtime smoke probes ─────────────────────────────────
//
// Each calls into the corresponding header's `runtime_smoke_test()`,
// forcing the consteval-vs-constexpr-vs-runtime path through the
// header's lattice / Graded operations.  The functions are inline
// `void`-returning; the optimizer can elide them under -O3, but
// the front-end still type-checks every call site, which is the
// load-bearing property here.

void test_modality_compile() {
    // The static_asserts in detail::modality_self_test fire at TU
    // include time (above).  Reaching this body proves the header
    // compiled clean under the test target's warning matrix.
}

void test_lattice_concepts_compile() {
    // Same shape — Lattice.h's TrivialBoolLattice + TrivialBoolSemiring
    // self-tests fire at include time; nothing to call at runtime.
}

void test_graded_runtime_smoke() {
    ::crucible::algebra::detail::graded_self_test::runtime_smoke_test();
}

void test_qtt_semiring_runtime_smoke() {
    ::crucible::algebra::lattices::detail::qtt_self_test::runtime_smoke_test();
}

void test_bool_lattice_runtime_smoke() {
    ::crucible::algebra::lattices::detail::bool_lattice_self_test::runtime_smoke_test();
}

void test_conf_lattice_runtime_smoke() {
    ::crucible::algebra::lattices::detail::conf_lattice_self_test::runtime_smoke_test();
}

void test_trust_lattice_runtime_smoke() {
    ::crucible::algebra::lattices::detail::trust_lattice_self_test::runtime_smoke_test();
}

void test_fractional_lattice_runtime_smoke() {
    ::crucible::algebra::lattices::detail::fractional_lattice_self_test::runtime_smoke_test();
}

void test_monotone_lattice_runtime_smoke() {
    ::crucible::algebra::lattices::detail::monotone_lattice_self_test::runtime_smoke_test();
}

void test_seq_prefix_lattice_runtime_smoke() {
    ::crucible::algebra::lattices::detail::seq_prefix_lattice_self_test::runtime_smoke_test();
}

void test_staleness_semiring_runtime_smoke() {
    ::crucible::algebra::lattices::detail::staleness_semiring_self_test::runtime_smoke_test();
}

void test_product_lattice_runtime_smoke() {
    ::crucible::algebra::lattices::detail::product_lattice_self_test::runtime_smoke_test();
}

void test_happens_before_runtime_smoke() {
    ::crucible::algebra::lattices::detail::happens_before_self_test::runtime_smoke_test();
}

void test_lifetime_lattice_runtime_smoke() {
    ::crucible::algebra::lattices::detail::lifetime_lattice_self_test::runtime_smoke_test();
}

void test_consistency_lattice_runtime_smoke() {
    ::crucible::algebra::lattices::detail::consistency_lattice_self_test::runtime_smoke_test();
}

void test_tolerance_lattice_runtime_smoke() {
    ::crucible::algebra::lattices::detail::tolerance_lattice_self_test::runtime_smoke_test();
}

void test_det_safe_lattice_runtime_smoke() {
    ::crucible::algebra::lattices::detail::det_safe_lattice_self_test::runtime_smoke_test();
}

void test_hot_path_lattice_runtime_smoke() {
    ::crucible::algebra::lattices::detail::hot_path_lattice_self_test::runtime_smoke_test();
}

void test_wait_lattice_runtime_smoke() {
    ::crucible::algebra::lattices::detail::wait_lattice_self_test::runtime_smoke_test();
}

void test_mem_order_lattice_runtime_smoke() {
    ::crucible::algebra::lattices::detail::mem_order_lattice_self_test::runtime_smoke_test();
}

}  // namespace

int main() {
    std::fprintf(stderr, "test_algebra_compile:\n");

    run_test("test_modality_compile",                test_modality_compile);
    run_test("test_lattice_concepts_compile",        test_lattice_concepts_compile);
    run_test("test_graded_runtime_smoke",            test_graded_runtime_smoke);
    run_test("test_qtt_semiring_runtime_smoke",      test_qtt_semiring_runtime_smoke);
    run_test("test_bool_lattice_runtime_smoke",      test_bool_lattice_runtime_smoke);
    run_test("test_conf_lattice_runtime_smoke",      test_conf_lattice_runtime_smoke);
    run_test("test_trust_lattice_runtime_smoke",     test_trust_lattice_runtime_smoke);
    run_test("test_fractional_lattice_runtime_smoke",
             test_fractional_lattice_runtime_smoke);
    run_test("test_monotone_lattice_runtime_smoke",
             test_monotone_lattice_runtime_smoke);
    run_test("test_seq_prefix_lattice_runtime_smoke",
             test_seq_prefix_lattice_runtime_smoke);
    run_test("test_staleness_semiring_runtime_smoke",
             test_staleness_semiring_runtime_smoke);
    run_test("test_product_lattice_runtime_smoke",
             test_product_lattice_runtime_smoke);
    run_test("test_happens_before_runtime_smoke",
             test_happens_before_runtime_smoke);
    run_test("test_lifetime_lattice_runtime_smoke",
             test_lifetime_lattice_runtime_smoke);
    run_test("test_consistency_lattice_runtime_smoke",
             test_consistency_lattice_runtime_smoke);
    run_test("test_tolerance_lattice_runtime_smoke",
             test_tolerance_lattice_runtime_smoke);
    run_test("test_det_safe_lattice_runtime_smoke",
             test_det_safe_lattice_runtime_smoke);
    run_test("test_hot_path_lattice_runtime_smoke",
             test_hot_path_lattice_runtime_smoke);
    run_test("test_wait_lattice_runtime_smoke",
             test_wait_lattice_runtime_smoke);
    run_test("test_mem_order_lattice_runtime_smoke",
             test_mem_order_lattice_runtime_smoke);

    std::fprintf(stderr, "\n%d passed, %d failed\n", total_passed, total_failed);
    if (total_failed > 0) return EXIT_FAILURE;
    std::fprintf(stderr, "ALL PASSED\n");
    return EXIT_SUCCESS;
}
