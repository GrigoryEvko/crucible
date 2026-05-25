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
#include <crucible/fixy/Source.h>   // FIXY-U-096x: tags::source::External
#include <crucible/fixy/Wrap.h>     // FIXY-U-096x: Tagged / Refined / bounded_above

#include <cstddef>
#include <cstdint>
#include <limits>
#include <type_traits>

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

// WRAP-TensorMeta-2 (#1035): Tensor storage addresses enter Crucible
// from PyTorch / trace / disk boundaries.  The runtime only hashes and
// compares them as opaque external cookies until a later validator
// explicitly retags them.
using ExternalDataPtr = ::crucible::fixy::wrap::Tagged<
    void*, ::crucible::fixy::tags::source::External>;

static_assert(sizeof(ExternalDataPtr) == sizeof(void*),
    "Tagged<void*, source::External> must EBO-collapse so TensorMeta "
    "stays layout-stable");
static_assert(std::is_trivially_copyable_v<ExternalDataPtr>);
static_assert(std::is_standard_layout_v<ExternalDataPtr>);

// WRAP-TensorMeta-7 (#1040): PyTorch grad_fn identity is process-local.
// The value may depend on autograd object identity and must never be
// treated as a persistent Family-A key.  Tagged keeps the 8-byte layout
// but forces every consumer to acknowledge the Family-B lane.
using GradFnHash = ::crucible::fixy::wrap::Tagged<
    uint64_t, ::crucible::hash_family::FamilyB>;

static_assert(sizeof(GradFnHash) == sizeof(uint64_t),
    "Tagged<uint64_t, hash_family::FamilyB> must EBO-collapse so "
    "TensorMeta stays layout-stable");
static_assert(std::is_trivially_copyable_v<GradFnHash>);
static_assert(std::is_standard_layout_v<GradFnHash>);

// WRAP-TensorMeta-1 (#1034): sizes/strides are bounded by the largest
// dtype byte width before storage-span arithmetic consumes them.  The
// raw storage remains int64_t[8] so DimHash/StorageNbytes can keep
// zero-copy SIMD loads; writes go through TensorDim.
inline constexpr int64_t kTensorDimElementByteBudget = 16;
inline constexpr int64_t kMaxTensorDimExtent =
    std::numeric_limits<int64_t>::max() / kTensorDimElementByteBudget;

using TensorDim = ::crucible::fixy::wrap::Refined<
    ::crucible::fixy::wrap::bounded_above<kMaxTensorDimExtent>, int64_t>;

static_assert(sizeof(TensorDim) == sizeof(int64_t),
    "Refined<bounded_above<kMaxTensorDimExtent>, int64_t> must "
    "EBO-collapse so TensorMeta stays layout-stable");
static_assert(std::is_trivially_copyable_v<TensorDim>);
static_assert(std::is_standard_layout_v<TensorDim>);

struct TensorDimArray {
private:
  int64_t lanes_[kMaxTensorNDim]{};

public:
  struct Slot {
    int64_t* lane = nullptr;

    constexpr void operator=(TensorDim dim) const noexcept {
      *lane = dim.value();
    }

    [[nodiscard]] constexpr int64_t value() const noexcept {
      return *lane;
    }

    [[nodiscard]] constexpr operator TensorDim() const noexcept {
      return TensorDim{*lane, TensorDim::Trusted{}};
    }
  };

  struct ConstSlot {
    const int64_t* lane = nullptr;

    [[nodiscard]] constexpr int64_t value() const noexcept {
      return *lane;
    }

    [[nodiscard]] constexpr operator TensorDim() const noexcept {
      return TensorDim{*lane, TensorDim::Trusted{}};
    }
  };

  [[nodiscard]] constexpr Slot operator[](std::size_t index) noexcept {
    return Slot{&lanes_[index]};
  }

  [[nodiscard]] constexpr ConstSlot operator[](std::size_t index) const noexcept {
    return ConstSlot{&lanes_[index]};
  }

  [[nodiscard]] constexpr int64_t* raw_data() noexcept {
    return lanes_;
  }

  [[nodiscard]] constexpr const int64_t* raw_data() const noexcept {
    return lanes_;
  }
};

static_assert(sizeof(TensorDimArray) == sizeof(int64_t) * kMaxTensorNDim,
    "TensorDimArray must remain the same 64-byte lane block as int64_t[8]");
static_assert(std::is_trivially_copyable_v<TensorDimArray>);
static_assert(std::is_standard_layout_v<TensorDimArray>);

[[nodiscard]] inline constexpr TensorDim
tensor_dim(int64_t value) noexcept {
  return TensorDim{value};
}

[[nodiscard]] inline constexpr int64_t
raw_tensor_dim(TensorDim dim) noexcept {
  return dim.value();
}

[[nodiscard]] inline constexpr int64_t
raw_tensor_dim(TensorDimArray::Slot dim) noexcept {
  return dim.value();
}

