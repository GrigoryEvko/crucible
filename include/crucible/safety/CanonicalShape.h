#pragma once

// One concept over every canonical parameter shape, and its
// complement for a signature that matches none of them.
//
// The shapes are mutually exclusive: a function satisfies at most one
// of them.  The order the chain below tests them in therefore does not
// change which one is reported.  It only keeps the reported kind
// stable, so reordering the branches must leave every test result
// untouched.

#include <crucible/safety/BinaryTransform.h>
#include <crucible/safety/ConsumerEndpoint.h>
#include <crucible/safety/PipelineStage.h>
#include <crucible/safety/ProducerEndpoint.h>
#include <crucible/safety/Reduction.h>
#include <crucible/safety/SwmrReader.h>
#include <crucible/safety/SwmrWriter.h>
#include <crucible/safety/UnaryTransform.h>

#include <cstdint>
#include <string_view>

namespace crucible::safety::extract {

template <auto FnPtr>
concept CanonicalShape = UnaryTransform<FnPtr> || BinaryTransform<FnPtr> || Reduction<FnPtr> || ProducerEndpoint<FnPtr>
                      || ConsumerEndpoint<FnPtr> || SwmrWriter<FnPtr> || SwmrReader<FnPtr> || PipelineStage<FnPtr>;

template <auto FnPtr>
concept NonCanonical = !CanonicalShape<FnPtr>;

template <auto FnPtr>
inline constexpr bool is_canonical_shape_v = CanonicalShape<FnPtr>;

template <auto FnPtr>
inline constexpr bool is_non_canonical_v = NonCanonical<FnPtr>;

enum class CanonicalShapeKind : std::uint8_t {
    NonCanonical = 0,
    UnaryTransform,
    BinaryTransform,
    Reduction,
    ProducerEndpoint,
    ConsumerEndpoint,
    SwmrWriter,
    SwmrReader,
    PipelineStage,
};

namespace detail {

template <auto FnPtr>
consteval CanonicalShapeKind canonical_shape_kind_impl() noexcept {
    if constexpr (UnaryTransform<FnPtr>) {
        return CanonicalShapeKind::UnaryTransform;
    } else if constexpr (BinaryTransform<FnPtr>) {
        return CanonicalShapeKind::BinaryTransform;
    } else if constexpr (Reduction<FnPtr>) {
        return CanonicalShapeKind::Reduction;
    } else if constexpr (ProducerEndpoint<FnPtr>) {
        return CanonicalShapeKind::ProducerEndpoint;
    } else if constexpr (ConsumerEndpoint<FnPtr>) {
        return CanonicalShapeKind::ConsumerEndpoint;
    } else if constexpr (SwmrWriter<FnPtr>) {
        return CanonicalShapeKind::SwmrWriter;
    } else if constexpr (SwmrReader<FnPtr>) {
        return CanonicalShapeKind::SwmrReader;
    } else if constexpr (PipelineStage<FnPtr>) {
        return CanonicalShapeKind::PipelineStage;
    } else {
        return CanonicalShapeKind::NonCanonical;
    }
}

}  // namespace detail

template <auto FnPtr>
inline constexpr CanonicalShapeKind canonical_shape_kind_v = detail::canonical_shape_kind_impl<FnPtr>();

[[nodiscard]] constexpr std::string_view canonical_shape_name(CanonicalShapeKind k) noexcept {
    switch (k) {
        case CanonicalShapeKind::UnaryTransform:
            return "UnaryTransform";
        case CanonicalShapeKind::BinaryTransform:
            return "BinaryTransform";
        case CanonicalShapeKind::Reduction:
            return "Reduction";
        case CanonicalShapeKind::ProducerEndpoint:
            return "ProducerEndpoint";
        case CanonicalShapeKind::ConsumerEndpoint:
            return "ConsumerEndpoint";
        case CanonicalShapeKind::SwmrWriter:
            return "SwmrWriter";
        case CanonicalShapeKind::SwmrReader:
            return "SwmrReader";
        case CanonicalShapeKind::PipelineStage:
            return "PipelineStage";
        case CanonicalShapeKind::NonCanonical:
            return "NonCanonical";
        // The cases above are exhaustive.  This arm answers only a
        // value that was never one of the enumerators.
        default:
            return "Unknown";
    }
}

template <auto FnPtr>
inline constexpr std::string_view canonical_shape_name_of_v = canonical_shape_name(canonical_shape_kind_v<FnPtr>);

// Only the fallback is covered here.  A witness for each shape would
// instantiate all of their wrappers in every consumer of this header.

namespace detail::canonical_shape_self_test {

inline void f_two_ints(int, int) noexcept {}
static_assert(!CanonicalShape<&f_two_ints>);
static_assert(NonCanonical<&f_two_ints>);
static_assert(canonical_shape_kind_v<&f_two_ints> == CanonicalShapeKind::NonCanonical);
static_assert(canonical_shape_name(CanonicalShapeKind::NonCanonical) == std::string_view{"NonCanonical"});

inline void f_three_params(int, int, int) noexcept {}
static_assert(!CanonicalShape<&f_three_params>);
static_assert(NonCanonical<&f_three_params>);

}  // namespace detail::canonical_shape_self_test

inline bool canonical_shape_smoke_test() noexcept {
    using namespace detail::canonical_shape_self_test;

    volatile std::size_t const cap = 4;
    bool ok = true;
    for (std::size_t i = 0; i < cap; ++i) {
        ok = ok && !CanonicalShape<&f_two_ints>;
        ok = ok && NonCanonical<&f_two_ints>;
        ok = ok && (canonical_shape_kind_v<&f_two_ints> == CanonicalShapeKind::NonCanonical);
        ok = ok && (canonical_shape_name(canonical_shape_kind_v<&f_three_params>) == std::string_view{"NonCanonical"});
    }
    return ok;
}

}  // namespace crucible::safety::extract
