#include "../vessel/torch/vessel_api_typed.h"

#include <crucible/TensorMeta.h>
#include <crucible/Types.h>
#include <crucible/Vigil.h>
#include <crucible/safety/Tagged.h>

#include <array>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <type_traits>

namespace {

using crucible::vessel::TypedDataPtr;
using crucible::vessel::TypedHandle;
using crucible::vessel::TypedMeta;
using crucible::vessel::TypedSchemaName;

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

// ── GAPS-096 type-shape pins ──────────────────────────────────────
//
// data_ptr_typed and schema_name_typed must EBO-collapse to the bare
// pointer width — same regime-1 guarantee that vessel_api_typed.h
// makes for TypedHandle and TypedMeta.  Strong-distinction pins
// witness that the wrong-tag direction (External vs Sanitized vs
// ABIBoundary) refuses implicit conversion at the type level —
// these are the load-bearing properties the neg-compile fixtures
// rely on.

static_assert(std::is_same_v<
    TypedDataPtr,
    crucible::safety::Tagged<void*, crucible::safety::source::External>>);
static_assert(sizeof(TypedDataPtr) == sizeof(void*));
static_assert(alignof(TypedDataPtr) == alignof(void*));
static_assert(std::is_trivially_copy_constructible_v<TypedDataPtr>);

static_assert(std::is_same_v<
    TypedSchemaName,
    crucible::safety::Tagged<const char*, crucible::safety::source::Sanitized>>);
static_assert(sizeof(TypedSchemaName) == sizeof(const char*));
static_assert(alignof(TypedSchemaName) == alignof(const char*));
static_assert(std::is_trivially_copy_constructible_v<TypedSchemaName>);

// data_ptr provenance distinct from ABIBoundary (the typed-meta
// container's tag): a value with the wrong tag cannot launder into
// the typed-data-ptr slot.
static_assert(!std::is_convertible_v<
    crucible::safety::Tagged<void*, crucible::safety::source::ABIBoundary>,
    TypedDataPtr>);
static_assert(!std::is_convertible_v<
    crucible::safety::Tagged<void*, crucible::safety::source::Sanitized>,
    TypedDataPtr>);

// schema_name provenance distinct from External: validated names
// cannot be confused with raw FFI input.
static_assert(!std::is_convertible_v<
    crucible::safety::Tagged<const char*, crucible::safety::source::External>,
    TypedSchemaName>);

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
    auto* vigil_ptr = std::bit_cast<crucible::Vigil*>(storage.data());
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
    meta.data_ptr = std::bit_cast<void*>(static_cast<std::uintptr_t>(0xCAFEBABE));
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
    EXPECT(typed_multi.value() == std::bit_cast<const crucible::TensorMeta*>(&arr[0]),
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
    const crucible::TensorMeta* raw_ptr = std::bit_cast<const crucible::TensorMeta*>(&arr[0]);
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

void test_data_ptr_typed() {
    // Build a meta array and verify data_ptr_typed reads the same
    // bits the wire struct holds, just with provenance-tagging.
    CrucibleMeta arr[3]{};
    arr[0].data_ptr = std::bit_cast<void*>(static_cast<std::uintptr_t>(0xAAAA0000));
    arr[1].data_ptr = std::bit_cast<void*>(static_cast<std::uintptr_t>(0xBBBB0000));
    arr[2].data_ptr = std::bit_cast<void*>(static_cast<std::uintptr_t>(0xCCCC0000));

    auto typed_arr = crucible::vessel::as_meta_typed(arr, 3);

    auto p0 = crucible::vessel::data_ptr_typed(typed_arr, 0);
    auto p1 = crucible::vessel::data_ptr_typed(typed_arr, 1);
    auto p2 = crucible::vessel::data_ptr_typed(typed_arr, 2);

    EXPECT(p0.value() == arr[0].data_ptr,
           "data_ptr_typed[0] must read the wire pointer");
    EXPECT(p1.value() == arr[1].data_ptr,
           "data_ptr_typed[1] must read the wire pointer");
    EXPECT(p2.value() == arr[2].data_ptr,
           "data_ptr_typed[2] must read the wire pointer");

    // Distinct inputs → distinct typed values, no caching/aliasing.
    EXPECT(p0.value() != p1.value(),
           "distinct meta data_ptrs must yield distinct typed values");
    EXPECT(p1.value() != p2.value(),
           "distinct meta data_ptrs must yield distinct typed values");

    // Layout invariant: the typed wrapper holds the same bytes as
    // the bare void* — regime-1 EBO collapse claim.
    std::array<std::byte, sizeof(TypedDataPtr)> typed_bytes{};
    std::memcpy(typed_bytes.data(), &p0, sizeof(p0));
    std::array<std::byte, sizeof(void*)> raw_bytes{};
    std::memcpy(raw_bytes.data(), &arr[0].data_ptr, sizeof(void*));
    EXPECT(typed_bytes == raw_bytes,
           "TypedDataPtr layout must be byte-identical to void*");
}

void test_schema_name_typed() {
    // Register a name through the C++-side API (the test target links
    // libcrucible, not libcrucible_vessel — so we drive the typed
    // helper through the same global SchemaTable the C-ABI thunk
    // would use).  Verifies schema_name_typed wraps the raw lookup
    // result with source::Sanitized provenance.
    constexpr uint64_t hash_a = 0xA1A2A3A4A5A6A7A8ULL;
    constexpr uint64_t hash_b = 0xB1B2B3B4B5B6B7B8ULL;

    auto& table = crucible::global_schema_table();
    if (!table.is_sealed()) {
        auto view = table.mint_mutable_view();
        crucible::register_schema_name(
            view,
            crucible::SchemaHash{hash_a},
            crucible::SchemaTable::SanitizedName{"aten::test_op_a"});
        crucible::register_schema_name(
            view,
            crucible::SchemaHash{hash_b},
            crucible::SchemaTable::SanitizedName{"aten::test_op_b"});
    }

    auto a = crucible::vessel::schema_name_typed(crucible::SchemaHash{hash_a});
    auto b = crucible::vessel::schema_name_typed(crucible::SchemaHash{hash_b});

    EXPECT(a.value() != nullptr, "registered name a must lookup");
    EXPECT(b.value() != nullptr, "registered name b must lookup");
    EXPECT(a.value() != b.value(),
           "distinct schema hashes must yield distinct name pointers");

    // Idempotent re-lookup.
    auto a2 = crucible::vessel::schema_name_typed(crucible::SchemaHash{hash_a});
    EXPECT(a2.value() == a.value(),
           "repeated lookup must return same interned pointer (stability)");

    // Unknown hash returns null typed view — caller branches on
    // .value() == nullptr exactly as on the raw API.
    auto missing = crucible::vessel::schema_name_typed(
        crucible::SchemaHash{0xDEADC0DEDEADC0DEULL});
    EXPECT(missing.value() == nullptr,
           "unknown schema hash must return null typed view");
}

void test_abi_version_constant() {
    // The C ABI version stamp must be a non-zero compile-time constant.
    // The .so-side `crucible_abi_version()` function returns this same
    // macro, so verifying the macro here is sufficient at TU level —
    // a runtime cross-check of the .so is what
    // crucible_native.py::_check_abi performs at load time.
    static_assert(CRUCIBLE_VESSEL_ABI_VERSION != 0,
                  "ABI version must be non-zero (zero is reserved)");
    EXPECT(CRUCIBLE_VESSEL_ABI_VERSION >= 1,
           "ABI version must be a positive integer");
}

void test_handle_distinct_pointers() {
    // Two distinct Vigil-shaped storage blocks produce distinct
    // typed handles whose `.value()` differs — verifies the wrapper
    // does not collapse / cache / re-use values inadvertently.
    alignas(crucible::Vigil) std::array<std::byte, sizeof(crucible::Vigil)>
        storage_a{};
    alignas(crucible::Vigil) std::array<std::byte, sizeof(crucible::Vigil)>
        storage_b{};
    auto* a = std::bit_cast<crucible::Vigil*>(storage_a.data());
    auto* b = std::bit_cast<crucible::Vigil*>(storage_b.data());
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
    test_data_ptr_typed();
    test_schema_name_typed();
    test_abi_version_constant();
    test_handle_distinct_pointers();

    if (g_failures != 0) {
        std::fprintf(stderr, "test_vessel_api_typed: FAIL (%d)\n", g_failures);
        return EXIT_FAILURE;
    }

    std::fprintf(stderr, "test_vessel_api_typed: PASS\n");
    return EXIT_SUCCESS;
}
