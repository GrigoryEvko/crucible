#pragma once

// ═══════════════════════════════════════════════════════════════════
// crucible::safety::proto — ContentAddressed<T> quotient combinator
//                            (SEPLOG-L1, task #361, Appendix D.5)
//
// ContentAddressed<T> is a PAYLOAD WRAPPER marking a value type as
// eligible for content-hash-based wire elision.  Semantics per
// session_types.md Appendix D.5:
//
//     Send<ContentAddressed<T>, K> semantically means:
//       if recipient has hash(T) cached, skip wire transmission;
//       else send the bytes.
//
// The recipient's observable protocol state depends ONLY on the
// payload's content hash — whether bytes arrived on the wire or
// were found in a local content-addressed cache is opaque at the
// protocol level.  This is a QUOTIENT SEMANTICS:  the protocol is
// INVARIANT under content-hash-preserving transformations.
//
// ─── Type-level contract ──────────────────────────────────────────
//
// ContentAddressed<T> and T are mutually subsortable — they're
// interchangeable payload types at the subtype level:
//
//     is_subsort<ContentAddressed<T>, T>  == true
//     is_subsort<T, ContentAddressed<T>>  == true
//
// Consequently, Send<ContentAddressed<T>, K> and Send<T, K> are
// mutually subtypes (via Send's payload-covariance through subsort).
// Same for Recv.  A protocol that refactors to opt INTO wire-level
// dedup at the transport layer stays provably equivalent to its
// pre-refactor version at the type level.
//
// ─── Wrapping and unwrapping ──────────────────────────────────────
//
//     ContentAddressed<T>::wrapped                  == T
//     content_addressed_underlying_t<ContentAddressed<T>>  == T
//     content_addressed_underlying_t<T>             == T  (non-wrapped)
//     unwrap_content_addressed_t<ContentAddressed<ContentAddressed<T>>>
//                                                   == T
//                                                   (recursive strip)
//     content_addressed_depth_v<T>                  number of nested
//                                                   ContentAddressed<>
//                                                   wrappers
//
// ─── Why this is a TYPE-level layer, not a runtime construct ─────
//
// The wire-elision decision — "does the recipient already have
// hash(T) cached?" — is a RUNTIME question answered by the transport
// layer (Cipher dedup, cross-session prefix sharing for KV cache,
// KernelCache broadcast).  The TYPE system's job is to certify that
// the protocol's correctness does not depend on which path (wire
// transmit vs cache hit) delivers the payload.  ContentAddressed<T>
// is that certification.
//
// ─── Canonical Crucible uses ──────────────────────────────────────
//
//   * Cipher three-tier publication — L1 IR002 snapshots, L2 IR003
//     per-vendor, L3 compiled-binary; all content-addressed; cross-
//     session dedup eligible.
//   * KernelCache broadcast — identical kernels distributed to all
//     workers, dedup'd at the transport.
//   * PagedKVCache — cross-session prefix sharing (CRUCIBLE.md §IV.27);
//     prefix pages content-addressed; multiple inference sessions
//     share physical storage for identical prefix content.
//   * Replay trace events — identical ops across iterations hit the
//     same content hash.
//
// Each of these uses Send<ContentAddressed<Payload>, _> in its session
// type to declare "this wire message is dedup-eligible" without
// committing to which specific dedup mechanism the runtime uses.
//
// ─── What ships here; what doesn't ────────────────────────────────
//
// Shipped:
//   * ContentAddressed<T> wrapper type
//   * is_content_addressed_v / _depth_v / _underlying_t traits
//   * unwrap_content_addressed_t recursive unwrap
//   * is_subsort specialisations for mutual interchangeability
//   * ContentAddressedType concept
//
// NOT shipped (by design):
//   * Runtime content-hash computation — that's the transport's job;
//     the type layer doesn't prescribe a hash function.
//   * Dedup cache machinery — Cipher / KernelCache own this.
//   * Wire-format negotiation (cache-miss hint) — transport-level.
//
// ─── References ───────────────────────────────────────────────────
//
//   session_types.md Appendix D.5 — specification of this combinator.
//   Honda-Yoshida-Carbone 2008 — classical MPST doesn't handle
//     quotient payloads; this is an extension specific to Crucible.
// ═══════════════════════════════════════════════════════════════════

#include <crucible/Platform.h>
#include <crucible/safety/Session.h>
#include <crucible/safety/SessionSubtype.h>

