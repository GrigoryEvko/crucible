#pragma once

// GAPS-174. Network-kernel recipe constraints for Forge RecipeSelect.
//
// This header is deliberately only the admission/equivalence substrate.
// It does not implement Forge phases, collective kernels, SHARP, QUIC,
// RDMA, or cross-vendor CI. It records the recipe-tier facts that those
// layers must consume: which collective classes are admissible, whether
// participant order is load-bearing, and which equivalence class a
// network-kernel result is allowed to claim.

#include <crucible/NumericalRecipe.h>
#include <crucible/Platform.h>
#include <crucible/safety/Refined.h>
#include <crucible/safety/RefinedAlgebra.h>
#include <crucible/safety/Tagged.h>

#include <cstdint>
#include <concepts>
#include <expected>
#include <string_view>
#include <type_traits>

namespace crucible::forge::recipes {

inline constexpr std::uint8_t kNetworkRecipeMaxChunks = 64;

enum class NetworkRecipeError : std::uint8_t {
    None = 0,
    InvalidChunkCount,
    EmptyParticipantSet,
    NonPowerOfTwoParticipantSet,
    AlgorithmForbiddenByRecipe,
};

enum class NetworkCollectiveAlgorithm : std::uint8_t {
    Ring = 0,
    DeterministicTree,
    Sharp,
    RecursiveHalvingDoubling,
    TopKCompressed,
    Quantized,
    FecProtected,
};

enum class NetworkEquivalenceClass : std::uint8_t {
    ApproximateRelative = 0,
    OrderedTolerance,
    TensorCoreUlp,
    ByteIdentical,
};

using NetworkChunkCount =
    safety::Bounded<std::uint8_t{1}, kNetworkRecipeMaxChunks, std::uint8_t>;
using NetworkParticipantCount = safety::Positive<std::uint16_t>;

struct NetworkRecipeConstraints {
    bool ring_eligible = true;
    bool deterministic_tree_eligible = true;
    bool sharp_eligible = false;
    bool recursive_halving_doubling_eligible = false;
    bool requires_sorted_participants = false;
    bool lossy_compression_allowed = false;
    bool quantization_allowed = false;
    bool fec_allowed = true;
    NetworkChunkCount max_chunk_count{
        kNetworkRecipeMaxChunks, typename NetworkChunkCount::Trusted{}};
    NetworkEquivalenceClass equivalence = NetworkEquivalenceClass::OrderedTolerance;
};

struct NetworkReductionLaws {
    bool associative = false;
    bool commutative = false;
};

using DeclaredNetworkRecipeConstraints = safety::Tagged<
    NetworkRecipeConstraints, safety::source::NetworkRecipeRegistry>;

[[nodiscard]] std::string_view
network_recipe_error_name(NetworkRecipeError error) noexcept;

[[nodiscard]] std::string_view
network_collective_algorithm_name(NetworkCollectiveAlgorithm algorithm) noexcept;

[[nodiscard]] std::string_view
network_equivalence_class_name(NetworkEquivalenceClass eq) noexcept;

[[nodiscard]] constexpr bool is_power_of_two_participant_count(
    NetworkParticipantCount participants) noexcept {
    auto const count = participants.value();
    return count != 0u && (count & static_cast<std::uint16_t>(count - 1u)) == 0u;
}

[[nodiscard]] constexpr std::expected<NetworkChunkCount, NetworkRecipeError>
admit_network_chunk_count(std::uint8_t count) noexcept {
    if (count == 0u || count > kNetworkRecipeMaxChunks) {
        return std::unexpected(NetworkRecipeError::InvalidChunkCount);
    }
    return NetworkChunkCount{count, typename NetworkChunkCount::Trusted{}};
}

[[nodiscard]] constexpr std::expected<NetworkParticipantCount, NetworkRecipeError>
admit_network_participant_count(std::uint16_t count) noexcept {
    if (count == 0u) {
        return std::unexpected(NetworkRecipeError::EmptyParticipantSet);
    }
    return NetworkParticipantCount{count, typename NetworkParticipantCount::Trusted{}};
}

[[nodiscard]] constexpr NetworkEquivalenceClass
equivalence_class_for(ReductionDeterminism determinism) noexcept {
    switch (determinism) {
        case ReductionDeterminism::UNORDERED:
            return NetworkEquivalenceClass::ApproximateRelative;
        case ReductionDeterminism::ORDERED:
            return NetworkEquivalenceClass::OrderedTolerance;
        case ReductionDeterminism::BITEXACT_TC:
            return NetworkEquivalenceClass::TensorCoreUlp;
        case ReductionDeterminism::BITEXACT_STRICT:
            return NetworkEquivalenceClass::ByteIdentical;
        default:
            return NetworkEquivalenceClass::ApproximateRelative;
    }
}

[[nodiscard]] constexpr DeclaredNetworkRecipeConstraints
query_constraints(NumericalRecipe const& recipe,
                  NetworkReductionLaws laws) noexcept {
    NetworkRecipeConstraints constraints{};
    constraints.equivalence = equivalence_class_for(recipe.determinism);
    constraints.requires_sorted_participants =
        recipe.determinism == ReductionDeterminism::BITEXACT_STRICT;
    constraints.sharp_eligible = laws.associative && laws.commutative
        && recipe.determinism != ReductionDeterminism::BITEXACT_STRICT;
    constraints.recursive_halving_doubling_eligible = true;
    constraints.lossy_compression_allowed =
        recipe.determinism == ReductionDeterminism::UNORDERED
        || recipe.determinism == ReductionDeterminism::ORDERED;
    constraints.quantization_allowed = constraints.lossy_compression_allowed;
    if (recipe.determinism == ReductionDeterminism::BITEXACT_STRICT) {
        constraints.max_chunk_count =
            NetworkChunkCount{std::uint8_t{1},
                              typename NetworkChunkCount::Trusted{}};
    }
    return DeclaredNetworkRecipeConstraints{constraints};
}

[[nodiscard]] constexpr DeclaredNetworkRecipeConstraints
query_constraints(NumericalRecipe const& recipe) noexcept {
    return query_constraints(recipe, NetworkReductionLaws{});
}

[[nodiscard]] constexpr std::expected<void, NetworkRecipeError>
algorithm_eligible(DeclaredNetworkRecipeConstraints constraints,
                   NetworkCollectiveAlgorithm algorithm,
                   NetworkParticipantCount participants) noexcept {
    auto const& raw = constraints.value();
    switch (algorithm) {
        case NetworkCollectiveAlgorithm::Ring:
            return raw.ring_eligible
                ? std::expected<void, NetworkRecipeError>{}
                : std::unexpected(NetworkRecipeError::AlgorithmForbiddenByRecipe);
        case NetworkCollectiveAlgorithm::DeterministicTree:
            return raw.deterministic_tree_eligible
                ? std::expected<void, NetworkRecipeError>{}
                : std::unexpected(NetworkRecipeError::AlgorithmForbiddenByRecipe);
        case NetworkCollectiveAlgorithm::Sharp:
            return raw.sharp_eligible
                ? std::expected<void, NetworkRecipeError>{}
                : std::unexpected(NetworkRecipeError::AlgorithmForbiddenByRecipe);
        case NetworkCollectiveAlgorithm::RecursiveHalvingDoubling:
            if (!raw.recursive_halving_doubling_eligible) {
                return std::unexpected(
                    NetworkRecipeError::AlgorithmForbiddenByRecipe);
            }
            if (!is_power_of_two_participant_count(participants)) {
                return std::unexpected(
                    NetworkRecipeError::NonPowerOfTwoParticipantSet);
            }
            return {};
        case NetworkCollectiveAlgorithm::TopKCompressed:
            return raw.lossy_compression_allowed
                ? std::expected<void, NetworkRecipeError>{}
                : std::unexpected(NetworkRecipeError::AlgorithmForbiddenByRecipe);
        case NetworkCollectiveAlgorithm::Quantized:
            return raw.quantization_allowed
                ? std::expected<void, NetworkRecipeError>{}
                : std::unexpected(NetworkRecipeError::AlgorithmForbiddenByRecipe);
        case NetworkCollectiveAlgorithm::FecProtected:
            return raw.fec_allowed
                ? std::expected<void, NetworkRecipeError>{}
                : std::unexpected(NetworkRecipeError::AlgorithmForbiddenByRecipe);
        default:
            return std::unexpected(NetworkRecipeError::AlgorithmForbiddenByRecipe);
    }
}

template <class Recipe>
concept DeclaresNetworkRecipe =
    requires {
        { Recipe::determinism } -> std::convertible_to<ReductionDeterminism>;
        { Recipe::associative } -> std::convertible_to<bool>;
        { Recipe::commutative } -> std::convertible_to<bool>;
        { Recipe::participant_count_power_of_two } -> std::convertible_to<bool>;
    };

template <class Algorithm>
concept DeclaresNetworkAlgorithm =
    requires {
        { Algorithm::kind } -> std::convertible_to<NetworkCollectiveAlgorithm>;
    };

[[nodiscard]] constexpr bool compile_time_algorithm_eligible(
    ReductionDeterminism determinism,
    bool associative,
    bool commutative,
    bool participant_count_power_of_two,
    NetworkCollectiveAlgorithm algorithm) noexcept {
    switch (algorithm) {
        case NetworkCollectiveAlgorithm::Ring:
        case NetworkCollectiveAlgorithm::DeterministicTree:
        case NetworkCollectiveAlgorithm::FecProtected:
            return true;
        case NetworkCollectiveAlgorithm::Sharp:
            return associative && commutative
                && determinism != ReductionDeterminism::BITEXACT_STRICT;
        case NetworkCollectiveAlgorithm::RecursiveHalvingDoubling:
            return participant_count_power_of_two;
        case NetworkCollectiveAlgorithm::TopKCompressed:
        case NetworkCollectiveAlgorithm::Quantized:
            return determinism == ReductionDeterminism::UNORDERED
                || determinism == ReductionDeterminism::ORDERED;
        default:
            return false;
    }
}

template <class Recipe, class Algorithm>
concept NetworkRecipeEligible =
    DeclaresNetworkRecipe<Recipe>
    && DeclaresNetworkAlgorithm<Algorithm>
    && compile_time_algorithm_eligible(
        Recipe::determinism,
        Recipe::associative,
        Recipe::commutative,
        Recipe::participant_count_power_of_two,
        Algorithm::kind);

struct RingAlgorithm {
    static constexpr NetworkCollectiveAlgorithm kind =
        NetworkCollectiveAlgorithm::Ring;
};
struct DeterministicTreeAlgorithm {
    static constexpr NetworkCollectiveAlgorithm kind =
        NetworkCollectiveAlgorithm::DeterministicTree;
};
struct SharpAlgorithm {
    static constexpr NetworkCollectiveAlgorithm kind =
        NetworkCollectiveAlgorithm::Sharp;
};
struct RecursiveHalvingDoublingAlgorithm {
    static constexpr NetworkCollectiveAlgorithm kind =
        NetworkCollectiveAlgorithm::RecursiveHalvingDoubling;
};
struct TopKCompressedAlgorithm {
    static constexpr NetworkCollectiveAlgorithm kind =
        NetworkCollectiveAlgorithm::TopKCompressed;
};
struct QuantizedAlgorithm {
    static constexpr NetworkCollectiveAlgorithm kind =
        NetworkCollectiveAlgorithm::Quantized;
};
struct FecProtectedAlgorithm {
    static constexpr NetworkCollectiveAlgorithm kind =
        NetworkCollectiveAlgorithm::FecProtected;
};

static_assert(sizeof(NetworkChunkCount) == sizeof(std::uint8_t));
static_assert(sizeof(DeclaredNetworkRecipeConstraints)
              == sizeof(NetworkRecipeConstraints));
static_assert(std::is_trivially_copyable_v<NetworkRecipeConstraints>);

}  // namespace crucible::forge::recipes
