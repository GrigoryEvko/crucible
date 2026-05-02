#pragma once

// ── crucible::safety::Permission<Tag> — CSL frame-rule primitive ─────
//
// Phantom-typed move-only token encoding "the bearer holds the
// exclusive right to mutate the memory region named by Tag".  The
// type system mechanizes Concurrent Separation Logic's frame rule
// (O'Hearn 2007): if two threads each hold a Permission for disjoint
// Tags, they cannot conflict — the optimizer can prove it AND the
// human reviewer gets a one-line check at every handoff.
//
//   Axiom coverage: BorrowSafe, ThreadSafe, MemSafe (code_guide §II).
//   Runtime cost:   zero — sizeof(Permission<Tag>) == 1
//                   (empty class minimum); collapses to 0 via
//                   [[no_unique_address]] in containing structs.
//
// ─── How linearity encodes the frame rule ───────────────────────────
//
// CSL's separating conjunction `*` says "P and Q hold over disjoint
// regions".  In C++, "disjoint" is captured by:
//
//   Linear (move-only)  → "exactly one owner at any moment"  (the * of CSL)
//   Tag                 → "which region this permission covers" (the labels)
//   move                → "ownership transfers"                  (frame's R)
//   mint_permission_split    → "splitting a region into disjoint parts" (P * Q ⊢ P, Q)
//   mint_permission_combine  → "merging two parts back"                (P, Q ⊢ P * Q)
//
// A thread that wants to mutate region R must hold Permission<R>.  By
// linearity, no two simultaneously-live Permission<R> values can
// exist; by transitivity, no two threads can mutate R simultaneously.
//
// ── MIGRATE-7 — façade migration (audit MIGRATE-7a, 2026-04-26) ────
//
// 25_04_2026.md §2.3 specifies the mapping:
//
//   template <typename Tag>
//   using SharedPermission = Graded<Absolute, FractionalLattice, Tag>;
//
// The audit (MIGRATE-7a) found this mapping STRUCTURALLY INCONSISTENT
// with the existing implementation, and chose the FAÇADE path over a
// full restructure.  Rationale:
//
// 1. The existing SharedPermission<Tag> is an EMPTY phantom token
//    (sizeof 1, EBO-collapsible to 0).  It is a PROOF that a non-zero
//    fractional share exists — not a CARRIER of the rational value.
//    The actual share count lives in SharedPermissionPool<Tag>'s
//    atomic state (count + EXCLUSIVE_OUT_BIT).
//
// 2. A literal `using SharedPermission = Graded<Absolute,
//    FractionalLattice, Tag>` would force SharedPermission to STORE a
//    Rational at every instance — breaking EBO, breaking the proof-
//    token design, breaking every production caller of Pool::lend()
//    that relies on the Guard returning a copyable empty token.
//
// 3. The 25_04 doc's mapping conflates two roles: SharedPermission
//    (the proof) and SharedPermissionPool (the carrier).  The carrier
//    IS structurally a Graded<Absolute, FractionalLattice, Tag> in
//    spirit — its atomic state encodes the current share grade —
//    but lifting the runtime atomic state into the Graded substrate
//    would require a fundamentally different concurrent primitive.
//
// FAÇADE MIGRATION (this commit):
//
//   - SharedPermission<Tag> stays a phantom proof-token; semantics
//     unchanged; sizeof unchanged.
//   - Adds a public `graded_type = Graded<Absolute, FractionalLattice,
//     Tag>` typedef for diagnostic introspection (test_migration_
//     verification, future GradedWrapper concept).
//   - Adds `lattice_name()` + `value_type_name()` consteval forwarders
//     mirroring the audit-Tier-2 cross-wrapper parity from the eight
//     migrated wrappers (Linear/Refined/Tagged/Secret/Monotonic/
//     AppendOnly/Stale/TimeOrdered, commits 0640168 + e21f6ba).
//   - Documents this as a "regime-5" classification (proof-token,
//     non-zero-cost, runtime carrier elsewhere) — distinct from
//     regime-1 (zero-cost EBO collapse: Linear/Refined/Tagged/Secret),
//     regime-2 (T == element_type collapse: Monotonic),
//     regime-3 (derived grade from container: AppendOnly),
//     regime-4 (T + grade carried per instance: Stale/TimeOrdered).
//
// Permission<Tag>, SharedPermissionPool, ReadView<Tag>, and the
// mint_permission_root / mint_permission_split / mint_permission_combine /
// mint_permission_split_n factories stay structural — they encode the CSL
// frame rule's discharge mechanics, not a graded value.  Do not
// extend SharedPermission with new specializations — extend the
// Graded algebra instead.
// ───────────────────────────────────────────────────────────────────
// Compile-time enforcement of separation logic.
//
// ─── Discipline (what the framework enforces vs what it doesn't) ───
//
// ENFORCED at compile time:
//   * Move-only (no two values for the same Tag exist)
//   * Tag identity at every handoff site (type system, no implicit conv)
//   * Declared splits via splits_into trait (compile error if undeclared)
//   * Construction only via grep-discoverable friend factories
//
// NOT enforced:
//   * That Tag actually corresponds to the memory you're claiming
//     (you must wire the Tag into your handle types — see
//     concurrent/Queue.h's per-Kind tag trees for the canonical pattern)
//   * That the body of code holding a permission only touches that
//     region (no flow-sensitive alias analysis in C++)
//
// For full proof discipline, hold the Permission inside a handle
// (Pinned, RAII) so the handle's methods become the gated operations
// — see Queue<T, Kind>::ProducerHandle in concurrent/Queue.h.
//
// ─── Usage pattern ──────────────────────────────────────────────────
//
//   // 1. Define the region-tag tree (declarative manifest):
//   namespace ring_tags {
//       struct Whole {};
//       struct Producer {};
//       struct Consumer {};
//   }
//   namespace crucible::safety {
//       template <> struct splits_into<ring_tags::Whole,
//                                      ring_tags::Producer,
//                                      ring_tags::Consumer>
//                    : std::true_type {};
//   }
//
//   // 2. Mint root at startup (single call per Whole tag):
//   auto whole = mint_permission_root<ring_tags::Whole>();
//
//   // 3. Split into disjoint subregions:
//   auto [prod, cons] = mint_permission_split<ring_tags::Producer,
//                                        ring_tags::Consumer>(
//                                                std::move(whole));
//
//   // 4. Hand off across threads via std::jthread move (or use
//   //    mint_permission_fork for structured fork-join — see
//   //    safety/PermissionFork.h):
//   std::jthread producer_thread{[p = std::move(prod)](auto) mutable {
//       /* p is the only Permission<Producer> in the program */
//   }};
//
// ─── Discipline (anti-patterns to reject on review) ─────────────────
//
// 1. NEVER store a `Permission<Tag>` in a long-lived data structure
//    that is shared between threads.  The struct may be aliased and
//    the type system can't see it; defeats linearity.
// 2. Mint each root tag exactly once.  No machinery enforces this —
//    the rule is grep-discoverable (`mint_permission_root<` is the
//    only construction call).
// 3. Tag tree splits_into specializations belong in the SAME
//    translation unit as the tag definitions.  Reviewers should see
//    the whole region tree in one place.

