#pragma once

// ═══════════════════════════════════════════════════════════════════
// crucible::safety::proto — L2 typing context Γ (SEPLOG-I1, task #343)
//
// The typing context Γ is a compile-time finite map from
// (session_tag, role_tag) pairs to LOCAL session types.  Every
// downstream session-type layer depends on it:
//
//   * L5 Association (SessionAssoc.h, task #345) — Δ ⊑_s G iterates
//     Δ's entries, looks up each (session, role), compares the local
//     type against the projection of G onto that role.
//   * L7 Parametric safety φ (SessionSafety.h, task #346) — φ
//     predicates are defined over Γ; reachable-contexts enumeration
//     walks Γ's reduction graph.
//   * L8 Crash-stop (SessionCrash.h, task #347) — mandatory-crash-
//     branch enumeration iterates Γ's entries and checks every Offer
//     from an unreliable peer has a crash-handling branch.
//   * L9 CSL × session (`PermissionedSession.h`, FOUND-C v1) —
//     `is_permission_balanced_v<Γ, InitialPerms>` ships standalone;
//     conjunction with `is_safe_v` deferred until L7 (Task #346).
//
// This layer ships the STRUCTURE only (no reduction, no event-based
// advance, no reachable-contexts BFS — those arrive with the layers
// that need them).  The operations here are:
//
//     Entry<S, R, T>               a single triple
//     Context<Entries...>          a typing context (disjoint keys)
//     EmptyContext                 Context<>
//
//     compose_context_t<Γ1, Γ2>    disjoint-union; fails on overlap
//     lookup_context_t<Γ, S, R>    fetch the local type for (S, R)
//     contains_key_v<Γ, S, R>      boolean presence test
//     domain_of_t<Γ>               KeySet<Key<S, R>...> of all keys
//     update_entry_t<Γ, S, R, T>   replace (S, R)'s local type with T
//     remove_entry_t<Γ, S, R>      delete (S, R)'s entry
//     context_size_v<Γ>            number of entries
//     is_empty_context_v<Γ>        Γ == Context<>
//
// ─── Design invariants ────────────────────────────────────────────
//
// (INV-1) Keys are pairs (session_tag, role_tag).  Two entries with
//         the same session tag but different roles are distinct (the
//         canonical case: a TraceRing has Producer and Consumer
//         entries with the same session tag).  Two entries with the
//         same role but different sessions are also distinct (one
//         role playing across multiple sessions).  Two entries with
//         identical (session, role) are NOT allowed — that would be
//         ambiguous, and the Context constructor's static_assert
//         rejects it.
//
// (INV-2) Composition via compose_context_t<Γ1, Γ2> requires the key
//         sets of Γ1 and Γ2 to be disjoint.  This is CSL's frame
//         rule lifted to typing contexts.
//
// (INV-3) Every operation is a pure type-level metafunction.  Zero
//         runtime cost.  The types are markers; Context<Es...> has
//         no runtime state.
//
// ─── Worked example ────────────────────────────────────────────────
//
//   struct TraceRingSession {};
//   struct Producer {};
//   struct Consumer {};
//
//   using ProducerT = Loop<Send<TraceEntry, Continue>>;
//   using ConsumerT = Loop<Recv<TraceEntry, Continue>>;
//
//   using TraceRingΓ = Context<
//       Entry<TraceRingSession, Producer, ProducerT>,
//       Entry<TraceRingSession, Consumer, ConsumerT>
//   >;
//
//   static_assert(context_size_v<TraceRingΓ> == 2);
//   static_assert(std::is_same_v<
//       lookup_context_t<TraceRingΓ, TraceRingSession, Producer>,
//       ProducerT>);
//   static_assert(contains_key_v<TraceRingΓ, TraceRingSession, Consumer>);
//
// Two sessions, compose disjointly:
//
//   struct KernelCacheSession {};
//   struct Writer {};
//   struct Reader {};
//
//   using KernelΓ = Context<
//       Entry<KernelCacheSession, Writer, WriterT>,
//       Entry<KernelCacheSession, Reader, ReaderT>
//   >;
//
//   using CombinedΓ = compose_context_t<TraceRingΓ, KernelΓ>;
//   static_assert(context_size_v<CombinedΓ> == 4);
//
// Attempted overlapping composition is a compile error:
//
//   using BadCombined = compose_context_t<TraceRingΓ, TraceRingΓ>;
//   // ^ static_assert fires: "Γ1 and Γ2 share one or more keys"
//
// ─── References ────────────────────────────────────────────────────
//
//   Scalas-Yoshida 2019, "Less is More: Multiparty Session Types
//     Revisited" — Γ structure and the typing judgment
//     Θ · Γ ⊢ P with φ(Γ) that every downstream layer builds on.
//   Hou-Yoshida-Kuhn 2024, "Less is More Revisited" — the subject-
//     reduction fix via association Δ ⊑_s G consumes this Context
//     via lookup_context_t + domain_of_t.
//   Honda-Yoshida-Carbone 2008 / JACM16 — multiparty session types;
//     Γ's multi-participant structure traces back here.
// ═══════════════════════════════════════════════════════════════════

