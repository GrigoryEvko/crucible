// ── test_fixy_lattice_points — positive-compile lattice coverage ───
//
// Positive-compile witnesses that every reachable lattice point on
// the Security and Trust axes is wired through a named grant tag and
// projects to the expected substrate slot.
//
// Background: misc/16_05_2026_fixy.md §4 ships per-axis relaxation
// tags for each DimensionAxis.  Phase A shipped the Security axis
// with ONE relaxation (`declassify<Policy>` → SecLevel::Public) and
// the Trust axis with ONE relaxation (`trust_assumed<Rationale>` →
// safety::trust::Assumed).  Other lattice points were unreachable —
// callers could not pin Security to Secret/Internal/Unclassified or
// Trust to Tested/Unverified/External through a single grant tag.
//
// FIXY-LAT-Security + FIXY-LAT-Trust add the missing tags:
//
//   Security: as_unclassified / as_public / as_internal / as_classified /
//             as_secret  (5 tags spanning every SecLevel enumerator)
//   Trust:    trust_verified / trust_tested / trust_unverified /
//             trust_external  (4 tags spanning every safety::trust::*)
//
// This TU exercises each new tag in a full 20-engaged fixy::fn<...>
// binding and proves the resolver projects to the matching substrate
// slot.  Negative-compile fixtures live in test/fixy_neg/.

#include <crucible/fixy/Fn.h>

#include <type_traits>

namespace fixy = crucible::fixy;
namespace gr   = crucible::fixy::grant;
using D        = crucible::fixy::dim::DimensionAxis;

template <D Axis>
using strict = gr::accept_default_strict_for<Axis>;

// ─── Security lattice — every SecLevel reachable ───────────────────
//
// Replace the strict<D::Security> marker in an otherwise-strict pack
// with each new tag, then read back the resolved Fn's security_v.

namespace lat_sec_unclassified {
using fn_t = fixy::fn<int,
    strict<D::Refinement>, strict<D::Usage>, strict<D::Effect>,
    gr::as_unclassified,
    strict<D::Protocol>, strict<D::Lifetime>, strict<D::Provenance>,
    strict<D::Trust>, strict<D::Representation>, strict<D::Observability>,
    strict<D::Complexity>, strict<D::Precision>, strict<D::Space>,
    strict<D::Overflow>, strict<D::Mutation>, strict<D::Reentrancy>,
    strict<D::Size>, strict<D::Version>, strict<D::Staleness>, strict<D::Synchronization>, strict<D::Regime>,
    strict<D::FpMode>, strict<D::SyscallSurface>, strict<D::ControlFlow>, strict<D::CallShape>, strict<D::StackUse>, strict<D::GlobalState>, strict<D::Stdio>, strict<D::HwInstruction>, strict<D::BarrierStrength>, strict<D::SimdIsa>>;
static_assert(fn_t::security_v == crucible::safety::fn::SecLevel::Unclassified,
    "as_unclassified must resolve Security to SecLevel::Unclassified.");
}  // namespace lat_sec_unclassified

namespace lat_sec_public {
using fn_t = fixy::fn<int,
    strict<D::Refinement>, strict<D::Usage>, strict<D::Effect>,
    gr::as_public,
    strict<D::Protocol>, strict<D::Lifetime>, strict<D::Provenance>,
    strict<D::Trust>, strict<D::Representation>, strict<D::Observability>,
    strict<D::Complexity>, strict<D::Precision>, strict<D::Space>,
    strict<D::Overflow>, strict<D::Mutation>, strict<D::Reentrancy>,
    strict<D::Size>, strict<D::Version>, strict<D::Staleness>, strict<D::Synchronization>, strict<D::Regime>,
    strict<D::FpMode>, strict<D::SyscallSurface>, strict<D::ControlFlow>, strict<D::CallShape>, strict<D::StackUse>, strict<D::GlobalState>, strict<D::Stdio>, strict<D::HwInstruction>, strict<D::BarrierStrength>, strict<D::SimdIsa>>;
static_assert(fn_t::security_v == crucible::safety::fn::SecLevel::Public,
    "as_public must resolve Security to SecLevel::Public.");
}  // namespace lat_sec_public

namespace lat_sec_internal {
using fn_t = fixy::fn<int,
    strict<D::Refinement>, strict<D::Usage>, strict<D::Effect>,
    gr::as_internal,
    strict<D::Protocol>, strict<D::Lifetime>, strict<D::Provenance>,
    strict<D::Trust>, strict<D::Representation>, strict<D::Observability>,
    strict<D::Complexity>, strict<D::Precision>, strict<D::Space>,
    strict<D::Overflow>, strict<D::Mutation>, strict<D::Reentrancy>,
    strict<D::Size>, strict<D::Version>, strict<D::Staleness>, strict<D::Synchronization>, strict<D::Regime>,
    strict<D::FpMode>, strict<D::SyscallSurface>, strict<D::ControlFlow>, strict<D::CallShape>, strict<D::StackUse>, strict<D::GlobalState>, strict<D::Stdio>, strict<D::HwInstruction>, strict<D::BarrierStrength>, strict<D::SimdIsa>>;
static_assert(fn_t::security_v == crucible::safety::fn::SecLevel::Internal,
    "as_internal must resolve Security to SecLevel::Internal.");
}  // namespace lat_sec_internal