#include <crucible/Platform.h>
#include <crucible/algebra/Graded.h>
#include <crucible/algebra/lattices/FractionalLattice.h>
#include <crucible/safety/Pinned.h>

#include <atomic>
#include <concepts>
#include <cstdint>
#include <optional>
#include <string_view>
#include <tuple>
#include <type_traits>
#include <utility>

namespace crucible::safety {

// ── Forward declarations for fractional-permission family ───────────
template <typename Tag> class SharedPermission;
template <typename Tag> class SharedPermissionGuard;
template <typename Tag> class SharedPermissionPool;

// ── splits_into trait ────────────────────────────────────────────────
//
// Declarative manifest of valid splits.  Specialize per region tree:
//
//   template <> struct splits_into<Whole, Producer, Consumer>
//                : std::true_type {};
//
// mint_permission_split<L, R>(Permission<In>&&) requires
// splits_into<In, L, R>::value.  Same trait constrains
// mint_permission_combine<In>(Permission<L>&&, Permission<R>&&).

template <typename Parent, typename L, typename R>
struct splits_into : std::false_type {};

template <typename Parent, typename L, typename R>
inline constexpr bool splits_into_v = splits_into<Parent, L, R>::value;

// ── splits_into_pack trait ───────────────────────────────────────────
//
// N-ary variant — declares Parent splits into Children...  Used for
// sharded grids (M producers × N consumers) and any other case
// where a region naturally decomposes into more than two disjoint parts.

template <typename Parent, typename... Children>
struct splits_into_pack : std::false_type {};

template <typename Parent, typename... Children>
inline constexpr bool splits_into_pack_v =
    splits_into_pack<Parent, Children...>::value;

// ── Permission<Tag> ──────────────────────────────────────────────────
//
// Phantom-typed linear token.  Tag is never instantiated; only its
// identity matters.  The token itself carries no data — it is proof,
// not payload.

template <typename Tag>
class [[nodiscard]] Permission {
    // Empty — sizeof is the empty-class minimum (1 byte).  Marking
    // the field [[no_unique_address]] in containing types collapses
    // it to 0 bytes via EBO.

    // Private default constructor.  Only the friended factories
    // construct Permissions; every construction call site is
    // discoverable via grep on the factory names.
    constexpr Permission() noexcept = default;

    // Friend access list — kept short on purpose.  Each addition is
    // a new way to mint a Permission and demands review.

    template <typename T>
    friend constexpr Permission<T> mint_permission_root() noexcept;

    template <typename L, typename R, typename In>
    friend constexpr std::pair<Permission<L>, Permission<R>>
    mint_permission_split(Permission<In>&&) noexcept;

    template <typename In, typename L, typename R>
    friend constexpr Permission<In>
    mint_permission_combine(Permission<L>&&, Permission<R>&&) noexcept;

    template <typename... Children, typename In>
    friend constexpr std::tuple<Permission<Children>...>
    mint_permission_split_n(Permission<In>&&) noexcept;

    // N-ary inverse of mint_permission_split_n.  Added in FOUND-C Phase 1.5
    // for session_fork's rebuild path: after mint_permission_split_n hands
    // out N child Permissions to N spawned bodies, and the bodies join,
    // mint_permission_combine_n folds the N children back into the parent
    // exclusive Permission.  Same splits_into_pack_v<Parent, Children...>
    // gate as split_n.
    template <typename Parent, typename... Children>
    friend constexpr Permission<Parent>
    mint_permission_combine_n(Permission<Children>&&...) noexcept;

