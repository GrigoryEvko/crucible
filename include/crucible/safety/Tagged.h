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

#include <cstdlib>
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
    // Plumtree: broadcast messages and repair summaries admitted through
    // canopy/Plumtree.h. Raw message IDs and HyParView peer identities
    // cannot directly drive eager/lazy tree state transitions.
    struct Plumtree     {};
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
    // NetworkRecipeRegistry: Forge network-kernel recipe constraints
    // admitted through forge/recipes/Network.h. Raw booleans or
    // ad-hoc policy tables cannot directly choose collective
    // algorithms at RecipeSelect boundaries.
    struct NetworkRecipeRegistry {};
    // Ir001: Forge IR001 op nodes admitted by forge/Ir001/* substrate.
    // Raw op descriptors cannot cross into Forge phase visitors or
    // serialization boundaries without this provenance tag.
    struct Ir001 {};
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
    // AdmissionDecision: CNT-P backpressure/admission-control decisions
    // minted by cntp/BackpressureRuntime.h. Raw accept/reject structs cannot
    // cross runtime boundaries as operator-visible admission outcomes.
    struct AdmissionDecision {};
    // ConnectionPool: CNT-P connection lease/reuse events minted by
    // cntp/ConnectionPoolRuntime.h. Raw pool events cannot substitute for the
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
    // KernelTelemetry: NIC telemetry admitted from Linux kernel-visible
    // counters such as sysfs netdev stats, qdisc backlog, sysctl snapshots,
    // and hwmon temperature readings. Raw text cannot directly drive
    // topology capacity, health, or routing decisions.
    struct KernelTelemetry {};
    // AfXdp: CNT-P AF_XDP socket / UMEM configuration admitted through
    // cntp/AfXdp.h. Raw ring sizes, frame sizes, queue IDs, and interface
    // names cannot directly mint a zero-copy transport surface.
    struct AfXdp {};
    // Xdp / BpfMap: CNT-P dataplane BPF/XDP plans admitted through
    // cntp/dataplane/Xdp.h.
    // Raw program descriptors or map dimensions cannot directly attach a
    // NIC dataplane program or allocate a userspace-visible map surface.
    struct Xdp {};
    struct BpfMap {};
    // GossipMulticast: CNT-P XDP_TX multicast plans admitted through
    // cntp/GossipMulticast.h. Raw topic hashes, neighbor arrays, and XDP
    // descriptors cannot directly drive kernel-side gossip replication.
    struct GossipMulticast {};
    // TcEbpf: CNT-P dataplane TC direct-action eBPF plans admitted through
    // cntp/dataplane/TcEbpf.h. Raw skb action descriptors, DSCP values, and map specs
    // cannot directly attach an egress/ingress TC dataplane program.
    struct TcEbpf {};
    // OverlayMulticast: CNT-P application-layer multicast plans admitted
    // through cntp/OverlayMulticast.h. Raw CogIdentity values and unbounded
    // stripe/tree plans cannot directly drive cross-peer fanout.
    struct OverlayMulticast {};
    // SdcVerified: observe/SdcDetect.h result value that survived redundant
    // execution comparison. Raw operation results and externally-tagged
    // values cannot substitute for the post-comparison evidence.
    struct SdcVerified {};
    // NicConfig: cog/NicConfig.h ethtool/sysctl/qdisc configuration intent
    // admitted at the hardware-Cog boundary. Raw ring sizes, queue counts,
    // qdisc kinds, sysctl byte counts, and TCP congestion strings cannot
    // directly drive privileged NIC mutation.
    struct NicConfig {};
    // SrIov: cog/SrIov.h virtual-function partitioning intent admitted at
    // the physical NIC boundary. Raw VF counts, MACs, VLAN ids, and QoS
    // knobs cannot directly drive privileged SR-IOV mutation.
    struct SrIov {};
    // TcamTable: CNT-P hardware ACL / flow-steering table intent admitted
    // through cntp/Tcam.h. Raw Cog identities and unbounded capacity
    // requests cannot directly allocate hardware TCAM state.
    struct TcamTable {};
    // TcamFlowRule: CNT-P hardware ACL / flow-steering rules admitted
    // through cntp/Tcam.h. Raw five-tuples and actions cannot directly
    // program NIC or switch TCAM tables.
    struct TcamFlowRule {};
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

