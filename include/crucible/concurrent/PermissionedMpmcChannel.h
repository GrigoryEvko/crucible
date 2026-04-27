#pragma once

// ═══════════════════════════════════════════════════════════════════
// PermissionedMpmcChannel<T, Capacity, UserTag> — MPMC worked example
//
// Combines MpmcRing<T, Capacity> (Nikolaev SCQ — DISC 2019) with
// the CSL fractional-permission family on BOTH sides:
//
//   * Producer side — fractional via SharedPermissionPool<Producer>.
//                     Many concurrent producers; each holds a
//                     SharedPermissionGuard for its lifetime.
//   * Consumer side — fractional via SharedPermissionPool<Consumer>.
//                     Many concurrent consumers; each holds a
//                     SharedPermissionGuard for its lifetime.
//
// This is the genuinely-novel construction — no existing MPMC library
// (Vyukov, LCRQ, SCQ, wCQ, MoodyCamel, folly-MPMC, concurrentqueue)
// encodes producer/consumer roles at the type level.  See
// THREADING.md §5.5.1 for the design rationale.
//
//   sizeof(ProducerHandle) == sizeof(Channel*) + sizeof(Guard)
//   sizeof(ConsumerHandle) == sizeof(Channel*) + sizeof(Guard)
//
// Per-operation cost (steady state, uncontended):
//   try_push() : ~15-25 ns (MpmcRing's FAA on Tail + per-cell CAS)
//   try_pop()  : ~15-25 ns (MpmcRing's FAA on Head + per-cell OR/CAS)
//   producer() / consumer() lend : ~15 ns each (Pool atomic-CAS)
//
// ─── The four cells of the channel-permission family ──────────────
//
//   linear × linear     = PermissionedSpscChannel    (one prod, one cons)
//   linear × fractional = PermissionedSnapshot       (one writer, N readers)
//   fractional × linear = PermissionedMpscChannel    (N prods, one cons)
//   fractional × fractional = PermissionedMpmcChannel (N prods, N cons) ← here
//
// All four are the same machinery (Linear Permission +
// SharedPermissionPool) with different role-axis fractionality.  This
// channel is the most general — every concurrent-channel pattern in
// Crucible is expressible by composing these four cells.
//
// ─── The TWO independent pools ─────────────────────────────────────
//
// Producer and Consumer pools are INDEPENDENT atomic state machines.
// Each tracks its own outstanding share count, its own
// EXCLUSIVE_OUT_BIT.  The reasons:
//
//   1. Producer mode-transition (e.g., schema upgrade visible only to
//      producers) doesn't need to drain consumers, and vice versa.
//   2. The two pools' state words live on independent cache lines —
//      producer-side lend() doesn't ping the consumer-side line.
//   3. with_drained_access() that needs BOTH sides drained must
//      atomically upgrade both — see the dedicated method below.
//
// ─── with_drained_access — atomic upgrade of BOTH pools ────────────
//
// For operations that touch the channel's SHARED state (capacity
// resize, replacement, structural reset), BOTH pools must be drained
// — neither producer nor consumer can be in flight while the body
// runs.  The implementation:
//
//   1. try_upgrade producer pool (single CAS).  Fail → return false.
//   2. try_upgrade consumer pool (single CAS).  Fail → DEPOSIT
//      producer back to avoid leaking the upgrade.  Return false.
//   3. Both upgrades in hand: run body, deposit both back in reverse
//      order (consumer first, then producer).
//
// This is structurally an attempt-then-rollback pattern — no
// blocking, no spinning, no deadlock window because each pool has
// its own state machine.  Body runs only when ALL handles have
// returned to their respective pools.
//
// ─── Constraints ────────────────────────────────────────────────────
//
//   * T satisfies MpmcValue (trivially-copyable, trivially-
//     destructible).  Inherited from MpmcRing.
//   * Capacity ≥ 2 and a power of two.  Inherited from MpmcRing.
//   * Each PermissionedMpmcChannel uses a distinct UserTag.  Per
//     Permission.h's grep-discoverable rule, mint each Whole<Tag>
//     EXACTLY ONCE per program.  The channel mints both pool roots
//     INTERNALLY (no user-supplied root) — the Pool IS the root-of-
//     trust for fractional permissions.
//   * ProducerHandle / ConsumerHandle are move-only via their
//     embedded Guard.
//
// ─── Worked example ─────────────────────────────────────────────────
//
//   struct WorkChannel {};
//   PermissionedMpmcChannel<int, 1024, WorkChannel> ch;
//
//   // Both pools' roots are minted internally — no user-side mint.
//   // Spawn N producers + M consumers:
//   for (int i = 0; i < 8; ++i) {
//       std::jthread{[&ch, i](auto) {
//           auto p_opt = ch.producer();
//           if (!p_opt) return;
//           auto p = std::move(*p_opt);
//           p.try_push(i);
//       }};
//   }
//   for (int j = 0; j < 4; ++j) {
//       std::jthread{[&ch](auto) {
//           auto c_opt = ch.consumer();
//           if (!c_opt) return;
//           auto c = std::move(*c_opt);
//           while (auto v = c.try_pop()) { /* use *v */ }
//       }};
//   }
//
//   // p.try_pop()  is a COMPILE ERROR — no such method on ProducerHandle
//   // c.try_push() is a COMPILE ERROR — no such method on ConsumerHandle
//
// ─── References ─────────────────────────────────────────────────────
//
//   THREADING.md §5.5.1 — "the MPMC slot is genuinely beyond-Vyukov"
//   PermissionedMpscChannel.h — sibling fractional × linear pattern
//   PermissionedSnapshot.h — sibling linear × fractional pattern
//   PermissionedSpscChannel.h — sibling linear × linear pattern
//   safety/Permission.h — Permission/SharedPermissionPool machinery
//   concurrent/MpmcRing.h — underlying lock-free SCQ ring primitive
//   27_04_2026.md §5.3 — foundation requirement for the family
// ═══════════════════════════════════════════════════════════════════

