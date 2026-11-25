// A bulk drain must deliver exactly what the same number of single pops
// would deliver, in the same order.  Everything below is a way of
// cornering that one claim.

#include <crucible/TraceRing.h>
#include <fixy/Core.h>
#include <fixy/Ctx.h>
#include <fixy/check/Check.h>
#include <fixy/os/ThreadTasks.h>
#include <foundation/Platform.h>
#include <foundation/effects/Effect.h>

#include "trace_rig.h"

#include <cstdint>
#include <utility>

namespace {

// The context of the test, which each check of the Check family takes.
constexpr ::fixy::TestRunnerCtx kTest{::foundation::effects::testing::test()};

// A ring on the heap: its slots are too large for the stack.
[[nodiscard]] ::fixy::Box<crucible::test::OpenRing> make_ring() noexcept {
    return ::fixy::mint_box<crucible::test::OpenRing>(::foundation::effects::testing::test().alloc);
}

}  // namespace

using crucible::SchemaHash;
using crucible::ShapeHash;
using crucible::ScopeHash;
using crucible::CallsiteHash;
using crucible::MetaIndex;
using crucible::OpDepth;
using crucible::TraceRing;

// Every field of an entry is derived from the sequence number, so a
// drained entry identifies its own position in the push order.
static TraceRing::Entry make_entry(uint64_t i) noexcept {
    TraceRing::Entry e{};
    e.schema_hash = SchemaHash{i};
    e.shape_hash = ShapeHash{i ^ 0xDEADBEEFCAFEBABEULL};
    e.num_inputs = static_cast<uint16_t>(i & 0xFF);
    e.num_outputs = static_cast<uint16_t>((i >> 8) & 0xFF);
    return e;
}

static MetaIndex make_meta(uint64_t i) noexcept { return MetaIndex{static_cast<uint32_t>(i & 0xFFFFFF)}; }

static ScopeHash make_scope(uint64_t i) noexcept { return ScopeHash{i * 0x9E3779B97F4A7C15ULL}; }

static CallsiteHash make_callsite(uint64_t i) noexcept { return CallsiteHash{i ^ 0xCBF29CE484222325ULL}; }

// The depths go round 0 to 6, so a depth that a drain drops or moves reads
// as a different depth at most positions.
static OpDepth make_depth(uint64_t i) noexcept { return OpDepth{static_cast<uint16_t>(i % 7)}; }

// The call of try_pop_batch of this file.  The batch drain inlines with its
// copies, and the function is not inlined, so each caller holds one call.
// Each frame gives its runs a brand of their own, so each of the two callers
// has its own instance.
template <class Entries, class Metas, class Scopes, class Calls, class Depths>
[[gnu::noinline]] static uint32_t pop_batch(crucible::test::OpenRing& ring, Entries entries, Metas metas, Scopes scopes,
                                            Calls calls, Depths depths) noexcept {
    return ring.try_pop_batch(std::move(entries), std::move(metas), std::move(scopes), std::move(calls),
                              std::move(depths));
}

// The five outputs of a drain, each a heap array of one count.
struct Outputs {
    ::fixy::HeapArray<TraceRing::Entry> entries;
    ::fixy::HeapArray<MetaIndex> metas;
    ::fixy::HeapArray<ScopeHash> scopes;
    ::fixy::HeapArray<CallsiteHash> calls;
    ::fixy::HeapArray<OpDepth> depths;
};

[[nodiscard]] static Outputs make_outputs(std::size_t count) noexcept {
    ::foundation::effects::Alloc const alloc = ::foundation::effects::testing::test().alloc;
    return Outputs{.entries = ::fixy::mint_heap_array(alloc, count, TraceRing::Entry{}),
                   .metas = ::fixy::mint_heap_array(alloc, count, MetaIndex{}),
                   .scopes = ::fixy::mint_heap_array(alloc, count, ScopeHash{}),
                   .calls = ::fixy::mint_heap_array(alloc, count, CallsiteHash{}),
                   .depths = ::fixy::mint_heap_array(alloc, count, OpDepth{})};
}

// The two drains of the ring.
enum class DrainForm : uint8_t {
    PopBatch,
    Drain
};

