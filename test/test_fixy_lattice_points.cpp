// Every point on the security and trust lattices must be reachable
// through one named grant tag, and each tag must project to the
// matching substrate slot.  One binding per point is what makes the
// coverage exhaustive: a point with no tag of its own can only be
// reached by relaxing some other axis, which a caller cannot do.

#include <crucible/fixy/Fn.h>

#include <type_traits>

namespace fixy = crucible::fixy;
namespace gr = crucible::fixy::grant;
using D = crucible::fixy::dim::DimensionAxis;

template <D Axis>
using strict = gr::accept_default_strict_for<Axis>;

namespace lat_sec_unclassified {
using fn_t = fixy::fn<int, strict<D::Refinement>, strict<D::Usage>, strict<D::Effect>, gr::as_unclassified,
                      strict<D::Protocol>, strict<D::Lifetime>, strict<D::Provenance>, strict<D::Trust>,
                      strict<D::Representation>, strict<D::Observability>, strict<D::Complexity>, strict<D::Precision>,
                      strict<D::Space>, strict<D::Overflow>, strict<D::Mutation>, strict<D::Reentrancy>,
                      strict<D::Size>, strict<D::Version>, strict<D::Staleness>, strict<D::Synchronization>,
                      strict<D::Regime>, strict<D::FpMode>, strict<D::SyscallSurface>, strict<D::ControlFlow>,
                      strict<D::CallShape>, strict<D::StackUse>, strict<D::GlobalState>, strict<D::Stdio>,
                      strict<D::HwInstruction>, strict<D::BarrierStrength>, strict<D::SimdIsa>, strict<D::MemoryScope>>;
static_assert(fn_t::security_v == crucible::safety::fn::SecLevel::Unclassified,
              "as_unclassified must resolve Security to SecLevel::Unclassified.");
}  // namespace lat_sec_unclassified

namespace lat_sec_public {
using fn_t = fixy::fn<int, strict<D::Refinement>, strict<D::Usage>, strict<D::Effect>, gr::as_public,
                      strict<D::Protocol>, strict<D::Lifetime>, strict<D::Provenance>, strict<D::Trust>,
                      strict<D::Representation>, strict<D::Observability>, strict<D::Complexity>, strict<D::Precision>,
                      strict<D::Space>, strict<D::Overflow>, strict<D::Mutation>, strict<D::Reentrancy>,
                      strict<D::Size>, strict<D::Version>, strict<D::Staleness>, strict<D::Synchronization>,
                      strict<D::Regime>, strict<D::FpMode>, strict<D::SyscallSurface>, strict<D::ControlFlow>,
                      strict<D::CallShape>, strict<D::StackUse>, strict<D::GlobalState>, strict<D::Stdio>,
                      strict<D::HwInstruction>, strict<D::BarrierStrength>, strict<D::SimdIsa>, strict<D::MemoryScope>>;
static_assert(fn_t::security_v == crucible::safety::fn::SecLevel::Public,
              "as_public must resolve Security to SecLevel::Public.");
}  // namespace lat_sec_public

namespace lat_sec_internal {
using fn_t = fixy::fn<int, strict<D::Refinement>, strict<D::Usage>, strict<D::Effect>, gr::as_internal,
                      strict<D::Protocol>, strict<D::Lifetime>, strict<D::Provenance>, strict<D::Trust>,
                      strict<D::Representation>, strict<D::Observability>, strict<D::Complexity>, strict<D::Precision>,
                      strict<D::Space>, strict<D::Overflow>, strict<D::Mutation>, strict<D::Reentrancy>,
                      strict<D::Size>, strict<D::Version>, strict<D::Staleness>, strict<D::Synchronization>,
                      strict<D::Regime>, strict<D::FpMode>, strict<D::SyscallSurface>, strict<D::ControlFlow>,
                      strict<D::CallShape>, strict<D::StackUse>, strict<D::GlobalState>, strict<D::Stdio>,
                      strict<D::HwInstruction>, strict<D::BarrierStrength>, strict<D::SimdIsa>, strict<D::MemoryScope>>;
static_assert(fn_t::security_v == crucible::safety::fn::SecLevel::Internal,
              "as_internal must resolve Security to SecLevel::Internal.");
}  // namespace lat_sec_internal

