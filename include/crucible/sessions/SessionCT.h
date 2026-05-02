#pragma once

// ═══════════════════════════════════════════════════════════════════
// crucible::safety::ct — CTPayload<T> + ct::eq for CT-required sessions
//                       (SAFEINT-C20, #409,
//                       misc/24_04_2026_safety_integration.md §20)
//
// Sessions whose payloads carry secrets that must be compared in
// constant time (auth tags, MAC digests, encrypted blobs, session
// tokens) need an API surface that STRUCTURALLY REJECTS naive
// comparison.  Today this is review discipline: a careful reviewer
// catches `if (received_tag == expected_tag)` and rewrites it to
// `ct::eq(...)`.  Reviewers miss things; PRs slip; side-channels
// leak credentials.
//
// `CTPayload<T>` raises the discipline to the API surface.  Three
// layers of enforcement compose:
//
//   (1) `requires_ct<T>` trait — the user explicitly opts a type
//       into requiring CT comparison.  Audit-grep `requires_ct<`
//       finds every CT-required type.
//
//   (2) `CTPayload<T>` wrapper — move-only; `operator==` and
//       `operator!=` are EXPLICITLY DELETED with audit-readable
//       reason strings.  Naive comparison won't compile.
//
//   (3) `ct::eq(CTPayload, CTPayload)` overload — the SANCTIONED
//       chokepoint, routes through the existing
//       `ct::eq(byte*, byte*, n)` byte-comparison primitive.  This
//       is the only path that compiles; reviewers grep `ct::eq(`
//       to audit every CT comparison site.
//
// ─── Design ────────────────────────────────────────────────────────
//
// CTPayload stores a raw `T` (not Secret<T>) because the wrapper
// itself IS the audit-discipline marker — it carries the same
// "do not let bytes escape silently" semantics as Secret, plus the
// stronger "comparison must be constant-time" constraint.  Users
// who want Secret-style declassification CAN still wrap CTPayload
// inside Secret — the two compose orthogonally.
//
// The `bytes()` accessor uses `std::as_bytes` (which is well-defined
// for trivially-copyable T — required by the RequiresCT concept).
// No reinterpret_cast, no std::launder — pure standard-library
// reading of the object representation.
//
// ─── What this ships ───────────────────────────────────────────────
//
//   * `requires_ct<T>` trait — user-specialized opt-in.
//   * `requires_ct_v<T>` bool, `RequiresCT<T>` concept.
//   * `CTPayload<T>` move-only wrapper with deleted ==/!= and
//      .bytes() / .declassify_ct<Policy>() accessors.
//   * `ct::eq(CTPayload<T>, CTPayload<T>)` overload — the single
//      sanctioned comparison chokepoint.
//   * Composition with Send/Recv via the existing payload
//      covariance/contravariance rules.
//
// ─── What this deliberately doesn't ship ───────────────────────────
//
//   * Auto-CT-compare SessionHandle specialisation.  Comparison is
//     a transport-side concern; the framework provides the
//     primitives, the user decides where to invoke them.
//   * Subsort axiom CTPayload<T> ⩽ T.  Same load-bearing asymmetry
//     as DeclassifyOnSend (§13) and External / FromUser tags (§6) —
//     allowing the wrapper to silently drop would defeat the
//     audit discoverability of `grep CTPayload<`.
//   * Variable-time access patterns (operator[], element extraction
//     by index, etc.).  Only `bytes()` and explicit policy-tagged
//     declassification are exposed.
//
// ─── Worked example: HMAC tag verification ─────────────────────────
//
//     struct HmacTag { std::array<std::byte, 32> bytes; };
//
//     // Opt HmacTag into the CT discipline.
//     namespace crucible::safety::ct {
//         template <> struct requires_ct<HmacTag> : std::true_type {};
//     }
//
//     using AuthAck = bool;
//     using AuthVerify = Recv<CTPayload<HmacTag>, Send<AuthAck, End>>;
//
//     auto handle = mint_session_handle<AuthVerify>(channel);
//     auto [received_tag, after_recv] = std::move(handle).recv(
//         [](Channel*& c) noexcept -> CTPayload<HmacTag> {
//             return CTPayload<HmacTag>{ c->read_hmac() };
//         });
//
//     CTPayload<HmacTag> expected_tag{ compute_expected_hmac() };
//
//     // ct::eq is the ONLY comparison that compiles.  operator== on
//     // CTPayload is explicitly deleted; the discipline is structural.
//     bool ok = ct::eq(received_tag, expected_tag);
//
//     auto end_handle = std::move(after_recv).send(
//         ok,
//         [](Channel*& c, AuthAck v) noexcept { c->write_ack(v); });
//
// ─── Resolves ──────────────────────────────────────────────────────
//
//   * misc/24_04_2026_safety_integration.md §20 — design rationale.
//   * Composes with #402 DeclassifyOnSend: a payload may be both
//     CT-required AND wire-classified by stacking `DeclassifyOnSend<
//     CTPayload<T>, Policy>` (Secret-class discipline + CT-comparison
//     discipline + wire-policy at the protocol declaration).
//   * Foundational for Cipher cold-tier MAC verification, Canopy peer
//     auth tag comparison, mTLS session-token equality.
//
// ─── Cost ──────────────────────────────────────────────────────────
//
//   sizeof(CTPayload<T>) == sizeof(T)
//
// .bytes() is a span-construction over the address of the inline T;
// declassify_ct is one move; ct::eq compares N bytes in straight-line
// code per the existing ct::eq primitive.  Zero overhead beyond the
// underlying byte-comparison.
//
// ─── References ────────────────────────────────────────────────────
//
//   safety/ConstantTime.h — the underlying ct::eq / ct::less etc.
//                           primitives that this header consumes.
//   safety/Secret.h — the orthogonal classification discipline; user
//                     can compose Secret<CTPayload<T>> for both.
//   safety/Session.h — Send/Recv combinators that consume CTPayload
//                      via the existing payload covariance rules.
// ═══════════════════════════════════════════════════════════════════

