#pragma once

// ── crucible::safety::Tagged<T, Tag> ────────────────────────────────
//
// Phantom-type wrapper attaching a compile-time tag to a value.  Used
// for provenance tracking, trust level, access mode, and schema
// version — all zero-cost type discrimination.
//
//   Axiom coverage: TypeSafe (code_guide §II).
//   Runtime cost:   zero.  sizeof(Tagged<T, Tag>) == sizeof(T).
//
// Tag namespaces provided:
//   source::*  — provenance: FromUser, FromDb, FromConfig, FromInternal,
//                External (untrusted input), ABIBoundary, Sanitized,
//                IntegrityVerified.
//   trust::*   — verification status: Verified, Tested, Unverified,
//                Assumed, External.
//   access::*  — access mode (register / column / field semantics):
//                RW, RO, WO, W1C, W1S, WriteOnce, AppendOnly, Unique,
//                AutoIncrement, Deprecated.
//   version::* — schema version tagging: V1, V2, V3, ...
//
// Retagging is explicit via .retag<NewTag>().  Unrelated Tagged types
// do not implicitly convert, so a function demanding
// Tagged<T, source::Sanitized> will not accept Tagged<T, source::External>.
//
// ── MIGRATED to Graded<RelativeMonad, TrustLattice<Tag>, T>  (#464) ─
//
// As of MIGRATE-4 (2026-04-26) Tagged<T, Tag> is a thin wrapper
// around the algebraic primitive
//
//   Graded<ModalityKind::RelativeMonad,
//          TrustLattice<Tag>,
//          T>
//
// per misc/25_04_2026.md §2.3.  The wrapper preserves every existing
// public API surface (value() / value_mut() / retag() / into() /
// implicit deduction guide).  Storage is delegated to Graded; the
// lattice element_type is empty (TrustLattice<Tag>'s singleton tag
// at type level) and EBO collapses both grade_ and the wrapper
// itself, so sizeof(Tagged<T, Tag>) == sizeof(T) is preserved by
// structural guarantee — same as pre-migration.
//
// Per the Graded storage-regime taxonomy (memory rule
// feedback_graded_storage_regimes), this is regime #1: empty grade
// via EBO.  Same shape as Linear and Refined.
//
// MUTATION via value_mut() forwards to Graded::peek_mut(), which is
// gated by `requires (AbsoluteModality<M> || std::is_empty_v<grade
// _type>)`.  Tagged is RelativeMonad modality, but TrustLattice
// <Source>::element_type is empty — the second clause of the gate
// admits the call.  See Graded.h's "REFINED GATE" comment.
// ───────────────────────────────────────────────────────────────────
//
// Pattern: cross every trust boundary with a source:: tag; every
// verified fact with trust::Verified; every schema-versioned structure
// with version::V<N>.

#include <crucible/Platform.h>
#include <crucible/algebra/Graded.h>
#include <crucible/algebra/lattices/TrustLattice.h>

#include <string_view>
#include <type_traits>
#include <utility>

