// ═══════════════════════════════════════════════════════════════════
// test_producer_endpoint — sentinel TU for safety/ProducerEndpoint.h
//
// Same blind-spot rationale as test_unary_transform / test_binary_
// transform: a header shipped with embedded static_asserts is
// unverified under the project warning flags unless a .cpp TU
// includes it.  This sentinel forces ProducerEndpoint.h through the
// test target's full -Werror=shadow / -Werror=conversion /
// -Wanalyzer-* matrix and exercises the runtime smoke test inline
// body.
//
// Coverage extends beyond the header self-test to:
//   * Positive: minimum producer-endpoint shape with synthetic handle.
//   * Positive: distinct payload + region element types (concept
//     admits the syntactic match; value_consistent_v predicate
//     catches the semantic mismatch).
//   * Positive: matching payload + region element types (the §3.4
//     well-formed case).
//   * Negative: arity 0, 1, 3.
//   * Negative: handle by lvalue ref (cannot consume handle).
//   * Negative: handle by const&& (cannot move from const).
//   * Negative: region by lvalue ref (cannot consume region).
//   * Negative: region by const&& (cannot move from const).
//   * Negative: param 0 is not a handle (plain int / pointer / region).
//   * Negative: param 0 is a CONSUMER handle (D05 vs D06 disjoint).
//   * Negative: param 0 is a hybrid handle (D05 explicitly excludes).
//   * Negative: param 1 is not a region.
//   * Negative: non-void return.
//   * Negative: BOTH params being OwnedRegion (BinaryTransform shape).
//   * Cross-shape exclusion: ProducerEndpoint vs UnaryTransform vs
//     BinaryTransform — mutually exclusive on the shape matchers.
//   * Cross-shape exclusion: tag-free fns are not ProducerEndpoint.
//   * Inferred tags: D11 inferred_permission_tags_t harvests the
//     region's tag (handles do NOT contribute to D11's harvest;
//     handles are ProducerHandle-shaped, not OwnedRegion-shaped).
//   * Volatile&& on either parameter — admitted (orthogonal axis).
// ═══════════════════════════════════════════════════════════════════

#include <crucible/safety/ProducerEndpoint.h>

#include <crucible/safety/BinaryTransform.h>
#include <crucible/safety/InferredPermissionTags.h>
#include <crucible/safety/OwnedRegion.h>
#include <crucible/safety/UnaryTransform.h>

#include <cstdio>
#include <cstdlib>
#include <type_traits>

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

namespace extract = ::crucible::safety::extract;
namespace safety  = ::crucible::safety;

struct in_tag  {};
struct out_tag {};

}  // namespace

// ── Synthetic handle witnesses ────────────────────────────────────
//
// These mirror the synthetic handles in is_producer_handle_self_test
// — exposing exactly the shape D05 recognizes — but in a separate
// namespace so the ProducerEndpoint test isn't coupled to the D05
// header's private testing types.