namespace lat_sec_classified {
using fn_t = fixy::fn<int,
    strict<D::Refinement>, strict<D::Usage>, strict<D::Effect>,
    gr::as_classified,
    strict<D::Protocol>, strict<D::Lifetime>, strict<D::Provenance>,
    strict<D::Trust>, strict<D::Representation>, strict<D::Observability>,
    strict<D::Complexity>, strict<D::Precision>, strict<D::Space>,
    strict<D::Overflow>, strict<D::Mutation>, strict<D::Reentrancy>,
    strict<D::Size>, strict<D::Version>, strict<D::Staleness>, strict<D::Synchronization>, strict<D::Regime>,
    strict<D::FpMode>, strict<D::SyscallSurface>, strict<D::ControlFlow>, strict<D::CallShape>, strict<D::StackUse>, strict<D::GlobalState>, strict<D::Stdio>, strict<D::HwInstruction>, strict<D::BarrierStrength>, strict<D::SimdIsa>>;
static_assert(fn_t::security_v == crucible::safety::fn::SecLevel::Classified,
    "as_classified must resolve Security to SecLevel::Classified — "
    "matching the substrate strict default.");
}  // namespace lat_sec_classified

namespace lat_sec_secret {
using fn_t = fixy::fn<int,
    strict<D::Refinement>, strict<D::Usage>, strict<D::Effect>,
    gr::as_secret,
    strict<D::Protocol>, strict<D::Lifetime>, strict<D::Provenance>,
    strict<D::Trust>, strict<D::Representation>, strict<D::Observability>,
    strict<D::Complexity>, strict<D::Precision>, strict<D::Space>,
    strict<D::Overflow>, strict<D::Mutation>, strict<D::Reentrancy>,
    strict<D::Size>, strict<D::Version>, strict<D::Staleness>, strict<D::Synchronization>, strict<D::Regime>,
    strict<D::FpMode>, strict<D::SyscallSurface>, strict<D::ControlFlow>, strict<D::CallShape>, strict<D::StackUse>, strict<D::GlobalState>, strict<D::Stdio>, strict<D::HwInstruction>, strict<D::BarrierStrength>, strict<D::SimdIsa>>;
static_assert(fn_t::security_v == crucible::safety::fn::SecLevel::Secret,
    "as_secret must resolve Security to SecLevel::Secret.");
}  // namespace lat_sec_secret

// ─── Trust lattice — every safety::trust::* reachable ──────────────

namespace lat_trust_verified {
using fn_t = fixy::fn<int,
    strict<D::Refinement>, strict<D::Usage>, strict<D::Effect>,
    strict<D::Security>, strict<D::Protocol>, strict<D::Lifetime>,
    strict<D::Provenance>,
    gr::trust_verified,
    strict<D::Representation>, strict<D::Observability>,
    strict<D::Complexity>, strict<D::Precision>, strict<D::Space>,
    strict<D::Overflow>, strict<D::Mutation>, strict<D::Reentrancy>,
    strict<D::Size>, strict<D::Version>, strict<D::Staleness>, strict<D::Synchronization>, strict<D::Regime>,
    strict<D::FpMode>, strict<D::SyscallSurface>, strict<D::ControlFlow>, strict<D::CallShape>, strict<D::StackUse>, strict<D::GlobalState>, strict<D::Stdio>, strict<D::HwInstruction>, strict<D::BarrierStrength>, strict<D::SimdIsa>>;
static_assert(std::is_same_v<typename fn_t::trust_t,
                             crucible::safety::trust::Verified>,
    "trust_verified must resolve Trust to safety::trust::Verified.");
}  // namespace lat_trust_verified

namespace lat_trust_tested {
using fn_t = fixy::fn<int,
    strict<D::Refinement>, strict<D::Usage>, strict<D::Effect>,
    strict<D::Security>, strict<D::Protocol>, strict<D::Lifetime>,
    strict<D::Provenance>,
    gr::trust_tested,
    strict<D::Representation>, strict<D::Observability>,
    strict<D::Complexity>, strict<D::Precision>, strict<D::Space>,
    strict<D::Overflow>, strict<D::Mutation>, strict<D::Reentrancy>,
    strict<D::Size>, strict<D::Version>, strict<D::Staleness>, strict<D::Synchronization>, strict<D::Regime>,
    strict<D::FpMode>, strict<D::SyscallSurface>, strict<D::ControlFlow>, strict<D::CallShape>, strict<D::StackUse>, strict<D::GlobalState>, strict<D::Stdio>, strict<D::HwInstruction>, strict<D::BarrierStrength>, strict<D::SimdIsa>>;
static_assert(std::is_same_v<typename fn_t::trust_t,
                             crucible::safety::trust::Tested>,
    "trust_tested must resolve Trust to safety::trust::Tested.");
}  // namespace lat_trust_tested

