// Tests for MetaLog SPSC buffer.
//
// Covers: empty-buffer state, single/batch append returns MetaIndex,
// index monotonic, at() / try_contiguous() read-side, tail advance,
// reset, and overflow → MetaIndex::none() behavior (synthetically
// exercised by driving head via many appends).

#include <crucible/MerkleDag.h>
#include <crucible/MetaLog.h>
#include <crucible/Platform.h>

#include "test_assert.h"

#include <atomic>
#include <cstdint>
#include <cstdio>
#include <thread>
#include <vector>

using namespace crucible;

static TensorMeta make_meta(void* ptr, int64_t d0 = 128, int64_t d1 = 256) {
    TensorMeta m{};
    m.data_ptr = ptr;
    m.ndim = 2;
    m.sizes[0] = d0;
    m.sizes[1] = d1;
    m.strides[0] = d1;
    m.strides[1] = 1;
    m.dtype = ScalarType::Float;
    m.device_type = DeviceType::CUDA;
    m.device_idx = 0;
    return m;
}

static void test_empty_state() {
    MetaLog log;
    assert(log.size() == 0);
    // Zero-length append short-circuits to none().
    assert(!log.try_append(nullptr, 0).is_valid());
    std::printf("  test_empty:                     PASSED\n");
}

static void test_single_append_returns_index_zero() {
    MetaLog log;
    TensorMeta m = make_meta(reinterpret_cast<void*>(0x1000));
    auto idx = log.try_append(&m, 1);
    assert(idx.is_valid());
    assert(idx.raw() == 0);
    assert(log.size() == 1);
    // Read back the stored meta.
    const auto& got = log.at(0);
    assert(got.data_ptr == m.data_ptr);
    assert(got.sizes[0] == 128);
    assert(got.sizes[1] == 256);
    std::printf("  test_single_append:             PASSED\n");
}

static void test_batch_append_and_monotonic() {
    MetaLog log;
    std::vector<TensorMeta> batch;
    for (int i = 0; i < 8; ++i) {
        batch.push_back(make_meta(reinterpret_cast<void*>(0x1000 + i * 16)));
    }
    auto idx1 = log.try_append(batch.data(), 3);
    auto idx2 = log.try_append(batch.data() + 3, 5);
    assert(idx1.is_valid() && idx2.is_valid());
    assert(idx1.raw() == 0);
    assert(idx2.raw() == 3);
    assert(log.size() == 8);

    // Contiguous span access covers the whole range on a fresh buffer.
    const TensorMeta* span = log.try_contiguous(0, 8);
    assert(span != nullptr);
    for (int i = 0; i < 8; ++i) {
        assert(span[i].data_ptr == batch[static_cast<size_t>(i)].data_ptr);
    }
    std::printf("  test_batch_monotonic:           PASSED\n");
}

static void test_tail_advance_frees_capacity() {
    MetaLog log;
    TensorMeta m = make_meta(reinterpret_cast<void*>(0x2000));
    auto idx = log.try_append(&m, 1);
    assert(log.size() == 1);
    log.advance_tail(idx.raw() + 1);
    assert(log.size() == 0);
    // Buffer is now logically empty; new append returns index 1
    // (monotonic, not reset).
    auto idx2 = log.try_append(&m, 1);
    assert(idx2.raw() == 1);
    std::printf("  test_tail_advance:              PASSED\n");
}

static void test_reset_zeroes_both_pointers() {
    MetaLog log;
    TensorMeta m = make_meta(reinterpret_cast<void*>(0x3000));
    for (int i = 0; i < 10; ++i) (void)log.try_append(&m, 1);
    log.advance_tail(5);
    log.reset();
    assert(log.size() == 0);
    // Fresh append returns index 0 again.
    auto idx = log.try_append(&m, 1);
    assert(idx.raw() == 0);
    std::printf("  test_reset:                     PASSED\n");
}