// ── retag_policy<From, To> — phantom-transition opt-in (FIXY-V-022) ─
//
// Primary template: every (From → To) phantom-tag transition is
// REJECTED by default.  Safe transitions are admitted by EXPLICIT
// specialization (FIXY-V-023) — the discipline is fail-closed,
// review-discoverable, and grep-able by `retag_policy<` at every
// authoritative trust-boundary mutation.
//
// Why fail-closed: `Tagged<T, Tag>` carries provenance / trust / access
// / version / vessel-trust phantoms across boundaries that the type
// system otherwise can't see — laundering External-tagged user input
// to Sanitized at a glance is exactly the bug we want the compiler to
// catch.  An open-by-default policy (silent permitting) would defeat
// the whole point of the phantom axis.  A fail-closed policy makes the
// safe-transition catalog (V-023) the single source of truth.
//
// Layout note (V-024): the primary template + identity specialization
// + `RetagAllowed` concept live BEFORE `class Tagged` because
// `Tagged<T, Tag>::retag<NewTag>()` has a requires-clause referencing
// the concept by unqualified name.  The V-023 catalog of additional
// specializations and the V-022 sentinel-tag witnesses live AFTER the
// class — specialization lookup is performed at the instantiation
// site, so adding catalog cells later in the TU works.
//
// Axiom coverage:
//   TypeSafe — the phantom-tag transition is a type-level fact; the
//              policy admits it at compile time, not by runtime tag-
//              compare.
//   InitSafe — no runtime state; `allowed` is a constexpr bool.
//
// Cost: zero.  `RetagAllowed<A, B>` is a constraint check during
// overload resolution; no symbol, no storage, no runtime test.
template <typename From, typename To>
struct retag_policy {
    // Default: NO transition admitted.  Explicit specialization
    // required for every safe direction.  V-023 lands the catalog
    // (External → Sanitized → IntegrityVerified, FromPytorch →
    // Validated, etc.) BELOW the class declaration.
    static constexpr bool allowed = false;
};

// Identity is always safe — `Tagged<T, X>` to `Tagged<T, X>` is a
// no-op transition (same phantom).  Admitted unconditionally so that
// generic code that re-asserts the existing tag (template recursion,
// concept satisfaction tests) doesn't trip the gate.
template <typename Tag>
struct retag_policy<Tag, Tag> {
    static constexpr bool allowed = true;
};

// Concept-form gate used at call sites.  V-024 pins this onto
// `Tagged::retag<NewTag>() requires RetagAllowed<Tag, NewTag>`.
template <typename From, typename To>
concept RetagAllowed = retag_policy<From, To>::allowed;

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
    //
    // FIXY-V-024: gated by `RetagAllowed<Tag, NewTag>` — V-022's
    // concept consults the V-023 catalog of admitted phantom-tag
    // transitions.  Every retag site is reviewed against the catalog
    // (which lives below the class definition); transitions not in
    // the catalog are rejected at compile time with a constraint
    // diagnostic.  Identity (X → X) is always admitted by V-022's
    // identity specialization, so generic code that re-asserts the
    // existing tag does not trip the gate.
    template <typename NewTag>
        requires RetagAllowed<Tag, NewTag>
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

// ── Sentinel tag pair for fail-closed witness ───────────────────────
//
// Reserved by V-022 for the file-scope static_asserts below AND for
// the HS14 neg-compile fixtures.  These tags MUST NEVER be specialized
// to `allowed = true` — they exist solely to witness the primary
// template's fail-closed default at compile time, independent of
// whatever transitions V-023+ admits in the production catalog.
//
// Living in `detail::retag_policy_test::` keeps them grep-discoverable
// AND out of reach for application code: production tags live in
// `source::* / trust::* / access::* / version::* / vessel_trust::*`,
// never in `detail::`.  Reviewers can grep this namespace and
// confirm no specialization escapes.
namespace detail::retag_policy_test {
    // Sentinel tag pair — fail-closed witness for V-022 + HS14 fixtures.
    // Never specialize.
    struct NeverFrom {};
    struct NeverTo   {};
}  // namespace detail::retag_policy_test

