// This translation unit exists so that the compile-time checks
// embedded in the row-hash fold header run at least once under the
// project's full warning matrix.  An assertion inside a header that
// no translation unit pulls in is never compiled at all.
//
// Every raw result below lands in a volatile store.  Without that the
// optimizer folds each check back to a compile-time constant, and a
// consteval path that disagrees with the runtime path stays hidden.

#include <crucible/safety/diag/RowHashFold.h>
#include <crucible/safety/diag/RowHashGrade.h>
#include <crucible/Types.h>
#include <crucible/effects/_Computation.h>
#include <crucible/safety/_HotPath.h>

#include "test_assert.h"

#include <cstdio>
#include <cstdint>

namespace ce = crucible::effects;
namespace cd = crucible::safety::diag;
using crucible::RowHash;

static void test_runtime_permutation_invariance() {
    using ce::Effect;
    using ce::Row;

    volatile std::uint64_t sink_ai = cd::row_hash_of_v<Row<Effect::Alloc, Effect::IO>>.raw();
    volatile std::uint64_t sink_ia = cd::row_hash_of_v<Row<Effect::IO, Effect::Alloc>>.raw();
    assert(sink_ai == sink_ia);

    volatile std::uint64_t sink_bg = cd::row_hash_of_v<Row<Effect::Block, Effect::Bg>>.raw();
    volatile std::uint64_t sink_gb = cd::row_hash_of_v<Row<Effect::Bg, Effect::Block>>.raw();
    assert(sink_bg == sink_gb);

    volatile std::uint64_t sink_aib = cd::row_hash_of_v<Row<Effect::Alloc, Effect::IO, Effect::Block>>.raw();
    volatile std::uint64_t sink_bia = cd::row_hash_of_v<Row<Effect::Block, Effect::IO, Effect::Alloc>>.raw();
    volatile std::uint64_t sink_iba = cd::row_hash_of_v<Row<Effect::IO, Effect::Block, Effect::Alloc>>.raw();
    assert(sink_aib == sink_bia);
    assert(sink_aib == sink_iba);
    assert(sink_bia == sink_iba);

    std::printf("  test_permutation_invariance:    PASSED\n");
}

// A row and a strict superset of it must not share a cache slot, so
// adding one effect always changes the hash.
static void test_runtime_cardinality_discrimination() {
    using ce::Effect;
    using ce::Row;

    volatile std::uint64_t h0 = cd::row_hash_of_v<Row<>>.raw();
    volatile std::uint64_t h1 = cd::row_hash_of_v<Row<Effect::Alloc>>.raw();
    volatile std::uint64_t h2 = cd::row_hash_of_v<Row<Effect::Alloc, Effect::IO>>.raw();
    volatile std::uint64_t h3 = cd::row_hash_of_v<Row<Effect::Alloc, Effect::IO, Effect::Block>>.raw();
    volatile std::uint64_t h6 =
        cd::row_hash_of_v<Row<Effect::Alloc, Effect::IO, Effect::Block, Effect::Bg, Effect::Init, Effect::Test>>.raw();

    assert(h0 != h1);
    assert(h1 != h2);
    assert(h2 != h3);
    assert(h3 != h6);
    assert(h0 != h6);

    // Zero marks a bare type and UINT64_MAX marks an empty cache
    // slot.  A real row hash has to land between them.
    assert(h0 != 0);
    assert(h1 != 0);
    assert(h6 != 0);
    assert(h0 != static_cast<std::uint64_t>(-1));
    assert(h6 != static_cast<std::uint64_t>(-1));

    std::printf("  test_cardinality:               PASSED\n");
}