#include <crucible/Platform.h>

#include <algorithm>
#include <array>
#include <compare>
#include <cstddef>
#include <cstdint>
#include <string_view>
#include <type_traits>

namespace crucible::safety::proto {

// ═════════════════════════════════════════════════════════════════════
// ── Entry<SessionTag, RoleTag, LocalType> ─────────────────────────
// ═════════════════════════════════════════════════════════════════════
//
// A single triple:  (session, role) ↦ local_type.  Pure type marker;
// zero runtime footprint.  The nested aliases expose each field for
// metaprogramming:
//
//     using entry = Entry<MySession, MyRole, Loop<Send<Msg, Continue>>>;
//     using s     = typename entry::session;     // MySession
//     using r     = typename entry::role;        // MyRole
//     using t     = typename entry::local_type;  // Loop<Send<...>>

template <typename SessionTag, typename RoleTag, typename LocalType>
struct Entry {
    using session    = SessionTag;
    using role       = RoleTag;
    using local_type = LocalType;
};

// ═════════════════════════════════════════════════════════════════════
// ── Distinctness check (all_keys_distinct_v) ───────────────────────
// ═════════════════════════════════════════════════════════════════════
//
// Two entries share a key iff both session and role are identical.
// (Having the same session tag with different roles is NOT a key
// collision — that's the canonical multi-role-per-session pattern.)
//
// Implementation (#373 SEPLOG-PERF-3): O(N log N) consteval sort-and-
// scan over a hash-projected key array.  Replaces the previous
// O(N²) recursive `head_key_fresh_v` template-instantiation chain
// (every prefix's head conjoined against its suffix).  At Γ sizes
// of tens of entries the new path is one consteval call instead of
// a quadratic instantiation tree; at ~100 entries the
// instantiation-count saving is ~10×.
//
// Key collision robustness: the composite EntryKey holds two 64-bit
// hashes (session, role).  Probability of false-positive collision
// across distinct (session, role) pairs is ~1/2^128 — same risk
// profile as the framework's other content-hashing paths
// (default_schema_hash, KernelCache content_hash).  Practical Γ
// hand-written by users are immune.

namespace detail::ctx {

// FNV-1a 64-bit hash of a string view at consteval.
[[nodiscard]] inline consteval std::uint64_t fnv1a_64(std::string_view s) noexcept {
    constexpr std::uint64_t kFnvOffsetBasis = 0xcbf29ce484222325ULL;
    constexpr std::uint64_t kFnvPrime       = 0x100000001b3ULL;
    std::uint64_t h = kFnvOffsetBasis;
    for (char c : s) {
        h ^= static_cast<std::uint64_t>(static_cast<unsigned char>(c));
        h *= kFnvPrime;
    }
    return h;
}

// Per-type stable 64-bit identifier.  __PRETTY_FUNCTION__ inside the
// template includes T's mangled spelling — same T → same string →
// same hash across TUs of the same build.  Cross-build / cross-
// platform identity is NOT promised (mangling differs); session-
// distinctness is checked within one TU's compile, so this is
// sufficient.
template <typename T>
[[nodiscard]] inline consteval std::uint64_t type_id_hash_for() noexcept {
    return fnv1a_64(std::string_view{__PRETTY_FUNCTION__});
}

template <typename T>
inline constexpr std::uint64_t type_id_hash_v = type_id_hash_for<T>();

// Composite key for an Entry: (session_hash, role_hash).  Defaulted
// <=> gives lexicographic ordering for std::sort; defaulted == does
// component-wise equality for the adjacent-duplicate scan.
struct EntryKey {
    std::uint64_t session_hash{};
    std::uint64_t role_hash{};
    constexpr auto operator<=>(const EntryKey&) const noexcept = default;
    constexpr bool operator==(const EntryKey&)  const noexcept = default;
};

// O(N log N) distinctness: project each entry to its EntryKey,
// sort, scan adjacent for duplicates.
template <typename... Entries>
[[nodiscard]] inline consteval bool all_keys_distinct_impl() noexcept {
    constexpr std::size_t N = sizeof...(Entries);
    if constexpr (N <= 1) {
        return true;
    } else {
        std::array<EntryKey, N> keys{
            EntryKey{
                type_id_hash_v<typename Entries::session>,
                type_id_hash_v<typename Entries::role>
            }...
        };
        std::ranges::sort(keys);
        for (std::size_t i = 1; i < N; ++i) {
            if (keys[i - 1] == keys[i]) return false;
        }
        return true;
    }
}

template <typename... Entries>
inline constexpr bool all_keys_distinct_v = all_keys_distinct_impl<Entries...>();

// "dependent_false" — always false, but dependent on the template
// parameter pack so it only fires on template instantiation (not at
// definition time).  Used in partial specialisations that represent
// a user-error path.
template <typename...>
inline constexpr bool dependent_false_v = false;

}  // namespace detail::ctx

// ═════════════════════════════════════════════════════════════════════
// ── Context<Entries...> ────────────────────────────────────────────
// ═════════════════════════════════════════════════════════════════════
//
// A compile-time finite map (session, role) → local_type, encoded as a
// type-level list of Entries with a pairwise-distinct-keys invariant
// enforced by static_assert at construction.

template <typename... Entries>
struct Context {
    static_assert(detail::ctx::all_keys_distinct_v<Entries...>,
        "crucible::session::diagnostic [Context_Domain_Collision]: "
        "Context<Entries...>: entries must have pairwise-distinct "
        "(session, role) keys.  Two entries with the SAME session and "
        "role is a typing-context collision.  (Two entries with the "
        "same session but different roles, or the same role but "
        "different sessions, are both allowed — that's the canonical "
        "multi-role-per-session and multi-session patterns.)");

