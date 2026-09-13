#include <crucible/safety/CanonicalShape.h>

#include <crucible/safety/InferredPermissionTags.h>
#include <crucible/safety/OwnedRegion.h>
#include <crucible/safety/reduce_into.h>

#include <cstdio>
#include <cstdlib>
#include <optional>
#include <string_view>
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

#define EXPECT_TRUE(cond)                                                                            \
    do {                                                                                             \
        if (!(cond)) {                                                                               \
            std::fprintf(stderr, "    EXPECT_TRUE failed: %s (%s:%d)\n", #cond, __FILE__, __LINE__); \
            throw TestFailure{};                                                                     \
        }                                                                                            \
    } while (0)

namespace extract = ::crucible::safety::extract;
namespace safety = ::crucible::safety;

struct in_tag {};
struct out_tag {};

}  // namespace

namespace cs_test {

struct producer_int {
    [[nodiscard]] bool try_push(int const&) noexcept { return true; }
};
struct consumer_int {
    [[nodiscard]] std::optional<int> try_pop() noexcept { return 0; }
};
struct swmr_writer_int {
    void publish(int const&) noexcept {}
};
struct swmr_reader_int {
    int load() const noexcept { return 0; }
};
// Stateless, because that is what the reduction predicate asks for.
struct reducer_int_plus {
    constexpr int operator()(int const& a, int const& b) const noexcept { return a + b; }
};

using OR_in = ::crucible::safety::OwnedRegion<int, ::in_tag>;
using OR_out = ::crucible::safety::OwnedRegion<int, ::out_tag>;
using RI_int = ::crucible::safety::reduce_into<int, reducer_int_plus>;

// One worked example per shape, none of them ever called.

OR_out f_unary(OR_in&&) noexcept;

void f_binary(OR_in&&, OR_out&&) noexcept;

void f_reduction(OR_in&&, RI_int&) noexcept;

void f_producer(producer_int&&, OR_in&&) noexcept;

void f_consumer(consumer_int&&, OR_out&&) noexcept;

void f_swmr_writer(swmr_writer_int&&, int) noexcept;

int f_swmr_reader(swmr_reader_int&&) noexcept;

void f_pipeline(consumer_int&&, producer_int&&) noexcept;

void f_non_canonical_two_ints(int, int) noexcept;
int f_non_canonical_three_params(int, int, int) noexcept;
void f_non_canonical_no_param() noexcept;

}  // namespace cs_test

