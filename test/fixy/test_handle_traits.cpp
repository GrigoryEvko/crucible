// The four channel poles, driven at run time and cross-checked against
// a real channel.
//
// fixy/concurrent/HandleTraits.h keeps its static_assert wall, which
// reads each predicate against a synthetic of the shape that predicate
// names.  Two things the wall cannot do live here.
//
// The first is the run-time read: every predicate consulted through a
// volatile bound, so the trait reads are not folded away.  Those three
// bodies were inline smoke tests in the old headers, compiled into every
// translation unit that included them and called by nothing, which is
// a shape the port removed.
//
// The second is the cross-check that matters more.  A synthetic proves
// the predicate matches the shape the predicate was written for, which
// is close to circular.  What settles it is the production handle: a
// PermissionedSpscChannel's ConsumerHandle must answer yes to exactly
// the consumer predicate, and its ProducerHandle to exactly the producer
// one, with no synthetic in the way.
//
// The old tree's test/test_is_swmr_handle.cpp cross-checked its two
// predicates against PermissionedSnapshot and SwmrSession.  Neither is
// in the new tree yet, so the single-writer poles are witnessed here by
// synthetics alone.  That is a gap in this file, named rather than
// hidden: when the snapshot channel is ported, its writer and reader go
// into the cross-check below beside the two SPSC handles.

#include <fixy/concurrent/HandleTraits.h>
#include <fixy/concurrent/PermissionedSpscChannel.h>
#include <foundation/permissions/Permission.h>

#include <cstdio>
#include <optional>
#include <type_traits>
#include <utility>

