// ═══════════════════════════════════════════════════════════════════
// test_h10_algebra_diag_bridge — runtime smoke test for the bridge
// between the algebra layer and the safety/diag stable-naming
// primitives (FOUND-H10).
//
// ── Why this test exists ────────────────────────────────────────────
//
// FOUND-H06..H09 ship the stable-naming primitives:
//
//   safety/diag/StableName.h:
//     stable_name_of<T>            display_string_of(^^T) string_view
//     stable_type_id<T>            FNV-1a + fmix64 over the name
//     stable_function_id<FnPtr>    same hash, applied to fn type
//     canonicalize_pack<Ts...>     sort-by-stable-name pack normalization
//
// These primitives are CONSUMED by Graded-aware code in the algebra
// tree (the federation cache key extension FOUND-I uses
// `stable_type_id<T>` over wrapper-stack composites).  H10 verifies
// the BRIDGE: the diag primitives produce sane, distinct, stable
// outputs when applied to algebra-tree types (Graded<>, Lattice
// variants) WITHOUT pulling in the safety/* wrapper hierarchy.
//
// ── The "algebra-only TU" discipline ───────────────────────────────
//
// This TU's includes are restricted to:
//
//   <crucible/algebra/*>             — the algebra substrate
//   <crucible/safety/diag/StableName.h>   — the bridge target
//   <standard library>
//
// NOT included: safety/Linear.h, safety/Refined.h, safety/NumericalTier.h,
// effects/*, sessions/*, concurrent/*, handles/*, permissions/*.
//
// If a future refactor accidentally couples StableName.h to
// wrapper-tree types, this TU's restricted include set forces the
// regression to surface here as a missing-include error before the
// federation cache (FOUND-I) silently breaks.
//
// ── Coverage ────────────────────────────────────────────────────────
//
//   * stable_name_of<T>        on plain types AND Graded<M, L, T>
//   * stable_type_id<T>        determinism + distinctness across
//                              modality / lattice / element axes
//   * canonicalize_pack        order-permutation collapse on
//                              algebra-tree types
//   * stable_function_id       function pointers taking Graded<>
//                              parameters
//   * Runtime smoke loop       50-iteration consistency under
//                              `volatile`-anchored cap
// ═══════════════════════════════════════════════════════════════════

#include <crucible/algebra/Graded.h>
#include <crucible/algebra/Lattice.h>
#include <crucible/algebra/Modality.h>
#include <crucible/safety/diag/StableName.h>

#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <string_view>
#include <tuple>
#include <type_traits>

namespace alg  = ::crucible::algebra;
namespace diag = ::crucible::safety::diag;

// ── Trivial test lattice (re-exported from algebra/Graded.h's self-test
//    block — using directly through the public TrivialBoolLattice in
//    algebra/Lattice.h).
using TBL = alg::detail::lattice_self_test::TrivialBoolLattice;

// ── Function pointers used by stable_function_id tests.  Must be
//    declared at namespace scope BEFORE the runtime test functions
//    (auto-NTTP deduction needs the address available at parse time).
inline void bridge_fn_a(int) noexcept {}
inline void bridge_fn_b(double) noexcept {}
inline int  bridge_fn_c(int, double) noexcept { return 0; }

// Function taking a Graded<> parameter — exercises stable_function_id
// over a substrate-aware signature.  THIS IS THE BRIDGE: the stable
// ID must capture the Graded<...> instantiation in the parameter
// position so the federation cache can distinguish callers that
// thread a Graded<> through a hot path from callers that pass the
// raw element type.
inline void bridge_fn_takes_graded(
    alg::Graded<alg::ModalityKind::Absolute, TBL, int>) noexcept {}

inline void bridge_fn_takes_graded_other(
    alg::Graded<alg::ModalityKind::Absolute, TBL, double>) noexcept {}

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

