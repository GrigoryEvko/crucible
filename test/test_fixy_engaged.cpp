// Every claim here is a static_assert, so compiling the file is the test.

#include <crucible/fixy/Reject.h>
#include <crucible/fixy/Grant.h>
#include <crucible/fixy/Dim.h>
#include <crucible/fixy/Default.h>

#include <tuple>
#include <type_traits>

namespace fixy = crucible::fixy;
namespace gr = crucible::fixy::grant;
using D = crucible::fixy::dim::DimensionAxis;

// IsAccepted demands that every dimension be engaged, so each stance below
// starts from an accept-strict marker on every axis and replaces one or two of
// them with a relaxation. The arrow marks the axis a relaxation engages, which
// the grant spelling alone does not name.

template <D Axis>
using strict = gr::accept_default_strict_for<Axis>;

// IsAccepted injects the Type-axis marker itself, which is why no pack below
// spells one.
static_assert(
    fixy::IsAccepted<int, strict<D::Refinement>, strict<D::Usage>, strict<D::Effect>, strict<D::Security>,
                     strict<D::Protocol>, strict<D::Lifetime>, strict<D::Provenance>, strict<D::Trust>,
                     strict<D::Representation>, strict<D::Observability>, strict<D::Complexity>, strict<D::Precision>,
                     strict<D::Space>, strict<D::Overflow>, strict<D::Mutation>, strict<D::Reentrancy>, strict<D::Size>,
                     strict<D::Version>, strict<D::Staleness>, strict<D::Synchronization>, strict<D::Regime>,
                     strict<D::FpMode>, strict<D::SyscallSurface>, strict<D::ControlFlow>, strict<D::CallShape>,
                     strict<D::StackUse>, strict<D::GlobalState>, strict<D::Stdio>, strict<D::HwInstruction>,
                     strict<D::BarrierStrength>, strict<D::SimdIsa>, strict<D::MemoryScope>>,
    "the all-strict baseline must be accepted.");

static_assert(
    fixy::IsAccepted<int, strict<D::Refinement>,
                     gr::copy,  // <-- Usage
                     strict<D::Effect>, strict<D::Security>, strict<D::Protocol>, strict<D::Lifetime>,
                     strict<D::Provenance>, strict<D::Trust>, strict<D::Representation>, strict<D::Observability>,
                     strict<D::Complexity>, strict<D::Precision>, strict<D::Space>, strict<D::Overflow>,
                     strict<D::Mutation>, strict<D::Reentrancy>, strict<D::Size>, strict<D::Version>,
                     strict<D::Staleness>, strict<D::Synchronization>, strict<D::Regime>, strict<D::FpMode>,
                     strict<D::SyscallSurface>, strict<D::ControlFlow>, strict<D::CallShape>, strict<D::StackUse>,
                     strict<D::GlobalState>, strict<D::Stdio>, strict<D::HwInstruction>, strict<D::BarrierStrength>,
                     strict<D::SimdIsa>, strict<D::MemoryScope>>,
    "copy engages the Usage axis.");

// A binding that keeps the strict Security default, which is Classified, and
// also engages an IO effect without a declassify is rejected. as_public is
// therefore the only Security shape that composes with with<IO> here.
static_assert(
    fixy::IsAccepted<int, strict<D::Refinement>, strict<D::Usage>,
                     gr::with<crucible::effects::Effect::IO>,  // <-- Effect
                     gr::as_public,  // <-- Security
                     strict<D::Protocol>, strict<D::Lifetime>, strict<D::Provenance>, strict<D::Trust>,
                     strict<D::Representation>, strict<D::Observability>, strict<D::Complexity>, strict<D::Precision>,
                     strict<D::Space>, strict<D::Overflow>, strict<D::Mutation>, strict<D::Reentrancy>, strict<D::Size>,
                     strict<D::Version>, strict<D::Staleness>, strict<D::Synchronization>, strict<D::Regime>,
                     strict<D::FpMode>, strict<D::SyscallSurface>, strict<D::ControlFlow>, strict<D::CallShape>,
                     strict<D::StackUse>, strict<D::GlobalState>, strict<D::Stdio>, strict<D::HwInstruction>,
                     strict<D::BarrierStrength>, strict<D::SimdIsa>, strict<D::MemoryScope>>,
    "with<IO> engages Effect, and Security must be as_public for the "
    "composition to be accepted.");

static_assert(fixy::IsAccepted<int, strict<D::Refinement>,
                               gr::copy,  // Usage
                               gr::with<crucible::effects::Effect::Block>,  // Effect
                               strict<D::Security>, strict<D::Protocol>, strict<D::Lifetime>, strict<D::Provenance>,
                               strict<D::Trust>, strict<D::Representation>, strict<D::Observability>,
                               strict<D::Complexity>, strict<D::Precision>, strict<D::Space>, strict<D::Overflow>,
                               strict<D::Mutation>, strict<D::Reentrancy>, strict<D::Size>, strict<D::Version>,
                               strict<D::Staleness>, strict<D::Synchronization>, strict<D::Regime>, strict<D::FpMode>,
                               strict<D::SyscallSurface>, strict<D::ControlFlow>, strict<D::CallShape>,
                               strict<D::StackUse>, strict<D::GlobalState>, strict<D::Stdio>, strict<D::HwInstruction>,
                               strict<D::BarrierStrength>, strict<D::SimdIsa>, strict<D::MemoryScope>>,
              "Usage and Effect must both count as engaged.");

