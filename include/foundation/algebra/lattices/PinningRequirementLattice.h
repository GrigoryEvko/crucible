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

#include <foundation/algebra/ClaimOrientation.h>
#include <foundation/algebra/Lattice.h>
#include <foundation/algebra/lattices/ChainLattice.h>
#include <foundation/reflect/EnumName.h>

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

// A broader coherence domain is the stronger claim.
struct PinningRequirementLattice
    : EnumChainLattice<PinningRequirementLattice, PinningRequirement, ClaimOrientation::stronger_is_higher> {
    template <PinningRequirement P>
    struct At : PinnedAt<PinningRequirementLattice, P> {
        static constexpr PinningRequirement requirement = P;
    };
};

namespace pinning_requirement {
using NotRequiredPin = PinningRequirementLattice::At<PinningRequirement::NotRequired>;
using PerCorePin = PinningRequirementLattice::At<PinningRequirement::PerCore>;
using PerSocketPin = PinningRequirementLattice::At<PinningRequirement::PerSocket>;
using CrossSocketSafePin = PinningRequirementLattice::At<PinningRequirement::CrossSocketSafe>;
}  // namespace pinning_requirement

}  // namespace foundation::algebra::lattices
