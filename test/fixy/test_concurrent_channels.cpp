// What fixy/concurrent/PermissionedSpscChannel.h,
// fixy/concurrent/PermissionedMpscChannel.h and
// fixy/concurrent/WorkingSet.h claim, checked.
//
// The two channels put the same ring behind two different ownership
// arguments.  SPSC splits one whole permission into two linear halves,
// so single-producer-single-consumer is a fact about the token types.
// MPSC keeps the consumer linear and makes the producer side a
// refcounted pool, because the ring is structurally many-producer and
// its membership is dynamic.  The refcount is the half that has runtime
// behaviour, so it is the half with the most runtime checks here.

#include <fixy/concurrent/PermissionedMpscChannel.h>
#include <fixy/concurrent/PermissionedSpscChannel.h>

#include <atomic>
#include <cstddef>
#include <cstdio>
#include <optional>
#include <thread>
#include <type_traits>
#include <utility>
#include <vector>

namespace c = fixy::concurrent;
namespace perm = foundation::permissions;

namespace {

struct TraceTag {};
struct MetaTag {};
struct OtherTag {};
// A tag of its own for the threaded channel.  Two channels sharing a
// UserTag share Permission types, so each would mint a second root for
// the same tag — which is the one thing the headers say nothing checks
// at runtime.
struct BulkTag {};

using Spsc = c::PermissionedSpscChannel<int, 8, TraceTag>;
using Mpsc = c::PermissionedMpscChannel<int, 8, MetaTag>;
using OtherSpsc = c::PermissionedSpscChannel<int, 8, OtherTag>;

// ── Ownership is in the types ────────────────────────────────────────

static_assert(!std::is_copy_constructible_v<Spsc::ProducerHandle>);
static_assert(!std::is_copy_assignable_v<Spsc::ProducerHandle>);
static_assert(std::is_move_constructible_v<Spsc::ProducerHandle>);
// A handle binds to one channel for life.  Rebinding would orphan the
// original permission and let a second producer coexist with it.
static_assert(!std::is_move_assignable_v<Spsc::ProducerHandle>);
static_assert(!std::is_move_assignable_v<Spsc::ConsumerHandle>);
static_assert(!std::is_copy_constructible_v<Mpsc::ProducerHandle>);
static_assert(std::is_move_constructible_v<Mpsc::ProducerHandle>);
static_assert(!std::is_move_assignable_v<Mpsc::ConsumerHandle>);

// The channel's identity is its address: the ring's atomics depend on a
// stable one.
static_assert(!std::is_move_constructible_v<Spsc>);
static_assert(!std::is_move_constructible_v<Mpsc>);

// Each channel's tags are distinct types, so two channels cannot trade
// endpoints even when their value type and capacity agree.
static_assert(!std::is_same_v<Spsc::producer_tag, OtherSpsc::producer_tag>);
static_assert(!std::is_same_v<Spsc::producer_tag, Mpsc::producer_tag>);

// The split relation admits exactly the channel's own triple, in that
// order, and both the binary and the variadic form carry an authoring
// witness beside them so a forged half is caught.
static_assert(perm::splits_into_v<Spsc::whole_tag, Spsc::producer_tag, Spsc::consumer_tag>);
static_assert(perm::splits_into_authoring_witness_v<Spsc::whole_tag, Spsc::producer_tag, Spsc::consumer_tag>);
static_assert(perm::splits_into_pack_v<Spsc::whole_tag, Spsc::producer_tag, Spsc::consumer_tag>);
static_assert(perm::splits_into_pack_authoring_witness_v<Spsc::whole_tag, Spsc::producer_tag, Spsc::consumer_tag>);
static_assert(perm::splits_into_v<Mpsc::whole_tag, Mpsc::producer_tag, Mpsc::consumer_tag>);

// Nothing else splits.  Mixing two channels' tags, or reversing the
// halves, is not a relation edge.
static_assert(!perm::splits_into_v<Spsc::whole_tag, Spsc::consumer_tag, Spsc::producer_tag>);
static_assert(!perm::splits_into_v<Spsc::whole_tag, OtherSpsc::producer_tag, Spsc::consumer_tag>);
static_assert(!perm::splits_into_v<Spsc::whole_tag, Mpsc::producer_tag, Mpsc::consumer_tag>);
static_assert(!perm::splits_into_v<OtherSpsc::whole_tag, Spsc::producer_tag, Spsc::consumer_tag>);

// ── The declared footprint ───────────────────────────────────────────

static_assert(c::hot_path_cache_line_bytes == 64);
static_assert(c::conservative_l1d_per_core < c::conservative_l2_per_core);
static_assert(c::conservative_l2_per_core < c::conservative_l3_total);

static_assert(c::cell_line_footprint(0) == 0);
static_assert(c::cell_line_footprint(1) == 64);
static_assert(c::cell_line_footprint(64) == 64);
static_assert(c::cell_line_footprint(65) == 128);

// Two control lines for the SPSC ring's head and tail, three for the
// MPSC ring's head, tail and bitmap word, plus the cell.
static_assert(Spsc::ProducerHandle::per_call_working_set == 2 * 64 + 64);
static_assert(Mpsc::ProducerHandle::per_call_working_set == 3 * 64 + 64);

// Unknown is the maximum, so a type that declares nothing reads as too
// big to parallelize blindly rather than as free.
struct DeclaresNothing {};
static_assert(c::HasStaticPerCallWorkingSet<Spsc::ProducerHandle>);
static_assert(!c::HasStaticPerCallWorkingSet<DeclaresNothing>);
static_assert(c::per_call_working_set_of_v<DeclaresNothing> == c::unknown_per_call_working_set);
static_assert(c::per_call_working_set_of_v<Spsc::ProducerHandle> == Spsc::ProducerHandle::per_call_working_set);

// The saturating add is what keeps a composed footprint from wrapping
// into a small number and reading as cache-resident.
static_assert(c::saturating_ws_add(64, 64) == 128);
static_assert(c::saturating_ws_add(c::unknown_per_call_working_set, 64) == c::unknown_per_call_working_set);
static_assert(c::saturating_ws_add(c::unknown_per_call_working_set - 1, 2) == c::unknown_per_call_working_set);

// ── Runtime: the SPSC split ──────────────────────────────────────────

[[nodiscard]] int spsc_split_and_run() {
    Spsc channel{};

    // One root per whole tag per program.  Splitting it is what creates
    // the two endpoint tokens, and there is no other way to get one.
    auto whole = perm::mint_permission_root<Spsc::whole_tag>();
    auto [producer_perm, consumer_perm] =
        perm::mint_permission_split<Spsc::producer_tag, Spsc::consumer_tag>(std::move(whole));

    auto producer = channel.producer(std::move(producer_perm));
    auto consumer = channel.consumer(std::move(consumer_perm));

    for (int i = 0; i < 8; ++i) {
        if (!producer.try_push(i)) {
            std::fprintf(stderr, "SPSC producer was refused below capacity\n");
            return 1;
        }
    }
    if (producer.try_push(8)) {
        std::fprintf(stderr, "SPSC producer pushed past capacity\n");
        return 1;
    }
    if (producer.size_approx() != 8 || consumer.size_approx() != 8 || channel.size_approx() != 8) {
        std::fprintf(stderr, "the three size views disagreed\n");
        return 1;
    }

    for (int i = 0; i < 8; ++i) {
        const auto item = consumer.try_pop();
        if (!item || *item != i) {
            std::fprintf(stderr, "SPSC consumer got item %d out of order\n", i);
            return 1;
        }
    }
    if (consumer.try_pop().has_value()) {
        std::fprintf(stderr, "SPSC consumer popped from an empty channel\n");
        return 1;
    }

    // Always false by construction: there is no refcount to drain, and
    // the accessor exists only so this channel matches the shape of the
    // pool-backed ones.  Surrendering the recombined whole permission
    // is what proves no endpoint handle is alive here.
    static_assert(!Spsc::is_exclusive_active());
    return 0;
}

// ── Runtime: the MPSC pool ───────────────────────────────────────────

[[nodiscard]] int mpsc_pool_accounting() {
    Mpsc channel{};
    auto consumer = channel.consumer(perm::mint_permission_root<Mpsc::consumer_tag>());

    if (channel.outstanding_producers() != 0 || channel.is_exclusive_active()) {
        std::fprintf(stderr, "a fresh MPSC channel reported outstanding producers\n");
        return 1;
    }

    {
        auto first = channel.producer();
        auto second = channel.producer();
        if (!first || !second) {
            std::fprintf(stderr, "the pool refused a second producer — the fractional side is not fractional\n");
            return 1;
        }
        if (channel.outstanding_producers() != 2) {
            std::fprintf(stderr, "the pool counted %llu shares where two were out\n",
                         static_cast<unsigned long long>(channel.outstanding_producers()));
            return 1;
        }

        // Draining is what exclusive access means, and it cannot
        // succeed while a share is out.
        bool body_ran = false;
        if (channel.with_drained_access([&body_ran] { body_ran = true; })) {
            std::fprintf(stderr, "with_drained_access ran while producers were still out\n");
            return 1;
        }
        if (body_ran) {
            std::fprintf(stderr, "the drained body ran after the upgrade was refused\n");
            return 1;
        }

        if (!first->try_push(1) || !second->try_push(2)) {
            std::fprintf(stderr, "a producer handle could not push\n");
            return 1;
        }
    }

    if (channel.outstanding_producers() != 0) {
        std::fprintf(stderr, "the pool did not reclaim its shares when the handles went out of scope\n");
        return 1;
    }

    bool body_ran = false;
    if (!channel.with_drained_access([&body_ran] { body_ran = true; })) {
        std::fprintf(stderr, "with_drained_access refused with no producers out\n");
        return 1;
    }
    if (!body_ran) {
        std::fprintf(stderr, "with_drained_access returned true without running the body\n");
        return 1;
    }

    // The pool lends again once the body returns, which is what makes
    // the exclusive window scoped rather than terminal.
    {
        auto after = channel.producer();
        if (!after) {
            std::fprintf(stderr, "the pool stayed exclusive after the body returned\n");
            return 1;
        }
    }

    const auto first_item = consumer.try_pop();
    const auto second_item = consumer.try_pop();
    if (!first_item || !second_item) {
        std::fprintf(stderr, "the consumer lost what the two producers pushed\n");
        return 1;
    }
    if (*first_item + *second_item != 3) {
        std::fprintf(stderr, "the consumer got the wrong payloads\n");
        return 1;
    }
    return 0;
}

// Several producer threads, each holding its own pool share.  The
// consumer is linear and stays on this thread, which is the ring's
// requirement rather than the pool's.
[[nodiscard]] int mpsc_many_producer_threads() {
    constexpr int kProducers = 4;
    constexpr int kPerProducer = 500;

    using BulkChannel = c::PermissionedMpscChannel<int, 256, BulkTag>;
    BulkChannel channel{};
    auto consumer = channel.consumer(perm::mint_permission_root<BulkChannel::consumer_tag>());

    std::atomic<int> failures{0};
    std::atomic<int> finished{0};
    int received = 0;
    long long sum = 0;

    {
        std::vector<std::jthread> producers;
        for (int p = 0; p < kProducers; ++p) {
            producers.emplace_back([&channel, &failures, &finished] {
                auto handle = channel.producer();
                if (!handle) {
                    failures.fetch_add(1, std::memory_order_release);
                    finished.fetch_add(1, std::memory_order_release);
                    return;
                }
                for (int i = 0; i < kPerProducer;) {
                    if (handle->try_push(1)) ++i;
                }
                finished.fetch_add(1, std::memory_order_release);
            });
        }

        while (received < kProducers * kPerProducer) {
            if (const auto item = consumer.try_pop()) {
                sum += *item;
                ++received;
            } else if (finished.load(std::memory_order_acquire) == kProducers && consumer.empty_approx()) {
                break;
            }
        }
    }

    if (failures.load(std::memory_order_acquire) != 0) {
        std::fprintf(stderr, "the pool refused a producer thread a share\n");
        return 1;
    }
    if (sum != kProducers * kPerProducer) {
        std::fprintf(stderr, "the channel delivered %lld of %d\n", sum, kProducers * kPerProducer);
        return 1;
    }
    if (channel.outstanding_producers() != 0) {
        std::fprintf(stderr, "shares outlived their producer threads\n");
        return 1;
    }
    return 0;
}

}  // namespace

int main() {
    if (const int rc = spsc_split_and_run(); rc != 0) return rc;
    if (const int rc = mpsc_pool_accounting(); rc != 0) return rc;
    if (const int rc = mpsc_many_producer_threads(); rc != 0) return rc;
    return 0;
}
