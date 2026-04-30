#pragma once

// ── crucible::safety::extract::is_vendor_v ──────────────────────────
//
// FOUND-D30 (third of batch) — wrapper-detection predicate for
// `Vendor<Backend, T>`.  Mechanical extension of D21-D24/D30
// CipherTier/ResidencyHeat — partial-spec captures the
// VendorBackend_v NTTP enum alongside the wrapped type.
//
// VendorBackend is the largest enum in the D30 batch (8 values:
// None / CPU / NV / AMD / TPU / TRN / CER / Portable, where Portable=255
// is non-contiguous with the 0-6 range).  The detector itself is
// indifferent to enum cardinality or underlying-int spacing — partial
// spec keys on the wrapper class identity, not the underlying value.

#include <crucible/safety/Vendor.h>

#include <type_traits>

namespace crucible::safety::extract {

using ::crucible::safety::VendorBackend_v;

namespace detail {

template <typename T>
struct is_vendor_impl : std::false_type {
    using value_type = void;
    static constexpr bool has_backend = false;
};

template <VendorBackend_v Backend, typename U>
struct is_vendor_impl<::crucible::safety::Vendor<Backend, U>>
    : std::true_type
{
    using value_type = U;
    static constexpr VendorBackend_v backend = Backend;
    static constexpr bool has_backend = true;
};

}  // namespace detail

template <typename T>
inline constexpr bool is_vendor_v =
    detail::is_vendor_impl<std::remove_cvref_t<T>>::value;

template <typename T>
concept IsVendor = is_vendor_v<T>;

template <typename T>
    requires is_vendor_v<T>
using vendor_value_t =
    typename detail::is_vendor_impl<
        std::remove_cvref_t<T>>::value_type;

template <typename T>
    requires is_vendor_v<T>
inline constexpr VendorBackend_v vendor_backend_v =
    detail::is_vendor_impl<std::remove_cvref_t<T>>::backend;

// ── Self-test ─────────────────────────────────────────────────────

namespace detail::is_vendor_self_test {

using V_int_none     = ::crucible::safety::Vendor<VendorBackend_v::None,     int>;
using V_int_cpu      = ::crucible::safety::Vendor<VendorBackend_v::CPU,      int>;
using V_int_nv       = ::crucible::safety::Vendor<VendorBackend_v::NV,       int>;
using V_int_amd      = ::crucible::safety::Vendor<VendorBackend_v::AMD,      int>;
using V_int_tpu      = ::crucible::safety::Vendor<VendorBackend_v::TPU,      int>;
using V_int_trn      = ::crucible::safety::Vendor<VendorBackend_v::TRN,      int>;
using V_int_cer      = ::crucible::safety::Vendor<VendorBackend_v::CER,      int>;
using V_int_portable = ::crucible::safety::Vendor<VendorBackend_v::Portable, int>;
using V_double_nv    = ::crucible::safety::Vendor<VendorBackend_v::NV,       double>;

static_assert(is_vendor_v<V_int_none>);
static_assert(is_vendor_v<V_int_cpu>);
static_assert(is_vendor_v<V_int_nv>);
static_assert(is_vendor_v<V_int_amd>);
static_assert(is_vendor_v<V_int_tpu>);
static_assert(is_vendor_v<V_int_trn>);
static_assert(is_vendor_v<V_int_cer>);
static_assert(is_vendor_v<V_int_portable>);
static_assert(is_vendor_v<V_double_nv>);

static_assert(is_vendor_v<V_int_nv&>);
static_assert(is_vendor_v<V_int_nv const&>);

static_assert(!is_vendor_v<int>);
static_assert(!is_vendor_v<int*>);
static_assert(!is_vendor_v<void>);

struct LookalikeVendor { int value; VendorBackend_v backend; };
static_assert(!is_vendor_v<LookalikeVendor>);

static_assert(!is_vendor_v<V_int_nv*>);

static_assert(IsVendor<V_int_nv>);
static_assert(!IsVendor<int>);

static_assert(std::is_same_v<vendor_value_t<V_int_nv>, int>);
static_assert(std::is_same_v<vendor_value_t<V_double_nv>, double>);

static_assert(vendor_backend_v<V_int_none>     == VendorBackend_v::None);
static_assert(vendor_backend_v<V_int_cpu>      == VendorBackend_v::CPU);
static_assert(vendor_backend_v<V_int_nv>       == VendorBackend_v::NV);
static_assert(vendor_backend_v<V_int_amd>      == VendorBackend_v::AMD);
static_assert(vendor_backend_v<V_int_tpu>      == VendorBackend_v::TPU);
static_assert(vendor_backend_v<V_int_trn>      == VendorBackend_v::TRN);
static_assert(vendor_backend_v<V_int_cer>      == VendorBackend_v::CER);
static_assert(vendor_backend_v<V_int_portable> == VendorBackend_v::Portable);

// Non-contiguous-underlying-value invariant: Portable underlies as
// 255 while the middle vendors are 1-6.  The detector must NOT
// silently lose this distinction.
static_assert(static_cast<std::uint8_t>(VendorBackend_v::Portable) == 255);
static_assert(static_cast<std::uint8_t>(VendorBackend_v::CER)      == 6);
static_assert(vendor_backend_v<V_int_portable>
              != vendor_backend_v<V_int_cer>);

}  // namespace detail::is_vendor_self_test

inline bool is_vendor_smoke_test() noexcept {
    using namespace detail::is_vendor_self_test;

    volatile std::size_t const cap = 4;
    bool ok = true;
    for (std::size_t i = 0; i < cap; ++i) {
        ok = ok && is_vendor_v<V_int_nv>;
        ok = ok && is_vendor_v<V_int_portable>;
        ok = ok && !is_vendor_v<int>;
        ok = ok && IsVendor<V_int_nv&&>;
        ok = ok && (vendor_backend_v<V_int_nv> == VendorBackend_v::NV);
        ok = ok && (vendor_backend_v<V_int_portable>
                    == VendorBackend_v::Portable);
    }
    return ok;
}

}  // namespace crucible::safety::extract