#include <crucible/Platform.h>
#include <crucible/safety/ConstantTime.h>
#include <crucible/safety/Secret.h>          // for DeclassificationPolicy concept
#include <crucible/sessions/Session.h>
#include <crucible/sessions/SessionSubtype.h>

#include <cstddef>
#include <span>
#include <type_traits>
#include <utility>

namespace crucible::safety::ct {

// ═════════════════════════════════════════════════════════════════════
// ── requires_ct<T> trait (user-opt-in) ─────────────────────────────
// ═════════════════════════════════════════════════════════════════════
//
// Default false — types are NOT CT-required by default.  The user
// explicitly specialises this for crypto-tag / MAC / session-token
// types that must be compared in constant time:
//
//     namespace crucible::safety::ct {
//         template <> struct requires_ct<MyHmacTag> : std::true_type {};
//     }
//
// Audit-grep `requires_ct<` finds every CT-required type.

template <typename T>
struct requires_ct : std::false_type {};

template <typename T>
inline constexpr bool requires_ct_v = requires_ct<T>::value;

// Concept: T is CT-required AND trivially-copyable.  The trivial-
// copyability constraint is needed for `std::as_bytes` to give us
// well-defined byte-level access without UB.  Crypto tags, MAC
// digests, and key material naturally satisfy this (they're typically
// small POD arrays); types that don't are not appropriate for CT
// payload semantics anyway.

template <typename T>
concept RequiresCT = requires_ct_v<T> && std::is_trivially_copyable_v<T>;

// ═════════════════════════════════════════════════════════════════════
// ── CTPayload<T> wrapper ───────────────────────────────────────────
// ═════════════════════════════════════════════════════════════════════

template <typename T>
    requires RequiresCT<T>
class [[nodiscard]] CTPayload {
    T value_;

public:
    using value_type = T;

    // Construct from raw T.  Move-in (T is trivially copyable so
    // this is a copy at the value level, but we move the wrapper).
    constexpr explicit CTPayload(T v) noexcept
        : value_{std::move(v)} {}