namespace pe_test {

// Producer-handle witness — int payload.
struct producer_handle_int {
    [[nodiscard]] bool try_push(int const&) noexcept { return true; }
};

// Producer-handle witness — float payload.
struct producer_handle_float {
    [[nodiscard]] bool try_push(float const&) noexcept { return true; }
};

// Consumer-handle witness — try_pop only, no try_push.
struct consumer_handle_int {
    [[nodiscard]] int try_pop() noexcept { return 0; }
};

// Hybrid — both try_push AND try_pop, rejected by D05.
struct hybrid_handle {
    [[nodiscard]] bool try_push(int const&) noexcept { return true; }
    [[nodiscard]] int  try_pop() noexcept { return 0; }
};

// Producer-shaped with NON-noexcept try_push.  D05 admits — its
// signature decomp has two specialisations, one for `noexcept` and
// one without.  D15 must therefore admit too.
struct producer_handle_int_no_noexcept {
    [[nodiscard]] bool try_push(int const&) { return true; }
};

// Producer-shaped with const-qualified try_push.  D05 REJECTS —
// the signature decomp specialisations match unqualified member-
// function-pointer types only.  Reject propagates to D15.
struct producer_handle_const_push {
    [[nodiscard]] bool try_push(int const&) const noexcept { return true; }
};

// Producer-shaped with rvalue-ref-qualified try_push.  D05 REJECTS.
struct producer_handle_rref_push {
    [[nodiscard]] bool try_push(int const&) && noexcept { return true; }
};

using OR_int_in   = ::crucible::safety::OwnedRegion<int,    ::in_tag>;
using OR_float_in = ::crucible::safety::OwnedRegion<float,  ::in_tag>;
using OR_int_out  = ::crucible::safety::OwnedRegion<int,    ::out_tag>;

// ── Positive shapes ─────────────────────────────────────────────

// Canonical §3.4 well-formed: producer payload type matches region
// element type; both rvalue-ref'd; void return.
void f_well_formed(producer_handle_int&&, OR_int_in&&) noexcept;

// Concept-admits, value-mismatch: producer takes int, region carries
// float.  Concept says yes (syntactic shape OK); the consistency
// predicate says no (semantic mismatch).
void f_value_mismatch(producer_handle_int&&, OR_float_in&&) noexcept;

// Different-element-type variant: float producer + float region —
// well-formed but with a different element type than f_well_formed.
void f_well_formed_float(producer_handle_float&&, OR_float_in&&) noexcept;

// Volatile&& on handle — admitted (volatile orthogonal to ownership).
void f_handle_volatile(producer_handle_int volatile&&, OR_int_in&&) noexcept;

// Volatile&& on region — admitted.
void f_region_volatile(producer_handle_int&&, OR_int_in volatile&&) noexcept;

// Volatile&& on both — still admitted.
void f_both_volatile(producer_handle_int volatile&&,
                     OR_int_in volatile&&) noexcept;

// ── Negative shapes ─────────────────────────────────────────────

// Arity wrong.
void f_no_param() noexcept;
void f_one_param(producer_handle_int&&) noexcept;
void f_three_params(producer_handle_int&&, OR_int_in&&, int) noexcept;

// Handle by lvalue ref — cannot consume handle by-value.
void f_handle_lvalue_ref(producer_handle_int&, OR_int_in&&) noexcept;

// Handle by const&& — cannot move from const.
void f_handle_const_rvalue_ref(producer_handle_int const&&,
                               OR_int_in&&) noexcept;

// Region by lvalue ref — cannot consume region by-borrow.
void f_region_lvalue_ref(producer_handle_int&&, OR_int_in&) noexcept;

// Region by const&& — cannot move from const.
void f_region_const_rvalue_ref(producer_handle_int&&,
                               OR_int_in const&&) noexcept;

// Param 0 is not a handle — plain int.
void f_int_in_handle_slot(int, OR_int_in&&) noexcept;

// Param 0 is not a handle — pointer to a handle.
void f_ptr_to_handle(producer_handle_int*, OR_int_in&&) noexcept;

// Param 0 is a CONSUMER handle (try_pop only) — should be rejected
// because is_producer_handle_v requires try_push.
void f_consumer_in_producer_slot(consumer_handle_int&&,
                                 OR_int_in&&) noexcept;

// Param 0 is a hybrid handle — D05 rejects, so D15 also rejects.
void f_hybrid_in_producer_slot(hybrid_handle&&, OR_int_in&&) noexcept;

// Param 1 is not a region — plain int.
void f_int_in_region_slot(producer_handle_int&&, int) noexcept;

// Non-void return — fails the void-return clause.
int f_int_return(producer_handle_int&&, OR_int_in&&) noexcept;

// Non-void OwnedRegion return — even though §3.1 UnaryTransform
// admits this, ProducerEndpoint requires void return per §3.4.
OR_int_out f_region_return(producer_handle_int&&, OR_int_in&&) noexcept;

// Both params are OwnedRegion — this is a BinaryTransform shape,
// NOT a ProducerEndpoint.  Mutual exclusion check.
void f_two_regions(OR_int_in&&, OR_int_in&&) noexcept;

// Non-noexcept producer handle — D05 admits via the second decomp
// specialisation; D15 must therefore admit too.
void f_no_noexcept_producer(producer_handle_int_no_noexcept&&,
                            OR_int_in&&) noexcept;

// Const-qualified try_push on producer — D05 rejects.
void f_const_push_in_producer_slot(producer_handle_const_push&&,
                                   OR_int_in&&) noexcept;

// Rvalue-ref-qualified try_push on producer — D05 rejects.
void f_rref_push_in_producer_slot(producer_handle_rref_push&&,
                                  OR_int_in&&) noexcept;

}  // namespace pe_test

