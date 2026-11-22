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

#include <cstdint>

namespace {

// The context of the test, which each check of the Check family takes.
constexpr ::fixy::TestRunnerCtx kTest{::foundation::effects::testing::test()};

// A ring on the heap: its slots are too large for the stack.
[[nodiscard]] ::fixy::Box<crucible::TraceRing> make_ring() noexcept {
    return ::fixy::mint_box<crucible::TraceRing>(::foundation::effects::testing::test().alloc);
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

// The one call of try_pop_batch in this file.  The batch drain inlines with
// its copies, and one copy of it keeps the object below its size limit.
[[gnu::noinline]] static uint32_t pop_batch(TraceRing& ring, ::fixy::View<TraceRing::Entry> entries,
                                            ::fixy::View<MetaIndex> metas, ::fixy::View<ScopeHash> scopes,
                                            ::fixy::View<CallsiteHash> calls, ::fixy::View<OpDepth> depths) noexcept {
    return ring.try_pop_batch(entries, metas, scopes, calls, depths);
}

static void test_empty_returns_zero() {
    ::fixy::Box<TraceRing> ring = make_ring();

    ::fixy::FixedArray<TraceRing::Entry, 8> out_entries{};
    ::fixy::FixedArray<MetaIndex, 8> out_metas{};
    ::fixy::FixedArray<ScopeHash, 8> out_scopes{};
    ::fixy::FixedArray<CallsiteHash, 8> out_calls{};
    ::fixy::FixedArray<OpDepth, 8> out_depths{};

    uint32_t n = out_entries.with_mut([&](auto entry_run) noexcept {
        return out_metas.with_mut([&](auto meta_run) noexcept {
            return out_scopes.with_mut([&](auto scope_run) noexcept {
                return out_calls.with_mut([&](auto call_run) noexcept {
                    return out_depths.with_mut([&](auto depth_run) noexcept {
                        return pop_batch(*ring, entry_run.view(), meta_run.view(), scope_run.view(), call_run.view(),
                                         depth_run.view());
                    });
                });
            });
        });
    });
    CRUCIBLE_TEST_ASSERT(kTest, n == 0);
    CRUCIBLE_TEST_ASSERT(kTest, ring->size().peek() == 0);

    ::fixy::check::pass(kTest, "  test_empty_returns_zero: PASSED\n");
}

// Five empty outputs return zero and drain nothing.
static void test_max_count_zero() {
    ::fixy::Box<TraceRing> ring = make_ring();

    // The ring is deliberately non-empty: a zero count must win over
    // available entries.
    CRUCIBLE_TEST_ASSERT(kTest, ring->try_append(make_entry(7)));
    CRUCIBLE_TEST_ASSERT(kTest, ring->size().peek() == 1);

    uint32_t n = pop_batch(*ring, ::fixy::View<TraceRing::Entry>{}, ::fixy::View<MetaIndex>{},
                           ::fixy::View<ScopeHash>{}, ::fixy::View<CallsiteHash>{}, ::fixy::View<OpDepth>{});
    CRUCIBLE_TEST_ASSERT(kTest, n == 0);
    CRUCIBLE_TEST_ASSERT(kTest, ring->size().peek() == 1);  // entry not drained

    ::fixy::check::pass(kTest, "  test_max_count_zero: PASSED\n");
}

static void test_single_thread_fifo() {
    ::fixy::Box<TraceRing> ring = make_ring();

    constexpr uint32_t N = 100;
    for (uint64_t i = 0; i < N; ++i) {
        CRUCIBLE_TEST_ASSERT(
            kTest, ring->try_append(make_entry(i), make_meta(i), make_scope(i), make_callsite(i), make_depth(i)));
    }
    CRUCIBLE_TEST_ASSERT(kTest, ring->size().peek() == N);

    ::fixy::FixedArray<TraceRing::Entry, N> out_entries{};
    ::fixy::FixedArray<MetaIndex, N> out_metas{};
    ::fixy::FixedArray<ScopeHash, N> out_scopes{};
    ::fixy::FixedArray<CallsiteHash, N> out_calls{};
    ::fixy::FixedArray<OpDepth, N> out_depths{};

    uint32_t n = out_entries.with_mut([&](auto entry_run) noexcept {
        return out_metas.with_mut([&](auto meta_run) noexcept {
            return out_scopes.with_mut([&](auto scope_run) noexcept {
                return out_calls.with_mut([&](auto call_run) noexcept {
                    return out_depths.with_mut([&](auto depth_run) noexcept {
                        return pop_batch(*ring, entry_run.view(), meta_run.view(), scope_run.view(), call_run.view(),
                                         depth_run.view());
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
    ::fixy::Box<TraceRing> ring = make_ring();

    constexpr uint32_t N = 1000;
    for (uint64_t i = 0; i < N; ++i) {
        CRUCIBLE_TEST_ASSERT(
            kTest, ring->try_append(make_entry(i), make_meta(i), make_scope(i), make_callsite(i), make_depth(i)));
    }

    ::fixy::FixedArray<TraceRing::Entry, N> out_entries{};
    ::fixy::FixedArray<MetaIndex, N> out_metas{};
    ::fixy::FixedArray<ScopeHash, N> out_scopes{};
    ::fixy::FixedArray<CallsiteHash, N> out_calls{};
    ::fixy::FixedArray<OpDepth, N> out_depths{};

    // The sizes are irregular on purpose, so no drain starts or ends on
    // a round boundary and none of them is a multiple of another.  The
    // last batch stops at the end of the outputs.
    constexpr ::fixy::FixedArray<uint32_t, 9> batch_sizes{7, 13, 31, 64, 100, 200, 500, 100, 50};
    uint32_t total = 0;
    for (uint32_t batch_size : batch_sizes) {
        const uint32_t batch = ::fixy::min(batch_size, N - total);
        uint32_t n = out_entries.with_mut([&](auto entry_run) noexcept {
            return out_metas.with_mut([&](auto meta_run) noexcept {
                return out_scopes.with_mut([&](auto scope_run) noexcept {
                    return out_calls.with_mut([&](auto call_run) noexcept {
                        return out_depths.with_mut([&](auto depth_run) noexcept {
                            return pop_batch(*ring, entry_run.view().window(total, batch).expect("the batch fits"),
                                             meta_run.view().window(total, batch).expect("the batch fits"),
                                             scope_run.view().window(total, batch).expect("the batch fits"),
                                             call_run.view().window(total, batch).expect("the batch fits"),
                                             depth_run.view().window(total, batch).expect("the batch fits"));
                        });
                    });
                });
            });
        });
        total += n;
        if (total >= N) break;
    }

    CRUCIBLE_TEST_ASSERT(kTest, total == N);
    for (uint32_t i = 0; i < N; ++i) {
        CRUCIBLE_TEST_ASSERT(
            kTest, out_entries.get(i).expect("a test reads an element at or past the count of its array")->schema_hash
                       == SchemaHash{i});
        CRUCIBLE_TEST_ASSERT(kTest,
                             *out_metas.get(i).expect("a test reads an element at or past the count of its array")
                                 == make_meta(i));
        CRUCIBLE_TEST_ASSERT(kTest,
                             *out_depths.get(i).expect("a test reads an element at or past the count of its array")
                                 == make_depth(i));
    }

    ::fixy::check::pass(kTest, "  test_multi_batch_drain: PASSED\n");
}

// Draining a run of entries that straddles the end of the ring has to
// copy two segments rather than one.  The push-then-drain below moves
// head and tail away from zero without leaving live entries behind,
// which is the only way to reach that boundary from an empty ring.

static void test_wrap_around() {
    ::fixy::Box<TraceRing> ring = make_ring();

    // The staging buffers have static storage: an array of half the ring's
    // entries would not fit on the stack.
    constexpr uint32_t HALF = TraceRing::CAPACITY / 2;
    for (uint64_t i = 0; i < HALF; ++i) {
        CRUCIBLE_TEST_ASSERT(kTest, ring->try_append(make_entry(i)));
    }
    {
        static ::fixy::FixedArray<TraceRing::Entry, HALF> half_e{};
        static ::fixy::FixedArray<MetaIndex, HALF> half_m{};
        static ::fixy::FixedArray<ScopeHash, HALF> half_s{};
        static ::fixy::FixedArray<CallsiteHash, HALF> half_c{};
        static ::fixy::FixedArray<OpDepth, HALF> half_d{};
        uint32_t n = half_e.with_mut([&](auto entry_run) noexcept {
            return half_m.with_mut([&](auto meta_run) noexcept {
                return half_s.with_mut([&](auto scope_run) noexcept {
                    return half_c.with_mut([&](auto call_run) noexcept {
                        return half_d.with_mut([&](auto depth_run) noexcept {
                            return pop_batch(*ring, entry_run.view(), meta_run.view(), scope_run.view(),
                                             call_run.view(), depth_run.view());
                        });
                    });
                });
            });
        });
        CRUCIBLE_TEST_ASSERT(kTest, n == HALF);
    }

    constexpr uint32_t WRAP_N = 1000;
    for (uint64_t i = 0; i < WRAP_N; ++i) {
        // The payloads start well above the ones drained already, so an
        // entry from the first round would be recognisable here.
        CRUCIBLE_TEST_ASSERT(kTest, ring->try_append(make_entry(0x10000ULL + i), MetaIndex::none(), ScopeHash{},
                                                     CallsiteHash{}, make_depth(i)));
    }

    ::fixy::FixedArray<TraceRing::Entry, WRAP_N> e{};
    ::fixy::FixedArray<MetaIndex, WRAP_N> m{};
    ::fixy::FixedArray<ScopeHash, WRAP_N> s{};
    ::fixy::FixedArray<CallsiteHash, WRAP_N> c{};
    ::fixy::FixedArray<OpDepth, WRAP_N> d{};
    uint32_t n = e.with_mut([&](auto entry_run) noexcept {
        return m.with_mut([&](auto meta_run) noexcept {
            return s.with_mut([&](auto scope_run) noexcept {
                return c.with_mut([&](auto call_run) noexcept {
                    return d.with_mut([&](auto depth_run) noexcept {
                        return pop_batch(*ring, entry_run.view(), meta_run.view(), scope_run.view(), call_run.view(),
                                         depth_run.view());
                    });
                });
            });
        });
    });
    CRUCIBLE_TEST_ASSERT(kTest, n == WRAP_N);

    for (uint32_t i = 0; i < WRAP_N; ++i) {
        CRUCIBLE_TEST_ASSERT(kTest,
                             e.get(i).expect("a test reads an element at or past the count of its array")->schema_hash
                                 == SchemaHash{0x10000ULL + i});
        CRUCIBLE_TEST_ASSERT(kTest, *d.get(i).expect("a test reads an element at or past the count of its array")
                                        == make_depth(i));
    }

    ::fixy::check::pass(kTest, "  test_wrap_around: PASSED\n");
}

// Two rings receive the same stream.  One is drained in random batch
// sizes and the other one entry at a time, and the two output
// sequences are then compared field by field.  This is the claim the
// rest of the file only approaches from one side at a time.

static void test_equivalence_with_single_pop() {
    ::fixy::Box<TraceRing> ring_a = make_ring();
    ::fixy::Box<TraceRing> ring_b = make_ring();

    constexpr uint32_t N = 5000;
    for (uint64_t i = 0; i < N; ++i) {
        CRUCIBLE_TEST_ASSERT(
            kTest, ring_a->try_append(make_entry(i), make_meta(i), make_scope(i), make_callsite(i), make_depth(i)));
        CRUCIBLE_TEST_ASSERT(
            kTest, ring_b->try_append(make_entry(i), make_meta(i), make_scope(i), make_callsite(i), make_depth(i)));
    }

    // The outputs have static storage: the two entry arrays would not fit
    // on the stack.
    static ::fixy::FixedArray<TraceRing::Entry, N> a_entries{};
    static ::fixy::FixedArray<TraceRing::Entry, N> b_entries{};
    static ::fixy::FixedArray<MetaIndex, N> a_metas{};
    static ::fixy::FixedArray<MetaIndex, N> b_metas{};
    static ::fixy::FixedArray<ScopeHash, N> a_scopes{};
    static ::fixy::FixedArray<ScopeHash, N> b_scopes{};
    static ::fixy::FixedArray<CallsiteHash, N> a_calls{};
    static ::fixy::FixedArray<CallsiteHash, N> b_calls{};
    static ::fixy::FixedArray<OpDepth, N> a_depths{};
    static ::fixy::FixedArray<OpDepth, N> b_depths{};

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
        uint32_t n = a_entries.with_mut([&](auto entry_run) noexcept {
            return a_metas.with_mut([&](auto meta_run) noexcept {
                return a_scopes.with_mut([&](auto scope_run) noexcept {
                    return a_calls.with_mut([&](auto call_run) noexcept {
                        return a_depths.with_mut([&](auto depth_run) noexcept {
                            return pop_batch(*ring_a, entry_run.view().window(a_total, batch).expect("the batch fits"),
                                             meta_run.view().window(a_total, batch).expect("the batch fits"),
                                             scope_run.view().window(a_total, batch).expect("the batch fits"),
                                             call_run.view().window(a_total, batch).expect("the batch fits"),
                                             depth_run.view().window(a_total, batch).expect("the batch fits"));
                        });
                    });
                });
            });
        });
        CRUCIBLE_TEST_ASSERT(kTest, n == batch);  // the batch is clamped, so it always fills
        a_total += n;
    }

    for (uint32_t i = 0; i < N; ++i) {
        uint32_t n = b_entries.with_mut([&](auto entry_run) noexcept {
            return b_metas.with_mut([&](auto meta_run) noexcept {
                return b_scopes.with_mut([&](auto scope_run) noexcept {
                    return b_calls.with_mut([&](auto call_run) noexcept {
                        return b_depths.with_mut([&](auto depth_run) noexcept {
                            return ring_b->drain(entry_run.view().window(i, 1).expect("one entry fits"),
                                                 meta_run.view().window(i, 1).expect("one entry fits"),
                                                 scope_run.view().window(i, 1).expect("one entry fits"),
                                                 call_run.view().window(i, 1).expect("one entry fits"),
                                                 depth_run.view().window(i, 1).expect("one entry fits"));
                        });
                    });
                });
            });
        });
        CRUCIBLE_TEST_ASSERT(kTest, n == 1);
    }

    for (uint32_t i = 0; i < N; ++i) {
        CRUCIBLE_TEST_ASSERT(
            kTest,
            a_entries.get(i).expect("a test reads an element at or past the count of its array")->schema_hash
                == b_entries.get(i).expect("a test reads an element at or past the count of its array")->schema_hash);
        CRUCIBLE_TEST_ASSERT(
            kTest,
            a_entries.get(i).expect("a test reads an element at or past the count of its array")->shape_hash
                == b_entries.get(i).expect("a test reads an element at or past the count of its array")->shape_hash);
        CRUCIBLE_TEST_ASSERT(
            kTest,
            a_entries.get(i).expect("a test reads an element at or past the count of its array")->num_inputs
                == b_entries.get(i).expect("a test reads an element at or past the count of its array")->num_inputs);
        CRUCIBLE_TEST_ASSERT(
            kTest,
            a_entries.get(i).expect("a test reads an element at or past the count of its array")->num_outputs
                == b_entries.get(i).expect("a test reads an element at or past the count of its array")->num_outputs);
        CRUCIBLE_TEST_ASSERT(
            kTest, *a_metas.get(i).expect("a test reads an element at or past the count of its array")
                       == *b_metas.get(i).expect("a test reads an element at or past the count of its array"));
        CRUCIBLE_TEST_ASSERT(
            kTest, *a_scopes.get(i).expect("a test reads an element at or past the count of its array")
                       == *b_scopes.get(i).expect("a test reads an element at or past the count of its array"));
        CRUCIBLE_TEST_ASSERT(
            kTest, *a_calls.get(i).expect("a test reads an element at or past the count of its array")
                       == *b_calls.get(i).expect("a test reads an element at or past the count of its array"));
        CRUCIBLE_TEST_ASSERT(kTest, *a_depths.get(i).expect("a test reads an element at or past the count of its array")
                                        == make_depth(i));
        CRUCIBLE_TEST_ASSERT(kTest, *b_depths.get(i).expect("a test reads an element at or past the count of its array")
                                        == make_depth(i));
    }

    ::fixy::check::pass(kTest, "  test_equivalence_with_single_pop: PASSED ({} entries)\n", N);
}

// One producer, one consumer, and a consumer that keeps changing how it
// drains.  Mixing the two drain paths against a live producer is what
// exposes an index that one path advances differently from the other.

static void test_spsc_stress() {
    ::fixy::Box<TraceRing> ring = make_ring();
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
        ::fixy::FixedArray<TraceRing::Entry, 64> out_entries{};
        ::fixy::FixedArray<MetaIndex, 64> out_metas{};
        ::fixy::FixedArray<ScopeHash, 64> out_scopes{};
        ::fixy::FixedArray<CallsiteHash, 64> out_calls{};
        ::fixy::FixedArray<OpDepth, 64> out_depths{};

        // The first batch entries of each output.
        const auto drain_batch = [&](uint32_t batch, bool is_pop_batch) -> uint32_t {
            return out_entries.with_mut([&](auto entry_run) noexcept {
                return out_metas.with_mut([&](auto meta_run) noexcept {
                    return out_scopes.with_mut([&](auto scope_run) noexcept {
                        return out_calls.with_mut([&](auto call_run) noexcept {
                            return out_depths.with_mut([&](auto depth_run) noexcept {
                                const auto entries = entry_run.view().window(0, batch).expect("the batch fits");
                                const auto metas = meta_run.view().window(0, batch).expect("the batch fits");
                                const auto scopes = scope_run.view().window(0, batch).expect("the batch fits");
                                const auto calls = call_run.view().window(0, batch).expect("the batch fits");
                                const auto depths = depth_run.view().window(0, batch).expect("the batch fits");
                                return is_pop_batch ? pop_batch(*ring, entries, metas, scopes, calls, depths)
                                                    : ring->drain(entries, metas, scopes, calls, depths);
                            });
                        });
                    });
                });
            });
        };

        uint64_t mode = 0;
        while (received < N) {
            uint32_t got = 0;
            const uint64_t m = mode++ & 0x3;
            if (m == 0) {
                got = drain_batch(64, true);
            } else if (m == 1) {
                got = drain_batch(16, true);
            } else if (m == 2) {
                got = drain_batch(32, false);
            } else {
                got = drain_batch(1, false);
            }

            if (got > 0) {
                mirror = ::fixy::with_mut(std::move(mirror), [&out_entries, received, got](auto run) noexcept {
                    for (uint32_t i = 0; i < got; ++i) {
                        *run.get(received + i).expect("the ring delivers at most N entries") =
                            out_entries.get(i)
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
                                         ::fixy::thread::ThreadOptions{}, producer, consumer)
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