    // In-place construction.
    template <typename... Args>
        requires std::is_constructible_v<T, Args...>
    constexpr explicit CTPayload(std::in_place_t, Args&&... args)
        noexcept(std::is_nothrow_constructible_v<T, Args...>)
        : value_{std::forward<Args>(args)...} {}

    // Move-only.  Copy is deleted with audit-readable reason.
    CTPayload(const CTPayload&)
        = delete("CTPayload wraps CT-required data; classified values cannot silently duplicate");
    CTPayload& operator=(const CTPayload&)
        = delete("CTPayload wraps CT-required data; classified values cannot silently duplicate");
    CTPayload(CTPayload&&)            noexcept = default;
    CTPayload& operator=(CTPayload&&) noexcept = default;
    ~CTPayload()                                = default;

    // ── Comparison: explicitly deleted ─────────────────────────────
    //
    // Naive `if (received == expected)` won't compile.  The deleted
    // operator's reason string names the canonical fix in the
    // diagnostic; reviewers reading the error message learn the
    // pattern at the rejection site.

    bool operator==(const CTPayload&) const
        = delete("CTPayload comparison must use crucible::safety::ct::eq() to avoid timing side-channel; operator== is deleted to enforce this at compile time");
    bool operator!=(const CTPayload&) const
        = delete("CTPayload comparison must use crucible::safety::ct::eq() to avoid timing side-channel; operator!= is deleted to enforce this at compile time");

    // ── Byte-level read access ─────────────────────────────────────
    //
    // The only non-consuming access path.  Returns a const byte span
    // covering the underlying T's storage, computed via std::as_bytes
    // (well-defined for trivially-copyable T).  Used by the ct::eq
    // overload for byte-by-byte CT comparison.

    [[nodiscard]] std::span<const std::byte> bytes() const noexcept {
        return std::as_bytes(std::span<const T, 1>{&value_, 1});
    }

    // ── Declassification (auditable extraction) ────────────────────
    //
    // Consumes *this and returns the raw T.  Requires a
    // DeclassificationPolicy tag — the call site
    // `.declassify_ct<some_policy>()` is the audit trail.  Same
    // discipline as Secret::declassify; the `_ct` suffix
    // distinguishes the call sites in greps from generic
    // `.declassify<>()` for non-CT secrets.

