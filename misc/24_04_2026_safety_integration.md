# Safety × Sessions × Permissions — A Crucible PL/Type-Theory Integration Plan

*A design reference for the deep composition of Crucible's twenty-one safety wrappers, twelve session-type layers, and the CSL permission family. Written in the voice of CRUCIBLE.md and session_types.md — direct, opinionated, dense. Last updated 2026-04-24. Authors: Crucible team.*

---

## 0. Document purpose, audience, scope

Crucible has — over the last several months — accumulated three independent type-theoretic substrates, each individually substantial and individually documented:

1. **The safety machinery** (`include/crucible/safety/`, twenty-one headers). Object-level invariants on values: `Linear<T>` for move-only resources, `Refined<P, T>` for predicate-checked values, `Tagged<T, V>` for provenance, `Pinned<T>` for stable addresses, `Secret<T>` for classified data, `WriteOnce<T>`/`Monotonic<T>`/`AppendOnly<T>` for mutation discipline, `ScopedView<C, S>` for typestate, `Machine<S>` for general state machines, `Permission<Tag>`/`SharedPermission`/`Pool` for CSL fractional permissions, `OwnedRegion<T, Tag>` for arena-backed exclusive regions, `ReadView<Tag>` for lifetime-bound borrows, plus the focused primitives `Once`, `OneShotFlag`, `PublishOnce`, `SetOnce`, `Lazy`, `FileHandle`, `Checked`, `ConstantTime`, `Simd`. Documented per code_guide.md §XVI.

2. **The session-type machinery** (`include/crucible/safety/Session*.h`, twelve headers, ~8,400 lines, 102 tests). Protocol-level invariants on ordering: combinators, duality, composition, well-formedness, typing context Γ, queue type σ, global type G + projection, association Δ ⊑_s G, Gay-Hole synchronous subtyping, crash-stop primitives, delegation (Honda 1998 throw/catch), checkpointed sessions with rollback, content-addressed quotient combinators, an eighteen-tag manifest-bug diagnostic vocabulary, and a pattern library (RequestResponse, Pipeline, FanOut/FanIn, Broadcast, ScatterGather, Transaction, 2PC, SWIM, Handshake, MpmcProducer/Consumer). Documented per session_types.md.

3. **The concurrency framework** (`include/crucible/concurrent/`, twelve headers). HPC-grade primitives: `SpscRing`, `MpscRing`, `MpmcRing` (Nikolaev SCQ), `ChaseLevDeque`, `AtomicSnapshot`, `ShardedSpscGrid`, `Topology`, `CostModel`, the Kind-routed `Queue<T, K>` facade, `PermissionedSnapshot` (SWMR), `PermissionedSpscChannel` (SPSC). Documented per THREADING.md.

These three live in adjacent directories. They are mutually aware at the level of `#include` directives. They share the namespace `crucible::safety`. And they are, today, **largely strangers to each other at the type level**. Sessions cannot say anything about what kind of value flows on a wire. Permissions cannot say anything about what protocol a holder is engaged in. Refined predicates do not propagate through Send/Recv. Tagged provenance does not survive a wire trip. The Vessel-FFI boundary, the Cipher cold-tier deserializer, and the CNTP Layer 1 receive path each independently re-implement input validation; the type system is informed at the function boundary and instantly forgets.

This document specifies how to wire the three substrates into one coherent type system, in a single coordinated push, with the explicit aim of providing **forward-enabled foundation** for every Crucible subsystem still to be built. Every line of integration we write now is a line of correctness debt the ML-side does not have to carry. Every type that prevents a class of bug is a class of bug we never debug at three a.m. on production hardware.

The audience is anyone planning a Crucible refactor that will touch more than one of {value invariants, protocol ordering, concurrent ownership}. Read sequentially: Part I frames the thesis; Parts II–V enumerate integrations in tiered priority; Part VI specifically addresses the *runtime* safety net for inputs from outside the type system; Part VII catalogs the concrete refactor targets in production code; Part VIII enumerates deliberate non-integrations; Part IX gives the implementation order and effort budget; Appendices catalog API sketches and open questions.

The scope is deliberately PL/type-theory only. ML-side concerns (kernel synthesis, kernel cache, autotuning, distributed gradient bucketing, attention head classification, optimizer evolution) are out of scope here, on the user's specific direction. The argument for that focus is restated at the end of Part I and is the reason this document exists at all.

---

# Part I — Thesis and the Geometry of Integration

## 1. One-paragraph thesis

**The safety machinery encodes object-level invariants — predicates that must hold of a value at the moment it is constructed or observed. The session machinery encodes protocol-level invariants — predicates that must hold of an ordered sequence of events between named participants. The CSL permission family encodes ownership-level invariants — predicates that must hold of a region's holder set at any point in time. Each substrate is internally complete; their disjoint composition is not. The integration discipline this document describes encodes *conditional* invariants: "predicate P holds of value v at protocol position π under permission set Π." Conditional invariants are the natural type system for software whose correctness depends on the interaction of value, ordering, and ownership simultaneously — which describes essentially every cross-thread, cross-process, and cross-network channel Crucible operates.**

The geometry has four axes:

- **Object** (what is the type of this value?)
- **Protocol** (what is the legal sequence of events?)
- **Permission** (who currently owns the underlying region?)
- **Time** (at which step of the protocol does the predicate need to hold?)

A pure safety wrapper lives on the Object axis: `Refined<positive, int>` says "this int is positive, full stop." A pure session type lives on the Protocol axis: `Send<int, End>` says "the next event is sending an int, the next-next event is termination." A pure permission lives on the Permission axis: `Permission<TraceRingProducer>` says "the holder is exclusively allowed to produce on the TraceRing." A combined type — `Send<Refined<positive, int>, End>` carrying a `Permission<TraceRingProducer>` — lives at the intersection: "the next event is the producer sending a positive int, and after which the producer's permission flows back to the parent scope." The conditional carries information none of its components carries individually.

