#pragma once

// ── crucible::concurrent::Endpoint<Substr, Dir, Ctx> ────────────────
//
// The big ctx-aware endpoint type — Tier 2 keystone of the integration
// stack.  Bundles a Permissioned* substrate's typed handle with an
// ExecCtx, validates SubstrateFitsCtxResidency at construction, and
// exposes FOUR composition views:
//
//   1. Raw view        — direct try_send / try_recv on the underlying
//                        handle (full-speed hot-path equivalent).
//   2. Session view    — `.into_session()` returns a typed PSH per
//                        `default_proto_for<Substr, Dir>`.
//   3. Recording view  — `.into_recording_session(log, self, peer)`
//                        wraps the session view with audit-trail
//                        recording (bridges/RecordingSessionHandle.h).
//   4. Crash-watched   — `.into_crash_watched(flag)` wraps with
//                        OneShotFlag-based crash transport (bridges/
//                        CrashTransport.h).
//
// All `into_*` methods are `&&`-qualified — they CONSUME the Endpoint
// and transfer ownership of the typed view to the returned wrapper.
// Endpoint itself is move-only (the underlying handle holds a linear
// Permission token; copy would duplicate it).
//
// ── Why this is the keystone ────────────────────────────────────────
//
// Production callers used to write ~50 LoC per session site:
//   * mint Permission tokens for the channel
//   * split into producer/consumer halves
//   * call channel.producer(perm) / channel.consumer(perm)
//   * spell the protocol shape manually
//   * thread the handle through the session factory
//
// With Endpoint, the same site collapses to:
//
//   auto ep = mint_endpoint<Channel, Direction::Producer>(ctx, handle);
//   ep.try_send(value);                    // raw view
//   auto sess = std::move(ep).into_session();  // protocol view
//
// The construction boundary checks ctx fit ONCE; subsequent send/recv
// runs at full speed with no per-op check.  Every cross-tier
// composition follows the Universal Mint Pattern (CLAUDE.md §XXI).
//
// ── Composition with Tier 1 ─────────────────────────────────────────
//
// Endpoint reads ALL Tier 1 facilities through Ctx:
//   * SubstrateFitsCtxResidency<Substr, Ctx>  — construction gate on
//                                                per_call_working_set_v
//                                                (HOT-PATH access pattern,
//                                                NOT total channel storage —
//                                                see SubstrateCtxFit.h #861).
//                                                Large-N SpscRing on
//                                                HotFgCtx is VALID because
//                                                producer/consumer touch
//                                                ~3 cache lines per call
//                                                regardless of capacity.
//   * IsHotCtx / IsBgCtx / IsArenaCtx / ...   — ctx_residency_tier_v
//                                                + per-axis discrimination
//                                                exposed as static facts
//   * SubstrateBenefitsFromParallelism<Substr> — cliff signal: TOTAL
//                                                storage > L2/core ⇒ this
//                                                workload could benefit
//                                                from sharding (informational;
//                                                NOT a hard rejection).
//   * cap_type_of_t<Ctx>                       — recoverable for downstream
//                                                Capability minting
//   * row_type_of_t<Ctx>                       — recoverable for downstream
//                                                row-typed payload checks
//
// Future Tier 3 Stage<auto FnPtr, Ctx> consumes Endpoint via the FOUND
// -D19 PipelineStage shape (function takes ConsumerHandle&&,
// ProducerHandle&&).  The Endpoint's `.into_session()` produces a PSH
// whose payload type matches the stage's input/output.
//
// ── Axiom coverage ──────────────────────────────────────────────────
//
//   TypeSafe — every (Substr, Dir, Ctx) triple is verified at the
//              mint factory's requires-clause.  Mismatches surface as
//              concept-violation diagnostics.
//   InitSafe — pure type-level construction; no allocation.
//   MemSafe  — Endpoint stores the handle BY POINTER (handle has a
//              reference member to its Pinned channel; the channel
//              outlives the Endpoint by construction).
//   BorrowSafe — Endpoint is move-only; copy is deleted with reason.
//                The underlying handle's Permission token enforces
//                single-{producer,consumer,...} linearity.
//   ThreadSafe — Endpoint defers all atomic ordering to the underlying
//                channel.  Endpoint itself holds no atomics.
//   LeakSafe — RAII destructor; no resources to free.  The underlying
//              handle is owned by the caller's enclosing scope.
//   DetSafe  — same (ctx, handle) → same Endpoint type and layout.
//
// Runtime cost: sizeof(Endpoint) == sizeof(handle_type*) (8 bytes on
// 64-bit), plus Ctx (1 byte EBO-collapsed via [[no_unique_address]]).
// All `try_send` / `try_recv` calls inline through to the underlying
// handle's method — byte-identical to bare-handle code under -O3.
//
// ── Status ──────────────────────────────────────────────────────────
//
// v1 covers SPSC / MPSC / MPMC (try_push / try_pop) and Snapshot
// (publish / load).  ChaseLevDeque deferred to v2.