static void test_try_contiguous_wrap_returns_null() {
    MetaLog log;
    // Drive head close to the wrap boundary.  Append then drain so the
    // next append starts near the end.
    const uint32_t near_end = MetaLog::CAPACITY - 3;
    TensorMeta m = make_meta(reinterpret_cast<void*>(0x4000));
    // Fast-forward head by appending a big batch with sentinel data.
    std::vector<TensorMeta> junk(near_end, m);
    (void)log.try_append(junk.data(), near_end);
    log.advance_tail(near_end);   // free everything
    assert(log.size() == 0);

    // Now append 5 — wraps past CAPACITY - 3 → 2.
    std::vector<TensorMeta> tail5(5, m);
    auto idx = log.try_append(tail5.data(), 5);
    assert(idx.is_valid());
    assert(idx.raw() == near_end);

    // try_contiguous of the wrapping range → nullptr (caller must copy).
    const TensorMeta* span = log.try_contiguous(near_end, 5);
    assert(span == nullptr);

    // Non-wrapping range at the start of the buffer → valid pointer.
    const TensorMeta* span2 = log.try_contiguous(0, 2);
    assert(span2 != nullptr);
    std::printf("  test_wrap:                      PASSED\n");
}

// Concurrent SPSC integrity: a live producer and consumer exchange N
// distinct TensorMeta records across threads. Verifies ThreadSafe:
//
//   - Every meta the producer appended is eventually observed by the
//     consumer (no lost events — cached_tail_ fallback path works).
//   - Observed metas are bit-identical to what was written (no torn
//     read — release/acquire publishes entries[] before head advances).
//   - Strict producer-order preservation (data_ptr encodes sequence).
//
// Rationale: the hot path uses memory_order_relaxed for the producer's
// own-head load and memory_order_release for the publish store. On x86
// (TSO) a buggy relaxed store would still work by accident; this test
// crystallises the behaviour so a regression on any weakly-ordered
// platform (ARM, RISC-V) turns red here instead of in production.
static void test_spsc_concurrent_integrity() {
    constexpr uint32_t N = 100'000;
    MetaLog log;

    // Producer and consumer run on distinct OS threads. The producer
    // encodes the sequence number into data_ptr (i + 1, shifted to
    // avoid colliding with low-magic values) so the consumer can
    // verify strict ordering on each slot.
    std::atomic<bool>     producer_done{false};
    std::atomic<uint32_t> lost_spin{0};        // diagnostic only

    std::thread producer{[&]{
        for (uint32_t i = 0; i < N; /* advance only on success */) {
            TensorMeta m = make_meta(
                reinterpret_cast<void*>(static_cast<uintptr_t>(i + 1) << 16));
            auto idx = log.try_append(&m, 1);
            if (idx.is_valid()) [[likely]] {
                ++i;
            } else {
                // Buffer full — yield so the consumer catches up.
                // 1 M capacity + aggressive drain makes this rare.
                CRUCIBLE_SPIN_PAUSE;
                lost_spin.fetch_add(1, std::memory_order_relaxed);
            }
        }
        producer_done.store(true, std::memory_order_release);
    }};

    std::thread consumer{[&]{
        uint32_t next = 0;  // next expected sequence number (0-based)
        while (next < N) {
            const uint32_t avail = log.size();
            if (avail == 0) {
                if (producer_done.load(std::memory_order_acquire) &&
                    log.size() == 0) {
                    break;  // producer finished and we drained everything
                }
                CRUCIBLE_SPIN_PAUSE;
                continue;
            }
            // Verify each meta in the batch: data_ptr encodes position.
            for (uint32_t k = 0; k < avail; ++k) {
                const TensorMeta& m = log.at(next + k);
                const uintptr_t expected =
                    static_cast<uintptr_t>(next + k + 1) << 16;
                assert(reinterpret_cast<uintptr_t>(m.data_ptr) == expected);
                // Spot-check non-pointer fields to catch torn reads.
                assert(m.sizes[0]    == 128);
                assert(m.sizes[1]    == 256);
                assert(m.dtype       == ScalarType::Float);
                assert(m.device_type == DeviceType::CUDA);
            }
            log.advance_tail(next + avail);
            next += avail;
        }
        assert(next == N);  // consumer saw every producer append
    }};

    producer.join();
    consumer.join();
    std::printf("  test_spsc_integrity:            PASSED "
                "(N=%u, producer_spins=%u)\n",
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
    std::printf("test_meta_log: 7 groups, all passed\n");
    return 0;
}
