#include "../vessel/torch/vessel_api_typed.h"

#include <crucible/TensorMeta.h>
#include <crucible/Types.h>
#include <crucible/Vigil.h>
#include <crucible/safety/Tagged.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <type_traits>

namespace {

using crucible::vessel::TypedHandle;
using crucible::vessel::TypedMeta;

// ── Type-shape pins (compile-time invariants) ──────────────────────
//
// These witness the EBO-collapse claim of safety::Tagged at the
// Vessel ABI boundary.  If any of these fire, the migration's
// "zero runtime cost" promise is broken — the typed wrapper is
// adding storage to the handle/meta pointer.

static_assert(std::is_same_v<
    TypedHandle,
    crucible::safety::Tagged<
        crucible::Vigil*,
        crucible::safety::source::ABIBoundary>>);
static_assert(sizeof(TypedHandle) == sizeof(CrucibleHandle));
static_assert(alignof(TypedHandle) == alignof(CrucibleHandle));
static_assert(std::is_trivially_copy_constructible_v<TypedHandle>);

static_assert(std::is_same_v<
    TypedMeta,
    crucible::safety::Tagged<
        const crucible::TensorMeta*,
        crucible::safety::source::ABIBoundary>>);
static_assert(sizeof(TypedMeta) == sizeof(const CrucibleMeta*));
static_assert(alignof(TypedMeta) == alignof(const CrucibleMeta*));
static_assert(std::is_trivially_copy_constructible_v<TypedMeta>);

// Strong-distinction pin: a `Tagged<T, source::External>` is a
// DIFFERENT class instantiation from `Tagged<T, source::ABIBoundary>`,
// with no implicit conversion between them.  This is the property
// the wrong-tag neg-compile fixture (test/safety_neg/...) leans on.
static_assert(!std::is_convertible_v<
    crucible::safety::Tagged<crucible::Vigil*, crucible::safety::source::External>,
    TypedHandle>);
static_assert(!std::is_convertible_v<
    crucible::safety::Tagged<const crucible::TensorMeta*, crucible::safety::source::External>,
    TypedMeta>);

// ── Runtime smoke harness ──────────────────────────────────────────

int g_failures = 0;

#define EXPECT(cond, msg) do {                                                \
    if (!(cond)) {                                                             \
        std::fprintf(stderr, "FAIL: %s -- %s (%s:%d)\n",                      \
                     #cond, (msg), __FILE__, __LINE__);                        \
        ++g_failures;                                                          \
    }                                                                          \
} while (0)

void test_handle_roundtrip() {
    alignas(crucible::Vigil) std::array<std::byte, sizeof(crucible::Vigil)>
        storage{};
    auto* vigil_ptr = reinterpret_cast<crucible::Vigil*>(storage.data());
    CrucibleHandle raw = static_cast<CrucibleHandle>(vigil_ptr);

    auto typed = crucible::vessel::as_vigil_typed(raw);
    EXPECT(typed.value() == vigil_ptr,
           "typed handle must preserve the Vigil pointer value");

    CrucibleHandle roundtrip = crucible::vessel::from_typed(typed);
    EXPECT(roundtrip == raw, "typed handle must roundtrip to CrucibleHandle");

    // Trivial-copy invariant: bytes match the raw pointer.
    std::array<std::byte, sizeof(TypedHandle)> typed_bytes{};
    std::memcpy(typed_bytes.data(), &typed, sizeof(typed));
    std::array<std::byte, sizeof(CrucibleHandle)> raw_bytes{};
    std::memcpy(raw_bytes.data(), &raw, sizeof(raw));
    EXPECT(typed_bytes == raw_bytes,
           "TypedHandle layout must be byte-identical to CrucibleHandle");
}

void test_meta_roundtrip() {
    // Build a layout-realistic meta: arbitrary sizes, strides, ndim,
    // dtype, etc., to verify the layout-compat reinterpret yields the
    // same field values when accessed via either struct.
    CrucibleMeta meta{};
    meta.sizes[0] = 4;
    meta.sizes[1] = 8;
    meta.sizes[2] = 16;
    meta.strides[0] = 128;
    meta.strides[1] = 16;
    meta.strides[2] = 1;
    meta.data_ptr = reinterpret_cast<void*>(static_cast<std::uintptr_t>(0xCAFEBABE));
    meta.ndim = 3;
    meta.dtype = static_cast<int8_t>(crucible::ScalarType::Float);
    meta.device_type = static_cast<int8_t>(crucible::DeviceType::CUDA);
    meta.device_idx = 1;
    meta.layout = static_cast<int8_t>(crucible::Layout::Strided);
    meta.requires_grad = 1;
    meta.flags = 0x07;
    meta.output_nr = 2;
    meta.storage_offset = 32;
    meta.version = 5;
    meta.storage_nbytes = 4 * 8 * 16 * 4;
    meta.grad_fn_hash = 0x0123456789ABCDEFULL;

    const CrucibleMeta arr[2] = {meta, meta};

    // Single-meta path with explicit n_metas=1.  Default n_metas=0 is
    // reserved for the (nullptr, 0) trivial-op case — the helper's
    // contract refuses (non-null, 0) under debug build because the
    // C ABI invariant is "n_metas==0 ⇒ metas==nullptr".
    auto typed_single = crucible::vessel::as_meta_typed(&arr[0], 1);
    EXPECT(typed_single.value() != nullptr,
           "as_meta_typed on non-null meta must return non-null pointer");

    // Multi-meta path with n_metas argument.
    auto typed_multi = crucible::vessel::as_meta_typed(arr, 2);
    EXPECT(typed_multi.value() == reinterpret_cast<const crucible::TensorMeta*>(arr),
           "as_meta_typed pointer must match raw pointer (layout-compat reinterpret)");

    // Field-equivalence: read the same field through TensorMeta — the
    // values must match what we wrote into CrucibleMeta.  Three picks
    // exercise sizes[], a scalar field, and a tail field to catch
    // offset-skew bugs across the struct.
    const auto* tm = typed_multi.value();
    EXPECT(tm[0].sizes[0] == 4, "TensorMeta::sizes[0] must read 4 from CrucibleMeta");
    EXPECT(tm[0].sizes[1] == 8, "TensorMeta::sizes[1] must read 8 from CrucibleMeta");
    EXPECT(tm[0].ndim == 3, "TensorMeta::ndim must read 3");
    EXPECT(tm[0].grad_fn_hash == 0x0123456789ABCDEFULL,
           "TensorMeta::grad_fn_hash must read all 64 bits intact");
    EXPECT(tm[1].storage_nbytes == 4 * 8 * 16 * 4,
           "Second-element storage_nbytes must read intact (no array stride bug)");

    // Round-trip through metas_from_typed.
    const CrucibleMeta* roundtripped = crucible::vessel::metas_from_typed(typed_multi);
    EXPECT(roundtripped == arr,
           "metas_from_typed must round-trip to the original CrucibleMeta pointer");

    // Trivial-copy invariant: the typed view holds the same bytes as
    // the raw pointer it wraps.
    std::array<std::byte, sizeof(TypedMeta)> typed_bytes{};
    std::memcpy(typed_bytes.data(), &typed_multi, sizeof(typed_multi));
    const crucible::TensorMeta* raw_ptr = reinterpret_cast<const crucible::TensorMeta*>(arr);
    std::array<std::byte, sizeof(const crucible::TensorMeta*)> raw_bytes{};
    std::memcpy(raw_bytes.data(), &raw_ptr, sizeof(raw_ptr));
    EXPECT(typed_bytes == raw_bytes,
           "TypedMeta layout must be byte-identical to const TensorMeta*");
}

void test_meta_zero_n_with_null() {
    // The C ABI promise: n_metas==0 implies metas==nullptr.  The typed
    // helper accepts this and returns a typed nullptr that round-trips
    // cleanly — used when an op has zero tensor arguments (e.g.,
    // profiler::_record_function_enter_new).
    auto typed = crucible::vessel::as_meta_typed(nullptr, 0);
    EXPECT(typed.value() == nullptr,
           "as_meta_typed(nullptr, 0) must return null typed view");

    const CrucibleMeta* roundtripped = crucible::vessel::metas_from_typed(typed);
    EXPECT(roundtripped == nullptr,
           "metas_from_typed of null typed view must round-trip to nullptr");
}

void test_handle_distinct_pointers() {
    // Two distinct Vigil-shaped storage blocks produce distinct
    // typed handles whose `.value()` differs — verifies the wrapper
    // does not collapse / cache / re-use values inadvertently.
    alignas(crucible::Vigil) std::array<std::byte, sizeof(crucible::Vigil)>
        storage_a{};
    alignas(crucible::Vigil) std::array<std::byte, sizeof(crucible::Vigil)>
        storage_b{};
    auto* a = reinterpret_cast<crucible::Vigil*>(storage_a.data());
    auto* b = reinterpret_cast<crucible::Vigil*>(storage_b.data());
    EXPECT(a != b, "test setup: distinct storage must yield distinct pointers");

    auto ta = crucible::vessel::as_vigil_typed(static_cast<CrucibleHandle>(a));
    auto tb = crucible::vessel::as_vigil_typed(static_cast<CrucibleHandle>(b));
    EXPECT(ta.value() == a, "typed handle for a must alias a");
    EXPECT(tb.value() == b, "typed handle for b must alias b");
    EXPECT(ta.value() != tb.value(),
           "distinct inputs must yield distinct typed handle values");
}

} // namespace

int main() {
    test_handle_roundtrip();
    test_meta_roundtrip();
    test_meta_zero_n_with_null();
    test_handle_distinct_pointers();

    if (g_failures != 0) {
        std::fprintf(stderr, "test_vessel_api_typed: FAIL (%d)\n", g_failures);
        return EXIT_FAILURE;
    }

    std::fprintf(stderr, "test_vessel_api_typed: PASS\n");
    return EXIT_SUCCESS;
}
