#include <crucible/MerkleDag.h>
#include <crucible/MetaLog.h>
#include <fixy/Aliases.h>
#include <fixy/Core.h>
#include <fixy/Ctx.h>
#include <fixy/check/Check.h>
#include <foundation/Platform.h>
#include <foundation/effects/Effect.h>

#include "test_assert.h"

#include <atomic>
#include <bit>
#include <cstdint>
#include <cstdio>
#include <thread>

using namespace crucible;

static TensorMeta make_meta(void* ptr, int64_t d0 = 128, int64_t d1 = 256) {
    TensorMeta m{};
    m.data_ptr = external_data_ptr(ptr);
    m.ndim = 2;
    m.sizes[0] = ::crucible::tensor_dim(d0);
    m.sizes[1] = ::crucible::tensor_dim(d1);
    m.strides[0] = ::crucible::tensor_dim(d1);
    m.strides[1] = ::crucible::tensor_dim(1);
    m.dtype = ScalarType::Float;
    m.device_type = DeviceType::CUDA;
    m.device_idx = 0;
    return m;
}

static void test_empty_state() {
    MetaLog log{::foundation::effects::testing::test().alloc};
    assert(log.size().peek() == 0);
    assert(!log.try_append(::fixy::View<const TensorMeta>{}).is_valid());
    crucible::test::pass("  test_empty:                     PASSED\n");
}

static void test_single_append_returns_index_zero() {
    MetaLog log{::foundation::effects::testing::test().alloc};
    TensorMeta m = make_meta(std::bit_cast<void*>(static_cast<std::uintptr_t>(0x1000)));
    const ::fixy::FixedArray<TensorMeta, 1> record{m};
    auto idx = log.try_append(record.view());
    assert(idx.is_valid());
    assert(idx.raw() == 0);
    assert(log.size().peek() == 1);
    const TensorMeta got = log.copy_record(0);
    assert(got.data_ptr.value() == m.data_ptr.value());
    assert(::crucible::raw_tensor_dim(got.sizes[0]) == 128);
    assert(::crucible::raw_tensor_dim(got.sizes[1]) == 256);
    crucible::test::pass("  test_single_append:             PASSED\n");
}

static void test_batch_append_and_monotonic() {
    MetaLog log{::foundation::effects::testing::test().alloc};
    ::fixy::FixedArray<TensorMeta, 8> batch{};
    for (uint32_t i = 0; i < 8; ++i) {
        crucible::test::element_at(batch, i) =
            make_meta(std::bit_cast<void*>(static_cast<std::uintptr_t>(0x1000 + i * 16)));
    }
    const ::fixy::View<const TensorMeta> records{batch.view()};
    auto idx1 = log.try_append(records.window(0, 3).expect("the first run fits the batch"));
    auto idx2 = log.try_append(records.window(3, 5).expect("the second run fits the batch"));
    assert(idx1.is_valid() && idx2.is_valid());
    assert(idx1.raw() == 0);
    assert(idx2.raw() == 3);
    assert(log.size().peek() == 8);

    // The copy of the two appends is the eight records in their order.
    ::fixy::FixedArray<TensorMeta, 8> copied{};
    log.copy_run(0, copied.view());
    for (size_t i = 0; i < 8; ++i) {
        assert(crucible::test::element_at(copied, i).data_ptr.value()
               == crucible::test::element_at(batch, i).data_ptr.value());
    }
    crucible::test::pass("  test_batch_monotonic:           PASSED\n");
}

static void test_tail_advance_frees_capacity() {
    MetaLog log{::foundation::effects::testing::test().alloc};
    TensorMeta m = make_meta(std::bit_cast<void*>(static_cast<std::uintptr_t>(0x2000)));
    const ::fixy::FixedArray<TensorMeta, 1> record{m};
    auto idx = log.try_append(record.view());
    assert(log.size().peek() == 1);
    log.advance_tail(idx.raw() + 1);
    assert(log.size().peek() == 0);
    // Freeing capacity does not rewind the index.  Indices keep
    // counting, so the next append is 1 and not 0.
    auto idx2 = log.try_append(record.view());
    assert(idx2.raw() == 1);
    crucible::test::pass("  test_tail_advance:              PASSED\n");
}