#include <cstddef>
#include <type_traits>

namespace crucible::safety::proto {

// ═════════════════════════════════════════════════════════════════════
// ── ContentAddressed<T> ────────────────────────────────────────────
// ═════════════════════════════════════════════════════════════════════
//
// Pure type marker.  Zero runtime footprint.  The nested alias
// `wrapped` exposes the underlying payload type for introspection.

template <typename T>
struct ContentAddressed {
    using wrapped = T;
};

// ═════════════════════════════════════════════════════════════════════
// ── Shape traits ───────────────────────────────────────────────────
// ═════════════════════════════════════════════════════════════════════

template <typename T>
struct is_content_addressed : std::false_type {};

template <typename T>
struct is_content_addressed<ContentAddressed<T>> : std::true_type {};

template <typename T>
inline constexpr bool is_content_addressed_v =
    is_content_addressed<T>::value;

// Concept form for require-clauses.
template <typename T>
concept ContentAddressedType = is_content_addressed_v<T>;

// ═════════════════════════════════════════════════════════════════════
// ── content_addressed_underlying_t<T> ──────────────────────────────
// ═════════════════════════════════════════════════════════════════════
//
// Strip ONE layer of ContentAddressed<>.  For ContentAddressed<T>
// returns T; for any other type, returns the type unchanged.  This
// is the COMMON case — wrappers are typically unnested.
//
// For deep unwrap across multiple wrappers, use
// unwrap_content_addressed_t below.

template <typename T>
struct content_addressed_underlying {
    using type = T;
};

template <typename T>
struct content_addressed_underlying<ContentAddressed<T>> {
    using type = T;
};

template <typename T>
using content_addressed_underlying_t =
    typename content_addressed_underlying<T>::type;

// ═════════════════════════════════════════════════════════════════════
// ── unwrap_content_addressed_t<T> (recursive) ──────────────────────
// ═════════════════════════════════════════════════════════════════════
//
// Strip ALL layers of ContentAddressed<>.  Recurses until T is no
// longer content-addressed.  Useful for identity checks after
// arbitrary wrapping:
//
//     using T = ContentAddressed<ContentAddressed<ContentAddressed<int>>>;
//     static_assert(std::is_same_v<unwrap_content_addressed_t<T>, int>);
//
// Unusual to have nesting, but the trait handles it robustly.

template <typename T>
struct unwrap_content_addressed {
    using type = T;
};

template <typename T>
struct unwrap_content_addressed<ContentAddressed<T>>
    : unwrap_content_addressed<T> {};

template <typename T>
using unwrap_content_addressed_t =
    typename unwrap_content_addressed<T>::type;

// ═════════════════════════════════════════════════════════════════════
// ── content_addressed_depth_v<T> ───────────────────────────────────
// ═════════════════════════════════════════════════════════════════════
//
// Count the number of nested ContentAddressed<> wrappers.  Returns
// 0 for non-wrapped types, 1 for ContentAddressed<T>, N for N-deep
// nesting.

namespace detail::ca {

template <typename T>
struct depth : std::integral_constant<std::size_t, 0> {};

template <typename T>
struct depth<ContentAddressed<T>>
    : std::integral_constant<std::size_t, 1 + depth<T>::value> {};

}  // namespace detail::ca

template <typename T>
inline constexpr std::size_t content_addressed_depth_v =
    detail::ca::depth<T>::value;

// ═════════════════════════════════════════════════════════════════════
// ── is_subsort integration: mutual interchangeability ──────────────
// ═════════════════════════════════════════════════════════════════════
//
// ContentAddressed<T> and T are mutually subsortable at the value-
// type level.  Consequences via Send's payload covariance and Recv's
// payload contravariance:
//
//   Send<ContentAddressed<T>, K>  ⩽  Send<T, K>            (wrap-to-raw)
//   Send<T, K>  ⩽  Send<ContentAddressed<T>, K>            (raw-to-wrap)
//
//   Recv<ContentAddressed<T>, K>  ⩽  Recv<T, K>            (wrap-to-raw,
//                                                           contravariant)
//   Recv<T, K>  ⩽  Recv<ContentAddressed<T>, K>            (raw-to-wrap)
//
// I.e., at the protocol level, the wrapper-and-raw forms are
// interchangeable.  A protocol that opts into ContentAddressed<> at
// the transport-hint layer stays provably equivalent to the
// pre-opt-in version.
//
// Specialisation ordering:  these partial specialisations pin the
// relationship between ContentAddressed<X> and X; they're strictly
// more specialised than the primary `is_subsort<T, U> = is_same<T, U>`.
// The compiler picks them when applicable; no collision with
// user-defined subsort specialisations on unrelated type pairs.

template <typename T>
struct is_subsort<ContentAddressed<T>, T> : std::true_type {};

template <typename T>
struct is_subsort<T, ContentAddressed<T>> : std::true_type {};

// Reflexive case ContentAddressed<T> ↔ ContentAddressed<T> — falls
// through to the primary `is_subsort<T, T> = true` (via is_same).
// We don't need an explicit specialisation for this; verify in the
// self-tests below.

// ═════════════════════════════════════════════════════════════════════
// ── Framework self-test static_asserts ─────────────────────────────
// ═════════════════════════════════════════════════════════════════════

namespace detail::ca::ca_self_test {

// Fixture payload.
struct Msg {};
struct Ack {};

// ─── Shape traits ─────────────────────────────────────────────────

static_assert( is_content_addressed_v<ContentAddressed<Msg>>);
static_assert(!is_content_addressed_v<Msg>);
static_assert(!is_content_addressed_v<int>);
static_assert(!is_content_addressed_v<End>);

// Concept form.
template <ContentAddressedType T>
consteval bool requires_content_addressed() { return true; }
static_assert(requires_content_addressed<ContentAddressed<Msg>>());

// ─── content_addressed_underlying_t (single-layer strip) ──────────

static_assert(std::is_same_v<
    content_addressed_underlying_t<ContentAddressed<Msg>>, Msg>);
static_assert(std::is_same_v<
    content_addressed_underlying_t<Msg>, Msg>);
static_assert(std::is_same_v<
    content_addressed_underlying_t<int>, int>);

// Nested: strips only the OUTER wrapper, leaves inner intact.
static_assert(std::is_same_v<
    content_addressed_underlying_t<ContentAddressed<ContentAddressed<Msg>>>,
    ContentAddressed<Msg>>);

// ─── unwrap_content_addressed_t (recursive strip) ─────────────────

static_assert(std::is_same_v<
    unwrap_content_addressed_t<Msg>, Msg>);
static_assert(std::is_same_v<
    unwrap_content_addressed_t<ContentAddressed<Msg>>, Msg>);
static_assert(std::is_same_v<
    unwrap_content_addressed_t<ContentAddressed<ContentAddressed<Msg>>>,
    Msg>);
static_assert(std::is_same_v<
    unwrap_content_addressed_t<
        ContentAddressed<ContentAddressed<ContentAddressed<Msg>>>>,
    Msg>);

// ─── content_addressed_depth_v ────────────────────────────────────

static_assert(content_addressed_depth_v<Msg> == 0);
static_assert(content_addressed_depth_v<int> == 0);
static_assert(content_addressed_depth_v<ContentAddressed<Msg>> == 1);
static_assert(content_addressed_depth_v<
    ContentAddressed<ContentAddressed<Msg>>> == 2);
static_assert(content_addressed_depth_v<
    ContentAddressed<ContentAddressed<ContentAddressed<Msg>>>> == 3);

// ─── is_subsort: mutual interchangeability ────────────────────────

static_assert( is_subsort_v<ContentAddressed<Msg>, Msg>);
static_assert( is_subsort_v<Msg, ContentAddressed<Msg>>);

// Reflexivity via the primary is_subsort<T, T>.
static_assert( is_subsort_v<ContentAddressed<Msg>, ContentAddressed<Msg>>);
static_assert( is_subsort_v<Msg, Msg>);

// Unrelated pairs are NOT subsortable (primary returns false).
static_assert(!is_subsort_v<ContentAddressed<Msg>, Ack>);
static_assert(!is_subsort_v<Msg, Ack>);
static_assert(!is_subsort_v<ContentAddressed<Msg>, ContentAddressed<Ack>>);

// ─── Session-type protocol-level integration ─────────────────────

// Send is covariant in payload — so Send<ContentAddressed<Msg>, K>
// and Send<Msg, K> are mutual subtypes.
static_assert(is_subtype_sync_v<
    Send<ContentAddressed<Msg>, End>,
    Send<Msg, End>>);
static_assert(is_subtype_sync_v<
    Send<Msg, End>,
    Send<ContentAddressed<Msg>, End>>);

// Equivalence (sync): both directions hold.
static_assert(equivalent_sync_v<
    Send<ContentAddressed<Msg>, End>,
    Send<Msg, End>>);

// Recv is contravariant in payload.  Mutual interchangeability
// means both directions hold for Recv too.
static_assert(is_subtype_sync_v<
    Recv<ContentAddressed<Msg>, End>,
    Recv<Msg, End>>);
static_assert(is_subtype_sync_v<
    Recv<Msg, End>,
    Recv<ContentAddressed<Msg>, End>>);

static_assert(equivalent_sync_v<
    Recv<ContentAddressed<Msg>, End>,
    Recv<Msg, End>>);

// ─── Composition: wrapping inside Loop/Select/Offer ──────────────

// Loop over a content-addressed payload is a subtype of the raw-
// payload loop, and vice versa.
using CaLoopSend = Loop<Send<ContentAddressed<Msg>, Continue>>;
using RawLoopSend = Loop<Send<Msg, Continue>>;

static_assert(is_subtype_sync_v<CaLoopSend, RawLoopSend>);
static_assert(is_subtype_sync_v<RawLoopSend, CaLoopSend>);
static_assert(equivalent_sync_v<CaLoopSend, RawLoopSend>);

// Select with mixed branches — a content-addressed branch is
// interchangeable with its raw counterpart.
using SelectMixed = Select<
    Send<ContentAddressed<Msg>, End>,
    Send<Ack, End>>;
using SelectRaw = Select<
    Send<Msg, End>,
    Send<Ack, End>>;

static_assert(is_subtype_sync_v<SelectMixed, SelectRaw>);
static_assert(is_subtype_sync_v<SelectRaw, SelectMixed>);

// ─── Dual preservation ──────────────────────────────────────────
//
// ContentAddressed<T> is a VALUE TYPE, not a session combinator, so
// it's NOT dualised by dual_of.  The wrapper passes through duality
// unchanged — peers see the same wrapper or lack-of-wrapper.

using CaProto    = Send<ContentAddressed<Msg>, End>;
using CaProtoDual = dual_of_t<CaProto>;
static_assert(std::is_same_v<CaProtoDual, Recv<ContentAddressed<Msg>, End>>);

// Involution.
static_assert(std::is_same_v<dual_of_t<CaProtoDual>, CaProto>);

// ─── Well-formedness ────────────────────────────────────────────

static_assert(is_well_formed_v<Send<ContentAddressed<Msg>, End>>);
static_assert(is_well_formed_v<Loop<Send<ContentAddressed<Msg>, Continue>>>);
static_assert(is_well_formed_v<Recv<ContentAddressed<Ack>, End>>);

// ─── Mix with other payload subsort relations ──────────────────
//
// If a user declares an additional subsort relation (e.g., Derived <:
// Base), that relation COMPOSES with the ContentAddressed<> mutual
// interchangeability — sort of.  Actually our framework doesn't
// compose subsort transitively (see SessionSubtype.h's explicit
// note on user-responsibility for transitivity); so we don't
// automatically conclude:
//
//     Send<ContentAddressed<Derived>, End>  ⩽  Send<Base, End>
//
// unless the user EXPLICITLY declares is_subsort<Derived, Base>.
// Our ContentAddressed<T> ↔ T specialisation doesn't create
// compositional closures it shouldn't.

// ─── Canonical worked example: Cipher-like publication ─────────

// Cipher publishes content-addressed snapshots on a Loop.  The
// content-addressed variant is interchangeable with the raw variant
// at the session-type level.

struct CipherSnapshot {};
using CipherPublisher_CA = Loop<Send<ContentAddressed<CipherSnapshot>, Continue>>;
using CipherSubscriber_CA = dual_of_t<CipherPublisher_CA>;

static_assert(std::is_same_v<
    CipherSubscriber_CA,
    Loop<Recv<ContentAddressed<CipherSnapshot>, Continue>>>);
static_assert(is_well_formed_v<CipherPublisher_CA>);
static_assert(is_well_formed_v<CipherSubscriber_CA>);

// Equivalence with the non-dedup variant.
using CipherPublisher_Raw = Loop<Send<CipherSnapshot, Continue>>;
static_assert(equivalent_sync_v<CipherPublisher_CA, CipherPublisher_Raw>);

}  // namespace detail::ca::ca_self_test

}  // namespace crucible::safety::proto
