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
#include <crucible/safety/Refined.h>
#include <crucible/safety/Tagged.h>

#include <cstdint>

namespace crucible {

// ── Maximum tensor dimensionality (#534 PROD-WRAP-5) ───────────────
//
// The TensorMeta layout inlines sizes[8] and strides[8].  Any tensor
// with ndim > 8 cannot be expressed without out-of-bounds reads on
// the inline arrays — the structural bound is 8 per definition,
// independent of any runtime configuration.  `kMaxTensorNDim` names
// the bound at the type level so call sites can reference it without
// re-deriving the constant from the array sizeof.
inline constexpr uint8_t kMaxTensorNDim = 8;

struct TensorMeta {
  int64_t sizes[8]{};        // 64B — zero-init prevents hash instability
  int64_t strides[8]{};      // 64B
  void* data_ptr = nullptr;  // 8B — tensor data pointer (for dataflow tracking)
  uint8_t ndim = 0;          // 1B — dimensions used (0..kMaxTensorNDim)
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

// WRAP-StorageNbytes-5 (#1022): storage-span computation is an
// adversarial-defense boundary over TensorMeta values read from
// Vessel / traces / disk.  Callers must now explicitly mark the input
// as source::External before compute_storage_nbytes* consumes it.
// Reference payload keeps the 168-byte TensorMeta layout untouched.
using ExternalTensorMeta = ::crucible::safety::Tagged<
    const TensorMeta&, ::crucible::safety::source::External>;

[[nodiscard]] inline constexpr ExternalTensorMeta
external_tensor_meta(const TensorMeta& meta) noexcept {
  return ExternalTensorMeta{meta};
}

// ── Validated ndim carrier (#534 PROD-WRAP-5) ──────────────────────
//
// `TensorMeta::ndim` is structurally bounded by 8 (the inline
// sizes[]/strides[] array length).  Reading a uint8_t from disk / FFI
// / external trace can deliver ANY byte in [0, 255]; values in [9,
// 255] are corrupt or adversarial and would, if propagated, produce:
//
//   * out-of-bounds reads on `sizes[d]` and `strides[d]` for d up to
//     ndim (the inline arrays only hold 8 slots);
//   * silent contract violation in compute_storage_nbytes (which
//     carries `pre(meta.ndim <= kMaxTensorNDim)` and would terminate
//     under semantic=enforce, or invoke [[assume]] UB under
//     semantic=ignore on hot-path TUs);
//   * incorrect SIMD strides in DimHash (which reads sizes[ndim]
//     indices speculatively to fold the dim hash).
//
// `ValidNDim` is the type-level witness that ndim has been validated
// at the boundary it crossed.  Construction is gated by Refined<>'s
// `pre(bounded_above<8>(v))` clause; under semantic=enforce the
// runtime path is contract violation -> handle_contract_violation
// (logged + std::abort), under semantic=ignore the optimizer treats
// the bound as `[[assume]]` and downstream loops vectorize on the
// trip-count knowledge.
//
// `make_ndim` is a [[gnu::const]] factory that lifts the witness back
// to a bare uint8_t for storage in the TensorMeta::ndim field —
// preserving the 168-byte layout lock + trivially-relocatable
// classification while routing every external write through the
// ValidNDim ctor.
//
// Defense-in-depth: TraceLoader's existing `if (metas[i].ndim > 8)
// [[unlikely]] return nullptr` runtime guard at line 321 is retained.
// ValidNDim catches the byte at deserialize entry; the loader guard
// catches it at iteration entry.  Both layers must reject for the
// type-level bound and the runtime path to disagree.
//
// Cost: regime-1 EBO collapse — sizeof(ValidNDim) == sizeof(uint8_t).
using ValidNDim = ::crucible::safety::Refined<
    ::crucible::safety::bounded_above<kMaxTensorNDim>, uint8_t>;

[[nodiscard, gnu::const]] inline constexpr
uint8_t make_ndim(ValidNDim raw) noexcept {
    return raw.value();
}

}  // namespace crucible
