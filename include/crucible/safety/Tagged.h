#pragma once

#include <crucible/Platform.h>
#include <crucible/algebra/Graded.h>
#include <crucible/algebra/lattices/TrustLattice.h>

#include <cstdlib>
#include <string_view>
#include <tuple>
#include <type_traits>
#include <utility>

namespace crucible::safety {

namespace source {
struct FromUser {};
struct FromDb {};
struct FromConfig {};
struct FromInternal {};
struct External {};  // raw untrusted input (network, FFI)
struct ABIBoundary {};  // opaque value crossing a C ABI / FFI boundary
struct Sanitized {};  // validated, safe to pass to sanitized-only APIs
struct FormatVersion {};  // in-process format/version constant
struct Loaded {};  // loaded from validated serialized state
struct Interned {};  // canonicalized by an interning owner
struct Arena {};  // arena-owned object pointer/reference
struct Singleton {};  // process-global singleton accessor result
struct Recorded {};  // produced from live RECORD-mode tracing
struct Replayed {};  // reconstructed from replay/Cipher state
// Durable came from disk. Computed was derived at startup or at run
// time from Durable state plus inputs. The pair lets a reader tell
// the two apart where init code mixes them.
struct Durable {};
struct Computed {};
// Hardware-vendor-supplied attributes: model strings, firmware
// revisions, microarchitecture identifiers the device reports about
// itself. Separate from FromConfig, which is operator-supplied, and
// FromInternal, which is computed locally, because vendor truth can
// be wrong, it lags behavior when firmware changes before the
// metadata advertises it, and it crosses a driver or firmware
// boundary Crucible does not own.
struct Vendor {};
// Measured against real silicon by Crucible's own calibration pass.
// Authoritative where it disagrees with Vendor, which reports
// datasheet numbers rather than what this die achieves at this
// thermal headroom.
struct Calibrated {};
// Timestamps minted by hybrid-logical-clock state. Separate from an
// External timestamp received from a peer, which must be admitted
// explicitly before it can drive ordering decisions.
struct Hlc {};
// Local marks a write authored by this replica. Gossiped marks state
// received by anti-entropy exchange and admitted at the merge
// boundary.
struct Local {};
struct Gossiped {};
// Identity admitted into the SWIM membership view. Raw discovery
// output must be admitted explicitly before it can drive peer-health
// or gossip-fanout decisions.
struct SwimMember {};
// Identity admitted into the bounded active and passive overlay. Raw
// discovery output and foreign membership tags cannot drive overlay
// repair or broadcast fanout.
struct HyParView {};
// Broadcast messages and repair summaries admitted at the broadcast
// tree boundary. Raw message identifiers and overlay peer identities
// cannot drive eager and lazy tree transitions.
struct Plumtree {};
// Payload whose end-to-end hash trailer was recomputed at the
// receiver and matched. Raw wire bytes and merely gossiped payloads
// cannot substitute.
struct IntegrityVerified {};
// Recipe and catalog rows admitted from the embedded or loaded JSON
// registry. Separate from FromConfig because registry rows drive
// deterministic recipe selection and must not be substituted by
// arbitrary user strings.
struct JsonRegistry {};
// Network-kernel recipe constraints. Raw booleans and ad-hoc policy
// tables cannot choose a collective algorithm.
struct NetworkRecipeRegistry {};
// Op nodes admitted by the IR001 substrate. Raw op descriptors cannot
// cross into phase visitors or serialization boundaries.
struct Ir001 {};
// Congestion-control selection. Raw enum values and raw kernel
// strings cannot drive a per-socket congestion change.
struct CcAlgorithm {};
// Queueing-discipline and pacing configuration. Raw interface strings
// and qdisc names cannot drive pacing policy.
struct QdiscConfig {};
// Pointer borrowed from a fixed-capacity ring's inline slot storage.
// The pointee lives inside the ring's own array and stays valid for
// the ring's lifetime, because the ring deletes copy and move so the
// interior pointer never dangles through relocation. Distinct from
// Arena and from a lifetime-scoped borrow: Ring also records which
// ring's slot pool the pointer came from.
struct Ring {};
// Pointer published by the background region pipeline, valid for the
// lifetime of that background thread. A holder can rely on the
// publishing release store happening before any dereference of the
// pointee. Distinct from Arena and Ring in naming which pipeline
// produced the pointer.
struct Vigil {};
// Hardware-capability value measured against real silicon by the
// startup calibration pass. The value is opaque and vendor-encoded,
// so a holder can rely on the measurement having happened but not on
// any particular range. Distinct from Calibrated, which covers a
// calibration result from any source at any time.
struct Meridian {};
// Pointer to a cached region's trace-entry array. It stays valid only
// while the cached region is alive, and a weak reference held
// alongside it is what bounds that envelope. The tag records where
// the pointer came from and does not itself prevent a dangling read.
struct RegionOps {};
// Fan-in mitigation configuration. Raw booleans and byte counts
// cannot tune a socket retransmission timeout or receiver credit.
struct IncastConfig {};
// RoCEv2 fabric configuration. Raw PFC masks, DSCP values, and DCQCN
// knobs cannot drive privileged NIC or fabric policy.
struct RoceConfig {};
// Mutual-TLS policy and authenticated peer identity. Raw certificate
// bytes, DNS names, cipher selections, and peer fingerprints cannot
// drive federation transport identity.
struct Mtls {};
// Backpressure and admission-control decisions. Raw accept and reject
// structs cannot cross runtime boundaries as operator-visible
// outcomes.
struct AdmissionDecision {};
// Connection lease and reuse events. Raw pool events cannot
// substitute for the runtime-owned lease audit surface.
struct ConnectionPool {};
// Topology latency measurements. Raw probe outcomes cannot update
// fleet latency histograms or anomaly reports.
struct Pingmesh {};
// Timestamp and clock-status facts. Raw clock reads, packet
// timestamps, and integer file descriptors cannot seed a
// precision-time consumer.
struct Ptp {};
// Application-level path-swap plan. Raw path identifiers cannot drive
// a live session resource transition.
struct PathSwap {};
// Congestion telemetry admitted from the kernel TCP information
// interface, or from an explicitly tagged synthetic test source. Raw
// counters cannot drive topology congestion policy.
struct TcpInfo {};
// NIC telemetry admitted from kernel-visible counters: sysfs netdev
// statistics, qdisc backlog, sysctl snapshots, hwmon temperatures.
// Raw text cannot drive capacity, health, or routing decisions.
struct KernelTelemetry {};
// AF_XDP socket and UMEM configuration. Raw ring sizes, frame sizes,
// queue identifiers, and interface names cannot mint a zero-copy
// transport surface.
struct AfXdp {};
// Dataplane BPF and XDP plans. Raw program descriptors and map
// dimensions cannot attach a NIC dataplane program or allocate a
// userspace-visible map.
struct Xdp {};
struct BpfMap {};
// Multicast plans driven from the transmit path. Raw topic hashes,
// neighbor arrays, and descriptors cannot drive kernel-side gossip
// replication.
struct GossipMulticast {};
// Traffic-control direct-action eBPF plans. Raw action descriptors,
// DSCP values, and map specifications cannot attach an egress or
// ingress dataplane program.
struct TcEbpf {};
// Application-layer multicast plans. Raw identities and unbounded
// stripe or tree plans cannot drive cross-peer fanout.
struct OverlayMulticast {};
// Result value that survived redundant execution comparison. Raw
// results and externally tagged values cannot substitute for the
// post-comparison evidence.
struct SdcVerified {};
// NIC configuration intent admitted at the hardware boundary. Raw
// ring sizes, queue counts, qdisc kinds, sysctl byte counts, and
// congestion strings cannot drive privileged NIC mutation.
struct NicConfig {};
// Virtual-function partitioning intent admitted at the physical NIC
// boundary. Raw VF counts, MAC addresses, VLAN identifiers, and QoS
// knobs cannot drive privileged mutation.
struct SrIov {};
// Hardware ACL and flow-steering table intent. Raw identities and
// unbounded capacity requests cannot allocate hardware TCAM state.
struct TcamTable {};
// Hardware ACL and flow-steering rules. Raw five-tuples and actions
// cannot program a NIC or switch TCAM table.
struct TcamFlowRule {};

// Names the pipeline phase that produced the value. Distinct from
// Ir001 and NetworkRecipeRegistry, which are entry-boundary
// admission tags: this one is the in-pipeline lane that flows
// along the phase chain, so a consumer demanding a later phase
// rejects a value from an earlier one.
template <char Phase>
struct ForgePhase {
    static_assert(Phase >= 'A' && Phase <= 'L', "source::ForgePhase<P>: P must be one of A..L "
                                                "(Forge 12-phase pipeline: A=INGEST, B=ANALYZE, "
                                                "C=REWRITE, D=FUSE, E=LOWER_TO_KERNELS, F=TILE, "
                                                "G=MEMPLAN, H=COMPILE, I=SCHEDULE, J=EMIT, "
                                                "K=DISTRIBUTE, L=VALIDATE).");
};

// Marks a payload that has actually transited a transport of the
// named posture. Distinct from CcAlgorithm, QdiscConfig, Mtls and
// AfXdp, which admit raw transport configuration: this one travels
// with the value, so a sink demanding one posture rejects a payload
// carrying another.
enum class TransportPostureTag : unsigned char {
    LowLatency = 0,
    BulkData = 1,
    Reliable = 2,
    UnreliableMulticast = 3,
};
template <TransportPostureTag Posture>
struct TransportPosture {};

// A per-call parallelism recommendation minted by the workload
// profiler. The tag is the proof of origin a dispatch routine needs:
// without it a caller could synthesize a free-standing decision and
// bypass the profiler's cache-tier reasoning. Distinct from
// Calibrated and Meridian, which mark measurements rather than
// per-call recommendations.
struct WorkloadProfiler {};
}  // namespace source

namespace trust {
struct Verified {};  // proved by SMT / type system / test
struct Tested {};  // covered by tests but not formally verified
struct Unverified {};  // no formal coverage
struct Assumed {};  // axiom / mathematical assumption
struct External {};  // trust delegated to outside source
}  // namespace trust

namespace access {
struct RW {};  // unrestricted
struct RO {};  // read-only (writes rejected)
struct WO {};  // write-only (reads rejected)
struct W1C {};  // write-1-to-clear (HW registers)
struct W1S {};  // write-1-to-set (HW registers)
struct WriteOnce {};  // written exactly once, then read-only
struct AppendOnly {};  // add only, never remove
struct Unique {};  // globally unique across instances
struct AutoIncrement {};  // system-assigned (DB columns)
struct Deprecated {};  // accessible but warns about removal
}  // namespace access

namespace version {
template <unsigned N>
struct V {
    static constexpr unsigned number = N;
};
}  // namespace version

// Internal code may construct a Validated value directly, so the tag
// is only as strong as the review of its construction sites.
namespace vessel_trust {
struct FromPytorch {};  // raw uint64_t / pointer / scalar from the FFI
struct Validated {};  // Vessel-side validation produced a well-formed value
}  // namespace vessel_trust

// Every tag transition is rejected unless an explicit specialization
// admits it. Fail-closed is the point: an open-by-default policy would
// silently permit laundering untrusted input into a Sanitized tag,
// which is exactly the bug the phantom axis exists to catch. Closing
// by default makes the catalog below the single source of truth.
//
// The primary template, the identity specialization and the concept
// must precede class Tagged, whose retag() requires-clause names the
// concept. The rest of the catalog can follow the class, because
// specialization lookup happens at the instantiation site.
template <typename From, typename To>
struct retag_policy {
    static constexpr bool allowed = false;
};

// Identity is a no-op transition, admitted unconditionally so generic
// code that re-asserts the tag it already holds does not trip the gate.
template <typename Tag>
struct retag_policy<Tag, Tag> {
    static constexpr bool allowed = true;
};

template <typename From, typename To>
concept RetagAllowed = retag_policy<From, To>::allowed;

template <typename T, typename Tag>
class [[nodiscard]] Tagged {
public:
    using value_type = T;
    using tag_type = Tag;
    using lattice_type = ::crucible::algebra::lattices::TrustLattice<Tag>;
    static constexpr ::crucible::algebra::ModalityKind modality = ::crucible::algebra::ModalityKind::RelativeMonad;
    using graded_type = ::crucible::algebra::Graded<::crucible::algebra::ModalityKind::RelativeMonad, lattice_type, T>;

private:
    graded_type impl_;

public:
    // THIS CONSTRUCTOR IS PUBLIC, AND THAT IS THE WHOLE CONTRACT.
    //
    // Tagged carries PROVENANCE, not VALIDATION.  `Tagged<T, Src>`
    // asserts that somebody wrote the tag Src on this value.  It does
    // not assert that the value passed any check, because nothing here
    // runs one.  Any caller can write `Tagged<T, Src>{raw}` around a
    // hand-built aggregate and get a value indistinguishable from one
    // a validating factory produced.
    //
    // That matters most for the `Declared*` alias family, whose name
    // reads like a guarantee.  There are 51 such aliases.  27 of them
    // have a factory that genuinely validates — it returns
    // std::expected and short-circuits on a failed predicate — and for
    // those 27 this constructor is a bypass around a real check.  21
    // have only a pass-through factory that wraps without checking, so
    // the name is the entire guarantee.  3 (DeclaredPeerSet,
    // DeclaredPingmeshMeasurement, DeclaredGossipMulticastPlan) have no
    // factory at all.  Reading any `Declared*` as evidence of
    // validation is therefore wrong for at least 24 of the 51, and is
    // only conditionally right for the rest.
    //
    // The constructor stays public deliberately.  Closing it would
    // reach 27 factories that need friending, 11 member-default-init
    // sites that need a default-constructible carve-out, and 7
    // negative-compile fixtures that forge a tag on purpose to prove a
    // downstream API rejects it — those would then fail because the
    // constructor is inaccessible, which is not the property under
    // test.  It would also push validation semantics into a wrapper
    // that is equally used for pure provenance (source::Arena,
    // source::Interned, source::Recorded), where a factory is ceremony.
    //
    // When a value must carry a CHECKED property rather than a
    // provenance mark, the type for that is Refined<Pred, T> — its
    // constructor evaluates Pred.  Compose the two when both are
    // wanted: Tagged<Refined<Pred, T>, Src>.
    constexpr explicit Tagged(T v) noexcept(std::is_nothrow_move_constructible_v<T>)
        : impl_{std::move(v), typename lattice_type::element_type{}} {}