namespace crucible::safety {

namespace source {
    struct FromUser     {};
    struct FromDb       {};
    struct FromConfig   {};
    struct FromInternal {};
    struct External     {};  // raw untrusted input (network, FFI)
    struct ABIBoundary  {};  // opaque value crossing a C ABI / FFI boundary
    struct Sanitized    {};  // validated, safe to pass to sanitized-only APIs
    struct FormatVersion {}; // in-process format/version constant
    struct Loaded       {};  // loaded from validated serialized state
    struct Interned     {};  // canonicalized by an interning owner
    struct Arena        {};  // arena-owned object pointer/reference
    struct Singleton    {};  // process-global singleton accessor result
    struct Recorded     {};  // produced from live RECORD-mode tracing
    struct Replayed     {};  // reconstructed from replay/Cipher state
    // Durable: loaded from on-disk state (Cipher, config, snapshots).
    // Computed: derived at startup / runtime from Durable + inputs.
    // The pair lets a reader distinguish "this came from disk" from "this
    // is a computation result" at the type level — useful when init code
    // mixes both and a reviewer needs to see which is load-bearing.
    struct Durable      {};
    struct Computed     {};
    // Vendor: hardware-vendor-supplied attributes — model strings,
    // firmware/BIOS revisions, microarchitecture identifiers reported
    // by the device itself (PCIe config space, BMC, SMBIOS, vendor
    // ioctl).  Distinct from FromConfig (operator-supplied) and
    // FromInternal (computed locally) because vendor truth needs its
    // own provenance lane: it can be wrong (vendor bug, counterfeit
    // hardware), it lags the wire (firmware updates change behavior
    // before the metadata advertises it), and it crosses a real trust
    // boundary (driver / firmware code path Crucible doesn't own).
    // Cog identity (cog/CogIdentity.h, GAPS-185) is the canonical
    // consumer.
    struct Vendor       {};
    // Calibrated: measured at startup or runtime by Crucible's own
    // calibration pass against real silicon — cross-checked against
    // Vendor truth and used as the authoritative source when the two
    // disagree (e.g. Vendor reports tflops_fp16 from datasheet,
    // Calibrated reports the actual achieved number on this die at
    // this thermal headroom).  Per-Cog TargetCaps (GAPS-186) split
    // into Vendor-tagged vs Calibrated-tagged subsets so the planner
    // can refuse to schedule against unverified vendor claims.
    struct Calibrated   {};
    // Hlc: timestamps minted by canopy/Hybrid Logical Clock state.
    // Distinct from External timestamps received from peers: received
    // bytes must be admitted explicitly before they can drive CRDT /
    // Cipher ordering decisions.
    struct Hlc          {};
    // Local / Gossiped: CRDT provenance lanes.  Local marks writes
    // authored by this replica; Gossiped marks state received from
    // anti-entropy / Scuttlebutt exchange and admitted at the CRDT
    // merge boundary.
    struct Local        {};
    struct Gossiped     {};
    // SwimMember: CogIdentity admitted into the SWIM membership view.
    // Raw discovery output must be admitted explicitly before it can
    // drive peer-health or gossip fanout decisions.
    struct SwimMember   {};
    // HyParView: CogIdentity admitted into the bounded active/passive
    // Canopy overlay. Raw discovery output and foreign membership tags
    // cannot directly drive overlay repair or Plumtree fanout.
    struct HyParView    {};
    // IntegrityVerified: CNT-P payload whose end-to-end xxHash64 trailer
    // has been recomputed and matched at the receiver.  Raw wire bytes
    // and merely gossiped payloads cannot substitute for this tag.
    struct IntegrityVerified {};
    // JsonRegistry: recipe/catalog rows admitted from Crucible's
    // embedded or loaded JSON registry. Distinct from FromConfig
    // because registry-origin values drive deterministic recipe
    // selection and must not be substituted by arbitrary user strings
    // or ad-hoc diagnostic spans.
    struct JsonRegistry {};
    // CcAlgorithm: CNT-P congestion-control selection admitted through
    // cntp/CongestionControl.h. Raw enum values and raw kernel strings
    // cannot directly drive per-socket TCP_CONGESTION changes.
    struct CcAlgorithm {};
    // QdiscConfig: CNT-P queueing-discipline and pacing configuration
    // admitted through cntp/Pacing.h. Raw interface strings / qdisc
    // names cannot directly drive pacing policy.
    struct QdiscConfig {};
    // IncastConfig: CNT-P fan-in mitigation configuration admitted through
    // cntp/IncastControl.h. Raw booleans / byte counts cannot directly
    // tune socket RTO or receiver-issued credit pacing.
    struct IncastConfig {};
    // RoceConfig: CNT-P RoCEv2 fabric configuration admitted through
    // cntp/RoceConfig.h. Raw PFC masks, DSCP values, and DCQCN knobs
    // cannot directly drive privileged NIC/fabric policy.
    struct RoceConfig {};
    // Mtls: CNT-P mutual-TLS policy and authenticated peer identity
    // admitted through cntp/MtlsTransport.h. Raw certificate bytes,
    // DNS names, cipher selections, and peer fingerprints cannot
    // directly drive federation transport identity.
    struct Mtls {};
    // KtlsOffloaded: CNT-P kernel-TLS offload intent admitted through
    // cntp/KtlsOffload.h. Raw sockets and raw TLS traffic keys cannot
    // directly program NIC/kernel TLS state.
    struct KtlsOffloaded {};
    // AdmissionDecision: CNT-P backpressure/admission-control decisions
    // minted by rt/Backpressure.h. Raw accept/reject structs cannot
    // cross runtime boundaries as operator-visible admission outcomes.
    struct AdmissionDecision {};
    // ConnectionPool: CNT-P connection lease/reuse events minted by
    // rt/ConnectionPool.h. Raw pool events cannot substitute for the
    // runtime-owned lease audit surface.
    struct ConnectionPool {};
    // Pingmesh: topology latency measurements admitted by
    // topology/Pingmesh.h. Raw UDP/probe outcomes cannot directly
    // update fleet latency histograms or anomaly reports.
    struct Pingmesh {};
    // Ptp: timestamp / clock-status facts admitted by topology/Ptp.h.
    // Raw clock_gettime values, packet timestamps, and integer file
    // descriptors cannot directly seed PTP-sensitive consumers.
    struct Ptp {};
    // PathSwap: CNT-P application-level path-swap plan admitted through
    // cntp/PathSwap.h. Raw path IDs cannot directly drive a live
    // SessionHandle resource transition.
    struct PathSwap {};
    // TcpInfo: congestion telemetry admitted from Linux TCP_INFO /
    // TCP_CC_INFO or an explicitly tagged synthetic test source.
    // Raw counters cannot directly drive topology congestion policy.
    struct TcpInfo {};
    // AfXdp: CNT-P AF_XDP socket / UMEM configuration admitted through
    // cntp/AfXdp.h. Raw ring sizes, frame sizes, queue IDs, and interface
    // names cannot directly mint a zero-copy transport surface.
    struct AfXdp {};
    // Xdp / BpfMap: runtime-owned BPF/XDP plans admitted through rt/Xdp.h.
    // Raw program descriptors or map dimensions cannot directly attach a
    // NIC dataplane program or allocate a userspace-visible map surface.
    struct Xdp {};
    struct BpfMap {};
    // GossipMulticast: CNT-P XDP_TX multicast plans admitted through
    // cntp/GossipMulticast.h. Raw topic hashes, neighbor arrays, and XDP
    // descriptors cannot directly drive kernel-side gossip replication.
    struct GossipMulticast {};
    // TcEbpf: runtime-owned TC direct-action eBPF plans admitted through
    // rt/TcEbpf.h. Raw skb action descriptors, DSCP values, and map specs
    // cannot directly attach an egress/ingress TC dataplane program.
    struct TcEbpf {};
    // OverlayMulticast: CNT-P application-layer multicast plans admitted
    // through cntp/OverlayMulticast.h. Raw CogIdentity values and unbounded
    // stripe/tree plans cannot directly drive cross-peer fanout.
    struct OverlayMulticast {};
}

namespace trust {
    struct Verified   {};  // proved by SMT / type system / test
    struct Tested     {};  // covered by tests but not formally verified
    struct Unverified {};  // no formal coverage
    struct Assumed    {};  // axiom / mathematical assumption
    struct External   {};  // trust delegated to outside source
}

namespace access {
    struct RW            {};  // unrestricted
    struct RO            {};  // read-only (writes rejected)
    struct WO            {};  // write-only (reads rejected)
    struct W1C           {};  // write-1-to-clear (HW registers)
    struct W1S           {};  // write-1-to-set (HW registers)
    struct WriteOnce     {};  // written exactly once, then read-only
    struct AppendOnly    {};  // add only, never remove
    struct Unique        {};  // globally unique across instances
    struct AutoIncrement {};  // system-assigned (DB columns)
    struct Deprecated    {};  // accessible but warns about removal
}

namespace version {
    template <unsigned N> struct V { static constexpr unsigned number = N; };
}

// Vessel-boundary provenance: values crossing from Python / PyTorch /
// any foreign runtime carry FromPytorch until validated by Vessel-side
// code, at which point they are retagged to Validated.  Internal paths
// that record / compile / replay require Validated at their entry
// points; FromPytorch cannot substitute for Validated — the type system
// rejects the call.
//
// Internal code (tests, synthetic drivers, replay engines that fabricate
// Entry values) may construct Tagged<T, Validated> directly.  Audit by
// grep for `vessel_trust::Validated` — anything outside of validator
// functions or known-trusted internal constructors is a review concern.
namespace vessel_trust {
    struct FromPytorch {};  // raw uint64_t / pointer / scalar from the FFI
    struct Validated   {};  // Vessel-side validation produced a well-formed value
}

template <typename T, typename Tag>
class [[nodiscard]] Tagged {
public:
    using value_type = T;
    using tag_type   = Tag;
    using lattice_type = ::crucible::algebra::lattices::TrustLattice<Tag>;
    // Modality declaration — Round-4 CHEAT-5; see Linear.h for the
    // rationale.  Tagged is RelativeMonad — provenance flows
    // monadically with the inner T (retag is a relative-monad map).
    static constexpr ::crucible::algebra::ModalityKind modality =
        ::crucible::algebra::ModalityKind::RelativeMonad;
    // Public per GRADED-TRAIT-1 — see Linear.h for the rationale.
    using graded_type  = ::crucible::algebra::Graded<
        ::crucible::algebra::ModalityKind::RelativeMonad, lattice_type, T>;

private:
    // Empty-lattice grade_type collapses via [[no_unique_address]] in
    // Graded; impl_ is sizeof(T).  Wrapper adds no other state.
    graded_type impl_;

public:

