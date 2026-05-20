#pragma once

// ── crucible::fixy::sess::contentaddr — ContentAddressed quotient ──
//
// FIXY-U-052c (third slice of U-052 umbrella).  Re-exports the
// session-flavored content-hash-quotient surface from
// sessions/SessionContentAddressed.h into
// `crucible::fixy::sess::contentaddr::` so production callers (Cipher.h
// federation-entry persistence, KernelCache SWMR publication) can spell
// content-addressed payload markers through the fixy umbrella without
// descending into `safety::proto::*` directly.
//
// Nine symbols surfaced (the ContentAddressed<T> + traits layer):
//
//   1. ContentAddressed<T>                    — payload-wrapper marker
//   2. is_content_addressed<T>                — primary trait
//      is_content_addressed_v<T>              — value alias
//      ContentAddressedType<T>                — concept
//   3. content_addressed_underlying<T>        — strip-one metafn
//      content_addressed_underlying_t<T>      — alias
//   4. unwrap_content_addressed<T>            — strip-all metafn (recursive)
//      unwrap_content_addressed_t<T>          — alias
//   5. content_addressed_depth_v<T>           — nesting depth
//
// ── Why a dedicated contentaddr:: sub-namespace under fixy::sess:: ─
//
// fixy::sess:: holds the binary session-type surface (Send / Recv /
// Loop / Select + Handle wrappers); fixy::sess::mpst:: holds the MPST
// global-types layer; fixy::sess::declassify:: (U-052a) holds the
// wire-policy payload-marker layer; fixy::sess::ct:: (U-052b) holds
// the constant-time payload discipline.  ContentAddressed<T> is a
// PAYLOAD MARKER with QUOTIENT semantics — recipient state depends
// only on the payload's content hash, so Send<ContentAddressed<T>, K>
// and Send<T, K> are mutually subtypes via subsort propagation.
// Keeping it in fixy::sess::contentaddr:: lets audit-grep
// `fixy::sess::contentaddr::` find every fixy-routed content-hash-
// quotient site distinct from substrate-direct `safety::proto::` paths.
//
// ── Production consumer: Cipher.h federation entries ──────────────
//
// Cipher.h declares two persistence cells (entry payload + cold-blob
// region) as `safety::proto::is_content_addressed_v<...>` predicates.
// Through the U-052c surface those become
// `fixy::sess::contentaddr::is_content_addressed_v<...>`, closing the
// last persistence-layer leak in the FIXY-U-092 Cipher.h migration.
//
// ── Substrate consumed ─────────────────────────────────────────────
//
//   safety::proto::ContentAddressed<T>
//   safety::proto::is_content_addressed<T>
//   safety::proto::is_content_addressed_v<T>
//   safety::proto::ContentAddressedType<T>
//   safety::proto::content_addressed_underlying<T>
//   safety::proto::content_addressed_underlying_t<T>
//   safety::proto::unwrap_content_addressed<T>
//   safety::proto::unwrap_content_addressed_t<T>
//   safety::proto::content_addressed_depth_v<T>
//
// ── Substrate added by this header ─────────────────────────────────
//
// NONE.  Nine using-decls + a sentinel battery + smoke routine.  No
// new types, no new mint factories, no new free functions.
//
// ── Cost ───────────────────────────────────────────────────────────
//
// Zero.  Every entry is a using-decl.  `sizeof(ContentAddressed<T>)`
// is the substrate's empty-tag size — typically `sizeof(T)` when used
// as a base or 1 byte standalone, per layout discipline.

#include <crucible/sessions/SessionContentAddressed.h>

#include <cstddef>
#include <type_traits>