namespace lat_sec_classified {
using fn_t = fixy::fn<int, strict<D::Refinement>, strict<D::Usage>, strict<D::Effect>, gr::as_classified,
                      strict<D::Protocol>, strict<D::Lifetime>, strict<D::Provenance>, strict<D::Trust>,
                      strict<D::Representation>, strict<D::Observability>, strict<D::Complexity>, strict<D::Precision>,
                      strict<D::Space>, strict<D::Overflow>, strict<D::Mutation>, strict<D::Reentrancy>,
                      strict<D::Size>, strict<D::Version>, strict<D::Staleness>, strict<D::Synchronization>,
                      strict<D::Regime>, strict<D::FpMode>, strict<D::SyscallSurface>, strict<D::ControlFlow>,
                      strict<D::CallShape>, strict<D::StackUse>, strict<D::GlobalState>, strict<D::Stdio>,
                      strict<D::HwInstruction>, strict<D::BarrierStrength>, strict<D::SimdIsa>, strict<D::MemoryScope>>;
// Classified is also the strict default, so this case alone cannot
// distinguish a working tag from one that does nothing.  The other
// four points carry that weight.
static_assert(fn_t::security_v == crucible::safety::fn::SecLevel::Classified,
              "as_classified must resolve Security to SecLevel::Classified.");
}  // namespace lat_sec_classified

namespace lat_sec_secret {
using fn_t = fixy::fn<int, strict<D::Refinement>, strict<D::Usage>, strict<D::Effect>, gr::as_secret,
                      strict<D::Protocol>, strict<D::Lifetime>, strict<D::Provenance>, strict<D::Trust>,
                      strict<D::Representation>, strict<D::Observability>, strict<D::Complexity>, strict<D::Precision>,
                      strict<D::Space>, strict<D::Overflow>, strict<D::Mutation>, strict<D::Reentrancy>,
                      strict<D::Size>, strict<D::Version>, strict<D::Staleness>, strict<D::Synchronization>,
                      strict<D::Regime>, strict<D::FpMode>, strict<D::SyscallSurface>, strict<D::ControlFlow>,
                      strict<D::CallShape>, strict<D::StackUse>, strict<D::GlobalState>, strict<D::Stdio>,
                      strict<D::HwInstruction>, strict<D::BarrierStrength>, strict<D::SimdIsa>, strict<D::MemoryScope>>;
static_assert(fn_t::security_v == crucible::safety::fn::SecLevel::Secret,
              "as_secret must resolve Security to SecLevel::Secret.");
}  // namespace lat_sec_secret

namespace lat_trust_verified {
using fn_t = fixy::fn<int, strict<D::Refinement>, strict<D::Usage>, strict<D::Effect>, strict<D::Security>,
                      strict<D::Protocol>, strict<D::Lifetime>, strict<D::Provenance>, gr::trust_verified,
                      strict<D::Representation>, strict<D::Observability>, strict<D::Complexity>, strict<D::Precision>,
                      strict<D::Space>, strict<D::Overflow>, strict<D::Mutation>, strict<D::Reentrancy>,
                      strict<D::Size>, strict<D::Version>, strict<D::Staleness>, strict<D::Synchronization>,
                      strict<D::Regime>, strict<D::FpMode>, strict<D::SyscallSurface>, strict<D::ControlFlow>,
                      strict<D::CallShape>, strict<D::StackUse>, strict<D::GlobalState>, strict<D::Stdio>,
                      strict<D::HwInstruction>, strict<D::BarrierStrength>, strict<D::SimdIsa>, strict<D::MemoryScope>>;
static_assert(std::is_same_v<typename fn_t::trust_t, crucible::safety::trust::Verified>,
              "trust_verified must resolve Trust to safety::trust::Verified.");
}  // namespace lat_trust_verified

namespace lat_trust_tested {
using fn_t = fixy::fn<int, strict<D::Refinement>, strict<D::Usage>, strict<D::Effect>, strict<D::Security>,
                      strict<D::Protocol>, strict<D::Lifetime>, strict<D::Provenance>, gr::trust_tested,
                      strict<D::Representation>, strict<D::Observability>, strict<D::Complexity>, strict<D::Precision>,
                      strict<D::Space>, strict<D::Overflow>, strict<D::Mutation>, strict<D::Reentrancy>,
                      strict<D::Size>, strict<D::Version>, strict<D::Staleness>, strict<D::Synchronization>,
                      strict<D::Regime>, strict<D::FpMode>, strict<D::SyscallSurface>, strict<D::ControlFlow>,
                      strict<D::CallShape>, strict<D::StackUse>, strict<D::GlobalState>, strict<D::Stdio>,
                      strict<D::HwInstruction>, strict<D::BarrierStrength>, strict<D::SimdIsa>, strict<D::MemoryScope>>;
static_assert(std::is_same_v<typename fn_t::trust_t, crucible::safety::trust::Tested>,
              "trust_tested must resolve Trust to safety::trust::Tested.");
}  // namespace lat_trust_tested