    // PermissionFork rebuilds the parent Permission after children
    // have been consumed by their callables.  See safety/PermissionFork.h.
    template <typename T>
    friend constexpr Permission<T> permission_fork_rebuild_() noexcept;

    // The Pool's try_upgrade re-emits the parked Permission when the
    // refcount of outstanding shares hits zero.  Construction is sound:
    // the atomic state-machine CAS guarantees no other holder exists
    // at the moment of issue.  See SharedPermissionPool below.
    template <typename T> friend class SharedPermissionPool;

    // Note: mint_permission_share does NOT need friend access — it consumes
    // a Permission via rvalue-ref (public move binding) and constructs
    // a SharedPermission (which friends mint_permission_share itself).

public:
    using tag_type = Tag;

    // Linearity: copy deleted with reason; move defaulted (the
    // moved-from Permission is empty and inert).  -Werror=use-after-move
    // catches double-consume.
    Permission(const Permission&)
        = delete("Permission<Tag>: linear — duplicating creates two simultaneous owners of the same region, breaking CSL's frame rule.  Use std::move to transfer.");
    Permission& operator=(const Permission&)
        = delete("Permission<Tag>: linear — assignment would overwrite an existing permission token.");
    constexpr Permission(Permission&&) noexcept            = default;
    constexpr Permission& operator=(Permission&&) noexcept = default;
    ~Permission() = default;
};

// ── Free function: explicit drop ────────────────────────────────────
//
// Equivalent to letting the rvalue go out of scope, but communicates
// "I am intentionally discarding this permission" at the call site.
// The corresponding region is unowned forever after this — a fresh
// permission cannot be re-minted at the same Tag (or rather, can only
// be done by re-calling mint_permission_root, which is discouraged
// outside startup).

template <typename Tag>
constexpr void permission_drop(Permission<Tag>&&) noexcept {
    // The rvalue parameter destructs at end of scope.
}

// ── Factories ────────────────────────────────────────────────────────

// Root mint.  Call exactly once per Tag at startup.  No machinery
// detects double-mint; the call site is grep-discoverable
// (`mint_permission_root<` matches every minting site).
//
// Cost: returns a 1-byte empty token.  Inlined to a no-op.
template <typename Tag>
[[nodiscard]] constexpr Permission<Tag> mint_permission_root() noexcept {
    return Permission<Tag>{};
}

// Binary split.  Returns disjoint Permission<L> and Permission<R>;
// the input Permission<In> is consumed.  Compile error if
// splits_into<In, L, R> hasn't been specialized true.
template <typename L, typename R, typename In>
[[nodiscard]] constexpr std::pair<Permission<L>, Permission<R>>
mint_permission_split(Permission<In>&& parent) noexcept {
    static_assert(splits_into_v<In, L, R>,
                  "mint_permission_split<L, R>(Permission<In>&&) requires "
                  "splits_into<In, L, R>::value to be specialized true.  "
                  "Declare the split in the same TU that defines the tags.");
    (void)parent;
    return std::pair<Permission<L>, Permission<R>>{
        Permission<L>{}, Permission<R>{}
    };
}

// Inverse: combine two disjoint permissions back into the parent.
// Symmetric to split — same splits_into constraint.
template <typename In, typename L, typename R>
[[nodiscard]] constexpr Permission<In>
mint_permission_combine(Permission<L>&& left, Permission<R>&& right) noexcept {
    static_assert(splits_into_v<In, L, R>,
                  "mint_permission_combine<In>(Permission<L>&&, Permission<R>&&) "
                  "requires splits_into<In, L, R>::value true.");
    (void)left;
    (void)right;
    return Permission<In>{};
}

// N-ary split.  Returns a tuple of disjoint Permissions — one per
// Child tag.  Used for sharded grids (one Permission per shard) and
// the structured-concurrency fork primitive (PermissionFork.h).
template <typename... Children, typename In>
[[nodiscard]] constexpr std::tuple<Permission<Children>...>
mint_permission_split_n(Permission<In>&& parent) noexcept {
    static_assert(splits_into_pack_v<In, Children...>,
                  "mint_permission_split_n<Children...>(Permission<In>&&) "
                  "requires splits_into_pack<In, Children...>::value true.");
    (void)parent;
    return std::tuple<Permission<Children>...>{
        Permission<Children>{}...
    };
}

// ── mint_permission_combine_n — N-ary inverse of split_n ─────────────────
//
// Folds N disjoint child Permissions back into the parent.  The
// caller passes the children as separate rvalue arguments (typically
// destructured from the tuple returned by mint_permission_split_n).  Same
// splits_into_pack_v<Parent, Children...> gate as split_n: every
// rebuild site is checked against the same declarative manifest.
//
// Used by FOUND-C session_fork to reclaim the parent Permission after
// all role bodies join.  The structural-join invariant guarantees no
// child Permission outlives the join: each spawned body's lambda
// destructor consumes the moved-in child token.
template <typename Parent, typename... Children>
[[nodiscard]] constexpr Permission<Parent>
mint_permission_combine_n(Permission<Children>&&... children) noexcept {
    static_assert(splits_into_pack_v<Parent, Children...>,
                  "mint_permission_combine_n<Parent, Children...>("
                  "Permission<Children>&&...) requires "
                  "splits_into_pack<Parent, Children...>::value true.  "
                  "The combine call must mirror the prior split_n; "
                  "declare the manifest in the same TU as the tags.");
    (void)std::tie(children...);  // consumed by move
    return Permission<Parent>{};
}

// ── Internal: rebuild helper for PermissionFork ──────────────────────
//
// Friend of Permission so safety/PermissionFork.h can reconstruct the
// parent Permission after all children have been consumed by their
// callables (and the jthreads have joined).  Per the discipline note
// in the header doc, the rebuild is justified by the structural-join
// invariant: every child callable consumed its child Permission inside
// its body; the jthread destructor in PermissionFork joined the worker
// before returning; no Permission<Child_i> for any i remains live; so
// the parent region is again exclusively available to the joining
// scope and a fresh Permission<Parent> can be issued.
//
// When child handles OWN their Permission via [[no_unique_address]]
// (the canonical pattern in concurrent/Queue.h), the consumption is
// structural: the handle's destructor at the end of the lambda body
// destructs the embedded Permission.
//
// NOT a public API; users must go through mint_permission_fork.
template <typename T>
[[nodiscard]] constexpr Permission<T> permission_fork_rebuild_() noexcept {
    return Permission<T>{};
}

// ─────────────────────────────────────────────────────────────────────
// ── Fractional permissions (Bornat-Calcagno-O'Hearn 2005) ──────────
// ─────────────────────────────────────────────────────────────────────
//
// Plain CSL permissions are binary: you either own a region (full,
// linear) or you don't.  Fractional permissions generalize to
// `e ↦_p v` for `0 < p ≤ 1`:
//   * p == 1.0 → exclusive (read+write)
//   * 0 < p < 1.0 → shared read (multiple holders OK)
//   * Σp_i = 1 → recoverable to exclusive
//
// The split-merge law `e ↦_(p+q) v ⟺ e ↦_p v * e ↦_q v` lets us
// hand out N read-only shares and recombine them when all return.
//
// C++ encoding (the three-piece split):
//
//   Permission<Tag>            — exclusive (linear, from above).
//   SharedPermission<Tag>      — shared read proof; copyable empty
//                                class (sizeof = 1, EBO-collapsible).
//                                Pure type-level proof, no runtime
//                                state.
//   SharedPermissionGuard<Tag> — RAII refcount holder; move-only.
//                                Construction bumps the Pool's
//                                outstanding count; destruction
//                                decrements.  The Guard's lifetime
//                                IS the share's lifetime.
//   SharedPermissionPool<Tag>  — Pinned manager; holds the parked
//                                exclusive Permission + an atomic
//                                state machine (refcount + "exclusive
//                                upgraded out" bit) implementing the
//                                lock-free mode-transition protocol.
//
// The proof and the lifetime are intentionally decoupled: the
// SharedPermission token can be passed around freely as a "you have
// read access" proof, while the Guard ensures the refcount reflects
// the share's actual lifetime.  Discipline rule: SharedPermission is
// only valid while a Guard exists somewhere — the type system can't
// enforce this without a borrow checker, so we accept it as a
// discipline gap (callers should pair token() with the Guard's scope).
//
// ─── The mode-transition race and how the atomic state resolves it ─
//
// Naive design: Pool holds an atomic count, lend() does fetch_add,
// try_upgrade() reads count==0 and takes parked.  The race:
//
//   Thread A: try_upgrade reads count = 0
//   Thread B: lend reads count = 0, fetch_add → 1
//   Thread A: takes parked (now exclusive is OUT)
//   ...   B's Guard exists, claiming a share that no longer exists.
//
// Wrong — two holders for the same region.  We need an indivisible
// state transition.  Solution: encode state in one atomic uint64_t:
//
//   bit 63        — "exclusive upgraded out" flag (1 = forbidden lend)
//   bits 62 .. 0  — outstanding share count (capacity 2^63)
//
// lend() (CAS loop): conditional-increment-if-bit-clear
//   * Reads observed; if bit 63 set, fail (return nullopt).
//   * Else CAS observed → observed + 1.  Retry on contention.
//
// try_upgrade() (single CAS):
//   * Expect 0 (count == 0 ∧ ¬excl_out); set EXCLUSIVE_OUT_BIT.
//   * If CAS fails, return nullopt (either count > 0 OR already up).
//   * If CAS succeeds, no other holder exists — issue the parked
//     Permission to the caller.
//
// guard~: fetch_sub(1) — unconditional, count reaches 0 eventually.
//
// deposit_exclusive(perm): re-park the exclusive; clear the bit.
//   * Pre: parked is empty (we MUST have upgraded out previously).
//   * After this, lend() succeeds again.
//
// This is a TaDA-style atomic triple:
//   ⟨ count = 0 ∧ ¬excl_out ⟩ try_upgrade ⟨ excl_out ∧ Permission → caller ⟩

// ── SharedPermission<Tag> ────────────────────────────────────────────
//
// Copyable empty class; sizeof 1 (EBO-collapsible to 0).  Multiple
// instances may co-exist for the same Tag — that's the point of
// fractional permissions.  Construction is friended: Pool::Guard
// produces them, or mint_permission_share() converts an exclusive.

template <typename Tag>
class [[nodiscard]] SharedPermission {
    constexpr SharedPermission() noexcept = default;

