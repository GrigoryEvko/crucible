#pragma once

// ═══════════════════════════════════════════════════════════════════
// PermissionedSpscChannel<T, Capacity, UserTag> — SPSC worked example
//
// Combines SpscRing<T, Capacity> (lock-free single-producer single-
// consumer ring) with two Permission<Tag> tokens (one per endpoint).
// The result: TraceRing-style channel where the type system
// distinguishes Producer from Consumer at compile time, AND the
// Permission discipline enforces that exactly ONE producer and ONE
// consumer endpoint can exist per channel instance.
//
//   sizeof(ProducerHandle) == sizeof(PermissionedSpscChannel*)  (Permission EBO)
//   sizeof(ConsumerHandle) == sizeof(PermissionedSpscChannel*)  (Permission EBO)
//
// Per-operation cost (steady state):
//   try_push() : ~5-8 ns  (SpscRing's acquire/release atomics)
//   try_pop()  : ~5-8 ns  (SpscRing's acquire/release atomics)
//   handle construction: 0 ns (move semantics, no allocation)
//
// ─── The two-piece architecture ─────────────────────────────────────
//
//   SpscRing<T, Capacity>      — handles the LOCK-FREE WIRE
//                                  (head/tail acquire-release on
//                                   power-of-two-sized cell array)
//   Permission<Producer<Tag>>  — handles the TYPE-LEVEL ROLE
//   Permission<Consumer<Tag>>     enforcement (linear tokens, no two
//                                  producers nor two consumers per
//                                  channel can exist simultaneously)
//   PermissionedSpscChannel    — composes the two with type-system
//                                  endpoint discrimination
//
// Why both layers?  SpscRing alone is sound for SPSC (head/tail
// happen-before establishes the slot ownership invariant).  But it
// offers no API-level distinction between producer and consumer — any
// thread can call try_push or try_pop because both are methods on
// the ring.  Wrapping with Permission<Producer/Consumer> adds:
//
//   1. Compile-time role discrimination (ProducerHandle has only
//      try_push; ConsumerHandle has only try_pop)
//   2. Linearity tracking (Permission move-only ensures no two
//      producer endpoints coexist for the same channel)
//   3. Grep-able audit trail (every endpoint construction goes
//      through producer()/consumer() factory; every handoff is a
//      visible Permission move into a jthread or function param)
//
// ─── The TraceRing use case (SEPLOG-INT-1, task #384) ──────────────
//
// TraceRing's Vessel-dispatch / bg-drain pair is the canonical
// SPSC channel in Crucible (CRUCIBLE.md §IV.2).  PermissionedSpsc-
// Channel gives:
//
//   * Type system enforces that bg-drain cannot accidentally push
//     into the ring (no .try_push() on ConsumerHandle)
//   * Type system enforces that Vessel-dispatch cannot accidentally
//     drain from the ring (no .try_pop() on ProducerHandle)
//   * Permission discipline prevents two Vessel threads from ever
//     constructing producer endpoints for the same TraceRing (one
//     Permission<Producer> token per channel, linear, must be
//     surrendered to producer() to construct the handle)
//
// All without touching the SpscRing's hot-path bytes — the typed
// wrapper is purely compile-time (release-mode handles are
// sizeof(Channel*) via [[no_unique_address]] EBO; the Permission
// collapses to 0 bytes since it's an empty class).
//
// ─── The two-linear-permissions pattern (vs SWMR's hybrid) ─────────
//
// PermissionedSnapshot.h's SWMR pattern uses ONE linear Permission
// (Writer) + N fractional SharedPermission (Reader) backed by a Pool
// with refcount + mode-transition CAS.
//
// PermissionedSpscChannel uses TWO LINEAR Permissions (Producer +
// Consumer) — both endpoints are unique, neither is fractional.
// Consequence: no Pool, no refcount, no mode transition, simpler
// machinery.  Each endpoint's lifetime is the lifetime of the
// owning ProducerHandle / ConsumerHandle.
//
// ─── Constraints ────────────────────────────────────────────────────
//
//   * T satisfies SpscValue (trivially-copyable, trivially-
//     destructible).  Inherited from SpscRing.
//   * Capacity is a power of two, > 0.  Inherited from SpscRing.
//   * Each PermissionedSpscChannel instance must use a distinct
//     UserTag (or different (T, Capacity)) so its Permission tags
//     don't collide.  In practice: define a tag-class per logical
//     channel (TraceRingChannel, MetaLogChannel, etc.).
//   * ProducerHandle / ConsumerHandle are move-only via embedded
//     Permission's deleted copy.
//   * Per Permission.h's grep-discoverable rule, mint each Whole<Tag>
//     root EXACTLY ONCE per program.  No runtime check enforces this.
//
// ─── Worked example ─────────────────────────────────────────────────
//
//   struct MyChannel {};
//   PermissionedSpscChannel<int, 1024, MyChannel> channel;
//
//   auto whole = permission_root_mint<spsc_tag::Whole<MyChannel>>();
//   auto [prod_perm, cons_perm] = permission_split<
//       spsc_tag::Producer<MyChannel>,
//       spsc_tag::Consumer<MyChannel>>(std::move(whole));
//
//   auto producer = channel.producer(std::move(prod_perm));
//   auto consumer = channel.consumer(std::move(cons_perm));
//
//   // Cross-thread handoff via jthread move:
//   std::jthread producer_thread{
//       [p = std::move(producer)](auto) mutable {
//           for (int i = 0; i < 1000; ++i) {
//               while (!p.try_push(i)) std::this_thread::yield();
//           }
//       }
//   };
//
//   std::jthread consumer_thread{
//       [c = std::move(consumer)](auto) mutable {
//           for (;;) {
//               if (auto v = c.try_pop()) { /* use *v */ }
//               else                       std::this_thread::yield();
//           }
//       }
//   };
//
//   // producer.try_pop()  is a COMPILE ERROR — no such method
//   // consumer.try_push() is a COMPILE ERROR — no such method
//
// ─── References ─────────────────────────────────────────────────────
//
//   THREADING.md §5.5 — Tier 4 queue facade design
//   PermissionedSnapshot.h — sibling SWMR worked example
//   safety/Permission.h — Permission/permission_split machinery
//   concurrent/SpscRing.h — underlying lock-free ring primitive
//   session_types.md §IV.2 — TraceRing as a typed session
//   CRUCIBLE.md §IV.2 — TraceRing runtime spec
// ═══════════════════════════════════════════════════════════════════