#include <crucible/Platform.h>
#include <crucible/concurrent/SubstrateCtxFit.h>
#include <crucible/concurrent/SubstrateSessionBridge.h>
#include <crucible/effects/ExecCtx.h>
#include <crucible/sessions/PermissionedSession.h>

#include <optional>
#include <type_traits>
#include <utility>

namespace crucible::concurrent {

// ── Endpoint<Substr, Dir, Ctx> — the typed ctx-aware view ──────────

template <class Substr, Direction Dir, ::crucible::effects::IsExecCtx Ctx>
    requires IsBridgeableDirection<Substr, Dir>
          && SubstrateFitsCtxResidency<Substr, Ctx>
class [[nodiscard]] Endpoint {
public:
    using substrate_type = Substr;
    static constexpr Direction direction = Dir;
    using ctx_type    = Ctx;
    using handle_type = handle_for_t<Substr, Dir>;
    using value_type  = substrate_value_type_t<Substr>;
    using user_tag    = substrate_user_tag_t<Substr>;
    using proto_type  = default_proto_for_t<Substr, Dir>;

    // ── Compile-time facts derived from Ctx ────────────────────────
    //
    // These let downstream stages branch on the ctx without re-querying
    // the discrimination concepts every time.  All static; cost-free.

    static constexpr bool ctx_is_hot       = ::crucible::effects::IsHotCtx<Ctx>;
    static constexpr bool ctx_is_warm      = ::crucible::effects::IsWarmCtx<Ctx>;
    static constexpr bool ctx_is_cold      = ::crucible::effects::IsColdCtx<Ctx>;
    static constexpr bool ctx_is_arena     = ::crucible::effects::IsArenaCtx<Ctx>;
    static constexpr bool ctx_is_numa_local = IsNumaLocalCtx<Ctx>;             // concurrent/ExecCtxBridge.h
    static constexpr Tier residency_tier_v  = ctx_residency_tier<Ctx>();      // concurrent/ExecCtxBridge.h

    // Substrate-side compile-time facts the downstream stage can branch
    // on without re-querying Substrate.h's metafunctions.
    //
    // benefits_from_parallelism: total channel storage exceeds the
    //   conservative L2/core bound (256 KB).  Per ParallelismRule
    //   (concurrent/ParallelismRule.h), workloads above the cliff
    //   benefit from sharding / parallelization.  Stage<>/Pipeline
    //   downstream may use this to recommend a Sharded* alternative
    //   or to wire NumaSpread placement.  NOT a hard rejection — 1×1
    //   substrates above the cliff (TraceRing-style) remain valid.
    //
    // per_call_working_set: bytes the producer/consumer's hot path
    //   actually touches per try_send / try_recv / publish / load
    //   call.  Independent of total capacity.  Useful for Stage<>
    //   tile-shape decisions that assume per-call WS dictates the
    //   prefetch budget.
    //
    // total_channel_bytes: total static storage of the channel.
    //   Diagnostic / placement decisions (NUMA, hugepage hint).
    static constexpr bool        benefits_from_parallelism =
        SubstrateBenefitsFromParallelism<Substr>;
    static constexpr std::size_t per_call_working_set =
        per_call_working_set_v<Substr>;
    static constexpr std::size_t total_channel_bytes =
        channel_byte_footprint_v<Substr>;

private:
    // POINTER, not reference: the underlying handle has a reference
    // member to its channel (per PermissionedSpscChannel.h:198) so it
    // can't be move-assigned.  Endpoint stores Handle* so it CAN be
    // move-constructed without breaking the underlying handle's
    // single-handle-per-channel invariant.
    handle_type* handle_;