static void test_reset_zeroes_both_pointers() {
    MetaLog log{::foundation::effects::testing::test().alloc};
    TensorMeta m = make_meta(std::bit_cast<void*>(static_cast<std::uintptr_t>(0x3000)));
    const ::fixy::FixedArray<TensorMeta, 1> record{m};
    for (int i = 0; i < 10; ++i)
        (void)log.try_append(record.view());
    log.advance_tail(5);
    log.reset();
    assert(log.size().peek() == 0);
    // A reset does rewind the index, where advancing the tail does not.
    auto idx = log.try_append(record.view());
    assert(idx.raw() == 0);
    crucible::test::pass("  test_reset:                     PASSED\n");
}

static void test_copy_run_across_the_end() {
    MetaLog log{::foundation::effects::testing::test().alloc};
    // The three counters start three slots from the end of the ring.  The
    // next append then passes the end.  A fill and a drain of the ring hold a
    // second buffer of 176 MB, and the ring holds 176 MB too.  The
    // reset is correct only when no other thread uses the log, and this test
    // has one thread.
    const uint32_t near_end = MetaLog::CAPACITY - 3;
    log.head.reset_under_quiescence(near_end);
    log.tail.reset_under_quiescence(near_end);
    log.cached_tail_.reset_under_quiescence(near_end);
    assert(log.size().peek() == 0);

    // Five entries into three remaining slots: the batch straddles the
    // end of the ring.
    ::fixy::FixedArray<TensorMeta, 5> tail5{};
    for (uint32_t i = 0; i < 5; ++i) {
        crucible::test::element_at(tail5, i) =
            make_meta(std::bit_cast<void*>(static_cast<std::uintptr_t>(0x5000 + i * 0x100)));
    }
    auto idx = log.try_append(tail5.view());
    assert(idx.is_valid());
    assert(idx.raw() == near_end);

    // The copy joins the three records at the end of the buffer and the two
    // at its start, in the order of the append.
    ::fixy::FixedArray<TensorMeta, 5> copied{};
    log.copy_run(near_end, copied.view());
    for (uint32_t i = 0; i < 5; ++i) {
        assert(crucible::test::element_at(copied, i).data_ptr.value()
               == crucible::test::element_at(tail5, i).data_ptr.value());
    }

    // A run that does not pass the end is one copy.
    ::fixy::FixedArray<TensorMeta, 2> head2{};
    log.copy_run(near_end + 3, head2.view());
    assert(head2.at(::fixy::index<0>()).data_ptr.value() == tail5.at(::fixy::index<3>()).data_ptr.value());
    assert(head2.at(::fixy::index<1>()).data_ptr.value() == tail5.at(::fixy::index<4>()).data_ptr.value());
    crucible::test::pass("  test_copy_run_across_the_end:   PASSED\n");
}