    static constexpr std::size_t size = sizeof...(Entries);
};

using EmptyContext = Context<>;

// context_size_v<Γ> — number of entries in Γ.
template <typename Γ>
inline constexpr std::size_t context_size_v = Γ::size;

// is_empty_context_v<Γ> — true iff Γ == Context<>.
template <typename Γ>
inline constexpr bool is_empty_context_v = (context_size_v<Γ> == 0);

// ═════════════════════════════════════════════════════════════════════
// ── compose_context_t<Γ1, Γ2> ──────────────────────────────────────
// ═════════════════════════════════════════════════════════════════════
//
// Disjoint union of two typing contexts.  Fails with static_assert if
// Γ1 and Γ2 overlap on any (session, role) key.  This is CSL's frame
// rule lifted to typing contexts — disjoint contexts compose without
// interference.

template <typename Γ1, typename Γ2>
struct ComposeContext;

template <typename... E1, typename... E2>
struct ComposeContext<Context<E1...>, Context<E2...>> {
    static_assert(detail::ctx::all_keys_distinct_v<E1..., E2...>,
        "crucible::session::diagnostic [Context_Domain_Collision]: "
        "compose_context_t<Γ1, Γ2>: Γ1 and Γ2 share one or more "
        "(session, role) keys.  CSL's frame rule requires disjoint "
        "contexts; resolve by renaming one side's session tag, "
        "giving different roles within a shared session, or lifting "
        "the shared entry into a common prefix before composition.");

