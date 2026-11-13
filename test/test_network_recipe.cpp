#include <crucible/forge/recipes/Network.h>

#include "test_assert.h"
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
    static constexpr crucible::ReductionDeterminism determinism = crucible::ReductionDeterminism::ORDERED;
    static constexpr bool associative = true;
    static constexpr bool commutative = true;
    static constexpr bool participant_count_power_of_two = true;
};

struct StrictAssociativeRecipe {
    static constexpr crucible::ReductionDeterminism determinism = crucible::ReductionDeterminism::BITEXACT_STRICT;
    static constexpr bool associative = true;
    static constexpr bool commutative = true;
    static constexpr bool participant_count_power_of_two = true;
};

struct OrderedOddParticipantRecipe {
    static constexpr crucible::ReductionDeterminism determinism = crucible::ReductionDeterminism::ORDERED;
    static constexpr bool associative = true;
    static constexpr bool commutative = true;
    static constexpr bool participant_count_power_of_two = false;
};

void test_names_and_layout() {
    static_assert(sizeof(net::NetworkChunkCount) == sizeof(std::uint8_t));
    static_assert(sizeof(net::DeclaredNetworkRecipeConstraints) == sizeof(net::NetworkRecipeConstraints));
    static_assert(std::is_trivially_copy_constructible_v<net::NetworkRecipeConstraints>);
    static_assert(!std::is_trivially_copyable_v<net::NetworkRecipeConstraints>,
                  "the refined chunk count keeps a byte copy from building the constraints");

    assert(net::network_recipe_error_name(net::NetworkRecipeError::InvalidChunkCount)
           == ::fixy::TextView{"InvalidChunkCount"});
    assert(net::network_collective_algorithm_name(net::NetworkCollectiveAlgorithm::RecursiveHalvingDoubling)
           == ::fixy::TextView{"RecursiveHalvingDoubling"});
    assert(net::network_equivalence_class_name(net::NetworkEquivalenceClass::ByteIdentical)
           == ::fixy::TextView{"ByteIdentical"});

    assert(net::admit_network_chunk_count(1).is_ok());
    assert(net::admit_network_chunk_count(net::kNetworkRecipeMaxChunks).is_ok());
    assert(net::admit_network_chunk_count(0).is_err());
    assert(net::admit_network_chunk_count(net::kNetworkRecipeMaxChunks + 1u).is_err());
    assert(net::admit_network_participant_count(2).is_ok());
    assert(net::admit_network_participant_count(0).is_err());

    crucible::test::pass("  test_names_and_layout: PASSED\n");
}

void test_runtime_constraints_by_tier() {
    auto unordered = net::query_constraints(recipe(crucible::ReductionDeterminism::UNORDERED),
                                            net::NetworkReductionLaws{.associative = true, .commutative = true});
    assert(unordered.value().sharp_eligible);
    assert(unordered.value().lossy_compression_allowed);
    assert(unordered.value().quantization_allowed);
    assert(unordered.value().equivalence == net::NetworkEquivalenceClass::ApproximateRelative);

    auto tc = net::query_constraints(recipe(crucible::ReductionDeterminism::BITEXACT_TC),
                                     net::NetworkReductionLaws{.associative = true, .commutative = true});
    assert(tc.value().sharp_eligible);
    assert(!tc.value().lossy_compression_allowed);
    assert(!tc.value().quantization_allowed);
    assert(tc.value().fec_allowed);
    assert(tc.value().equivalence == net::NetworkEquivalenceClass::TensorCoreUlp);

    auto strict = net::query_constraints(recipe(crucible::ReductionDeterminism::BITEXACT_STRICT),
                                         net::NetworkReductionLaws{.associative = true, .commutative = true});
    assert(strict.value().requires_sorted_participants);
    assert(!strict.value().sharp_eligible);
    assert(strict.value().max_chunk_count.value() == 1u);
    assert(strict.value().equivalence == net::NetworkEquivalenceClass::ByteIdentical);

    auto unknown_laws = net::query_constraints(recipe(crucible::ReductionDeterminism::ORDERED));
    assert(!unknown_laws.value().sharp_eligible);

    auto non_commutative = net::query_constraints(recipe(crucible::ReductionDeterminism::ORDERED),
                                                  net::NetworkReductionLaws{.associative = true, .commutative = false});
    assert(!non_commutative.value().sharp_eligible);

    crucible::test::pass("  test_runtime_constraints_by_tier: PASSED\n");
}

void test_algorithm_admission() {
    auto four = net::admit_network_participant_count(4).expect("four participants are admitted");
    auto three = net::admit_network_participant_count(3).expect("three participants are admitted");
    auto ordered = net::query_constraints(recipe(crucible::ReductionDeterminism::ORDERED),
                                          net::NetworkReductionLaws{.associative = true, .commutative = true});
    auto strict = net::query_constraints(recipe(crucible::ReductionDeterminism::BITEXACT_STRICT),
                                         net::NetworkReductionLaws{.associative = true, .commutative = true});

    assert(net::algorithm_eligible(ordered, net::NetworkCollectiveAlgorithm::Ring, three).is_ok());
    assert(net::algorithm_eligible(ordered, net::NetworkCollectiveAlgorithm::Sharp, three).is_ok());
    assert(net::algorithm_eligible(ordered, net::NetworkCollectiveAlgorithm::TopKCompressed, three).is_ok());
    assert(net::algorithm_eligible(ordered, net::NetworkCollectiveAlgorithm::RecursiveHalvingDoubling, four).is_ok());

    auto rhd_bad = net::algorithm_eligible(ordered, net::NetworkCollectiveAlgorithm::RecursiveHalvingDoubling, three);
    assert(rhd_bad.is_err());
    assert(std::move(rhd_bad).err().expect("the participant count is not a power of two")
           == net::NetworkRecipeError::NonPowerOfTwoParticipantSet);

    assert(net::algorithm_eligible(strict, net::NetworkCollectiveAlgorithm::Sharp, four).is_err());
    assert(net::algorithm_eligible(strict, net::NetworkCollectiveAlgorithm::TopKCompressed, four).is_err());
    assert(net::algorithm_eligible(strict, net::NetworkCollectiveAlgorithm::FecProtected, four).is_ok());

    crucible::test::pass("  test_algorithm_admission: PASSED\n");
}

void test_compile_time_concepts() {
    static_assert(net::NetworkRecipeEligible<OrderedAssociativeRecipe, net::SharpAlgorithm>);
    static_assert(!net::NetworkRecipeEligible<StrictAssociativeRecipe, net::SharpAlgorithm>);
    static_assert(net::NetworkRecipeEligible<StrictAssociativeRecipe, net::DeterministicTreeAlgorithm>);
    static_assert(!net::NetworkRecipeEligible<StrictAssociativeRecipe, net::TopKCompressedAlgorithm>);
    static_assert(!net::NetworkRecipeEligible<OrderedOddParticipantRecipe, net::RecursiveHalvingDoublingAlgorithm>);

    crucible::test::pass("  test_compile_time_concepts: PASSED\n");
}

}  // namespace

int main() {
    ::fixy::report(::fixy::Sink::Out, "test_network_recipe:\n");
    test_names_and_layout();
    test_runtime_constraints_by_tier();
    test_algorithm_admission();
    test_compile_time_concepts();
    crucible::test::pass("test_network_recipe: all PASSED\n");
    return 0;
}