// A row is a set, so a repeated atom must not open a second cache
// slot for the same row.
static void test_runtime_set_semantic_dedup() {
    using ce::Effect;
    using ce::Row;

    volatile std::uint64_t single_alloc = cd::row_hash_of_v<Row<Effect::Alloc>>.raw();
    volatile std::uint64_t double_alloc = cd::row_hash_of_v<Row<Effect::Alloc, Effect::Alloc>>.raw();
    assert(single_alloc == double_alloc);

    volatile std::uint64_t single_io = cd::row_hash_of_v<Row<Effect::IO>>.raw();
    volatile std::uint64_t double_io = cd::row_hash_of_v<Row<Effect::IO, Effect::IO>>.raw();
    assert(single_io == double_io);

    volatile std::uint64_t triple_io = cd::row_hash_of_v<Row<Effect::IO, Effect::IO, Effect::IO>>.raw();
    assert(single_io == triple_io);

    volatile std::uint64_t alloc_io_pair = cd::row_hash_of_v<Row<Effect::Alloc, Effect::IO>>.raw();
    volatile std::uint64_t alloc_io_left_dup = cd::row_hash_of_v<Row<Effect::Alloc, Effect::Alloc, Effect::IO>>.raw();
    volatile std::uint64_t alloc_io_right_dup = cd::row_hash_of_v<Row<Effect::Alloc, Effect::IO, Effect::IO>>.raw();
    assert(alloc_io_pair == alloc_io_left_dup);
    assert(alloc_io_pair == alloc_io_right_dup);

    volatile std::uint64_t bg_io_pair = cd::row_hash_of_v<Row<Effect::IO, Effect::Bg>>.raw();
    volatile std::uint64_t bg_io_interleaved =
        cd::row_hash_of_v<Row<Effect::Bg, Effect::IO, Effect::Bg, Effect::IO>>.raw();
    volatile std::uint64_t io_bg_interleaved =
        cd::row_hash_of_v<Row<Effect::IO, Effect::Bg, Effect::Bg, Effect::IO>>.raw();
    assert(bg_io_pair == bg_io_interleaved);
    assert(bg_io_pair == io_bg_interleaved);

    // Four atoms, two of them unique.  A seed fed the raw pack size
    // rather than the count of unique atoms would differ from the
    // canonical pair's seed, and only this shape shows it.
    volatile std::uint64_t alloc_io_2x2 =
        cd::row_hash_of_v<Row<Effect::Alloc, Effect::Alloc, Effect::IO, Effect::IO>>.raw();
    assert(alloc_io_2x2 == alloc_io_pair);

    // Deduplication must not over-collapse: a repeated atom is still
    // a smaller row than two distinct atoms.
    volatile std::uint64_t io_bg_canonical = cd::row_hash_of_v<Row<Effect::IO, Effect::Bg>>.raw();
    assert(double_io != io_bg_canonical);

    std::printf("  test_set_semantic_dedup:        PASSED\n");
}

static void test_runtime_bare_types_zero() {
    volatile std::uint64_t h_int = cd::row_hash_of_v<int>.raw();
    volatile std::uint64_t h_float = cd::row_hash_of_v<float>.raw();
    volatile std::uint64_t h_double = cd::row_hash_of_v<double>.raw();
    volatile std::uint64_t h_void = cd::row_hash_of_v<void>.raw();

    assert(h_int == 0);
    assert(h_float == 0);
    assert(h_double == 0);
    assert(h_void == 0);

    // Zero says "bare type" and is not the empty-slot marker.  The
    // two are distinct cache states.
    auto rh_int = cd::row_hash_of_v<int>;
    assert(!rh_int.is_sentinel());
    assert(rh_int == RowHash{});

    std::printf("  test_bare_types_zero:           PASSED\n");
}

// An empty row is still a row.  A bare payload declares no row at
// all.  The cache has to tell the two apart.
static void test_runtime_empty_row_distinct_from_bare() {
    volatile std::uint64_t h_empty_row = cd::row_hash_of_v<ce::EmptyRow>.raw();
    volatile std::uint64_t h_bare = cd::row_hash_of_v<int>.raw();

    assert(h_empty_row != h_bare);
    assert(h_empty_row != 0);
    assert(h_bare == 0);

    // The empty row's hash is a published constant.  Pinning it here
    // catches a change of seed strategy at run time too.
    assert(h_empty_row == cd::detail::EMPTY_ROW_HASH);

    std::printf("  test_empty_row_distinct:        PASSED\n");
}

static void test_runtime_determinism() {
    using ce::Effect;
    using ce::Row;

    auto get_hash = []() noexcept -> std::uint64_t { return cd::row_hash_of_v<Row<Effect::Alloc, Effect::IO>>.raw(); };

    volatile std::uint64_t a = get_hash();
    volatile std::uint64_t b = get_hash();
    volatile std::uint64_t c = get_hash();
    assert(a == b);
    assert(b == c);
    assert(a == c);

    std::printf("  test_determinism:               PASSED\n");
}