    using type = Context<E1..., E2...>;
};

template <typename Γ1, typename Γ2>
using compose_context_t = typename ComposeContext<Γ1, Γ2>::type;

// ═════════════════════════════════════════════════════════════════════
// ── contains_key_v<Γ, SessionTag, RoleTag> ─────────────────────────
// ═════════════════════════════════════════════════════════════════════
//
// Boolean: does Γ have an entry for (SessionTag, RoleTag)?  O(|Γ|)
// fold-expression scan.

namespace detail::ctx {

template <typename Γ, typename S, typename R>
struct contains_key;

template <typename S, typename R>
struct contains_key<Context<>, S, R> : std::false_type {};

template <typename... Es, typename S, typename R>
struct contains_key<Context<Es...>, S, R>
    : std::bool_constant<
          ((std::is_same_v<typename Es::session, S> &&
            std::is_same_v<typename Es::role,    R>) || ...)
      > {};

}  // namespace detail::ctx

template <typename Γ, typename SessionTag, typename RoleTag>
inline constexpr bool contains_key_v =
    detail::ctx::contains_key<Γ, SessionTag, RoleTag>::value;

// ═════════════════════════════════════════════════════════════════════
// ── lookup_context_t<Γ, SessionTag, RoleTag> ───────────────────────
// ═════════════════════════════════════════════════════════════════════
//
// Fetch the local_type for (SessionTag, RoleTag).  Emits a static_assert
// if no matching entry exists — diagnostic names the failure at the
// call site rather than producing a "no type named 'type'" template
// instantiation noise.
//
// Implementation: recursive partial specialisation.  The "match" spec
// (head entry's key equals the requested key) is more specialised
// than the "skip" spec (head entry's key differs), so C++'s partial-
// ordering rule picks the match spec when applicable.

template <typename Γ, typename SessionTag, typename RoleTag>
struct LookupContext;

// Empty context — no matching entry.  Emit a clear diagnostic.
template <typename S, typename R>
struct LookupContext<Context<>, S, R> {
    static_assert(detail::ctx::dependent_false_v<S, R>,
        "crucible::session::diagnostic [Context_Lookup_Miss]: "
        "lookup_context_t<Γ, S, R>: Γ has no entry for (S, R).  "
        "Use contains_key_v<Γ, S, R> to test presence before lookup, "
        "or check the (S, R) you're querying matches the Γ you're "
        "querying.");
};

// Match: head entry has the requested key.  Stop, return its local_type.
template <typename S, typename R, typename T, typename... Rest>
struct LookupContext<Context<Entry<S, R, T>, Rest...>, S, R> {
    using type = T;
};

// Skip: head entry has a different key — recurse on tail.  This is
// LESS specialised than the match spec (any Head, not pinned to S/R),
// so the compiler picks the match spec when applicable.
template <typename Head, typename... Rest, typename S, typename R>
struct LookupContext<Context<Head, Rest...>, S, R>
    : LookupContext<Context<Rest...>, S, R> {};

template <typename Γ, typename SessionTag, typename RoleTag>
using lookup_context_t =
    typename LookupContext<Γ, SessionTag, RoleTag>::type;

// ═════════════════════════════════════════════════════════════════════
// ── domain_of_t<Γ> ─────────────────────────────────────────────────
// ═════════════════════════════════════════════════════════════════════
//
// Project Γ onto its key set: a KeySet<Key<S, R>...> with one Key per
// entry.  Used by downstream layers that iterate over Γ's domain
// (L5 association, L8 crash-branch enumeration).

// Single key — the (session, role) pair extracted from an Entry.
template <typename SessionTag, typename RoleTag>
struct Key {
    using session = SessionTag;
    using role    = RoleTag;
};

// A compile-time set of Keys.  Set (not list) semantically because Γ's
// distinctness invariant guarantees no duplicates; we store as a
// parameter pack for convenient iteration.
template <typename... Ks>
struct KeySet {
    static constexpr std::size_t size = sizeof...(Ks);
};

template <typename Γ>
struct DomainOf;

template <typename... Es>
struct DomainOf<Context<Es...>> {
    using type = KeySet<Key<typename Es::session, typename Es::role>...>;
};

template <typename Γ>
using domain_of_t = typename DomainOf<Γ>::type;

// ═════════════════════════════════════════════════════════════════════
// ── update_entry_t<Γ, SessionTag, RoleTag, NewT> ───────────────────
// ═════════════════════════════════════════════════════════════════════
//
// Replace the local_type of the entry for (SessionTag, RoleTag) with
// NewT.  Leaves other entries unchanged.  Compile-time error if no
// matching entry exists.  Used by L7 reduction (advance one role's
// local type past a communication event) and by protocol refinement
// (upgrade one role's local type to a more precise subtype).

namespace detail::ctx {

template <typename E, typename S, typename R, typename NewT>
struct rewrite_entry_impl {
    using type = E;  // key doesn't match — keep as-is
};

template <typename S, typename R, typename OldT, typename NewT>
struct rewrite_entry_impl<Entry<S, R, OldT>, S, R, NewT> {
    using type = Entry<S, R, NewT>;  // key matches — substitute
};

template <typename E, typename S, typename R, typename NewT>
using rewrite_entry_t = typename rewrite_entry_impl<E, S, R, NewT>::type;

}  // namespace detail::ctx

template <typename Γ, typename SessionTag, typename RoleTag, typename NewT>
struct UpdateEntry;

template <typename... Es, typename S, typename R, typename NewT>
struct UpdateEntry<Context<Es...>, S, R, NewT> {
    static_assert(contains_key_v<Context<Es...>, S, R>,
        "crucible::session::diagnostic [Context_Lookup_Miss]: "
        "update_entry_t<Γ, S, R, NewT>: Γ has no entry for (S, R) to "
        "update.  Use compose_context_t to introduce a new entry, "
        "or query contains_key_v<Γ, S, R> first.");