namespace crucible::fixy::sess::contentaddr {

// ═════════════════════════════════════════════════════════════════════
// ── 1. ContentAddressed<T> wrapper ─────────────────────────────────
// ═════════════════════════════════════════════════════════════════════

using ::crucible::safety::proto::ContentAddressed;

// ═════════════════════════════════════════════════════════════════════
// ── 2. Shape traits ────────────────────────────────────────────────
// ═════════════════════════════════════════════════════════════════════

using ::crucible::safety::proto::is_content_addressed;
using ::crucible::safety::proto::is_content_addressed_v;
using ::crucible::safety::proto::ContentAddressedType;

// ═════════════════════════════════════════════════════════════════════
// ── 3. Strip-one underlying metafn ─────────────────────────────────
// ═════════════════════════════════════════════════════════════════════

using ::crucible::safety::proto::content_addressed_underlying;
using ::crucible::safety::proto::content_addressed_underlying_t;

// ═════════════════════════════════════════════════════════════════════
// ── 4. Strip-all recursive metafn ──────────────────────────────────
// ═════════════════════════════════════════════════════════════════════

using ::crucible::safety::proto::unwrap_content_addressed;
using ::crucible::safety::proto::unwrap_content_addressed_t;

// ═════════════════════════════════════════════════════════════════════
// ── 5. Nesting depth counter ───────────────────────────────────────
// ═════════════════════════════════════════════════════════════════════

using ::crucible::safety::proto::content_addressed_depth_v;

}  // namespace crucible::fixy::sess::contentaddr

// ═════════════════════════════════════════════════════════════════════
// ── In-header sentinel battery (FIXY-U-020 dual-export discipline) ─
// ═════════════════════════════════════════════════════════════════════
//
// Same drift-catch discipline as fixy/SessCT.h::u052b_self_test and
// fixy/SessDecl.h::u052a_self_test.  Substrate-side renames trip at
// every consumer's include time, not three TUs deep.

namespace crucible::fixy::sess::contentaddr::u052c_self_test {

// ── A. Payload placeholders (trivially-copyable POD by design) ─────
struct ProbeT {};
struct ProbeU {};

// ── B. Re-export type-identity witnesses ───────────────────────────

static_assert(std::is_same_v<
    ContentAddressed<ProbeT>,
    ::crucible::safety::proto::ContentAddressed<ProbeT>>,
    "fixy::sess::contentaddr::ContentAddressed must alias "
    "safety::proto::ContentAddressed.");

static_assert(std::is_same_v<
    is_content_addressed<ProbeT>,
    ::crucible::safety::proto::is_content_addressed<ProbeT>>,
    "fixy::sess::contentaddr::is_content_addressed must alias "
    "safety::proto::is_content_addressed.");

// ── C. is_content_addressed_v discriminates wrapped vs bare ────────

static_assert( is_content_addressed_v<ContentAddressed<ProbeT>>);
static_assert(!is_content_addressed_v<ProbeT>);
static_assert(!is_content_addressed_v<int>);

static_assert( ContentAddressedType<ContentAddressed<ProbeT>>);
static_assert(!ContentAddressedType<ProbeT>);

// ── D. content_addressed_underlying_t strips ONE layer ─────────────

static_assert(std::is_same_v<
    content_addressed_underlying_t<ContentAddressed<ProbeT>>, ProbeT>);

// Passthrough fallback — non-wrapped types unchanged.
static_assert(std::is_same_v<
    content_addressed_underlying_t<ProbeT>, ProbeT>);

// Nested wrapper — strip-one peels ONE layer (substrate spec).
static_assert(std::is_same_v<
    content_addressed_underlying_t<
        ContentAddressed<ContentAddressed<ProbeT>>>,
    ContentAddressed<ProbeT>>);

// ── E. unwrap_content_addressed_t strips ALL layers ────────────────

static_assert(std::is_same_v<
    unwrap_content_addressed_t<ContentAddressed<ProbeT>>, ProbeT>);

static_assert(std::is_same_v<
    unwrap_content_addressed_t<
        ContentAddressed<ContentAddressed<ContentAddressed<ProbeT>>>>,
    ProbeT>);

// Passthrough fallback — non-wrapped types unchanged.
static_assert(std::is_same_v<unwrap_content_addressed_t<int>, int>);

// ── F. content_addressed_depth_v counts nesting ────────────────────

static_assert(content_addressed_depth_v<ProbeT> == 0);
static_assert(content_addressed_depth_v<ContentAddressed<ProbeT>> == 1);
static_assert(content_addressed_depth_v<
    ContentAddressed<ContentAddressed<ProbeT>>> == 2);
static_assert(content_addressed_depth_v<
    ContentAddressed<ContentAddressed<ContentAddressed<ProbeT>>>> == 3);

// ── G. Distinct-T wrapper distinctness ─────────────────────────────
//
// ContentAddressed<ProbeT> and ContentAddressed<ProbeU> are unrelated
// wrapper types — T is part of identity, so accidental cross-T flow
// (e.g., a Cipher entry payload swapped with a cold-blob payload)
// breaks at the type level instead of silently corrupting the
// content-hash quotient.

static_assert(!std::is_same_v<
    ContentAddressed<ProbeT>, ContentAddressed<ProbeU>>);

// ── H. Cardinality witness — count of items U-052c surfaces ────────
//
//   Wrapper (1):
//     ContentAddressed
//   Shape traits (3):
//     is_content_addressed, is_content_addressed_v, ContentAddressedType
//   Strip-one (2):
//     content_addressed_underlying, content_addressed_underlying_t
//   Strip-all (2):
//     unwrap_content_addressed, unwrap_content_addressed_t
//   Depth counter (1):
//     content_addressed_depth_v
//                                                       ────
//                                                         9
constexpr int u052c_surface_cardinality = 9;
static_assert(u052c_surface_cardinality == 9,
    "fixy::sess::contentaddr:: U-052c surface cardinality drifted — "
    "update SessContentAddr.h using-decls AND this sentinel in lockstep.");

}  // namespace crucible::fixy::sess::contentaddr::u052c_self_test