    template <DeclassificationPolicy Policy>
    [[nodiscard]] constexpr T declassify_ct() && noexcept {
        return std::move(value_);
    }
};

// ═════════════════════════════════════════════════════════════════════
// ── ct::eq overload for CTPayload pairs ────────────────────────────
// ═════════════════════════════════════════════════════════════════════
//
// The sanctioned comparison chokepoint.  Routes through the existing
// `ct::eq(byte*, byte*, n)` byte-comparison primitive.  Time depends
// only on sizeof(T), not on byte content or position of differences.
//
// audit-grep `ct::eq(` finds every CT comparison site across the
// codebase.

template <typename T>
    requires RequiresCT<T>
[[nodiscard]] bool eq(CTPayload<T> const& a, CTPayload<T> const& b) noexcept {
    auto ab = a.bytes();
    auto bb = b.bytes();
    return crucible::safety::ct::eq(ab.data(), bb.data(), ab.size());
}

// ═════════════════════════════════════════════════════════════════════
// ── Shape traits + concept ─────────────────────────────────────────
// ═════════════════════════════════════════════════════════════════════

template <typename T>
struct is_ct_payload : std::false_type {};

template <typename T>
struct is_ct_payload<CTPayload<T>> : std::true_type {};

template <typename T>
inline constexpr bool is_ct_payload_v = is_ct_payload<T>::value;

template <typename T>
concept CTPayloadType = is_ct_payload_v<T>;

// ct_payload_value_type_t<CTPayload<T>> — the underlying T.  For
// non-CTPayload types, returns the type unchanged (so generic code
// can use this without first checking is_ct_payload_v).

template <typename T>
struct ct_payload_value_type {
    using type = T;
};

template <typename T>
struct ct_payload_value_type<CTPayload<T>> {
    using type = T;
};

template <typename T>
using ct_payload_value_type_t = typename ct_payload_value_type<T>::type;

// ═════════════════════════════════════════════════════════════════════
// ── Framework self-test static_asserts ─────────────────────────────
// ═════════════════════════════════════════════════════════════════════

#ifdef CRUCIBLE_SESSION_SELF_TESTS
namespace detail::ct_self_test {

// Fixture: a CT-required type.  Trivially copyable POD by design.
struct TestTag {
    std::byte bytes[16]{};
};

}  // namespace detail::ct_self_test

// Specialise requires_ct for the test fixture (must be in the
// requires_ct namespace).
template <>
struct requires_ct<detail::ct_self_test::TestTag> : std::true_type {};

namespace detail::ct_self_test {

// requires_ct trait fires for opted-in types.
static_assert( requires_ct_v<TestTag>);
static_assert( RequiresCT<TestTag>);

// Default: not CT-required.
static_assert(!requires_ct_v<int>);
static_assert(!RequiresCT<int>);

// CTPayload requires the trait.  Construction with a non-opted-in
// type is rejected at template instantiation.
using TagPayload = CTPayload<TestTag>;

static_assert( is_ct_payload_v<TagPayload>);
static_assert(!is_ct_payload_v<TestTag>);
static_assert(!is_ct_payload_v<int>);

static_assert( CTPayloadType<TagPayload>);
static_assert(!CTPayloadType<TestTag>);

// ct_payload_value_type_t extracts the inner T.
static_assert(std::is_same_v<ct_payload_value_type_t<TagPayload>, TestTag>);
static_assert(std::is_same_v<ct_payload_value_type_t<int>, int>);

// Move-only discipline.
static_assert(!std::is_copy_constructible_v<TagPayload>);
static_assert( std::is_move_constructible_v<TagPayload>);

// Zero-cost size guarantee.
static_assert(sizeof(TagPayload) == sizeof(TestTag));

// Comparison operators are deleted (verified at the test TU level
// via concept-based check that == is not a valid expression on the
// wrapper).

template <typename A, typename B>
concept has_operator_eq = requires(A a, B b) { a == b; };

static_assert(!has_operator_eq<TagPayload const&, TagPayload const&>);
static_assert(!has_operator_eq<TagPayload, TagPayload>);

// ── Composition with Send/Recv via existing payload covariance ────
//
// CTPayload<T> is a payload type — it composes with Send/Recv like
// any other.  The wrapper carries STRONGER discipline than bare T;
// it must NOT silently flow to a bare-T position via subtyping —
// same load-bearing asymmetry as DeclassifyOnSend (§13) and
// External / FromUser tags (§6).

using namespace crucible::safety::proto;

// Reflexivity: CTPayload<T> ⩽ CTPayload<T>.
static_assert(is_subtype_sync_v<
    Send<TagPayload, End>,
    Send<TagPayload, End>>);

// DELIBERATELY ABSENT axioms: CTPayload subsort is one-way-rejected.
//
// A protocol declaring Send<CTPayload<T>, K> must NOT silently flow
// to Send<T, K> — the wrapper's audit-discipline is what makes
// `grep CTPayload<` mechanically discoverable.  Same for Recv.

static_assert(!is_subtype_sync_v<
    Send<TagPayload, End>,
    Send<TestTag,    End>>);

static_assert(!is_subtype_sync_v<
    Send<TestTag,    End>,
    Send<TagPayload, End>>);

static_assert(!is_subtype_sync_v<
    Recv<TagPayload, End>,
    Recv<TestTag,    End>>);

static_assert(!is_subtype_sync_v<
    Recv<TestTag,    End>,
    Recv<TagPayload, End>>);

}  // namespace detail::ct_self_test
#endif  // CRUCIBLE_SESSION_SELF_TESTS

}  // namespace crucible::safety::ct