namespace {

namespace c = ::fixy::concurrent;
namespace perm = ::foundation::permissions;

int g_failures = 0;

#define EXPECT(cond)                                                               \
    do {                                                                           \
        if (!(cond)) {                                                             \
            std::fprintf(stderr, "FAIL: %s (%s:%d)\n", #cond, __FILE__, __LINE__); \
            ++g_failures;                                                          \
        }                                                                          \
    } while (0)

// ── the synthetics, one per pole and one per near-miss ───────────────

struct synth_consumer {
    [[nodiscard]] std::optional<int> try_pop() noexcept { return {}; }
};

struct synth_producer {
    [[nodiscard]] bool try_push(int const&) noexcept { return true; }
};

struct synth_writer {
    void publish(int const&) noexcept {}
};

struct synth_reader {
    [[nodiscard]] int load() const noexcept { return 0; }
};

struct synth_hybrid {
    [[nodiscard]] std::optional<int> try_pop() noexcept { return {}; }
    [[nodiscard]] bool try_push(int const&) noexcept { return true; }
};

struct synth_swmr_hybrid {
    void publish(int const&) noexcept {}
    [[nodiscard]] int load() const noexcept { return 0; }
};

// ── the real channel ────────────────────────────────────────────────

struct HandleTraitsTag {};
using Spsc = c::PermissionedSpscChannel<int, 8, HandleTraitsTag>;

// The production handles answer exactly one predicate each.  These are
// the cells that are not circular: neither handle was written with this
// header in view, and the trait reads the member it actually has.
static_assert(c::is_consumer_handle_v<Spsc::ConsumerHandle>);
static_assert(!c::is_producer_handle_v<Spsc::ConsumerHandle>);
static_assert(!c::is_swmr_writer_v<Spsc::ConsumerHandle>);
static_assert(!c::is_swmr_reader_v<Spsc::ConsumerHandle>);

static_assert(c::is_producer_handle_v<Spsc::ProducerHandle>);
static_assert(!c::is_consumer_handle_v<Spsc::ProducerHandle>);
static_assert(!c::is_swmr_writer_v<Spsc::ProducerHandle>);
static_assert(!c::is_swmr_reader_v<Spsc::ProducerHandle>);

// And the payload the trait recovers is the channel's own element type,
// not something the synthetic taught it.
static_assert(std::is_same_v<c::consumer_handle_value_t<Spsc::ConsumerHandle>, int>);
static_assert(std::is_same_v<c::producer_handle_value_t<Spsc::ProducerHandle>, int>);

// The channel itself is not a handle of any pole.  It hands handles out;
// it is not one.
static_assert(!c::is_consumer_handle_v<Spsc>);
static_assert(!c::is_producer_handle_v<Spsc>);

// A different element type carries through.
using SpscBytes = c::PermissionedSpscChannel<unsigned char, 8, HandleTraitsTag>;
static_assert(std::is_same_v<c::consumer_handle_value_t<SpscBytes::ConsumerHandle>, unsigned char>);
static_assert(std::is_same_v<c::producer_handle_value_t<SpscBytes::ProducerHandle>, unsigned char>);

// ── the run-time reads ──────────────────────────────────────────────

void every_predicate_reads_at_run_time() {
    volatile std::size_t const cap = 4;

    for (std::size_t i = 0; i < cap; ++i) {
        EXPECT(c::is_consumer_handle_v<synth_consumer>);
        EXPECT(c::IsConsumerHandle<synth_consumer&&>);
        EXPECT(!c::is_consumer_handle_v<int>);
        EXPECT(!c::is_consumer_handle_v<synth_producer>);
        EXPECT(!c::is_consumer_handle_v<synth_hybrid>);

        EXPECT(c::is_producer_handle_v<synth_producer>);
        EXPECT(c::IsProducerHandle<synth_producer&&>);
        EXPECT(!c::is_producer_handle_v<int>);
        EXPECT(!c::is_producer_handle_v<synth_consumer>);
        EXPECT(!c::is_producer_handle_v<synth_hybrid>);

        EXPECT(c::is_swmr_writer_v<synth_writer>);
        EXPECT(c::IsSwmrWriter<synth_writer&&>);
        EXPECT(c::is_swmr_reader_v<synth_reader>);
        EXPECT(c::IsSwmrReader<synth_reader&&>);
        EXPECT(!c::is_swmr_writer_v<synth_reader>);
        EXPECT(!c::is_swmr_reader_v<synth_writer>);
        EXPECT(!c::is_swmr_writer_v<synth_swmr_hybrid>);
        EXPECT(!c::is_swmr_reader_v<synth_swmr_hybrid>);

        // The production handles, read the same way.
        EXPECT(c::is_consumer_handle_v<Spsc::ConsumerHandle>);
        EXPECT(c::is_producer_handle_v<Spsc::ProducerHandle>);
        EXPECT(!c::is_consumer_handle_v<Spsc::ProducerHandle>);
        EXPECT(!c::is_producer_handle_v<Spsc::ConsumerHandle>);
    }
}

// The handle the trait recognized is the handle that works.  A predicate
// that answered yes about a shape nothing can drive would be worthless,
// so the two halves are exercised through the members the trait named.
void the_recognized_handles_carry_a_value() {
    Spsc channel{};
    auto whole = perm::mint_permission_root<Spsc::whole_tag>();
    auto [producer_perm, consumer_perm] =
        perm::mint_permission_split<Spsc::producer_tag, Spsc::consumer_tag>(std::move(whole));

    auto producer = channel.producer(std::move(producer_perm));
    auto consumer = channel.consumer(std::move(consumer_perm));

    EXPECT(producer.try_push(7));

    auto popped = consumer.try_pop();
    EXPECT(popped.has_value());
    if (popped.has_value()) {
        EXPECT(*popped == 7);
    }

    // An empty channel reports empty through the optional, which is the
    // shape is_consumer_handle_v insists on.
    auto empty = consumer.try_pop();
    EXPECT(!empty.has_value());
}

}  // namespace

int main() {
    every_predicate_reads_at_run_time();
    the_recognized_handles_carry_a_value();

    if (g_failures != 0) {
        std::fprintf(stderr, "test_handle_traits: %d failure(s)\n", g_failures);
        return 1;
    }
    std::printf("test_handle_traits: four poles, and the two SPSC handles answer one each\n");
    return 0;
}