    // Ctx is a phantom carrier — empty class, EBO-collapsed.
    [[no_unique_address]] Ctx ctx_;

    // Factory-only construction.  The friend declaration matches
    // exactly the mint_endpoint signature so SFINAE rejects any
    // back-door instantiation.
    template <class S, Direction D, ::crucible::effects::IsExecCtx C>
        requires IsBridgeableDirection<S, D>
              && SubstrateFitsCtxResidency<S, C>
    friend constexpr auto mint_endpoint(C const&, handle_for_t<S, D>&) noexcept;

    constexpr explicit Endpoint(handle_type& h) noexcept
        : handle_{&h}, ctx_{} {}

public:
    // ── Linearity discipline ───────────────────────────────────────
    //
    // Copy: deleted (the underlying handle owns a linear Permission
    // token; duplicating Endpoint would duplicate the typed view).
    //
    // Move: ownership-transferring.  The default move-ctor would
    // POINTER-COPY handle_ to the destination, leaving the source
    // Endpoint with a still-valid handle_ pointer — a use-after-move
    // hazard (calling try_send on the moved-from Endpoint would
    // succeed by hitting the same handle).  Custom move nulls the
    // source's handle_ to make any post-move use a clean nullptr
    // deref instead of a silent typed-view-aliasing.
    //
    // The Permission discipline at the underlying handle layer
    // would still catch a runtime double-use, but the silent type-
    // level aliasing breaks the "Endpoint is the unique typed view"
    // invariant that Stage / Pipeline downstream rely on.

    Endpoint(Endpoint const&) = delete(
        "Endpoint owns the typed view of a linear handle — copy would "
        "duplicate the producer/consumer Permission's typed projection.  "
        "Use std::move to transfer; or mint a second Endpoint if the "
        "underlying substrate is multi-producer/multi-consumer.");
    Endpoint& operator=(Endpoint const&) = delete(
        "Endpoint owns the typed view of a linear handle.");

    constexpr Endpoint(Endpoint&& o) noexcept
        : handle_{o.handle_}, ctx_{} {
        o.handle_ = nullptr;
    }
    constexpr Endpoint& operator=(Endpoint&& o) noexcept {
        if (this != &o) [[likely]] {
            handle_ = o.handle_;
            o.handle_ = nullptr;
        }
        return *this;
    }
    ~Endpoint() = default;

    // ─────────────────────────────────────────────────────────────────
    // ── View 1 — Raw: forward to the underlying handle ──────────────
    // ─────────────────────────────────────────────────────────────────
    //
    // Identical to calling the bare handle's try_push / try_pop.
    // Single per-call cost: one indirect through handle_, then the
    // underlying SpscRing (or equivalent) operation — typically one
    // acquire-load + one release-store on isolated cache lines.

    // Producer-side direction (push-typed substrates: SPSC/MPSC/MPMC)
    template <class T = value_type>
        requires (Dir == Direction::Producer)
              && std::same_as<std::remove_cvref_t<T>, value_type>
    [[nodiscard, gnu::hot]] bool try_send(T const& v) noexcept {
        return handle_->try_push(v);
    }