namespace {

// ── Tests ────────────────────────────────────────────────────────

void test_runtime_smoke() {
    EXPECT_TRUE(extract::producer_endpoint_smoke_test());
}

void test_positive_well_formed() {
    static_assert( extract::ProducerEndpoint<&pe_test::f_well_formed>);
    static_assert( extract::is_producer_endpoint_v<&pe_test::f_well_formed>);
    static_assert( extract::ProducerEndpoint<&pe_test::f_well_formed_float>);
}

void test_positive_value_mismatch_admitted_by_concept() {
    // The concept admits any (handle, region) with the right shape,
    // even when handle's payload type and region's element type
    // disagree.  Reasoning: dispatchers want the SHAPE to match so
    // they can emit a more specific diagnostic (via
    // value_consistent_v) than "shape didn't match."
    static_assert( extract::ProducerEndpoint<&pe_test::f_value_mismatch>);
    static_assert( extract::is_producer_endpoint_v<&pe_test::f_value_mismatch>);
}

void test_negative_arity_mismatch() {
    static_assert(!extract::ProducerEndpoint<&pe_test::f_no_param>);
    static_assert(!extract::ProducerEndpoint<&pe_test::f_one_param>);
    static_assert(!extract::ProducerEndpoint<&pe_test::f_three_params>);
}

void test_negative_handle_not_rvalue_ref() {
    static_assert(!extract::ProducerEndpoint<&pe_test::f_handle_lvalue_ref>);
}

void test_negative_handle_const_rvalue_ref() {
    static_assert(!extract::ProducerEndpoint<
        &pe_test::f_handle_const_rvalue_ref>);
}

void test_negative_region_not_rvalue_ref() {
    static_assert(!extract::ProducerEndpoint<&pe_test::f_region_lvalue_ref>);
}

void test_negative_region_const_rvalue_ref() {
    static_assert(!extract::ProducerEndpoint<
        &pe_test::f_region_const_rvalue_ref>);
}

void test_negative_handle_slot_not_handle() {
    static_assert(!extract::ProducerEndpoint<&pe_test::f_int_in_handle_slot>);
    static_assert(!extract::ProducerEndpoint<&pe_test::f_ptr_to_handle>);
}

void test_negative_consumer_in_producer_slot() {
    static_assert(!extract::ProducerEndpoint<
        &pe_test::f_consumer_in_producer_slot>);
}

void test_negative_hybrid_in_producer_slot() {
    static_assert(!extract::ProducerEndpoint<
        &pe_test::f_hybrid_in_producer_slot>);
}

void test_negative_region_slot_not_region() {
    static_assert(!extract::ProducerEndpoint<&pe_test::f_int_in_region_slot>);
}

void test_negative_non_void_return() {
    static_assert(!extract::ProducerEndpoint<&pe_test::f_int_return>);
    static_assert(!extract::ProducerEndpoint<&pe_test::f_region_return>);
}

void test_negative_two_regions_is_binary() {
    // f_two_regions has shape (OR&&, OR&&) — that's a BinaryTransform,
    // not a ProducerEndpoint.  Confirm the exclusion.
    static_assert(!extract::ProducerEndpoint<&pe_test::f_two_regions>);
    static_assert( extract::BinaryTransform<&pe_test::f_two_regions>);
}

void test_positive_non_noexcept_try_push() {
    // D05 has TWO signature_decomp specialisations:
    //   bool(C::*)(P const&)           [non-noexcept]
    //   bool(C::*)(P const&) noexcept  [noexcept]
    // Both must be admitted by D15.  This test exercises the
    // non-noexcept branch — the noexcept branch is exercised by
    // every other f_well_formed-style test.
    static_assert( extract::ProducerEndpoint<
        &pe_test::f_no_noexcept_producer>);
    static_assert(std::is_same_v<
        extract::producer_endpoint_handle_value_t<
            &pe_test::f_no_noexcept_producer>,
        int>);
}

void test_negative_cv_qualified_try_push() {
    // D05's signature_decomp matches member-function-pointer types
    // WITHOUT cv-ref qualifiers on the method.  A const-qualified
    // try_push produces `bool (C::*)(P const&) const [noexcept]`
    // which does NOT match either decomp specialisation, so D05
    // rejects.  D15 inherits the rejection.
    static_assert(!extract::ProducerEndpoint<
        &pe_test::f_const_push_in_producer_slot>);

    // Same for rvalue-ref-qualified try_push — `bool (C::*)(P
    // const&) && [noexcept]`.
    static_assert(!extract::ProducerEndpoint<
        &pe_test::f_rref_push_in_producer_slot>);
}

void test_volatile_admitted_on_either_or_both() {
    static_assert(extract::ProducerEndpoint<&pe_test::f_handle_volatile>);
    static_assert(extract::ProducerEndpoint<&pe_test::f_region_volatile>);
    static_assert(extract::ProducerEndpoint<&pe_test::f_both_volatile>);
}

void test_handle_value_extraction() {
    static_assert(std::is_same_v<
        extract::producer_endpoint_handle_value_t<&pe_test::f_well_formed>,
        int>);
    static_assert(std::is_same_v<
        extract::producer_endpoint_handle_value_t<
            &pe_test::f_well_formed_float>,
        float>);
    // Value-mismatch: handle is int; region is float.
    static_assert(std::is_same_v<
        extract::producer_endpoint_handle_value_t<&pe_test::f_value_mismatch>,
        int>);
}

void test_region_tag_extraction() {
    static_assert(std::is_same_v<
        extract::producer_endpoint_region_tag_t<&pe_test::f_well_formed>,
        in_tag>);
    static_assert(std::is_same_v<
        extract::producer_endpoint_region_tag_t<&pe_test::f_value_mismatch>,
        in_tag>);
    static_assert(std::is_same_v<
        extract::producer_endpoint_region_tag_t<&pe_test::f_well_formed_float>,
        in_tag>);
}

void test_region_value_extraction() {
    static_assert(std::is_same_v<
        extract::producer_endpoint_region_value_t<&pe_test::f_well_formed>,
        int>);
    static_assert(std::is_same_v<
        extract::producer_endpoint_region_value_t<
            &pe_test::f_well_formed_float>,
        float>);
    // Value-mismatch case: region is float despite handle being int.
    static_assert(std::is_same_v<
        extract::producer_endpoint_region_value_t<&pe_test::f_value_mismatch>,
        float>);
}

void test_value_consistency_predicate() {
    // Well-formed: handle int + region int → consistent.
    static_assert(extract::producer_endpoint_value_consistent_v<
        &pe_test::f_well_formed>);
    static_assert(extract::producer_endpoint_value_consistent_v<
        &pe_test::f_well_formed_float>);

    // Mismatch: handle int + region float → not consistent.
    static_assert(!extract::producer_endpoint_value_consistent_v<
        &pe_test::f_value_mismatch>);
}

void test_concept_form_in_constraints() {
    // Verify the concept form composes correctly with `requires`-
    // clauses — the dispatcher's primary use case.
    auto callable_with_endpoint = []<auto FnPtr>()
        requires extract::ProducerEndpoint<FnPtr>
    {
        return true;
    };

    EXPECT_TRUE(callable_with_endpoint.template operator()<
        &pe_test::f_well_formed>());
    EXPECT_TRUE(callable_with_endpoint.template operator()<
        &pe_test::f_well_formed_float>());
    EXPECT_TRUE(callable_with_endpoint.template operator()<
        &pe_test::f_value_mismatch>());
}

void test_cross_shape_exclusion_with_unary_and_binary() {
    // Mutual exclusion: ProducerEndpoint vs UnaryTransform vs
    // BinaryTransform.  Each canonical shape is structurally
    // disjoint from the others.

    // f_well_formed is ProducerEndpoint (handle + region).
    static_assert( extract::ProducerEndpoint<&pe_test::f_well_formed>);
    static_assert(!extract::UnaryTransform<&pe_test::f_well_formed>);
    static_assert(!extract::BinaryTransform<&pe_test::f_well_formed>);

    // f_two_regions is BinaryTransform (region + region).
    static_assert(!extract::ProducerEndpoint<&pe_test::f_two_regions>);
    static_assert(!extract::UnaryTransform<&pe_test::f_two_regions>);
    static_assert( extract::BinaryTransform<&pe_test::f_two_regions>);

    // f_handle_lvalue_ref fails ProducerEndpoint (handle is not
    // rvalue-ref) AND BinaryTransform (param 0 is not OwnedRegion).
    static_assert(!extract::ProducerEndpoint<&pe_test::f_handle_lvalue_ref>);
    static_assert(!extract::BinaryTransform<&pe_test::f_handle_lvalue_ref>);
    static_assert(!extract::UnaryTransform<&pe_test::f_handle_lvalue_ref>);
}

void test_inferred_tags_harvests_region_tag_only() {
    // D11 inferred_permission_tags_t harvests OwnedRegion tags.
    // Producer/consumer handles are NOT OwnedRegion-shaped, so they
    // do NOT contribute to the harvested set.  The dispatcher's
    // permission tree for a ProducerEndpoint shape contains exactly
    // ONE permission tag — the input region's Tag.
    namespace proto = ::crucible::safety::proto;

    using Expected = proto::PermSet<in_tag>;
    static_assert(proto::perm_set_equal_v<
        extract::inferred_permission_tags_t<&pe_test::f_well_formed>,
        Expected>);

    static_assert(extract::inferred_permission_tags_count_v<
        &pe_test::f_well_formed> == 1);
    static_assert(extract::function_has_tag_v<
        &pe_test::f_well_formed, in_tag>);
    // Out-tag is not part of this signature.
    static_assert(!extract::function_has_tag_v<
        &pe_test::f_well_formed, out_tag>);
}

void test_runtime_consistency() {
    volatile std::size_t const cap = 50;
    bool baseline_pos =
        extract::is_producer_endpoint_v<&pe_test::f_well_formed>;
    bool baseline_neg =
        !extract::is_producer_endpoint_v<&pe_test::f_one_param>;
    EXPECT_TRUE(baseline_pos);
    EXPECT_TRUE(baseline_neg);
    for (std::size_t i = 0; i < cap; ++i) {
        EXPECT_TRUE(baseline_pos
            == extract::is_producer_endpoint_v<&pe_test::f_well_formed>);
        EXPECT_TRUE(baseline_neg
            == !extract::is_producer_endpoint_v<&pe_test::f_one_param>);
        EXPECT_TRUE(extract::ProducerEndpoint<&pe_test::f_well_formed>);
        EXPECT_TRUE(!extract::ProducerEndpoint<&pe_test::f_no_param>);
    }
}

}  // namespace

