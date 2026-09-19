#include <crucible/MerkleDag.h>
#include <crucible/MetaLog.h>
#include <crucible/Platform.h>
#include <crucible/effects/_EffectRow.h>
#include <crucible/effects/FxAliases.h>

#include "test_assert.h"

#include <atomic>
#include <bit>
#include <cstdint>
#include <cstdio>
#include <thread>
#include <vector>

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
    MetaLog log;
    assert(log.size().peek() == 0);
    assert(!log.try_append(nullptr, 0).is_valid());
    std::printf("  test_empty:                     PASSED\n");
}

static void test_single_append_returns_index_zero() {
    MetaLog log;
    TensorMeta m = make_meta(std::bit_cast<void*>(static_cast<std::uintptr_t>(0x1000)));
    auto idx = log.try_append(&m, 1);
    assert(idx.is_valid());
    assert(idx.raw() == 0);
    assert(log.size().peek() == 1);
    const auto& got = log.at(0);
    assert(raw_data_ptr(got) == raw_data_ptr(m));
    assert(::crucible::raw_tensor_dim(got.sizes[0]) == 128);
    assert(::crucible::raw_tensor_dim(got.sizes[1]) == 256);
    std::printf("  test_single_append:             PASSED\n");
}

static void test_batch_append_and_monotonic() {
    MetaLog log;
    std::vector<TensorMeta> batch;
    for (int i = 0; i < 8; ++i) {
        batch.push_back(make_meta(std::bit_cast<void*>(static_cast<std::uintptr_t>(0x1000 + i * 16))));
    }
    auto idx1 = log.try_append(batch.data(), 3);
    auto idx2 = log.try_append(batch.data() + 3, 5);
    assert(idx1.is_valid() && idx2.is_valid());
    assert(idx1.raw() == 0);
    assert(idx2.raw() == 3);
    assert(log.size().peek() == 8);

    // A fresh buffer has not wrapped, so the whole range is contiguous.
    const TensorMeta* span = log.try_contiguous(0, 8);
    assert(span != nullptr);
    for (int i = 0; i < 8; ++i) {
        assert(raw_data_ptr(span[i]) == raw_data_ptr(batch[static_cast<size_t>(i)]));
    }
    std::printf("  test_batch_monotonic:           PASSED\n");
}

static void test_tail_advance_frees_capacity() {
    MetaLog log;
    TensorMeta m = make_meta(std::bit_cast<void*>(static_cast<std::uintptr_t>(0x2000)));
    auto idx = log.try_append(&m, 1);
    assert(log.size().peek() == 1);
    log.advance_tail(idx.raw() + 1);
    assert(log.size().peek() == 0);
    // Freeing capacity does not rewind the index.  Indices keep
    // counting, so the next append is 1 and not 0.
    auto idx2 = log.try_append(&m, 1);
    assert(idx2.raw() == 1);
    std::printf("  test_tail_advance:              PASSED\n");
}

static void test_reset_zeroes_both_pointers() {
    MetaLog log;
    TensorMeta m = make_meta(std::bit_cast<void*>(static_cast<std::uintptr_t>(0x3000)));
    for (int i = 0; i < 10; ++i)
        (void)log.try_append(&m, 1);
    log.advance_tail(5);
    log.reset();
    assert(log.size().peek() == 0);
    // A reset does rewind the index, where advancing the tail does not.
    auto idx = log.try_append(&m, 1);
    assert(idx.raw() == 0);
    std::printf("  test_reset:                     PASSED\n");
}