static void test_runtime_computation_specialization() {
    using ce::Effect;
    using ce::EmptyRow;
    using ce::Row;
    using ce::Computation;

    volatile std::uint64_t sink_comp_int = cd::row_hash_of_v<Computation<EmptyRow, int>>.raw();
    volatile std::uint64_t sink_int = cd::row_hash_of_v<int>.raw();
    assert(sink_comp_int != sink_int);
    assert(sink_comp_int != 0);

    volatile std::uint64_t sink_empty_row = cd::row_hash_of_v<EmptyRow>.raw();
    assert(sink_comp_int != sink_empty_row);

    // The carrier's hash ignores its payload and reads only its row.
    volatile std::uint64_t sink_comp_double = cd::row_hash_of_v<Computation<EmptyRow, double>>.raw();
    assert(sink_comp_int == sink_comp_double);

    volatile std::uint64_t sink_comp_alloc_int = cd::row_hash_of_v<Computation<Row<Effect::Alloc>, int>>.raw();
    volatile std::uint64_t sink_comp_alloc_char = cd::row_hash_of_v<Computation<Row<Effect::Alloc>, char>>.raw();
    assert(sink_comp_alloc_int == sink_comp_alloc_char);

    volatile std::uint64_t sink_comp_io_int = cd::row_hash_of_v<Computation<Row<Effect::IO>, int>>.raw();
    assert(sink_comp_alloc_int != sink_comp_io_int);
    assert(sink_comp_alloc_int != sink_comp_int);

    // Permutation invariance and cardinality discrimination both lift
    // from a bare row through the carrier.
    volatile std::uint64_t sink_comp_alloc_io =
        cd::row_hash_of_v<Computation<Row<Effect::Alloc, Effect::IO>, int>>.raw();
    volatile std::uint64_t sink_comp_io_alloc =
        cd::row_hash_of_v<Computation<Row<Effect::IO, Effect::Alloc>, int>>.raw();
    assert(sink_comp_alloc_io == sink_comp_io_alloc);

    assert(sink_comp_alloc_int != sink_comp_alloc_io);

    volatile std::uint64_t sink_nested =
        cd::row_hash_of_v<Computation<EmptyRow, Computation<Row<Effect::IO>, int>>>.raw();
    assert(sink_nested != sink_comp_int);
    assert(sink_nested != sink_comp_io_int);

    // None of these lands on the empty-slot marker.
    assert(sink_comp_int != static_cast<std::uint64_t>(-1));
    assert(sink_comp_alloc_int != static_cast<std::uint64_t>(-1));
    assert(sink_nested != static_cast<std::uint64_t>(-1));

    std::printf("  test_computation_specialization: PASSED\n");
}