// Self-test that the primary template is fail-closed AND identity is
// admitted unconditionally.  Sentinel-tag witnesses decouple V-022
// from V-023's catalog: production-tag transitions that V-023 admits
// (External → Sanitized, FromPytorch → Validated, etc.) would
// invalidate a production-tag-based fail-closed assertion when their
// specialization lands.  The sentinel pair stays unspecialized
// forever, so these asserts witness the STRUCTURAL property "primary
// template is fail-closed" rather than the transient property "this
// specific pair is not yet in the catalog".
static_assert(retag_policy<source::FromUser, source::FromUser>::allowed,
    "retag_policy identity specialization must admit (X → X)");
static_assert(!retag_policy<detail::retag_policy_test::NeverFrom,
                            detail::retag_policy_test::NeverTo>::allowed,
    "retag_policy primary template MUST be fail-closed for any "
    "(From → To) pair without an explicit specialization");
static_assert(RetagAllowed<source::FromUser, source::FromUser>,
    "RetagAllowed concept must admit identity");
static_assert(!RetagAllowed<detail::retag_policy_test::NeverFrom,
                             detail::retag_policy_test::NeverTo>,
    "RetagAllowed concept must reject unspecialized transitions");

// ── V-023 safe-transition catalog ──────────────────────────────────
//
// Every specialization below admits exactly one (From → To) phantom-
// tag transition.  The catalog is the SINGLE source of truth for the
// safe laundering surface — every safety-critical retag review reads
// this list and verifies that the validator at the call site
// discharges the implicit invariant.  Adding a specialization here
// is a security-review gate: which validator proves this transition
// is safe?
//
// Discipline:
//   - Group by tag-family axis (trust::, source::, vessel_trust::).
//     Cross-family transitions are NEVER added — laundering across
//     orthogonal axes (e.g., source::* → trust::*) confounds the
//     phantom semantic.
//   - Within a group, list shorter-distance transitions first
//     (Unverified → Tested before Unverified → Verified).
//   - Every specialization is paired with rationale naming the
//     validator that discharges the invariant.
//   - The inverse direction (downgrade) stays REJECTED by the V-022
//     fail-closed primary template — confirmed by inverse static
//     asserts in the self-test block.
//
// V-024 wires this catalog into Tagged::retag()'s requires-clause.
// Until then, the catalog is consultative — V-022's concept gate
// compiles, V-024 pins it onto retag().

// ── trust:: catalog — verification-status escalation ───────────────
//
// Trust is a one-way ratchet: Unverified ⊏ Tested ⊏ Verified.
// Assumed is a sibling pre-condition that discharges into Verified
// once the assumption is checked.  The validator at the retag site —
// test suite execution, proof checker, assumption pre-condition
// verification — IS the safety-bearing component; the retag is the
// type-level record that it ran.

template <> struct retag_policy<trust::Unverified, trust::Tested> {
    // Discharge: test suite ran and passed against this value.
    static constexpr bool allowed = true;
};
template <> struct retag_policy<trust::Unverified, trust::Verified> {
    // Discharge: formal proof / cryptographic verification completed.
    static constexpr bool allowed = true;
};
template <> struct retag_policy<trust::Tested, trust::Verified> {
    // Discharge: proof obligation discharged on top of test coverage.
    static constexpr bool allowed = true;
};
template <> struct retag_policy<trust::Assumed, trust::Verified> {
    // Discharge: previously-assumed pre-condition was checked.
    static constexpr bool allowed = true;
};

// ── source:: catalog — provenance laundering ───────────────────────
//
// External / FromUser → Sanitized: the input validator (well-formed
// ness, bounds, character-class) ran and accepted the value.
// External / Sanitized → IntegrityVerified: the integrity check
// (CRC / xxHash / HMAC trailer) ran and matched.  Recorded → Loaded:
// the recording pipeline finished and the value was admitted to
// persistent state.

