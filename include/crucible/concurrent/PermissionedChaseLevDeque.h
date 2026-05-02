#pragma once

// ═══════════════════════════════════════════════════════════════════
// PermissionedChaseLevDeque<T, Capacity, UserTag>
// — work-stealing deque worked example (Lê et al. 2013)
//
// Wraps ChaseLevDeque<T, Capacity> with the CSL permission family.
// The work-stealing shape — single owner with LIFO push/pop on the
// bottom, many thieves with FIFO steal on the top — maps to a
// hybrid linear × fractional permission discipline:
//
//   * Owner side  — LINEAR Permission<Owner>.  One owner per deque,
//                   exactly the CL contract on push_bottom / pop_bottom.
//                   The linear discipline forbids two threads
//                   concurrently calling push_bottom (data race on
//                   bottom_); the type system enforces it structurally.
//
//   * Thieves     — FRACTIONAL via SharedPermissionPool<Thief>.
//                   Many concurrent thieves; each holds a Guard share
//                   for its lifetime.  CL's steal_top is intrinsically
//                   safe under concurrent thieves (CAS on top_), so
//                   fractional permissions are the right encoding.
//
// This is the SIXTH cell of the channel-permission family — the
// linear × fractional axis with ASYMMETRIC operations:
//
//   linear × linear         = PermissionedSpscChannel    (push, pop)
//   linear × fractional     = PermissionedSnapshot       (publish, load)
//                             PermissionedChaseLevDeque  (push/pop, steal) ← here
//   fractional × linear     = PermissionedMpscChannel    (push, pop)
//   fractional × fractional = PermissionedMpmcChannel    (push, pop)
//   linear-grid × linear-grid = PermissionedShardedGrid  (push slot, recv slot)
//
// PermissionedSnapshot and PermissionedChaseLevDeque share the
// linear × fractional shape but differ in operation count: snapshot
// has one operation per side (publish / load); deque has TWO on the
// linear side (push, pop) and ONE on the fractional side (steal).
// The asymmetry is the design feature — the owner exclusively owns
// both LIFO ends.
//
//   sizeof(OwnerHandle) == sizeof(Deque*) (Permission EBO)
//   sizeof(ThiefHandle) == sizeof(Deque*) + sizeof(Guard)
//
// Per-operation cost (steady state, uncontended):
//   push_bottom : ~3-5 ns  (CL's relaxed-load + relaxed-store)
//   pop_bottom  : ~5-10 ns (CL's seq_cst fence + conditional CAS)
//   steal_top   : ~10-30 ns (CL's seq_cst fence + CAS on top_)
//   thief() lend : ~15 ns  (Pool's atomic-CAS conditional increment)
//   owner() factory : 0 ns (move semantics)
//
// ─── The Permission discipline catches what CL alone doesn't ──────
//
// ChaseLevDeque alone is correct for SINGLE-OWNER × MANY-THIEVES — the
// memory ordering proofs (Lê et al. 2013) depend on the owner being
// EXACTLY ONE thread.  But the bare API exposes push_bottom/pop_bottom
// methods on the deque itself, so two threads can call push_bottom
// simultaneously by accident — silent data race on bottom_, NO
// compile error.
//
// The Permission wrapping closes this gap:
//
//   1. Permission<Owner> is LINEAR — only one OwnerHandle exists per
//      deque (move-only, deleted copy).  Two threads cannot
//      simultaneously hold OwnerHandles for the same deque.
//
//   2. ThiefHandle exposes only steal_top — calling push_bottom or
//      pop_bottom on a ThiefHandle is a hard compile error (no such
//      method).  Type system enforces role discipline.
//
//   3. SharedPermissionPool tracks outstanding ThiefHandles.  The
//      `with_drained_access` mode-transition primitive promotes the
//      pool to exclusive — useful for snapshot reset, capacity
//      resize, deque migration.
//
// ─── The KernelCompile pool use case (QUEUE-5, task #280) ──────────
//
// Crucible's kernel-compile pool is the canonical work-stealing
// consumer.  The keeper thread owns a per-worker deque and pushes
// compile jobs; worker threads steal from peer deques during
// otherwise-idle time.  Each per-worker deque becomes:
//
//   PermissionedChaseLevDeque<CompileJob, 256, Worker_K>
//
// Owner = Worker K's compile thread, Thieves = peer workers
// stealing slack.  The Permission discipline ensures Worker K is the
// only thread calling push_bottom on its own deque.
//
// ─── Constraints ────────────────────────────────────────────────────
//
//   * T satisfies DequeValue (trivially-copyable, trivially-
//     destructible, std::atomic<T>::is_always_lock_free).
//   * Capacity is a power of two, > 0, ≤ 2^30.  Inherited from CL.
//   * Each PermissionedChaseLevDeque uses a distinct UserTag.  Per
//     Permission.h's grep-discoverable rule, mint each Whole<UserTag>
//     EXACTLY ONCE per program.  The user keeps the Owner Permission
//     separately (mint via mint_permission_root<owner_tag>(), hand
//     to owner() factory).  Thief pool's root is minted internally.
//   * OwnerHandle and ThiefHandle are move-only.
//
// ─── Worked example ─────────────────────────────────────────────────
//
//   struct CompilePoolDeque {};
//   PermissionedChaseLevDeque<CompileJob, 256, CompilePoolDeque> deque;
//
//   // Mint Owner Permission ONLY — the deque mints the thief pool
//   // root internally (mirrors PermissionedSnapshot reader pool).
//   auto owner_perm = mint_permission_root<
//       deque_tag::Owner<CompilePoolDeque>>();
//   auto owner = deque.owner(std::move(owner_perm));
//
//   // Owner thread pushes/pops:
//   while (auto job = next_job()) owner.try_push(job);
//   while (auto job = owner.try_pop()) execute(job);
//
//   // Peer threads steal:
//   for (int i = 0; i < N_PEERS; ++i) {
//       std::jthread{[&deque](auto) {
//           auto t_opt = deque.thief();
//           if (!t_opt) return;
//           auto t = std::move(*t_opt);
//           while (auto job = t.try_steal()) execute(job);
//       }};
//   }
//
//   // owner.try_steal() is a COMPILE ERROR — no such method
//   // thief.try_push(job) is a COMPILE ERROR — no such method
//   // thief.try_pop()    is a COMPILE ERROR — no such method
//
// ─── References ─────────────────────────────────────────────────────
//
//   THREADING.md §5.5      — Tier 4 queue facade design
//   PermissionedSnapshot.h — sibling linear × fractional pattern
//   PermissionedMpscChannel.h — sibling fractional × linear pattern
//   safety/Permission.h    — Permission/SharedPermissionPool machinery
//   concurrent/ChaseLevDeque.h — underlying lock-free CL primitive
//   27_04_2026.md §5.3     — foundation requirement for the family
// ═══════════════════════════════════════════════════════════════════