// Drains at most count entries of the ring into the outputs, from the position
// first, and gives the count of the entries.
[[nodiscard]] static uint32_t drain_into(crucible::test::OpenRing& ring, Outputs& outputs, std::size_t first,
                                         std::size_t count, DrainForm form) noexcept {
    uint32_t drained = 0;
    outputs.entries = ::fixy::with_mut(std::move(outputs.entries), [&](auto entry_run) noexcept {
        auto entries = ::fixy::take_window(std::move(entry_run), first, count).expect("the batch fits");
        outputs.metas = ::fixy::with_mut(std::move(outputs.metas), [&](auto meta_run) noexcept {
            auto metas = ::fixy::take_window(std::move(meta_run), first, count).expect("the batch fits");
            outputs.scopes = ::fixy::with_mut(std::move(outputs.scopes), [&](auto scope_run) noexcept {
                auto scopes = ::fixy::take_window(std::move(scope_run), first, count).expect("the batch fits");
                outputs.calls = ::fixy::with_mut(std::move(outputs.calls), [&](auto call_run) noexcept {
                    auto calls = ::fixy::take_window(std::move(call_run), first, count).expect("the batch fits");
                    outputs.depths = ::fixy::with_mut(std::move(outputs.depths), [&](auto depth_run) noexcept {
                        auto depths = ::fixy::take_window(std::move(depth_run), first, count).expect("the batch fits");
                        if (form == DrainForm::PopBatch) {
                            drained = pop_batch(ring, std::move(entries), std::move(metas), std::move(scopes),
                                                std::move(calls), std::move(depths));
                        } else {
                            drained = ring.drain(std::move(entries), std::move(metas), std::move(scopes),
                                                 std::move(calls), std::move(depths));
                        }
                    });
                });
            });
        });
    });
    return drained;
}

static void test_empty_returns_zero() {
    ::fixy::Box<crucible::test::OpenRing> ring = make_ring();

    Outputs out = make_outputs(8);
    uint32_t const n = drain_into(*ring, out, 0, 8, DrainForm::PopBatch);
    CRUCIBLE_TEST_ASSERT(kTest, n == 0);
    CRUCIBLE_TEST_ASSERT(kTest, ring->size().peek() == 0);

    ::fixy::check::pass(kTest, "  test_empty_returns_zero: PASSED\n");
}

// Five empty outputs return zero and drain nothing.
static void test_max_count_zero() {
    ::fixy::Box<crucible::test::OpenRing> ring = make_ring();

    // The ring is deliberately non-empty: a zero count must win over
    // available entries.
    CRUCIBLE_TEST_ASSERT(kTest, ring->try_append(make_entry(7)));
    CRUCIBLE_TEST_ASSERT(kTest, ring->size().peek() == 1);

    // Five empty windows of five frames.
    Outputs out = make_outputs(1);
    uint32_t const n = drain_into(*ring, out, 0, 0, DrainForm::PopBatch);
    CRUCIBLE_TEST_ASSERT(kTest, n == 0);
    CRUCIBLE_TEST_ASSERT(kTest, ring->size().peek() == 1);  // entry not drained

    ::fixy::check::pass(kTest, "  test_max_count_zero: PASSED\n");
}