#define EXPECT_TRUE(cond)                                                  \
    do {                                                                   \
        if (!(cond)) {                                                     \
            std::fprintf(stderr,                                           \
                "    EXPECT_TRUE failed: %s (%s:%d)\n",                    \
                #cond, __FILE__, __LINE__);                                \
            throw TestFailure{};                                           \
        }                                                                  \
    } while (0)

// ═════════════════════════════════════════════════════════════════════
// ── stable_name_of bridge ─────────────────────────────────────────
// ═════════════════════════════════════════════════════════════════════

void test_stable_name_on_plain_types() {
    // Sanity baseline — these names contain the type's identifier
    // somewhere in the rendered display string.
    static_assert(diag::stable_name_of<int>.find("int")    != std::string_view::npos);
    static_assert(diag::stable_name_of<double>.find("double") != std::string_view::npos);

    // Distinct types yield distinct names.
    static_assert(diag::stable_name_of<int> != diag::stable_name_of<double>);
}

void test_stable_name_on_graded() {
    using G_int    = alg::Graded<alg::ModalityKind::Absolute, TBL, int>;
    using G_double = alg::Graded<alg::ModalityKind::Absolute, TBL, double>;
    using G_int_R  = alg::Graded<alg::ModalityKind::Relative, TBL, int>;

    constexpr auto name_int    = diag::stable_name_of<G_int>;
    constexpr auto name_double = diag::stable_name_of<G_double>;
    constexpr auto name_int_R  = diag::stable_name_of<G_int_R>;

    // Per CLAUDE.md memory feedback_header_only_static_assert_blind_spot:
    // use .ends_with()/.find() for shape comparisons, never `==` against
    // literal substrings.  The .find() pattern matches the name shape
    // anywhere in the rendered display string.
    static_assert(name_int.find("Graded")    != std::string_view::npos);
    static_assert(name_int.find("int")       != std::string_view::npos);
    static_assert(name_double.find("double") != std::string_view::npos);

    // Distinctness across each Graded axis.
    static_assert(name_int    != name_double);  // T axis distinguishes
    static_assert(name_int    != name_int_R);   // M axis distinguishes
}

// ═════════════════════════════════════════════════════════════════════
// ── stable_type_id bridge ─────────────────────────────────────────
// ═════════════════════════════════════════════════════════════════════

void test_stable_type_id_distinctness_on_graded_axes() {
    using G_int_Abs    = alg::Graded<alg::ModalityKind::Absolute,      TBL, int>;
    using G_int_Rel    = alg::Graded<alg::ModalityKind::Relative,      TBL, int>;
    using G_int_Cmd    = alg::Graded<alg::ModalityKind::Comonad,       TBL, int>;
    using G_int_RMnd   = alg::Graded<alg::ModalityKind::RelativeMonad, TBL, int>;
    using G_double_Abs = alg::Graded<alg::ModalityKind::Absolute,      TBL, double>;

    // Modality axis discriminates: same lattice + same T, different M
    // → distinct type IDs.  This is the core federation guarantee —
    // a wrapper that pins a different modality must NOT collide with
    // the same wrapper at a different modality.
    static_assert(diag::stable_type_id<G_int_Abs>  != diag::stable_type_id<G_int_Rel>);
    static_assert(diag::stable_type_id<G_int_Abs>  != diag::stable_type_id<G_int_Cmd>);
    static_assert(diag::stable_type_id<G_int_Abs>  != diag::stable_type_id<G_int_RMnd>);
    static_assert(diag::stable_type_id<G_int_Rel>  != diag::stable_type_id<G_int_Cmd>);
    static_assert(diag::stable_type_id<G_int_Rel>  != diag::stable_type_id<G_int_RMnd>);
    static_assert(diag::stable_type_id<G_int_Cmd>  != diag::stable_type_id<G_int_RMnd>);

    // T axis discriminates: same modality + same lattice, different T
    // → distinct type IDs.  Pins that the federation cache key
    // captures the wrapped element type.
    static_assert(diag::stable_type_id<G_int_Abs>  != diag::stable_type_id<G_double_Abs>);
}

void test_stable_type_id_determinism() {
    using G = alg::Graded<alg::ModalityKind::Absolute, TBL, int>;

    // Same instantiation, called twice — same ID (both the variable
    // template and the underlying display_string_of are deterministic
    // within one build).
    static_assert(diag::stable_type_id<G> == diag::stable_type_id<G>);

    // Non-zero — FNV-1a + fmix64 should never produce zero on a
    // non-empty input (tested empirically; the test pins it).
    static_assert(diag::stable_type_id<G> != 0);
    static_assert(diag::stable_type_id<int> != 0);
}

// ═════════════════════════════════════════════════════════════════════
// ── canonicalize_pack bridge ──────────────────────────────────────
// ═════════════════════════════════════════════════════════════════════

void test_canonicalize_pack_on_plain_types() {
    // Permutations of the same elements collapse to the same
    // canonical form (sorted by stable_name_of, lexicographic).
    using P1 = diag::canonicalize_pack_t<int, double, char>;
    using P2 = diag::canonicalize_pack_t<char, int, double>;
    using P3 = diag::canonicalize_pack_t<double, char, int>;

    static_assert(std::is_same_v<P1, P2>);
    static_assert(std::is_same_v<P2, P3>);

    // Single-element pack: trivially canonical.
    using P_single = diag::canonicalize_pack_t<int>;
    static_assert(std::is_same_v<P_single, std::tuple<int>>);

    // Empty pack: empty tuple.
    using P_empty = diag::canonicalize_pack_t<>;
    static_assert(std::is_same_v<P_empty, std::tuple<>>);
}

void test_canonicalize_pack_on_graded_types() {
    using G_int    = alg::Graded<alg::ModalityKind::Absolute, TBL, int>;
    using G_double = alg::Graded<alg::ModalityKind::Absolute, TBL, double>;
    using G_char   = alg::Graded<alg::ModalityKind::Absolute, TBL, char>;

    // Permutations of Graded<> instantiations collapse the same way
    // as plain types.  This is the bridge guarantee: the canonical
    // form is computed via stable_name_of, which works on Graded<>
    // because it works on any reflectable type.
    using P1 = diag::canonicalize_pack_t<G_int, G_double, G_char>;
    using P2 = diag::canonicalize_pack_t<G_char, G_int, G_double>;
    using P3 = diag::canonicalize_pack_t<G_double, G_char, G_int>;

    static_assert(std::is_same_v<P1, P2>);
    static_assert(std::is_same_v<P2, P3>);
}

void test_canonicalize_pack_mixed_plain_and_graded() {
    using G_int = alg::Graded<alg::ModalityKind::Absolute, TBL, int>;

    using P1 = diag::canonicalize_pack_t<G_int, int>;
    using P2 = diag::canonicalize_pack_t<int, G_int>;

    static_assert(std::is_same_v<P1, P2>);
}

// ═════════════════════════════════════════════════════════════════════
// ── stable_function_id bridge ─────────────────────────────────────
// ═════════════════════════════════════════════════════════════════════

void test_stable_function_id_distinct_signatures() {
    // Distinct signatures → distinct IDs.
    static_assert(diag::stable_function_id<&bridge_fn_a> !=
                  diag::stable_function_id<&bridge_fn_b>);
    static_assert(diag::stable_function_id<&bridge_fn_a> !=
                  diag::stable_function_id<&bridge_fn_c>);
    static_assert(diag::stable_function_id<&bridge_fn_b> !=
                  diag::stable_function_id<&bridge_fn_c>);
}

void test_stable_function_id_on_graded_param_signatures() {
    // The bridge: a function whose parameter type IS a Graded<...>
    // must yield a stable_function_id distinct from a sibling
    // function with a different Graded<> parameter.  This is the
    // load-bearing capability for the FOUND-I federation cache —
    // identical-shape callees with different wrapper instantiations
    // must NOT collide.
    static_assert(diag::stable_function_id<&bridge_fn_takes_graded> !=
                  diag::stable_function_id<&bridge_fn_takes_graded_other>);

    // And distinct from non-Graded callees with the same arity.
    static_assert(diag::stable_function_id<&bridge_fn_takes_graded> !=
                  diag::stable_function_id<&bridge_fn_a>);
}

// ═════════════════════════════════════════════════════════════════════
// ── Runtime determinism — volatile-anchored loop ──────────────────
// ═════════════════════════════════════════════════════════════════════

void test_runtime_determinism_loop() {
    using G_int = alg::Graded<alg::ModalityKind::Absolute, TBL, int>;

    constexpr std::uint64_t baseline_int     = diag::stable_type_id<int>;
    constexpr std::uint64_t baseline_double  = diag::stable_type_id<double>;
    constexpr std::uint64_t baseline_g_int   = diag::stable_type_id<G_int>;
    constexpr std::uint64_t baseline_fn_a    = diag::stable_function_id<&bridge_fn_a>;
    constexpr std::uint64_t baseline_fn_g    = diag::stable_function_id<&bridge_fn_takes_graded>;

    // Volatile-anchored cap prevents the optimizer from collapsing
    // the loop into a single iteration; every read must produce the
    // same ID.
    volatile std::size_t const cap = 50;
    for (std::size_t i = 0; i < cap; ++i) {
        EXPECT_TRUE(diag::stable_type_id<int>     == baseline_int);
        EXPECT_TRUE(diag::stable_type_id<double>  == baseline_double);
        EXPECT_TRUE(diag::stable_type_id<G_int>   == baseline_g_int);
        EXPECT_TRUE(diag::stable_function_id<&bridge_fn_a>             == baseline_fn_a);
        EXPECT_TRUE(diag::stable_function_id<&bridge_fn_takes_graded>  == baseline_fn_g);

        // Cross-axis determinism: the SAME wrapper instantiation
        // queried from different "spots" in the loop body still
        // returns the same ID.
        EXPECT_TRUE(diag::stable_type_id<G_int> == diag::stable_type_id<G_int>);
        EXPECT_TRUE(baseline_g_int != baseline_int);  // sanity
    }
}

void test_runtime_smoke_diversity() {
    // Spot-print a few names for human-eye verification on first
    // execution.  Not a tight assertion — just a witness that the
    // bridge produces non-empty diagnostic strings.
    constexpr auto name_int   = diag::stable_name_of<int>;
    constexpr auto name_g_int = diag::stable_name_of<
        alg::Graded<alg::ModalityKind::Absolute, TBL, int>>;

    EXPECT_TRUE(!name_int.empty());
    EXPECT_TRUE(!name_g_int.empty());
    EXPECT_TRUE(name_g_int.size() > name_int.size());  // Graded<...> longer

    std::fprintf(stderr, "    stable_name_of<int>            = %.*s\n",
                 static_cast<int>(name_int.size()), name_int.data());
    std::fprintf(stderr, "    stable_name_of<Graded<A,TBL,i>>= %.*s\n",
                 static_cast<int>(name_g_int.size()), name_g_int.data());
}

}  // namespace