    template <typename T> friend class SharedPermissionGuard;
    template <typename T>
    friend constexpr SharedPermission<T>
    mint_permission_share(Permission<T>&&) noexcept;

public:
    using tag_type = Tag;

    // ── Façade-migration alias (MIGRATE-7, regime-5) ───────────────
    //
    // The 25_04_2026.md §2.3 mapping points SharedPermission<Tag> at
    // Graded<Absolute, FractionalLattice, Tag>.  The audit (MIGRATE-
    // 7a) chose the FAÇADE path: SharedPermission stays a proof-only
    // empty token, but exposes graded_type for diagnostic
    // introspection (GradedWrapper concept, test_migration_
    // verification harness, mCRL2 export).  See the file-header
    // MIGRATE-7 audit block for the rationale (atomic share lives in
    // SharedPermissionPool; the token is the proof, not the carrier).
    //
    // value_type is Tag itself — the phantom region label — because
    // the proof-token's "value" is its identity.  No Rational is
    // stored at the SharedPermission instance level.
    using value_type   = Tag;
    // GRADED-CONCEPT-C4: family-uniform lattice_type alias.  Every
    // graded-backed wrapper exposes lattice_type for substrate
    // introspection; SharedPermission's regime-5 façade joins the
    // family for GradedWrapper concept satisfaction.
    using lattice_type = ::crucible::algebra::lattices::FractionalLattice;
    // Modality declaration — Round-4 CHEAT-5; see safety/Linear.h.
    static constexpr ::crucible::algebra::ModalityKind modality =
        ::crucible::algebra::ModalityKind::Absolute;
    using graded_type  = ::crucible::algebra::Graded<
        ::crucible::algebra::ModalityKind::Absolute,
        lattice_type,
        Tag>;