// The outputs are five keyed arrays, so the drain has the fixed extent of a
// batch, as the drain stage of the background thread has.
static void test_single_thread_fifo() {
    ::fixy::Box<crucible::test::OpenRing> ring = make_ring();

    constexpr uint32_t N = 100;
    for (uint64_t i = 0; i < N; ++i) {
        CRUCIBLE_TEST_ASSERT(
            kTest, ring->try_append(make_entry(i), make_meta(i), make_scope(i), make_callsite(i), make_depth(i)));
    }
    CRUCIBLE_TEST_ASSERT(kTest, ring->size().peek() == N);

    auto [out_entries, out_entries_key] = ::fixy::mint_keyed_owner(::fixy::fixed_array_owner<TraceRing::Entry, N>);
    auto [out_metas, out_metas_key] = ::fixy::mint_keyed_owner(::fixy::fixed_array_owner<MetaIndex, N>);
    auto [out_scopes, out_scopes_key] = ::fixy::mint_keyed_owner(::fixy::fixed_array_owner<ScopeHash, N>);
    auto [out_calls, out_calls_key] = ::fixy::mint_keyed_owner(::fixy::fixed_array_owner<CallsiteHash, N>);
    auto [out_depths, out_depths_key] = ::fixy::mint_keyed_owner(::fixy::fixed_array_owner<OpDepth, N>);

    uint32_t n = out_entries.with_mut(out_entries_key, [&](auto entry_run) noexcept {
        return out_metas.with_mut(out_metas_key, [&](auto meta_run) noexcept {
            return out_scopes.with_mut(out_scopes_key, [&](auto scope_run) noexcept {
                return out_calls.with_mut(out_calls_key, [&](auto call_run) noexcept {
                    return out_depths.with_mut(out_depths_key, [&](auto depth_run) noexcept {
                        return pop_batch(*ring, std::move(entry_run), std::move(meta_run), std::move(scope_run),
                                         std::move(call_run), std::move(depth_run));
                    });
                });
            });
        });
    });
    CRUCIBLE_TEST_ASSERT(kTest, n == N);
    CRUCIBLE_TEST_ASSERT(kTest, ring->size().peek() == 0);

    for (uint32_t i = 0; i < N; ++i) {
        CRUCIBLE_TEST_ASSERT(
            kTest, out_entries.get(i).expect("a test reads an element at or past the count of its array")->schema_hash
                       == SchemaHash{i});
        CRUCIBLE_TEST_ASSERT(
            kTest, out_entries.get(i).expect("a test reads an element at or past the count of its array")->shape_hash
                       == ShapeHash{uint64_t(i) ^ 0xDEADBEEFCAFEBABEULL});
        CRUCIBLE_TEST_ASSERT(
            kTest, out_entries.get(i).expect("a test reads an element at or past the count of its array")->num_inputs
                       == (i & 0xFF));
        CRUCIBLE_TEST_ASSERT(kTest,
                             *out_metas.get(i).expect("a test reads an element at or past the count of its array")
                                 == make_meta(i));
        CRUCIBLE_TEST_ASSERT(kTest,
                             *out_scopes.get(i).expect("a test reads an element at or past the count of its array")
                                 == make_scope(i));
        CRUCIBLE_TEST_ASSERT(kTest,
                             *out_calls.get(i).expect("a test reads an element at or past the count of its array")
                                 == make_callsite(i));
        CRUCIBLE_TEST_ASSERT(kTest,
                             *out_depths.get(i).expect("a test reads an element at or past the count of its array")
                                 == make_depth(i));
    }

    ::fixy::check::pass(kTest, "  test_single_thread_fifo: PASSED\n");
}

static void test_multi_batch_drain() {
    ::fixy::Box<crucible::test::OpenRing> ring = make_ring();

    constexpr uint32_t N = 1000;
    for (uint64_t i = 0; i < N; ++i) {
        CRUCIBLE_TEST_ASSERT(
            kTest, ring->try_append(make_entry(i), make_meta(i), make_scope(i), make_callsite(i), make_depth(i)));
    }

    Outputs out = make_outputs(N);

    // The sizes are irregular on purpose, so no drain starts or ends on
    // a round boundary and none of them is a multiple of another.  The
    // last batch stops at the end of the outputs.
    constexpr ::fixy::FixedArray<uint32_t, 9> batch_sizes{7, 13, 31, 64, 100, 200, 500, 100, 50};
    uint32_t total = 0;
    for (uint32_t batch_size : batch_sizes) {
        total += drain_into(*ring, out, total, ::fixy::min(batch_size, N - total), DrainForm::PopBatch);
        if (total >= N) break;
    }

    CRUCIBLE_TEST_ASSERT(kTest, total == N);
    for (uint32_t i = 0; i < N; ++i) {
        CRUCIBLE_TEST_ASSERT(
            kTest, out.entries.get(i).expect("a test reads an element at or past the count of its array")->schema_hash
                       == SchemaHash{i});
        CRUCIBLE_TEST_ASSERT(kTest,
                             *out.metas.get(i).expect("a test reads an element at or past the count of its array")
                                 == make_meta(i));
        CRUCIBLE_TEST_ASSERT(kTest,
                             *out.depths.get(i).expect("a test reads an element at or past the count of its array")
                                 == make_depth(i));
    }

    ::fixy::check::pass(kTest, "  test_multi_batch_drain: PASSED\n");
}

// Draining a run of entries that straddles the end of the ring has to
// copy two segments rather than one.  The push-then-drain below moves
// head and tail away from zero without leaving live entries behind,
// which is the only way to reach that boundary from an empty ring.