    // A default constructor weakens the provenance discipline, so it
    // exists only to let an array of slots start empty before any of
    // them is filled. The explicit constructor stays the only way to
    // materialise a non-default value, which keeps every
    // provenance-bearing construction site a deliberate call.
    constexpr Tagged() noexcept(std::is_nothrow_default_constructible_v<T>)
        requires std::default_initializable<T>
    = default;

    Tagged(const Tagged&) = default;
    Tagged(Tagged&&) = default;
    Tagged& operator=(const Tagged&) = default;
    Tagged& operator=(Tagged&&) = default;
    ~Tagged() = default;

    [[nodiscard]] constexpr const T& value() const noexcept { return impl_.peek(); }

    // The substrate admits mutation when the modality is absolute or
    // the grade is empty. This wrapper is relative-monad, so it is the
    // empty grade of TrustLattice that satisfies the gate.
    [[nodiscard]] constexpr T& value_mut() noexcept { return impl_.peek_mut(); }

    template <typename NewTag>
        requires RetagAllowed<Tag, NewTag>
    [[nodiscard]] constexpr Tagged<T, NewTag> retag() && noexcept(std::is_nothrow_move_constructible_v<T>) {
        return Tagged<T, NewTag>{std::move(impl_).consume()};
    }

    [[nodiscard]] constexpr T into() && noexcept(std::is_nothrow_move_constructible_v<T>) {
        return std::move(impl_).consume();
    }