// The head and the tail count each record that the log held, and the index of
// a record is its count.  Appends and releases pass 2^32 records with no wrap.
// In 32 bits the record at 2^32 - 1 takes the index of none, and the append
// after it moves the head back.
static void test_counters_pass_two_to_the_32() {
    MetaLog log{::foundation::effects::testing::test().alloc};
    // No other thread uses the log, which is the condition of the reset.
    constexpr uint32_t BELOW_WRAP = 0xFFFFFFF0u;
    log.head.reset_under_quiescence(BELOW_WRAP);
    log.tail.reset_under_quiescence(BELOW_WRAP);
    log.cached_tail_.reset_under_quiescence(BELOW_WRAP);

    ::fixy::FixedArray<TensorMeta, 32> run{};
    for (uint32_t i = 0; i < 32; ++i) {
        crucible::test::element_at(run, i) =
            make_meta(std::bit_cast<void*>(static_cast<std::uintptr_t>(0x6000 + i * 0x100)));
    }
    const ::fixy::View<const TensorMeta> records{run.view()};
    const MetaIndex first = log.try_append(records.window(0, 15).expect("the first run fits the array"));
    assert(first.is_valid() && first.raw() == uint64_t{BELOW_WRAP});
    const MetaIndex last_below = log.try_append(records.window(15, 1).expect("the record fits the array"));
    assert(last_below.is_valid() && last_below.raw() == uint64_t{BELOW_WRAP} + 15);
    const MetaIndex across = log.try_append(records.window(16, 16).expect("the last run fits the array"));
    assert(across.is_valid() && across.raw() == (uint64_t{1} << 32));
    assert(log.size().peek() == 32);

    // The records keep their order across the boundary, in the copy of the
    // run and in the copy of one record at each index.
    ::fixy::FixedArray<TensorMeta, 32> copied{};
    log.copy_run(first.raw(), copied.view());
    for (uint32_t i = 0; i < 32; ++i) {
        assert(crucible::test::element_at(copied, i).data_ptr.value()
               == crucible::test::element_at(run, i).data_ptr.value());
    }
    const TensorMeta read_below = log.copy_record(last_below.raw());
    const TensorMeta read_across = log.copy_record(across.raw());
    assert(read_below.data_ptr.value() == run.at(::fixy::index<15>()).data_ptr.value());
    assert(read_across.data_ptr.value() == run.at(::fixy::index<16>()).data_ptr.value());

    log.advance_tail(across.raw() + 16);
    assert(log.size().peek() == 0);
    assert(log.tail.get() == (uint64_t{1} << 32) + 16);
    crucible::test::pass("  test_counters_pass_2_to_the_32: PASSED\n");
}

// A run longer than the buffer holds records that the producer wrote over.
// The copy refuses such a run before it reads a slot.  The copy runs in a
// child through expect_dies of the Check family.
static void test_copy_run_refuses_a_run_longer_than_the_buffer(::fixy::TestRunnerCtx const& test) {
    MetaLog log{::foundation::effects::testing::test().alloc};
    ::fixy::HeapArray<TensorMeta> destination =
        ::fixy::mint_heap_array(::foundation::effects::testing::test().alloc, MetaLog::CAPACITY + 1, TensorMeta{});
    ::fixy::check::expect_dies(
        test, [&log, &destination] noexcept { log.copy_run(0, destination.view()); },
        ::fixy::TextView{"foundation: contract violation: dst.size() <= CAPACITY"});
    ::fixy::check::pass(test, "  test_copy_run_refuses_long_run: PASSED\n");
}