[[nodiscard]] inline constexpr int64_t
raw_tensor_dim(TensorDimArray::ConstSlot dim) noexcept {
  return dim.value();
}

struct TensorMeta {
  TensorDimArray sizes{};     // 64B — zero-init prevents hash instability
  TensorDimArray strides{};   // 64B
  ExternalDataPtr data_ptr{nullptr}; // 8B — external tensor pointer cookie
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
  GradFnHash grad_fn_hash{0}; // 8B — Family-B FNV-1a grad_fn class-name hash
                              //      0 = no grad_fn (leaf tensor or no autograd)
};

static_assert(sizeof(TensorMeta) == 168, "TensorMeta layout check");
CRUCIBLE_ASSERT_TRIVIALLY_RELOCATABLE_STRICT(TensorMeta);

[[nodiscard]] inline constexpr ExternalDataPtr
external_data_ptr(void* ptr) noexcept {
  return ExternalDataPtr{ptr};
}

[[nodiscard]] inline constexpr void*
raw_data_ptr(ExternalDataPtr ptr) noexcept {
  return ptr.value();
}

[[nodiscard]] inline constexpr void*
raw_data_ptr(const TensorMeta& meta) noexcept {
  return raw_data_ptr(meta.data_ptr);
}

[[nodiscard]] inline constexpr GradFnHash
grad_fn_hash(uint64_t hash) noexcept {
  return GradFnHash{hash};
}

[[nodiscard]] inline constexpr uint64_t
raw_grad_fn_hash(GradFnHash hash) noexcept {
  return hash.value();
}

[[nodiscard]] inline constexpr uint64_t
raw_grad_fn_hash(const TensorMeta& meta) noexcept {
  return raw_grad_fn_hash(meta.grad_fn_hash);
}

// WRAP-StorageNbytes-5 (#1022): storage-span computation is an
// adversarial-defense boundary over TensorMeta values read from
// Vessel / traces / disk.  Callers must now explicitly mark the input
// as source::External before compute_storage_nbytes* consumes it.
// Reference payload keeps the 168-byte TensorMeta layout untouched.
using ExternalTensorMeta = ::crucible::fixy::wrap::Tagged<
    const TensorMeta&, ::crucible::fixy::tags::source::External>;

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
using ValidNDim = ::crucible::fixy::wrap::Refined<
    ::crucible::fixy::wrap::bounded_above<kMaxTensorNDim>, uint8_t>;

[[nodiscard, gnu::const]] inline constexpr
uint8_t make_ndim(ValidNDim raw) noexcept {
    return raw.value();
}

// ── read_meta dtype gate (sibling of #534 ndim / #892 kernel_id) ──────
//
// read_meta() reconstructs `m.dtype = r.r<ScalarType>()` from a single
// untrusted byte.  ScalarType is a SPARSE int8_t enum (0..11, 15,
// 23..26, -1) — a corrupt or version-skewed Cipher file can deliver a
// gap value (e.g. 14) or an out-of-range byte.  The unchecked cast then
// flows into element_size(), whose `default: std::unreachable()` makes
// any non-enumerator value UNDEFINED BEHAVIOUR in the size-math path.
//
// `valid_scalar_type` is a named-case predicate (mirrors element_size's
// switch, fails closed on unknown values); ValidScalarType's ctor
// pre-clause rejects an invalid byte via P1494R5 partial-program
// correctness, exactly as ValidNDim guards ndim.  make_scalar_type lifts
// the witness back to a bare ScalarType for storage — the 168-byte
// TensorMeta layout lock + wire format are unchanged (1 byte in, 1 byte
// out; the read switches from r.r<ScalarType>() to r.r<int8_t>() which
// consumes the identical byte).
//
// Cost: regime-1 EBO collapse — sizeof(ValidScalarType) == sizeof(int8_t).
inline constexpr auto valid_scalar_type =
    [](auto raw) constexpr noexcept -> bool {
        switch (static_cast<ScalarType>(static_cast<std::int8_t>(raw))) {
            case ScalarType::Byte:            case ScalarType::Char:
            case ScalarType::Short:           case ScalarType::Int:
            case ScalarType::Long:            case ScalarType::Half:
            case ScalarType::Float:           case ScalarType::Double:
            case ScalarType::ComplexHalf:     case ScalarType::ComplexFloat:
            case ScalarType::ComplexDouble:   case ScalarType::Bool:
            case ScalarType::BFloat16:
            case ScalarType::Float8_e5m2:     case ScalarType::Float8_e4m3fn:
            case ScalarType::Float8_e5m2fnuz: case ScalarType::Float8_e4m3fnuz:
            case ScalarType::Undefined:
                return true;
            default:
                return false;
        }
    };

using ValidScalarType = ::crucible::fixy::wrap::Refined<
    valid_scalar_type, std::int8_t>;

[[nodiscard, gnu::const]] inline constexpr
ScalarType make_scalar_type(ValidScalarType raw) noexcept {
    return static_cast<ScalarType>(raw.value());
}

}  // namespace crucible