namespace lat_trust_unverified {
using fn_t = fixy::fn<int,
    strict<D::Refinement>, strict<D::Usage>, strict<D::Effect>,
    strict<D::Security>, strict<D::Protocol>, strict<D::Lifetime>,
    strict<D::Provenance>,
    gr::trust_unverified,
    strict<D::Representation>, strict<D::Observability>,
    strict<D::Complexity>, strict<D::Precision>, strict<D::Space>,
    strict<D::Overflow>, strict<D::Mutation>, strict<D::Reentrancy>,
    strict<D::Size>, strict<D::Version>, strict<D::Staleness>, strict<D::Synchronization>, strict<D::Regime>,
    strict<D::FpMode>, strict<D::SyscallSurface>, strict<D::ControlFlow>, strict<D::CallShape>, strict<D::StackUse>, strict<D::GlobalState>, strict<D::Stdio>, strict<D::HwInstruction>, strict<D::BarrierStrength>, strict<D::SimdIsa>>;
static_assert(std::is_same_v<typename fn_t::trust_t,
                             crucible::safety::trust::Unverified>,
    "trust_unverified must resolve Trust to safety::trust::Unverified.");
}  // namespace lat_trust_unverified

namespace lat_trust_external {
using fn_t = fixy::fn<int,
    strict<D::Refinement>, strict<D::Usage>, strict<D::Effect>,
    strict<D::Security>, strict<D::Protocol>, strict<D::Lifetime>,
    strict<D::Provenance>,
    gr::trust_external,
    strict<D::Representation>, strict<D::Observability>,
    strict<D::Complexity>, strict<D::Precision>, strict<D::Space>,
    strict<D::Overflow>, strict<D::Mutation>, strict<D::Reentrancy>,
    strict<D::Size>, strict<D::Version>, strict<D::Staleness>, strict<D::Synchronization>, strict<D::Regime>,
    strict<D::FpMode>, strict<D::SyscallSurface>, strict<D::ControlFlow>, strict<D::CallShape>, strict<D::StackUse>, strict<D::GlobalState>, strict<D::Stdio>, strict<D::HwInstruction>, strict<D::BarrierStrength>, strict<D::SimdIsa>>;
static_assert(std::is_same_v<typename fn_t::trust_t,
                             crucible::safety::trust::External>,
    "trust_external must resolve Trust to safety::trust::External.");
}  // namespace lat_trust_external

// ─── Cross-axis coexistence — relaxations on Security and Trust ────

namespace lat_cross_secret_external {
using fn_t = fixy::fn<int,
    strict<D::Refinement>, strict<D::Usage>, strict<D::Effect>,
    gr::as_secret,
    strict<D::Protocol>, strict<D::Lifetime>, strict<D::Provenance>,
    gr::trust_external,
    strict<D::Representation>, strict<D::Observability>,
    strict<D::Complexity>, strict<D::Precision>, strict<D::Space>,
    strict<D::Overflow>, strict<D::Mutation>, strict<D::Reentrancy>,
    strict<D::Size>, strict<D::Version>, strict<D::Staleness>, strict<D::Synchronization>, strict<D::Regime>,
    strict<D::FpMode>, strict<D::SyscallSurface>, strict<D::ControlFlow>, strict<D::CallShape>, strict<D::StackUse>, strict<D::GlobalState>, strict<D::Stdio>, strict<D::HwInstruction>, strict<D::BarrierStrength>, strict<D::SimdIsa>>;
static_assert(fn_t::security_v == crucible::safety::fn::SecLevel::Secret,
    "Cross-axis: as_secret + trust_external still pins Security to Secret.");
static_assert(std::is_same_v<typename fn_t::trust_t,
                             crucible::safety::trust::External>,
    "Cross-axis: as_secret + trust_external still pins Trust to External.");
}  // namespace lat_cross_secret_external

// EBO collapse on the new tags — fixy::fn<T, ...all-strict-except-new>
// must still be sizeof(T) since the new tags are empty + final +
// grant_base (1-byte each, EBO-collapsed in the wrapper).
static_assert(sizeof(lat_sec_secret::fn_t)        == sizeof(int),
    "fixy::fn with as_secret must EBO-collapse to sizeof(int).");
static_assert(sizeof(lat_trust_external::fn_t)    == sizeof(int),
    "fixy::fn with trust_external must EBO-collapse to sizeof(int).");
static_assert(sizeof(lat_cross_secret_external::fn_t) == sizeof(int),
    "fixy::fn with as_secret + trust_external must EBO-collapse.");

int main() { return 0; }