    // Copyable: the whole point of fractional permissions.
    constexpr SharedPermission(const SharedPermission&) noexcept            = default;
    constexpr SharedPermission(SharedPermission&&) noexcept                 = default;
    constexpr SharedPermission& operator=(const SharedPermission&) noexcept = default;
    constexpr SharedPermission& operator=(SharedPermission&&) noexcept      = default;
    ~SharedPermission()                                                     = default;

    // ── Diagnostic names (forwarded from Graded substrate) ─────────
    //
    // value_type_name(): Tag's display string via reflection.
    // lattice_name(): "FractionalLattice" — the ℚ[0,1] semiring.
    //
    // Audit-Tier-2 cross-wrapper parity sweep — every migrated
    // wrapper (eight Graded-backed + this façade) ships these two
    // forwarders so review-time diagnostics introspect uniformly.
    [[nodiscard]] static consteval std::string_view value_type_name() noexcept {
        return graded_type::value_type_name();
    }
    [[nodiscard]] static consteval std::string_view lattice_name() noexcept {
        return graded_type::lattice_name();
    }
};

// ── SharedPermissionGuard<Tag> ───────────────────────────────────────
//
// Move-only RAII.  Construction bumps Pool's refcount; destruction
// decrements.  Copy deleted (would double-count); move sets source's
// pool_ to nullptr so only one decrement happens.

template <typename Tag>
class [[nodiscard]] SharedPermissionGuard {
    SharedPermissionPool<Tag>* pool_ = nullptr;

    constexpr explicit SharedPermissionGuard(SharedPermissionPool<Tag>& p) noexcept
        : pool_{&p} {}
    friend class SharedPermissionPool<Tag>;

public:
    using tag_type = Tag;

    // Move-only.  Source's pool_ is exchanged to nullptr so its
    // destructor doesn't double-decrement.
    SharedPermissionGuard(const SharedPermissionGuard&)
        = delete("RAII guard owns one outstanding share — copy would double-count");
    SharedPermissionGuard& operator=(const SharedPermissionGuard&)
        = delete("RAII guard owns one outstanding share — assignment would double-count");
    constexpr SharedPermissionGuard(SharedPermissionGuard&& other) noexcept
        : pool_{std::exchange(other.pool_, nullptr)} {}
    SharedPermissionGuard& operator=(SharedPermissionGuard&&)
        = delete("RAII guard's lifetime is fixed at construction; reassignment would double-decrement");

    ~SharedPermissionGuard();   // defined out-of-line below (needs Pool definition)

    // Yield the proof token.  Zero-cost copyable.  The Guard remains
    // alive (the share is still outstanding); the token is the proof.
    [[nodiscard]] constexpr SharedPermission<Tag> token() const noexcept {
        return SharedPermission<Tag>{};
    }

    // Non-null until moved-from.  Useful for diagnostics.
    [[nodiscard]] constexpr bool holds_share() const noexcept {
        return pool_ != nullptr;
    }
};

// ── SharedPermissionPool<Tag> ────────────────────────────────────────
//
// Pinned manager for fractional permissions on a region tagged Tag.
// Holds the parked exclusive Permission plus an atomic state-machine
// word (alignas(64) to avoid false sharing with caller state).

template <typename Tag>
class SharedPermissionPool : public Pinned<SharedPermissionPool<Tag>> {
public:
    using tag_type = Tag;

    // ── State encoding ──────────────────────────────────────────────
    static constexpr std::uint64_t EXCLUSIVE_OUT_BIT = std::uint64_t{1} << 63;
    static constexpr std::uint64_t COUNT_MASK        = EXCLUSIVE_OUT_BIT - std::uint64_t{1};