// The producer reads its own head relaxed and publishes with a release
// store.  On a total-store-order machine a missing release would still
// appear to work, so this test exists to pin the ordering: a regression
// reddens here rather than only on a weakly ordered target.
//
// The producer encodes the sequence number into data_ptr, shifted clear
// of small sentinel values, so the consumer can check order, loss and
// duplication from the payload alone.
static void test_spsc_concurrent_integrity() {
    constexpr uint32_t N = 100000;
    MetaLog log{::foundation::effects::testing::test().alloc};

    std::atomic<bool> producer_done{false};
    std::atomic<uint32_t> lost_spin{0};  // diagnostic only

    std::jthread producer{[&] {
        for (uint32_t i = 0; i < N; /* advance only on success */) {
            TensorMeta m = make_meta(std::bit_cast<void*>(static_cast<std::uintptr_t>(i + 1) << 16));
            const ::fixy::FixedArray<TensorMeta, 1> record{m};
            auto idx = log.try_append(record.view());
            if (idx.is_valid()) [[likely]] {
                ++i;
            } else {
                // The buffer is full, so back off and let the consumer
                // drain before trying the same index again.
                CRUCIBLE_SPIN_PAUSE;
                lost_spin.fetch_add(1, std::memory_order_relaxed);
            }
        }
        producer_done.store(true, std::memory_order_release);
    }};

    std::jthread consumer{[&] {
        uint32_t next = 0;  // next expected sequence number (0-based)
        while (next < N) {
            const uint32_t avail = log.size().peek();
            if (avail == 0) {
                // The size is re-read after observing the flag.  A
                // producer that appended and then set the flag between
                // the two reads would otherwise be missed.
                if (producer_done.load(std::memory_order_acquire) && log.size().peek() == 0) {
                    break;
                }
                CRUCIBLE_SPIN_PAUSE;
                continue;
            }
            for (uint32_t k = 0; k < avail; ++k) {
                const TensorMeta m = log.copy_record(next + k);
                const uintptr_t expected = static_cast<uintptr_t>(next + k + 1) << 16;
                assert(std::bit_cast<uintptr_t>(m.data_ptr.value()) == expected);
                // The remaining fields are identical in every record,
                // so they carry no sequence information.  They are
                // checked anyway: a torn read shows up here.
                assert(::crucible::raw_tensor_dim(m.sizes[0]) == 128);
                assert(::crucible::raw_tensor_dim(m.sizes[1]) == 256);
                assert(m.dtype == ScalarType::Float);
                assert(m.device_type == DeviceType::CUDA);
            }
            log.advance_tail(next + avail);
            next += avail;
        }
        assert(next == N);  // nothing was lost and nothing repeated
    }};

    producer.join();
    consumer.join();
    crucible::test::pass("  test_spsc_integrity:            PASSED "
                         "(N={}, producer_spins={})\n",
                         N, lost_spin.load());
}

// Only the accepting legs of the row-typed facade are exercised here.
// The rejecting legs are negative-compile fixtures, because a caller
// with an impure row fails to compile rather than failing at runtime.

static void test_try_append_pure() {
    namespace eff = ::fixy;

    {
        MetaLog log{::foundation::effects::testing::test().alloc};
        TensorMeta m = make_meta(std::bit_cast<void*>(static_cast<std::uintptr_t>(0xA0)));
        const ::fixy::FixedArray<TensorMeta, 1> record{m};
        auto idx = log.try_append_pure(record.view());
        assert(idx.is_valid());
        assert(idx.raw() == 0);
        assert(log.size().peek() == 1);
        const TensorMeta got = log.copy_record(idx.raw());
        assert(got.data_ptr.value() == m.data_ptr.value());
    }

    {
        MetaLog log{::foundation::effects::testing::test().alloc};
        TensorMeta m1 = make_meta(std::bit_cast<void*>(static_cast<std::uintptr_t>(0xB0)));
        TensorMeta m2 = make_meta(std::bit_cast<void*>(static_cast<std::uintptr_t>(0xC0)));
        const ::fixy::FixedArray<TensorMeta, 1> first_record{m1};
        const ::fixy::FixedArray<TensorMeta, 1> second_record{m2};
        auto idx1 = log.try_append_pure<eff::Row<>>(first_record.view());
        auto idx2 = log.try_append_pure<eff::Row<>>(second_record.view());
        assert(idx1.is_valid() && idx2.is_valid());
        assert(idx1.raw() == 0);
        assert(idx2.raw() == 1);
        assert(log.size().peek() == 2);
    }

    // Interleaved calls share one index sequence with no gap and no
    // repeat, which is what shows the facade forwards into the same
    // storage rather than keeping a second one.
    {
        MetaLog log{::foundation::effects::testing::test().alloc};
        TensorMeta m = make_meta(std::bit_cast<void*>(static_cast<std::uintptr_t>(0xD0)));
        const ::fixy::FixedArray<TensorMeta, 1> record{m};
        auto i0 = log.try_append(record.view());  // index 0
        auto i1 = log.try_append_pure(record.view());  // index 1
        auto i2 = log.try_append(record.view());  // index 2
        auto i3 = log.try_append_pure<eff::Row<>>(record.view());  // index 3
        assert(i0.raw() == 0);
        assert(i1.raw() == 1);
        assert(i2.raw() == 2);
        assert(i3.raw() == 3);
        assert(log.size().peek() == 4);
    }

    // The aliases below are what production callers name, so each is
    // pinned on the side of the purity fence it belongs on.  A change
    // that moved one across would otherwise surface only as a
    // negative-compile fixture flipping.
    static_assert(eff::IsPure<eff::Row<>>);
    static_assert(eff::IsPure<eff::PureRow>);
    static_assert(!eff::IsPure<eff::DivRow>);  // carries Block
    static_assert(!eff::IsPure<eff::Row<eff::Effect::IO>>);
    static_assert(!eff::IsPure<eff::Row<eff::Effect::Bg>>);

    crucible::test::pass("  test_try_append_pure: PASSED\n");
}

