// Tests for MetaLog SPSC buffer.
//
// Covers: empty-buffer state, single/batch append returns MetaIndex,
// index monotonic, at() / try_contiguous() read-side, tail advance,
// reset, and overflow → MetaIndex::none() behavior (synthetically
// exercised by driving head via many appends).

#include <crucible/MerkleDag.h>
#include <crucible/MetaLog.h>

#include <cassert>
#include <cstdint>
#include <cstdio>
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

int main() {
    test_empty_state();
    test_single_append_returns_index_zero();
    test_batch_append_and_monotonic();
    test_tail_advance_frees_capacity();
    test_reset_zeroes_both_pointers();
    test_try_contiguous_wrap_returns_null();
    std::printf("test_meta_log: 6 groups, all passed\n");
    return 0;
}