    constexpr explicit Tagged(T v)
        noexcept(std::is_nothrow_move_constructible_v<T>)
        : impl_{std::move(v), typename lattice_type::element_type{}} {}

    Tagged(const Tagged&)            = default;
    Tagged(Tagged&&)                 = default;
    Tagged& operator=(const Tagged&) = default;
    Tagged& operator=(Tagged&&)      = default;
    ~Tagged()                        = default;

    // Read-only access — forwards through Graded::peek().
    [[nodiscard]] constexpr const T& value() const noexcept { return impl_.peek(); }

    // Mutable access — forwards through Graded::peek_mut(), admitted
    // by the refined gate `(AbsoluteModality || empty grade)`.
    // TrustLattice<Tag> has empty element_type, so the second clause
    // satisfies even though Tagged is RelativeMonad modality.
    [[nodiscard]] constexpr T& value_mut() noexcept { return impl_.peek_mut(); }

    // Retagging is explicit — produces a new Tagged with a new tag.
    // The phantom Tag template parameter changes; the value moves
    // through.  Underlying storage / modality / lattice element shape
    // is identical (different Tag, same TrustLattice<...>::element_type
    // singleton), so the move is zero-cost.
    template <typename NewTag>
    [[nodiscard]] constexpr Tagged<T, NewTag> retag() &&
        noexcept(std::is_nothrow_move_constructible_v<T>)
    {
        return Tagged<T, NewTag>{std::move(impl_).consume()};
    }