#include <crucible/Platform.h>
#include <crucible/concurrent/SpscRing.h>
#include <crucible/safety/Permission.h>
#include <crucible/safety/Pinned.h>

#include <cstddef>
#include <optional>
#include <type_traits>
#include <utility>

namespace crucible::concurrent {

// ── Tag tree for PermissionedSpscChannel ───────────────────────────
//
// Each logical channel picks its own UserTag (a phantom type, typically
// an empty struct).  The (Whole, Producer, Consumer) triple is
// auto-specialized for splits_into below — no per-tag boilerplate.

namespace spsc_tag {

template <typename UserTag> struct Whole    {};
template <typename UserTag> struct Producer {};
template <typename UserTag> struct Consumer {};

}  // namespace spsc_tag

// ── PermissionedSpscChannel<T, Capacity, UserTag> ──────────────────

template <SpscValue T, std::size_t Capacity, typename UserTag = void>
class PermissionedSpscChannel
    : public safety::Pinned<PermissionedSpscChannel<T, Capacity, UserTag>> {
public:
    using value_type   = T;
    using user_tag     = UserTag;
    using whole_tag    = spsc_tag::Whole<UserTag>;
    using producer_tag = spsc_tag::Producer<UserTag>;
    using consumer_tag = spsc_tag::Consumer<UserTag>;

    static constexpr std::size_t channel_capacity = Capacity;

    // ── Construction ──────────────────────────────────────────────
    //
    // Default-constructed channel; user mints + splits Permission
    // separately and hands the halves to producer() / consumer().
    // Pinned (per CRTP base): no copy, no move — the channel's
    // identity IS its memory address (the SpscRing's atomics depend
    // on a stable address).

    PermissionedSpscChannel() noexcept = default;

    // ── ProducerHandle ────────────────────────────────────────────
    //
    // Move-only via the embedded Permission's deleted copy.  Constructed
    // ONLY through producer() factory (private ctor + friend).
    // sizeof(ProducerHandle) == sizeof(PermissionedSpscChannel*) via
    // EBO (the Permission is an empty class; [[no_unique_address]]
    // collapses it to 0 bytes; in DEBUG with the future tracker it
    // grows by 1 byte + alignment).
    //
    // EXPOSES try_push only — try_pop is structurally impossible.

    class ProducerHandle {
        // Reference (not pointer): the channel is Pinned, its address
        // is stable for life, and a handle is bound to ONE channel
        // permanently.  Reference forbids reassign + default-construct;
        // implicitly deletes move-assignment which would otherwise
        // silently violate Permission linearity (defaulted move on an
        // empty Permission is a no-op, leaving BOTH source and target
        // claiming the linear token).
        PermissionedSpscChannel& ch_;
        [[no_unique_address]] safety::Permission<producer_tag> perm_;

        constexpr ProducerHandle(PermissionedSpscChannel& c,
                                 safety::Permission<producer_tag>&& p) noexcept
            : ch_{c}, perm_{std::move(p)} {}
        friend class PermissionedSpscChannel;

    public:
        ProducerHandle(const ProducerHandle&)
            = delete("ProducerHandle owns the Producer Permission — copy would duplicate the linear token");
        ProducerHandle& operator=(const ProducerHandle&)
            = delete("ProducerHandle owns the Producer Permission — assignment would overwrite the linear token");
        constexpr ProducerHandle(ProducerHandle&&) noexcept = default;
        // Move-assignment is implicitly deleted by the reference member
        // (reference can't be rebound).  Explicit `= delete` here makes
        // the intent visible at the API surface; without it, the
        // diagnostic on attempted move-assign points at the implicitly-
        // deleted special member which is harder to grep.
        ProducerHandle& operator=(ProducerHandle&&)
            = delete("ProducerHandle binds to ONE channel for life — rebinding would orphan the original Permission and silently allow a second producer to coexist");

        // Push — ~5-8 ns uncontended per SpscRing's contract.  Returns
        // false iff the ring is full; caller decides backpressure
        // (yield + retry, drop, log, etc.).  Inlined to single SpscRing
        // call by the optimizer.
        [[nodiscard, gnu::hot]] bool try_push(const T& item) noexcept {
            return ch_.ring_.try_push(item);
        }

        // Diagnostics — snapshot reads, NOT exact (use for telemetry
        // and "should we keep retrying?" decisions only, NEVER for
        // correctness invariants).
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

    // ── ConsumerHandle ────────────────────────────────────────────
    //
    // Move-only mirror of ProducerHandle.  EXPOSES try_pop only —
    // try_push is structurally impossible.

    class ConsumerHandle {
        // Reference (not pointer): same rationale as ProducerHandle.
        // Forbids move-assign which would silently violate the
        // Consumer Permission's linearity.
        PermissionedSpscChannel& ch_;
        [[no_unique_address]] safety::Permission<consumer_tag> perm_;

        constexpr ConsumerHandle(PermissionedSpscChannel& c,
                                 safety::Permission<consumer_tag>&& p) noexcept
            : ch_{c}, perm_{std::move(p)} {}
        friend class PermissionedSpscChannel;

    public:
        ConsumerHandle(const ConsumerHandle&)
            = delete("ConsumerHandle owns the Consumer Permission — copy would duplicate the linear token");
        ConsumerHandle& operator=(const ConsumerHandle&)
            = delete("ConsumerHandle owns the Consumer Permission — assignment would overwrite the linear token");
        constexpr ConsumerHandle(ConsumerHandle&&) noexcept = default;
        ConsumerHandle& operator=(ConsumerHandle&&)
            = delete("ConsumerHandle binds to ONE channel for life — rebinding would orphan the original Permission and silently allow a second consumer to coexist");

        // Pop — ~5-8 ns uncontended per SpscRing's contract.  Returns
        // nullopt iff the ring is empty; caller decides whether to
        // yield/spin/sleep.  Inlined to single SpscRing call.
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

    // Producer endpoint — consumes the Producer Permission token.
    // Caller mints via `permission_root_mint<whole_tag>()` then
    // `permission_split<producer_tag, consumer_tag>(whole)` at startup,
    // moves the producer half into producer().  Returns the unique
    // ProducerHandle for this channel.
    [[nodiscard]] ProducerHandle producer(safety::Permission<producer_tag>&& perm) noexcept {
        return ProducerHandle{*this, std::move(perm)};
    }

    // Consumer endpoint — symmetric to producer().
    [[nodiscard]] ConsumerHandle consumer(safety::Permission<consumer_tag>&& perm) noexcept {
        return ConsumerHandle{*this, std::move(perm)};
    }

    // ── Channel-level diagnostics (any thread, NOT exact) ─────────

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
    SpscRing<T, Capacity> ring_;
};

}  // namespace crucible::concurrent

// ── splits_into auto-specialization ─────────────────────────────────
//
// User declares Permission<Whole<MyTag>> at startup; framework does
// the rest.  splits_into binary form supports the canonical
// (Whole → Producer + Consumer) decomposition without per-tag
// boilerplate.  Both forms (binary splits_into and N-ary
// splits_into_pack) are specialized so users can use either
// permission_split (binary) or permission_split_n (variadic).

namespace crucible::safety {

template <typename UserTag>
struct splits_into<concurrent::spsc_tag::Whole<UserTag>,
                   concurrent::spsc_tag::Producer<UserTag>,
                   concurrent::spsc_tag::Consumer<UserTag>>
    : std::true_type {};

template <typename UserTag>
struct splits_into_pack<concurrent::spsc_tag::Whole<UserTag>,
                        concurrent::spsc_tag::Producer<UserTag>,
                        concurrent::spsc_tag::Consumer<UserTag>>
    : std::true_type {};

}  // namespace crucible::safety