// The interleaving check above is single-threaded, which cannot see a
// hidden atomic or a weaker ordering inside the facade.  This repeats
// the concurrent scenario with the producer driven entirely through the
// row-typed entry point; the consumer side is unchanged.

static void test_try_append_pure_concurrent() {
    constexpr uint32_t N = 50000;
    MetaLog log{::foundation::effects::testing::test().alloc};

    std::atomic<bool> producer_done{false};
    std::atomic<uint32_t> lost_spin{0};

    std::jthread producer{[&] {
        for (uint32_t i = 0; i < N; /* advance only on success */) {
            TensorMeta m = make_meta(std::bit_cast<void*>(static_cast<std::uintptr_t>(i + 1) << 16));
            const ::fixy::FixedArray<TensorMeta, 1> record{m};
            auto idx = log.try_append_pure(record.view());
            if (idx.is_valid()) [[likely]] {
                ++i;
            } else {
                CRUCIBLE_SPIN_PAUSE;
                lost_spin.fetch_add(1, std::memory_order_relaxed);
            }
        }
        producer_done.store(true, std::memory_order_release);
    }};

    std::jthread consumer{[&] {
        uint32_t next = 0;
        while (next < N) {
            const uint32_t avail = log.size().peek();
            if (avail == 0) {
                if (producer_done.load(std::memory_order_acquire) && log.size().peek() == 0) {
                    break;
                }
                CRUCIBLE_SPIN_PAUSE;
                continue;
            }
            for (uint32_t k = 0; k < avail; ++k) {
                const TensorMeta m = log.copy_record(next + k);
                const uintptr_t expected = static_cast<uintptr_t>(next + k + 1) << 16;
                assert(std::bit_cast<uintptr_t>(m.data_ptr.value()) == expected);
                assert(::crucible::raw_tensor_dim(m.sizes[0]) == 128);
                assert(::crucible::raw_tensor_dim(m.sizes[1]) == 256);
                assert(m.dtype == ScalarType::Float);
                assert(m.device_type == DeviceType::CUDA);
            }
            log.advance_tail(next + avail);
            next += avail;
        }
        assert(next == N);
    }};

    producer.join();
    consumer.join();
    crucible::test::pass("  test_try_append_pure_concurrent: "
                         "PASSED (N={}, producer_spins={})\n",
                         N, lost_spin.load());
}

int main() {
    ::fixy::TestRunnerCtx const test{::foundation::effects::testing::test()};
    // First, while the test process has one thread: the refused copy runs in
    // a child process.
    test_copy_run_refuses_a_run_longer_than_the_buffer(test);
    test_empty_state();
    test_single_append_returns_index_zero();
    test_batch_append_and_monotonic();
    test_tail_advance_frees_capacity();
    test_reset_zeroes_both_pointers();
    test_copy_run_across_the_end();
    test_counters_pass_two_to_the_32();
    test_spsc_concurrent_integrity();
    test_try_append_pure();
    test_try_append_pure_concurrent();
    ::fixy::check::pass(test, "test_meta_log: 11 groups, all passed\n");
    return 0;
}