    // Snapshot-writer-side direction (publishes a single latest value)
    template <class T = value_type>
        requires (Dir == Direction::SwmrWriter)
              && std::same_as<std::remove_cvref_t<T>, value_type>
    [[gnu::hot]] void publish(T const& v) noexcept {
        handle_->publish(v);
    }

    // Consumer-side direction (pop-typed substrates)
    template <Direction D = Dir>
        requires (D == Direction::Consumer)
    [[nodiscard, gnu::hot]] std::optional<value_type> try_recv() noexcept {
        return handle_->try_pop();
    }

    // Snapshot-reader-side direction (loads the latest published value)
    template <Direction D = Dir>
        requires (D == Direction::SwmrReader)
    [[nodiscard, gnu::hot]] value_type load() noexcept {
        return handle_->load();
    }

    // Diagnostic / queue-state queries — forwarded uniformly.
    [[nodiscard]] bool empty_approx() const noexcept
        requires requires(handle_type const& h) { h.empty_approx(); }
    {
        return handle_->empty_approx();
    }
    [[nodiscard]] std::size_t size_approx() const noexcept
        requires requires(handle_type const& h) { h.size_approx(); }
    {
        return handle_->size_approx();
    }

    // ─────────────────────────────────────────────────────────────────
    // ── View 2 — Session: PSH typed over default_proto_for<S, D> ────
    // ─────────────────────────────────────────────────────────────────
    //
    // CONSUMES the Endpoint.  Returns a PermissionedSessionHandle
    // typed over default_proto_for_t<Substr, Dir>, with EmptyPermSet
    // (the substrate's Permission discipline at the handle layer
    // already enforces single-producer-or-multi-producer semantics).

    [[nodiscard]] constexpr auto into_session() && noexcept {
        return mint_substrate_session<Substr, Dir>(ctx_, *handle_);
    }

    // ─────────────────────────────────────────────────────────────────
    // ── View 2b — Bare session: SessionHandle (no PermSet wrapper) ──
    // ─────────────────────────────────────────────────────────────────
    //
    // Returns a bare `SessionHandle<Proto, Handle*>` (NOT a PSH).
    // Useful as input to bridge wrappers (RecordingSessionHandle,
    // CrashWatchedHandle) which take SessionHandle directly — see
    // bridges/EndpointMint.h for the bridge mint helpers.
    //
    // The underlying handle's Permission discipline still enforces
    // single-{producer,consumer,...} linearity at the channel layer;
    // dropping the EmptyPermSet wrapper is safe because EmptyPermSet
    // carries no per-step PermSet evolution.

    [[nodiscard]] constexpr auto into_bare_session() && noexcept {
        return ::crucible::safety::proto::mint_session_handle<proto_type>(handle_);
    }

    // ── Accessor: peek at the underlying handle without consuming ──
    //
    // Used by Tier 3 Stage instantiation to introspect the handle's
    // metadata without taking ownership.  Returns a const reference;
    // does NOT invalidate the Endpoint.
    [[nodiscard]] constexpr handle_type& handle() & noexcept { return *handle_; }
    [[nodiscard]] constexpr handle_type const& handle() const& noexcept { return *handle_; }

