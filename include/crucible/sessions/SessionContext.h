#pragma once

#include <crucible/Platform.h>
#include <crucible/permissions/_PermSet.h>
#include <crucible/sessions/Session.h>
#include <crucible/sessions/SessionCrash.h>
#include <crucible/sessions/SessionPermPayloads.h>

#include <algorithm>
#include <array>
#include <compare>
#include <cstddef>
#include <cstdint>
#include <string_view>
#include <type_traits>

namespace crucible::safety::proto {

template <typename SessionTag, typename RoleTag, typename LocalType>
struct Entry {
    using session = SessionTag;
    using role = RoleTag;
    using local_type = LocalType;
};

// Distinctness compares hashes, not types, so two distinct keys whose two
// 64-bit hashes both collide would be reported as duplicates.

namespace detail::ctx {

[[nodiscard]] inline consteval std::uint64_t fnv1a_64(std::string_view s) noexcept {
    constexpr std::uint64_t kFnvOffsetBasis = 0xcbf29ce484222325ULL;
    constexpr std::uint64_t kFnvPrime = 0x100000001b3ULL;
    std::uint64_t h = kFnvOffsetBasis;
    for (char c : s) {
        h ^= static_cast<std::uint64_t>(static_cast<unsigned char>(c));
        h *= kFnvPrime;
    }
    return h;
}

// __PRETTY_FUNCTION__ carries T's spelling, so the same T hashes the same way
// across translation units of one build.  The spelling depends on the compiler,
// so the value is not stable across builds or platforms.  Distinctness is only
// ever asked within a single compile, so that is enough.
template <typename T>
[[nodiscard]] inline consteval std::uint64_t type_id_hash_for() noexcept {
    return fnv1a_64(std::string_view{__PRETTY_FUNCTION__});
}

template <typename T>
inline constexpr std::uint64_t type_id_hash_v = type_id_hash_for<T>();

struct EntryKey {
    std::uint64_t session_hash{};
    std::uint64_t role_hash{};
    constexpr auto operator<=>(const EntryKey&) const noexcept = default;
    constexpr bool operator==(const EntryKey&) const noexcept = default;
};

template <typename... Entries>
[[nodiscard]] inline consteval bool all_keys_distinct_impl() noexcept {
    constexpr std::size_t N = sizeof...(Entries);
    if constexpr (N <= 1) {
        return true;
    } else {
        std::array<EntryKey, N> keys{
            EntryKey{type_id_hash_v<typename Entries::session>, type_id_hash_v<typename Entries::role>}...};
        std::ranges::sort(keys);
        for (std::size_t i = 1; i < N; ++i) {
            if (keys[i - 1] == keys[i]) return false;
        }
        return true;
    }
}

template <typename... Entries>
inline constexpr bool all_keys_distinct_v = all_keys_distinct_impl<Entries...>();

// Always false, but dependent on the pack, so an error path written as a
// partial specialisation fires only when that specialisation is instantiated.
template <typename...>
inline constexpr bool dependent_false_v = false;

}  // namespace detail::ctx

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

template <typename Γ>
inline constexpr std::size_t context_size_v = Γ::size;

template <typename Γ>
inline constexpr bool is_empty_context_v = (context_size_v<Γ> == 0);

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

namespace detail::ctx {

template <typename Γ, typename S, typename R>
struct contains_key;

template <typename S, typename R>
struct contains_key<Context<>, S, R> : std::false_type {};

template <typename... Es, typename S, typename R>
struct contains_key<Context<Es...>, S, R>
    : std::bool_constant<((std::is_same_v<typename Es::session, S> && std::is_same_v<typename Es::role, R>) || ...)> {};

}  // namespace detail::ctx

template <typename Γ, typename SessionTag, typename RoleTag>
inline constexpr bool contains_key_v = detail::ctx::contains_key<Γ, SessionTag, RoleTag>::value;

template <typename Γ, typename SessionTag, typename RoleTag>
struct LookupContext;

template <typename S, typename R>
struct LookupContext<Context<>, S, R> {
    static_assert(detail::ctx::dependent_false_v<S, R>, "crucible::session::diagnostic [Context_Lookup_Miss]: "
                                                        "lookup_context_t<Γ, S, R>: Γ has no entry for (S, R).  "
                                                        "Use contains_key_v<Γ, S, R> to test presence before lookup, "
                                                        "or check the (S, R) you're querying matches the Γ you're "
                                                        "querying.");
};

template <typename S, typename R, typename T, typename... Rest>
struct LookupContext<Context<Entry<S, R, T>, Rest...>, S, R> {
    using type = T;
};

// This specialisation is the less specialised of the two, so partial ordering
// selects the one above whenever the head key matches.
template <typename Head, typename... Rest, typename S, typename R>
struct LookupContext<Context<Head, Rest...>, S, R> : LookupContext<Context<Rest...>, S, R> {};

template <typename Γ, typename SessionTag, typename RoleTag>
using lookup_context_t = typename LookupContext<Γ, SessionTag, RoleTag>::type;

template <typename SessionTag, typename RoleTag>
struct Key {
    using session = SessionTag;
    using role = RoleTag;
};

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

namespace detail::ctx {

template <typename E, typename S, typename R, typename NewT>
struct rewrite_entry_impl {
    using type = E;
};

template <typename S, typename R, typename OldT, typename NewT>
struct rewrite_entry_impl<Entry<S, R, OldT>, S, R, NewT> {
    using type = Entry<S, R, NewT>;
};

template <typename E, typename S, typename R, typename NewT>
using rewrite_entry_t = typename rewrite_entry_impl<E, S, R, NewT>::type;

}  // namespace detail::ctx

template <typename Γ, typename SessionTag, typename RoleTag, typename NewT>
struct UpdateEntry;

template <typename... Es, typename S, typename R, typename NewT>
struct UpdateEntry<Context<Es...>, S, R, NewT> {
    static_assert(contains_key_v<Context<Es...>, S, R>, "crucible::session::diagnostic [Context_Lookup_Miss]: "
                                                        "update_entry_t<Γ, S, R, NewT>: Γ has no entry for (S, R) to "
                                                        "update.  Use compose_context_t to introduce a new entry, "
                                                        "or query contains_key_v<Γ, S, R> first.");