// The strict Security default is already Classified, so a binding that
// consumes classified data needs no relaxation. The accept-strict marker is
// the correct engagement, and this pack is identical to the baseline.
static_assert(
    fixy::IsAccepted<int, strict<D::Refinement>, strict<D::Usage>, strict<D::Effect>, strict<D::Security>,
                     strict<D::Protocol>, strict<D::Lifetime>, strict<D::Provenance>, strict<D::Trust>,
                     strict<D::Representation>, strict<D::Observability>, strict<D::Complexity>, strict<D::Precision>,
                     strict<D::Space>, strict<D::Overflow>, strict<D::Mutation>, strict<D::Reentrancy>, strict<D::Size>,
                     strict<D::Version>, strict<D::Staleness>, strict<D::Synchronization>, strict<D::Regime>,
                     strict<D::FpMode>, strict<D::SyscallSurface>, strict<D::ControlFlow>, strict<D::CallShape>,
                     strict<D::StackUse>, strict<D::GlobalState>, strict<D::Stdio>, strict<D::HwInstruction>,
                     strict<D::BarrierStrength>, strict<D::SimdIsa>, strict<D::MemoryScope>>,
    "an explicit accept-strict marker on Security engages the Classified "
    "default.");

static_assert(
    fixy::IsAccepted<int,
                     gr::refined_with<crucible::safety::fn::pred::True>,  // <-- Refinement
                     strict<D::Usage>, strict<D::Effect>, strict<D::Security>, strict<D::Protocol>, strict<D::Lifetime>,
                     strict<D::Provenance>, strict<D::Trust>, strict<D::Representation>, strict<D::Observability>,
                     strict<D::Complexity>, strict<D::Precision>, strict<D::Space>, strict<D::Overflow>,
                     strict<D::Mutation>, strict<D::Reentrancy>, strict<D::Size>, strict<D::Version>,
                     strict<D::Staleness>, strict<D::Synchronization>, strict<D::Regime>, strict<D::FpMode>,
                     strict<D::SyscallSurface>, strict<D::ControlFlow>, strict<D::CallShape>, strict<D::StackUse>,
                     strict<D::GlobalState>, strict<D::Stdio>, strict<D::HwInstruction>, strict<D::BarrierStrength>,
                     strict<D::SimdIsa>, strict<D::MemoryScope>>,
    "refined_with<Pred> engages the Refinement axis.");

static_assert(
    fixy::IsAccepted<
        int, strict<D::Refinement>, strict<D::Usage>, strict<D::Effect>, strict<D::Security>, strict<D::Protocol>,
        gr::in_region<42>,  // <-- Lifetime
        strict<D::Provenance>, strict<D::Trust>, strict<D::Representation>, strict<D::Observability>,
        strict<D::Complexity>, strict<D::Precision>, strict<D::Space>, strict<D::Overflow>, strict<D::Mutation>,
        strict<D::Reentrancy>, strict<D::Size>, strict<D::Version>, strict<D::Staleness>, strict<D::Synchronization>,
        strict<D::Regime>, strict<D::FpMode>, strict<D::SyscallSurface>, strict<D::ControlFlow>, strict<D::CallShape>,
        strict<D::StackUse>, strict<D::GlobalState>, strict<D::Stdio>, strict<D::HwInstruction>,
        strict<D::BarrierStrength>, strict<D::SimdIsa>, strict<D::MemoryScope>>,
    "in_region<Tag> engages the Lifetime axis.");

static_assert(
    fixy::IsAccepted<int, strict<D::Refinement>, strict<D::Usage>, strict<D::Effect>, strict<D::Security>,
                     strict<D::Protocol>, strict<D::Lifetime>, strict<D::Provenance>, strict<D::Trust>,
                     strict<D::Representation>, strict<D::Observability>, strict<D::Complexity>, strict<D::Precision>,
                     strict<D::Space>, strict<D::Overflow>, strict<D::Mutation>, strict<D::Reentrancy>, strict<D::Size>,
                     gr::version<3>,  // <-- Version
                     strict<D::Staleness>, strict<D::Synchronization>, strict<D::Regime>, strict<D::FpMode>,
                     strict<D::SyscallSurface>, strict<D::ControlFlow>, strict<D::CallShape>, strict<D::StackUse>,
                     strict<D::GlobalState>, strict<D::Stdio>, strict<D::HwInstruction>, strict<D::BarrierStrength>,
                     strict<D::SimdIsa>, strict<D::MemoryScope>>,
    "version<N> engages the Version axis.");

// An empty pack still gets the injected Type marker, so it is the one case
// where something is engaged and the binding is rejected anyway.
static_assert(!fixy::IsAccepted<int>, "an empty grants pack must reject, because injection engages Type and "
                                      "nothing else.");

static_assert(
    !fixy::IsAccepted<void, strict<D::Refinement>, strict<D::Usage>, strict<D::Effect>, strict<D::Security>,
                      strict<D::Protocol>, strict<D::Lifetime>, strict<D::Provenance>, strict<D::Trust>,
                      strict<D::Representation>, strict<D::Observability>, strict<D::Complexity>, strict<D::Precision>,
                      strict<D::Space>, strict<D::Overflow>, strict<D::Mutation>, strict<D::Reentrancy>,
                      strict<D::Size>, strict<D::Version>, strict<D::Staleness>, strict<D::Synchronization>,
                      strict<D::Regime>, strict<D::FpMode>, strict<D::SyscallSurface>, strict<D::ControlFlow>,
                      strict<D::CallShape>, strict<D::StackUse>, strict<D::GlobalState>, strict<D::Stdio>,
                      strict<D::HwInstruction>, strict<D::BarrierStrength>, strict<D::SimdIsa>, strict<D::MemoryScope>>,
    "void must reject, because Fn requires a complete object type.");

int main() { return 0; }