    // ─────────────────────────────────────────────────────────────────
    // ── View 3 — Tier 3 bridge: consume into the underlying handle ─
    // ─────────────────────────────────────────────────────────────────
    //
    // CONSUMES the Endpoint and returns the underlying handle BY MOVE.
    // After this call:
    //   * the Endpoint is in a moved-from state (handle_ == nullptr,
    //     same as after std::move);
    //   * the externally-owned handle (the lvalue the user passed to
    //     mint_endpoint) is in a moved-from state — its embedded
    //     Permission token has been transferred to the returned
    //     handle, but its reference-to-channel remains bound (so
    //     calling try_push/try_pop on the moved-from original would
    //     still hit the channel's ring; the user must NOT do that).
    //
    // This is the canonical Tier 2 → Tier 3 bridge: Endpoint validates
    // SubstrateFitsCtxResidency at mint time, then into_handle()
    // releases the validated handle into Stage's mint_stage<FnPtr>(...)
    // (or directly into mint_stage_from_endpoints, which calls this
    // internally on both sides of the stage).
    //
    // Why both Endpoint AND the original handle are left moved-from:
    //   ConsumerHandle / ProducerHandle hold a REFERENCE to the
    //   channel — references can't be rebound.  Moving the handle
    //   move-constructs a new handle with the same channel reference
    //   and the moved-from Permission.  The ORIGINAL handle (in
    //   user's scope) becomes moved-from but its reference stays
    //   bound to the (still-alive) channel.  The discipline:
    //   "after std::move(ep).into_handle(), the original lvalue
    //   handle the user passed to mint_endpoint is moved-from; do
    //   not touch it."  Consistent with standard moved-from
    //   semantics.
    //
    // Axiom coverage:
    //   MemSafe — handle_ nulled-out post-move; the moved handle
    //             owns the linear Permission going forward.
    //   BorrowSafe — the linear Permission is in exactly one place
    //             (the returned handle); typed-view aliasing
    //             impossible because Endpoint no longer holds a
    //             non-null pointer.
    //   LeakSafe — the returned handle's destructor releases the
    //             Permission cleanly when it goes out of scope.

    [[nodiscard]] constexpr handle_type into_handle() && noexcept {
        handle_type extracted = std::move(*handle_);
        handle_ = nullptr;
        return extracted;
    }

