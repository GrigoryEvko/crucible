#pragma once

// ═══════════════════════════════════════════════════════════════════
// PermissionedMpscChannel<T, Capacity, UserTag> — MPSC worked example
//
// Combines MpscRing<T, Capacity> (lock-free Vyukov per-cell sequence
// MPSC ring) with the CSL permission family:
//
//   * Producer side — fractional via SharedPermissionPool<Producer>.
//                     Many concurrent producers; each holds a
//                     SharedPermissionGuard for its lifetime.
//   * Consumer side — LINEAR via Permission<Consumer>.  Exactly one
//                     consumer endpoint may exist per channel
//                     (MpscRing's try_pop has a sole-consumer
//                     contract; the type system enforces it).
//
//   sizeof(ProducerHandle) == sizeof(Channel*) + sizeof(Guard)
//   sizeof(ConsumerHandle) == sizeof(Channel*)  (Permission EBO)
//
// Per-operation cost (steady state):
//   try_push() : ~12-15 ns  (MpscRing's CAS on global head + per-cell
//                            release on sequence)
//   try_pop()  : ~5-10 ns   (single-consumer non-CAS path)
//   producer() lend : ~15 ns (Pool's atomic-CAS conditional increment)
//   consumer() : ~0 ns      (move semantics)
//
// ─── Why fractional producers + linear consumer ────────────────────
//
// MpscRing's contract is symmetric to PermissionedSnapshot but
// flipped: snapshot has ONE writer + N readers; MPSC has N producers
// + ONE consumer.  We mirror PermissionedSnapshot's machinery with
// roles swapped:
//
//   PermissionedSnapshot:                PermissionedMpscChannel:
//     writer_pool_ ABSENT  (linear)        producer_pool_ PRESENT (frac)
//     writer Permission    (linear)        producer SharedPermission (frac)
//     reader_pool_ PRESENT (frac)          consumer_pool_ ABSENT  (linear)
//     reader SharedPermission (frac)       consumer Permission    (linear)
//
// The three reasons for SharedPermissionPool on the producer side:
//
//   1. MpscRing is structurally many-producer.  A linear Producer
//      Permission would force the user to either hand off the
//      Permission token between producer threads (defeating the
//      "many concurrent producers" property) or split the token
//      via permission_split_n into M phantom producer slots
//      (defeating dynamic membership).  Fractional permissions
//      handle dynamic membership naturally.
//
//   2. Mode-transition primitive.  with_drained_access() promotes
//      the Pool to exclusive — used for snapshot reset, capacity
//      resize, channel migration.  The atomic CAS pair is the
//      only synchronization needed.
//
//   3. Pool refcount becomes runtime telemetry — outstanding
//      producers visible via outstanding_producers().  Useful for
//      diagnostics ("which Vessel threads have producer endpoints
//      live?") without the framework keeping a separate registry.
//
// The consumer remains LINEAR because MpscRing's try_pop has a hard
// "single consumer" contract (no synchronization on the consumer
// side; head is producer-claimed, tail is consumer-owned).  Two
// concurrent consumers would race on tail → data race on cell reads.
// Linear Permission<consumer_tag> + the type-level "ConsumerHandle
// has only try_pop" rule together make this race a compile error.
//
// ─── Constraints ────────────────────────────────────────────────────
//
//   * T satisfies RingValue (trivially-copyable, trivially-
//     destructible).  Inherited from MpscRing.
//   * Capacity is a power of two.  Inherited from MpscRing.
//   * Each PermissionedMpscChannel uses a distinct UserTag (or
//     different (T, Capacity)) so its tags don't collide across
//     channels.  Per Permission.h's grep-discoverable rule, mint
//     each Whole<UserTag> EXACTLY ONCE per program.
//   * ProducerHandle and ConsumerHandle are move-only via their
//     embedded Guard / Permission.
//
// ─── Worked example ─────────────────────────────────────────────────
//
//   struct InboxChannel {};
//   PermissionedMpscChannel<int, 256, InboxChannel> ch;
//
//   // Mint Consumer Permission ONLY — the channel mints its own
//   // producer root internally.  Mirror of PermissionedSnapshot
//   // (which mints reader root internally; user mints writer).
//   auto cons_perm = permission_root_mint<
//       mpsc_tag::Consumer<InboxChannel>>();
//   auto consumer = ch.consumer(std::move(cons_perm));
//
//   // N concurrent producers each get a Pool share:
//   for (int i = 0; i < 8; ++i) {
//       std::jthread{[&ch, i](auto) {
//           auto p_opt = ch.producer();
//           if (!p_opt) return;  // exclusive mode active
//           auto p = std::move(*p_opt);
//           p.try_push(i);
//       }};
//   }
//
//   while (auto v = consumer.try_pop()) { /* use *v */ }
//
//   // p.try_pop()  is a COMPILE ERROR — no such method on ProducerHandle
//   // consumer.try_push() is a COMPILE ERROR — no such method on ConsumerHandle
//
// ─── References ─────────────────────────────────────────────────────
//
//   THREADING.md §5.5    — Tier 4 queue facade design
//   PermissionedSnapshot.h — fractional pool sibling pattern
//   PermissionedSpscChannel.h — two-linear-permissions sibling pattern
//   safety/Permission.h  — Permission/SharedPermissionPool machinery
//   concurrent/MpscRing.h — underlying lock-free ring primitive
//   27_04_2026.md §5.3   — foundation requirement for Permissioned*
// ═══════════════════════════════════════════════════════════════════

