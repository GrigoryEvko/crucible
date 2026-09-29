#pragma once

// Chain over the determinism of the sources a value's bytes came from.
// bottom is NonDeterministicSyscall and top is Pure.  Stronger
// replay-safety is higher, so leq(weak, strong) reads "a consumer that
// tolerates the weaker tier accepts a stronger provider".  join takes
// the purer of two providers and meet takes the less pure.
//
// The rungs rank how reproducible a source is on replay, not how
// expensive it is to read.  A monotonic clock outranks a wall clock
// because it never steps backwards, and a wall clock outranks entropy
// because entropy is unreproducible by construction.  Philox sits just
// below Pure: the same counter and key give the same bits on any
// machine, but the generator state is still observable.

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

enum class DetSafeTier : std::uint8_t {
    NonDeterministicSyscall = 0,  // any syscall whose result cannot be pinned
    FilesystemMtime = 1,  // an externally mutable timestamp
    EntropyRead = 2,  // /dev/urandom, getrandom(2), hardware RNG
    WallClockRead = 3,  // system_clock::now() — can step backwards
    MonotonicClockRead = 4,  // steady_clock::now() — bounded within one run
    PhiloxRng = 5,  // counter-based PRNG, replay-deterministic
    Pure = 6,  // a pure function of the declared inputs
};

// A purer source is the stronger claim.
struct DetSafeLattice : EnumChainLattice<DetSafeLattice, DetSafeTier, ClaimOrientation::stronger_is_higher> {
    template <DetSafeTier T>
    struct At : PinnedAt<DetSafeLattice, T> {
        static constexpr DetSafeTier tier = T;
    };
};

namespace det_safe_tier {
using NdsTier = DetSafeLattice::At<DetSafeTier::NonDeterministicSyscall>;
using FsMtimeTier = DetSafeLattice::At<DetSafeTier::FilesystemMtime>;
using EntropyTier = DetSafeLattice::At<DetSafeTier::EntropyRead>;
using WallClockTier = DetSafeLattice::At<DetSafeTier::WallClockRead>;
using MonoClockTier = DetSafeLattice::At<DetSafeTier::MonotonicClockRead>;
using PhiloxTier = DetSafeLattice::At<DetSafeTier::PhiloxRng>;
using PureTier = DetSafeLattice::At<DetSafeTier::Pure>;
}  // namespace det_safe_tier

namespace detail::det_safe_lattice_self_test {

static_assert(::foundation::reflect::enum_count<DetSafeTier> == 7,
              "DetSafeTier catalog diverged from {NDS, FsMtime, Entropy, WallClock, MonoClock, Philox, Pure}.  "
              "Confirm intent and update the callers that pin a tier.");

// The generic walk covers the declaration order, the exhaustive axioms,
// the reflected names and the shape of every At<tier>.
static_assert(verify_chain_lattice<DetSafeLattice>(), "DetSafeLattice: the chain order, the pinned grades or the "
                                                      "reflected names diverged from the DetSafeTier enumerator list.");

static_assert(!UnboundedLattice<DetSafeLattice>);
static_assert(!Semiring<DetSafeLattice>);

// The chain is not a stored grade, and a pinned tier is.  The element of
// a pinned tier is empty, and it names one claim.
static_assert(!GradableLattice<DetSafeLattice>);
static_assert(claim_orientation_v<det_safe_tier::PureTier> == ClaimOrientation::one_claim);

// The specific pins: which enumerators bound the chain, and the exact
// spellings that the reflection builds.
static_assert(DetSafeLattice::bottom() == DetSafeTier::NonDeterministicSyscall);
static_assert(DetSafeLattice::top() == DetSafeTier::Pure);

static_assert(DetSafeLattice::name() == "DetSafeLattice");
static_assert(det_safe_tier::NdsTier::name() == "DetSafeLattice::At<NonDeterministicSyscall>");
static_assert(det_safe_tier::PureTier::name() == "DetSafeLattice::At<Pure>");
static_assert(DetSafeLattice::At<static_cast<DetSafeTier>(255)>::name() == "DetSafeLattice::At<?>");

static_assert(det_safe_tier::NdsTier::tier == DetSafeTier::NonDeterministicSyscall);
static_assert(det_safe_tier::PureTier::tier == DetSafeTier::Pure);

}  // namespace detail::det_safe_lattice_self_test

}  // namespace foundation::algebra::lattices