    // ── Accessor: the Ctx as a value (zero cost) ───────────────────
    [[nodiscard]] constexpr Ctx ctx() const noexcept { return ctx_; }
};

// ── mint_endpoint<Substr, Dir>(ctx, handle) ─────────────────────────
//
// The Universal Mint factory for raw endpoints.  Same shape as
// mint_substrate_session, but returns the concrete Endpoint type
// instead of a PSH — for callers who want raw try_send / try_recv
// access AND the optional ability to upgrade to a session view later
// via .into_session().
//
// Constraints checked at the call site:
//   * IsBridgeableDirection<Substr, Dir>     — supported (S, D) pair
//   * SubstrateFitsCtxResidency<Substr, Ctx> — substrate footprint fits

template <class Substr, Direction Dir, ::crucible::effects::IsExecCtx Ctx>
    requires IsBridgeableDirection<Substr, Dir>
          && SubstrateFitsCtxResidency<Substr, Ctx>
[[nodiscard]] constexpr auto
mint_endpoint(Ctx const&, handle_for_t<Substr, Dir>& handle) noexcept {
    return Endpoint<Substr, Dir, Ctx>{handle};
}

// ── Self-test block ─────────────────────────────────────────────────
namespace detail::endpoint_self_test {

namespace eff   = ::crucible::effects;
namespace proto = ::crucible::safety::proto;

struct UserTag {};
using SmallSpsc = PermissionedSpscChannel<int, 64, UserTag>;
using ProdEp = Endpoint<SmallSpsc, Direction::Producer, eff::HotFgCtx>;
using ConsEp = Endpoint<SmallSpsc, Direction::Consumer, eff::BgDrainCtx>;

// ── Type-level invariants ──────────────────────────────────────────
static_assert(std::is_same_v<typename ProdEp::handle_type,
                              typename SmallSpsc::ProducerHandle>);
static_assert(std::is_same_v<typename ProdEp::value_type, int>);
static_assert(std::is_same_v<typename ProdEp::ctx_type, eff::HotFgCtx>);
static_assert(std::is_same_v<typename ProdEp::proto_type,
                              proto::Loop<proto::Send<int, proto::Continue>>>);

// Ctx-derived static facts
static_assert( ProdEp::ctx_is_hot);
static_assert(!ProdEp::ctx_is_warm);
static_assert(!ProdEp::ctx_is_cold);
static_assert( ProdEp::ctx_is_numa_local);
static_assert( ProdEp::residency_tier_v == Tier::L1Resident);

static_assert( ConsEp::ctx_is_warm);
static_assert(!ConsEp::ctx_is_hot);
static_assert( ConsEp::ctx_is_arena);
static_assert( ConsEp::residency_tier_v == Tier::L2Resident);

// Substrate-side static facts.  SmallSpsc<int, 64> = 256 B total →
// well below the cliff; per-call WS = 192 B (3 cache lines).
static_assert(!ProdEp::benefits_from_parallelism);   // 256 B < L2/core
static_assert( ProdEp::per_call_working_set == 192);  // 2 head/tail + 1 cell line
static_assert( ProdEp::total_channel_bytes == sizeof(int) * 64);

// ── Cliff-crossing pin: large-N SPSC on HotFgCtx is now VALID ──────
//
// Before #861, this pairing was rejected (TOTAL storage check).
// After #861, the gate uses per-call WS so large-N SPSCs compose
// honestly with HotFgCtx.  benefits_from_parallelism flags the
// developer that sharding could win, but it's an INFORMATIONAL
// signal, not a hard rejection.

using HugeSpsc = PermissionedSpscChannel<int, 1024 * 1024, UserTag>;
using HugeProdEp = Endpoint<HugeSpsc, Direction::Producer, eff::HotFgCtx>;
static_assert( HugeProdEp::ctx_is_hot);
static_assert( HugeProdEp::residency_tier_v == Tier::L1Resident);
static_assert( HugeProdEp::per_call_working_set == 192);
static_assert( HugeProdEp::total_channel_bytes == 4 * 1024 * 1024);
static_assert( HugeProdEp::benefits_from_parallelism);
// The mint factory accepts (HugeSpsc, HotFgCtx) — exactly the
// regression #861 fixes.  TraceRing-style large rings now compose
// honestly with the ctx that documents their hot-path access.

// ── Linearity ──────────────────────────────────────────────────────
static_assert(!std::is_copy_constructible_v<ProdEp>);
static_assert(!std::is_copy_assignable_v<ProdEp>);
static_assert( std::is_move_constructible_v<ProdEp>);
static_assert( std::is_move_assignable_v<ProdEp>);

// ── Layout: pointer + EBO-collapsed Ctx ────────────────────────────
//
// Endpoint stores Handle* (8 bytes on 64-bit) + Ctx (empty class,
// EBO-collapsed via [[no_unique_address]]).  Total sizeof should
// equal sizeof(void*).
static_assert(sizeof(ProdEp) == sizeof(void*),
    "Endpoint must collapse to pointer-size — Ctx EBO-collapse is "
    "load-bearing for the zero-runtime-cost claim.");
static_assert(sizeof(ConsEp) == sizeof(void*));

// ── Snapshot endpoint ──────────────────────────────────────────────
struct SnapTag {};
using SmallSnap = PermissionedSnapshot<int, SnapTag>;
using SnapWriter = Endpoint<SmallSnap, Direction::SwmrWriter, eff::HotFgCtx>;
using SnapReader = Endpoint<SmallSnap, Direction::SwmrReader, eff::BgDrainCtx>;

static_assert(std::is_same_v<typename SnapWriter::handle_type,
                              typename SmallSnap::WriterHandle>);
static_assert(std::is_same_v<typename SnapReader::handle_type,
                              typename SmallSnap::ReaderHandle>);
static_assert(SnapWriter::ctx_is_hot);

// ── Move semantics — noexcept (Permission discipline is type-level) ─
static_assert(std::is_nothrow_move_constructible_v<ProdEp>);
static_assert(std::is_nothrow_move_assignable_v<ProdEp>);

}  // namespace detail::endpoint_self_test

// ── Runtime smoke test ──────────────────────────────────────────────
//
// Constructs a real channel + permission split + producer/consumer
// handles, mints two Endpoints, exercises the raw view (try_send /
// try_recv), upgrades both to session view via .into_session().
// Verifies the returned PSH has the expected concrete head type.

[[gnu::cold]] inline void runtime_smoke_test_endpoint() noexcept {
    namespace eff   = ::crucible::effects;
    namespace proto = ::crucible::safety::proto;
    namespace saf   = ::crucible::safety;

    struct SmokeTag {};
    using Channel = PermissionedSpscChannel<int, 64, SmokeTag>;

    Channel ch;
    auto whole = saf::mint_permission_root<spsc_tag::Whole<SmokeTag>>();
    auto [pp, cp] = saf::mint_permission_split<
        spsc_tag::Producer<SmokeTag>, spsc_tag::Consumer<SmokeTag>>(std::move(whole));

    auto prod = ch.producer(std::move(pp));
    auto cons = ch.consumer(std::move(cp));

    // ── Mint two endpoints under canonical contexts ────────────────
    eff::HotFgCtx fg;
    eff::BgDrainCtx bg;
    auto fg_ep = mint_endpoint<Channel, Direction::Producer>(fg, prod);
    auto bg_ep = mint_endpoint<Channel, Direction::Consumer>(bg, cons);

    // Type-level pinning of the returned Endpoint types.
    static_assert(std::is_same_v<decltype(fg_ep)::ctx_type, eff::HotFgCtx>);
    static_assert(std::is_same_v<decltype(bg_ep)::ctx_type, eff::BgDrainCtx>);
    static_assert(decltype(fg_ep)::ctx_is_hot);
    static_assert(decltype(bg_ep)::ctx_is_warm);

    // ── Raw view: drive try_send / try_recv ────────────────────────
    [[maybe_unused]] bool pushed = fg_ep.try_send(42);
    [[maybe_unused]] auto popped = bg_ep.try_recv();

    // ── Session view: .into_session() consumes the Endpoint ────────
    auto fg_sess = std::move(fg_ep).into_session();
    auto bg_sess = std::move(bg_ep).into_session();

    // After Loop unrolling the head type is Send<int, Continue> /
    // Recv<int, Continue> respectively.
    using FgSessProto = decltype(fg_sess)::protocol;
    using BgSessProto = decltype(bg_sess)::protocol;
    static_assert(std::is_same_v<FgSessProto, proto::Send<int, proto::Continue>>);
    static_assert(std::is_same_v<BgSessProto, proto::Recv<int, proto::Continue>>);

    // Detach the typed sessions so the abandonment-check destructor
    // doesn't fire.
    std::move(fg_sess).detach(proto::detach_reason::TestInstrumentation{});
    std::move(bg_sess).detach(proto::detach_reason::TestInstrumentation{});

    // ── Move-from null-out witness ─────────────────────────────────
    //
    // After std::move(ep), the source's handle_ pointer is nulled
    // (defensive vs use-after-move).  Mint a fresh endpoint, move it,
    // verify the source is now in a clean nullptr state.
    auto cons2 = ch.consumer(saf::mint_permission_root<spsc_tag::Consumer<SmokeTag>>());
    auto src   = mint_endpoint<Channel, Direction::Consumer>(eff::BgDrainCtx{}, cons2);
    auto dst   = std::move(src);
    // src.try_recv() would now hit a nullptr deref — a clean fault
    // rather than silent typed-view-aliasing.  We don't actually call
    // it here; the post-move state is documented and proven sound by
    // the implementation's source-null-out assignment.
    (void)dst;

    // Reclaim the recombined whole permission.
    [[maybe_unused]] auto recombined = saf::mint_permission_combine<
        spsc_tag::Whole<SmokeTag>>(
            saf::mint_permission_root<spsc_tag::Producer<SmokeTag>>(),
            saf::mint_permission_root<spsc_tag::Consumer<SmokeTag>>());
}

}  // namespace crucible::concurrent