int main() {
    std::fprintf(stderr, "test_producer_endpoint:\n");
    run_test("test_runtime_smoke",
             test_runtime_smoke);
    run_test("test_positive_well_formed",
             test_positive_well_formed);
    run_test("test_positive_value_mismatch_admitted_by_concept",
             test_positive_value_mismatch_admitted_by_concept);
    run_test("test_negative_arity_mismatch",
             test_negative_arity_mismatch);
    run_test("test_negative_handle_not_rvalue_ref",
             test_negative_handle_not_rvalue_ref);
    run_test("test_negative_handle_const_rvalue_ref",
             test_negative_handle_const_rvalue_ref);
    run_test("test_negative_region_not_rvalue_ref",
             test_negative_region_not_rvalue_ref);
    run_test("test_negative_region_const_rvalue_ref",
             test_negative_region_const_rvalue_ref);
    run_test("test_negative_handle_slot_not_handle",
             test_negative_handle_slot_not_handle);
    run_test("test_negative_consumer_in_producer_slot",
             test_negative_consumer_in_producer_slot);
    run_test("test_negative_hybrid_in_producer_slot",
             test_negative_hybrid_in_producer_slot);
    run_test("test_negative_region_slot_not_region",
             test_negative_region_slot_not_region);
    run_test("test_negative_non_void_return",
             test_negative_non_void_return);
    run_test("test_negative_two_regions_is_binary",
             test_negative_two_regions_is_binary);
    run_test("test_positive_non_noexcept_try_push",
             test_positive_non_noexcept_try_push);
    run_test("test_negative_cv_qualified_try_push",
             test_negative_cv_qualified_try_push);
    run_test("test_volatile_admitted_on_either_or_both",
             test_volatile_admitted_on_either_or_both);
    run_test("test_handle_value_extraction",
             test_handle_value_extraction);
    run_test("test_region_tag_extraction",
             test_region_tag_extraction);
    run_test("test_region_value_extraction",
             test_region_value_extraction);
    run_test("test_value_consistency_predicate",
             test_value_consistency_predicate);
    run_test("test_concept_form_in_constraints",
             test_concept_form_in_constraints);
    run_test("test_cross_shape_exclusion_with_unary_and_binary",
             test_cross_shape_exclusion_with_unary_and_binary);
    run_test("test_inferred_tags_harvests_region_tag_only",
             test_inferred_tags_harvests_region_tag_only);
    run_test("test_runtime_consistency",
             test_runtime_consistency);
    std::fprintf(stderr, "\n%d passed, %d failed\n",
                 total_passed, total_failed);
    if (total_failed > 0) return EXIT_FAILURE;
    std::fprintf(stderr, "ALL PASSED\n");
    return EXIT_SUCCESS;
}