#include <crucible/Platform.h>
#include <crucible/concurrent/MpmcRing.h>
#include <crucible/permissions/Permission.h>
#include <crucible/safety/Pinned.h>

#include <cstddef>
#include <cstdint>
#include <optional>
#include <type_traits>
#include <utility>

namespace crucible::concurrent {

// ── Tag tree for PermissionedMpmcChannel ───────────────────────────

namespace mpmc_tag {

template <typename UserTag> struct Whole    {};
template <typename UserTag> struct Producer {};
template <typename UserTag> struct Consumer {};

}  // namespace mpmc_tag

// ── PermissionedMpmcChannel<T, Capacity, UserTag> ──────────────────

template <MpmcValue T, std::size_t Capacity, typename UserTag = void>
class PermissionedMpmcChannel
    : public safety::Pinned<PermissionedMpmcChannel<T, Capacity, UserTag>> {
public:
    using value_type   = T;
    using user_tag     = UserTag;
    using whole_tag    = mpmc_tag::Whole<UserTag>;
    using producer_tag = mpmc_tag::Producer<UserTag>;
    using consumer_tag = mpmc_tag::Consumer<UserTag>;

    static constexpr std::size_t channel_capacity = Capacity;

    // ── Construction ──────────────────────────────────────────────
    //
    // Both pool roots are minted internally — the Pool IS the
    // fractional root-of-trust for its tag.  No user-supplied
    // Permission accepted on construction.  This matches the
    // PermissionedSnapshot reader-pool convention.

    PermissionedMpmcChannel() noexcept
        : producer_pool_{safety::permission_root_mint<producer_tag>()}
        , consumer_pool_{safety::permission_root_mint<consumer_tag>()} {}

    // ── ProducerHandle ────────────────────────────────────────────
    //
    // Move-only via embedded SharedPermissionGuard's deleted copy.
    // Constructed via producer() factory; holds a producer-pool
    // refcount share for its lifetime.  EXPOSES try_push only.

    class ProducerHandle {
        PermissionedMpmcChannel* ch_ = nullptr;
        safety::SharedPermissionGuard<producer_tag> guard_;

        constexpr ProducerHandle(PermissionedMpmcChannel& c,
                                 safety::SharedPermissionGuard<producer_tag>&& g) noexcept
            : ch_{&c}, guard_{std::move(g)} {}
        friend class PermissionedMpmcChannel;

    public:
        ProducerHandle(const ProducerHandle&)
            = delete("ProducerHandle owns a producer-pool refcount share — copy would double-count");
        ProducerHandle& operator=(const ProducerHandle&)
            = delete("ProducerHandle owns a producer-pool refcount share — assignment would double-count");
        constexpr ProducerHandle(ProducerHandle&&) noexcept = default;

        // Push — many producers may call concurrently.  Returns false
        // iff the ring is full or transient SCQ contention condition.
        // ~15-25 ns uncontended (FAA + CAS).
        [[nodiscard, gnu::hot]] bool try_push(const T& item) noexcept {
            return ch_->ring_.try_push(item);
        }

        [[nodiscard]] bool empty_approx() const noexcept {
            return ch_->ring_.empty_approx();
        }
        [[nodiscard]] static constexpr std::size_t capacity() noexcept {
            return Capacity;
        }
    };

    // ── ConsumerHandle ────────────────────────────────────────────
    //
    // Symmetric to ProducerHandle.  Holds a consumer-pool refcount
    // share for its lifetime.  EXPOSES try_pop only.

    class ConsumerHandle {
        PermissionedMpmcChannel* ch_ = nullptr;
        safety::SharedPermissionGuard<consumer_tag> guard_;

        constexpr ConsumerHandle(PermissionedMpmcChannel& c,
                                 safety::SharedPermissionGuard<consumer_tag>&& g) noexcept
            : ch_{&c}, guard_{std::move(g)} {}
        friend class PermissionedMpmcChannel;

    public:
        ConsumerHandle(const ConsumerHandle&)
            = delete("ConsumerHandle owns a consumer-pool refcount share — copy would double-count");
        ConsumerHandle& operator=(const ConsumerHandle&)
            = delete("ConsumerHandle owns a consumer-pool refcount share — assignment would double-count");
        constexpr ConsumerHandle(ConsumerHandle&&) noexcept = default;

        // Pop — many consumers may call concurrently.  Returns nullopt
        // iff the ring is empty or transient SCQ contention condition.
        [[nodiscard, gnu::hot]] std::optional<T> try_pop() noexcept {
            return ch_->ring_.try_pop();
        }

        [[nodiscard]] bool empty_approx() const noexcept {
            return ch_->ring_.empty_approx();
        }
        [[nodiscard]] static constexpr std::size_t capacity() noexcept {
            return Capacity;
        }
    };

    // ── Factories ─────────────────────────────────────────────────

    // Producer endpoint — lends a producer-pool share.  Returns
    // nullopt iff exclusive mode is active on the producer pool.
    [[nodiscard]] std::optional<ProducerHandle> producer() noexcept {
        auto guard = producer_pool_.lend();
        if (!guard) return std::nullopt;
        return ProducerHandle{*this, std::move(*guard)};
    }

    // Consumer endpoint — lends a consumer-pool share.  Returns
    // nullopt iff exclusive mode is active on the consumer pool.
    [[nodiscard]] std::optional<ConsumerHandle> consumer() noexcept {
        auto guard = consumer_pool_.lend();
        if (!guard) return std::nullopt;
        return ConsumerHandle{*this, std::move(*guard)};
    }

    // ── Mode transition: scoped exclusive access on BOTH pools ────
    //
    // Atomic upgrade of BOTH producer and consumer pools — body runs
    // with zero live handles on either side.  Used for capacity
    // resize, channel reset, migration.
    //
    // Implementation:
    //   1. try_upgrade producer pool (1 CAS).
    //   2. try_upgrade consumer pool (1 CAS).
    //   3. If consumer upgrade fails: DEPOSIT producer back to avoid
    //      a permission leak; return false.
    //   4. Run body.
    //   5. Deposit both in reverse order (consumer, then producer).
    //
    // No blocking, no spinning.  Returns true iff body ran.
    template <typename Body>
        requires std::is_invocable_v<Body>
    bool with_drained_access(Body&& body)
        noexcept(std::is_nothrow_invocable_v<Body>)
    {
        auto prod_upgrade = producer_pool_.try_upgrade();
        if (!prod_upgrade) return false;

        auto cons_upgrade = consumer_pool_.try_upgrade();
        if (!cons_upgrade) {
            // Roll back the producer upgrade — must not leak the
            // exclusive Permission.  Deposit returns it to the
            // parked state; subsequent producer() calls succeed.
            producer_pool_.deposit_exclusive(std::move(*prod_upgrade));
            return false;
        }

        std::forward<Body>(body)();

        // Deposit in reverse order.  Order doesn't matter for
        // correctness (the pools are independent), but reverse-of-
        // acquisition mirrors the typical resource discipline.
        consumer_pool_.deposit_exclusive(std::move(*cons_upgrade));
        producer_pool_.deposit_exclusive(std::move(*prod_upgrade));
        return true;
    }

    // ── Diagnostics ───────────────────────────────────────────────

    [[nodiscard]] std::uint64_t outstanding_producers() const noexcept {
        return producer_pool_.outstanding();
    }
    [[nodiscard]] std::uint64_t outstanding_consumers() const noexcept {
        return consumer_pool_.outstanding();
    }
    [[nodiscard]] bool is_producer_exclusive_active() const noexcept {
        return producer_pool_.is_exclusive_out();
    }
    [[nodiscard]] bool is_consumer_exclusive_active() const noexcept {
        return consumer_pool_.is_exclusive_out();
    }
    [[nodiscard]] bool empty_approx() const noexcept {
        return ring_.empty_approx();
    }
    [[nodiscard]] static constexpr std::size_t capacity() noexcept {
        return Capacity;
    }

private:
    MpmcRing<T, Capacity>                            ring_;
    safety::SharedPermissionPool<producer_tag>       producer_pool_;
    safety::SharedPermissionPool<consumer_tag>       consumer_pool_;
};

}  // namespace crucible::concurrent

// ── splits_into auto-specialization ─────────────────────────────────
//
// Both binary (splits_into) and N-ary (splits_into_pack) forms
// specialized for the (Whole, Producer, Consumer) triple.

namespace crucible::safety {

template <typename UserTag>
struct splits_into<concurrent::mpmc_tag::Whole<UserTag>,
                   concurrent::mpmc_tag::Producer<UserTag>,
                   concurrent::mpmc_tag::Consumer<UserTag>>
    : std::true_type {};

template <typename UserTag>
struct splits_into_pack<concurrent::mpmc_tag::Whole<UserTag>,
                        concurrent::mpmc_tag::Producer<UserTag>,
                        concurrent::mpmc_tag::Consumer<UserTag>>
    : std::true_type {};

}  // namespace crucible::safety
