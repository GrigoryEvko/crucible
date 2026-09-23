#pragma once

// The tag namespaces of the provenance, trust, access, version and
// policy axes.  A tag is an empty type that a wrapper carries as a
// phantom argument, so this header holds declarations and nothing that
// runs.  The retag catalog that admits a transition between two tags,
// the roster of the policy tags and the completeness check over it
// belong to the wrappers that consume them (fixy/Tagged.h and
// fixy/Secret.h).  The composition law of the architecture
// pins, arch_compatible, is defined beside the collision rules that
// read it (fixy/Collision.h).
//
// Old spellings: include/crucible/safety/Tagged.h (source, trust,
// access, version, vessel_trust), include/crucible/safety/source/Path.h
// and Arch.h (the path and architecture members of source),
// include/crucible/safety/Secret.h (secret_policy) and
// include/crucible/Types.h:188 (hash_family), reached from the old
// fixy tree through the aliases of include/crucible/fixy/Source.h.

namespace fixy::tags {

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

// Paths supplied by an interactive operator: argv entries, REPL input, stdin.
// The most adversarial input class, because the operator can construct a path
// specifically to escape the intended sandbox.
struct FromUserPath {};

// Paths read from environment variables. The environment may be inherited from
// a parent process Crucible does not own, so the bytes stay untrusted even when
// a deployment harness is expected to set them.
struct FromEnvPath {};

// Paths drawn from operator-authored configuration files. A structured parser
// normally applies a schema check first, so a sanitize policy for this lane can
// rely on malformed input already having been rejected. The bytes are still
// external.
struct FromConfigPath {};

// Paths assembled inside Crucible from an already-sanitized storage root and a
// hex-formatted content hash. The bytes never crossed an untrusted boundary, so
// the directory-anchored open helpers trust a value carrying this tag. Keeping
// this tag out of the three external lanes is what stops operator-supplied
// bytes from reaching those helpers.
struct CipherPath {};

// The CPU instruction-set trunk a value is pinned to. A value acquires a pin
// when it carries the result of a fence or a hand-vectorized computation, both
// of which are spelled per trunk.
enum class ArchTag : unsigned char {
    X86 = 0,  // mfence/lfence/sfence, SSE through AVX-512
    Arm = 1,  // DMB ISH, NEON and SVE
    Portable = 2,  // no trunk constraint
};

template <ArchTag Arch>
struct ArchPinned {
    static constexpr ArchTag arch = Arch;
};

using X86Pinned = ArchPinned<ArchTag::X86>;
using ArmPinned = ArchPinned<ArchTag::Arm>;
using PortablePinned = ArchPinned<ArchTag::Portable>;
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

// Searching for a declassification by its policy tag only enumerates
// every escape from classification if the policy universe is closed.
// A convention alone would not close it, because an ad-hoc struct
// declared anywhere would serve as a policy and the search would miss
// the site. The marker base below is what makes the set closed, and
// each tag is final so that no subclass can launder the audit trail
// through subtype coercion.
namespace secret_policy {

// Empty, and exists only to be detected. Every policy inherits it.
struct secret_policy_base {};

struct AuditedLogging final : secret_policy_base {};  // log with audit trail
struct WireSerialize final : secret_policy_base {};  // encrypted-channel serialization
struct HashForCompare final : secret_policy_base {};  // release as hash (not the source)
struct LengthOnly final : secret_policy_base {};  // release only size metadata
struct UserDisplay final : secret_policy_base {};  // display in UI (e.g., last-4 of card)

// A declassification policy has to match the axis it discharges. The
// policies above authorize an export channel or a relaxation of
// confidentiality, and neither says anything about how stale a value
// may be. Using one of them to admit a replay window would be an
// over-broad discharge, so the temporal axis gets its own tag.
struct AuthorizedReplay final : secret_policy_base {};  // admits a bounded replay window

}  // namespace secret_policy

namespace hash_family {
struct FamilyA {};
struct FamilyB {};
}  // namespace hash_family

}  // namespace fixy::tags