    using type = Context<detail::ctx::rewrite_entry_t<Es, S, R, NewT>...>;
};

template <typename Γ, typename SessionTag, typename RoleTag, typename NewT>
using update_entry_t = typename UpdateEntry<Γ, SessionTag, RoleTag, NewT>::type;

namespace detail::ctx {

template <typename S, typename R, typename Acc, typename... Remaining>
struct remove_fold;

template <typename S, typename R, typename... Acc>
struct remove_fold<S, R, Context<Acc...>> {
    using type = Context<Acc...>;
};

template <typename S, typename R, typename... Acc, typename T, typename... Rest>
struct remove_fold<S, R, Context<Acc...>, Entry<S, R, T>, Rest...> {
    using type = typename remove_fold<S, R, Context<Acc...>, Rest...>::type;
};

template <typename S, typename R, typename... Acc, typename Head, typename... Rest>
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

    using type = typename detail::ctx::remove_fold<S, R, Context<>, Es...>::type;
};

template <typename Γ, typename SessionTag, typename RoleTag>
using remove_entry_t = typename RemoveEntry<Γ, SessionTag, RoleTag>::type;

// The balance check is a set comparison over the tags a context transfers.  It
// answers "is every transferred tag both sent and received, and was it declared
// up front", and nothing beyond that.  In particular it does not detect two
// roles receiving the same single permission, a send ordered before the
// matching role reaches it, or fractional splits at a fork, because none of
// those are visible without walking reachable states.

namespace detail::ctx::balance {

// Insertion is idempotent, so a tag that appears in several entries merges
// rather than being rejected as an overlap.
template <typename PS1, typename PS2>
struct perm_set_merge;

template <typename PS1>
struct perm_set_merge<PS1, PermSet<>> {
    using type = PS1;
};

template <typename PS1, typename Head, typename... Tail>
struct perm_set_merge<PS1, PermSet<Head, Tail...>> : perm_set_merge<perm_set_insert_t<PS1, Head>, PermSet<Tail...>> {};

template <typename PS1, typename PS2>
using perm_set_merge_t = typename perm_set_merge<PS1, PS2>::type;

// A head with no specialisation contributes no tags.  That covers the
// terminals, and it also means any combinator the walker does not yet handle is
// invisible to the balance check rather than rejected by it.
template <typename T>
struct enumerate_send_tags_impl {
    using type = EmptyPermSet;
};

template <typename T>
struct enumerate_recv_tags_impl {
    using type = EmptyPermSet;
};

template <typename Payload, typename K>
struct enumerate_send_tags_impl<Send<Payload, K>> {
    using next = typename enumerate_send_tags_impl<K>::type;
    using payload_tag_or_void = payload_perm_tag_t<Payload>;

