#include <crucible/forge/recipes/Network.h>

namespace crucible::forge::recipes {

std::string_view network_recipe_error_name(NetworkRecipeError error) noexcept {
    switch (error) {
        case NetworkRecipeError::None: return "None";
        case NetworkRecipeError::InvalidChunkCount:
            return "InvalidChunkCount";
        case NetworkRecipeError::EmptyParticipantSet:
            return "EmptyParticipantSet";
        case NetworkRecipeError::NonPowerOfTwoParticipantSet:
            return "NonPowerOfTwoParticipantSet";
        case NetworkRecipeError::AlgorithmForbiddenByRecipe:
            return "AlgorithmForbiddenByRecipe";
        default: return "<unknown NetworkRecipeError>";
    }
}

std::string_view network_collective_algorithm_name(
    NetworkCollectiveAlgorithm algorithm) noexcept {
    switch (algorithm) {
        case NetworkCollectiveAlgorithm::Ring: return "Ring";
        case NetworkCollectiveAlgorithm::DeterministicTree:
            return "DeterministicTree";
        case NetworkCollectiveAlgorithm::Sharp: return "Sharp";
        case NetworkCollectiveAlgorithm::RecursiveHalvingDoubling:
            return "RecursiveHalvingDoubling";
        case NetworkCollectiveAlgorithm::TopKCompressed:
            return "TopKCompressed";
        case NetworkCollectiveAlgorithm::Quantized: return "Quantized";
        case NetworkCollectiveAlgorithm::FecProtected: return "FecProtected";
        default: return "<unknown NetworkCollectiveAlgorithm>";
    }
}

std::string_view network_equivalence_class_name(
    NetworkEquivalenceClass eq) noexcept {
    switch (eq) {
        case NetworkEquivalenceClass::ApproximateRelative:
            return "ApproximateRelative";
        case NetworkEquivalenceClass::OrderedTolerance:
            return "OrderedTolerance";
        case NetworkEquivalenceClass::TensorCoreUlp:
            return "TensorCoreUlp";
        case NetworkEquivalenceClass::ByteIdentical:
            return "ByteIdentical";
        default: return "<unknown NetworkEquivalenceClass>";
    }
}

}  // namespace crucible::forge::recipes