template <> struct retag_policy<source::External, source::Sanitized> {
    // Discharge: input sanitizer at the trust boundary accepted bytes.
    static constexpr bool allowed = true;
};
template <> struct retag_policy<source::External, source::IntegrityVerified> {
    // Discharge: end-to-end integrity check (xxHash64 trailer, etc.)
    // recomputed at receiver and matched the wire value.
    static constexpr bool allowed = true;
};
template <> struct retag_policy<source::Sanitized, source::IntegrityVerified> {
    // Discharge: sanitized value additionally passes integrity check.
    // The two predicates compose (sanitized AND integrity-verified).
    static constexpr bool allowed = true;
};
template <> struct retag_policy<source::FromUser, source::Sanitized> {
    // Discharge: user-supplied value passed the input sanitizer.
    static constexpr bool allowed = true;
};
template <> struct retag_policy<source::Recorded, source::Loaded> {
    // Discharge: recording pipeline closed the trace; value is now
    // admitted into validated persistent state (Cipher load path).
    static constexpr bool allowed = true;
};

// ── vessel_trust:: catalog — Vessel-boundary validation ────────────
//
// FromPytorch marks raw input crossing the PyTorch ABI boundary;
// Validated marks the same value after Vessel-side well-formedness
// checks pass.  Internal paths (record / replay / KernelCache lookup)
// demand Validated at entry; the type system rejects the
// `FromPytorch → record` shortcut at the call site.

template <> struct retag_policy<vessel_trust::FromPytorch, vessel_trust::Validated> {
    // Discharge: Vessel adapter's well-formedness checks ran on input.
    static constexpr bool allowed = true;
};

// ── V-023 self-test ────────────────────────────────────────────────
//
// Positive: every catalog specialization admits its transition.
// Negative: every inverse direction stays rejected — the fail-closed
// primary template does NOT auto-admit the reverse, so the V-022
// property survives intact under the V-023 catalog.

// trust:: positives — catalog admits forward escalation
static_assert(retag_policy<trust::Unverified, trust::Tested>::allowed,
    "trust::Unverified → trust::Tested must be admitted");
static_assert(retag_policy<trust::Unverified, trust::Verified>::allowed,
    "trust::Unverified → trust::Verified must be admitted");
static_assert(retag_policy<trust::Tested, trust::Verified>::allowed,
    "trust::Tested → trust::Verified must be admitted");
static_assert(retag_policy<trust::Assumed, trust::Verified>::allowed,
    "trust::Assumed → trust::Verified must be admitted");

// trust:: inverse rejected — verification strip is NEVER safe.
// Pair each forward specialization with its inverse witness so a
// future reviewer adding "by symmetry" cannot accidentally admit a
// downgrade direction.
static_assert(!retag_policy<trust::Verified, trust::Unverified>::allowed,
    "trust::Verified → trust::Unverified would erase the proof");
static_assert(!retag_policy<trust::Verified, trust::Tested>::allowed,
    "trust:: catalog is one-way; Verified → Tested is a downgrade");
static_assert(!retag_policy<trust::Tested, trust::Unverified>::allowed,
    "trust::Tested → trust::Unverified would erase test coverage");
static_assert(!retag_policy<trust::Verified, trust::Assumed>::allowed,
    "trust::Verified → trust::Assumed would downgrade discharged proof "
    "back to a mere assumption");

// source:: positives — catalog admits forward laundering
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

// source:: inverse rejected — taint cannot be reintroduced.
// One inverse witness per forward cell above so the 1:1 matrix is
// complete (a missing inverse-reject is the easier oversight when
// adding a forward specialization "by symmetry").
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

// vessel_trust:: positive + inverse
static_assert(retag_policy<vessel_trust::FromPytorch, vessel_trust::Validated>::allowed,
    "vessel_trust::FromPytorch → vessel_trust::Validated must be admitted");
static_assert(!retag_policy<vessel_trust::Validated, vessel_trust::FromPytorch>::allowed,
    "vessel_trust::Validated → FromPytorch would erase well-formedness");

// Concept-form positives + cross-axis rejection survives V-023
static_assert(RetagAllowed<source::External, source::Sanitized>,
    "RetagAllowed concept admits catalog transitions");
static_assert(RetagAllowed<trust::Unverified, trust::Verified>,
    "RetagAllowed concept admits trust escalation");
