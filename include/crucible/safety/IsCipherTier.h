#pragma once

// ── crucible::safety::extract::is_cipher_tier_v ─────────────────────
//
// FOUND-D30 (first of batch) — wrapper-detection predicate for
// `CipherTier<Tier, T>`.  Mechanical extension of the D21-D24
// template — partial-spec captures the CipherTierTag_v NTTP enum
// alongside the wrapped type.
//
// ── What this header ships ──────────────────────────────────────────
//
//   is_cipher_tier_v<T>      Variable template; cv-ref-stripped.
//   IsCipherTier<T>           Concept form.
//   cipher_tier_value_t<T>    Wrapped element type; constrained.
//   cipher_tier_tag_v<T>      Pinned CipherTierTag_v; constrained.

#include <crucible/safety/CipherTier.h>

#include <type_traits>

namespace crucible::safety::extract {

using ::crucible::safety::CipherTierTag_v;

namespace detail {

template <typename T>
struct is_cipher_tier_impl : std::false_type {
    using value_type = void;
    static constexpr bool has_tag = false;
};

template <CipherTierTag_v Tier, typename U>
struct is_cipher_tier_impl<::crucible::safety::CipherTier<Tier, U>>
    : std::true_type
{
    using value_type = U;
    static constexpr CipherTierTag_v tier = Tier;
    static constexpr bool has_tag = true;
};

}  // namespace detail

template <typename T>
inline constexpr bool is_cipher_tier_v =
    detail::is_cipher_tier_impl<std::remove_cvref_t<T>>::value;

template <typename T>
concept IsCipherTier = is_cipher_tier_v<T>;

template <typename T>
    requires is_cipher_tier_v<T>
using cipher_tier_value_t =
    typename detail::is_cipher_tier_impl<
        std::remove_cvref_t<T>>::value_type;

template <typename T>
    requires is_cipher_tier_v<T>
inline constexpr CipherTierTag_v cipher_tier_tag_v =
    detail::is_cipher_tier_impl<std::remove_cvref_t<T>>::tier;

// ── Self-test ─────────────────────────────────────────────────────

namespace detail::is_cipher_tier_self_test {

using CT_int_hot =
    ::crucible::safety::CipherTier<CipherTierTag_v::Hot, int>;
using CT_double_warm =
    ::crucible::safety::CipherTier<CipherTierTag_v::Warm, double>;
using CT_int_cold =
    ::crucible::safety::CipherTier<CipherTierTag_v::Cold, int>;

static_assert(is_cipher_tier_v<CT_int_hot>);
static_assert(is_cipher_tier_v<CT_double_warm>);
static_assert(is_cipher_tier_v<CT_int_cold>);

static_assert(is_cipher_tier_v<CT_int_hot&>);
static_assert(is_cipher_tier_v<CT_int_hot const&>);

static_assert(!is_cipher_tier_v<int>);
static_assert(!is_cipher_tier_v<int*>);
static_assert(!is_cipher_tier_v<void>);

struct LookalikeCipherTier { int value; CipherTierTag_v tier; };
static_assert(!is_cipher_tier_v<LookalikeCipherTier>);

static_assert(!is_cipher_tier_v<CT_int_hot*>);

static_assert(IsCipherTier<CT_int_hot>);
static_assert(!IsCipherTier<int>);

static_assert(std::is_same_v<cipher_tier_value_t<CT_int_hot>, int>);
static_assert(std::is_same_v<cipher_tier_value_t<CT_double_warm>, double>);

static_assert(cipher_tier_tag_v<CT_int_hot>  == CipherTierTag_v::Hot);
static_assert(cipher_tier_tag_v<CT_double_warm> == CipherTierTag_v::Warm);
static_assert(cipher_tier_tag_v<CT_int_cold> == CipherTierTag_v::Cold);

}  // namespace detail::is_cipher_tier_self_test

inline bool is_cipher_tier_smoke_test() noexcept {
    using namespace detail::is_cipher_tier_self_test;

    volatile std::size_t const cap = 4;
    bool ok = true;
    for (std::size_t i = 0; i < cap; ++i) {
        ok = ok && is_cipher_tier_v<CT_int_hot>;
        ok = ok && !is_cipher_tier_v<int>;
        ok = ok && IsCipherTier<CT_int_hot&&>;
        ok = ok && (cipher_tier_tag_v<CT_int_hot> == CipherTierTag_v::Hot);
    }
    return ok;
}

}  // namespace crucible::safety::extract