    // Construct from an exclusive Permission.  Pool starts in the
    // "exclusive parked, 0 outstanding shares" state; lend() is
    // immediately available.
    constexpr explicit SharedPermissionPool(Permission<Tag>&& exc) noexcept
        : parked_{std::move(exc)}, state_{0} {}

    // ── lend (any thread, any number) ───────────────────────────────
    //
    // CAS-loop conditional increment: bumps count iff exclusive is
    // not currently upgraded out.  Returns the RAII Guard wrapped in
    // optional (nullopt iff exclusive is out — caller may retry later
    // after the exclusive holder calls deposit_exclusive).
    //
    // Memory ordering: acq_rel on the CAS so the count update
    // synchronizes with try_upgrade's reads.
    [[nodiscard]] std::optional<SharedPermissionGuard<Tag>> lend() noexcept {
        std::uint64_t observed = state_.load(std::memory_order_acquire);
        for (;;) {
            if (observed & EXCLUSIVE_OUT_BIT) [[unlikely]] {
                return std::nullopt;  // upgrade in progress
            }
            // Overflow guard: practical limit is 2^63 simultaneous
            // shares, which is unreachable, but defend with abort.
            if ((observed & COUNT_MASK) == COUNT_MASK) [[unlikely]] {
                std::abort();
            }
            const std::uint64_t desired = observed + std::uint64_t{1};
            if (state_.compare_exchange_weak(
                    observed, desired,
                    std::memory_order_acq_rel,
                    std::memory_order_acquire)) {
                return SharedPermissionGuard<Tag>{*this};
            }
            // CAS failed — observed updated by CAS, retry.
        }
    }

    // ── try_upgrade (any thread, but typically the writer) ──────────
    //
    // Atomic mode transition: succeeds iff the state is exactly
    // (count = 0 ∧ ¬excl_out).  Sets EXCLUSIVE_OUT_BIT and re-emits
    // the parked Permission to the caller.  After this, lend() fails
    // until deposit_exclusive() is called.
    //
    // The single CAS resolves the lend-vs-upgrade race: if lend was
    // about to succeed, its CAS would have observed our state change
    // and retried (and failed because EXCLUSIVE_OUT_BIT is now set);
    // if lend already incremented, our CAS sees count > 0 and fails.
    [[nodiscard]] std::optional<Permission<Tag>> try_upgrade() noexcept {
        std::uint64_t expected = 0;  // count==0 ∧ ¬excl_out
        if (!state_.compare_exchange_strong(
                expected, EXCLUSIVE_OUT_BIT,
                std::memory_order_acq_rel,
                std::memory_order_acquire)) {
            return std::nullopt;
        }
        // We won.  parked_ MUST hold the exclusive (invariant: it's
        // populated whenever EXCLUSIVE_OUT_BIT is clear, and we just
        // transitioned from clear to set).  Move it out.
        Permission<Tag> exc = std::move(*parked_);
        parked_.reset();
        return exc;
    }

    // ── deposit_exclusive (the upgraded holder, returning) ──────────
    //
    // Re-parks the exclusive Permission and clears EXCLUSIVE_OUT_BIT,
    // making lend() available again.  Pre: parked_ is empty (we must
    // have upgraded out earlier and now be returning the Permission).
    void deposit_exclusive(Permission<Tag>&& exc) noexcept
        pre (!parked_.has_value())
    {
        parked_ = std::move(exc);
        // Clear the bit + count (count is 0 by invariant when
        // EXCLUSIVE_OUT_BIT is set).  Release-store so any subsequent
        // lend() sees the freshly-parked Permission.
        state_.store(0, std::memory_order_release);
    }

    // ── Diagnostics ─────────────────────────────────────────────────

    [[nodiscard]] std::uint64_t outstanding() const noexcept {
        return state_.load(std::memory_order_acquire) & COUNT_MASK;
    }

    [[nodiscard]] bool is_exclusive_out() const noexcept {
        return (state_.load(std::memory_order_acquire) & EXCLUSIVE_OUT_BIT) != 0;
    }

private:
    // Allow Guard to dec the refcount via direct state_ access.
    friend class SharedPermissionGuard<Tag>;

    // Parked exclusive Permission.  Has value iff EXCLUSIVE_OUT_BIT
    // is clear.  std::optional gives us the empty / present
    // distinction without requiring Permission to have a "moved-from"
    // sentinel value.
    std::optional<Permission<Tag>> parked_;

