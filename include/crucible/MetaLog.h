#pragma once

#include <atomic>
#include <cstdint>
#include <cstdlib>

#include <crucible/Platform.h>
#include <crucible/MerkleDag.h>

namespace crucible {

// Parallel SPSC buffer for tensor metadata.
//
// The TraceRing stores 64B fingerprints per op (~5ns hot path). Full tensor
// metadata (sizes, strides, dtype, device, data_ptr) is too large to inline.
// MetaLog holds this "fat" metadata in a separate SPSC buffer; each ring
// entry's corresponding meta_starts[] slot stores the index into here.
//
// Same SPSC protocol as TraceRing:
//   - Foreground thread writes at head
//   - Background thread reads and advances tail
//   - Overflow → meta_start = UINT32_MAX (entry still works for
//     iteration detection; background skips DAG building for that op)
struct MetaLog {
  static constexpr uint32_t CAPACITY = 1 << 20; // 1M entries (~144MB)
  static constexpr uint32_t MASK = CAPACITY - 1;

  // Producer state (foreground writes, consumer reads).
  alignas(64) std::atomic<uint32_t> head{0};

  // Consumer state (consumer reads/writes, producer reads for fullness).
  alignas(64) std::atomic<uint32_t> tail{0};

  TensorMeta* entries;

  MetaLog()
      : entries(static_cast<TensorMeta*>(
            std::malloc(CAPACITY * sizeof(TensorMeta)))) {
    if (!entries) [[unlikely]] std::abort(); // 144MB alloc failed — unrecoverable
  }

  ~MetaLog() { std::free(entries); }

  MetaLog(const MetaLog&) = delete("SPSC buffer is pinned to producer/consumer thread pair");
  MetaLog& operator=(const MetaLog&) = delete("SPSC buffer is pinned to producer/consumer thread pair");

  // ── Producer (foreground): append n consecutive TensorMetas ──
  //
  // Returns the start index, or UINT32_MAX if the buffer is full.
  [[nodiscard]] CRUCIBLE_INLINE uint32_t try_append(const TensorMeta* metas, uint32_t n) {
    if (n == 0) [[unlikely]] return UINT32_MAX;
    uint32_t h = head.load(std::memory_order_relaxed);
    uint32_t t = tail.load(std::memory_order_relaxed);
    if (h - t + n > CAPACITY) [[unlikely]] {
      return UINT32_MAX;
    }
    for (uint32_t i = 0; i < n; i++) {
      entries[(h + i) & MASK] = metas[i];
    }
    head.store(h + n, std::memory_order_release);
    return h;
  }

  // ── Consumer (background): read meta at absolute index ──
  [[nodiscard]] const TensorMeta& at(uint32_t idx) const {
    return entries[idx & MASK];
  }

  // ── Consumer (background): advance tail past consumed entries ──
  void advance_tail(uint32_t new_tail) {
    tail.store(new_tail, std::memory_order_relaxed);
  }

  [[nodiscard]] uint32_t size() const {
    return head.load(std::memory_order_relaxed) -
           tail.load(std::memory_order_relaxed);
  }

  void reset() {
    head.store(0, std::memory_order_relaxed);
    tail.store(0, std::memory_order_relaxed);
  }
};

} // namespace crucible