#include <crucible/Platform.h>
#include <crucible/concurrent/MpscRing.h>
#include <crucible/permissions/Permission.h>
#include <crucible/safety/Pinned.h>

#include <cstddef>
#include <cstdint>
#include <optional>
#include <type_traits>
#include <utility>

namespace crucible::concurrent {

// ── Tag tree for PermissionedMpscChannel ───────────────────────────
//
// Each logical channel picks its own UserTag (a phantom type, typically
// an empty struct).  The (Whole, Producer, Consumer) triple is
// auto-specialized for splits_into below.

namespace mpsc_tag {

template <typename UserTag> struct Whole    {};
template <typename UserTag> struct Producer {};
template <typename UserTag> struct Consumer {};

}  // namespace mpsc_tag

// ── PermissionedMpscChannel<T, Capacity, UserTag> ──────────────────

template <RingValue T, std::size_t Capacity, typename UserTag = void>
class PermissionedMpscChannel
    : public safety::Pinned<PermissionedMpscChannel<T, Capacity, UserTag>> {
public:
    using value_type   = T;
    using user_tag     = UserTag;
    using whole_tag    = mpsc_tag::Whole<UserTag>;
    using producer_tag = mpsc_tag::Producer<UserTag>;
    using consumer_tag = mpsc_tag::Consumer<UserTag>;

    static constexpr std::size_t channel_capacity = Capacity;

    // ── Construction ──────────────────────────────────────────────
    //
    // The channel mints its own Producer Permission root internally
    // and parks it in the Pool — same convention as
    // PermissionedSnapshot mints its Reader root internally.  The
    // user keeps the Consumer Permission separately (mint via
    // permission_root_mint<consumer_tag>(), hand to consumer()
    // factory below) — symmetric to PermissionedSnapshot's Writer
    // permission flow.
    //
    // Why mint internally for the fractional side: the Pool IS the
    // root-of-trust for fractional permissions; bringing in an
    // external Permission would let the user park unrelated
    // permissions in the wrong Pool.  Internal minting keeps the
    // Pool the single mint site for its tag.

    PermissionedMpscChannel() noexcept
        : producer_pool_{safety::permission_root_mint<producer_tag>()} {}

    // ── ProducerHandle ────────────────────────────────────────────
    //
    // Move-only via the embedded SharedPermissionGuard's deleted
    // copy.  Constructed via producer() factory; holds a Pool refcount
    // share for its lifetime (decrement on destruction).
    // sizeof(ProducerHandle) == sizeof(Channel*) + sizeof(Guard).
    //
    // EXPOSES try_push only — try_pop is structurally impossible.

    class ProducerHandle {
        PermissionedMpscChannel* ch_ = nullptr;
        safety::SharedPermissionGuard<producer_tag> guard_;

        constexpr ProducerHandle(PermissionedMpscChannel& c,
                                 safety::SharedPermissionGuard<producer_tag>&& g) noexcept
            : ch_{&c}, guard_{std::move(g)} {}
        friend class PermissionedMpscChannel;

    public:
        ProducerHandle(const ProducerHandle&)
            = delete("ProducerHandle owns a Pool refcount share — copy would double-count");
        ProducerHandle& operator=(const ProducerHandle&)
            = delete("ProducerHandle owns a Pool refcount share — assignment would double-count");
        constexpr ProducerHandle(ProducerHandle&&) noexcept = default;
        // Move-assignment deleted because Guard's lifetime is fixed
        // at construction (Guard rejects move-assign).

        // Push — many producers may call concurrently.  Returns false
        // iff the ring is full; caller decides backpressure.
        // ~12-15 ns under low contention, scales by FAA bandwidth.
        [[nodiscard, gnu::hot]] bool try_push(T item) noexcept {
            return ch_->ring_.try_push(std::move(item));
        }

        // Diagnostics — snapshot reads, NOT exact.
        [[nodiscard]] bool empty_approx() const noexcept {
            return ch_->ring_.empty_approx();
        }
        [[nodiscard]] std::size_t size_approx() const noexcept {
            return ch_->ring_.size_approx();
        }
        [[nodiscard]] static constexpr std::size_t capacity() noexcept {
            return Capacity;
        }
    };

    // ── ConsumerHandle ────────────────────────────────────────────
    //
    // Move-only via the embedded Permission's deleted copy.
    // Constructed via consumer() factory; consumes the Consumer
    // Permission (linear — exactly one consumer per channel).
    // sizeof(ConsumerHandle) == sizeof(Channel*) via EBO.
    //
    // EXPOSES try_pop only — try_push is structurally impossible.

    class ConsumerHandle {
        // Reference (not pointer): the channel is Pinned, address
        // stable for life.  Reference forbids reassign + default-
        // construct; implicitly deletes move-assign which would
        // silently violate Permission linearity (defaulted move on
        // empty Permission is a no-op, leaving BOTH source and target
        // claiming the linear token).
        PermissionedMpscChannel& ch_;
        [[no_unique_address]] safety::Permission<consumer_tag> perm_;

        constexpr ConsumerHandle(PermissionedMpscChannel& c,
                                 safety::Permission<consumer_tag>&& p) noexcept
            : ch_{c}, perm_{std::move(p)} {}
        friend class PermissionedMpscChannel;

    public:
        ConsumerHandle(const ConsumerHandle&)
            = delete("ConsumerHandle owns the Consumer Permission — copy would duplicate the linear token");
        ConsumerHandle& operator=(const ConsumerHandle&)
            = delete("ConsumerHandle owns the Consumer Permission — assignment would overwrite the linear token");
        constexpr ConsumerHandle(ConsumerHandle&&) noexcept = default;
        ConsumerHandle& operator=(ConsumerHandle&&)
            = delete("ConsumerHandle binds to ONE channel for life — rebinding would orphan the original Permission and silently allow a second consumer to coexist (MpscRing's try_pop is single-consumer-only)");

        // Pop — single consumer ONLY (caller ensures via Permission
        // linearity).  Returns nullopt iff the ring is empty.
        // ~5-10 ns uncontended.
        [[nodiscard, gnu::hot]] std::optional<T> try_pop() noexcept {
            return ch_.ring_.try_pop();
        }

        // Diagnostics — snapshot reads, NOT exact.
        [[nodiscard]] bool empty_approx() const noexcept {
            return ch_.ring_.empty_approx();
        }
        [[nodiscard]] std::size_t size_approx() const noexcept {
            return ch_.ring_.size_approx();
        }
        [[nodiscard]] static constexpr std::size_t capacity() noexcept {
            return Capacity;
        }
    };

    // ── Factories ─────────────────────────────────────────────────

    // Producer endpoint — lends a Pool share.  Returns nullopt iff
    // (a) the Producer Permission root has not been attached, or
    // (b) exclusive mode is active (with_drained_access in flight).
    // Multiple concurrent producers may hold ProducerHandles —
    // that's the point of the fractional permission.
    [[nodiscard]] std::optional<ProducerHandle> producer() noexcept {
        auto guard = producer_pool_.lend();
        if (!guard) return std::nullopt;
        return ProducerHandle{*this, std::move(*guard)};
    }

    // Consumer endpoint — consumes the Consumer Permission token.
    // Caller mints + splits the Whole, hands the Consumer half here.
    // Returns the unique ConsumerHandle for this channel.
    [[nodiscard]] ConsumerHandle consumer(safety::Permission<consumer_tag>&& perm) noexcept {
        return ConsumerHandle{*this, std::move(perm)};
    }

    // ── Mode transition: scoped exclusive access ──────────────────
    //
    // For special operations that need ALL producers out — atomic
    // reset, capacity resize, channel migration.  Body runs while
    // producers are blocked from acquiring new shares.  Returns true
    // iff body ran (false iff producers were active).
    //
    // Body signature: void() noexcept.
    //
    // Cost: one CAS to acquire (succeeds iff outstanding == 0),
    // one release-store to deposit back.  Body's runtime is the rest.
    // Subsequent producer() calls succeed once body returns.
    template <typename Body>
        requires std::is_invocable_v<Body>
    bool with_drained_access(Body&& body)
        noexcept(std::is_nothrow_invocable_v<Body>)
    {
        auto upgrade = producer_pool_.try_upgrade();
        if (!upgrade) return false;
        std::forward<Body>(body)();
        producer_pool_.deposit_exclusive(std::move(*upgrade));
        return true;
    }

    // ── Diagnostics ───────────────────────────────────────────────

    [[nodiscard]] std::uint64_t outstanding_producers() const noexcept {
        return producer_pool_.outstanding();
    }

    [[nodiscard]] bool is_exclusive_active() const noexcept {
        return producer_pool_.is_exclusive_out();
    }

    [[nodiscard]] bool empty_approx() const noexcept {
        return ring_.empty_approx();
    }
    [[nodiscard]] std::size_t size_approx() const noexcept {
        return ring_.size_approx();
    }
    [[nodiscard]] static constexpr std::size_t capacity() noexcept {
        return Capacity;
    }

private:
    MpscRing<T, Capacity>                            ring_;
    safety::SharedPermissionPool<producer_tag>       producer_pool_;
};

}  // namespace crucible::concurrent

// ── splits_into auto-specialization ─────────────────────────────────
//
// User declares Permission<Whole<MyTag>> at startup; framework does
// the rest.  Both binary (splits_into) and N-ary (splits_into_pack)
// forms specialized so users can use either permission_split or
// permission_split_n.

namespace crucible::safety {

template <typename UserTag>
struct splits_into<concurrent::mpsc_tag::Whole<UserTag>,
                   concurrent::mpsc_tag::Producer<UserTag>,
                   concurrent::mpsc_tag::Consumer<UserTag>>
    : std::true_type {};

template <typename UserTag>
struct splits_into_pack<concurrent::mpsc_tag::Whole<UserTag>,
                        concurrent::mpsc_tag::Producer<UserTag>,
                        concurrent::mpsc_tag::Consumer<UserTag>>
    : std::true_type {};

}  // namespace crucible::safety