namespace {

void test_runtime_smoke() { EXPECT_TRUE(extract::canonical_shape_smoke_test()); }

void test_unary_transform_recognized() {
    static_assert(extract::CanonicalShape<&cs_test::f_unary>);
    static_assert(!extract::NonCanonical<&cs_test::f_unary>);
    static_assert(extract::canonical_shape_kind_v<&cs_test::f_unary> == extract::CanonicalShapeKind::UnaryTransform);
    static_assert(extract::canonical_shape_name_of_v<&cs_test::f_unary> == std::string_view{"UnaryTransform"});
}

void test_binary_transform_recognized() {
    static_assert(extract::CanonicalShape<&cs_test::f_binary>);
    static_assert(extract::canonical_shape_kind_v<&cs_test::f_binary> == extract::CanonicalShapeKind::BinaryTransform);
    static_assert(extract::canonical_shape_name_of_v<&cs_test::f_binary> == std::string_view{"BinaryTransform"});
}

void test_reduction_recognized() {
    static_assert(extract::CanonicalShape<&cs_test::f_reduction>);
    static_assert(!extract::NonCanonical<&cs_test::f_reduction>);
    static_assert(extract::canonical_shape_kind_v<&cs_test::f_reduction> == extract::CanonicalShapeKind::Reduction);
    static_assert(extract::canonical_shape_name_of_v<&cs_test::f_reduction> == std::string_view{"Reduction"});
}

void test_producer_endpoint_recognized() {
    static_assert(extract::CanonicalShape<&cs_test::f_producer>);
    static_assert(extract::canonical_shape_kind_v<&cs_test::f_producer>
                  == extract::CanonicalShapeKind::ProducerEndpoint);
    static_assert(extract::canonical_shape_name_of_v<&cs_test::f_producer> == std::string_view{"ProducerEndpoint"});
}

void test_consumer_endpoint_recognized() {
    static_assert(extract::CanonicalShape<&cs_test::f_consumer>);
    static_assert(extract::canonical_shape_kind_v<&cs_test::f_consumer>
                  == extract::CanonicalShapeKind::ConsumerEndpoint);
    static_assert(extract::canonical_shape_name_of_v<&cs_test::f_consumer> == std::string_view{"ConsumerEndpoint"});
}

void test_swmr_writer_recognized() {
    static_assert(extract::CanonicalShape<&cs_test::f_swmr_writer>);
    static_assert(extract::canonical_shape_kind_v<&cs_test::f_swmr_writer> == extract::CanonicalShapeKind::SwmrWriter);
    static_assert(extract::canonical_shape_name_of_v<&cs_test::f_swmr_writer> == std::string_view{"SwmrWriter"});
}

void test_swmr_reader_recognized() {
    static_assert(extract::CanonicalShape<&cs_test::f_swmr_reader>);
    static_assert(extract::canonical_shape_kind_v<&cs_test::f_swmr_reader> == extract::CanonicalShapeKind::SwmrReader);
    static_assert(extract::canonical_shape_name_of_v<&cs_test::f_swmr_reader> == std::string_view{"SwmrReader"});
}

void test_pipeline_stage_recognized() {
    static_assert(extract::CanonicalShape<&cs_test::f_pipeline>);
    static_assert(extract::canonical_shape_kind_v<&cs_test::f_pipeline> == extract::CanonicalShapeKind::PipelineStage);
    static_assert(extract::canonical_shape_name_of_v<&cs_test::f_pipeline> == std::string_view{"PipelineStage"});
}

void test_non_canonical_signatures() {
    static_assert(!extract::CanonicalShape<&cs_test::f_non_canonical_two_ints>);
    static_assert(extract::NonCanonical<&cs_test::f_non_canonical_two_ints>);
    static_assert(extract::canonical_shape_kind_v<&cs_test::f_non_canonical_two_ints>
                  == extract::CanonicalShapeKind::NonCanonical);
    static_assert(extract::canonical_shape_name_of_v<&cs_test::f_non_canonical_two_ints>
                  == std::string_view{"NonCanonical"});

    static_assert(extract::NonCanonical<&cs_test::f_non_canonical_three_params>);
    static_assert(extract::NonCanonical<&cs_test::f_non_canonical_no_param>);
}

void test_mutual_exclusivity_unary() {
    // Each of these eight tests takes one worked example and asserts
    // that its own predicate holds and the other seven do not.
    static_assert(extract::UnaryTransform<&cs_test::f_unary>);
    static_assert(!extract::BinaryTransform<&cs_test::f_unary>);
    static_assert(!extract::Reduction<&cs_test::f_unary>);
    static_assert(!extract::ProducerEndpoint<&cs_test::f_unary>);
    static_assert(!extract::ConsumerEndpoint<&cs_test::f_unary>);
    static_assert(!extract::SwmrWriter<&cs_test::f_unary>);
    static_assert(!extract::SwmrReader<&cs_test::f_unary>);
    static_assert(!extract::PipelineStage<&cs_test::f_unary>);
}

void test_mutual_exclusivity_binary() {
    static_assert(!extract::UnaryTransform<&cs_test::f_binary>);
    static_assert(extract::BinaryTransform<&cs_test::f_binary>);
    static_assert(!extract::Reduction<&cs_test::f_binary>);
    static_assert(!extract::ProducerEndpoint<&cs_test::f_binary>);
    static_assert(!extract::ConsumerEndpoint<&cs_test::f_binary>);
    static_assert(!extract::SwmrWriter<&cs_test::f_binary>);
    static_assert(!extract::SwmrReader<&cs_test::f_binary>);
    static_assert(!extract::PipelineStage<&cs_test::f_binary>);
}

void test_mutual_exclusivity_reduction() {
    // This one is the close call.  A reduction and a binary transform
    // both take two parameters with an owned region first.  What
    // separates them is the second parameter: which wrapper it is, and
    // whether it arrives by lvalue or rvalue reference.
    static_assert(!extract::UnaryTransform<&cs_test::f_reduction>);
    static_assert(!extract::BinaryTransform<&cs_test::f_reduction>);
    static_assert(extract::Reduction<&cs_test::f_reduction>);
    static_assert(!extract::ProducerEndpoint<&cs_test::f_reduction>);
    static_assert(!extract::ConsumerEndpoint<&cs_test::f_reduction>);
    static_assert(!extract::SwmrWriter<&cs_test::f_reduction>);
    static_assert(!extract::SwmrReader<&cs_test::f_reduction>);
    static_assert(!extract::PipelineStage<&cs_test::f_reduction>);
}

void test_mutual_exclusivity_producer() {
    static_assert(!extract::UnaryTransform<&cs_test::f_producer>);
    static_assert(!extract::BinaryTransform<&cs_test::f_producer>);
    static_assert(!extract::Reduction<&cs_test::f_producer>);
    static_assert(extract::ProducerEndpoint<&cs_test::f_producer>);
    static_assert(!extract::ConsumerEndpoint<&cs_test::f_producer>);
    static_assert(!extract::SwmrWriter<&cs_test::f_producer>);
    static_assert(!extract::SwmrReader<&cs_test::f_producer>);
    static_assert(!extract::PipelineStage<&cs_test::f_producer>);
}

void test_mutual_exclusivity_consumer() {
    static_assert(!extract::UnaryTransform<&cs_test::f_consumer>);
    static_assert(!extract::BinaryTransform<&cs_test::f_consumer>);
    static_assert(!extract::Reduction<&cs_test::f_consumer>);
    static_assert(!extract::ProducerEndpoint<&cs_test::f_consumer>);
    static_assert(extract::ConsumerEndpoint<&cs_test::f_consumer>);
    static_assert(!extract::SwmrWriter<&cs_test::f_consumer>);
    static_assert(!extract::SwmrReader<&cs_test::f_consumer>);
    static_assert(!extract::PipelineStage<&cs_test::f_consumer>);
}

void test_mutual_exclusivity_swmr_writer() {
    static_assert(!extract::UnaryTransform<&cs_test::f_swmr_writer>);
    static_assert(!extract::BinaryTransform<&cs_test::f_swmr_writer>);
    static_assert(!extract::Reduction<&cs_test::f_swmr_writer>);
    static_assert(!extract::ProducerEndpoint<&cs_test::f_swmr_writer>);
    static_assert(!extract::ConsumerEndpoint<&cs_test::f_swmr_writer>);
    static_assert(extract::SwmrWriter<&cs_test::f_swmr_writer>);
    static_assert(!extract::SwmrReader<&cs_test::f_swmr_writer>);
    static_assert(!extract::PipelineStage<&cs_test::f_swmr_writer>);
}

void test_mutual_exclusivity_swmr_reader() {
    static_assert(!extract::UnaryTransform<&cs_test::f_swmr_reader>);
    static_assert(!extract::BinaryTransform<&cs_test::f_swmr_reader>);
    static_assert(!extract::Reduction<&cs_test::f_swmr_reader>);
    static_assert(!extract::ProducerEndpoint<&cs_test::f_swmr_reader>);
    static_assert(!extract::ConsumerEndpoint<&cs_test::f_swmr_reader>);
    static_assert(!extract::SwmrWriter<&cs_test::f_swmr_reader>);
    static_assert(extract::SwmrReader<&cs_test::f_swmr_reader>);
    static_assert(!extract::PipelineStage<&cs_test::f_swmr_reader>);
}

void test_mutual_exclusivity_pipeline() {
    static_assert(!extract::UnaryTransform<&cs_test::f_pipeline>);
    static_assert(!extract::BinaryTransform<&cs_test::f_pipeline>);
    static_assert(!extract::Reduction<&cs_test::f_pipeline>);
    static_assert(!extract::ProducerEndpoint<&cs_test::f_pipeline>);
    static_assert(!extract::ConsumerEndpoint<&cs_test::f_pipeline>);
    static_assert(!extract::SwmrWriter<&cs_test::f_pipeline>);
    static_assert(!extract::SwmrReader<&cs_test::f_pipeline>);
    static_assert(extract::PipelineStage<&cs_test::f_pipeline>);
}

void test_canonical_xor_non_canonical() {
    // One of the two holds for any function, which follows from the
    // definition of the second as the negation of the first.  The
    // assertions are therefore tautologies today, and stop being
    // tautologies the moment someone gives the negation a body of its
    // own.

    static_assert(extract::CanonicalShape<&cs_test::f_unary> != extract::NonCanonical<&cs_test::f_unary>);
    static_assert(extract::CanonicalShape<&cs_test::f_pipeline> != extract::NonCanonical<&cs_test::f_pipeline>);
    static_assert(extract::CanonicalShape<&cs_test::f_non_canonical_two_ints>
                  != extract::NonCanonical<&cs_test::f_non_canonical_two_ints>);
}

void test_non_canonical_zero_default() {
    // The non-canonical kind is the zero value, which matters twice
    // over.  Zero-initialised storage then reads as "no recognised
    // shape" rather than as a shape it never had, and a catch-all
    // branch can test the kind against zero without naming it.
    // Reordering the enum would take both away.
    static_assert(static_cast<std::uint8_t>(extract::CanonicalShapeKind::NonCanonical) == 0u);

    extract::CanonicalShapeKind k{};
    EXPECT_TRUE(k == extract::CanonicalShapeKind::NonCanonical);
}

void test_name_of_v_matches_name_of_kind_v() {
    // The name can be reached directly from the function or by way of
    // its kind.  Both routes must arrive at the same string, or a
    // diagnostic will one day contradict itself.

    static_assert(extract::canonical_shape_name_of_v<&cs_test::f_unary>
                  == extract::canonical_shape_name(extract::canonical_shape_kind_v<&cs_test::f_unary>));
    static_assert(extract::canonical_shape_name_of_v<&cs_test::f_binary>
                  == extract::canonical_shape_name(extract::canonical_shape_kind_v<&cs_test::f_binary>));
    static_assert(extract::canonical_shape_name_of_v<&cs_test::f_reduction>
                  == extract::canonical_shape_name(extract::canonical_shape_kind_v<&cs_test::f_reduction>));
    static_assert(extract::canonical_shape_name_of_v<&cs_test::f_producer>
                  == extract::canonical_shape_name(extract::canonical_shape_kind_v<&cs_test::f_producer>));
    static_assert(extract::canonical_shape_name_of_v<&cs_test::f_consumer>
                  == extract::canonical_shape_name(extract::canonical_shape_kind_v<&cs_test::f_consumer>));
    static_assert(extract::canonical_shape_name_of_v<&cs_test::f_swmr_writer>
                  == extract::canonical_shape_name(extract::canonical_shape_kind_v<&cs_test::f_swmr_writer>));
    static_assert(extract::canonical_shape_name_of_v<&cs_test::f_swmr_reader>
                  == extract::canonical_shape_name(extract::canonical_shape_kind_v<&cs_test::f_swmr_reader>));
    static_assert(extract::canonical_shape_name_of_v<&cs_test::f_pipeline>
                  == extract::canonical_shape_name(extract::canonical_shape_kind_v<&cs_test::f_pipeline>));
    static_assert(
        extract::canonical_shape_name_of_v<&cs_test::f_non_canonical_two_ints>
        == extract::canonical_shape_name(extract::canonical_shape_kind_v<&cs_test::f_non_canonical_two_ints>));
}

void test_non_canonical_fails_every_shape_predicate() {
    // Asking each predicate separately is redundant while the
    // umbrella is the disjunction of the eight.  It stops being
    // redundant if one of the eight starts matching a signature it
    // should not.

    static_assert(!extract::UnaryTransform<&cs_test::f_non_canonical_two_ints>);
    static_assert(!extract::BinaryTransform<&cs_test::f_non_canonical_two_ints>);
    static_assert(!extract::Reduction<&cs_test::f_non_canonical_two_ints>);
    static_assert(!extract::ProducerEndpoint<&cs_test::f_non_canonical_two_ints>);
    static_assert(!extract::ConsumerEndpoint<&cs_test::f_non_canonical_two_ints>);
    static_assert(!extract::SwmrWriter<&cs_test::f_non_canonical_two_ints>);
    static_assert(!extract::SwmrReader<&cs_test::f_non_canonical_two_ints>);
    static_assert(!extract::PipelineStage<&cs_test::f_non_canonical_two_ints>);

    // No shape takes three parameters, so this one fails on arity
    // alone.
    static_assert(!extract::UnaryTransform<&cs_test::f_non_canonical_three_params>);
    static_assert(!extract::BinaryTransform<&cs_test::f_non_canonical_three_params>);
    static_assert(!extract::Reduction<&cs_test::f_non_canonical_three_params>);
    static_assert(!extract::ProducerEndpoint<&cs_test::f_non_canonical_three_params>);
    static_assert(!extract::ConsumerEndpoint<&cs_test::f_non_canonical_three_params>);
    static_assert(!extract::SwmrWriter<&cs_test::f_non_canonical_three_params>);
    static_assert(!extract::SwmrReader<&cs_test::f_non_canonical_three_params>);
    static_assert(!extract::PipelineStage<&cs_test::f_non_canonical_three_params>);
}

void test_dispatcher_integration_example() {
    // How a dispatcher uses the kind: one compile-time branch per
    // shape, and a catch-all for everything it does not recognise.

    auto select_lowering = []<auto FnPtr>() consteval {
        constexpr auto kind = extract::canonical_shape_kind_v<FnPtr>;
        if constexpr (kind == extract::CanonicalShapeKind::UnaryTransform) {
            return 1;
        } else if constexpr (kind == extract::CanonicalShapeKind::BinaryTransform) {
            return 2;
        } else if constexpr (kind == extract::CanonicalShapeKind::Reduction) {
            return 3;
        } else if constexpr (kind == extract::CanonicalShapeKind::ProducerEndpoint) {
            return 4;
        } else if constexpr (kind == extract::CanonicalShapeKind::ConsumerEndpoint) {
            return 5;
        } else if constexpr (kind == extract::CanonicalShapeKind::SwmrWriter) {
            return 6;
        } else if constexpr (kind == extract::CanonicalShapeKind::SwmrReader) {
            return 7;
        } else if constexpr (kind == extract::CanonicalShapeKind::PipelineStage) {
            return 8;
        } else {
            // A real dispatcher emits a diagnostic here naming the
            // shape it did not recognise.
            return 0;
        }
    };

    static_assert(select_lowering.template operator()<&cs_test::f_unary>() == 1);
    static_assert(select_lowering.template operator()<&cs_test::f_binary>() == 2);
    static_assert(select_lowering.template operator()<&cs_test::f_reduction>() == 3);
    static_assert(select_lowering.template operator()<&cs_test::f_producer>() == 4);
    static_assert(select_lowering.template operator()<&cs_test::f_consumer>() == 5);
    static_assert(select_lowering.template operator()<&cs_test::f_swmr_writer>() == 6);
    static_assert(select_lowering.template operator()<&cs_test::f_swmr_reader>() == 7);
    static_assert(select_lowering.template operator()<&cs_test::f_pipeline>() == 8);
    static_assert(select_lowering.template operator()<&cs_test::f_non_canonical_two_ints>() == 0);
}

void test_kind_to_name_round_trip() {
    static_assert(!extract::canonical_shape_name(extract::CanonicalShapeKind::UnaryTransform).empty());
    static_assert(!extract::canonical_shape_name(extract::CanonicalShapeKind::BinaryTransform).empty());
    static_assert(!extract::canonical_shape_name(extract::CanonicalShapeKind::Reduction).empty());
    static_assert(!extract::canonical_shape_name(extract::CanonicalShapeKind::ProducerEndpoint).empty());
    static_assert(!extract::canonical_shape_name(extract::CanonicalShapeKind::ConsumerEndpoint).empty());
    static_assert(!extract::canonical_shape_name(extract::CanonicalShapeKind::SwmrWriter).empty());
    static_assert(!extract::canonical_shape_name(extract::CanonicalShapeKind::SwmrReader).empty());
    static_assert(!extract::canonical_shape_name(extract::CanonicalShapeKind::PipelineStage).empty());
    static_assert(!extract::canonical_shape_name(extract::CanonicalShapeKind::NonCanonical).empty());

    static_assert(extract::canonical_shape_name(extract::CanonicalShapeKind::UnaryTransform)
                  != extract::canonical_shape_name(extract::CanonicalShapeKind::BinaryTransform));
    static_assert(extract::canonical_shape_name(extract::CanonicalShapeKind::SwmrWriter)
                  != extract::canonical_shape_name(extract::CanonicalShapeKind::SwmrReader));
}

void test_concept_form_in_constraints() {
    // A dispatcher constrains on these, so they have to work as
    // requires-clauses and not only as values.
    auto callable_with_canonical = []<auto FnPtr>()
        requires extract::CanonicalShape<FnPtr>
    { return true; };

    EXPECT_TRUE(callable_with_canonical.template operator()<&cs_test::f_unary>());
    EXPECT_TRUE(callable_with_canonical.template operator()<&cs_test::f_pipeline>());

    auto callable_with_non_canonical = []<auto FnPtr>()
        requires extract::NonCanonical<FnPtr>
    { return true; };

    EXPECT_TRUE(callable_with_non_canonical.template operator()<&cs_test::f_non_canonical_two_ints>());
}

void test_runtime_consistency() {
    volatile std::size_t const cap = 50;
    bool baseline_pos = extract::is_canonical_shape_v<&cs_test::f_unary>;
    bool baseline_neg = extract::is_non_canonical_v<&cs_test::f_non_canonical_two_ints>;
    EXPECT_TRUE(baseline_pos);
    EXPECT_TRUE(baseline_neg);
    for (std::size_t i = 0; i < cap; ++i) {
        EXPECT_TRUE(baseline_pos == extract::is_canonical_shape_v<&cs_test::f_unary>);
        EXPECT_TRUE(baseline_neg == extract::is_non_canonical_v<&cs_test::f_non_canonical_two_ints>);
        EXPECT_TRUE(extract::CanonicalShape<&cs_test::f_pipeline>);
        EXPECT_TRUE(!extract::CanonicalShape<&cs_test::f_non_canonical_no_param>);
    }
}

}  // namespace