    [[nodiscard]] static consteval std::string_view value_type_name() noexcept {
        return graded_type::value_type_name();
    }
    [[nodiscard]] static consteval std::string_view lattice_name() noexcept { return graded_type::lattice_name(); }
};

static_assert(sizeof(Tagged<int, source::FromUser>) == sizeof(int));
static_assert(sizeof(Tagged<void*, trust::Verified>) == sizeof(void*));
static_assert(sizeof(Tagged<long, access::AppendOnly>) == sizeof(long));

// These two tags must never be specialized. They exist only to witness
// the fail-closed default, and they sit outside every production tag
// namespace so application code cannot reach them.
namespace detail::retag_policy_test {
struct NeverFrom {};
struct NeverTo {};
}  // namespace detail::retag_policy_test

// The sentinel pair, rather than a production pair, is what makes the
// assertions below witness a structural property. A production pair
// would only witness that the catalog has not yet grown that edge, and
// would red the day it does.
static_assert(retag_policy<source::FromUser, source::FromUser>::allowed,
              "retag_policy identity specialization must admit (X → X)");
static_assert(!retag_policy<detail::retag_policy_test::NeverFrom, detail::retag_policy_test::NeverTo>::allowed,
              "retag_policy primary template MUST be fail-closed for any "
              "(From → To) pair without an explicit specialization");
static_assert(RetagAllowed<source::FromUser, source::FromUser>, "RetagAllowed concept must admit identity");
static_assert(!RetagAllowed<detail::retag_policy_test::NeverFrom, detail::retag_policy_test::NeverTo>,
              "RetagAllowed concept must reject unspecialized transitions");

// Each specialization below admits exactly one transition, and each
// carries the name of the validator that discharges it. That validator
// is the safety-bearing component. The retag is only the type-level
// record that it ran.
//
// Adding a cell here is a security review. The discipline has three
// parts. Group by tag family and never admit a cross-family edge,
// because laundering across orthogonal axes confounds what the phantom
// means. List shorter transitions first. Leave every inverse direction
// to the fail-closed primary template.
//
// Trust is a one-way ratchet. Assumed is a sibling precondition that
// discharges into Verified once the assumption is checked.

template <>
struct retag_policy<trust::Unverified, trust::Tested> {
    // Discharge: test suite ran and passed against this value.
    static constexpr bool allowed = true;
};
template <>
struct retag_policy<trust::Unverified, trust::Verified> {
    // Discharge: formal proof / cryptographic verification completed.
    static constexpr bool allowed = true;
};
template <>
struct retag_policy<trust::Tested, trust::Verified> {
    // Discharge: proof obligation discharged on top of test coverage.
    static constexpr bool allowed = true;
};
template <>
struct retag_policy<trust::Assumed, trust::Verified> {
    // Discharge: the precondition it rested on is now checked.
    static constexpr bool allowed = true;
};
template <>
struct retag_policy<trust::Unverified, trust::Assumed> {
    // Discharge: an axiom statement was authored about this value.
    // The retag site names the holder of that responsibility.
    static constexpr bool allowed = true;
};

template <>
struct retag_policy<source::External, source::Sanitized> {
    // Discharge: input sanitizer at the trust boundary accepted bytes.
    static constexpr bool allowed = true;
};
template <>
struct retag_policy<source::External, source::IntegrityVerified> {
    // Discharge: end-to-end integrity check (xxHash64 trailer, etc.)
    // recomputed at receiver and matched the wire value.
    static constexpr bool allowed = true;
};
template <>
struct retag_policy<source::Sanitized, source::IntegrityVerified> {
    // Discharge: sanitized value additionally passes integrity check.
    // The two predicates compose (sanitized AND integrity-verified).
    static constexpr bool allowed = true;
};
template <>
struct retag_policy<source::FromUser, source::Sanitized> {
    // Discharge: user-supplied value passed the input sanitizer.
    static constexpr bool allowed = true;
};
template <>
struct retag_policy<source::Recorded, source::Loaded> {
    // Discharge: the recording pipeline closed the trace, which
    // admits the value into validated persistent state.
    static constexpr bool allowed = true;
};

template <>
struct retag_policy<source::FromDb, source::Sanitized> {
    // Discharge: the row passed schema validation, which enforces
    // well-formedness of every field before the row is admitted.
    static constexpr bool allowed = true;
};
template <>
struct retag_policy<source::FromConfig, source::Sanitized> {
    // Discharge: the config parser ran its schema and range check at
    // load time.
    static constexpr bool allowed = true;
};
template <>
struct retag_policy<source::ABIBoundary, source::Sanitized> {
    // Discharge: the adapter marshalling the call ran the sanitizer on
    // the opaque value before admitting it.
    static constexpr bool allowed = true;
};
template <>
struct retag_policy<source::Loaded, source::IntegrityVerified> {
    // Discharge: the persisted value additionally passed the
    // integrity-check predicate. The two claims compose.
    static constexpr bool allowed = true;
};
template <>
struct retag_policy<source::Replayed, source::Loaded> {
    // Discharge: replay produced a deterministic value matching the
    // recorded checkpoint. This is the read side of the same discharge
    // that Recorded to Loaded gates on the write side.
    static constexpr bool allowed = true;
};
template <>
struct retag_policy<source::Recorded, source::IntegrityVerified> {
    // Discharge: the recorded value additionally passed the integrity
    // check taken at trace close.
    static constexpr bool allowed = true;
};

template <>
struct retag_policy<vessel_trust::FromPytorch, vessel_trust::Validated> {
    // Discharge: the adapter's well-formedness checks ran on the input.
    static constexpr bool allowed = true;
};

static_assert(retag_policy<trust::Unverified, trust::Tested>::allowed,
              "trust::Unverified → trust::Tested must be admitted");
static_assert(retag_policy<trust::Unverified, trust::Verified>::allowed,
              "trust::Unverified → trust::Verified must be admitted");
static_assert(retag_policy<trust::Tested, trust::Verified>::allowed,
              "trust::Tested → trust::Verified must be admitted");
static_assert(retag_policy<trust::Assumed, trust::Verified>::allowed,
              "trust::Assumed → trust::Verified must be admitted");
static_assert(retag_policy<trust::Unverified, trust::Assumed>::allowed,
              "trust::Unverified → trust::Assumed must be admitted: promoting no "
              "claim to an axiom-level claim, as Unverified → Tested does for "
              "test coverage");

// Every forward cell is paired with an inverse witness so a reviewer
// who later adds a specialization "by symmetry" cannot quietly admit
// a downgrade.
static_assert(!retag_policy<trust::Verified, trust::Unverified>::allowed,
              "trust::Verified → trust::Unverified would erase the proof");
static_assert(!retag_policy<trust::Verified, trust::Tested>::allowed,
              "trust:: catalog is one-way; Verified → Tested is a downgrade");
static_assert(!retag_policy<trust::Tested, trust::Unverified>::allowed,
              "trust::Tested → trust::Unverified would erase test coverage");
static_assert(!retag_policy<trust::Verified, trust::Assumed>::allowed,
              "trust::Verified → trust::Assumed would downgrade discharged proof "
              "back to a mere assumption");
static_assert(!retag_policy<trust::Assumed, trust::Unverified>::allowed,
              "trust::Assumed → trust::Unverified would erase the axiom-author "
              "responsibility claim recorded at the Unverified → Assumed retag "
              "site");

static_assert(retag_policy<source::External, source::Sanitized>::allowed,
              "source::External → source::Sanitized must be admitted");
static_assert(retag_policy<source::External, source::IntegrityVerified>::allowed,
              "source::External → source::IntegrityVerified must be admitted");
static_assert(retag_policy<source::Sanitized, source::IntegrityVerified>::allowed,
              "source::Sanitized → source::IntegrityVerified must be admitted");
static_assert(retag_policy<source::FromUser, source::Sanitized>::allowed,
              "source::FromUser → source::Sanitized must be admitted");
static_assert(retag_policy<source::Recorded, source::Loaded>::allowed,
              "source::Recorded → source::Loaded must be admitted");
static_assert(retag_policy<source::FromDb, source::Sanitized>::allowed,
              "source::FromDb → source::Sanitized must be admitted");
static_assert(retag_policy<source::FromConfig, source::Sanitized>::allowed,
              "source::FromConfig → source::Sanitized must be admitted");
static_assert(retag_policy<source::ABIBoundary, source::Sanitized>::allowed,
              "source::ABIBoundary → source::Sanitized must be admitted");
static_assert(retag_policy<source::Loaded, source::IntegrityVerified>::allowed,
              "source::Loaded → source::IntegrityVerified must be admitted");
static_assert(retag_policy<source::Replayed, source::Loaded>::allowed,
              "source::Replayed → source::Loaded must be admitted");
static_assert(retag_policy<source::Recorded, source::IntegrityVerified>::allowed,
              "source::Recorded → source::IntegrityVerified must be admitted");

static_assert(!retag_policy<source::Sanitized, source::External>::allowed,
              "source::Sanitized → source::External would reintroduce taint");
static_assert(!retag_policy<source::IntegrityVerified, source::External>::allowed,
              "source::IntegrityVerified → source::External would erase integrity");
static_assert(!retag_policy<source::Loaded, source::Recorded>::allowed,
              "source::Loaded → source::Recorded would unwind admitted state");
static_assert(!retag_policy<source::IntegrityVerified, source::Sanitized>::allowed,
              "source::IntegrityVerified → source::Sanitized would erase the "
              "additional integrity-check guarantee, downgrading to merely sanitized");
static_assert(!retag_policy<source::Sanitized, source::FromUser>::allowed,
              "source::Sanitized → source::FromUser would re-introduce taint by "
              "regressing a validated value to raw user-supplied provenance");
static_assert(!retag_policy<source::Sanitized, source::FromDb>::allowed,
              "source::Sanitized → source::FromDb would re-introduce taint by "
              "regressing a validated value to raw DB-row provenance");
static_assert(!retag_policy<source::Sanitized, source::FromConfig>::allowed,
              "source::Sanitized → source::FromConfig would re-introduce taint by "
              "regressing a validated value to raw config-file provenance");
static_assert(!retag_policy<source::Sanitized, source::ABIBoundary>::allowed,
              "source::Sanitized → source::ABIBoundary would re-introduce taint by "
              "regressing a validated value to raw FFI-boundary provenance");
static_assert(!retag_policy<source::IntegrityVerified, source::Loaded>::allowed,
              "source::IntegrityVerified → source::Loaded would erase the additional "
              "integrity-check guarantee");
static_assert(!retag_policy<source::Loaded, source::Replayed>::allowed,
              "source::Loaded → source::Replayed would unwind admitted state back to "
              "a replay transient");
static_assert(!retag_policy<source::IntegrityVerified, source::Recorded>::allowed,
              "source::IntegrityVerified → source::Recorded would erase the "
              "integrity-check guarantee, downgrading to merely-recorded state");

static_assert(retag_policy<vessel_trust::FromPytorch, vessel_trust::Validated>::allowed,
              "vessel_trust::FromPytorch → vessel_trust::Validated must be admitted");
static_assert(!retag_policy<vessel_trust::Validated, vessel_trust::FromPytorch>::allowed,
              "vessel_trust::Validated → FromPytorch would erase well-formedness");

static_assert(RetagAllowed<source::External, source::Sanitized>, "RetagAllowed concept admits catalog transitions");
static_assert(RetagAllowed<trust::Unverified, trust::Verified>, "RetagAllowed concept admits trust escalation");
static_assert(!RetagAllowed<source::External, trust::Verified>,
              "Cross-axis transition (source::* to trust::*) stays rejected — "
              "laundering across orthogonal axes is never safe");
static_assert(!RetagAllowed<source::FromUser, access::WriteOnce>,
              "Cross-axis transition (source::* to access::*) stays rejected");

// To extend the catalog, three things move in lockstep: the
// specialization with its discharge, a positive and an inverse
// sentinel, and this count plus the roster below. Anything less reds
// at compile time.
inline constexpr std::size_t kAdmittedForwardEdgeCount = 17;

inline constexpr bool kCatalogRosterMatchesCount =
    RetagAllowed<trust::Unverified, trust::Tested> && RetagAllowed<trust::Unverified, trust::Verified>
    && RetagAllowed<trust::Tested, trust::Verified> && RetagAllowed<trust::Assumed, trust::Verified>
    && RetagAllowed<trust::Unverified, trust::Assumed> && RetagAllowed<source::External, source::Sanitized>
    && RetagAllowed<source::External, source::IntegrityVerified>
    && RetagAllowed<source::Sanitized, source::IntegrityVerified> && RetagAllowed<source::FromUser, source::Sanitized>
    && RetagAllowed<source::Recorded, source::Loaded> && RetagAllowed<source::FromDb, source::Sanitized>
    && RetagAllowed<source::FromConfig, source::Sanitized> && RetagAllowed<source::ABIBoundary, source::Sanitized>
    && RetagAllowed<source::Loaded, source::IntegrityVerified> && RetagAllowed<source::Replayed, source::Loaded>
    && RetagAllowed<source::Recorded, source::IntegrityVerified>
    && RetagAllowed<vessel_trust::FromPytorch, vessel_trust::Validated>;

static_assert(kCatalogRosterMatchesCount, "retag_policy catalog roster check failed.  Either "
                                          "a specialization was deleted from this file (and the corresponding "
                                          "RetagAllowed<> entry in kCatalogRosterMatchesCount above must be "
                                          "removed plus kAdmittedForwardEdgeCount decremented), or one of the "
                                          "named edges is mis-spelled or its specialization is missing.");

// The boolean fold above cannot be counted, so the roster is restated
// as a tuple whose size can be. Without this pin the named count
// drifts from the fold silently.
using kCatalogRosterTuple =
    std::tuple<std::pair<trust::Unverified, trust::Tested>, std::pair<trust::Unverified, trust::Verified>,
               std::pair<trust::Tested, trust::Verified>, std::pair<trust::Assumed, trust::Verified>,
               std::pair<trust::Unverified, trust::Assumed>, std::pair<source::External, source::Sanitized>,
               std::pair<source::External, source::IntegrityVerified>,
               std::pair<source::Sanitized, source::IntegrityVerified>, std::pair<source::FromUser, source::Sanitized>,
               std::pair<source::Recorded, source::Loaded>, std::pair<source::FromDb, source::Sanitized>,
               std::pair<source::FromConfig, source::Sanitized>, std::pair<source::ABIBoundary, source::Sanitized>,
               std::pair<source::Loaded, source::IntegrityVerified>, std::pair<source::Replayed, source::Loaded>,
               std::pair<source::Recorded, source::IntegrityVerified>,
               std::pair<vessel_trust::FromPytorch, vessel_trust::Validated>>;
static_assert(std::tuple_size_v<kCatalogRosterTuple> == kAdmittedForwardEdgeCount,
              "kAdmittedForwardEdgeCount must match the kCatalogRosterTuple "
              "cardinality.  Drift between the named count and the structured "
              "roster fires here: bump the count, extend the tuple, and extend "
              "kCatalogRosterMatchesCount in lockstep.");

// mint_tagged is the §XXI-named factory, but it is NOT true that one
// `mint_` search finds every authorization point in the tree.  The
// factories that produce a Declared* value are also named declare_*,
// admit_*, validate_*, plan_*, query_*, and several one-offs
// (recommend_cc, eligibility_check, fallback_dispatch, open_stream,
// ptp_status_from_daemon_report).  A `mint_` grep finds roughly half
// of them.  Constructing a Tagged directly is likewise legal and
// escapes any such search — see the constructor's comment above for
// why that stays true by design.  To enumerate the authorization
// surface, search the alias names (`Declared`), not the factory
// prefix.

template <typename Tag>
concept ValidTaggedTag = std::is_class_v<Tag>;

template <typename Tag, typename T>
    requires ValidTaggedTag<Tag>
[[nodiscard]] constexpr Tagged<T, Tag> mint_tagged(T value) noexcept(std::is_nothrow_move_constructible_v<T>) {
    return Tagged<T, Tag>{std::move(value)};
}

namespace detail::tagged_self_test {

inline void runtime_smoke_test() {
    int seed = 7;

    Tagged<int, source::FromUser> u{seed * 6};
    if (u.value() != 42) std::abort();

    auto um = mint_tagged<source::FromUser, int>(seed * 6);
    if (um.value() != 42) std::abort();

    u.value_mut() = 100;
    if (u.value() != 100) std::abort();

    Tagged<int, source::Sanitized> s = std::move(u).template retag<source::Sanitized>();
    if (s.value() != 100) std::abort();

    int extracted = std::move(s).into();
    if (extracted != 100) std::abort();

    Tagged<long, trust::Verified> v{seed * seed};
    Tagged<long, trust::Unverified> uv{seed * seed};
    if (v.value() != uv.value()) std::abort();

    Tagged<int, version::V<3>> vt{seed};
    if (vt.value() != 7) std::abort();

    Tagged<int, vessel_trust::FromPytorch> raw{seed};
    auto validated = std::move(raw).template retag<vessel_trust::Validated>();
    if (validated.value() != 7) std::abort();
}

}  // namespace detail::tagged_self_test

}  // namespace crucible::safety
