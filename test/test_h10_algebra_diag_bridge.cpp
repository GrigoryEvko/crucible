// The include set of this translation unit is the algebra substrate,
// the stable-naming primitives and the standard library.  Nothing
// else, and the restriction is the point: if the naming primitives
// ever acquire a dependency on the safety wrapper hierarchy, the
// coupling surfaces here as a missing include, rather than silently
// later as a broken federation cache key.

#include <crucible/algebra/_Graded.h>
#include <crucible/algebra/_Lattice.h>
#include <crucible/algebra/_Modality.h>
#include <crucible/algebra/lattices/_ProductLattice.h>
#include <crucible/safety/diag/_StableName.h>

#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <string_view>
#include <tuple>
#include <type_traits>
#include <utility>

namespace alg = ::crucible::algebra;
namespace diag = ::crucible::safety::diag;

// Both lattices are reached out of internal detail namespaces.  They
// are the two concrete lattices this restricted include set offers,
// and only their type identity matters here.  The second one has an
// empty element type.
using TBL = alg::detail::lattice_self_test::TrivialBoolLattice;
using TEL = alg::detail::graded_self_test::TrivialEmptyLattice;

// A grade sometimes wraps a move-only resource such as an owned file
// descriptor.  Naming must stay purely type-level for those: a
// refactor that materializes a witness instance to read a name stops
// compiling against this type.
struct MoveOnlyWitness {
    int v = 0;
    constexpr MoveOnlyWitness() = default;
    constexpr explicit MoveOnlyWitness(int x) noexcept : v{x} {}
    MoveOnlyWitness(MoveOnlyWitness const&) = delete;
    MoveOnlyWitness& operator=(MoveOnlyWitness const&) = delete;
    constexpr MoveOnlyWitness(MoveOnlyWitness&&) noexcept = default;
    constexpr MoveOnlyWitness& operator=(MoveOnlyWitness&&) noexcept = default;
};
static_assert(!std::is_copy_constructible_v<MoveOnlyWitness>);
static_assert(std::is_move_constructible_v<MoveOnlyWitness>);

// These functions must sit at namespace scope ahead of every test
// that names one.  Taking the address as a template argument needs
// the address available at parse time.
inline void bridge_fn_a(int) noexcept {}
inline void bridge_fn_b(double) noexcept {}
inline int bridge_fn_c(int, double) noexcept { return 0; }

inline void bridge_fn_takes_graded(alg::Graded<alg::ModalityKind::Absolute, TBL, int>) noexcept {}

inline void bridge_fn_takes_graded_other(alg::Graded<alg::ModalityKind::Absolute, TBL, double>) noexcept {}

inline alg::Graded<alg::ModalityKind::Absolute, TBL, int> bridge_fn_returns_graded_int(int) noexcept { return {}; }

inline alg::Graded<alg::ModalityKind::Absolute, TBL, double> bridge_fn_returns_graded_double(int) noexcept {
    return {};
}

inline int bridge_fn_returns_int_same_arity(int) noexcept { return 0; }

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