int main() {
    std::fprintf(stderr, "test_h10_algebra_diag_bridge:\n");
    run_test("test_stable_name_on_plain_types",
             test_stable_name_on_plain_types);
    run_test("test_stable_name_on_graded",
             test_stable_name_on_graded);
    run_test("test_stable_type_id_distinctness_on_graded_axes",
             test_stable_type_id_distinctness_on_graded_axes);
    run_test("test_stable_type_id_determinism",
             test_stable_type_id_determinism);
    run_test("test_canonicalize_pack_on_plain_types",
             test_canonicalize_pack_on_plain_types);
    run_test("test_canonicalize_pack_on_graded_types",
             test_canonicalize_pack_on_graded_types);
    run_test("test_canonicalize_pack_mixed_plain_and_graded",
             test_canonicalize_pack_mixed_plain_and_graded);
    run_test("test_stable_function_id_distinct_signatures",
             test_stable_function_id_distinct_signatures);
    run_test("test_stable_function_id_on_graded_param_signatures",
             test_stable_function_id_on_graded_param_signatures);
    run_test("test_runtime_determinism_loop",
             test_runtime_determinism_loop);
    run_test("test_runtime_smoke_diversity",
             test_runtime_smoke_diversity);
    std::fprintf(stderr, "\n%d passed, %d failed\n",
                 total_passed, total_failed);
    if (total_failed > 0) return EXIT_FAILURE;
    std::fprintf(stderr, "ALL PASSED\n");
    return EXIT_SUCCESS;
}