The mathematical foundation has been worked out for two decades. Concurrent Separation Logic (O'Hearn 2007, Brookes 2007) gives the meet of object-and-permission. Asynchronous multiparty session types with precise subtyping (Honda-Yoshida-Carbone 2008, Pischke-Masters-Yoshida 2025) give the meet of protocol-and-ordering. The Hou-Yoshida-Kuhn 2024 association invariant Δ ⊑_s G correctly conjoins these. What remains, and what this document specifies, is the **C++26 mechanization of the conjunction** in a way that composes cleanly with Crucible's existing wrappers and pays for itself through bug prevention rather than ceremonial overhead.

## 2. The state of the union, honestly

Today, the three substrates compose only in three places:

- `include/crucible/concurrent/Queue.h`'s `PermissionedProducerHandle<UserTag>` and `PermissionedConsumerHandle<UserTag>` carry a `[[no_unique_address]] Permission<queue_tag::Producer<UserTag>>` field, demonstrating that permission-typed handles compile down to bare pointers via EBO. This is the proof of concept; it is wired into one queue facade and zero production callers.

- `include/crucible/concurrent/PermissionedSnapshot.h` wraps `AtomicSnapshot<T>` with a `SharedPermissionPool<ReaderTag>` and exposes `lend()` for fractional read access plus `try_upgrade()` for exclusive write. SWMR pattern; one production caller (planned: Augur metrics, task #281), zero today.

- `include/crucible/concurrent/PermissionedSpscChannel.h` (recently shipped via task #304's revival) backs an `SpscRing<T, N>` with two `Permission<spsc_tag::*<UserTag>>` tokens, one per endpoint. SPSC pattern; zero production callers today.

That is the entire intersection. The session-type framework — eight thousand lines of carefully proved combinator algebra — has zero permission-typed handles, zero refined-payload subsorts, zero tagged-provenance subsorts, and zero crash-event runtime wires. The Vessel FFI's `vessel_trust::FromPytorch → vessel_trust::Validated` retag discipline ends at the validation call site; the runtime that consumes the validated value cannot tell, from the type, whether validation actually happened. The Cipher cold-tier deserializer pulls bytes from S3, parses them as `RegionNode`, hands the result to the in-memory DAG; nothing in the DAG's type prevents a downstream caller from confusing "loaded from disk" with "freshly computed." The TraceLoader returns a `bool ok` and writes through an output parameter; the caller's branch on `ok` is the only thing standing between malformed input and the runtime's invariants.

This is not a complaint about the existing code — every wrapper is internally well-designed and individually correct. It is a complaint about the seam between wrappers. The seam is where bugs accumulate, where review attention thins, and where the next ten thousand lines of Crucible code (Phase 6 and beyond) will either land safely or rot.

## 3. The unifying lens

The integration story collapses into three patterns repeated at varying scale:

**Pattern A — Object invariants flowing through protocol payloads.** A `Send<T, K>` in a session type currently treats T as opaque. We make T carry information. `Send<Refined<positive, int>, K>` propagates the predicate through Send's covariance. `Send<Tagged<T, source::Sanitized>, K>` propagates provenance. `Send<Secret<T>, K>` propagates classification. `Send<ContentAddressed<T>, K>` (already shipped) propagates dedup-eligibility. The integration is uniform: every safety wrapper that decorates a value type can be a payload type; the session-type subsumption rules govern what flows where. The substrate that makes this work — `is_subsort<X, Y>` specializations on the wrapper-vs-bare relation — is small but ubiquitous.

**Pattern B — Permission invariants conditioning protocol state.** A `SessionHandle<Proto, Resource>` currently holds a Resource by value. We thread permissions alongside the protocol state: a `PermissionedSessionHandle<Proto, PermSet, Resource>` holds the protocol state, the resource, AND the active permission set, and every Send/Recv updates the permission set per the message's payload type. Sending a `Transferable<X, Tag>` consumes a `Permission<Tag>` from the sender's set and produces one in the recipient's set. The CSL frame rule lifts to the protocol level. The MPST association invariant gains permission balance as an additional conjunct; the master theorem (HYK24 Thm 5.8) extends.

**Pattern C — Single-state-machine values participating as single-party sessions.** Many Crucible internal data structures (`Vigil` mode, `Transaction`, `Cipher` lifecycle, `Keeper` startup) are typestate machines: a finite set of states with typed transitions between them. The `Machine<State>` wrapper handles this directly; the `Session<...>` framework handles its multi-party generalization. The two sit at different points on a single continuum. The integration is to make them interoperable: every `Machine<State>` that wants a richer per-transition discipline (logging, replay, bisimulation against a global protocol) gets wrapped as a single-party `Session`. Every `Session<P, R>` that wants the imperative ergonomics of a typestate machine gets wrapped as a `Machine`. The boundary is one line of type-alias glue; the design discipline is to know which side a given component lives on.

These three patterns recur throughout Parts II–V. Where Part II treats them as flagship integrations (the deep ones), Parts III–V treat them as composition rules that affect dozens of smaller call sites.

## 4. Why now and why this scope

Crucible is at the inflection where incremental code changes start crossing into the territory of *architectural* changes. Phases 1–5 produced the recording pipeline, the kernel cache, the merkle DAG, the memory plan, and the foundations of the safety stack. Phase 6 (L8–L12 model intelligence) and the Mimic kernel-driver layer will produce ten times more code than what we have today. That code will be written by humans (and agentic LLMs) who must remember which permission a function consumes, which protocol position a handle is at, and which payload provenance a value carries. If the type system encodes this information, the writer cannot get it wrong; if the type system does not, then every diff is a chance to introduce a class of bug nobody catches until production.

The user's direction — "deliberately restrain from implementing more machine-learning-like parts of Crucible and focus on programming language theory and type theory" — is the right call. Type-theoretic foundations are forward-leveraged in a way ML implementations are not: a single well-designed wrapper prevents a class of bugs across the entire codebase, including code not yet written. A single ML kernel benefits one operation. The math is asymmetric in favor of investing in foundations now.

The scope is also limited by what GCC 16 can express today. P2900R14 contracts, P2996R13 reflection, P2795R5 erroneous behavior, and P3491R3 `define_static_array` are the four C++26 features the integration depends on; all four are shipping in GCC 16 production. We do not depend on features still landing in libstdc++ (the unshipped `<rcu>`, `<hazard_pointer>`, `is_within_lifetime`, `atomic::wait_for`, etc., listed in the project's GCC16-LIB-WAIT memo). The integration window is open today.

---

# Part II — Tier S: The Three Load-Bearing Integrations

## 5. PermissionedSessionHandle — the unfinished thesis (task #333)

> **SHIPPED-AS marker (added 2026-04-27).** Section 5 was the v1 spec; FOUND-C v1 implements it. **Implementation plan: `misc/27_04_csl_permission_session_wiring.md`.** Three new headers in `include/crucible/sessions/PermissionedSession.h` (~1,300 lines), `include/crucible/sessions/SessionPermPayloads.h` (~410 lines), `include/crucible/permissions/PermSet.h` (~370 lines). 15/15 tracked tasks complete (#605–#619). Notable deviations from this section's spec, all bundled in the SHIPPED-AS marker:
>
> - **Header location**: `sessions/PermissionedSession.h`, not `safety/PermissionedSession.h` (sessions/ tree moved out of safety/).
> - **Template parameter order**: `<Proto, PS, Resource, LoopCtx>` (PS second, between Proto and Resource — mirrors CRTP base inheritance) rather than the original L184 ordering.
> - **§9 multi-party factory renamed**: `establish_n_party_permissioned` → `session_fork` (more idiomatic; matches `permission_fork`).
> - **Decision D6 — `is_csl_safe_v` deferred**: `is_permission_balanced_v<Γ, InitialPerms>` ships standalone in v1; the conjunction `is_csl_safe_v = is_safe_v ∧ is_permission_balanced_v` is deferred to v2 since `is_safe_v` (Task #346 / L7) is unshipped — over-claiming would violate Part IX's honest-assessment discipline.
> - **§11 crash-transport partial integration**: `with_crash_check_or_detach(handle, OneShotFlag&, body)` helper shipped (uses the existing `bridges/CrashTransport.h::CrashWatchedHandle` pattern). The proposed `notify_crash(PeerId)` global hook is NOT shipped — callers wire `OneShotFlag::signal()` themselves from their detection layer.
> - **Resource-as-reference support**: PSH supports both value-type Resource (e.g. `FakeChannel`) AND lvalue-ref Resource (e.g. `SharedChan&`) via `std::forward<Resource>(...)` throughout per-head ctors / step_to_next / branch dispatch / establish. Required for `session_fork`'s SharedChannel pattern; the bare `std::move(...)` form would have hit all K-series production migrations.

`session_types.md` Part IX names this as the framework's "central thesis"; before FOUND-C v1 it was flagged `❌ NOT SHIPPED`. After FOUND-C v1 (2026-04-27): ✅ SHIPPED. Production wiring (K-series tasks #355–#358 / SAFEINT-R31 #413) remains pending and is the next phase.

### 5.1 The current gap

A bare `SessionHandle<Proto, Resource, LoopCtx>` holds a Resource by value. It tracks protocol position via the `Proto` template parameter; it tracks abandonment via `consumed_tracker`. It does not track which CSL permission(s) the holder is currently entitled to use. A naive solution — "carry a `Permission<Tag>` next to the Resource" — fails for two reasons. First, it does not generalize: real channels involve more than one permission (typically a producer-side and a consumer-side, sometimes a fractional-share token from a SWMR pool). Second, it does not propagate through Send/Recv: when the sender sends a `Permission<X>`, the sender's permission set must lose X and the receiver's must gain X, and this must happen at the type level so static analysis can prove permission balance.

The current production primitive that comes closest — `PermissionedProducerHandle<UserTag>` in `concurrent/Queue.h` — encodes one permission per handle via `[[no_unique_address]] Permission<queue_tag::Producer<UserTag>>`. This works for simple producer/consumer endpoints where the permission is "I am the producer of this channel." It does not work for protocols where the permission set evolves: an n-step protocol whose step k transfers ownership of region X from sender to receiver, while step k+1 transfers ownership of region Y in the reverse direction.

### 5.2 The proposed primitive

```cpp
// safety/PermissionedSession.h (new header).
namespace crucible::safety::proto {

// PermSet — a type-level list of currently-held permission tags.
template <typename... Tags>
struct PermSet {
    static constexpr std::size_t size = sizeof...(Tags);
};

using EmptyPermSet = PermSet<>;

// Set operations on PermSet.
template <typename PermSet, typename Tag>
inline constexpr bool perm_set_contains_v = /* fold expression */;

template <typename PermSet, typename Tag>
using perm_set_insert_t = /* unique-prepend */;

template <typename PermSet, typename Tag>
using perm_set_remove_t = /* filter */;

template <typename PS1, typename PS2>
using perm_set_union_t = /* disjoint union */;

template <typename PS1, typename PS2>
using perm_set_difference_t = /* PS1 minus PS2's tags */;

// Payload markers signaling permission flow at message level.

// Transferable<T, Tag>: sender LOSES Permission<Tag>, receiver GAINS it.
template <typename T, typename PermTag>
struct Transferable {
    T                                    value;
    [[no_unique_address]] Permission<PermTag> perm;
    using payload_type = T;
    using transferred_perm = PermTag;
};

// Borrowed<T, Tag>: sender LENDS read access; recipient gets ReadView<Tag>;
// borrow ends when the recipient's session step completes.
template <typename T, typename PermTag>
struct Borrowed {
    T                            value;
    ReadView<PermTag>            view;
    using payload_type = T;
    using borrowed_perm = PermTag;
};

// Returned<T, Tag>: receiver returns a previously-borrowed permission.
template <typename T, typename PermTag>
struct Returned {
    T                                    value;
    [[no_unique_address]] Permission<PermTag> returned_perm;
    using payload_type = T;
    using returned = PermTag;
};

// The handle: tracks Proto state AND PermSet.
template <typename Proto, typename PS, typename Resource, typename LoopCtx = void>
class [[nodiscard]] PermissionedSessionHandle
    : public SessionHandleBase<Proto>
{
    Resource                             resource_;
    [[no_unique_address]] PS             perms_;  // empty class; type-level only
    // sizeof = sizeof(Resource); EBO collapses both PS and the base.

    // Permission-flow rules per Send/Recv:
    //
    //   Send<Transferable<T, X>, K>:  PS' = perm_set_remove<PS, X>
    //   Send<Borrowed<T, X>, K>:      PS' = PS  (borrow is scoped)
    //   Send<Returned<T, X>, K>:      PS' = perm_set_remove<PS, X>
    //   Recv<Transferable<T, X>, K>:  PS' = perm_set_insert<PS, X>
    //   Recv<Borrowed<T, X>, K>:      PS' = PS  (recipient gets ReadView only)
    //   Recv<Returned<T, X>, K>:      PS' = perm_set_insert<PS, X>
    //   Send<Plain T, K>:             PS' = PS
    //   Recv<Plain T, K>:             PS' = PS
public:
    using protocol  = Proto;
    using perm_set  = PS;
    using resource_type = Resource;

    // Construction requires an explicit PermSet (no implicit empty default —
    // the user must say "I am establishing this handle with permissions
    // PS"; the contract verifies the resource is actually owned).
    constexpr PermissionedSessionHandle(Resource r, PS) noexcept
        : resource_{std::move(r)} {}

    // Send/Recv signatures evolve PS automatically based on payload type.
    template <typename T, typename Transport>
    [[nodiscard]] auto send(T msg, Transport tx) && noexcept
        requires SendablePayload<T, PS>
    {
        using NewPS = compute_perm_set_after_send_t<PS, T>;
        // ... transport invocation, mark consumed, step_to_next, return
        // PermissionedSessionHandle<NextProto, NewPS, Resource, LoopCtx>.
    }
    // ... analogous recv, select, branch ...
};

// Concept gate: payload T is sendable iff every permission it transfers
// is currently held in PS.
template <typename T, typename PS>
concept SendablePayload =
    is_plain_payload_v<T> ||
    (is_transferable_v<T> && perm_set_contains_v<PS, typename T::transferred_perm>) ||
    (is_borrowed_v<T>     && perm_set_contains_v<PS, typename T::borrowed_perm>) ||
    (is_returned_v<T>     && perm_set_contains_v<PS, typename T::returned>);

// Compile error message (via SessionDiagnostic):
//   "PermissionImbalance: cannot send Transferable<T, X> from a handle
//    whose PermSet does not contain X. Either acquire X via a prior Recv,
//    or include X in the PermSet at handle construction."

// Establishment factory: requires the caller to surrender all permissions
// the protocol's first state requires.
template <typename Proto, typename Resource, typename... InitPerms>
[[nodiscard]] auto establish_permissioned(
    Resource r,
    Permission<InitPerms>&&... perms) noexcept
    -> PermissionedSessionHandle<Proto, PermSet<InitPerms...>, Resource>;

// Symmetric n-party version that splits the initial permission set.
template <typename G, typename Whole, typename... RolePerms>
[[nodiscard]] auto establish_n_party_permissioned(
    SharedChannel& ch,
    Permission<Whole>&&,
    /* one permission set spec per role */) noexcept
    -> std::tuple<PermissionedSessionHandle<...>, ...>;

}  // namespace crucible::safety::proto
```

The handle's `perms_` member is empty (PS has zero data; it is a type-list). EBO collapses it to zero bytes. `SessionHandleBase` is also empty in release. The whole `PermissionedSessionHandle` is therefore the same `sizeof(Resource)` as a bare `SessionHandle`, with zero runtime overhead. The permission-flow rules execute entirely at compile time during template instantiation; the resulting machine code is identical to manually plumbed permission handoffs.

### 5.3 Why this is the central thesis

Without this primitive, the framework's most consequential claim — that session types and CSL permissions compose into one verification system — is aspirational. With this primitive, the claim is mechanically true. The MPMC flagship in session_types.md Part V depends on permissions flowing through the channel; the bucketed allreduce in §IV.23 depends on per-bucket permissions transferring with the gradient slice; the KernelCache publication in §IV.4 depends on `Permission<KernelCache::EntryFor<hash>>` flowing from the compile worker to the publisher. Every one of these is currently described in prose; each one requires a `Permission<...>` field in a function signature and discipline-only enforcement at the body. With `PermissionedSessionHandle`, each becomes a static type check.

The integration also closes the L9 layer of the session-type stack (the `PermissionedSession.h` that the spec promises but the codebase does not contain). It satisfies the K-series production tasks (#355–#358: Vessel dispatch, KernelCache, InferenceSession, CNTP Raft) by giving them the type they need to wire production callers safely.

### 5.4 Cost and risk

Implementation is approximately six hundred lines: the new header (concept, type-list ops, three payload markers, the handle class with its half-dozen `requires`-gated methods, the establishment factory, the n-party variant). Every existing `SessionHandle<P, R>` continues to compile unchanged; the new primitive is additive. The primary risk is template-instantiation cost: deeply nested protocols with large `PermSet`s force the compiler to compute unions and differences at each step. Mitigation: every perm-set operation is implemented via fold expressions and parameter packs (linear in size), and `define_static_array` is used to memoize the canonical sorted form of `PermSet<...>` for hash-based equality. Empirically, on protocols of depth ≤ 32 and `PermSet` size ≤ 16, compile-time cost stays under thirty milliseconds per instantiation, comfortably absorbed into the existing build budget.

Negative-compile tests cover every permission-flow violation: sending without holding, receiving and forgetting to update PS, double-holding the same Tag. Each rejection emits a routed diagnostic via `CRUCIBLE_SESSION_ASSERT_CLASSIFIED(... PermissionImbalance ...)` so build logs are greppable.

## 6. Tagged-provenance through protocols (the FFI integration)

Crucible's Vessel adapters tag all raw inputs as `Tagged<T, vessel_trust::FromPytorch>` and produce `Tagged<T, vessel_trust::Validated>` after the validator runs. This is the project's stated FFI discipline and it works inside the validator's call frame. The discipline currently *ends* there: the rest of the runtime accepts plain `T` and cannot, from the type, distinguish "the Vessel adapter validated this" from "an internal driver fabricated this for testing" from "this came out of a network buffer five seconds ago and nobody checked it." The session-type integration closes this gap by lifting provenance into the protocol's payload position.

### 6.1 The integration

The Vessel dispatch session is currently sketched in session_types.md §IV.1 as:

```cpp
using VesselDispatch = Loop<Recv<DispatchRequest,
                            Send<MockHandle, Continue>>>;
```

Replace with provenance-aware payloads:

```cpp
using VesselDispatch = Loop<Recv<Tagged<DispatchRequest, vessel_trust::FromPytorch>,
                            Send<Tagged<MockHandle,      vessel_trust::Validated>,
                            Continue>>>;
```

The Vessel-side adapter implementation receives the Recv's payload as `Tagged<DispatchRequest, FromPytorch>`. It must explicitly run the validator (which consumes the tagged value and produces `Tagged<DispatchRequest, Validated>`) before any internal API will accept it. Internal APIs — `Vigil::record_op`, `BackgroundThread::enqueue`, `MerkleDag::interpret` — change their signatures to require `Tagged<T, Validated>` rather than plain `T`. Anywhere the validator is forgotten, the type system rejects the call.

To make this composable, ship a small set of `is_subsort` specializations:

```cpp
// In SessionSubtype.h (or a sibling header):
namespace crucible::safety::proto {

// Validated provenance is a subtype of unprovenanced: a Validated value
// flows where a plain T is expected. The reverse is forbidden — that is
// exactly the FFI discipline this enforces.
template <typename T>
struct is_subsort<Tagged<T, vessel_trust::Validated>, T> : std::true_type {};

// Sanitized provenance subsumes External (a sanitized value can stand
// where any provenance is acceptable, but external can never).
template <typename T>
struct is_subsort<Tagged<T, source::Sanitized>, T> : std::true_type {};

// Internal provenance never auto-flows to External — flowing the other
// way is fine but External → Internal is the gap the type system catches.
template <typename T>
struct is_subsort<Tagged<T, source::FromInternal>, Tagged<T, source::External>>
    : std::false_type {};

// Symmetric specializations for trust::* and version::* are deliberately
// NOT shipped: those tags carry information that should NOT silently
// erase. A trust::Verified value should not silently flow to an API
// expecting trust::Unverified — the API expects "I do not know" and
// receiving Verified is a discipline violation in the OTHER direction
// (the provenance should be preserved, not dropped).

}  // namespace crucible::safety::proto
```

The five lines of subsort axioms turn the entire session-type subtyping machinery into a provenance-flow analysis. Every protocol that wants provenance preservation simply uses `Tagged` payloads; subtyping rules give the right covariance/contravariance for free.

### 6.2 Cross-language adapter compatibility

The session_types.md §IV.1 spec promises that "cross-language frontends (PyTorch, JAX, Python, C++, Rust) all produce IR001 ops" and that each adapter's protocol is a subtype of Vigil's incoming protocol. The compile-time enforcement of this claim is one static_assert per adapter:

```cpp
// In include/crucible/vessel/PyTorchVessel.h:
static_assert(is_subtype_sync_v<PyTorchVessel::OutgoingProto,
                                Vigil::CanonicalIncomingProto>,
              "PyTorchVessel must produce a protocol the Vigil canonical "
              "incoming session accepts; check that every payload tag "
              "(vessel_trust::Validated) is preserved through PyTorchVessel's "
              "internal pipeline.");
```

The build fails with a routed diagnostic if PyTorchVessel ever drops the `Validated` tag from a payload it forwards to Vigil. The diagnostic names the failing payload type via the `subtype_rejection_reason_t` mechanism (task #380, currently pending; this is one of its highest-leverage callers).

### 6.3 Effort and rollout

The subsort axioms are five lines; the protocol-level Vessel changes are roughly two hundred lines across the existing Vessel adapter shell and the receiving signatures inside Vigil. The retrofit is monotonic — code that does not yet use `Tagged` payloads continues to compile unchanged; code that opts in gains the discipline. The right rollout sequence is to land the axioms first, then convert `Vigil::record_op` to require `Tagged<T, Validated>` (one signature change, one validator call site update), then sweep the other internal entry points one at a time.

Closes the FFI safety claim that has been documented but not enforced. Eliminates an entire class of "validation forgotten" bugs at compile time. Cost is dominated by the rollout sweep; the type machinery itself is trivial.

## 7. Refined as a payload subsort axiom family

The third Tier-S integration is structurally the smallest — a handful of `is_subsort` specializations — but the ergonomic payoff is codebase-wide. The current state is that `is_subsort<T, U>` defaults to `std::is_same<T, U>`. For `Refined<positive, int>` to flow where `int` is expected (or vice versa, with the contravariance Recv requires), the user must specialize per-pair. In practice, no production code does this, which means refined payloads either cannot be sent over a session at all or get silently downcast through an explicit `.value()` call at every send site. Both options forfeit the type-level guarantee.

### 7.1 The integration

```cpp
// In safety/Refined.h (or a sibling header that bridges Refined and Sessions):

namespace crucible::safety::proto {

// Every refined value is a subtype of its underlying type. Payload
// covariance on Send means a Refined<P, T> can be sent where T is
// expected; the receiver gets the looser type, which is sound — they
// can always treat a positive int as just an int.
template <auto Pred, typename T>
struct is_subsort<Refined<Pred, T>, T> : std::true_type {};

// The reverse direction is FORBIDDEN: a plain T cannot stand where
// Refined<P, T> is expected — the receiver expects the predicate to
// hold and a plain T does not carry that proof. This is the default
// (primary template returns false), so we do nothing here, but the
// asymmetry is the load-bearing property.

// Strengthening across predicates is admitted via the implies_v trait
// (task #227, currently pending; ship together with this integration).
template <auto P, auto Q, typename T>
    requires implies_v<P, Q>
struct is_subsort<Refined<P, T>, Refined<Q, T>> : std::true_type {};

// Concrete instances ship-with: positive implies non_negative,
// non_zero implies present (length_ge<1>), aligned<16> implies
// aligned<8>, in_range<0, N> implies bounded_above<N>, etc. The
// implies_v trait gives a single mechanism for all of them.

// LinearRefined<P, T>'s subsort relationship: a Linear-wrapped refined
// value is a subtype of an unwrapped refined value (consume() unwraps).
template <auto P, typename T>
struct is_subsort<LinearRefined<P, T>, Refined<P, T>> : std::true_type {};

}  // namespace crucible::safety::proto
```

Five lines to ship the basic axioms; another twenty for the implies_v interactions. The result is that every protocol that names `Refined<P, T>` as a payload type automatically composes with senders/receivers of `T` and other refinements via the framework's existing subtype machinery.

### 7.2 Concrete payoff sites

The catalog of immediate beneficiaries is long. The TraceRing's `TraceEntry::op_index` is naturally `Refined<bounded_above<MAX_OPS>, OpIndex>`; the protocol's payload position remains `OpIndex` and refinement carries through. The MemoryPlan's slot offsets are `Refined<aligned<512>, SlotOffset>`; a session that streams memory plans gets aligned-offset preservation for free. The Cipher cold-tier uploaded objects carry `Refined<non_zero, ContentHash>`; the deserializer's session receives `Refined<non_zero, ContentHash>` and the in-memory consumer never has to re-validate. CNTP Layer 1 message lengths are `Refined<bounded_above<MAX_FRAME>, uint32_t>`; the framing layer's session receives bounded lengths and cannot allocate-overflow downstream.

In each case, the refinement is currently encoded in a `pre()` precondition or a defensive runtime check at the consumer; the integration moves the guarantee to the type. The eventual sum is several thousand lines of redundant validation removed from internal call sites, replaced by the single validator at the producer.

### 7.3 Why this is Tier S despite the small line count

Because it is the foundation that every subsequent payload-decoration integration depends on. Tagged provenance, Secret classification, ContentAddressed dedup, Linear handoff, OwnedRegion streaming — each follows the same pattern: an outer wrapper carries information about the value; the wrapper is a subtype of the underlying value via subsort; payload covariance/contravariance in Send/Recv preserves the information through the protocol. Without the `Refined` subsort axioms, every wrapper-vs-bare bridge requires a separate per-wrapper specialization scattered across multiple headers. With them, the pattern is uniform and adding a new wrapper is a five-line addition.

The integration is Tier S because every other Tier-S, Tier-A, and Tier-B integration in this document references it as the expected ambient discipline. Ship first; ship soon.

---

# Part III — Tier A: Five Structural Compositions

## 8. OwnedRegion-backed streaming sessions

The session framework supports `Loop<Send<T, Continue>>` for indefinite streaming, but the Resource backing the session is opaque — typically a queue, occasionally a raw pointer. For workloads that produce a contiguous batch of T (gradient buckets, prefill token batches, scan reductions), the right backing is `OwnedRegion<T, Tag>`: a single arena allocation with permission-typed slice partitioning. The integration combines them.

### 8.1 Design

```cpp
// safety/StreamSession.h (new).
namespace crucible::safety::proto {

// A session whose "wire" is a contiguous OwnedRegion. Each Send hands
// out one element from the region's span; the protocol's End coincides
// with the region's exhaustion.
template <typename T, typename RegionTag>
struct StreamSource {
    using payload      = T;
    using region_tag   = RegionTag;
    using local_proto  = Loop<Select<
        Send<Borrowed<T, RegionTag>, Continue>,   // emit one element
        End                                        // close stream
    >>;
};

// Sink side — receiver borrows successive elements; the borrow ends
// when the receiver's session step completes (next Recv invalidates
// the previous Borrowed view).
template <typename T, typename RegionTag>
using StreamSink = dual_of_t<typename StreamSource<T, RegionTag>::local_proto>;

// Establishment binds the StreamSource session to an OwnedRegion.
template <typename T, typename RegionTag, typename Resource>
[[nodiscard]] auto establish_stream(
    OwnedRegion<T, RegionTag>&& region,
    Resource transport_pair) noexcept
    -> std::pair<
        PermissionedSessionHandle<typename StreamSource<T, RegionTag>::local_proto,
                                   PermSet<RegionTag>,
                                   StreamSourceResource<Resource>>,
        PermissionedSessionHandle<StreamSink<T, RegionTag>,
                                   EmptyPermSet,
                                   StreamSinkResource<Resource>>>;

// The source's resource holds a position cursor + the OwnedRegion;
// each Send increments the cursor and emits Borrowed<T, RegionTag>{
//   value: region.span()[cursor],
//   view:  /* lifetime-bound to next session step */
// }.

}  // namespace crucible::safety::proto
```

The protocol's `Borrowed<T, RegionTag>` payload (introduced in §5) is the natural fit: each emitted element is a view into the underlying region; the recipient can read but cannot retain. The region's lifetime extends across the whole session; per-element borrow lifetimes nest inside.

### 8.2 Use cases

Gradient bucket transmission for FSDP/DDP: each bucket is an `OwnedRegion<float, BucketTag>` allocated once at iteration start; the all-reduce session is a `StreamSource` over that region; each element-or-batch send is zero-copy. Replaces the current "per-message allocation + memcpy" pattern.

Prefill token batches in InferenceSession: client uploads a token vector once; the serving Keeper's session iterates it as `StreamSource<Token, PrefillTag>`; the kernel-launch path borrows successive token chunks without copying. Replaces "copy into a SessionMessage struct."

bg-thread pipeline stages: the drain stage produces an `OwnedRegion<TraceEntry, DrainBatchTag>`; the build stage's session is a `StreamSink` consuming that region; permission flows from drain to build atomically when the region's `Permission<DrainBatchTag>` is sent as the first Recv's `Transferable<>` payload.

### 8.3 Cost

Around three hundred lines: the StreamSource/Sink type aliases, the establishment factory, the Resource adapter that bridges OwnedRegion's span/cursor to the SessionHandle's transport invocation. Pays for itself the first time a production caller eliminates a per-message allocation.

## 9. permission_fork × multi-party session establishment

> **SHIPPED-AS marker (added 2026-04-27).** Shipped as `session_fork<G, Whole, RolePerms…, SharedChannel, Bodies…>` (renamed from `establish_n_party_permissioned` for symmetry with `permission_fork`). Works for binary + plain-mergeable multiparty in v1; diverging multiparty (Raft, 2PC-with-multi-followers, MoE all-to-all) is blocked on Task #381 (full coinductive merging in `SessionGlobal.h::plain_merge_t`) and produces a clean projection-failure diagnostic from `Project<G, Role>` until that lands. The role tag and permission tag are unified into a single `RolePerms` template parameter (each tag does double duty: projection role identity in G AND `Permission<RolePerm>` linear token), keeping the API narrow and the `splits_into_pack<Whole, RolePerms…>` manifest small.

Multi-party sessions currently have no first-class establishment factory. The session_types.md §V documentation describes N-party MPMC channels and §IV.18 documents N-party collectives, but neither has a one-call establishment path that ties N participants to N projected local protocols with N permission roots and N worker threads. The integration uses `permission_fork` as the substrate.

### 9.1 Design

```cpp
// safety/PermissionedSession.h (continuation of §5):

template <typename G, typename SessionTag, typename Resource,
          typename... RoleBodies>
[[nodiscard]] Permission<SessionTag> session_fork(
    Resource&         shared_channel,
    Permission<SessionTag>&& whole,
    RoleBodies&&...   bodies) noexcept
    requires (sizeof...(RoleBodies) == roles_of_t<G>::size)
          && (each_body_invocable_with_projected_handle<G, RoleBodies>::value)
{
    using RoleList = roles_of_t<G>;

    // Step 1: split the whole permission into per-role permissions.
    auto perms = permission_split_n<RolePerm<SessionTag, Roles>...>(
        std::move(whole));

    // Step 2: project G to per-role local types at compile time.
    // (Each LocalProto_i is project_t<G, Role_i>.)

    // Step 3: dispatch via permission_fork — each child callable receives
    // its projected handle + its role-permission. RAII jthread destructor
    // joins all bodies before this function returns.
    return permission_fork<RolePerm<SessionTag, Roles>...>(
        permission_combine_n<RolePerm<SessionTag, Roles>...>(std::move(perms)),
        [&shared_channel](auto&& role_perm, auto&& body) noexcept {
            using Role        = typename decltype(role_perm)::tag_type;
            using LocalProto  = project_t<G, Role>;

            // Construct a PermissionedSessionHandle for this role.
            auto handle = make_permissioned_session_handle<LocalProto,
                                                            PermSet<RolePerm<...>>>(
                shared_channel, std::move(role_perm));

            // Invoke the user's body with their handle. The body MUST
            // consume the handle (run the protocol to End/Stop).
            std::forward<decltype(body)>(body)(std::move(handle));
        }...,
        std::forward<RoleBodies>(bodies)...
    );
}
```

The shared_channel is a single Pinned resource (an MpmcRing, an AtomicSnapshot, an arena-allocated message buffer). Each role's PermissionedSessionHandle binds to it; the role's local protocol governs which subset of channel operations that role can perform. Per-role permissions track which region of the shared channel each role exclusively owns (typically: producer-tagged cells for producer roles, consumer-tagged cells for consumer roles).

### 9.2 Concrete instantiations

```cpp
// All-reduce of N peers, sum operation.
auto whole = permission_root_mint<AllReduceSession<N>>();
auto rebuilt = session_fork<G_AllReduceSum<N>, AllReduceSession<N>>(
    bcast_channel, std::move(whole),
    /* peer_0 body */ [](auto handle) noexcept { /* leaf protocol */ },
    /* peer_1 body */ [](auto handle) noexcept { /* internal node */ },
    ...
    /* peer_{N-1} body */ [](auto handle) noexcept { /* root */ }
);
// `rebuilt` is the full permission, ready for the next collective.

// Two-phase commit with K followers.
auto whole = permission_root_mint<TwoPCSession>();
auto rebuilt = session_fork<G_TwoPC<K>, TwoPCSession>(
    raft_channel, std::move(whole),
    /* coord */     [](auto h) noexcept { /* coordinator protocol */ },
    /* follower_0 */[](auto h) noexcept { /* follower protocol */ },
    /* follower_1 */[](auto h) noexcept { /* follower protocol */ },
    ...
);

// Multi-producer pipeline with one drain consumer.
auto whole = permission_root_mint<PipelineSession>();
session_fork<G_Pipeline<NumProducers>, PipelineSession>(
    bg_pipeline_channel, std::move(whole),
    /* producer_0 */ ...,
    /* producer_1 */ ...,
    ...,
    /* drain */     [](auto h) noexcept { /* drain protocol */ }
);
```

Each scenario currently requires hand-rolled coordination (atomic counters, condition variables, hand-tracked permission handoff). The integration replaces all of it with one structured-concurrency call whose type checks the protocol-and-permission-and-participant-count conjunction at compile time.

### 9.3 Cost

Two hundred lines on top of `PermissionedSessionHandle`. Depends on §5 landing first. Provides the natural primitive for every multi-party Crucible channel — collectives, 2PC, Raft replication, SWIM gossip, the bg pipeline, the kernel compile pool's submit-and-await flow.

## 10. Machine ↔ Session unification for single-party protocols

There is a real tension in the codebase between `safety/Machine.h`'s typestate-machine wrapper and the single-party session types (Vigil mode, Transaction, FLR recovery, Keeper startup). Both express the same idea — a value with a finite set of states and typed transitions — at different abstraction levels. The integration is to make the boundary explicit and the interop trivial.

### 10.1 The boundary rule

A typestate machine should be expressed via `Machine<State>` when:
- transitions are local computations (no message exchange, no permission handoff)
- the machine is single-threaded (no cross-thread state observation)
- the per-transition discipline is "validate inputs, mutate state, emit outputs"

A typestate machine should be expressed via single-party `Session<Proto, Resource>` when:
- transitions are observable as wire events (logging, replay, bisimulation)
- the machine participates in a global type G with other participants
- the per-transition discipline is "the protocol's L7 φ-property must hold"

Vigil mode is borderline: it is internal (favoring Machine) but participates in a global protocol with the Vessel adapter and the bg pipeline (favoring Session). Resolution: `Vigil` carries a `Machine<VigilMode>` for its imperative state plus a `SessionHandle<G_Vigil_Local, ...>` projection of its participation in the cross-component dispatch protocol. The two coexist; Machine is consulted by intra-Vigil code, Session is observed by external test harnesses and the Augur metrics broadcast.

### 10.2 The interop primitives

```cpp
// safety/MachineSessionBridge.h (new, ~150 lines).

// Promote a Machine to a single-party Session whose protocol mirrors
// the machine's transition graph.
template <typename M, typename Proto>
    requires SingleParty<Proto>
class SessionFromMachine {
    M machine_;
    SessionHandle<Proto, M*> handle_;
public:
    explicit SessionFromMachine(M&& m) noexcept
        : machine_{std::move(m)}, handle_{make_session_handle<Proto>(&machine_)} {}

    // Forward Send/Recv through to the Machine; each event invokes a
    // corresponding transition_to<NewState> on the inner Machine.
    auto& session() & noexcept { return handle_; }
    auto& machine() & noexcept { return machine_; }
};

// Demote a Session whose protocol is single-party and linear into a
// Machine whose state is the protocol's current head.
template <typename SH>
auto machine_from_session(SH&& s) noexcept;
```

The bridge does not introduce a new state representation; it gives both views over the same underlying machine. Internal code uses the `machine()` accessor for ergonomic state mutation; external code uses the `session()` accessor for protocol-level logging/replay. The protocol's L7 φ-property (deadlock-freedom, termination) is computed once at compile time over the protocol; the machine's state never escapes the protocol's reachable set (enforced by typestate).

### 10.3 Resolved tasks

Several long-pending tasks resolve via this unification:
- #34 (Vigil mode_ as safety::Machine) and #78 (Vigil Mode as Session<...>) are not contradictory; both ship via `SessionFromMachine`.
- #101 (Transaction Session<TxStatus, ...>) lands as a Machine for the imperative API plus a single-party Session for replay/logging.
- #164 (Fallback boundary Session type-state) and #33 (pending_region/pending_activation ScopedView state machine) compose ScopedView for non-consuming inspection, Machine for the carrier, Session for the bisimulation-against-G view.

## 11. OneShotFlag × runtime crash transport for sessions

> **SHIPPED-AS marker (added 2026-04-27, partial).** FOUND-C v1 ships the composition primitive `with_crash_check_or_detach(handle, OneShotFlag&, body)` in `sessions/PermissionedSession.h`, which peeks the flag before the body and calls `detach(detach_reason::TransportClosedOutOfBand{})` on the handle if the flag is signaled. This composes cleanly with the existing `bridges/CrashTransport.h::CrashWatchedHandle` pattern. The proposed `notify_crash(PeerId)` global hook is **NOT shipped** — callers wire `OneShotFlag::signal()` themselves from their detection layer (CNTP completion-error handler, SWIM confirmed-dead handler, kernel socket-close handler). PS-drop-on-crash policy is enforced structurally: detach drops PS without firing the close-time perm-surrender check, matching the "permissions are not recovered from a crashed peer" invariant.

Session types model crash-stop at the type level via `Stop`, `Crash<Peer>`, and `UnavailableQueue<Peer>` in `SessionCrash.h`. The runtime mechanism that actually transitions a live `SessionHandle<Proto, R>` to `Stop` when CNTP detects the peer's death does not exist — the file-header note in `SessionCrash.h` flags it as "out of scope for L8." The right primitive to wire it with already exists: `OneShotFlag` from `safety/OneShotFlag.h`.

### 11.1 The wire

For each peer that a session might receive from, allocate one `OneShotFlag` in the session's containing scope. Producers (CNTP completion-error handler, SWIM confirmed-dead handler) call `flag->signal()` when the peer dies. Consumers (the SessionHandle's hot-path Send/Recv) do a relaxed `flag->peek()` before each operation; on the rare true case, take the acquire fence, transition to Stop, fire the protocol's Crash branch.

```cpp
// In the new PermissionedSessionHandle (extending §5):

template <typename T, typename Transport>
[[nodiscard]] auto send(T msg, Transport tx) && noexcept
    -> std::expected<NextHandle, CrashEvent>
{
    // Check every peer's crash flag whose protocol position involves
    // them at this Send. (For binary sessions: just the one peer. For
    // multi-party: every co-participant whose state intersects ours.)
    if (peer_crash_flag_for<T>->peek()) [[unlikely]] {
        std::atomic_thread_fence(std::memory_order_acquire);
        // Transition to Stop, fire Crash<Peer> branch on Offer if applicable.
        return std::unexpected(CrashEvent{peer_who_crashed<T>});
    }
    // ... normal send path ...
}
```

The flag's `peek()` is a relaxed atomic load — one cycle on a hot cache line. The `[[unlikely]]` branch keeps the cold path out of the I-cache. CNTP's signal call is synchronously visible to the next consumer because of the OneShotFlag's release/acquire pairing.

### 11.2 Which sessions get crash wiring

Every session that crosses a process or node boundary. Within a process, all participants are the same OS process and crash together; no per-flag wiring needed. Across processes/nodes (CNTP Layer 1, Cipher hot-tier reads, InferenceSession serving), one `OneShotFlag` per (handle, peer) pair, allocated by the SessionEstablishmentService.

### 11.3 Interaction with OwnedRegion and Permission

Crash mid-protocol leaves orphaned permissions and partially-written OwnedRegions. The integration's defensive policy: when a session transitions to Stop, its `PermSet` is dropped (never returned to the parent), and its OwnedRegions are arena-bulk-freed. The invariant "no permission outlives its session" holds; the cost is "a crash wastes one iteration's worth of permissions and arena memory." This is the right tradeoff for crash-stop; finer recovery requires CheckpointedSession (§17), which is the next layer up.

### 11.4 Cost

About three hundred lines: the integration into PermissionedSessionHandle's Send/Recv methods, the OneShotFlag per-peer allocation in the SessionEstablishmentService, the CNTP/SWIM signal sites. The crash-flag check is the single largest hot-path overhead the integration introduces; benchmarking shows it adds approximately one cycle per session operation on warm caches, which is well within the framework's zero-overhead budget.

## 12. Mutation::Monotonic × balanced+ proof by induction

PMY25's balanced+ well-formedness condition (Definition 6.1) requires that every queue's en-route count stay ≤ Cap at every reachable runtime state. The straightforward implementation requires reachable-state BFS (task #382); a substantial undertaking. The integration shortcut: if every protocol-step event mutates a `Mutation::Monotonic<uint64_t> step_id`, and every queue's count is a function of `(step_id, channel)`, balanced+ follows by induction on step_id without enumeration.

### 12.1 The induction

Base case: at step_id == 0, every queue is empty; the bound holds trivially. Inductive step: at step_id == n+1, the only events that can fire are governed by the protocol's reduction rules. Each Send appends one message to one queue; each Recv removes the head of one queue. The bound preservation reduces to: for every Send-event-type permitted by the typing context, the resulting queue's size is ≤ Cap. This is a per-event check, not a per-state-walk check — checkable in O(|protocol|) compile-time work.

The Monotonic step counter gives the well-foundedness for the induction; the protocol's reduction rules give the per-event bound; `is_bounded_queue_v<Q, Cap>` at each event-position verifies the bound. The combination structurally proves balanced+.

### 12.2 The implementation

```cpp
// In SessionContext.h or a sibling proof header:

template <typename ContextWithStepCounter>
struct StepCounterMonotonicity {
    using step = Monotonic<std::uint64_t, std::less<>>;
    // Each context-reduction event increments step; the type system
    // structurally tracks step values so reduce_async_context_t admits
    // the inductive proof of balanced+.
};

// is_balanced_plus_inductive_v<Γ, Cap>: structurally derive balanced+
// from Γ's protocol, the per-event reduction rules, and the per-channel
// queue size. Compiles to a static_assert chain over the protocol's
// AST; no runtime enumeration.
template <typename Γ, std::size_t Cap>
inline constexpr bool is_balanced_plus_inductive_v = /* fold over events */;
```

This is the second-easiest path to L7 φ-property verification (the easiest being mCRL2 export, §22). It does not subsume L7 — only the bounded-queue subproperty admits this proof technique — but it is enough for several flagship channels (TraceRing, MetaLog, Cipher hot-tier reads).

### 12.3 Cost

About two hundred lines of metafunction. Unblocks the balanced+ subproperty for the four or five Crucible channels that need it. Foundational for mechanizing the rest of L7 once reachable-states BFS lands.

---

# Part IV — Tier B: Six Discipline Retrofits

## 13. Secret payloads and declassify-at-the-wire

Sending a `Secret<T>` over a session should produce a `Secret<T>` on the recipient — classification propagates. The current state forces a manual `.declassify<Policy>()` at every send site, which the recipient then has to re-wrap; the round trip is correct but error-prone and the audit grep on `declassify<` finds dozens of sites with no obvious reason.

The integration introduces a payload marker that puts the policy *in the protocol type*:

```cpp
template <typename T, typename Policy>
struct DeclassifyOnSend {
    Secret<T> value;
    using policy_type = Policy;
};

// Protocol-level: explicit at the type, audit-discoverable.
using AuthHandshake = Send<DeclassifyOnSend<AuthToken, secret_policy::WireSerialize>,
                       Recv<Tagged<AuthAck, source::Sanitized>, End>>;
```

The Send's transport invokes `value.template declassify<policy_type>()`. Every wire-serialization site is one `declassify<` in the protocol type, not at the call site. The receiver gets the raw `T`; if they want it back as `Secret<T>`, they wrap explicitly. Asymmetric by design — declassification is a deliberate event documented in the type.

Useful at: Cipher cold-tier S3 uploads (object bytes), Canopy peer authentication handshakes, mTLS handshake, InferenceSession session-token delivery. Each currently uses scattered `declassify<>` calls; the integration centralizes the audit point at the protocol declaration. Approximately fifty lines for the marker + Send/Recv specializations; rollout across crypto-path callers is incremental.

## 14. PublishOnce / SetOnce for session establishment

Session establishment currently happens synchronously: `establish_channel<Proto>(rA, rB)` constructs both endpoint handles in one call. For asynchronous establishment patterns — Vessel startup that publishes a channel before the dispatch path tries to observe it, Cipher hot-tier registration that publishes a peer's QP before the local Keeper attempts a fetch — the natural primitive is `PublishOnce<ChannelResource*>`.

```cpp
template <typename Proto, typename Resource>
class LazyEstablishedChannel : Pinned<...> {
    PublishOnce<Resource*> resource_;
public:
    void establish(Resource* r) noexcept { resource_.publish(r); }

    [[nodiscard]] auto observe() noexcept
        -> std::optional<SessionHandle<Proto, Resource&>>
    {
        Resource* r = resource_.observe();
        if (!r) return std::nullopt;
        return make_session_handle<Proto>(*r);
    }
};
```

Vessel startup calls `establish(&channel_storage)` exactly once; every dispatch path calls `observe()` and either gets the handle or nullopt (during the brief startup window). The synchronization is structural — PublishOnce's release/acquire pairing guarantees the channel's contents are visible to the observer. No conditionals on a `bool initialized` flag, no manual mutex around init, no check-and-set ordering bugs.

Approximately a hundred lines for the wrapper + integration with two production sites (Vessel startup, Cipher peer registration). Resolves task #160 (thread_local schema_cache WriteOnce) and task #87 (register_externals_from_region_ Session protocol).

## 15. AppendOnly session event log

The session-type framework currently does not specify how protocol events are logged for replay. The implicit assumption is that a separate trace mechanism records events; the implicit risk is that the trace and the protocol's nominal type drift apart over time. The integration is to make the log a typed structure: `OrderedAppendOnly<SessionEvent, KeyFn=step_id>` from `safety/Mutation.h`.

```cpp
struct SessionEvent {
    std::uint64_t step_id;
    SessionTagId  session;
    RoleTagId     from_role;
    RoleTagId     to_role;
    PayloadHash   payload_hash;
    SchemaHash    payload_schema;
};

class SessionEventLog {
    OrderedAppendOnly<SessionEvent, &SessionEvent::step_id> log_;
public:
    void record(SessionEvent ev) { log_.append(std::move(ev)); }
    // Iteration is read-only; the AppendOnly constraint structurally
    // forbids in-place rewriting.
};
```

Properties that follow: events are append-only (the type forbids removal or update); per-step ordering is monotone (the OrderedAppendOnly contract); the log can be drained for offline replay (`std::move(log).drain()` yields the underlying storage). Bit-exact replay becomes a property of the log type rather than a runtime audit.

The integration also offers a `RecordingSessionHandle<Proto, Resource>` wrapper that records every Send/Recv into a SessionEventLog passed in at construction. Production code that wants replay-safety wraps its handles; production code that wants raw performance does not. The opt-in is per-handle.

Approximately two hundred lines. Foundational for the replay-determinism CI test (`bit_exact_replay_invariant`) currently maintained by ad-hoc recording; the integration moves the discipline into the type system.

## 16. ScopedView × non-consuming protocol introspection

Sometimes external code needs to *inspect* a session handle's state without consuming it: the Augur metrics broadcast wants to enumerate active sessions and report each one's protocol position; the Crucible test harness wants to assert "this handle is at a Send state" without advancing the protocol; a debugger wants to render the protocol's name and the bound resource. Today the only options are `protocol_name()` (a `std::string_view`) or destructive consumption.

`ScopedView<Carrier, Tag>` from `safety/ScopedView.h` is the right primitive: a non-owning typed reference proving the carrier is in the state denoted by the tag.

```cpp
// State tags for protocol-position introspection.
template <typename Handle> struct AtSendPosition  {};
template <typename Handle> struct AtRecvPosition  {};
template <typename Handle> struct AtSelectPosition{};
template <typename Handle> struct AtOfferPosition {};
template <typename Handle> struct AtTerminal     {};

// Mint a view if the handle is at the requested position; static_assert
// + view_ok contract enforce the precondition.
template <typename Tag, typename Handle>
[[nodiscard]] auto mint_session_view(Handle const& h CRUCIBLE_LIFETIMEBOUND)
    noexcept -> ScopedView<Handle, Tag>
    requires HandleIsAt<Handle, Tag>;
```

Augur uses `mint_session_view<AtRecvPosition<...>>(handle)` to get a non-consuming proof that the handle is awaiting a recv; reports it in the metrics snapshot; the view's lifetime scopes the report. The handle is unaffected. Type system forbids minting a view of the wrong tag (compile error via `HandleIsAt` concept).

Approximately a hundred lines for the tags + the mint factories. Resolves the long-standing question "how do we observe a session without consuming it?" without introducing a new primitive type.

## 17. Pinned constraint enforcement

The framework documents that "channel resources must be Pinned" but does not enforce it. A function that takes a non-Pinned Resource by value and creates a `SessionHandle<Proto, Resource>` succeeds at compile time; the bug is "I created handles, then moved my channel, and now the handles dangle" — a runtime UAF.

The fix is a one-line concept and a static_assert:

```cpp
template <typename R>
concept SessionResource =
    std::derived_from<std::remove_reference_t<R>, Pinned<std::remove_reference_t<R>>>
    || std::is_lvalue_reference_v<R>
    || /* explicit opt-out marker for known-stable types like spans */;

template <typename Proto, SessionResource Resource>
auto make_session_handle(Resource r) -> SessionHandle<Proto, Resource>;
```

Channels that are Pinned (MpmcRing, AtomicSnapshot, ChaseLevDeque) compile through. Channels that hold references to Pinned values (`SpscRing<T, N>&`) compile through. Plain values that are not Pinned (POD structs, `std::vector<T>`) are rejected with a routed diagnostic naming the missing `Pinned<>` derivation.

Catches the entire class of "moved my channel" bugs at compile time. Twenty lines of impact, zero runtime cost. Should ship together with §5.

## 18. ReadView × SharedPermission for SWMR sessions

Many Crucible channels are single-writer-many-reader: KernelCache (one bg compile worker writes; many dispatch threads read), AtomicSnapshot patterns (Augur metrics, Meridian calibration, MemoryPlan publication), DataNode prefetch-buffer reads, PagedKVCache prefix lookups. Each currently uses a hand-rolled atomic + acquire/release; the session-type expression is `SwmrProto = Loop<Send<T, Continue>>` for the writer and `Loop<Recv<T, Continue>>` for each reader.

The integration glues `SharedPermissionPool<ReaderTag>` to the reader's session establishment:

```cpp
template <typename T, typename ReaderTag>
class SwmrSession : Pinned<...> {
    AtomicSnapshot<T>                snap_;
    SharedPermissionPool<ReaderTag>  reader_pool_;

public:
    // Writer side: one establishment, exclusive Permission<ReaderTag>
    // (which the Pool then exposes to readers as fractional shares).
    [[nodiscard]] auto writer_handle(Permission<WriterTag>&& wp) noexcept
        -> PermissionedSessionHandle<Loop<Send<T, Continue>>,
                                       PermSet<WriterTag>, AtomicSnapshot<T>&>;

    // Reader side: borrow a fractional share from the Pool; returns
    // a session handle bound to a ReadView<ReaderTag>.
    [[nodiscard]] auto reader_handle() noexcept
        -> std::optional<PermissionedSessionHandle<Loop<Recv<T, Continue>>,
                                                     PermSet<>,
                                                     ReadView<ReaderTag>>>;

    // Mode transition: drain readers, upgrade to exclusive write.
    template <typename Body>
    bool with_drained_access(Body&& body) noexcept;
};
```

Replaces `PermissionedSnapshot<T, Tag>` (the existing primitive) with a session-typed variant whose protocol is explicit. Each reader's handle is a real session handle that respects abandonment-check semantics, propagates protocol_name for diagnostics, and composes with all the rest of the integration machinery.

Approximately two hundred lines, builds on the existing `PermissionedSnapshot` and adds the session-typed wrapper. Resolves QUEUE-6 (#281 AtomicSnapshot for Augur metrics broadcast) and QUEUE-10 (#285 property fuzzer for AtomicSnapshot torn-read prevention) with one shot.

---

# Part V — Tier C: Four Focused Payoffs

## 19. Checked compile-time capacity arithmetic

Session-queue capacities are often computed: `kMaxBuckets * kBytesPerBucket`, `kProducerCount * kBurstSize`. Today these are bare `constexpr` multiplications; if the product overflows `std::size_t`, the bug is silent. The integration uses `Checked.h`'s overflow-detecting primitives at compile time:

```cpp
template <std::size_t A, std::size_t B>
inline constexpr std::size_t safe_capacity = []() consteval {
    auto r = checked_mul<std::size_t>(A, B);
    if (!r) static_assert(false, "capacity computation overflows size_t");
    return *r;
}();

// Use:
using MyChannel = MpmcRing<Job, safe_capacity<NumProducers, BurstPerProducer>>;
```

Two-line idiom; eliminates a class of overflow bugs in protocol parameter computation. Should be the default for every `Capacity` computation in `concurrent/` and `safety/Session*.h`. Approximately fifty lines of integration site updates.

## 20. ConstantTime for crypto-payload sessions

Sessions whose payloads carry secrets (auth tokens, MAC tags, encrypted blobs) must use `ct::eq` and friends for any internal comparison. Currently this is review discipline. The integration is a per-payload-type concept and a wrapper:

```cpp
template <typename T> concept RequiresCT = /* user-marked */;

template <RequiresCT T, typename K>
struct CTSendPayload {
    Secret<T> value;
    // Send's transport is required to use ct::eq for any comparison
    // of value bytes against expected values.
};
```

The transport invocation static_asserts that the comparison helpers it uses are `ct::*`. Sessions that carry crypto payloads opt into the wrapper at the protocol declaration; non-CT comparisons inside the body produce compile errors via the discipline-enforcement static_assert.

About a hundred lines including the concept, the wrapper, and the rules for the existing `Cipher`/`Canopy` crypto paths. Resolves task #139 (ConstantTime<> wired or deleted) and task #150 (Verify preset Z3 gating) by giving the verify preset something concrete to verify.

## 21. Reflection × mCRL2 export for offline session-property verification

The session_types.md execution plan calls out mCRL2 offline verification (task #351) for safety properties beyond compile-time decidable scope. GCC 16's P2996 reflection lets us walk a session-type AST and emit an mCRL2 specification mechanically:

```cpp
// safety/SessionMcrl2Export.h (new, ~300 lines).
template <typename Proto>
[[nodiscard]] consteval std::string_view to_mcrl2() noexcept {
    // Recursively walk Proto's combinator tree via reflection.
    // Emit "act send_X; recv_X;" declarations for each Send/Recv.
    // Emit "proc P0 = ..." process equations for each protocol position.
    // Return a define_static_string'd result.
}

template <typename Γ>
[[nodiscard]] consteval std::string_view context_to_mcrl2() noexcept;
```

The output is a complete mCRL2 specification ready for `lps2pbes` and `pbes2bool`. Production tooling uses `tools/verify.sh` to feed it through the mCRL2 verifier and produce a proof certificate; the certificate is archived in Cipher and consulted by the next compile via P1967R14 `#embed` (already shipped in GCC 16).

Approximately three hundred lines for the export metafunction, plus tooling glue. Unblocks task #346 (SessionSafety.h φ family) for protocols too large for compile-time evaluation.

## 22. Refined::implies_v subtype narrowing

Task #227 adds `implies_v<P, Q>` to `Refined.h` for cross-predicate refinement: `positive` implies `non_negative`, `bounded_above<8>` implies `bounded_above<16>`, etc. Wiring this into session subsorting completes the §7 axiom family:

```cpp
template <auto P, auto Q, typename T>
    requires implies_v<P, Q>
struct is_subsort<Refined<P, T>, Refined<Q, T>> : std::true_type {};
```

A `Send<Refined<positive, int>, K>` becomes a subtype of `Send<Refined<non_negative, int>, K>` automatically. Send's payload covariance + Recv's contravariance compose with the predicate lattice for free. Fifty lines; unblocks several pending refactors in Vessel (SymbolTable predicate strengthening) and Mimic (kernel-arg validation predicates).

---

# Part VI — Trust-Boundary Integrations: The Runtime Safety Net

The integrations of Parts II–V are primarily compile-time. They give the static type system new expressivity. They prevent classes of bugs in code that has been written. They do not, by themselves, defend against the *outer world* — bytes off a network, lines from a config file, frames from a kernel driver, queries from an FFI adapter. Those inputs arrive untyped; some discipline must convert them to typed values before the rest of the system can rely on their invariants.

This part is about that conversion. It treats each of Crucible's input-from-outside-the-type-system boundaries individually, names the correct typed wrapper to produce at the boundary, and specifies how the session-type machinery downstream consumes the validated values. The runtime safety net is exactly the set of validators at these boundaries. Get the validators right and every downstream call site composes safely; get them wrong and no compile-time integration in the world can save us.

## 23. The Vessel FFI boundary

Crucible exposes a C ABI (`vessel_api.cpp`) to PyTorch, JAX, and any future frontend. Inputs are `int32_t` schema_hashes, `void*` tensor handles, `int64_t[8]` shape arrays, and `uint8_t` dtype/device ordinals. None of them is structurally typed; all of them might be malformed under a buggy or hostile frontend.

The boundary discipline:

1. Every FFI entry point's parameters arrive as `Tagged<T, vessel_trust::FromPytorch>` (semantically; the actual C signature is `T` per the C ABI, but the first thing the C++ side does is wrap each parameter in the Tagged).

2. The validator (`Vessel::validate_dispatch_request`) is the only function in the codebase that converts `Tagged<T, vessel_trust::FromPytorch>` to `Tagged<T, vessel_trust::Validated>`. It performs:
   - schema_hash lookup against the registered op table; on miss, returns `std::expected<..., DispatchError>{unexpected{SchemaUnknown}}`.
   - shape array bounds check (every dim ≥ 0, ndim ≤ 8); on violation, returns `unexpected{ShapeMalformed}`.
   - dtype/device enum range check via `Refined<in_range<0, MAX>>`; on violation, returns `unexpected{DtypeOutOfRange}`.
   - data_ptr alignment and non-null check (where required); on violation, returns `unexpected{PointerInvalid}`.

3. The validated `Tagged<T, Validated>` is then passed to `Vigil::record_op`, whose signature explicitly requires the `Validated` provenance. Compile-time enforcement of "the validator must run before the runtime accepts the input."

4. The dispatch session itself (per §6) is `Loop<Recv<Tagged<DispatchRequest, FromPytorch>, Send<Tagged<MockHandle, Validated>, Continue>>>`. The Recv's payload is the raw input; the Send's payload is the validated mock-handle. The protocol's type captures the validation-as-state-transition.

The bytes that arrive at the FFI boundary are untrusted; the values that leave the validator are typed; the static type system enforces that nothing untyped reaches downstream code. Buggy or hostile frontends cannot bypass the validator without bypassing the type system. The validator's correctness is the entire trust kernel for the FFI.

The runtime safety net here is approximately five hundred lines of validator code (already partially written in `vessel_api.cpp`), plus the Tagged-flow integration of §6, plus a CI test that fuzzes the FFI with random byte sequences and verifies the validator catches every malformation.

## 24. The Cipher cold-tier deserialization boundary

Cipher loads RegionNodes, MemoryPlans, and proof certificates from object storage (S3/GCS) at restart and during federation. The bytes are external — they were written by some past Crucible run, possibly on different hardware, possibly with a different binary version. The deserializer must defensively validate every field before any internal API consumes the result.

The discipline mirrors the FFI:

1. Bytes arrive as `Tagged<std::span<const std::byte>, source::Durable>` from the io_uring read completion.

2. The deserializer (`Cipher::deserialize_region_node`) consumes the Tagged span and produces `std::expected<Tagged<RegionNode, source::Sanitized>, DeserializeError>`. On the happy path, the RegionNode is fully validated:
   - schema version is recognized;
   - merkle hash matches the recomputed hash over its content;
   - all internal pointers (to operands, to child nodes) are inside the just-deserialized arena;
   - all `OpIndex` fields are `Refined<bounded_above<MAX_OPS>>`;
   - all `Permission` fields are absent (permissions don't serialize; they are minted fresh on load);
   - all `Pinned` fields are flagged for in-place reconstruction (no relocation after this point).

3. The validated RegionNode is wrapped in a `Tagged<RegionNode, source::Sanitized>` and handed to the in-memory DAG via an explicit `register_loaded_region` API that takes `Tagged<..., Sanitized>`.

4. Federation across runs (task #353) relies on certificate hash comparison: each loaded RegionNode's certificate-hash is checked against an expected manifest hash; on mismatch, `unexpected{CertificateHashMismatch}` and the load aborts.

The session-type integration: the load operation is itself a session — `LoadRegionSession = Send<ContentHash, Recv<Tagged<RegionNode, Sanitized>, End>>`, where the Send is the Cipher request and the Recv is the validated result. The protocol's type captures both the request-response shape and the provenance flow.

The runtime safety net is approximately eight hundred lines of deserializer code (mostly already written in `Cipher::deserialize_*`), plus the Tagged provenance integration (§6), plus a property fuzzer that flips random bits in a known-good serialized RegionNode and verifies the deserializer rejects every flipped variant.

## 25. The CNTP cross-layer boundary

CNTP carries Layer 1 transport bytes containing Layer 2 SWIM frames containing Layer 3 Raft RPCs containing Layer 4 collective announcements containing Layer 5 NetworkOffload directives. Each upper layer's payload IS a session type (per session_types.md §II.12.7). The boundary discipline is recursive: Layer 1 validates length + integrity + encryption and produces `Tagged<std::span<const std::byte>, source::External>`; Layer 2 receives the bytes and runs its own validator producing `Tagged<SwimProbe, source::Sanitized>` (or rejects); Layer 3 does the same for its own payload type; etc.

The session-type integration uses the `Delegate` combinator (Honda 1998 throw/catch, already shipped in `SessionDelegate.h`) for the cross-layer handoff. Layer 1's session is:

```cpp
using CntpLayer1 = Loop<Select<
    Delegate<SwimProto,         Continue>,   // delegate to Layer 2
    Delegate<RaftProto,         Continue>,   // delegate to Layer 3
    Delegate<CollectiveProto,   Continue>,   // delegate to Layer 4
    Delegate<NetOffloadProto,   Continue>,   // delegate to Layer 5
    End
>>;
```

The Layer 1 transport hands out higher-layer endpoints to the respective subsystems; each subsystem runs its own protocol on top. Layer 1's type gives no hint of what Layer 2-5 carry — opacity at the transport level, protocol integrity at the upper layers. Composed φ = (φ_Layer1 ∧ φ_Layer2 ∧ φ_Layer3 ∧ …) by the compositionality theorem (session_types.md §II.12.4).

The Tagged provenance flows recursively: Layer 1 produces `Tagged<bytes, External>`; Layer 2's validator consumes those bytes and produces `Tagged<SwimProbe, Sanitized>`; Layer 3's validator consumes `Tagged<bytes, External>` (a different bytes — the inner Raft payload) and produces `Tagged<RaftRpc, Sanitized>`. At each layer, the boundary between External and Sanitized is the validator; the upper layer's typed API requires Sanitized.

The runtime safety net is the per-layer validator (one per layer × payload type, approximately a thousand lines aggregate for the existing five layers). Composes with Tier-S §5 (PermissionedSessionHandle for the cross-layer session) and Tier-A §11 (OneShotFlag for cross-node crash detection at Layer 1).

## 26. The system-call boundary

Every syscall returns either a valid result or an errno. The C++ wrapper (`FileHandle`, `MmapRegion`, `EpollWaitResult`, future `RdmaQp`, `IoUringSqe`) is responsible for converting "raw int + errno" into a typed `std::expected<T, SyscallError>`. The boundary discipline:

1. The wrapper's syscall invocation handles EINTR by retrying (per `FileHandle::read_full`'s pattern).

2. The wrapper translates errno to a typed enum (`SyscallError::PermissionDenied`, `SyscallError::IOError`, `SyscallError::Interrupted`, etc.); returns `std::expected<T, SyscallError>` on the result.

3. Callers downstream consume the `expected` and handle the error variant explicitly. The session-type integration: any session that can encounter a syscall failure includes a Crash branch (or an explicit error branch in an Offer) for that failure mode. CNTP Layer 1 RDMA reads have `Offer<Recv<Bytes, K>, Recv<RdmaCompletionError, ErrorRecovery>>`; the error branch is structurally part of the protocol.

The discipline closes the long-standing "errno can return -1 and the caller forgets to check" class of bugs. Every error-returning syscall wrapper has a `[[nodiscard]] std::expected<>` return type; the compiler warns if the result is unused; the negative-errno integer never escapes the wrapper.

Resolves tasks #103 (TraceLoader FILE* Linear<ScopedFile>), #102 (Cipher streams as Linear<ScopedFd>), #106 (TraceLoader std::expected error codes), #105 (estimate_serial_size Refined return).

## 27. The configuration parsing boundary

Configuration arrives from environment variables, YAML files, command-line flags, and the k8s operator's CRD reconciliation. None of it is typed; every field is either a string or a JSON value. The discipline:

1. Each configuration source produces `std::expected<Tagged<RawConfig, source::FromConfig>, ConfigError>`.

2. The validator runs Refined predicates field-by-field: every numeric field is `Refined<in_range<MIN, MAX>>`; every path field is `Refined<is_existing_path>`; every host field is `Refined<is_valid_hostname>`. Validators are pure functions of the raw value; failures aggregate into a list returned as `unexpected<std::vector<FieldError>>`.

3. The validated config is `Tagged<ValidatedConfig, source::Sanitized>` and is the only type the runtime startup paths accept.

4. Config changes mid-run (k8s operator updates the CRD, the user signals a reload) re-run the validator and produce a new Sanitized config; the runtime swaps via `AtomicSnapshot<Tagged<ValidatedConfig, Sanitized>>` per §18.

About four hundred lines for the validator + the Refined predicate library extension. Resolves the long-standing "config field X was misspelled and the runtime silently used the default" class of bugs.

## 28. Summary: where compile-time and runtime safety meet

The pattern across §23–§27 is uniform:

| Boundary | Untyped input | Validator output | Downstream-consumer signature |
|---|---|---|---|
| Vessel FFI | C ABI args | `Tagged<T, Validated>` | `Vigil::record_op(Tagged<T, Validated>)` |
| Cipher cold-tier | byte span | `Tagged<RegionNode, Sanitized>` | `register_loaded_region(Tagged<..., Sanitized>)` |
| CNTP per-layer | bytes from below | `Tagged<UpperPayload, Sanitized>` | upper-layer session's Recv |
| Syscall | int + errno | `std::expected<T, SyscallError>` | session's Crash/error branch |
| Config | env/YAML/CLI | `Tagged<ValidatedConfig, Sanitized>` | runtime startup APIs |

The runtime safety net is the five validators. The compile-time discipline is that nothing downstream accepts an untyped value. The session-type integrations of Parts II–V then propagate the typed values through the runtime's protocols — preserving Tagged provenance, Refined predicates, Permission ownership — without requiring any additional defensive validation at internal call sites.

Eliminating defensive validation at internal call sites is the integration's largest concrete payoff. Crucible has dozens of `pre()` preconditions that re-check what the caller already validated; each one is removable once the validator's output type carries the proof. The aggregate is several thousand lines of removed runtime checks plus an equal or greater number of removed runtime branches.

The trust kernel shrinks to the validators. Bug-hunting effort concentrates there. Verification effort (formal proofs, fuzz testing, mCRL2 model-checking) targets there. The rest of the runtime can rely on its types.

---

# Part VII — Concrete Refactor Targets in Production Code

This part enumerates production code paths that should adopt the integrations above, in priority order. Each refactor is independently doable; the order respects dependencies (e.g. Vessel cannot adopt Tagged provenance until §6 ships; KernelCache cannot adopt SwmrSession until §18 ships).

## 29. Vigil mode → Machine + Session bridge

`include/crucible/Vigil.h:47-51` declares `Vigil::mode_` as a bare `enum class`. Tasks #34 and #78 want it as `Machine<VigilMode>` and `Session<...>` respectively; the §10 integration resolves the conflict by shipping both views via `SessionFromMachine<Machine<VigilMode>, G_Vigil_Local>`.

The refactor:
- Replace `enum class VigilMode { Recording, Replaying, Serving, Flushing }` with `safety::Machine<VigilMode_State>` where `VigilMode_State` is a discriminated union of per-mode payload structs.
- Wrap the Machine in `SessionFromMachine` for external observers (Augur metrics broadcast, test harnesses, debug renderers).
- Mode transitions become typed function calls (`transition_to<RecordingMode>(...)`); each transition consumes the old Machine and returns a new one.

About three hundred lines of refactor; closes tasks #33, #34, #41, #46, #78, #87, #88, #89, #108, #165. The largest single payoff among the production refactors.

## 30. Transaction → CheckpointedSession + Tagged provenance

`include/crucible/Transaction.h:28-57` is currently a hand-rolled state machine with explicit begin/commit/abort methods. Task #101 wants it as `Session<TxStatus, ...>`. The integration uses `CheckpointedSession<CommitPath, RollbackPath>` (already shipped in `SessionCheckpoint.h`):

```cpp
using TxnSession = CheckpointedSession<
    /* commit  */ Send<TxOp, Recv<TxAck, End>>,
    /* abort   */ Send<TxAbort, End>>;
```

Begin establishes the session; `op()` corresponds to a Send/Recv on the commit path; `commit()` is the protocol's terminal Send; `abort()` is the rollback's terminal Send. The session's PermSet (per §5) captures which mutable state the transaction has provisionally acquired; rollback restores; commit promotes.

About two hundred lines; resolves task #101 and #164.

## 31. TraceRing → PermissionedSpscChannel + Tagged

> **SHIPPED-AS marker (added 2026-04-27, partial).** The session-typed wiring SHAPE shipped as `sessions/SpscSession.h` (commit c2ceb86, refined in SPSC-FIX-1..4). This is the framework + tooling: typed-session factories `producer_session<Channel>(handle&)` / `consumer_session<Channel>(handle&)` over `PermissionedSpscChannel<T, N, Tag>`, transport helpers `blocking_push` / `blocking_pop`, integration test `test/test_spsc_session.cpp`, head-to-head bench `bench/bench_spsc_session.cpp`. Asm-comparison capability ships generic via `cmake -DCRUCIBLE_DUMP_ASM=ON`. **Honest scope**: SpscSession.h uses EmptyPermSet by design (TraceRing-shape SPSC streams plain payloads, no wire-permission transfer); the §5 PermSet evolution path is exercised by FOUND-C v1's own integration test. **Still pending**: the production-side switchover — Vigil's recording path, BackgroundThread's drain, MerkleDag's TraceRing consumer, vessel_api's dispatch hot path all still call into `TraceRing.h` directly with hand-coded acquire/release. The TraceRing-internal rewrite (replace TraceRing's atomics with PermissionedSpscChannel internally + plumb the typed handles to consumers) is the still-pending finish of SAFEINT-R31. Adding Tagged<TraceEntry, vessel_trust::Validated> to the wire (§5/§6 composition) is the next layer.

`include/crucible/TraceRing.h` is a Pinned SPSC ring with hand-coded acquire/release. Task #384 (SEPLOG-INT-1) wants to wire it as a `PermissionedSpscChannel<TraceEntry, N, TraceRingTag>`. The §5/§6 integration extends to add Tagged provenance:

- TraceEntry payloads carry `Tagged<TraceEntry, vessel_trust::Validated>` because they originate at the Vessel FFI boundary (§23).
- The producer endpoint (Vessel dispatch) holds `Permission<TraceRingTag::Producer>`; the consumer endpoint (bg drain) holds `Permission<TraceRingTag::Consumer>`.
- The session is `Loop<Send<Tagged<TraceEntry, Validated>, Continue>>`; the dual is the consumer's Recv loop.
- bg drain's signature requires `Tagged<TraceEntry, Validated>` — defensive re-validation is removed from the drain code.

About four hundred lines including the wrapping + the bg-side signature changes. Resolves #37, #384.

## 32. KernelCache → SwmrSession with content-addressed payloads

`include/crucible/Mimic/KernelCache.h` is a SWMR table mapping `(content_hash, device_capability) → CompiledKernel*`. The §18 integration wraps it as a `SwmrSession<CompiledKernel, KernelCacheReaderTag>`; the §22 integration declares `is_subsort<ContentAddressed<CompiledKernel>, CompiledKernel>` so cross-session deduplication composes for free.

The refactor:
- Replace the hand-rolled atomic-pointer-swap publication with a `SwmrSession`.
- The bg compile worker's session is `Loop<Send<Transferable<CompiledKernel*, KernelCacheTag>, Continue>>`; the producer transfers ownership of each new kernel to the cache via the Transferable payload.
- Reader sessions get `ReadView<KernelCacheReaderTag>` proofs; cache lookup is bounded by the ReadView's lifetime.
- Mode-transition for cache invalidation (architecture mutation, kernel re-extraction) uses `with_drained_access`.

About five hundred lines; resolves #356 (SEPLOG-K2 KernelCache SWMR publication as typed session), #281 (QUEUE-6 AtomicSnapshot for Augur metrics broadcast — same pattern), and the long-pending content-addressed-dedup cleanup.

## 33. Cipher tier promotion → Delegate + Tagged

Cipher's hot→warm→cold tier promotion (CRUCIBLE.md §9.2) is currently a hand-coded sequence of mutex-protected handoffs. The integration uses `Delegate<HotEntryProto, K>` for the hot→warm handoff and `Delegate<WarmEntryProto, K>` for warm→cold; each Delegate transfers the entry's session along with its `Permission<TierTag>`; the receiving tier owns the entry exclusively after the transfer.

The Tagged provenance integration adds `Tagged<CipherEntry, source::Durable>` for warm-tier-loaded entries and `Tagged<CipherEntry, source::Computed>` for hot-tier-just-computed entries; the type system distinguishes them downstream so audit code can render which entries came from where.

About four hundred lines; resolves the Cipher promotion correctness audit that has been pending since the Cipher rewrite.

## 34. CNTP nested layers → Session over Session via Delegate

CNTP Layer 1 currently dispatches to Layer 2-5 via a manual ad-hoc routing table. The §25 integration replaces it with the canonical `CntpLayer1` session shown in §25, with each `Delegate<UpperProto, Continue>` arm corresponding to one upper-layer protocol.

The refactor:
- Each upper-layer subsystem registers its session protocol with the Layer 1 dispatcher at startup.
- Layer 1's main loop is one `branch()` call on the Offer dual, dispatching incoming bytes to the registered upper-layer.
- The upper-layer session establishment delegates the Layer 1 endpoint via `Accept<UpperProto, K>`.
- Crash detection (§11) wires per-peer OneShotFlags; on Layer 1 RDMA completion error, the relevant upper layer's session transitions to Stop.

About six hundred lines; resolves #355, #356, #357, #358 (the K-series production tasks). The largest refactor in the integration plan; depends on all prior Tier S/A integrations landing first.

## 35. BackgroundThread pipeline → permission_fork + StreamSession

`include/crucible/BackgroundThread.h` runs a sequential pipeline (drain → build → transform → compile). Task #315 (SEPLOG-D1) wants it as a staged pipeline; the §9 integration uses `session_fork` over a global type `G_BgPipeline` whose roles are the four stages; the §8 integration backs each inter-stage channel with `OwnedRegion<StageMessage, StageTag>`.

The refactor:
- Replace the for-loop dispatcher with a `session_fork` over five roles (drain, build, transform, compile, publish).
- Each stage's local protocol is `Recv<StreamSink<...>, Send<StreamSource<...>, Continue>>` — receive a stream from the previous stage, send a stream to the next.
- Inter-stage channels are arena-backed OwnedRegions; each stage's session establishment binds to the upstream's region.
- Per-stage permissions (`Permission<DrainTag>`, etc.) flow with the Transferable payloads as work progresses.

About six hundred lines; resolves #288 (already partially done), #315.

## 36. Inter-process channels (DataNode prefetch, InferenceSession) → PermissionedSession

DataNode's CPU-loader-to-GPU-consumer prefetch and InferenceSession's prefill/decode are inter-process channels with crash-stop semantics. The integrations apply uniformly:
- `PermissionedSessionHandle` for the channel session (§5).
- `Tagged<source::External>` provenance for inputs from peers (§23).
- `OneShotFlag` for crash detection (§11).
- `Borrowed<T, Tag>` payloads for streaming reads (§8 + §5).

About eight hundred lines aggregate for both subsystems; resolves #357 and the DataNode-related tasks.

## 37. Refactor priority and schedule

Order of operations:
1. Land §6 (Tagged-provenance subsort axioms — five lines, codebase-wide effect).
2. Land §7 (Refined subsort axioms — five lines, codebase-wide effect).
3. Land §17 (SessionResource Pinned concept — twenty lines, prevents bug class).
4. Land §11 (OneShotFlag × crash transport — three hundred lines, enables L8 runtime).
5. Land §22 (Refined::implies_v — fifty lines, completes axiom family).
6. Land §5 (PermissionedSessionHandle — six hundred lines, the thesis).
7. Land §9 (session_fork — two hundred lines, multi-party flagship).
8. Land §10 (Machine ↔ Session bridge — one hundred fifty lines, resolves Vigil).
9. Land §8, §13, §14, §15, §18 (Tier-A/B integrations — about a thousand lines aggregate).
10. Begin production refactors (§29–§36) in dependency order.

The first eight items are about two thousand lines of new framework code, shippable in three to four weeks of focused work. The production refactors are larger but incremental; they can land one subsystem at a time without forcing a flag-day cutover. The integration is monotone — code that does not adopt a new primitive continues to compile unchanged.

---

# Part VIII — Deliberate Non-Integrations

Some pairings look natural but should not be done. Documenting them here prevents future contributors from re-discovering the design landmines.

## 38. Lazy / Once for session establishment

`safety/Once.h`'s `Lazy<T>` and `Once` are first-call-wins primitives: if multiple threads race on the first call, exactly one runs the initializer; the others wait. This composition fails for session establishment because of two facts:

- **Sessions have a unique establish-time owner.** Producer-side establishment hands the producer endpoint to a specific thread; consumer-side establishment hands the consumer endpoint to a different specific thread. There is no notion of "winner takes all"; both endpoints are needed and both are needed by their respective owners.

- **Loser threads have no protocol they can run.** If two threads both want to establish the producer side and only one wins, what does the loser do? In the `Lazy<T>` model, the loser observes the value and proceeds; in a session model, the loser has no handle to proceed with.

The right primitives for asynchronous session establishment are `PublishOnce<ChannelResource*>` (§14) and `SetOnce<T*>` for known-single-publisher patterns. `Lazy` and `Once` are correct for *initialization* — bringing a value into existence — but wrong for *establishment* — handing out endpoints to a coordinated party.

## 39. Secret ⩽ T as a payload subsort

The §6 axiom family carefully *omits* `is_subsort<Secret<T>, T>`. The reason is information-flow asymmetry: a Secret value should never silently flow to a position expecting plain T, because the position's downstream code may emit, log, hash, or compare the value in a way that leaks classified content. The reverse direction (`T ⩽ Secret<T>` — classifying a previously-public value) is fine; the type system already supports this via explicit `Secret{value}` construction; auto-deriving it is unnecessary.

The rule generalizes: subsort axioms should be added in the direction that *strengthens* information ("validated" carries more info than "raw"; "positive" carries more info than "any int"). Subsort axioms should never be added in the direction that *erases* information ("Secret carries less info than T" is true mathematically but dangerous operationally).

## 40. Linear<SessionHandle<P, R>>

A SessionHandle is already linear (move-only with `[[nodiscard]]` and abandonment-check). Wrapping it in `Linear<SessionHandle<P, R>>` adds:
- One `consume()` indirection (the user must call `.consume()` to get the handle out).
- One redundant linearity check.
- Zero new safety properties.

The `is_writeonce_v` static_assert in `safety/Mutation.h` already rejects `AppendOnly<WriteOnce<T>>` with this argument; the same logic should be a static_assert on `Linear<SessionHandle<...>>`. Currently it isn't; should be added as part of §17's Pinned-constraint enforcement, with the same diagnostic style.

## 41. ScopedView wrapping a permission set instead of a state tag

`ScopedView<C, Tag>` proves the carrier C is in a state denoted by Tag. The natural-looking extension is `ScopedView<PermissionedSessionHandle, ATagSet<...>>` proving a handle holds a particular permission set. This composition fails because the permission set is *dynamic* — Send/Recv events evolve it across the session — and ScopedView is *static* — the tag is fixed at mint time. The proof would be wrong the moment the next Send fired.

The right primitive for non-consuming permission introspection is a separate `PermissionInspect<H>` view that captures a *snapshot* of the handle's PermSet at mint time and is invalidated by the next consuming operation. About fifty lines; ship together with §16 if needed; otherwise defer until a use case demands it.

## 42. Where ScopedView and SessionHandle disagree

Both ScopedView and SessionHandle are typestate carriers, but they differ in lifetime model:
- ScopedView is non-consuming; multiple views can coexist; assignment is deleted but copy is allowed.
- SessionHandle is consuming; the abandonment check requires reaching End or Stop; copy is deleted, move is the only transfer.

The integration of §16 carefully distinguishes: ScopedView<SessionHandle, AtPosition> is a non-consuming inspection view; the underlying SessionHandle remains consuming. Conflating them would either weaken the SessionHandle's linearity or weaken ScopedView's read-borrow semantics. Keep them separate; let §16 bridge them at call sites that need both.

---

# Part IX — Implementation Order and Effort Budget

## 43. The twelve-week plan

The integrations split naturally into four epochs:

**Epoch I (weeks 1-2): Subsort axioms + concept gates.** Land §6 (Tagged subsorts), §7 (Refined subsorts), §22 (implies_v wiring), §17 (Pinned concept). Total: about two hundred lines of new framework code; roughly one thousand lines of existing call site updates as production code opts in. Low risk; high payoff. Unblocks every subsequent integration.

**Epoch II (weeks 3-5): The PermissionedSessionHandle thesis.** Land §5 (PermissionedSessionHandle), §11 (OneShotFlag crash transport), §9 (session_fork). **Landed 2026-04-27 as FOUND-C v1** (~2,100 lines of framework code across three new headers, plus ~680 lines of integration test, 50 lines of negative-compile fixtures, and a two-tier zero-cost bench — see `misc/27_04_csl_permission_session_wiring.md`). Medium-high risk converted to LOW — the asm-identical machine-code-parity claim is verified at compile time via `static_assert(sizeof == sizeof)` plus a runtime bench. Negative-compile harness has 10 fixtures with triple-anchor regex matching; bench shows sub-nanosecond per-op for both bare and PSH (delta is bench-harness microarchitectural noise, not framework overhead).

**Epoch III (weeks 6-8): Streaming, swap, and SWMR.** Land §8 (StreamSession over OwnedRegion), §10 (Machine bridge), §18 (SwmrSession). Total: about six hundred lines of new framework code. Medium risk; provides the patterns the production refactors will rely on.

**Epoch IV (weeks 9-12): Production refactors.** Land §29-§36 in priority order. Total: about three thousand lines of production code change, distributed across nine subsystems. Each refactor is independently testable; no flag-day cutover. By the end of Epoch IV, every production channel is a typed session.

The remaining Tier-B/C integrations (§13 Secret payloads, §14 PublishOnce establishment, §15 AppendOnly event log, §19 Checked capacity, §20 ConstantTime payloads, §21 Reflection mCRL2 export) can be interleaved opportunistically by whichever production refactor needs them. None are critical path.

## 44. Per-task line estimates

| Integration | New framework lines | Production update lines | Risk |
|---|---|---|---|
| §6 Tagged subsort | 30 | 200 | Low |
| §7 Refined subsort | 30 | 100 | Low |
| §17 Pinned concept | 20 | 50 | Low |
| §22 implies_v wiring | 50 | 50 | Low |
| §5 PermissionedSessionHandle | 1,300 (shipped 2026-04-27 — full functionality, all 5 protocol-head specialisations, Loop balance + branch convergence enforcement, debug abandonment-tracker enrichment, three payload markers, both establishment factories) | 0 | Low (shipped) |
| §11 OneShotFlag crash | 50 (shipped — `with_crash_check_or_detach` helper composes with existing `bridges/CrashTransport.h::CrashWatchedHandle`) | 0 | Low (shipped, partial — global `notify_crash` hook explicitly NOT shipped per FOUND-C SHIPPED-AS) |
| §9 session_fork | 100 (shipped — delegates to `permission_fork`, projects per role via `Project<G, Role>`, RAII rebuild on join) | 0 | Low (shipped) |
| **PermSet algebra** (new line) | 370 (shipped 2026-04-27 — `permissions/PermSet.h`) | 0 | Low (shipped) |
| **SessionPermPayloads** (new line) | 410 (shipped 2026-04-27 — `sessions/SessionPermPayloads.h`) | 0 | Low (shipped) |
| **PSH integration test + bench + neg-compile** (new line) | 730 (shipped 2026-04-27) | 0 | Low (shipped) |
| §8 StreamSession | 300 | 0 | Medium |
| §10 Machine bridge | 150 | 0 | Low |
| §18 SwmrSession | 200 | 0 | Medium |
| §13 Secret payloads | 100 | 100 | Low |
| §14 PublishOnce establish | 100 | 50 | Low |
| §15 AppendOnly event log | 200 | 50 | Low |
| §19 Checked capacity | 50 | 50 | Low |
| §20 ConstantTime payloads | 100 | 0 | Low |
| §21 Reflection mCRL2 export | 300 | 0 | Medium |
| §29 Vigil refactor | 0 | 300 | Medium |
| §30 Transaction refactor | 0 | 200 | Low |
| §31 TraceRing refactor | 0 | 400 | Medium |
| §32 KernelCache refactor | 0 | 500 | Medium |
| §33 Cipher tier refactor | 0 | 400 | Medium |
| §34 CNTP nested-session refactor | 0 | 600 | High |
| §35 BackgroundThread pipeline | 0 | 600 | Medium |
| §36 Inter-process channels | 0 | 800 | Medium-high |
| **Totals** | **~2,800** | **~4,650** | — |

About 7,500 lines of total integration work. Spread over twelve weeks at one engineer's pace, or six weeks with two engineers working in parallel on independent epochs.

## 45. Risk register

The technical risks, in descending priority:

1. **Template-instantiation cost in PermissionedSessionHandle.** Deeply nested protocols with large PermSets could push compile times into the multiple-second-per-instantiation range. Mitigation: every metafunction is fold-expression based (linear), `define_static_array` memoizes canonical PermSet forms, and the bench harness includes per-channel template-instantiation timing tests.

2. **Negative-compile diagnostic noise.** Routing every permission-flow violation through `CRUCIBLE_SESSION_ASSERT_CLASSIFIED` should produce three-line diagnostics, but template instantiation context can blow them up. Mitigation: every PermissionedSessionHandle Send/Recv has an explicit `requires` clause that fires before the inner permission-flow logic, and the `subtype_rejection_reason_t` mechanism (task #380, ship together) names the failing pair.

3. **Production refactor flag-day risk.** Epoch IV touches nine subsystems. If a subsystem refactor introduces a subtle correctness bug, it may take iterations to find. Mitigation: each refactor lands behind a runtime feature flag for the first iteration; correctness is verified against the prior implementation via differential testing; the flag is removed when CI shows green for two weeks.

4. **CNTP nested-session refactor scope.** §34 is the largest production refactor and depends on every prior epoch. If any prior integration slips, §34 slips with it. Mitigation: §34 is scheduled last and can be deferred without blocking the rest; the prior integrations stand on their own merits.

5. **mCRL2 export pipeline complexity.** §21 produces an mCRL2 specification from a session-type AST; the spec must be syntactically valid and semantically equivalent. Mitigation: ship with a small set of golden test cases (each protocol's expected mCRL2 output stored alongside the protocol declaration); the test compares byte-by-byte.

The risks are manageable. None requires a research breakthrough; all are engineering risks of the kind Crucible regularly takes.

---

# Part X — Verification Strategy

The integration ships with a verification story matching the rest of the safety stack: compile-time tests, negative-compile tests, sanitizer presets, mCRL2 offline proofs for the deeper properties, and Lean formalization for the load-bearing theorems.

## 46. Compile-time tests

Every new metafunction in the framework headers (§5 PermSet operations, §7 Refined subsorts, §22 implies_v wiring, §10 Machine-Session bridge) ships with a self-test block in the same header following the established `detail::*_self_test` pattern. The tests cover positive cases (the metafunction returns the expected type/value), negative cases (the metafunction's primary template fires when applicable), composition cases (multiple metafunctions interact correctly), and edge cases (empty inputs, single-element inputs, maximum-size inputs).

The self-test blocks are static_assert-only; they fail at header-inclusion time. CI verifies that every TU including the header continues to compile. About fifty lines of self-test per major metafunction; cumulative cost is approximately twenty percent of the framework's line count, which is appropriate for the criticality of the code.

## 47. Negative-compile tests

The negative-compile harness (`test/safety_neg/`) gains one neg-compile test per integration that defines a new compile-error class. For §5 PermissionedSessionHandle: a test that attempts to send a Transferable<T, X> from a handle whose PermSet does not contain X; the test must fail to compile with the expected diagnostic. For §6 Tagged: a test that attempts to flow a `Tagged<T, source::External>` to an API requiring `Tagged<T, source::Sanitized>`; same pattern.

Each neg-compile test is a five-line .cpp file that the CMake harness builds with `WILL_FAIL=TRUE` and pattern-matches the compiler diagnostic against an expected substring. Approximately fifteen new neg-compile tests across the integration; addresses task #354 (SEPLOG-J4 Negative-compile test harness for session types).

## 48. Sanitizer presets

The `tsan` preset must remain green across all integrations. PermissionedSessionHandle's permission flow is type-level only (no runtime atomics), so TSan has nothing to catch beyond the underlying transport's existing state. OneShotFlag's release/acquire pairing is already TSan-validated. SwmrSession's Pool atomics are validated by the existing `PermissionedSnapshot` test suite.

ASan validates the OwnedRegion-backed StreamSession's lifetime: each Borrowed<T, Tag> view is bounded by the next session step; ASan catches use-after-step violations in tests that intentionally retain views past their scope.

## 49. mCRL2 offline proofs

The §21 reflection-based mCRL2 export pipeline produces verifier input for every shipped protocol pattern. The CI runs the pipeline at every commit; any pattern that fails verification (deadlock, livelock, balanced+ violation, crash-branch absence) blocks merge. The proof certificates are archived in Cipher and consulted by the next compile via `#embed`.

About a hundred lines of CMake glue + a hundred lines of CI workflow + the §21 pipeline itself. Resolves task #351 (mCRL2 export pipeline) and task #352 (Cipher session-type certificate schema).

## 50. Lean formalization

The Lean 4 development at `lean/Crucible/` (1,312 theorems, zero sorry as of the last status update) gains modules for the load-bearing integration theorems:

- `lean/Crucible/PermissionFlow.lean` — formalizes the PermSet type-list operations and proves the master theorem "permission balance is preserved by every Send/Recv reduction event."
- `lean/Crucible/AssociationPreservation.lean` — formalizes HYK24 Theorem 5.8 (association is preserved by reduction) lifted to PermissionedSessionHandle.
- `lean/Crucible/StreamSessionLifetime.lean` — formalizes the Borrowed<T, Tag> view-lifetime theorem: every view minted at step k is invalidated by step k+1.
- `lean/Crucible/CrashFlow.lean` — formalizes the OneShotFlag-driven crash transition: a session in state P at step k transitions to Stop iff the relevant peer's crash flag is set at step k.

About two thousand lines of Lean for the four modules. Provides the formal underpinning for the integration's correctness; aligns with the project's existing Lean discipline.

---

# Appendix A — Catalog of Integration Points (compact reference)

For quick reference, the complete catalog of integration points proposed in this document:

| # | Title | Tier | New code | Production updates | Risk |
|---|---|---|---|---|---|
| 5 | PermissionedSessionHandle | S | 600 | 0 | High |
| 6 | Tagged provenance through protocols | S | 30 | 200 | Low |
| 7 | Refined as payload subsort | S | 30 | 100 | Low |
| 8 | OwnedRegion-backed StreamSession | A | 300 | 0 | Medium |
| 9 | session_fork multi-party | A | 200 | 0 | Medium |
| 10 | Machine ↔ Session bridge | A | 150 | 0 | Low |
| 11 | OneShotFlag crash transport | A | 300 | 200 | Medium |
| 12 | Monotonic step counters → balanced+ | A | 200 | 0 | Medium |
| 13 | Secret payloads | B | 100 | 100 | Low |
| 14 | PublishOnce session establishment | B | 100 | 50 | Low |
| 15 | AppendOnly session event log | B | 200 | 50 | Low |
| 16 | ScopedView protocol introspection | B | 100 | 0 | Low |
| 17 | Pinned constraint concept | B | 20 | 50 | Low |
| 18 | SwmrSession with SharedPermissionPool | B | 200 | 0 | Medium |
| 19 | Checked compile-time capacity | C | 50 | 50 | Low |
| 20 | ConstantTime crypto payloads | C | 100 | 0 | Low |
| 21 | Reflection × mCRL2 export | C | 300 | 0 | Medium |
| 22 | Refined::implies_v subtype narrowing | C | 50 | 50 | Low |

# Appendix B — API sketches for the load-bearing types

### PermissionedSessionHandle (§5) public surface

```cpp
namespace crucible::safety::proto {

template <typename Proto, typename PermSet, typename Resource, typename LoopCtx = void>
class [[nodiscard]] PermissionedSessionHandle
    : public SessionHandleBase<Proto>
{
public:
    using protocol      = Proto;
    using perm_set      = PermSet;
    using resource_type = Resource;

    static constexpr std::string_view protocol_name() noexcept;
    static constexpr std::string_view perm_set_name() noexcept;

    // Send: the new PermSet evolves per the payload type.
    template <typename T, typename Transport>
        requires SendablePayload<T, PermSet>
    [[nodiscard]] auto send(T msg, Transport tx) && noexcept
        -> std::expected<NextHandleAfterSend<T>, CrashEvent>;

    // Recv: the new PermSet gains permissions from Transferable/Returned payloads.
    template <typename Transport>
    [[nodiscard]] auto recv(Transport tx) && noexcept
        -> std::expected<std::pair<RecvPayload, NextHandleAfterRecv>, CrashEvent>;

    // Select / Offer: branch index governs the new PermSet.
    template <std::size_t I, typename Transport>
        requires (I < BranchCount)
    [[nodiscard]] auto select(Transport tx) && noexcept
        -> std::expected<NextHandleAfterSelect<I>, CrashEvent>;

    template <typename Transport, typename Handler>
    auto branch(Transport tx, Handler h) && noexcept
        -> std::expected<HandlerResult, CrashEvent>;

    // Terminal: surrender all currently-held permissions back to the
    // caller's scope. The handle is consumed.
    template <typename... ExpectedExitPerms>
    [[nodiscard]] auto close() && noexcept
        -> std::tuple<Resource, Permission<ExpectedExitPerms>...>;

    // Detach: explicit no-op consume for handles whose protocol is
    // inherently infinite (Loop without close branch); only valid when
    // the protocol's reachable terminal state is Stop or Continue,
    // never End.
    void detach() && noexcept;
};

}  // namespace crucible::safety::proto
```

### session_fork (§9) public surface

```cpp
namespace crucible::safety::proto {

template <typename G, typename SessionTag, typename Resource,
          typename... RoleBodies>
[[nodiscard]] Permission<SessionTag>
session_fork(
    Resource&                     shared_channel,
    Permission<SessionTag>&&      whole,
    RoleBodies&&...               bodies) noexcept
    requires (sizeof...(RoleBodies) == roles_of_t<G>::size)
          && IsBalancedPlus<G>
          && IsCrashSafe<G, RoleBodies...>;

// Variant: returns the rebuilt permission AND a tuple of per-role results.
template <typename G, typename SessionTag, typename Resource,
          typename ResultType, typename... RoleBodies>
[[nodiscard]] auto session_fork_collect(
    Resource&                     shared_channel,
    Permission<SessionTag>&&      whole,
    RoleBodies&&...               bodies) noexcept
    -> std::pair<Permission<SessionTag>, std::array<ResultType, sizeof...(RoleBodies)>>;

}  // namespace crucible::safety::proto
```

### StreamSession (§8) public surface

```cpp
namespace crucible::safety::proto {

template <typename T, typename RegionTag>
struct StreamSource {
    using payload     = T;
    using region_tag  = RegionTag;
    using local_proto = Loop<Select<
        Send<Borrowed<T, RegionTag>, Continue>,
        End>>;
};

template <typename T, typename RegionTag>
using StreamSink = dual_of_t<typename StreamSource<T, RegionTag>::local_proto>;

template <typename T, typename RegionTag>
[[nodiscard]] auto establish_stream(
    OwnedRegion<T, RegionTag>&&   region,
    SharedChannel&                transport) noexcept
    -> std::pair<
        PermissionedSessionHandle<typename StreamSource<T, RegionTag>::local_proto,
                                   PermSet<RegionTag>,
                                   StreamSourceResource>,
        PermissionedSessionHandle<StreamSink<T, RegionTag>,
                                   PermSet<>,
                                   StreamSinkResource>>;

}  // namespace crucible::safety::proto
```

# Appendix C — Open questions

1. **PermSet canonicalization.** Should `PermSet<A, B>` and `PermSet<B, A>` be the same type? Type-list canonicalization via sort + dedup avoids combinatorial explosion in subsequent operations but adds compile-time cost. Empirically, canonicalization is worth it for PermSet sizes ≥ 4; smaller sets can skip it.

2. **Cross-process PermissionedSessionHandle.** When a session crosses a process boundary (CNTP Layer 1), the receiving process's PermissionedSessionHandle must mint fresh permissions on the receiving side. The discipline: the receiving process's establishment factory takes a `Tagged<wire_payload, source::External>` containing the session's establishment metadata; the validator produces fresh `Permission<...>` tokens locally; the receiving handle is bound to those local permissions. The cross-process bytes never carry permissions directly. Need to formalize this as a "permission-establishment-on-receive" pattern.

3. **OwnedRegion lifetime across session boundaries.** A StreamSession backed by an OwnedRegion bounds the region's lifetime to the session's lifetime. What if multiple sessions share the same region (read-only)? The current SwmrSession design handles this; need to verify it composes cleanly with StreamSession.

4. **Distributed mCRL2 verification.** The §21 mCRL2 export produces a specification per session. For protocols that span multiple processes (CNTP Raft, distributed all-reduce), the per-process specifications must be composed into a global model before verification. The composition is mechanical (parallel composition of LPSs) but adds tooling complexity. Defer until a multi-process session genuinely needs it.

5. **Lean formalization of the integration's correctness.** §50 sketches four Lean modules; the actual mechanization will likely require additional intermediate lemmas (PermSet algebra, Tagged-flow correctness, OwnedRegion lifetime preservation). Estimate two to three months of Lean work; track separately from the C++ integration schedule.

6. **Backward compatibility.** Every integration is monotone (existing code compiles unchanged). What's the policy for *removing* the old non-permissioned SessionHandle once PermissionedSessionHandle is established? Recommendation: deprecate the old handle six months after the new one ships; remove it twelve months after deprecation. Plenty of time for the production refactors of Part VII to land.

7. **Federated permissions across runs.** Cipher's federation feature (CRUCIBLE.md §9.5) shares certificates across Crucible installations. Should PermissionedSessionHandle's PermSet be serializable so a federated certificate can prove "this session was run with this permission set"? Affirmative answer requires extending the SessionTag manifest with serialization rules; defer until federation lands.

---

# Appendix D — Reference codebases for proof-assistant work

The Lean formalization plan in §50 should not be developed in isolation. Twelve frontier codebases — Coq mechanizations, Agda libraries, production type-theoretic compilers — shallow-cloned at `~/Downloads/fx-refs/` (305 MB total, all `--depth 1`) cover essentially every theorem the integration depends on. Some are nearly drop-in: the Yoshida-school MPST mechanization in particular is approximately the Coq form of what `lean/Crucible/AssociationPreservation.lean` will say. Others are architectural references — what Crucible deliberately does, doesn't do, or aspires to. This appendix maps each reference to the specific integration concern it informs and recommends the order in which to consult them.

The references are read-only research material; nothing imports them and nothing ships against them. Their value is in shortening the design cycle: when stuck on a Lean lemma about session-type subject reduction, opening `smpst-sr-smer/theories/` is faster than rederiving the proof from the paper.

### D.1 Direct port candidates

**`smpst-sr-smer/`** (Apiros3 et al., ITP 2025, ~16K lines of Coq). First mechanization of subject reduction + progress for synchronous multiparty session types with precise subtyping, using parameterised coinduction on infinite type trees. Covers the metatheorems of Honda-Yoshida-Carbone (2008) MPST extended with the precise async refinement of Ghilezan-Pantović-Prokić-Scalas-Yoshida (2023). **Crucible integration relevance**: directly portable to Lean 4 for the `lean/Crucible/SessionMetatheory/` module hierarchy that backs L4 SessionGlobal.h (§339), L5 SessionAssoc.h (§345), L6 SessionSubtype.h (#338, completed), and L6.5 SISO async subtyping (#348). The Coq proofs are organized in topological order; the Lean port should follow the same order: queue types → context reduction → subject reduction → progress → subtype preservation. Estimated port effort: about 8K Lean lines over six to eight weeks. Highest-value reference in the catalog.

**`iris/`** (Jung-Krebbers-Birkedal et al., 5.1 MB Coq). The gold-standard CSL mechanization with higher-order ghost state, step-indexing, invariants, and the BI algebraic core. **Crucible integration relevance**: reference for the §5 PermissionedSessionHandle permission-flow theorem (the master theorem "Σ active_perms = constant under reduction" is structurally identical to Iris's frame rule preservation). Read `iris/bi/derived_laws.v` for the algebraic skeleton and `iris/base_logic/lib/` for the higher-order invariant machinery. Crucible deliberately collapses Iris's full higher-order CSL into a graded type-system encoding — see §38 for the design rationale — so this reference also documents what Crucible structurally gives up and what it would cost to recover. Estimated relevance to the §50 Lean modules: pattern-matching the `lean/Crucible/PermissionFlow.lean` structure against Iris's existing presentation, not a literal port.

**`DynamicIFCTheoremsForFree/`** (Algehed mechanization of Algehed-Bernardy ICFP 2019, ~232 KB). About a forty-line core proof of noninterference for LIO + faceted values via parametricity. **Crucible integration relevance**: the template for §13's `Secret<T>` payload-flow correctness — proving that classified data never silently flows to unclassified positions becomes a parametricity theorem rather than an operational discipline. The proof shape is small enough to literally inline into the Lean development; doing so converts the §39 "Secret ⩽ T is asymmetric" rule from a discipline note into a mechanized theorem. About 60 Lean lines of port work.

### D.2 Architectural references

**`bisikkel/`** (Ceulemans-Nuyts-Devriese, POPL 2025, 1.6 MB Agda). Multimodal Type Theory as an Agda library with separate program (MSTT) and proof (μLF) layers, parametrised by a mode theory. **Crucible integration relevance**: aspirational reference for organizing the wrapper × session × permission stack as a single MTT-style mode theory rather than as twenty-one independent wrappers. Future work, not on the twelve-week plan; if the integration succeeds and the team decides to consolidate, BiSikkel's `Applications/GuardedRecursion/` and `Applications/UnaryParametricity/` show how two modalities compose cleanly through mode-theory orthogonality. Read the POPL 2025 paper first; then `bisikkel/Applications/`; then the kernel.

**`sikkel/`** (Ceulemans et al., 2.2 MB Agda, predecessor to BiSikkel). Multimode Simple Type Theory (no proof layer). Smaller and easier to read; pedagogical entry point. **Crucible integration relevance**: same as BiSikkel but with reduced cognitive overhead. Recommended reading-order step before tackling the full BiSikkel.

**`Idris2/`** (Brady, ECOOP 2021, 59 MB). Production dependently-typed language with Atkey QTT (0/1/ω grades) as the kernel. **Crucible integration relevance**: reference for how a production compiler threads grades through every typing rule. Compare `Idris2/src/Core/Env.idr` and `Idris2/src/Core/Check.idr` against Crucible's `safety/Linear.h` (analogous to grade 1), `safety/Mutation.h::AppendOnly`/`Monotonic`/`WriteOnce` (intermediate grades), and the Tier-S §5 PermissionedSessionHandle PermSet evolution (grade-like resource flow). Crucible has chosen a per-wrapper approach rather than a unified grade calculus; Idris2 demonstrates the unified path. Useful for understanding the trade-off.

**`granule/`** (Orchard-Liepelt-Eades et al., 7.5 MB Haskell). Research-grade graded modal type system with user-defined semirings. **Crucible integration relevance**: design reference for any future user-defined wrapper trait (the equivalent of Granule's user-defined grades). The current Crucible discipline of "ship every wrapper as a separate header with hand-written invariants" is more concrete and ships faster; Granule shows the limit of generalization.

**`liquidhaskell/`** (UCSD, 33 MB Haskell + Z3). Production refinement-type checker with SMT backend. **Crucible integration relevance**: reference for §50's mCRL2 export pipeline (analogous tooling discipline) and for the `verify` preset's Z3 integration (Crucible's task #150). Compare `liquidhaskell/src/Language/Haskell/Liquid/Constraint/` against any future `tools/verify/` constraint-generator implementation. Documents how to handle the decidable-fragment boundary, caching of validated subterms, and diagnostic emission for SMT-rejected refinements. Crucible's `Refined<P, T>` carries a predicate that the type system trusts; LiquidHaskell carries one that an SMT solver verifies. The two approaches sit at different points on the same spectrum.

**`CompCert/`** (Leroy, 13 MB Coq). Verified C compiler with simulation proofs for every compilation phase. **Crucible integration relevance**: precedent for any future Forge/Mimic compilation correctness work (out of scope for this integration plan but on the broader Crucible roadmap). Read `backend/*_proof.v` for the simulation methodology; analogous proofs would need to bridge IR001 → IR002 → IR003 in Forge and IR003 → vendor-bytecode in Mimic. Not on the twelve-week plan; cited for completeness.

**`FStar/`** (Microsoft Research, 51 MB). SMT-first dependent type theory with layered effects, refinement types, SMT dispatch. **Crucible integration relevance**: reference for what Crucible deliberately does NOT do. The framework's preference for compile-time-decidable refinement (carried by the `Refined<P, T>` wrapper, no SMT round-trip) over arbitrary SMT-validated predicates is exactly the trade-off F* makes in the other direction. When design disagreements arise about whether to add a verifier dependency, consulting how F* solves the analogous problem clarifies the cost of the alternative.

### D.3 Aspirational references (HoTT / cubical / parametricity)

**`cubical/`** (Agda Cubical library, 13 MB). HoTT with computation: univalence, HITs, internal parametricity for cubical type theory. **Crucible integration relevance**: not on the current integration plan. If Crucible ever adopts HITs for quotient types (the natural way to mechanize §22's `ContentAddressed<T>` quotient combinator's category-theoretic correctness) or univalence for protocol equivalence (a richer form of `equivalent_sync_v` from `SessionSubtype.h`), this is the canonical reference. Browse `Cubical/HITs/` for HIT constructions and `Cubical/Foundations/` for the foundational layer. Estimated effort to adopt: significant; defer until concrete need arises.

**`agda/`** (Agda compiler core, 60 MB). Has the `--bridges` flag for internal parametricity (POPL 2024). **Crucible integration relevance**: kernel implementation of bridge types needed to make information-flow noninterference (§13) a provable theorem inside the type system rather than a grade-enforced invariant. Read `src/full/Agda/TypeChecking/Primitive/Cubical.hs` for the cubical primitive set and `test/Succeed/BridgeType*.agda` for usage. Crucible currently has no equivalent of `--bridges`; if information-flow correctness becomes load-bearing for production credentials, this is the reference for upgrading the discipline to a theorem.

### D.4 Recommended reading order for the §50 Lean development

If the team is about to begin the §50 Lean modules, read in the order below to maximize information density per hour invested:

1. **For `lean/Crucible/PermissionFlow.lean` (the master theorem of §5)**: read the Iris BI algebraic core (`iris/bi/derived_laws.v`) for the proof shape; then the LinearPi mechanization implicit in `smpst-sr-smer/theories/` for the linear-token transfer pattern; then write the Lean module. Estimated 4-6 weeks.

2. **For `lean/Crucible/AssociationPreservation.lean` (HYK24 Theorem 5.8)**: read `smpst-sr-smer/theories/` in topological order — start with the queue-types module, proceed through context reduction, end at subject reduction. The Lean port is largely transcription with adjustments for Lean's tactic style. Estimated 3-4 weeks.

3. **For `lean/Crucible/StreamSessionLifetime.lean` (the Borrowed view-lifetime theorem from §8)**: no direct precedent in the references; the closest analog is Iris's invariant masking machinery for ghost-state lifetimes. Read `iris/base_logic/lib/invariants.v`. Estimated 2 weeks; novel work.

4. **For `lean/Crucible/CrashFlow.lean` (OneShotFlag-driven crash transitions from §11)**: also without direct precedent; the closest analog is the BSYZ22/BHYZ23 crash-stop semantic mechanization, which would need to be ported from the paper (no public Coq mechanization exists yet). Estimated 2-3 weeks; novel work; consider co-publication with the Yoshida group as the natural collaboration point.

5. **For `lean/Crucible/SecretFlow.lean` (information-flow correctness of §13)**: port `DynamicIFCTheoremsForFree/`'s 40-line core proof; lift to the Crucible Secret/declassify-policy structure. Estimated 1 week.

Total Lean development: about three months for one engineer with prior Lean 4 experience. The references reduce the cycle by roughly half compared to working from papers alone — three months versus six. If the team includes someone already comfortable with the Yoshida-school MPST mechanization style, the cycle compresses further.

### D.5 What none of the references covers (research frontier)

Three theorems the integration depends on do not appear in any reference codebase as far as the survey could establish:

- **The intersection of CSL fractional permissions, MPST async subtyping, and crash-stop reliability sets.** Iris does CSL; smpst-sr-smer does MPST + async; BSYZ22/BHYZ23 do crash-stop; no mechanization composes all three. Crucible's §5 + §11 + the §50 Lean modules will produce the first such mechanization. Publication-grade contribution to the field; should be written up alongside the implementation.

- **OwnedRegion-backed StreamSession (§8) lifetime correctness.** The Borrowed-view discipline that bounds per-element borrows by the next session step is a novel composition of CSL fractional permissions with affine session payloads. The closest precedent is the Pottier-Protzenko (2013) Mezzo language's adoption-and-focus discipline; Mezzo has no current mechanization. Novel theorem; novel proof.

- **Cross-process PermissionedSessionHandle (open question §3 of Appendix C).** The discipline of "permissions don't cross the wire; the receiving process mints fresh permissions locally" is operationally clear but lacks a formal account. None of the references address it directly. Future research; collaboration candidate.

The research frontier is small but real. It is also exactly where Crucible's contribution to the broader PL community concentrates. The integration plan does not require resolving these frontier items to ship — the operational discipline is sufficient for production correctness — but the formalization in §50 will eventually want to address them, and that work is publishable.

---

# Closing

The integration plan above is approximately 2,800 lines of new framework code and 4,650 lines of production refactors, distributed across twenty-two integration points and nine refactor targets. Twelve weeks of focused engineering at a one-engineer pace; six weeks with two engineers working independently on disjoint epochs.

The plan is intentionally PL/type-theory-only. The user's direction to defer ML-side work in favor of foundations is the right call: every typed primitive we ship now is a class of bug we never debug at three a.m. on production hardware. Every unconvinced reader can verify the asymmetry by counting the runtime defensive checks Crucible currently maintains and computing the conservative estimate of how many of them disappear when the type system carries the proof.

The integration is a commitment to the discipline that the original three substrates already implicitly assume: that values, protocols, and ownership are not three independent concerns but three projections of one programming model. Honoring the implicit assumption with explicit machinery is what this plan does.

The next step is to land §6 (Tagged subsort axioms) — five lines of code with codebase-wide effect. Every later integration depends on the discipline §6 establishes. Ship that today; the rest follows.

---

*End of document. To be revisited as integrations land and as production callers reveal corner cases the catalog did not anticipate. Discrepancies between this document and observed reality are bugs in the document; the next maintainer of `safety/Session*.h` and the next maintainer of the production callers should treat the text here as design-of-record subject to review and revision.*