#define EXPECT_TRUE(cond)                                                                            \
    do {                                                                                             \
        if (!(cond)) {                                                                               \
            std::fprintf(stderr, "    EXPECT_TRUE failed: %s (%s:%d)\n", #cond, __FILE__, __LINE__); \
            throw TestFailure{};                                                                     \
        }                                                                                            \
    } while (0)

void test_stable_name_on_plain_types() {
    static_assert(diag::stable_name_of<int>.find("int") != std::string_view::npos);
    static_assert(diag::stable_name_of<double>.find("double") != std::string_view::npos);

    static_assert(diag::stable_name_of<int> != diag::stable_name_of<double>);
}

void test_stable_name_on_graded() {
    using G_int = alg::Graded<alg::ModalityKind::Absolute, TBL, int>;
    using G_double = alg::Graded<alg::ModalityKind::Absolute, TBL, double>;
    using G_int_R = alg::Graded<alg::ModalityKind::Relative, TBL, int>;

    constexpr auto name_int = diag::stable_name_of<G_int>;
    constexpr auto name_double = diag::stable_name_of<G_double>;
    constexpr auto name_int_R = diag::stable_name_of<G_int_R>;

    // Match a shape with find(), never with == against a literal.  The
    // rendered display string is toolchain-specific, so only substring
    // containment is stable across compilers.
    static_assert(name_int.find("Graded") != std::string_view::npos);
    static_assert(name_int.find("int") != std::string_view::npos);
    static_assert(name_double.find("double") != std::string_view::npos);

    static_assert(name_int != name_double);
    static_assert(name_int != name_int_R);
}

void test_stable_type_id_distinctness_on_graded_axes() {
    using G_int_Abs = alg::Graded<alg::ModalityKind::Absolute, TBL, int>;
    using G_int_Rel = alg::Graded<alg::ModalityKind::Relative, TBL, int>;
    using G_int_Cmd = alg::Graded<alg::ModalityKind::Comonad, TBL, int>;
    using G_int_RMnd = alg::Graded<alg::ModalityKind::RelativeMonad, TBL, int>;
    using G_double_Abs = alg::Graded<alg::ModalityKind::Absolute, TBL, double>;

    // A wrapper that pins one modality must not share a cache key with
    // the same wrapper at another modality.
    static_assert(diag::stable_type_id<G_int_Abs> != diag::stable_type_id<G_int_Rel>);
    static_assert(diag::stable_type_id<G_int_Abs> != diag::stable_type_id<G_int_Cmd>);
    static_assert(diag::stable_type_id<G_int_Abs> != diag::stable_type_id<G_int_RMnd>);
    static_assert(diag::stable_type_id<G_int_Rel> != diag::stable_type_id<G_int_Cmd>);
    static_assert(diag::stable_type_id<G_int_Rel> != diag::stable_type_id<G_int_RMnd>);
    static_assert(diag::stable_type_id<G_int_Cmd> != diag::stable_type_id<G_int_RMnd>);

    static_assert(diag::stable_type_id<G_int_Abs> != diag::stable_type_id<G_double_Abs>);
}

void test_stable_type_id_determinism() {
    using G = alg::Graded<alg::ModalityKind::Absolute, TBL, int>;

    // Determinism holds within one build.  The rendered name is
    // toolchain-specific, so the identifier is not stable across
    // compilers.
    static_assert(diag::stable_type_id<G> == diag::stable_type_id<G>);

    // Nothing proves the mixer cannot land on zero for a non-empty
    // input.  These assertions pin the observed behaviour, so a change
    // to the mixer surfaces here.
    static_assert(diag::stable_type_id<G> != 0);
    static_assert(diag::stable_type_id<int> != 0);
}

void test_canonicalize_pack_on_plain_types() {
    // Canonical order is the lexicographic order of the stable names.
    using P1 = diag::canonicalize_pack_t<int, double, char>;
    using P2 = diag::canonicalize_pack_t<char, int, double>;
    using P3 = diag::canonicalize_pack_t<double, char, int>;

    static_assert(std::is_same_v<P1, P2>);
    static_assert(std::is_same_v<P2, P3>);

    using P_single = diag::canonicalize_pack_t<int>;
    static_assert(std::is_same_v<P_single, std::tuple<int>>);

    using P_empty = diag::canonicalize_pack_t<>;
    static_assert(std::is_same_v<P_empty, std::tuple<>>);
}

void test_canonicalize_pack_on_graded_types() {
    using G_int = alg::Graded<alg::ModalityKind::Absolute, TBL, int>;
    using G_double = alg::Graded<alg::ModalityKind::Absolute, TBL, double>;
    using G_char = alg::Graded<alg::ModalityKind::Absolute, TBL, char>;

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

void test_stable_function_id_distinct_signatures() {
    static_assert(diag::stable_function_id<&bridge_fn_a> != diag::stable_function_id<&bridge_fn_b>);
    static_assert(diag::stable_function_id<&bridge_fn_a> != diag::stable_function_id<&bridge_fn_c>);
    static_assert(diag::stable_function_id<&bridge_fn_b> != diag::stable_function_id<&bridge_fn_c>);
}

void test_stable_function_id_on_graded_param_signatures() {
    // Two callees of the same shape whose parameters differ only in
    // the wrapper instantiation must not share an identifier.
    static_assert(diag::stable_function_id<&bridge_fn_takes_graded>
                  != diag::stable_function_id<&bridge_fn_takes_graded_other>);

    static_assert(diag::stable_function_id<&bridge_fn_takes_graded> != diag::stable_function_id<&bridge_fn_a>);
}

void test_runtime_determinism_loop() {
    using G_int = alg::Graded<alg::ModalityKind::Absolute, TBL, int>;

    constexpr std::uint64_t baseline_int = diag::stable_type_id<int>;
    constexpr std::uint64_t baseline_double = diag::stable_type_id<double>;
    constexpr std::uint64_t baseline_g_int = diag::stable_type_id<G_int>;
    constexpr std::uint64_t baseline_fn_a = diag::stable_function_id<&bridge_fn_a>;
    constexpr std::uint64_t baseline_fn_g = diag::stable_function_id<&bridge_fn_takes_graded>;

    // The volatile cap stops the optimizer collapsing the loop to a
    // single iteration.
    volatile std::size_t const cap = 50;
    for (std::size_t i = 0; i < cap; ++i) {
        EXPECT_TRUE(diag::stable_type_id<int> == baseline_int);
        EXPECT_TRUE(diag::stable_type_id<double> == baseline_double);
        EXPECT_TRUE(diag::stable_type_id<G_int> == baseline_g_int);
        EXPECT_TRUE(diag::stable_function_id<&bridge_fn_a> == baseline_fn_a);
        EXPECT_TRUE(diag::stable_function_id<&bridge_fn_takes_graded> == baseline_fn_g);

        EXPECT_TRUE(diag::stable_type_id<G_int> == diag::stable_type_id<G_int>);
        EXPECT_TRUE(baseline_g_int != baseline_int);
    }
}

void test_stable_name_on_move_only_T() {
    using G_move = alg::Graded<alg::ModalityKind::Absolute, TBL, MoveOnlyWitness>;
    using G_int = alg::Graded<alg::ModalityKind::Absolute, TBL, int>;

    constexpr auto name_move = diag::stable_name_of<G_move>;
    constexpr auto name_int = diag::stable_name_of<G_int>;

    static_assert(!name_move.empty());
    static_assert(name_move.find("MoveOnlyWitness") != std::string_view::npos);
    static_assert(name_move != name_int);

    static_assert(diag::stable_type_id<G_move> != diag::stable_type_id<G_int>);
    static_assert(diag::stable_type_id<G_move> != 0);
}

// Production wrappers compose two lattices into a product.  A grade
// over that product must not share a cache key with a grade over
// either constituent alone.

void test_stable_type_id_on_product_lattice_composites() {
    using PL = alg::lattices::ProductLattice<TBL, TBL>;
    using G_pl = alg::Graded<alg::ModalityKind::Absolute, PL, int>;
    using G_tbl = alg::Graded<alg::ModalityKind::Absolute, TBL, int>;

    constexpr auto name_pl = diag::stable_name_of<G_pl>;
    constexpr auto name_tbl = diag::stable_name_of<G_tbl>;

    static_assert(name_pl.find("ProductLattice") != std::string_view::npos);
    static_assert(name_pl != name_tbl);

    static_assert(diag::stable_type_id<G_pl> != diag::stable_type_id<G_tbl>);
    static_assert(diag::stable_type_id<G_pl> != 0);

    using G_pl_double = alg::Graded<alg::ModalityKind::Absolute, PL, double>;
    static_assert(diag::stable_type_id<G_pl> != diag::stable_type_id<G_pl_double>);
}

// A matrix of static assertions cannot catch a divergence between the
// consteval and the runtime evaluation of the same identifier.  This
// peer drives the same modality matrix through volatile-anchored
// loads instead.

void test_runtime_modality_distinctness_peer() {
    using G_Abs = alg::Graded<alg::ModalityKind::Absolute, TBL, int>;
    using G_Rel = alg::Graded<alg::ModalityKind::Relative, TBL, int>;
    using G_Cmd = alg::Graded<alg::ModalityKind::Comonad, TBL, int>;
    using G_RMnd = alg::Graded<alg::ModalityKind::RelativeMonad, TBL, int>;

    // The volatile sink stops the optimizer folding each comparison
    // to a constant.
    volatile std::uint64_t id_abs = diag::stable_type_id<G_Abs>;
    volatile std::uint64_t id_rel = diag::stable_type_id<G_Rel>;
    volatile std::uint64_t id_cmd = diag::stable_type_id<G_Cmd>;
    volatile std::uint64_t id_rmnd = diag::stable_type_id<G_RMnd>;

    EXPECT_TRUE(id_abs != id_rel);
    EXPECT_TRUE(id_abs != id_cmd);
    EXPECT_TRUE(id_abs != id_rmnd);
    EXPECT_TRUE(id_rel != id_cmd);
    EXPECT_TRUE(id_rel != id_rmnd);
    EXPECT_TRUE(id_cmd != id_rmnd);

    EXPECT_TRUE(id_abs == diag::stable_type_id<G_Abs>);

    volatile std::size_t const cap = 25;
    for (std::size_t i = 0; i < cap; ++i) {
        EXPECT_TRUE(id_abs == diag::stable_type_id<G_Abs>);
        EXPECT_TRUE(id_rel == diag::stable_type_id<G_Rel>);
        EXPECT_TRUE(id_cmd == diag::stable_type_id<G_Cmd>);
        EXPECT_TRUE(id_rmnd == diag::stable_type_id<G_RMnd>);
    }
}

// Both legs of a product must reach the hash.  A product of one
// lattice with itself cannot show that: it already renders
// differently from the bare lattice, so an implementation that reads
// only the first leg still passes.  Two different lattices in both
// orders force the issue, because the two orders can differ only if
// both legs are read.

void test_stable_type_id_on_product_lattice_diff_legs() {
    using PL_AB = alg::lattices::ProductLattice<TBL, TEL>;
    using PL_BA = alg::lattices::ProductLattice<TEL, TBL>;
    using G_AB = alg::Graded<alg::ModalityKind::Absolute, PL_AB, int>;
    using G_BA = alg::Graded<alg::ModalityKind::Absolute, PL_BA, int>;

    static_assert(diag::stable_type_id<G_AB> != diag::stable_type_id<G_BA>);

    using G_TBL = alg::Graded<alg::ModalityKind::Absolute, TBL, int>;
    using G_TEL = alg::Graded<alg::ModalityKind::Absolute, TEL, int>;
    static_assert(diag::stable_type_id<G_AB> != diag::stable_type_id<G_TBL>);
    static_assert(diag::stable_type_id<G_AB> != diag::stable_type_id<G_TEL>);
    static_assert(diag::stable_type_id<G_BA> != diag::stable_type_id<G_TBL>);
    static_assert(diag::stable_type_id<G_BA> != diag::stable_type_id<G_TEL>);

    // Without this the two-order comparison above would be vacuous.
    static_assert(diag::stable_type_id<G_TBL> != diag::stable_type_id<G_TEL>);
}

// Canonicalization sorts and does not deduplicate.  Duplicates
// survive into the canonical form, and a caller that wants them
// merged has to do that itself.

void test_canonicalize_pack_with_graded_stutter() {
    using G_int = alg::Graded<alg::ModalityKind::Absolute, TBL, int>;
    using G_dbl = alg::Graded<alg::ModalityKind::Absolute, TBL, double>;

    using PStutter = diag::canonicalize_pack_t<G_int, G_int>;
    static_assert(std::is_same_v<PStutter, std::tuple<G_int, G_int>>);

    using PMixed1 = diag::canonicalize_pack_t<G_int, G_int, G_dbl>;
    using PMixed2 = diag::canonicalize_pack_t<G_dbl, G_int, G_int>;
    using PMixed3 = diag::canonicalize_pack_t<G_int, G_dbl, G_int>;

    static_assert(std::is_same_v<PMixed1, PMixed2>);
    static_assert(std::is_same_v<PMixed2, PMixed3>);

    static_assert(std::tuple_size_v<PMixed1> == 3);
}

// A downstream computation cache keys on a function's return type as
// well as on its parameters.  An implementation that hashes only the
// parameter list collides two callees that differ in return type
// alone, so the return-type axis gets its own witnesses.

void test_stable_function_id_on_graded_return_signatures() {
    constexpr auto id_returns_int_graded = diag::stable_function_id<&bridge_fn_returns_graded_int>;
    constexpr auto id_returns_dbl_graded = diag::stable_function_id<&bridge_fn_returns_graded_double>;
    constexpr auto id_returns_plain_int = diag::stable_function_id<&bridge_fn_returns_int_same_arity>;

    static_assert(id_returns_int_graded != id_returns_dbl_graded);

    static_assert(id_returns_int_graded != id_returns_plain_int);

    // The parameter axis and the return axis contribute independently.
    static_assert(id_returns_int_graded != diag::stable_function_id<&bridge_fn_takes_graded>);

    static_assert(id_returns_int_graded != 0);
    static_assert(id_returns_dbl_graded != 0);
}

void test_runtime_smoke_diversity() {
    // This one prints rather than asserts.  The exact rendering is
    // toolchain-specific, so the only claim available is that the
    // names come out non-empty and readable.
    constexpr auto name_int = diag::stable_name_of<int>;
    constexpr auto name_g_int = diag::stable_name_of<alg::Graded<alg::ModalityKind::Absolute, TBL, int>>;

    EXPECT_TRUE(!name_int.empty());
    EXPECT_TRUE(!name_g_int.empty());
    EXPECT_TRUE(name_g_int.size() > name_int.size());

    std::fprintf(stderr, "    stable_name_of<int>            = %.*s\n", static_cast<int>(name_int.size()),
                 name_int.data());
    std::fprintf(stderr, "    stable_name_of<Graded<A,TBL,i>>= %.*s\n", static_cast<int>(name_g_int.size()),
                 name_g_int.data());
}

}  // namespace

int main() {
    std::fprintf(stderr, "test_h10_algebra_diag_bridge:\n");
    run_test("test_stable_name_on_plain_types", test_stable_name_on_plain_types);
    run_test("test_stable_name_on_graded", test_stable_name_on_graded);
    run_test("test_stable_type_id_distinctness_on_graded_axes", test_stable_type_id_distinctness_on_graded_axes);
    run_test("test_stable_type_id_determinism", test_stable_type_id_determinism);
    run_test("test_canonicalize_pack_on_plain_types", test_canonicalize_pack_on_plain_types);
    run_test("test_canonicalize_pack_on_graded_types", test_canonicalize_pack_on_graded_types);
    run_test("test_canonicalize_pack_mixed_plain_and_graded", test_canonicalize_pack_mixed_plain_and_graded);
    run_test("test_stable_function_id_distinct_signatures", test_stable_function_id_distinct_signatures);
    run_test("test_stable_function_id_on_graded_param_signatures", test_stable_function_id_on_graded_param_signatures);
    run_test("test_runtime_determinism_loop", test_runtime_determinism_loop);
    run_test("test_runtime_smoke_diversity", test_runtime_smoke_diversity);
    run_test("test_stable_name_on_move_only_T", test_stable_name_on_move_only_T);
    run_test("test_stable_type_id_on_product_lattice_composites", test_stable_type_id_on_product_lattice_composites);
    run_test("test_runtime_modality_distinctness_peer", test_runtime_modality_distinctness_peer);
    run_test("test_stable_type_id_on_product_lattice_diff_legs", test_stable_type_id_on_product_lattice_diff_legs);
    run_test("test_canonicalize_pack_with_graded_stutter", test_canonicalize_pack_with_graded_stutter);
    run_test("test_stable_function_id_on_graded_return_signatures",
             test_stable_function_id_on_graded_return_signatures);
    std::fprintf(stderr, "\n%d passed, %d failed\n", total_passed, total_failed);
    if (total_failed > 0) return EXIT_FAILURE;
    std::fprintf(stderr, "ALL PASSED\n");
    return EXIT_SUCCESS;
}