    // Transferable and Returned both move ownership, so both consume the
    // sender's permission.  A borrow is scoped and a plain payload carries no
    // permission, so neither takes part in the balance.
    static constexpr bool transfers = is_transferable_v<Payload> || is_returned_v<Payload>;

    using type = std::conditional_t<transfers, perm_set_insert_t<next, payload_tag_or_void>, next>;
};

template <typename Payload, typename K>
struct enumerate_recv_tags_impl<Send<Payload, K>> {
    using type = typename enumerate_recv_tags_impl<K>::type;
};

template <typename Payload, typename K>
struct enumerate_send_tags_impl<Recv<Payload, K>> {
    using type = typename enumerate_send_tags_impl<K>::type;
};

template <typename Payload, typename K>
struct enumerate_recv_tags_impl<Recv<Payload, K>> {
    using next = typename enumerate_recv_tags_impl<K>::type;
    using payload_tag_or_void = payload_perm_tag_t<Payload>;

    static constexpr bool transfers = is_transferable_v<Payload> || is_returned_v<Payload>;

    using type = std::conditional_t<transfers, perm_set_insert_t<next, payload_tag_or_void>, next>;
};

template <typename Body>
struct enumerate_send_tags_impl<Loop<Body>> : enumerate_send_tags_impl<Body> {};

template <typename Body>
struct enumerate_recv_tags_impl<Loop<Body>> : enumerate_recv_tags_impl<Body> {};

// Branches merge idempotently, so a tag carried by several branches counts
// once.  That matches the set comparison the balance check performs.  Counting
// occurrences instead would need multiset permission sets.
template <typename... Ts>
struct enumerate_send_tags_select_fold {
    using type = EmptyPermSet;
};

template <typename T1>
struct enumerate_send_tags_select_fold<T1> {
    using type = typename enumerate_send_tags_impl<T1>::type;
};

template <typename T1, typename T2, typename... Tail>
struct enumerate_send_tags_select_fold<T1, T2, Tail...> {
    using head_tags = typename enumerate_send_tags_impl<T1>::type;
    using tail_tags = typename enumerate_send_tags_select_fold<T2, Tail...>::type;
    using type = perm_set_merge_t<head_tags, tail_tags>;
};

template <typename... Ts>
struct enumerate_recv_tags_select_fold {
    using type = EmptyPermSet;
};

template <typename T1>
struct enumerate_recv_tags_select_fold<T1> {
    using type = typename enumerate_recv_tags_impl<T1>::type;
};

template <typename T1, typename T2, typename... Tail>
struct enumerate_recv_tags_select_fold<T1, T2, Tail...> {
    using head_tags = typename enumerate_recv_tags_impl<T1>::type;
    using tail_tags = typename enumerate_recv_tags_select_fold<T2, Tail...>::type;
    using type = perm_set_merge_t<head_tags, tail_tags>;
};

template <typename... Branches>
struct enumerate_send_tags_impl<Select<Branches...>> : enumerate_send_tags_select_fold<Branches...> {};

template <typename... Branches>
struct enumerate_recv_tags_impl<Select<Branches...>> : enumerate_recv_tags_select_fold<Branches...> {};

// An Offer may carry a leading Sender annotation, which names the peer for
// crash analysis and is not a branch.  Both shapes need their own
// specialisation: folding the annotation as if it were branch zero would ask
// for a continuation it does not have.
template <typename... Branches>
struct enumerate_send_tags_impl<Offer<Branches...>> : enumerate_send_tags_select_fold<Branches...> {};

template <typename... Branches>
struct enumerate_recv_tags_impl<Offer<Branches...>> : enumerate_recv_tags_select_fold<Branches...> {};

template <typename Role, typename... Branches>
struct enumerate_send_tags_impl<Offer<Sender<Role>, Branches...>> : enumerate_send_tags_select_fold<Branches...> {};

template <typename Role, typename... Branches>
struct enumerate_recv_tags_impl<Offer<Sender<Role>, Branches...>> : enumerate_recv_tags_select_fold<Branches...> {};

template <typename T>
using enumerate_send_tags_t = typename enumerate_send_tags_impl<T>::type;

template <typename T>
using enumerate_recv_tags_t = typename enumerate_recv_tags_impl<T>::type;

template <typename... Es>
struct aggregate_send_tags;

template <>
struct aggregate_send_tags<> {
    using type = EmptyPermSet;
};

template <typename E1, typename... Rest>
struct aggregate_send_tags<E1, Rest...> {
    using head_tags = enumerate_send_tags_t<typename E1::local_type>;
    using rest_tags = typename aggregate_send_tags<Rest...>::type;
    using type = perm_set_merge_t<head_tags, rest_tags>;
};

template <typename... Es>
using aggregate_send_tags_t = typename aggregate_send_tags<Es...>::type;

template <typename... Es>
struct aggregate_recv_tags;

template <>
struct aggregate_recv_tags<> {
    using type = EmptyPermSet;
};

template <typename E1, typename... Rest>
struct aggregate_recv_tags<E1, Rest...> {
    using head_tags = enumerate_recv_tags_t<typename E1::local_type>;
    using rest_tags = typename aggregate_recv_tags<Rest...>::type;
    using type = perm_set_merge_t<head_tags, rest_tags>;
};

template <typename... Es>
using aggregate_recv_tags_t = typename aggregate_recv_tags<Es...>::type;

}  // namespace detail::ctx::balance

template <typename Γ, typename InitialPerms>
struct is_permission_balanced;

template <typename... Es, typename InitialPerms>
struct is_permission_balanced<Context<Es...>, InitialPerms> {
    using all_sends = detail::ctx::balance::aggregate_send_tags_t<Es...>;
    using all_recvs = detail::ctx::balance::aggregate_recv_tags_t<Es...>;