    using type = Context<detail::ctx::rewrite_entry_t<Es, S, R, NewT>...>;
};

template <typename Γ, typename SessionTag, typename RoleTag, typename NewT>
using update_entry_t =
    typename UpdateEntry<Γ, SessionTag, RoleTag, NewT>::type;

// ═════════════════════════════════════════════════════════════════════
// ── remove_entry_t<Γ, SessionTag, RoleTag> ─────────────────────────
// ═════════════════════════════════════════════════════════════════════
//
// Remove the entry for (SessionTag, RoleTag) from Γ.  Compile-time
// error if no matching entry exists.  Used by L8 crash-stop: when a
// peer dies, its entry is removed from the surviving Γ (the remaining
// participants' context no longer contains the crashed peer).

namespace detail::ctx {

// Split a parameter pack into two Contexts:  entries matching (S, R)
// become Context<MatchedEntries...>; non-matching become
// Context<OtherEntries...>.  Since keys are distinct, the match list
// has at most one entry.  We use the "other" list as the result.

template <typename S, typename R, typename Acc, typename... Remaining>
struct remove_fold;

// Base: no more remaining entries.
template <typename S, typename R, typename... Acc>
struct remove_fold<S, R, Context<Acc...>> {
    using type = Context<Acc...>;
};

// Match: head entry's key matches — skip it, recurse.
template <typename S, typename R, typename... Acc,
          typename T, typename... Rest>
struct remove_fold<S, R, Context<Acc...>, Entry<S, R, T>, Rest...> {
    using type = typename remove_fold<S, R, Context<Acc...>, Rest...>::type;
};

// Non-match: head entry's key differs — append to Acc, recurse.
template <typename S, typename R, typename... Acc,
          typename Head, typename... Rest>
struct remove_fold<S, R, Context<Acc...>, Head, Rest...> {
    using type = typename remove_fold<S, R, Context<Acc..., Head>, Rest...>::type;
};

}  // namespace detail::ctx

template <typename Γ, typename SessionTag, typename RoleTag>
struct RemoveEntry;

template <typename... Es, typename S, typename R>
struct RemoveEntry<Context<Es...>, S, R> {
    static_assert(contains_key_v<Context<Es...>, S, R>,
        "crucible::session::diagnostic [Context_Lookup_Miss]: "
        "remove_entry_t<Γ, S, R>: Γ has no entry for (S, R) to remove.  "
        "Check the key you're removing was present in Γ; the error "
        "is a strict precondition to keep the removal idempotent.");