static void test_wrap_around() {
    ::fixy::Box<crucible::test::OpenRing> ring = make_ring();

    constexpr uint32_t HALF = TraceRing::CAPACITY / 2;
    for (uint64_t i = 0; i < HALF; ++i) {
        CRUCIBLE_TEST_ASSERT(kTest, ring->try_append(make_entry(i)));
    }
    {
        Outputs half = make_outputs(HALF);
        CRUCIBLE_TEST_ASSERT(kTest, drain_into(*ring, half, 0, HALF, DrainForm::PopBatch) == HALF);
    }

    constexpr uint32_t WRAP_N = 1000;
    for (uint64_t i = 0; i < WRAP_N; ++i) {
        // The payloads start well above the ones drained already, so an
        // entry from the first round would be recognisable here.
        CRUCIBLE_TEST_ASSERT(kTest, ring->try_append(make_entry(0x10000ULL + i), MetaIndex::none(), ScopeHash{},
                                                     CallsiteHash{}, make_depth(i)));
    }

    Outputs out = make_outputs(WRAP_N);
    uint32_t const n = drain_into(*ring, out, 0, WRAP_N, DrainForm::PopBatch);
    CRUCIBLE_TEST_ASSERT(kTest, n == WRAP_N);

    for (uint32_t i = 0; i < WRAP_N; ++i) {
        CRUCIBLE_TEST_ASSERT(
            kTest, out.entries.get(i).expect("a test reads an element at or past the count of its array")->schema_hash
                       == SchemaHash{0x10000ULL + i});
        CRUCIBLE_TEST_ASSERT(kTest,
                             *out.depths.get(i).expect("a test reads an element at or past the count of its array")
                                 == make_depth(i));
    }

    ::fixy::check::pass(kTest, "  test_wrap_around: PASSED\n");
}

// Two rings receive the same stream.  One is drained in random batch
// sizes and the other one entry at a time, and the two output
// sequences are then compared field by field.  This is the claim the
// rest of the file only approaches from one side at a time.

static void test_equivalence_with_single_pop() {
    ::fixy::Box<crucible::test::OpenRing> ring_a = make_ring();
    ::fixy::Box<crucible::test::OpenRing> ring_b = make_ring();

    constexpr uint32_t N = 5000;
    for (uint64_t i = 0; i < N; ++i) {
        CRUCIBLE_TEST_ASSERT(
            kTest, ring_a->try_append(make_entry(i), make_meta(i), make_scope(i), make_callsite(i), make_depth(i)));
        CRUCIBLE_TEST_ASSERT(
            kTest, ring_b->try_append(make_entry(i), make_meta(i), make_scope(i), make_callsite(i), make_depth(i)));
    }

    Outputs a_out = make_outputs(N);
    Outputs b_out = make_outputs(N);

    // The seed is fixed, so a failing schedule is reproducible.  Each batch
    // size from 1 to 64 comes from the next block of Philox under the seed.
    constexpr uint64_t kSeed = 0xCAFEBABEDEADBEEFULL;
    uint64_t draw = 0;
    uint32_t a_total = 0;
    while (a_total < N) {
        const ::fixy::Philox4x32::Counter block = ::fixy::Philox4x32::generate(draw, kSeed);
        ++draw;
        const uint32_t drawn = 1u + block.at(::fixy::index<0>()) % 64u;
        uint32_t batch = ::fixy::min(drawn, N - a_total);
        uint32_t const n = drain_into(*ring_a, a_out, a_total, batch, DrainForm::PopBatch);
        CRUCIBLE_TEST_ASSERT(kTest, n == batch);  // the batch is clamped, so it always fills
        a_total += n;
    }

    for (uint32_t i = 0; i < N; ++i) {
        CRUCIBLE_TEST_ASSERT(kTest, drain_into(*ring_b, b_out, i, 1, DrainForm::Drain) == 1);
    }

    for (uint32_t i = 0; i < N; ++i) {
        TraceRing::Entry const& a_entry =
            *a_out.entries.get(i).expect("a test reads an element at or past the count of its array");
        TraceRing::Entry const& b_entry =
            *b_out.entries.get(i).expect("a test reads an element at or past the count of its array");
        CRUCIBLE_TEST_ASSERT(kTest, a_entry.schema_hash == b_entry.schema_hash);
        CRUCIBLE_TEST_ASSERT(kTest, a_entry.shape_hash == b_entry.shape_hash);
        CRUCIBLE_TEST_ASSERT(kTest, a_entry.num_inputs == b_entry.num_inputs);
        CRUCIBLE_TEST_ASSERT(kTest, a_entry.num_outputs == b_entry.num_outputs);
        CRUCIBLE_TEST_ASSERT(
            kTest, *a_out.metas.get(i).expect("a test reads an element at or past the count of its array")
                       == *b_out.metas.get(i).expect("a test reads an element at or past the count of its array"));
        CRUCIBLE_TEST_ASSERT(
            kTest, *a_out.scopes.get(i).expect("a test reads an element at or past the count of its array")
                       == *b_out.scopes.get(i).expect("a test reads an element at or past the count of its array"));
        CRUCIBLE_TEST_ASSERT(
            kTest, *a_out.calls.get(i).expect("a test reads an element at or past the count of its array")
                       == *b_out.calls.get(i).expect("a test reads an element at or past the count of its array"));
        CRUCIBLE_TEST_ASSERT(kTest,
                             *a_out.depths.get(i).expect("a test reads an element at or past the count of its array")
                                 == make_depth(i));
        CRUCIBLE_TEST_ASSERT(kTest,
                             *b_out.depths.get(i).expect("a test reads an element at or past the count of its array")
                                 == make_depth(i));
    }

    ::fixy::check::pass(kTest, "  test_equivalence_with_single_pop: PASSED ({} entries)\n", N);
}