int main() {
    std::fprintf(stderr, "test_canonical_shape:\n");
    run_test("test_runtime_smoke", test_runtime_smoke);
    run_test("test_unary_transform_recognized", test_unary_transform_recognized);
    run_test("test_binary_transform_recognized", test_binary_transform_recognized);
    run_test("test_reduction_recognized", test_reduction_recognized);
    run_test("test_producer_endpoint_recognized", test_producer_endpoint_recognized);
    run_test("test_consumer_endpoint_recognized", test_consumer_endpoint_recognized);
    run_test("test_swmr_writer_recognized", test_swmr_writer_recognized);
    run_test("test_swmr_reader_recognized", test_swmr_reader_recognized);
    run_test("test_pipeline_stage_recognized", test_pipeline_stage_recognized);
    run_test("test_non_canonical_signatures", test_non_canonical_signatures);
    run_test("test_mutual_exclusivity_unary", test_mutual_exclusivity_unary);
    run_test("test_mutual_exclusivity_binary", test_mutual_exclusivity_binary);
    run_test("test_mutual_exclusivity_reduction", test_mutual_exclusivity_reduction);
    run_test("test_mutual_exclusivity_producer", test_mutual_exclusivity_producer);
    run_test("test_mutual_exclusivity_consumer", test_mutual_exclusivity_consumer);
    run_test("test_mutual_exclusivity_swmr_writer", test_mutual_exclusivity_swmr_writer);
    run_test("test_mutual_exclusivity_swmr_reader", test_mutual_exclusivity_swmr_reader);
    run_test("test_mutual_exclusivity_pipeline", test_mutual_exclusivity_pipeline);
    run_test("test_canonical_xor_non_canonical", test_canonical_xor_non_canonical);
    run_test("test_non_canonical_zero_default", test_non_canonical_zero_default);
    run_test("test_name_of_v_matches_name_of_kind_v", test_name_of_v_matches_name_of_kind_v);
    run_test("test_non_canonical_fails_every_shape_predicate", test_non_canonical_fails_every_shape_predicate);
    run_test("test_dispatcher_integration_example", test_dispatcher_integration_example);
    run_test("test_kind_to_name_round_trip", test_kind_to_name_round_trip);
    run_test("test_concept_form_in_constraints", test_concept_form_in_constraints);
    run_test("test_runtime_consistency", test_runtime_consistency);
    std::fprintf(stderr, "\n%d passed, %d failed\n", total_passed, total_failed);
    if (total_failed > 0) return EXIT_FAILURE;
    std::fprintf(stderr, "ALL PASSED\n");
    return EXIT_SUCCESS;
}