    using type = typename detail::ctx::remove_fold<
        S, R, Context<>, Es...>::type;
};

template <typename Γ, typename SessionTag, typename RoleTag>
using remove_entry_t =
    typename RemoveEntry<Γ, SessionTag, RoleTag>::type;

// ═════════════════════════════════════════════════════════════════════
// ── Framework self-test static_asserts ─────────────────────────────
// ═════════════════════════════════════════════════════════════════════
//
// Verify every context operation at compile time.  Runs at header-
// inclusion time; any regression to the metafunctions above fails at
// the first TU that pulls us in.

#ifdef CRUCIBLE_SESSION_SELF_TESTS
namespace detail::ctx::context_self_test {

// Fixture tags.
struct TraceRingSession      {};
struct KernelCacheSession    {};
struct Producer              {};
struct Consumer              {};
struct Writer                {};
struct Reader                {};

// Fixture local types (concrete session-type expressions are fine; we
// only care about type identity here, so placeholders work).
struct ProducerT {};
struct ConsumerT {};
struct WriterT   {};
struct ReaderT   {};
struct NewProducerT {};

using TraceRingΓ = Context<
    Entry<TraceRingSession, Producer, ProducerT>,
    Entry<TraceRingSession, Consumer, ConsumerT>>;

using KernelΓ = Context<
    Entry<KernelCacheSession, Writer, WriterT>,
    Entry<KernelCacheSession, Reader, ReaderT>>;

// ─── Entry ─────────────────────────────────────────────────────────

using SampleEntry = Entry<TraceRingSession, Producer, ProducerT>;
static_assert(std::is_same_v<typename SampleEntry::session,    TraceRingSession>);
static_assert(std::is_same_v<typename SampleEntry::role,       Producer>);
static_assert(std::is_same_v<typename SampleEntry::local_type, ProducerT>);

// ─── Context: size, emptiness ──────────────────────────────────────

static_assert(context_size_v<EmptyContext> == 0);
static_assert(context_size_v<TraceRingΓ>   == 2);
static_assert(context_size_v<KernelΓ>      == 2);

static_assert( is_empty_context_v<EmptyContext>);
static_assert(!is_empty_context_v<TraceRingΓ>);

// ─── Distinctness allows same session + different role ─────────────
// (Both TraceRingΓ entries share TraceRingSession but differ on role.)
static_assert(context_size_v<TraceRingΓ> == 2);  // compiles = distinct

// Also allows same role + different session.
using Same_Role_Γ = Context<
    Entry<TraceRingSession,   Producer, ProducerT>,
    Entry<KernelCacheSession, Producer, WriterT>>;
static_assert(context_size_v<Same_Role_Γ> == 2);

// ─── compose_context_t ─────────────────────────────────────────────

using CombinedΓ = compose_context_t<TraceRingΓ, KernelΓ>;
static_assert(context_size_v<CombinedΓ> == 4);

// Composition is associative (structural — same entry order).
using Assoc1 = compose_context_t<compose_context_t<TraceRingΓ, KernelΓ>, EmptyContext>;
using Assoc2 = compose_context_t<TraceRingΓ, compose_context_t<KernelΓ, EmptyContext>>;
static_assert(std::is_same_v<Assoc1, Assoc2>);

// Identity: composing with EmptyContext is a no-op.
static_assert(std::is_same_v<
    compose_context_t<TraceRingΓ, EmptyContext>, TraceRingΓ>);
static_assert(std::is_same_v<
    compose_context_t<EmptyContext, TraceRingΓ>, TraceRingΓ>);

// ─── contains_key_v ────────────────────────────────────────────────

static_assert( contains_key_v<TraceRingΓ, TraceRingSession, Producer>);
static_assert( contains_key_v<TraceRingΓ, TraceRingSession, Consumer>);
static_assert(!contains_key_v<TraceRingΓ, TraceRingSession, Writer>);
static_assert(!contains_key_v<TraceRingΓ, KernelCacheSession, Producer>);

static_assert(!contains_key_v<EmptyContext, TraceRingSession, Producer>);

// Composed context contains keys from both sides.
static_assert( contains_key_v<CombinedΓ, TraceRingSession, Producer>);
static_assert( contains_key_v<CombinedΓ, KernelCacheSession, Reader>);

// ─── lookup_context_t ──────────────────────────────────────────────

static_assert(std::is_same_v<
    lookup_context_t<TraceRingΓ, TraceRingSession, Producer>, ProducerT>);
static_assert(std::is_same_v<
    lookup_context_t<TraceRingΓ, TraceRingSession, Consumer>, ConsumerT>);

// Lookup in composed Γ: matches either side.
static_assert(std::is_same_v<
    lookup_context_t<CombinedΓ, TraceRingSession, Producer>, ProducerT>);
static_assert(std::is_same_v<
    lookup_context_t<CombinedΓ, KernelCacheSession, Writer>, WriterT>);

// ─── domain_of_t ───────────────────────────────────────────────────

using TraceRingDomain = domain_of_t<TraceRingΓ>;
static_assert(TraceRingDomain::size == 2);
static_assert(std::is_same_v<
    TraceRingDomain,
    KeySet<Key<TraceRingSession, Producer>, Key<TraceRingSession, Consumer>>>);

using EmptyDomain = domain_of_t<EmptyContext>;
static_assert(EmptyDomain::size == 0);
static_assert(std::is_same_v<EmptyDomain, KeySet<>>);

// Composition preserves domain concatenation.
using CombinedDomain = domain_of_t<CombinedΓ>;
static_assert(CombinedDomain::size == 4);

// ─── update_entry_t ────────────────────────────────────────────────

using UpdatedΓ = update_entry_t<TraceRingΓ, TraceRingSession, Producer, NewProducerT>;
// Updated entry has new local_type; other entries unchanged.
static_assert(std::is_same_v<
    lookup_context_t<UpdatedΓ, TraceRingSession, Producer>, NewProducerT>);
static_assert(std::is_same_v<
    lookup_context_t<UpdatedΓ, TraceRingSession, Consumer>, ConsumerT>);
// Same size (update, not insert).
static_assert(context_size_v<UpdatedΓ> == context_size_v<TraceRingΓ>);

// Update preserves domain.
static_assert(std::is_same_v<
    domain_of_t<UpdatedΓ>, domain_of_t<TraceRingΓ>>);

// Update with the same type is a no-op.
using RewrittenIdent = update_entry_t<TraceRingΓ, TraceRingSession, Producer, ProducerT>;
static_assert(std::is_same_v<RewrittenIdent, TraceRingΓ>);

// ─── remove_entry_t ────────────────────────────────────────────────

using RemovedProducer = remove_entry_t<TraceRingΓ, TraceRingSession, Producer>;
static_assert(context_size_v<RemovedProducer> == 1);
static_assert( contains_key_v<RemovedProducer, TraceRingSession, Consumer>);
static_assert(!contains_key_v<RemovedProducer, TraceRingSession, Producer>);
static_assert(std::is_same_v<
    lookup_context_t<RemovedProducer, TraceRingSession, Consumer>, ConsumerT>);

// Remove both entries — results in EmptyContext.
using RemovedBoth = remove_entry_t<
    remove_entry_t<TraceRingΓ, TraceRingSession, Producer>,
    TraceRingSession, Consumer>;
static_assert(is_empty_context_v<RemovedBoth>);
static_assert(std::is_same_v<RemovedBoth, EmptyContext>);

// Remove from composed: removes only the matching entry; others remain.
using CombinedMinusWriter =
    remove_entry_t<CombinedΓ, KernelCacheSession, Writer>;
static_assert(context_size_v<CombinedMinusWriter> == 3);
static_assert( contains_key_v<CombinedMinusWriter, KernelCacheSession, Reader>);
static_assert(!contains_key_v<CombinedMinusWriter, KernelCacheSession, Writer>);

// ─── Round-trip invariants ────────────────────────────────────────
//
// For every (S, R) in domain_of_t<Γ>:
//   1. contains_key_v<Γ, S, R> is true
//   2. lookup_context_t<Γ, S, R> is some T
//   3. update_entry_t<Γ, S, R, T> leaves Γ unchanged (idempotence)
//
// Exercised above on (TraceRingSession, Producer).  The remove-then-
// reinsert pair:
//   remove_entry_t<Γ, S, R> does NOT contain (S, R), but composing it
//   with Context<Entry<S, R, OldT>> must reproduce something with the
//   same domain as Γ (order may differ; we verify size + presence).

using RemovedThenReadded = compose_context_t<
    remove_entry_t<TraceRingΓ, TraceRingSession, Producer>,
    Context<Entry<TraceRingSession, Producer, ProducerT>>>;
static_assert(context_size_v<RemovedThenReadded> == context_size_v<TraceRingΓ>);
static_assert(contains_key_v<RemovedThenReadded, TraceRingSession, Producer>);
static_assert(contains_key_v<RemovedThenReadded, TraceRingSession, Consumer>);

}  // namespace detail::ctx::context_self_test
#endif  // CRUCIBLE_SESSION_SELF_TESTS

}  // namespace crucible::safety::proto