    // Underlying-value extraction.  Use for re-wrapping or for known
    // trusted internal paths.  Forwards through Graded::consume() —
    // rvalue-this consumes the inner value.
    [[nodiscard]] constexpr T into() &&
        noexcept(std::is_nothrow_move_constructible_v<T>)
    {
        return std::move(impl_).consume();
    }

    // ── Diagnostic names (forwarded from Graded substrate) ─────────
    //
    // value_type_name(): T's display string via reflection (P2996R13).
    // lattice_name(): "TrustLattice<Tag>" — the provenance lattice.
    //
    // Audit-Tier-2 cross-wrapper parity — every migrated wrapper
    // ships these two consteval forwarders so diagnostic emission
    // can introspect uniformly.  See Linear.h's matching block for
    // the full rationale.
    [[nodiscard]] static consteval std::string_view value_type_name() noexcept {
        return graded_type::value_type_name();
    }
    [[nodiscard]] static consteval std::string_view lattice_name() noexcept {
        return graded_type::lattice_name();
    }
};

// Zero-cost guarantee: phantom Tag is template parameter, not a member.
static_assert(sizeof(Tagged<int,   source::FromUser>)  == sizeof(int));
static_assert(sizeof(Tagged<void*, trust::Verified>)   == sizeof(void*));
static_assert(sizeof(Tagged<long,  access::AppendOnly>) == sizeof(long));

} // namespace crucible::safety