    // Atomic state: bit 63 = EXCLUSIVE_OUT_BIT, bits 0-62 = refcount.
    // Own cache line (alignas) to prevent false sharing with parked_
    // and adjacent-struct state.  Pool is Pinned so this address is
    // stable for the Pool's lifetime.
    alignas(64) std::atomic<std::uint64_t> state_;
};

// SharedPermissionGuard's destructor — defined now that Pool is
// complete.  Decrements the Pool's count; the count update is
// acq_rel so try_upgrade's CAS observes it.
template <typename Tag>
inline SharedPermissionGuard<Tag>::~SharedPermissionGuard() {
    if (pool_ != nullptr) {
        pool_->state_.fetch_sub(std::uint64_t{1}, std::memory_order_acq_rel);
    }
}

// ── Free factories ──────────────────────────────────────────────────

// Convert an exclusive Permission to an UNTRACKED shared one.  No
// Pool involved — re-upgrade impossible; the SharedPermission is
// freely copyable but no one tracks how many copies exist.  Use when
// you know the share is one-shot and won't need re-upgrade (e.g.
// passing read access to a child task that won't outlive the
// caller's scope).
template <typename Tag>
[[nodiscard]] constexpr SharedPermission<Tag>
mint_permission_share(Permission<Tag>&& exc) noexcept {
    (void)exc;  // consumed
    return SharedPermission<Tag>{};
}

// ── with_shared_read — convenience scoped-borrow helper ────────────
//
// Lends a share, invokes the body with the SharedPermission token,
// and releases the share when body returns.  Returns the body's
// return value (or void).  Returns std::nullopt iff lend() failed
// (exclusive was upgraded out at the time of call).
//
// Body signature: (SharedPermission<Tag>) -> R, where R is anything
// (including void).  noexcept iff body is noexcept.
//
// Useful for short read-mostly critical sections where the caller
// doesn't need to hold the share across multiple statements.

template <typename Tag, typename Body>
    requires std::is_invocable_v<Body, SharedPermission<Tag>>
[[nodiscard]] auto with_shared_read(SharedPermissionPool<Tag>& pool, Body&& body)
    noexcept(std::is_nothrow_invocable_v<Body, SharedPermission<Tag>>)
    -> std::optional<std::invoke_result_t<Body, SharedPermission<Tag>>>
    requires (!std::is_void_v<std::invoke_result_t<Body, SharedPermission<Tag>>>)
{
    auto guard_opt = pool.lend();
    if (!guard_opt) return std::nullopt;
    return std::optional{std::forward<Body>(body)(guard_opt->token())};
}

// ── Concept gates (FOUND-C Phase 1.5, FOUND-D consumption) ──────────
//
// IsPermission<T>           T is some Permission<Tag>.
// IsSharedPermission<T>     T is some SharedPermission<Tag>.
// IsPermissionFor<T, Tag>   T is exactly Permission<Tag>.
// IsSharedPermissionFor<T, Tag>  T is exactly SharedPermission<Tag>.
//
// Every Permission<Tag> exposes `tag_type = Tag`; the concept tests
// for the typedef plus structural identity to Permission<tag_type>.
// Distinct from the class names (Permission, SharedPermission) by
// design — concepts and classes occupy the same namespace member
// lookup table, so `concept Permission` would shadow the class.

namespace detail {

template <typename T>
struct is_permission_impl : std::false_type {};

template <typename Tag>
struct is_permission_impl<Permission<Tag>> : std::true_type {
    using tag_type = Tag;
};

template <typename T>
struct is_shared_permission_impl : std::false_type {};

template <typename Tag>
struct is_shared_permission_impl<SharedPermission<Tag>> : std::true_type {
    using tag_type = Tag;
};

}  // namespace detail

template <typename T>
inline constexpr bool is_permission_v =
    detail::is_permission_impl<std::remove_cvref_t<T>>::value;

template <typename T>
inline constexpr bool is_shared_permission_v =
    detail::is_shared_permission_impl<std::remove_cvref_t<T>>::value;

template <typename T>
concept IsPermission = is_permission_v<T>;

template <typename T>
concept IsSharedPermission = is_shared_permission_v<T>;

template <typename T, typename Tag>
concept IsPermissionFor =
    IsPermission<T> &&
    std::is_same_v<typename detail::is_permission_impl<
                       std::remove_cvref_t<T>>::tag_type, Tag>;

template <typename T, typename Tag>
concept IsSharedPermissionFor =
    IsSharedPermission<T> &&
    std::is_same_v<typename detail::is_shared_permission_impl<
                       std::remove_cvref_t<T>>::tag_type, Tag>;

// Void-return overload — separate template because optional<void>
// doesn't exist.  Returns bool: true iff body ran (lend succeeded).
template <typename Tag, typename Body>
    requires std::is_invocable_v<Body, SharedPermission<Tag>>
          && std::is_void_v<std::invoke_result_t<Body, SharedPermission<Tag>>>
bool with_shared_read(SharedPermissionPool<Tag>& pool, Body&& body)
    noexcept(std::is_nothrow_invocable_v<Body, SharedPermission<Tag>>)
{
    auto guard_opt = pool.lend();
    if (!guard_opt) return false;
    std::forward<Body>(body)(guard_opt->token());
    return true;
}

// ── Zero-cost guarantees ──────────────────────────────────────────────

namespace detail {
    struct seplog_test_tag {};
    struct seplog_test_left {};
    struct seplog_test_right {};
}

// Permission is a 1-byte empty class; not movable across translation
// units without copies but the move constructor is a noop.
static_assert(sizeof(Permission<detail::seplog_test_tag>) == 1,
              "Permission<Tag> must be a 1-byte empty class");
static_assert(std::is_trivially_destructible_v<Permission<detail::seplog_test_tag>>,
              "Permission<Tag> destructor must be trivial");
static_assert(!std::is_copy_constructible_v<Permission<detail::seplog_test_tag>>,
              "Permission<Tag> must NOT be copy-constructible (linear)");
static_assert(!std::is_copy_assignable_v<Permission<detail::seplog_test_tag>>,
              "Permission<Tag> must NOT be copy-assignable (linear)");
static_assert(std::is_move_constructible_v<Permission<detail::seplog_test_tag>>,
              "Permission<Tag> must be move-constructible (handoff)");
static_assert(std::is_nothrow_move_constructible_v<Permission<detail::seplog_test_tag>>,
              "Permission<Tag> moves must be noexcept");

// SharedPermission: copyable empty class.
static_assert(sizeof(SharedPermission<detail::seplog_test_tag>) == 1,
              "SharedPermission<Tag> must be a 1-byte empty class");
static_assert(std::is_copy_constructible_v<SharedPermission<detail::seplog_test_tag>>,
              "SharedPermission<Tag> MUST be copy-constructible (fractional)");
static_assert(std::is_copy_assignable_v<SharedPermission<detail::seplog_test_tag>>,
              "SharedPermission<Tag> MUST be copy-assignable (fractional)");
static_assert(std::is_trivially_copyable_v<SharedPermission<detail::seplog_test_tag>>,
              "SharedPermission<Tag> must be trivially-copyable (zero-cost copy)");
static_assert(std::is_trivially_destructible_v<SharedPermission<detail::seplog_test_tag>>,
              "SharedPermission<Tag> destructor must be trivial");

// SharedPermissionGuard: move-only RAII (sizeof = pool pointer).
static_assert(!std::is_copy_constructible_v<SharedPermissionGuard<detail::seplog_test_tag>>,
              "SharedPermissionGuard<Tag> must NOT be copy-constructible");
static_assert(std::is_move_constructible_v<SharedPermissionGuard<detail::seplog_test_tag>>,
              "SharedPermissionGuard<Tag> must be move-constructible");
static_assert(sizeof(SharedPermissionGuard<detail::seplog_test_tag>) == sizeof(void*),
              "SharedPermissionGuard<Tag> must be exactly one pointer (the Pool*)");

// SharedPermissionPool: Pinned (non-copyable, non-movable).
static_assert(!std::is_copy_constructible_v<SharedPermissionPool<detail::seplog_test_tag>>,
              "SharedPermissionPool<Tag> must be Pinned (non-copyable)");
static_assert(!std::is_move_constructible_v<SharedPermissionPool<detail::seplog_test_tag>>,
              "SharedPermissionPool<Tag> must be Pinned (non-movable)");

// ── Concept gates (FOUND-C Phase 1.5) ───────────────────────────────
//
// Positive: real Permission / SharedPermission instantiations satisfy
// the gates.  Negative: random non-Permission types do not.

static_assert(is_permission_v<Permission<detail::seplog_test_tag>>);
static_assert(is_permission_v<Permission<detail::seplog_test_tag>&&>);
static_assert(is_permission_v<const Permission<detail::seplog_test_tag>&>);
static_assert(!is_permission_v<int>);
static_assert(!is_permission_v<SharedPermission<detail::seplog_test_tag>>);
static_assert(!is_permission_v<SharedPermissionGuard<detail::seplog_test_tag>>);

static_assert(is_shared_permission_v<SharedPermission<detail::seplog_test_tag>>);
static_assert(is_shared_permission_v<const SharedPermission<detail::seplog_test_tag>&>);
static_assert(!is_shared_permission_v<int>);
static_assert(!is_shared_permission_v<Permission<detail::seplog_test_tag>>);
static_assert(!is_shared_permission_v<SharedPermissionGuard<detail::seplog_test_tag>>);

static_assert(IsPermission<Permission<detail::seplog_test_tag>>);
static_assert(IsSharedPermission<SharedPermission<detail::seplog_test_tag>>);
static_assert(IsPermissionFor<Permission<detail::seplog_test_tag>,
                              detail::seplog_test_tag>);
static_assert(!IsPermissionFor<Permission<detail::seplog_test_tag>,
                               detail::seplog_test_left>);
static_assert(IsSharedPermissionFor<SharedPermission<detail::seplog_test_tag>,
                                    detail::seplog_test_tag>);
static_assert(!IsSharedPermissionFor<SharedPermission<detail::seplog_test_tag>,
                                     detail::seplog_test_left>);

// ── mint_permission_combine_n smoke ──────────────────────────────────────
//
// Reuses the existing splits_into_pack manifest below.  Round-trip:
// mint root, split_n into N children, recombine via combine_n,
// successfully reconstruct parent.

namespace detail {
struct seplog_combine_n_parent {};
struct seplog_combine_n_a {};
struct seplog_combine_n_b {};
struct seplog_combine_n_c {};
}

template <>
struct splits_into_pack<detail::seplog_combine_n_parent,
                        detail::seplog_combine_n_a,
                        detail::seplog_combine_n_b,
                        detail::seplog_combine_n_c>
    : std::true_type {};

namespace detail {
constexpr bool combine_n_round_trip() noexcept {
    auto whole = mint_permission_root<seplog_combine_n_parent>();
    auto [a, b, c] = mint_permission_split_n<seplog_combine_n_a,
                                        seplog_combine_n_b,
                                        seplog_combine_n_c>(std::move(whole));
    auto rebuilt = mint_permission_combine_n<seplog_combine_n_parent>(
        std::move(a), std::move(b), std::move(c));
    (void)rebuilt;
    return true;
}
static_assert(combine_n_round_trip());
}  // namespace detail

}  // namespace crucible::safety
