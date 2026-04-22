#pragma once

// ═══════════════════════════════════════════════════════════════════
// TensorMeta — extracted from MerkleDag.h to break the include cycle
// with DimHash.h.  TensorMeta is referenced by both MerkleDag (the
// content-hash machinery + RegionNode body) and DimHash (the SIMD
// dim-hash helper), and DimHash is itself called from inside
// MerkleDag::compute_content_hash.  Putting TensorMeta in its own
// leaf header lets both consumers include it without circular deps.
//
// Sizes and strides inlined for up to 8 dimensions, covering 99.9%
// of real tensors.  Arena-allocated when > 8 dims needed (via
// indirection at a higher level, not inside this struct).
// ═══════════════════════════════════════════════════════════════════

#include <crucible/Platform.h>
#include <crucible/Types.h>

#include <cstdint>

namespace crucible {

struct TensorMeta {
  int64_t sizes[8]{};        // 64B — zero-init prevents hash instability
  int64_t strides[8]{};      // 64B
  void* data_ptr = nullptr;  // 8B — tensor data pointer (for dataflow tracking)
  uint8_t ndim = 0;          // 1B — dimensions used (0-8)
  ScalarType dtype = ScalarType::Undefined; // 1B
  DeviceType device_type = DeviceType::CPU; // 1B
  int8_t device_idx = -1;   // 1B — -1 = CPU, 0+ = device index
  Layout layout = Layout::Strided; // 1B
  bool requires_grad = false; // 1B — tensor.requires_grad (forward/backward discriminator)

  // Packed tensor flags (1B). Bit layout:
  //   bit 0: is_leaf       — parameter or user-created tensor (no grad_fn)
  //   bit 1: is_contiguous — contiguous memory layout
  //   bit 2: has_grad_fn   — has autograd history (not a leaf)
  //   bit 3: is_view       — shares storage with another tensor
  //   bit 4: is_neg        — negation bit-view (torch._neg_view)
  //   bit 5: is_conj       — conjugate bit-view
  //   bit 6-7: reserved
  uint8_t flags = 0;          // 1B — packed tensor flags (see meta_flags)

  uint8_t output_nr = 0;     // 1B — autograd output number (multi-output ops)

  // ── Extended fields (24B) ─────────────────────────────────────────
  int64_t storage_offset = 0; // 8B — offset into underlying storage (view chains)
  uint32_t version = 0;      // 4B — tensor data version counter (in-place mutation detection)
  uint32_t storage_nbytes = 0; // 4B — actual storage size in bytes (may differ from view)
  uint64_t grad_fn_hash = 0;  // 8B — FNV-1a hash of grad_fn class name (e.g. "AddmmBackward0")
                               //      0 = no grad_fn (leaf tensor or no autograd)
};

static_assert(sizeof(TensorMeta) == 168, "TensorMeta layout check");
CRUCIBLE_ASSERT_TRIVIALLY_RELOCATABLE_STRICT(TensorMeta);

}  // namespace crucible