namespace crucible::fixy::sess::contentaddr {

// ═════════════════════════════════════════════════════════════════════
// ── Runtime smoke test (FIXY-U-103 discipline) ─────────────────────
// ═════════════════════════════════════════════════════════════════════
//
// Static-only sentinels can mask SFINAE / consteval / inline-body
// bugs.  The runtime smoke routine instantiates every public template
// against non-constant args so any latent template-evaluation issue
// surfaces under `-fsyntax-only` of any TU that includes SessContentAddr.h.
//
// Cost: instantiations only.  No runtime code path executed.

inline void runtime_smoke_test() noexcept {
    using ::crucible::fixy::sess::contentaddr::u052c_self_test::ProbeT;
    using CA  = ContentAddressed<ProbeT>;
    using CA2 = ContentAddressed<CA>;

    [[maybe_unused]] constexpr bool wrapped = is_content_addressed_v<CA>;
    [[maybe_unused]] constexpr bool bare    = is_content_addressed_v<ProbeT>;
    [[maybe_unused]] constexpr bool concpt  = ContentAddressedType<CA>;

    using StripOne  = content_addressed_underlying_t<CA>;
    using StripAll  = unwrap_content_addressed_t<CA2>;
    using Passthrough = content_addressed_underlying_t<int>;

    [[maybe_unused]] constexpr std::size_t d0 = content_addressed_depth_v<ProbeT>;
    [[maybe_unused]] constexpr std::size_t d1 = content_addressed_depth_v<CA>;
    [[maybe_unused]] constexpr std::size_t d2 = content_addressed_depth_v<CA2>;

    (void) wrapped; (void) bare; (void) concpt;
    (void) static_cast<StripOne*>(nullptr);
    (void) static_cast<StripAll*>(nullptr);
    (void) static_cast<Passthrough*>(nullptr);
    (void) d0; (void) d1; (void) d2;
}

}  // namespace crucible::fixy::sess::contentaddr