namespace lat_trust_unverified {
using fn_t = fixy::fn<int, strict<D::Refinement>, strict<D::Usage>, strict<D::Effect>, strict<D::Security>,
                      strict<D::Protocol>, strict<D::Lifetime>, strict<D::Provenance>, gr::trust_unverified,
                      strict<D::Representation>, strict<D::Observability>, strict<D::Complexity>, strict<D::Precision>,
                      strict<D::Space>, strict<D::Overflow>, strict<D::Mutation>, strict<D::Reentrancy>,
                      strict<D::Size>, strict<D::Version>, strict<D::Staleness>, strict<D::Synchronization>,
                      strict<D::Regime>, strict<D::FpMode>, strict<D::SyscallSurface>, strict<D::ControlFlow>,
                      strict<D::CallShape>, strict<D::StackUse>, strict<D::GlobalState>, strict<D::Stdio>,
                      strict<D::HwInstruction>, strict<D::BarrierStrength>, strict<D::SimdIsa>, strict<D::MemoryScope>>;
static_assert(std::is_same_v<typename fn_t::trust_t, crucible::safety::trust::Unverified>,
              "trust_unverified must resolve Trust to safety::trust::Unverified.");
}  // namespace lat_trust_unverified

namespace lat_trust_external {
using fn_t = fixy::fn<int, strict<D::Refinement>, strict<D::Usage>, strict<D::Effect>, strict<D::Security>,
                      strict<D::Protocol>, strict<D::Lifetime>, strict<D::Provenance>, gr::trust_external,
                      strict<D::Representation>, strict<D::Observability>, strict<D::Complexity>, strict<D::Precision>,
                      strict<D::Space>, strict<D::Overflow>, strict<D::Mutation>, strict<D::Reentrancy>,
                      strict<D::Size>, strict<D::Version>, strict<D::Staleness>, strict<D::Synchronization>,
                      strict<D::Regime>, strict<D::FpMode>, strict<D::SyscallSurface>, strict<D::ControlFlow>,
                      strict<D::CallShape>, strict<D::StackUse>, strict<D::GlobalState>, strict<D::Stdio>,
                      strict<D::HwInstruction>, strict<D::BarrierStrength>, strict<D::SimdIsa>, strict<D::MemoryScope>>;
static_assert(std::is_same_v<typename fn_t::trust_t, crucible::safety::trust::External>,
              "trust_external must resolve Trust to safety::trust::External.");
}  // namespace lat_trust_external

// Two relaxations in one binding must not interfere: each axis
// resolves to its own tag, not to the other tag's slot.

namespace lat_cross_secret_external {
using fn_t = fixy::fn<int, strict<D::Refinement>, strict<D::Usage>, strict<D::Effect>, gr::as_secret,
                      strict<D::Protocol>, strict<D::Lifetime>, strict<D::Provenance>, gr::trust_external,
                      strict<D::Representation>, strict<D::Observability>, strict<D::Complexity>, strict<D::Precision>,
                      strict<D::Space>, strict<D::Overflow>, strict<D::Mutation>, strict<D::Reentrancy>,
                      strict<D::Size>, strict<D::Version>, strict<D::Staleness>, strict<D::Synchronization>,
                      strict<D::Regime>, strict<D::FpMode>, strict<D::SyscallSurface>, strict<D::ControlFlow>,
                      strict<D::CallShape>, strict<D::StackUse>, strict<D::GlobalState>, strict<D::Stdio>,
                      strict<D::HwInstruction>, strict<D::BarrierStrength>, strict<D::SimdIsa>, strict<D::MemoryScope>>;
static_assert(fn_t::security_v == crucible::safety::fn::SecLevel::Secret,
              "A trust relaxation must leave the security pin intact.");
static_assert(std::is_same_v<typename fn_t::trust_t, crucible::safety::trust::External>,
              "A security relaxation must leave the trust pin intact.");
}  // namespace lat_cross_secret_external

// Each tag is an empty final type, so the binding collapses to the
// size of its payload however many tags it carries.
static_assert(sizeof(lat_sec_secret::fn_t) == sizeof(int), "fixy::fn with as_secret must EBO-collapse to sizeof(int).");
static_assert(sizeof(lat_trust_external::fn_t) == sizeof(int),
              "fixy::fn with trust_external must EBO-collapse to sizeof(int).");
static_assert(sizeof(lat_cross_secret_external::fn_t) == sizeof(int),
              "fixy::fn with as_secret + trust_external must EBO-collapse.");

int main() { return 0; }