// Every hex literal below duplicates a compile-time pin kept beside
// the fold itself.  The duplication is the point: a value that agrees
// at compile time and diverges at run time is the one failure a
// header-side assertion cannot see.  Recompute both sides together
// whenever the fold changes.
static void test_runtime_federation_hash_pins() {
    using ce::Effect;
    using ce::EmptyRow;
    using ce::Row;

    volatile std::uint64_t sink;

    sink = cd::row_hash_of_v<EmptyRow>.raw();
    assert(sink == 0xEFD01F60BA992926ULL);

    sink = cd::row_hash_of_v<Row<Effect::Alloc>>.raw();
    assert(sink == 0x436DAF9EDCB565C3ULL);

    sink = cd::row_hash_of_v<Row<Effect::IO>>.raw();
    assert(sink == 0x6FBFD0F707B63BECULL);

    sink = cd::row_hash_of_v<Row<Effect::Block>>.raw();
    assert(sink == 0x3117F06B828C9247ULL);

    sink = cd::row_hash_of_v<Row<Effect::Bg>>.raw();
    assert(sink == 0x008A519814C8FC81ULL);

    sink = cd::row_hash_of_v<Row<Effect::Init>>.raw();
    assert(sink == 0x9E23FC5AC81DA675ULL);

    sink = cd::row_hash_of_v<Row<Effect::Test>>.raw();
    assert(sink == 0x26A9EB08E748D58FULL);

    sink = cd::row_hash_of_v<Row<Effect::Alloc, Effect::IO>>.raw();
    assert(sink == 0x6CC046F52E6D7663ULL);

    sink =
        cd::row_hash_of_v<Row<Effect::Alloc, Effect::IO, Effect::Block, Effect::Bg, Effect::Init, Effect::Test>>.raw();
    assert(sink == 0x1C9D0E4F548FAAD6ULL);

    using ce::Computation;

    sink = cd::row_hash_of_v<Computation<EmptyRow, int>>.raw();
    assert(sink == 0x49A55BE1CFC23FB0ULL);

    sink = cd::row_hash_of_v<Computation<Row<Effect::Bg>, int>>.raw();
    assert(sink == 0x3ACE35615F0F9243ULL);

    sink = cd::row_hash_of_v<Computation<Row<Effect::Alloc, Effect::IO>, int>>.raw();
    assert(sink == 0x83D432DE6CDEACA7ULL);

    sink = cd::row_hash_of_v<Computation<EmptyRow, Computation<Row<Effect::IO>, int>>>.raw();
    assert(sink == 0x94EC56B861A6B8FDULL);

    sink = cd::row_hash_of_v<Computation<Row<Effect::Alloc>, int>>.raw();
    assert(sink == 0x058CA6EFB434D439ULL);

    sink = cd::row_hash_of_v<Computation<Row<Effect::IO>, int>>.raw();
    assert(sink == 0xCCFE717213BBA49CULL);

    sink = cd::row_hash_of_v<Computation<Row<Effect::Block>, int>>.raw();
    assert(sink == 0x6D28A236D0E146C7ULL);

    sink = cd::row_hash_of_v<Computation<Row<Effect::Init>, int>>.raw();
    assert(sink == 0x64EF4D0126C4A4E3ULL);

    sink = cd::row_hash_of_v<Computation<Row<Effect::Test>, int>>.raw();
    assert(sink == 0xF4060D16B464EFDEULL);

    sink = cd::row_hash_of_v<Computation<EmptyRow, Computation<Row<Effect::Alloc>, int>>>.raw();
    assert(sink == 0x0BECBF75AD6D7A0CULL);

    sink = cd::row_hash_of_v<Computation<EmptyRow, Computation<Row<Effect::Block>, int>>>.raw();
    assert(sink == 0x32894FE89819DEA1ULL);

    sink = cd::row_hash_of_v<Computation<EmptyRow, Computation<Row<Effect::Bg>, int>>>.raw();
    assert(sink == 0xEDF6E609659BD93CULL);

    sink = cd::row_hash_of_v<Computation<EmptyRow, Computation<Row<Effect::Init>, int>>>.raw();
    assert(sink == 0x93C6E9DAD4DDF07AULL);

    sink = cd::row_hash_of_v<Computation<EmptyRow, Computation<Row<Effect::Test>, int>>>.raw();
    assert(sink == 0x792A21E2C4F20C13ULL);

    sink = cd::row_hash_of_v<Computation<Row<Effect::Bg>, Computation<EmptyRow, int>>>.raw();
    assert(sink == 0x40D0E7791202A526ULL);

    sink = cd::row_hash_of_v<Computation<Row<Effect::Bg>, Computation<Row<Effect::Bg>, int>>>.raw();
    assert(sink == 0xAFCB34F7B12A2F95ULL);

    sink = cd::row_hash_of_v<Computation<Row<Effect::Alloc>, Computation<Row<Effect::IO>, int>>>.raw();
    assert(sink == 0xB25AFEA0CE322A7EULL);

    sink = cd::row_hash_of_v<Computation<Row<Effect::Alloc>,
        Computation<Row<Effect::IO>,
            Computation<Row<Effect::Block>, int>>>>.raw();
    assert(sink == 0xAC3F22322B23C1FEULL);

    std::printf("  test_federation_hash_pins:       PASSED\n");
}

// Two order properties, both easy to misread as symmetries.
//
// Inside a carrier the row slot and the payload slot are not
// interchangeable, because the combiner is not commutative.  The
// witness below swaps two rows rather than a row and a payload: a
// bare integral type is not row-shaped, so two distinct rows are the
// only legal way to fill both slots in either order.
//
// Across a wrapper, wrapping a carrier is not the same as wrapping
// the carrier's payload.  A reader who expects those two to agree
// gets an inequality here instead.  Flattening that distinction would
// reassign the cache slot of every wrapped row-typed carrier.
static void test_runtime_wrapper_computation_interleave_order() {
    using ce::Effect;
    using ce::EmptyRow;
    using ce::Row;
    using ce::Computation;
    using crucible::safety::HotPath;
    using crucible::safety::HotPathTier_v;

    using R_A = Row<Effect::Alloc>;
    using R_B = Row<Effect::IO>;

    volatile std::uint64_t sink_AB = cd::row_hash_of_v<Computation<R_A, R_B>>.raw();
    volatile std::uint64_t sink_BA = cd::row_hash_of_v<Computation<R_B, R_A>>.raw();

    assert(sink_AB != sink_BA);

    // The next two expressions name the same atoms.  Only the nesting
    // differs.
    volatile std::uint64_t sink_wrap_outside =
        cd::row_hash_of_v<HotPath<HotPathTier_v::Hot, Computation<Row<Effect::Bg>, int>>>.raw();
    volatile std::uint64_t sink_wrap_inside =
        cd::row_hash_of_v<Computation<Row<Effect::Bg>, HotPath<HotPathTier_v::Hot, int>>>.raw();
    assert(sink_wrap_outside != sink_wrap_inside);

    // The wrapper contributes from either nesting position, so both
    // also differ from the unwrapped carrier.
    volatile std::uint64_t sink_bare_comp = cd::row_hash_of_v<Computation<Row<Effect::Bg>, int>>.raw();
    assert(sink_wrap_outside != sink_bare_comp);
    assert(sink_wrap_inside != sink_bare_comp);

    std::printf("  test_wrapper_computation_interleave_order: PASSED\n");
}