static_assert(!RetagAllowed<source::External, trust::Verified>,
    "Cross-axis transition (source::* to trust::*) stays rejected — "
    "laundering across orthogonal axes is never safe");
static_assert(!RetagAllowed<source::FromUser, access::WriteOnce>,
    "Cross-axis transition (source::* to access::*) stays rejected");

// ── §XXI Universal Mint factory — fixy-A1-005 (#1547) ──────────────
//
// `mint_tagged<Tag, T>(value)` synthesizes an authoritative
// `Tagged<T, Tag>` at the §XXI grep-discoverable boundary.  Per
// CLAUDE.md §XXI: every cross-tier composition factory is named
// `mint_<noun>` so `grep "mint_"` finds every authorization point.
// Constructing `Tagged<T, Tag>{value}` directly bypasses the §XXI
// grep — production code crossing a trust / provenance / access /
// version boundary MUST route through this factory.
//
// HS14 gate: `ValidTaggedTag<Tag>` is the load-bearing soundness
// check — Tag MUST be a class type (struct / class).  The
// conventional phantom-tag shapes in `source::*` / `trust::*` /
// `access::*` / `version::*` / `vessel_trust::*` above are all
// empty structs; passing a scalar / pointer / reference / void as
// the Tag slot is a type-shape category error rejected at the
// concept boundary with a clean diagnostic.  Two HS14 neg-compile
// fixtures (scalar-tag, pointer-tag) at test/safety_neg/ witness
// the gate fires across both non-class-type families.
//
// Template parameter order: `<Tag, T>` — Tag explicit (the user-
// supplied phantom), T deduced from the argument.  Mirrors the
// `mint_permission_root<Tag>()` convention where the tag is
// explicit.
//
// Hot-path cost: zero — `[[nodiscard]] constexpr noexcept`, EBO
// collapses the Graded substrate.  Identical machine code to a
// raw `Tagged<T, Tag>{std::move(value)}` ctor call under -O3.

template <typename Tag>
concept ValidTaggedTag = std::is_class_v<Tag>;

template <typename Tag, typename T>
    requires ValidTaggedTag<Tag>
[[nodiscard]] constexpr Tagged<T, Tag> mint_tagged(T value)
    noexcept(std::is_nothrow_move_constructible_v<T>)
{
    return Tagged<T, Tag>{std::move(value)};
}

namespace detail::tagged_self_test {

// ── Runtime smoke test ──────────────────────────────────────────────
//
// Exercise value() / value_mut() / retag<>() / into() / mint_tagged
// forwarder per feedback_algebra_runtime_smoke_test_discipline.
// Catches Graded::peek_mut() ↔ Tagged::value_mut() forwarding
// regressions on the RelativeMonad-modality path that pure
// static_asserts miss.
inline void runtime_smoke_test() {
    int seed = 7;                                            // non-constant

    // Construct via direct ctor + mint forwarder.
    Tagged<int, source::FromUser> u{seed * 6};
    if (u.value() != 42) std::abort();

    auto um = mint_tagged<source::FromUser, int>(seed * 6);
    if (um.value() != 42) std::abort();

    // value_mut on lvalue.
    u.value_mut() = 100;
    if (u.value() != 100) std::abort();

    // Retag — moves through, keeps payload.
    Tagged<int, source::Sanitized> s = std::move(u).template retag<source::Sanitized>();
    if (s.value() != 100) std::abort();

    // .into() consumes — returns raw T.
    int extracted = std::move(s).into();
    if (extracted != 100) std::abort();

    // Verified vs Unverified trust tags — distinct types.
    Tagged<long, trust::Verified>    v{seed * seed};
    Tagged<long, trust::Unverified>  uv{seed * seed};
    if (v.value() != uv.value()) std::abort();

    // Version tag with NTTP.
    Tagged<int, version::V<3>> vt{seed};
    if (vt.value() != 7) std::abort();

    // Vessel-trust boundary: FromPytorch → Validated.
    Tagged<int, vessel_trust::FromPytorch> raw{seed};
    auto validated = std::move(raw).template retag<vessel_trust::Validated>();
    if (validated.value() != 7) std::abort();
}

}  // namespace detail::tagged_self_test

} // namespace crucible::safety