    static constexpr bool sends_match_recvs = perm_set_equal_v<all_sends, all_recvs>;
    static constexpr bool sends_within_initial = perm_set_subset_v<all_sends, InitialPerms>;

    // Implied by the two above, and kept so that a failure names the side it
    // failed on.
    static constexpr bool recvs_within_initial = perm_set_subset_v<all_recvs, InitialPerms>;

    static constexpr bool value = sends_match_recvs && sends_within_initial && recvs_within_initial;
};

template <typename Γ, typename InitialPerms>
inline constexpr bool is_permission_balanced_v = is_permission_balanced<Γ, InitialPerms>::value;

#ifdef CRUCIBLE_SESSION_SELF_TESTS
namespace detail::ctx::context_self_test {

struct TraceRingSession {};
struct KernelCacheSession {};
struct Producer {};
struct Consumer {};
struct Writer {};
struct Reader {};

// The context operations only compare local types by identity, so these
// placeholders stand in for real session-type expressions.
struct ProducerT {};
struct ConsumerT {};
struct WriterT {};
struct ReaderT {};
struct NewProducerT {};

using TraceRingΓ = Context<Entry<TraceRingSession, Producer, ProducerT>, Entry<TraceRingSession, Consumer, ConsumerT>>;

using KernelΓ = Context<Entry<KernelCacheSession, Writer, WriterT>, Entry<KernelCacheSession, Reader, ReaderT>>;

using SampleEntry = Entry<TraceRingSession, Producer, ProducerT>;
static_assert(std::is_same_v<typename SampleEntry::session, TraceRingSession>);
static_assert(std::is_same_v<typename SampleEntry::role, Producer>);
static_assert(std::is_same_v<typename SampleEntry::local_type, ProducerT>);

static_assert(context_size_v<EmptyContext> == 0);
static_assert(context_size_v<TraceRingΓ> == 2);
static_assert(context_size_v<KernelΓ> == 2);

static_assert(is_empty_context_v<EmptyContext>);
static_assert(!is_empty_context_v<TraceRingΓ>);

// Two entries that share a session but differ on role, and two that share a
// role but differ on session, are distinct keys.  These two contexts naming a
// size at all is the witness: a key collision would not compile.
static_assert(context_size_v<TraceRingΓ> == 2);

using Same_Role_Γ = Context<Entry<TraceRingSession, Producer, ProducerT>, Entry<KernelCacheSession, Producer, WriterT>>;
static_assert(context_size_v<Same_Role_Γ> == 2);

using CombinedΓ = compose_context_t<TraceRingΓ, KernelΓ>;
static_assert(context_size_v<CombinedΓ> == 4);

using Assoc1 = compose_context_t<compose_context_t<TraceRingΓ, KernelΓ>, EmptyContext>;
using Assoc2 = compose_context_t<TraceRingΓ, compose_context_t<KernelΓ, EmptyContext>>;
static_assert(std::is_same_v<Assoc1, Assoc2>);

static_assert(std::is_same_v<compose_context_t<TraceRingΓ, EmptyContext>, TraceRingΓ>);
static_assert(std::is_same_v<compose_context_t<EmptyContext, TraceRingΓ>, TraceRingΓ>);

static_assert(contains_key_v<TraceRingΓ, TraceRingSession, Producer>);
static_assert(contains_key_v<TraceRingΓ, TraceRingSession, Consumer>);
static_assert(!contains_key_v<TraceRingΓ, TraceRingSession, Writer>);
static_assert(!contains_key_v<TraceRingΓ, KernelCacheSession, Producer>);

static_assert(!contains_key_v<EmptyContext, TraceRingSession, Producer>);

static_assert(contains_key_v<CombinedΓ, TraceRingSession, Producer>);
static_assert(contains_key_v<CombinedΓ, KernelCacheSession, Reader>);

static_assert(std::is_same_v<lookup_context_t<TraceRingΓ, TraceRingSession, Producer>, ProducerT>);
static_assert(std::is_same_v<lookup_context_t<TraceRingΓ, TraceRingSession, Consumer>, ConsumerT>);

static_assert(std::is_same_v<lookup_context_t<CombinedΓ, TraceRingSession, Producer>, ProducerT>);
static_assert(std::is_same_v<lookup_context_t<CombinedΓ, KernelCacheSession, Writer>, WriterT>);

using TraceRingDomain = domain_of_t<TraceRingΓ>;
static_assert(TraceRingDomain::size == 2);
static_assert(
    std::is_same_v<TraceRingDomain, KeySet<Key<TraceRingSession, Producer>, Key<TraceRingSession, Consumer>>>);

using EmptyDomain = domain_of_t<EmptyContext>;
static_assert(EmptyDomain::size == 0);
static_assert(std::is_same_v<EmptyDomain, KeySet<>>);

using CombinedDomain = domain_of_t<CombinedΓ>;
static_assert(CombinedDomain::size == 4);

using UpdatedΓ = update_entry_t<TraceRingΓ, TraceRingSession, Producer, NewProducerT>;
static_assert(std::is_same_v<lookup_context_t<UpdatedΓ, TraceRingSession, Producer>, NewProducerT>);
static_assert(std::is_same_v<lookup_context_t<UpdatedΓ, TraceRingSession, Consumer>, ConsumerT>);
static_assert(context_size_v<UpdatedΓ> == context_size_v<TraceRingΓ>);

static_assert(std::is_same_v<domain_of_t<UpdatedΓ>, domain_of_t<TraceRingΓ>>);

using RewrittenIdent = update_entry_t<TraceRingΓ, TraceRingSession, Producer, ProducerT>;
static_assert(std::is_same_v<RewrittenIdent, TraceRingΓ>);

using RemovedProducer = remove_entry_t<TraceRingΓ, TraceRingSession, Producer>;
static_assert(context_size_v<RemovedProducer> == 1);
static_assert(contains_key_v<RemovedProducer, TraceRingSession, Consumer>);
static_assert(!contains_key_v<RemovedProducer, TraceRingSession, Producer>);
static_assert(std::is_same_v<lookup_context_t<RemovedProducer, TraceRingSession, Consumer>, ConsumerT>);

using RemovedBoth = remove_entry_t<remove_entry_t<TraceRingΓ, TraceRingSession, Producer>, TraceRingSession, Consumer>;
static_assert(is_empty_context_v<RemovedBoth>);
static_assert(std::is_same_v<RemovedBoth, EmptyContext>);

using CombinedMinusWriter = remove_entry_t<CombinedΓ, KernelCacheSession, Writer>;
static_assert(context_size_v<CombinedMinusWriter> == 3);
static_assert(contains_key_v<CombinedMinusWriter, KernelCacheSession, Reader>);
static_assert(!contains_key_v<CombinedMinusWriter, KernelCacheSession, Writer>);

// Removing an entry and composing it back reproduces the same domain, but not
// necessarily the same entry order, so the round trip is witnessed by size and
// presence rather than by type identity.
using RemovedThenReadded = compose_context_t<remove_entry_t<TraceRingΓ, TraceRingSession, Producer>,
                                             Context<Entry<TraceRingSession, Producer, ProducerT>>>;
static_assert(context_size_v<RemovedThenReadded> == context_size_v<TraceRingΓ>);
static_assert(contains_key_v<RemovedThenReadded, TraceRingSession, Producer>);
static_assert(contains_key_v<RemovedThenReadded, TraceRingSession, Consumer>);

struct WorkPerm {};
struct CfgPerm {};
struct HotPerm {};

struct CoordSession {};
struct Coordinator {};
struct WorkerRole {};

using ProducerProto = Send<Transferable<int, WorkPerm>, End>;
using ConsumerProto = Recv<Transferable<int, WorkPerm>, End>;

using Γ_balanced =
    Context<Entry<CoordSession, Coordinator, ProducerProto>, Entry<CoordSession, WorkerRole, ConsumerProto>>;

static_assert(is_permission_balanced_v<Γ_balanced, PermSet<WorkPerm>>);

static_assert(is_permission_balanced_v<EmptyContext, EmptyPermSet>);
static_assert(is_permission_balanced_v<EmptyContext, PermSet<WorkPerm>>);

using Γ_drain_only = Context<Entry<CoordSession, WorkerRole, Recv<Transferable<int, WorkPerm>, End>>>;

static_assert(!is_permission_balanced_v<Γ_drain_only, PermSet<WorkPerm>>);

using Γ_send_only = Context<Entry<CoordSession, Coordinator, Send<Transferable<int, WorkPerm>, End>>>;

static_assert(!is_permission_balanced_v<Γ_send_only, PermSet<WorkPerm>>);

// Both sides transfer the same tag, so the two sets agree, but the tag was
// never declared, so the subset gate rejects.
using Γ_unknown_tag =
    Context<Entry<CoordSession, Coordinator, ProducerProto>, Entry<CoordSession, WorkerRole, ConsumerProto>>;

static_assert(!is_permission_balanced_v<Γ_unknown_tag, EmptyPermSet>);
static_assert(!is_permission_balanced_v<Γ_unknown_tag, PermSet<HotPerm>>);

struct KernelCacheSession2 {};
using Γ_multi_tag =
    Context<Entry<CoordSession, Coordinator, ProducerProto>, Entry<CoordSession, WorkerRole, ConsumerProto>,
            Entry<KernelCacheSession2, Writer, Send<Transferable<double, CfgPerm>, End>>,
            Entry<KernelCacheSession2, Reader, Recv<Transferable<double, CfgPerm>, End>>>;

static_assert(is_permission_balanced_v<Γ_multi_tag, PermSet<WorkPerm, CfgPerm>>);

static_assert(!is_permission_balanced_v<Γ_multi_tag, PermSet<WorkPerm>>);
static_assert(!is_permission_balanced_v<Γ_multi_tag, PermSet<CfgPerm>>);

using BorrowProducer = Send<Borrowed<int, WorkPerm>, End>;
using BorrowConsumer = Recv<Borrowed<int, WorkPerm>, End>;

using Γ_borrowed_only =
    Context<Entry<CoordSession, Coordinator, BorrowProducer>, Entry<CoordSession, WorkerRole, BorrowConsumer>>;

static_assert(is_permission_balanced_v<Γ_borrowed_only, EmptyPermSet>);
static_assert(is_permission_balanced_v<Γ_borrowed_only, PermSet<WorkPerm>>);

using Γ_plain_only =
    Context<Entry<CoordSession, Coordinator, Send<int, End>>, Entry<CoordSession, WorkerRole, Recv<int, End>>>;

static_assert(is_permission_balanced_v<Γ_plain_only, EmptyPermSet>);

// Returned moves the permission the same way Transferable does: the sender
// loses it and the receiver gains it.
using Γ_returned = Context<Entry<CoordSession, Coordinator, Send<Returned<int, HotPerm>, End>>,
                           Entry<CoordSession, WorkerRole, Recv<Returned<int, HotPerm>, End>>>;

static_assert(is_permission_balanced_v<Γ_returned, PermSet<HotPerm>>);

using LoopProducer = Loop<Send<Transferable<int, WorkPerm>, Continue>>;
using LoopConsumer = Loop<Recv<Transferable<int, WorkPerm>, Continue>>;

using Γ_loop = Context<Entry<CoordSession, Coordinator, LoopProducer>, Entry<CoordSession, WorkerRole, LoopConsumer>>;

static_assert(is_permission_balanced_v<Γ_loop, PermSet<WorkPerm>>);

using ProducerThenStop = Send<Transferable<int, WorkPerm>, ::crucible::safety::proto::Stop>;
using Γ_with_stop =
    Context<Entry<CoordSession, Coordinator, ProducerThenStop>, Entry<CoordSession, WorkerRole, ConsumerProto>>;

static_assert(is_permission_balanced_v<Γ_with_stop, PermSet<WorkPerm>>);

using SelectProducer = Select<Send<Transferable<int, WorkPerm>, End>, Send<Transferable<double, CfgPerm>, End>>;
using OfferConsumer = Offer<Recv<Transferable<int, WorkPerm>, End>, Recv<Transferable<double, CfgPerm>, End>>;

using Γ_choice =
    Context<Entry<CoordSession, Coordinator, SelectProducer>, Entry<CoordSession, WorkerRole, OfferConsumer>>;

static_assert(is_permission_balanced_v<Γ_choice, PermSet<WorkPerm, CfgPerm>>);

using NestedProducer =
    Loop<Select<Send<Transferable<int, WorkPerm>, Continue>, Send<Transferable<double, CfgPerm>, End>>>;
using NestedConsumer =
    Loop<Offer<Recv<Transferable<int, WorkPerm>, Continue>, Recv<Transferable<double, CfgPerm>, End>>>;

using Γ_nested =
    Context<Entry<CoordSession, Coordinator, NestedProducer>, Entry<CoordSession, WorkerRole, NestedConsumer>>;

static_assert(is_permission_balanced_v<Γ_nested, PermSet<WorkPerm, CfgPerm>>);

using OfferConsumer_partial = Offer<Recv<Transferable<int, WorkPerm>, End>, Recv<int, End>>;

using Γ_choice_partial =
    Context<Entry<CoordSession, Coordinator, SelectProducer>, Entry<CoordSession, WorkerRole, OfferConsumer_partial>>;

static_assert(!is_permission_balanced_v<Γ_choice_partial, PermSet<WorkPerm, CfgPerm>>);

namespace dcb = ::crucible::safety::proto::detail::ctx::balance;

static_assert(perm_set_equal_v<dcb::enumerate_send_tags_t<ProducerProto>, PermSet<WorkPerm>>);
static_assert(perm_set_equal_v<dcb::enumerate_recv_tags_t<ProducerProto>, EmptyPermSet>);

static_assert(perm_set_equal_v<dcb::enumerate_send_tags_t<ConsumerProto>, EmptyPermSet>);
static_assert(perm_set_equal_v<dcb::enumerate_recv_tags_t<ConsumerProto>, PermSet<WorkPerm>>);

static_assert(perm_set_equal_v<dcb::enumerate_send_tags_t<BorrowProducer>, EmptyPermSet>);
static_assert(perm_set_equal_v<dcb::enumerate_recv_tags_t<BorrowConsumer>, EmptyPermSet>);

static_assert(perm_set_equal_v<dcb::enumerate_send_tags_t<Send<int, End>>, EmptyPermSet>);

static_assert(perm_set_equal_v<dcb::enumerate_send_tags_t<End>, EmptyPermSet>);
static_assert(perm_set_equal_v<dcb::enumerate_send_tags_t<::crucible::safety::proto::Stop>, EmptyPermSet>);
static_assert(perm_set_equal_v<dcb::enumerate_send_tags_t<Continue>, EmptyPermSet>);

static_assert(perm_set_equal_v<dcb::perm_set_merge_t<EmptyPermSet, EmptyPermSet>, EmptyPermSet>);
static_assert(perm_set_equal_v<dcb::perm_set_merge_t<PermSet<WorkPerm>, EmptyPermSet>, PermSet<WorkPerm>>);
static_assert(perm_set_equal_v<dcb::perm_set_merge_t<PermSet<WorkPerm>, PermSet<WorkPerm>>, PermSet<WorkPerm>>);
static_assert(perm_set_equal_v<dcb::perm_set_merge_t<PermSet<WorkPerm>, PermSet<CfgPerm>>, PermSet<WorkPerm, CfgPerm>>);

}  // namespace detail::ctx::context_self_test
#endif  // CRUCIBLE_SESSION_SELF_TESTS

}  // namespace crucible::safety::proto