// Two predicate types that behave identically hash to different
// refinement slots by default.  That fragmentation is the honest
// answer: structural identity is all the default can see.  Where two
// predicates really are interchangeable, specializing the canonical-id
// extension point collapses them onto one slot, and it must do so for
// the sealed and unsealed refinements together.

namespace found_058_witness {

struct pred_a_impl {
    constexpr bool operator()(int x) const noexcept { return x > 0; }
};
struct pred_b_impl {
    // Same behaviour as pred_a_impl, deliberately a distinct type.
    constexpr bool operator()(int x) const noexcept { return x > 0; }
};

inline constexpr pred_a_impl pred_a{};
inline constexpr pred_b_impl pred_b{};

}  // namespace found_058_witness

namespace crucible::safety::diag {
template <>
struct pred_canonical_id<found_058_witness::pred_b> {
    static constexpr std::uint64_t value = pred_canonical_id<found_058_witness::pred_a>::value;
};
}  // namespace crucible::safety::diag

static void test_pred_canonical_id_customization() {
    using crucible::safety::Refined;
    using crucible::safety::SealedRefined;
    namespace cd = crucible::safety::diag;
    using found_058_witness::pred_a;
    using found_058_witness::pred_b;

    constexpr auto h_a = cd::row_hash_contribution_v<Refined<pred_a, int>>;
    constexpr auto h_b_after = cd::row_hash_contribution_v<Refined<pred_b, int>>;

    assert(h_a == h_b_after);
    assert(h_a != 0);

    constexpr auto sh_a = cd::row_hash_contribution_v<SealedRefined<pred_a, int>>;
    constexpr auto sh_b_after = cd::row_hash_contribution_v<SealedRefined<pred_b, int>>;
    assert(sh_a == sh_b_after);
    assert(sh_a != 0);

    // Collapsing the predicate axis leaves the two refinement
    // wrappers apart: each carries its own salt.
    assert(h_a != sh_a);

    // The inner type axis survives too.  The inner type here is a
    // wrapper with its own contribution rather than a second
    // primitive: two bare primitives both fall through to the primary
    // template, contribute zero, and alias each other by design, so
    // they could not witness anything.
    constexpr auto h_a_linear_int = cd::row_hash_contribution_v<Refined<pred_a, crucible::safety::Linear<int>>>;
    assert(h_a != h_a_linear_int);

    std::printf("  test_pred_canonical_id_customization: PASSED\n");
}

// These wrappers carry their grade per instance, so two values of one
// wrapper type can differ without the type differing.  Hashing an
// instance folds that runtime grade in on top of the type-level
// contribution.  The type-level hash keeps collapsing on the wrapper
// tag alone: the instance hash is an additional surface, not a
// replacement for it.