static void test_try_contiguous_wrap_returns_null() {
    MetaLog log;
    // Filling and then draining leaves the head three slots from the
    // end of the ring, which is the only way to reach the wrap without
    // holding a full buffer of live entries.
    const uint32_t near_end = MetaLog::CAPACITY - 3;
    TensorMeta m = make_meta(std::bit_cast<void*>(static_cast<std::uintptr_t>(0x4000)));
    std::vector<TensorMeta> junk(near_end, m);
    (void)log.try_append(junk.data(), near_end);
    log.advance_tail(near_end);
    assert(log.size().peek() == 0);

    // Five entries into three remaining slots: the batch straddles the
    // end of the ring.
    std::vector<TensorMeta> tail5(5, m);
    auto idx = log.try_append(tail5.data(), 5);
    assert(idx.is_valid());
    assert(idx.raw() == near_end);

    // A straddling range has no single pointer, so the caller has to
    // copy instead.
    const TensorMeta* span = log.try_contiguous(near_end, 5);
    assert(span == nullptr);

    const TensorMeta* span2 = log.try_contiguous(0, 2);
    assert(span2 != nullptr);
    std::printf("  test_wrap:                      PASSED\n");
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
    MetaLog log;

    std::atomic<bool> producer_done{false};
    std::atomic<uint32_t> lost_spin{0};  // diagnostic only

    std::jthread producer{[&] {
        for (uint32_t i = 0; i < N; /* advance only on success */) {
            TensorMeta m = make_meta(std::bit_cast<void*>(static_cast<std::uintptr_t>(i + 1) << 16));
            auto idx = log.try_append(&m, 1);
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
                const TensorMeta& m = log.at(next + k);
                const uintptr_t expected = static_cast<uintptr_t>(next + k + 1) << 16;
                assert(std::bit_cast<uintptr_t>(raw_data_ptr(m)) == expected);
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
    std::printf("  test_spsc_integrity:            PASSED "
                "(N=%u, producer_spins=%u)\n",
                N, lost_spin.load());
}

// Only the accepting legs of the row-typed facade are exercised here.
// The rejecting legs are negative-compile fixtures, because a caller
// with an impure row fails to compile rather than failing at runtime.

static void test_try_append_pure_FOUND_I17() {
    namespace eff = crucible::effects;

    {
        MetaLog log;
        TensorMeta m = make_meta(std::bit_cast<void*>(static_cast<std::uintptr_t>(0xA0)));
        auto idx = log.try_append_pure(&m, 1);
        assert(idx.is_valid());
        assert(idx.raw() == 0);
        assert(log.size().peek() == 1);
        const auto& got = log.at(idx);
        assert(raw_data_ptr(got) == raw_data_ptr(m));
    }

    {
        MetaLog log;
        TensorMeta m1 = make_meta(std::bit_cast<void*>(static_cast<std::uintptr_t>(0xB0)));
        TensorMeta m2 = make_meta(std::bit_cast<void*>(static_cast<std::uintptr_t>(0xC0)));
        auto idx1 = log.try_append_pure<eff::Row<>>(&m1, 1);
        auto idx2 = log.try_append_pure<eff::Row<>>(&m2, 1);
        assert(idx1.is_valid() && idx2.is_valid());
        assert(idx1.raw() == 0);
        assert(idx2.raw() == 1);
        assert(log.size().peek() == 2);
    }

    // Interleaved calls share one index sequence with no gap and no
    // repeat, which is what shows the facade forwards into the same
    // storage rather than keeping a second one.
    {
        MetaLog log;
        TensorMeta m = make_meta(std::bit_cast<void*>(static_cast<std::uintptr_t>(0xD0)));
        auto i0 = log.try_append(&m, 1);  // index 0
        auto i1 = log.try_append_pure(&m, 1);  // index 1
        auto i2 = log.try_append(&m, 1);  // index 2
        auto i3 = log.try_append_pure<eff::Row<>>(&m, 1);  // index 3
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
    static_assert(eff::IsPure<eff::TotRow>);  // a synonym of the pure row
    static_assert(eff::IsPure<eff::GhostRow>);  // a synonym of the pure row
    static_assert(!eff::IsPure<eff::DivRow>);  // carries Block
    static_assert(!eff::IsPure<eff::Row<eff::Effect::IO>>);
    static_assert(!eff::IsPure<eff::Row<eff::Effect::Bg>>);

    std::printf("  test_try_append_pure_FOUND_I17: PASSED\n");
}

// The interleaving check above is single-threaded, which cannot see a
// hidden atomic or a weaker ordering inside the facade.  This repeats
// the concurrent scenario with the producer driven entirely through the
// row-typed entry point; the consumer side is unchanged.

static void test_try_append_pure_concurrent_FOUND_I17_AUDIT() {
    constexpr uint32_t N = 50000;
    MetaLog log;

    std::atomic<bool> producer_done{false};
    std::atomic<uint32_t> lost_spin{0};

    std::jthread producer{[&] {
        for (uint32_t i = 0; i < N; /* advance only on success */) {
            TensorMeta m = make_meta(std::bit_cast<void*>(static_cast<std::uintptr_t>(i + 1) << 16));
            auto idx = log.try_append_pure(&m, 1);
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
                const TensorMeta& m = log.at(next + k);
                const uintptr_t expected = static_cast<uintptr_t>(next + k + 1) << 16;
                assert(std::bit_cast<uintptr_t>(raw_data_ptr(m)) == expected);
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
    std::printf("  test_try_append_pure_concurrent_FOUND_I17_AUDIT: "
                "PASSED (N=%u, producer_spins=%u)\n",
                N, lost_spin.load());
}

int main() {
    test_empty_state();
    test_single_append_returns_index_zero();
    test_batch_append_and_monotonic();
    test_tail_advance_frees_capacity();
    test_reset_zeroes_both_pointers();
    test_try_contiguous_wrap_returns_null();
    test_spsc_concurrent_integrity();
    test_try_append_pure_FOUND_I17();
    test_try_append_pure_concurrent_FOUND_I17_AUDIT();
    std::printf("test_meta_log: 9 groups, all passed\n");
    return 0;
}