#include <crucible/Platform.h>
#include <crucible/concurrent/ChaseLevDeque.h>
#include <crucible/permissions/Permission.h>
#include <crucible/safety/Pinned.h>

#include <cstddef>
#include <cstdint>
#include <optional>
#include <type_traits>
#include <utility>

namespace crucible::concurrent {

// ── Tag tree for PermissionedChaseLevDeque ─────────────────────────

namespace deque_tag {

template <typename UserTag> struct Whole {};
template <typename UserTag> struct Owner {};
template <typename UserTag> struct Thief {};

}  // namespace deque_tag

// ── PermissionedChaseLevDeque<T, Capacity, UserTag> ────────────────

template <DequeValue T, std::size_t Capacity, typename UserTag = void>
class PermissionedChaseLevDeque
    : public safety::Pinned<PermissionedChaseLevDeque<T, Capacity, UserTag>> {
public:
    using value_type = T;
    using user_tag   = UserTag;
    using whole_tag  = deque_tag::Whole<UserTag>;
    using owner_tag  = deque_tag::Owner<UserTag>;
    using thief_tag  = deque_tag::Thief<UserTag>;

    static constexpr std::size_t deque_capacity = Capacity;

    // ── Construction ──────────────────────────────────────────────
    //
    // Thief pool root minted internally — mirrors
    // PermissionedSnapshot's reader pool convention.  User mints +
    // hands the Owner Permission to owner() factory.

    PermissionedChaseLevDeque() noexcept
        : thief_pool_{safety::mint_permission_root<thief_tag>()} {}

    // ── OwnerHandle ───────────────────────────────────────────────
    //
    // Move-only via embedded Permission's deleted copy.  Constructed
    // via owner() factory; consumes the linear Owner Permission.
    // EXPOSES try_push and try_pop only — try_steal is structurally
    // impossible.
    //
    // Reference (not pointer): same rationale as
    // PermissionedSpscChannel::ConsumerHandle — forbids reassign +
    // implicitly deletes move-assign.

    class OwnerHandle {
        PermissionedChaseLevDeque& deque_;
        [[no_unique_address]] safety::Permission<owner_tag> perm_;

        constexpr OwnerHandle(PermissionedChaseLevDeque& d,
                              safety::Permission<owner_tag>&& p) noexcept
            : deque_{d}, perm_{std::move(p)} {}
        friend class PermissionedChaseLevDeque;

    public:
        OwnerHandle(const OwnerHandle&)
            = delete("OwnerHandle owns the linear Owner Permission — copy would duplicate the token, allowing two threads to race on push_bottom/pop_bottom (data race on bottom_)");
        OwnerHandle& operator=(const OwnerHandle&)
            = delete("OwnerHandle owns the linear Owner Permission — assignment would overwrite the linear token");
        constexpr OwnerHandle(OwnerHandle&&) noexcept = default;
        OwnerHandle& operator=(OwnerHandle&&)
            = delete("OwnerHandle binds to ONE deque for life — rebinding would orphan the original Permission and silently allow a second owner to coexist (CL's push_bottom/pop_bottom is single-owner-only)");

        // Push to bottom — owner only.  ~3-5 ns uncontended.
        // Returns false on capacity overflow.
        [[nodiscard, gnu::hot]] bool try_push(T item) noexcept {
            return deque_.deque_.push_bottom(item);
        }

        // Pop from bottom — owner only.  ~5-10 ns uncontended.
        // May race with thieves on the LAST element; CL resolves via
        // CAS on top_.  Returns nullopt iff empty after the race.
        [[nodiscard, gnu::hot]] std::optional<T> try_pop() noexcept {
            return deque_.deque_.pop_bottom();
        }

        [[nodiscard]] std::size_t size_approx() const noexcept {
            return deque_.deque_.size_approx();
        }
        [[nodiscard]] bool empty_approx() const noexcept {
            return deque_.deque_.empty_approx();
        }
        [[nodiscard]] static constexpr std::size_t capacity() noexcept {
            return Capacity;
        }
    };

    // ── ThiefHandle ───────────────────────────────────────────────
    //
    // Move-only via embedded SharedPermissionGuard's deleted copy.
    // Constructed via thief() factory; holds a thief-pool refcount
    // share for its lifetime.  EXPOSES try_steal only — try_push
    // and try_pop are structurally impossible.

    class ThiefHandle {
        PermissionedChaseLevDeque* deque_ = nullptr;
        safety::SharedPermissionGuard<thief_tag> guard_;

        constexpr ThiefHandle(PermissionedChaseLevDeque& d,
                              safety::SharedPermissionGuard<thief_tag>&& g) noexcept
            : deque_{&d}, guard_{std::move(g)} {}
        friend class PermissionedChaseLevDeque;

    public:
        ThiefHandle(const ThiefHandle&)
            = delete("ThiefHandle owns a thief-pool refcount share — copy would double-count");
        ThiefHandle& operator=(const ThiefHandle&)
            = delete("ThiefHandle owns a thief-pool refcount share — assignment would double-count");
        constexpr ThiefHandle(ThiefHandle&&) noexcept = default;
        // Move-assign deleted (Guard's lifetime fixed at construction).

        // Steal from top — many thieves may call concurrently.
        // ~10-30 ns under contention.  Returns nullopt iff the deque
        // is empty OR the CAS race for top was lost (caller may retry).
        [[nodiscard, gnu::hot]] std::optional<T> try_steal() noexcept {
            return deque_->deque_.steal_top();
        }

        [[nodiscard]] std::size_t size_approx() const noexcept {
            return deque_->deque_.size_approx();
        }
        [[nodiscard]] bool empty_approx() const noexcept {
            return deque_->deque_.empty_approx();
        }
        [[nodiscard]] static constexpr std::size_t capacity() noexcept {
            return Capacity;
        }
    };

    // ── Factories ─────────────────────────────────────────────────

    // Owner endpoint — consumes the linear Owner Permission.
    [[nodiscard]] OwnerHandle owner(safety::Permission<owner_tag>&& perm) noexcept {
        return OwnerHandle{*this, std::move(perm)};
    }

    // Thief endpoint — lends a thief-pool share.  Returns nullopt
    // iff exclusive mode is active (with_drained_access in flight).

    [[nodiscard]] std::optional<ThiefHandle> thief() noexcept {
        auto guard = thief_pool_.lend();
        if (!guard) return std::nullopt;
        return ThiefHandle{*this, std::move(*guard)};
    }

    // ── Mode transition: scoped exclusive access on thief pool ────
    //
    // Atomic upgrade of the thief pool — body runs while no thieves
    // are in flight.  The OWNER Permission is independent (linear,
    // held by the owner thread); this transition does NOT affect the
    // owner.  Used for thief-side snapshot reset / migration that
    // doesn't involve the owner.

    // Unified mode-transition primitive (pool-based).  Atomic
    // upgrade of the thief pool — body runs while no thieves are in
    // flight.  The OWNER Permission is independent (linear, held by
    // the owner thread); this transition does NOT affect the owner.
    // Used for thief-side snapshot reset / migration that doesn't
    // involve the owner.
    //
    // Cost: one CAS to acquire (succeeds iff outstanding == 0),
    // one release-store to deposit back.  Body's runtime is the
    // rest.  Subsequent thief() calls succeed once body returns.
    template <typename Body>
        requires std::is_invocable_v<Body>
    bool with_drained_access(Body&& body)
        noexcept(std::is_nothrow_invocable_v<Body>)
    {
        auto upgrade = thief_pool_.try_upgrade();
        if (!upgrade) return false;
        std::forward<Body>(body)();
        thief_pool_.deposit_exclusive(std::move(*upgrade));
        return true;
    }

    // ── Diagnostics ───────────────────────────────────────────────

    [[nodiscard]] std::uint64_t outstanding_thieves() const noexcept {
        return thief_pool_.outstanding();
    }
    [[nodiscard]] bool is_exclusive_active() const noexcept {
        return thief_pool_.is_exclusive_out();
    }
    [[nodiscard]] std::size_t size_approx() const noexcept {
        return deque_.size_approx();
    }
    [[nodiscard]] bool empty_approx() const noexcept {
        return deque_.empty_approx();
    }
    [[nodiscard]] static constexpr std::size_t capacity() noexcept {
        return Capacity;
    }

private:
    ChaseLevDeque<T, Capacity>                 deque_;
    safety::SharedPermissionPool<thief_tag>    thief_pool_;
};

}  // namespace crucible::concurrent

// ── splits_into auto-specialization ─────────────────────────────────

namespace crucible::safety {

template <typename UserTag>
struct splits_into<concurrent::deque_tag::Whole<UserTag>,
                   concurrent::deque_tag::Owner<UserTag>,
                   concurrent::deque_tag::Thief<UserTag>>
    : std::true_type {};

template <typename UserTag>
struct splits_into_pack<concurrent::deque_tag::Whole<UserTag>,
                        concurrent::deque_tag::Owner<UserTag>,
                        concurrent::deque_tag::Thief<UserTag>>
    : std::true_type {};

}  // namespace crucible::safety