static void test_row_hash_with_grade() {
    using crucible::safety::Budgeted;
    using crucible::safety::EpochVersioned;
    using crucible::safety::NumaPlacement;
    using crucible::safety::RecipeSpec;
    using crucible::safety::BitsBudget;
    using crucible::safety::PeakBytes;
    using crucible::algebra::lattices::Epoch;
    using crucible::algebra::lattices::Generation;
    using crucible::algebra::lattices::NumaNodeId;
    using crucible::algebra::lattices::AffinityMask;
    using crucible::algebra::lattices::Tolerance;
    using crucible::algebra::lattices::RecipeFamily;

    Budgeted<int> b0{42, BitsBudget{1000}, PeakBytes{2000}};
    Budgeted<int> b0_dup{42, BitsBudget{1000}, PeakBytes{2000}};
    Budgeted<int> b1{42, BitsBudget{5000}, PeakBytes{2000}};  // bits differ
    Budgeted<int> b2{42, BitsBudget{1000}, PeakBytes{9999}};  // peak differs

    const std::uint64_t hb0 = cd::row_hash_with_grade(b0);
    const std::uint64_t hb0_dup = cd::row_hash_with_grade(b0_dup);
    const std::uint64_t hb1 = cd::row_hash_with_grade(b1);
    const std::uint64_t hb2 = cd::row_hash_with_grade(b2);

    assert(hb0 == hb0_dup);
    assert(hb0 != hb1);
    assert(hb0 != hb2);
    assert(hb1 != hb2);  // the two grade axes stay independent
    assert(hb0 != 0);

    constexpr std::uint64_t type_hash_budgeted = cd::row_hash_contribution_v<Budgeted<int>>;
    assert(type_hash_budgeted != hb0);
    assert(type_hash_budgeted != hb1);

    EpochVersioned<int> e0{17, Epoch{1}, Generation{0}};
    EpochVersioned<int> e0_dup{17, Epoch{1}, Generation{0}};
    EpochVersioned<int> e1{17, Epoch{2}, Generation{0}};  // epoch differs
    EpochVersioned<int> e2{17, Epoch{1}, Generation{42}};  // gen differs

    const std::uint64_t he0 = cd::row_hash_with_grade(e0);
    const std::uint64_t he0_dup = cd::row_hash_with_grade(e0_dup);
    const std::uint64_t he1 = cd::row_hash_with_grade(e1);
    const std::uint64_t he2 = cd::row_hash_with_grade(e2);

    assert(he0 == he0_dup);
    assert(he0 != he1);
    assert(he0 != he2);
    assert(he1 != he2);

    NumaPlacement<int> n0{99, NumaNodeId{0}, AffinityMask::single(0)};
    NumaPlacement<int> n0_dup{99, NumaNodeId{0}, AffinityMask::single(0)};
    NumaPlacement<int> n1{99, NumaNodeId{1}, AffinityMask::single(0)};  // node differs
    NumaPlacement<int> n2{99, NumaNodeId{0}, AffinityMask::single(7)};  // aff differs

    const std::uint64_t hn0 = cd::row_hash_with_grade(n0);
    const std::uint64_t hn0_dup = cd::row_hash_with_grade(n0_dup);
    const std::uint64_t hn1 = cd::row_hash_with_grade(n1);
    const std::uint64_t hn2 = cd::row_hash_with_grade(n2);

    assert(hn0 == hn0_dup);
    assert(hn0 != hn1);
    assert(hn0 != hn2);
    assert(hn1 != hn2);

    RecipeSpec<int> r0{7, Tolerance::ULP_FP32, RecipeFamily::Pairwise};
    RecipeSpec<int> r0_dup{7, Tolerance::ULP_FP32, RecipeFamily::Pairwise};
    RecipeSpec<int> r1{7, Tolerance::BITEXACT, RecipeFamily::Pairwise};  // tol differs
    RecipeSpec<int> r2{7, Tolerance::ULP_FP32, RecipeFamily::Kahan};  // family differs

    const std::uint64_t hr0 = cd::row_hash_with_grade(r0);
    const std::uint64_t hr0_dup = cd::row_hash_with_grade(r0_dup);
    const std::uint64_t hr1 = cd::row_hash_with_grade(r1);
    const std::uint64_t hr2 = cd::row_hash_with_grade(r2);

    assert(hr0 == hr0_dup);
    assert(hr0 != hr1);
    assert(hr0 != hr2);
    assert(hr1 != hr2);

    // The four anchor instances share an inner type and differ only
    // in wrapper and grade.  The wrapper tag is what keeps them
    // apart when two grade payloads happen to look alike.
    assert(hb0 != he0);
    assert(hb0 != hn0);
    assert(hb0 != hr0);
    assert(he0 != hn0);
    assert(he0 != hr0);
    assert(hn0 != hr0);

    std::printf("  test_row_hash_with_grade: PASSED\n");
}

int main() {
    test_runtime_permutation_invariance();
    test_runtime_cardinality_discrimination();
    test_runtime_set_semantic_dedup();
    test_runtime_bare_types_zero();
    test_runtime_empty_row_distinct_from_bare();
    test_runtime_determinism();
    test_runtime_computation_specialization();
    test_runtime_federation_hash_pins();
    test_runtime_wrapper_computation_interleave_order();
    test_pred_canonical_id_customization();
    test_row_hash_with_grade();
    std::printf("test_row_hash_fold: 11 groups, all passed\n");
    return 0;
}