// One producer, one consumer, and a consumer that keeps changing how it
// drains.  Mixing the two drain paths against a live producer is what
// exposes an index that one path advances differently from the other.

static void test_spsc_stress() {
    ::fixy::Box<crucible::test::OpenRing> ring = make_ring();
    constexpr uint64_t N = 100000;

    ::fixy::report(::fixy::Sink::Out, "  test_spsc_stress: 1 producer + 1 consumer, N={}...\n",
                   static_cast<unsigned long long>(N));

    ::fixy::Atomic<bool> producer_done{false};

    // The consumer thread owns this array and its count for the whole run
    // and main reads them only after both threads have joined.  Each
    // delivered schema hash is its own push index, so the array must come
    // out as 0, 1, 2 and so on with nothing missing or repeated.  A delivery
    // past N ends the process at the write of its element.
    ::fixy::HeapArray<uint64_t> mirror =
        ::fixy::mint_heap_array_zeroed<uint64_t>(::foundation::effects::testing::test().alloc, N);
    uint64_t received = 0;

    auto producer = [&] noexcept {
        for (uint64_t i = 0; i < N; ++i) {
            while (!ring->try_append(make_entry(i), make_meta(i), make_scope(i), make_callsite(i))) {
                // The ring is full, so back off and retry the same
                // index rather than dropping it.
                CRUCIBLE_SPIN_PAUSE;
            }
        }
        producer_done.store_release(true);
    };

    auto consumer = [&] noexcept {
        Outputs out = make_outputs(64);

        uint64_t mode = 0;
        while (received < N) {
            uint32_t got = 0;
            const uint64_t m = mode++ & 0x3;
            if (m == 0) {
                got = drain_into(*ring, out, 0, 64, DrainForm::PopBatch);
            } else if (m == 1) {
                got = drain_into(*ring, out, 0, 16, DrainForm::PopBatch);
            } else if (m == 2) {
                got = drain_into(*ring, out, 0, 32, DrainForm::Drain);
            } else {
                got = drain_into(*ring, out, 0, 1, DrainForm::Drain);
            }

            if (got > 0) {
                mirror =
                    ::fixy::with_mut(std::move(mirror), [&entries = out.entries, received, got](auto run) noexcept {
                        for (uint32_t i = 0; i < got; ++i) {
                            auto slot = run.get_mut(received + i).expect("the ring delivers at most N entries");
                            *slot = entries.get(i)
                                        .expect("a test reads an element at or past the count of its array")
                                        ->schema_hash.raw();
                        }
                    });
                received += got;
            } else if (producer_done.load_acquire() && ring->size().peek() == 0) {
                break;
            } else {
                CRUCIBLE_SPIN_PAUSE;
            }
        }
    };

    // The door joins the two threads before it returns.  Main must not read
    // the mirror before this.
    ::fixy::BgLoadCtx const bg{::foundation::effects::testing::bg()};
    ::fixy::thread::run_joined_raw_probe(bg, kTest, "the producer and the consumer share the ring on purpose",
                                         ::fixy::thread::ThreadOptions{}, std::move(producer), std::move(consumer))
        .expect("the thread door starts the producer and the consumer");

    CRUCIBLE_TEST_ASSERT(kTest, received == N);
    for (uint64_t i = 0; i < N; ++i) {
        const uint64_t got = *mirror.view().get(i).expect("the mirror holds N entries");
        if (got != i) {
            ::fixy::fatal("FIFO violation at i={}: expected {}, got {}", i, i, got);
        }
    }

    ::fixy::check::pass(kTest, "  test_spsc_stress: PASSED ({} entries delivered in order)\n",
                        static_cast<unsigned long long>(N));
}

int main() {
    ::fixy::report(::fixy::Sink::Out, "test_trace_ring_pop_batch:\n");

    test_empty_returns_zero();
    test_max_count_zero();
    test_single_thread_fifo();
    test_multi_batch_drain();
    test_wrap_around();
    test_equivalence_with_single_pop();
    test_spsc_stress();

    ::fixy::check::pass(kTest, "test_trace_ring_pop_batch: ALL PASSED\n");
    return 0;
}
