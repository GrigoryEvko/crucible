#include <crucible/forge/recipes/Network.h>

#include <cassert>
#include <cstdio>
#include <string_view>
#include <type_traits>

namespace net = crucible::forge::recipes;

namespace {

crucible::NumericalRecipe recipe(crucible::ReductionDeterminism det) {
    crucible::NumericalRecipe r{};
    r.out_dtype = crucible::ScalarType::Float;
    r.accum_dtype = crucible::ScalarType::Float;
    r.determinism = det;
    return r;
}

struct OrderedAssociativeRecipe {
    static constexpr crucible::ReductionDeterminism determinism =
        crucible::ReductionDeterminism::ORDERED;
    static constexpr bool associative = true;
    static constexpr bool commutative = true;
    static constexpr bool participant_count_power_of_two = true;
};

struct StrictAssociativeRecipe {
    static constexpr crucible::ReductionDeterminism determinism =
        crucible::ReductionDeterminism::BITEXACT_STRICT;
    static constexpr bool associative = true;
    static constexpr bool commutative = true;
    static constexpr bool participant_count_power_of_two = true;
};

struct OrderedOddParticipantRecipe {
    static constexpr crucible::ReductionDeterminism determinism =
        crucible::ReductionDeterminism::ORDERED;
    static constexpr bool associative = true;
    static constexpr bool commutative = true;
    static constexpr bool participant_count_power_of_two = false;
};

void test_names_and_layout() {
    static_assert(sizeof(net::NetworkChunkCount) == sizeof(std::uint8_t));
    static_assert(sizeof(net::DeclaredNetworkRecipeConstraints)
                  == sizeof(net::NetworkRecipeConstraints));
    static_assert(std::is_trivially_copyable_v<
                  net::NetworkRecipeConstraints>);

    assert(net::network_recipe_error_name(
               net::NetworkRecipeError::InvalidChunkCount)
           == std::string_view{"InvalidChunkCount"});
    assert(net::network_collective_algorithm_name(
               net::NetworkCollectiveAlgorithm::RecursiveHalvingDoubling)
           == std::string_view{"RecursiveHalvingDoubling"});
    assert(net::network_equivalence_class_name(
               net::NetworkEquivalenceClass::ByteIdentical)
           == std::string_view{"ByteIdentical"});

    assert(net::admit_network_chunk_count(1).has_value());
    assert(net::admit_network_chunk_count(
               net::kNetworkRecipeMaxChunks).has_value());
    assert(!net::admit_network_chunk_count(0).has_value());
    assert(!net::admit_network_chunk_count(
               net::kNetworkRecipeMaxChunks + 1u).has_value());
    assert(net::admit_network_participant_count(2).has_value());
    assert(!net::admit_network_participant_count(0).has_value());

    std::printf("  test_names_and_layout: PASSED\n");
}

void test_runtime_constraints_by_tier() {
    auto unordered = net::query_constraints(
        recipe(crucible::ReductionDeterminism::UNORDERED),
        net::NetworkReductionLaws{.associative = true, .commutative = true});
    assert(unordered.value().sharp_eligible);
    assert(unordered.value().lossy_compression_allowed);
    assert(unordered.value().quantization_allowed);
    assert(unordered.value().equivalence
           == net::NetworkEquivalenceClass::ApproximateRelative);

    auto tc = net::query_constraints(
        recipe(crucible::ReductionDeterminism::BITEXACT_TC),
        net::NetworkReductionLaws{.associative = true, .commutative = true});
    assert(tc.value().sharp_eligible);
    assert(!tc.value().lossy_compression_allowed);
    assert(!tc.value().quantization_allowed);
    assert(tc.value().fec_allowed);
    assert(tc.value().equivalence
           == net::NetworkEquivalenceClass::TensorCoreUlp);

    auto strict = net::query_constraints(
        recipe(crucible::ReductionDeterminism::BITEXACT_STRICT),
        net::NetworkReductionLaws{.associative = true, .commutative = true});
    assert(strict.value().requires_sorted_participants);
    assert(!strict.value().sharp_eligible);
    assert(strict.value().max_chunk_count.value() == 1u);
    assert(strict.value().equivalence
           == net::NetworkEquivalenceClass::ByteIdentical);

    auto unknown_laws = net::query_constraints(
        recipe(crucible::ReductionDeterminism::ORDERED));
    assert(!unknown_laws.value().sharp_eligible);

    auto non_commutative = net::query_constraints(
        recipe(crucible::ReductionDeterminism::ORDERED),
        net::NetworkReductionLaws{.associative = true, .commutative = false});
    assert(!non_commutative.value().sharp_eligible);

    std::printf("  test_runtime_constraints_by_tier: PASSED\n");
}

void test_algorithm_admission() {
    auto four = *net::admit_network_participant_count(4);
    auto three = *net::admit_network_participant_count(3);
    auto ordered = net::query_constraints(
        recipe(crucible::ReductionDeterminism::ORDERED),
        net::NetworkReductionLaws{.associative = true, .commutative = true});
    auto strict = net::query_constraints(
        recipe(crucible::ReductionDeterminism::BITEXACT_STRICT),
        net::NetworkReductionLaws{.associative = true, .commutative = true});

    assert(net::algorithm_eligible(
        ordered, net::NetworkCollectiveAlgorithm::Ring, three).has_value());
    assert(net::algorithm_eligible(
        ordered, net::NetworkCollectiveAlgorithm::Sharp, three).has_value());
    assert(net::algorithm_eligible(
        ordered, net::NetworkCollectiveAlgorithm::TopKCompressed,
        three).has_value());
    assert(net::algorithm_eligible(
        ordered, net::NetworkCollectiveAlgorithm::RecursiveHalvingDoubling,
        four).has_value());

    auto rhd_bad = net::algorithm_eligible(
        ordered, net::NetworkCollectiveAlgorithm::RecursiveHalvingDoubling,
        three);
    assert(!rhd_bad.has_value());
    assert(rhd_bad.error()
           == net::NetworkRecipeError::NonPowerOfTwoParticipantSet);

    assert(!net::algorithm_eligible(
        strict, net::NetworkCollectiveAlgorithm::Sharp, four).has_value());
    assert(!net::algorithm_eligible(
        strict, net::NetworkCollectiveAlgorithm::TopKCompressed,
        four).has_value());
    assert(net::algorithm_eligible(
        strict, net::NetworkCollectiveAlgorithm::FecProtected,
        four).has_value());

    std::printf("  test_algorithm_admission: PASSED\n");
}

void test_compile_time_concepts() {
    static_assert(net::NetworkRecipeEligible<
                  OrderedAssociativeRecipe, net::SharpAlgorithm>);
    static_assert(!net::NetworkRecipeEligible<
                  StrictAssociativeRecipe, net::SharpAlgorithm>);
    static_assert(net::NetworkRecipeEligible<
                  StrictAssociativeRecipe, net::DeterministicTreeAlgorithm>);
    static_assert(!net::NetworkRecipeEligible<
                  StrictAssociativeRecipe, net::TopKCompressedAlgorithm>);
    static_assert(!net::NetworkRecipeEligible<
                  OrderedOddParticipantRecipe,
                  net::RecursiveHalvingDoublingAlgorithm>);

    std::printf("  test_compile_time_concepts: PASSED\n");
}

}  // namespace

int main() {
    std::printf("test_network_recipe:\n");
    test_names_and_layout();
    test_runtime_constraints_by_tier();
    test_algorithm_admission();
    test_compile_time_concepts();
    std::printf("test_network_recipe: all PASSED\n");
    return 0;
}
