#pragma once

// Chain over how far a thread may migrate between two timestamp reads
// without corrupting the delta.  bottom is NotRequired and top is
// CrossSocketSafe.  A broader coherence domain is higher, so leq(narrow,
// broad) reads "a narrow consumer is satisfied by a broad provider": a
// socket-coherent source serves a per-core consumer.
//
// NotRequired is the absence of a claim, not a verified result.  A
// source shown to need no pinning at all declares CrossSocketSafe.
//
// The hardware fact behind the axis: a timestamp counter is not
// necessarily coherent across cores or across sockets.  A read pair that
// straddles a migration over an offset boundary can report a delta that
// runs backwards and wraps.

#include <foundation/algebra/Graded.h>
#include <foundation/algebra/Lattice.h>
#include <foundation/algebra/lattices/ChainLattice.h>
#include <foundation/reflect/Enumerate.h>

#include <cstdint>
#include <meta>
#include <string_view>
#include <type_traits>
#include <utility>

namespace foundation::algebra::lattices {

enum class PinningRequirement : std::uint8_t {
    NotRequired = 0,  // no coherence discipline declared
    PerCore = 1,  // coherent within one core, so the mask must be a singleton
    PerSocket = 2,  // coherent within one socket
    CrossSocketSafe = 3,  // coherent across every socket
};

inline constexpr std::size_t pinning_requirement_count = ::foundation::reflect::enum_count<PinningRequirement>;

// The identifier of p, or "<unknown PinningRequirement>" for a value
// outside the enum.
[[nodiscard]] consteval std::string_view pinning_requirement_name(PinningRequirement p) noexcept {
    return ::foundation::reflect::enum_name(p);
}

struct PinningRequirementLattice : ChainLatticeOps<PinningRequirement> {
    [[nodiscard]] static constexpr element_type bottom() noexcept { return PinningRequirement::NotRequired; }
    [[nodiscard]] static constexpr element_type top() noexcept { return PinningRequirement::CrossSocketSafe; }

    [[nodiscard]] static consteval std::string_view name() noexcept { return "PinningRequirementLattice"; }

    template <PinningRequirement P>
    struct AtElement : PinnedElement<P> {
        using pinning_requirement_value_type = PinningRequirement;
    };

    template <PinningRequirement P>
    struct At : PinnedAt<PinningRequirementLattice, P, AtElement<P>> {
        static constexpr PinningRequirement requirement = P;
    };
};

namespace pinning_requirement {
using NotRequiredPin = PinningRequirementLattice::At<PinningRequirement::NotRequired>;
using PerCorePin = PinningRequirementLattice::At<PinningRequirement::PerCore>;
using PerSocketPin = PinningRequirementLattice::At<PinningRequirement::PerSocket>;
using CrossSocketSafePin = PinningRequirementLattice::At<PinningRequirement::CrossSocketSafe>;
}  // namespace pinning_requirement

namespace detail::pinning_requirement_lattice_self_test {

static_assert(pinning_requirement_count == 4, "PinningRequirement catalog diverged from {NotRequired, PerCore, "
                                              "PerSocket, CrossSocketSafe}.  A new level needs every composite "
                                              "that names a level rechecked.");

static_assert(verify_chain_lattice<PinningRequirementLattice>(),
              "PinningRequirementLattice: the chain order, the pinned grades or "
              "the reflected names diverged from the PinningRequirement "
              "enumerator list.");

static_assert(!UnboundedLattice<PinningRequirementLattice>);
static_assert(!Semiring<PinningRequirementLattice>);

static_assert(PinningRequirementLattice::bottom() == PinningRequirement::NotRequired);
static_assert(PinningRequirementLattice::top() == PinningRequirement::CrossSocketSafe);

static_assert(PinningRequirementLattice::leq(PinningRequirement::PerCore, PinningRequirement::PerSocket),
              "A socket-coherent source serves a per-core consumer, because socket "
              "coherence contains core coherence.");
static_assert(!PinningRequirementLattice::leq(PinningRequirement::PerSocket, PinningRequirement::PerCore),
              "A merely core-coherent source does not serve a consumer that migrates "
              "across the socket.  That pairing is the backwards-delta read this "
              "axis forbids.");

static_assert(PinningRequirementLattice::name() == "PinningRequirementLattice");
static_assert(pinning_requirement::NotRequiredPin::name() == "PinningRequirementLattice::At<NotRequired>");
static_assert(pinning_requirement::CrossSocketSafePin::name() == "PinningRequirementLattice::At<CrossSocketSafe>");
static_assert(PinningRequirementLattice::At<static_cast<PinningRequirement>(255)>::name()
              == "PinningRequirementLattice::At<?>");

static_assert(pinning_requirement_name(PinningRequirement::PerSocket) == "PerSocket");
static_assert(pinning_requirement_name(static_cast<PinningRequirement>(255)) == "<unknown PinningRequirement>");

static_assert(pinning_requirement::NotRequiredPin::requirement == PinningRequirement::NotRequired);
static_assert(pinning_requirement::CrossSocketSafePin::requirement == PinningRequirement::CrossSocketSafe);

struct OneByteValue {
    char c{0};
};
struct EightByteValue {
    unsigned long long v{0};
};

template <typename T_>
using CorePinnedGraded = Graded<ModalityKind::Absolute, pinning_requirement::PerCorePin, T_>;
CRUCIBLE_GRADED_LAYOUT_INVARIANT(CorePinnedGraded, OneByteValue);
CRUCIBLE_GRADED_LAYOUT_INVARIANT(CorePinnedGraded, EightByteValue);
CRUCIBLE_GRADED_LAYOUT_INVARIANT(CorePinnedGraded, int);

template <typename T_>
using CrossSocketGraded = Graded<ModalityKind::Absolute, pinning_requirement::CrossSocketSafePin, T_>;
CRUCIBLE_GRADED_LAYOUT_INVARIANT(CrossSocketGraded, EightByteValue);

}  // namespace detail::pinning_requirement_lattice_self_test

}  // namespace foundation::algebra::lattices
