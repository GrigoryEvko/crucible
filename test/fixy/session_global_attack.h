#pragma once

// The shared part of test_session_global_attack: the explorer's model of a
// typing context, the build of that model from a context type, the
// association step, and the checks of one global type.  The explorer, the
// liveness check, the report and main are in test_session_global_attack.cpp.
// Each other source file of the test holds one group of attacks and the
// entry function that runs it.

#include <fixy/session/Liveness.h>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <initializer_list>
#include <meta>
#include <string_view>
#include <tuple>
#include <type_traits>
#include <utility>
#include <vector>

// A role feeds a stable id, so each role, label and payload has external
// linkage: a label word is a stable id, and a stable id refuses a type
// with internal linkage.
namespace test_session_global_attack_roles {
struct NeverSent {};
struct P {};
struct Q {};
struct R {};
struct S {};
struct GenA {};
struct GenB {};
struct GenC {};
struct GenD {};
struct M {};
struct M0 {};
struct M1 {};
struct M2 {};
struct X {};
struct Y {};
struct Z {};
struct Kk {};
struct L1 {};
struct L2 {};
struct Add {};
struct Sub {};
struct GenL0 {};
struct GenL1 {};
struct GenL2 {};
}  // namespace test_session_global_attack_roles

namespace test_session_global_attack {

namespace g = ::fixy::session::global;
namespace s = ::fixy::session;

// ── Names ────────────────────────────────────────────────────────────

// The printable name of a type.  The explorer uses it as the identity of
// a role, a label or a payload.
template <typename T>
consteval std::string_view name_of() {
    return std::string_view{std::define_static_string(std::meta::display_string_of(^^T))};
}

// Print the reason and stop the test.
[[noreturn]] void fail(std::string_view what);

// ── The explorer's model of a typing context ─────────────────────────

enum class NodeKind : std::uint8_t {
    End,
    Internal,
    External,
    Alias,
};

struct Branch {
    int peer = -1;
    int message = -1;
    int next = -1;
};

struct Node {
    NodeKind kind = NodeKind::End;
    int alias = -1;
    std::vector<Branch> branches;
};

struct Message {
    std::string_view label;
    std::string_view payload;
};

// Pairs (below, above) of the payload order that a test uses.  Each pair
// that the test adds is checked at compile time against
// is_payload_subsort_v, so the explorer cannot admit a pair that the
// session layer refuses.
struct PayloadPair {
    std::string_view below;
    std::string_view above;
};
extern std::vector<PayloadPair> payload_order;

struct System {
    std::vector<std::string_view> roles;
    std::vector<Message> messages;
    std::vector<Node> nodes;
    std::vector<int> start;
    std::vector<std::vector<int>> initial_queues;

    // True when a received message fits a branch that offers another
    // message: the same label, and a payload that is the same or below
    // (Definition 10 of Pischke, Masters and Yoshida).
    [[nodiscard]] bool fits(int sent, int offered) const {
        const Message& got = messages[static_cast<std::size_t>(sent)];
        const Message& want = messages[static_cast<std::size_t>(offered)];
        if (got.label != want.label) return false;
        if (got.payload == want.payload) return true;
        return std::ranges::any_of(payload_order, [&](const PayloadPair& pair) {
            return pair.below == got.payload && pair.above == want.payload;
        });
    }

    // The index of a role.  A peer that is not a role of the context
    // stops the test.  Complexity: linear in the number of roles.
    [[nodiscard]] int role_of(std::string_view name) const {
        for (std::size_t index = 0; index < roles.size(); ++index) {
            if (roles[index] == name) return static_cast<int>(index);
        }
        fail("a local type names a peer that is not a role of the context");
    }

    // The index of a (label, payload) pair, added on first use.
    // Complexity: linear in the number of distinct messages.
    [[nodiscard]] int message_of(std::string_view label, std::string_view payload) {
        for (std::size_t index = 0; index < messages.size(); ++index) {
            if (messages[index].label == label && messages[index].payload == payload) return static_cast<int>(index);
        }
        messages.push_back(Message{label, payload});
        return static_cast<int>(messages.size() - 1);
    }

    [[nodiscard]] int add_node(NodeKind kind) {
        nodes.push_back(Node{kind, -1, {}});
        return static_cast<int>(nodes.size() - 1);
    }

    // Follow Loop and Continue aliases to a node that acts or ends.  A
    // chain longer than the node count is a loop with no action.
    [[nodiscard]] int resolve(int index) const {
        for (std::size_t step = 0; step <= nodes.size(); ++step) {
            if (nodes[static_cast<std::size_t>(index)].kind != NodeKind::Alias) return index;
            index = nodes[static_cast<std::size_t>(index)].alias;
        }
        fail("a Loop reaches its Continue without an action");
    }

    [[nodiscard]] std::size_t pair_of(int from, int to) const {
        return static_cast<std::size_t>(from) * roles.size() + static_cast<std::size_t>(to);
    }
};

// One branch as the type walk sees it.  The walk passes names, and one
// function outside the templates turns them into indices, so that each
// instantiation of the walk stays small.
struct BranchSpec {
    std::string_view peer;
    std::string_view label;
    std::string_view payload;
    int next = -1;
};

void set_branches(System& sys, int node, std::initializer_list<BranchSpec> specs);
void set_alias(System& sys, int node, int target);
void add_queued(System& sys, std::string_view from, std::string_view to, std::string_view label,
                std::string_view payload);

// Build the node graph of a local type.  The primary template has no
// definition, so a combinator that the explorer does not model stops the
// build.
template <typename T>
struct LocalBuild;

template <>
struct LocalBuild<s::End> {
    static int build(System& sys, int) { return sys.add_node(NodeKind::End); }
};

template <>
struct LocalBuild<s::Continue> {
    static int build(System& sys, int loop) {
        if (loop < 0) fail("a Continue has no enclosing Loop");
        const int self = sys.add_node(NodeKind::Alias);
        set_alias(sys, self, loop);
        return self;
    }
};

template <typename Body>
struct LocalBuild<s::Loop<Body>> {
    static int build(System& sys, int) {
        const int self = sys.add_node(NodeKind::Alias);
        set_alias(sys, self, LocalBuild<Body>::build(sys, self));
        return self;
    }
};

template <typename Q, typename L, typename P, typename K>
struct LocalBuild<s::Send<s::PeerMsg<Q, L, P>, K>> {
    static int build(System& sys, int loop) {
        const int self = sys.add_node(NodeKind::Internal);
        set_branches(sys, self,
                     {BranchSpec{name_of<Q>(), name_of<L>(), name_of<P>(), LocalBuild<K>::build(sys, loop)}});
        return self;
    }
};

template <typename Q, typename L, typename P, typename K>
struct LocalBuild<s::Recv<s::PeerMsg<Q, L, P>, K>> {
    static int build(System& sys, int loop) {
        const int self = sys.add_node(NodeKind::External);
        set_branches(sys, self,
                     {BranchSpec{name_of<Q>(), name_of<L>(), name_of<P>(), LocalBuild<K>::build(sys, loop)}});
        return self;
    }
};

template <typename... Qs, typename... Ls, typename... Ps, typename... Ks>
struct LocalBuild<s::Select<s::Send<s::PeerMsg<Qs, Ls, Ps>, Ks>...>> {
    static int build(System& sys, int loop) {
        const int self = sys.add_node(NodeKind::Internal);
        set_branches(sys, self,
                     {BranchSpec{name_of<Qs>(), name_of<Ls>(), name_of<Ps>(), LocalBuild<Ks>::build(sys, loop)}...});
        return self;
    }
};

template <typename Peer, typename... Qs, typename... Ls, typename... Ps, typename... Ks>
struct LocalBuild<s::Offer<s::Sender<Peer>, s::Recv<s::PeerMsg<Qs, Ls, Ps>, Ks>...>> {
    static int build(System& sys, int loop) {
        const int self = sys.add_node(NodeKind::External);
        set_branches(sys, self,
                     {BranchSpec{name_of<Qs>(), name_of<Ls>(), name_of<Ps>(), LocalBuild<Ks>::build(sys, loop)}...});
        return self;
    }
};

template <typename Role, typename Queue>
struct QueueLoad;
template <typename Role, typename... Tos, typename... Ls, typename... Ps>
struct QueueLoad<Role, s::OutQueue<s::Queued<Tos, Ls, Ps>...>> {
    static void load([[maybe_unused]] System& sys) {
        (add_queued(sys, name_of<Role>(), name_of<Tos>(), name_of<Ls>(), name_of<Ps>()), ...);
    }
};

template <typename Ctx>
struct ContextLoad;
template <typename... Rs, typename... Qs, typename... Ts>
struct ContextLoad<s::TypingContext<s::RoleState<Rs, Qs, Ts>...>> {
    static System load() {
        System sys;
        sys.roles = {name_of<Rs>()...};
        sys.initial_queues.assign(sys.roles.size() * sys.roles.size(), {});
        (sys.start.push_back(LocalBuild<Ts>::build(sys, -1)), ...);
        for (int& entry : sys.start)
            entry = sys.resolve(entry);
        (QueueLoad<Rs, Qs>::load(sys), ...);
        return sys;
    }
};

template <typename Ctx>
System load_context() {
    return ContextLoad<Ctx>::load();
}

// ── The verdict of the explorer and the report ───────────────────────

struct Verdict {
    std::size_t states = 0;
    std::size_t deadlocks = 0;
    std::size_t unsafe = 0;
    std::size_t starved = 0;
    bool capped = false;

    [[nodiscard]] bool is_clean() const { return deadlocks == 0 && unsafe == 0 && starved == 0 && !capped; }
};

// Explore every reachable state of the context with queues of the given
// capacity, and check liveness (Definition 12).
[[nodiscard]] Verdict analyse(const System& sys, std::size_t capacity);

// Count a failure and print it when the condition does not hold.
void expect(bool holds, std::string_view what);

void print_verdict(std::string_view label, const Verdict& verdict);

// Run the context at each capacity and require the expected cleanliness.
void run_context(const System& sys, std::string_view label, bool expect_clean);

template <typename Ctx>
void expect_context(std::string_view label, bool expect_clean) {
    run_context(load_context<Ctx>(), label, expect_clean);
}

// ── The association step ─────────────────────────────────────────────
//
// Association refines each context entry with synchronous subtyping.
// For each global type that the gates accept, five rewrites of the
// projected context test that step:
//
//   safe      each Select keeps its first branch, each Offer gains a
//             branch at the end, and each int payload that a role sends
//             becomes a Sanitized value.  Association must hold, and the
//             context must be live, when the rewrite keeps each exit.
//             Refinement keeps the exits of the supertype, so association
//             refuses a Select that loses the only exit of its loop.  The
//             explorer must then show a state of the rewritten context
//             that can never end, where each role stands at a node that
//             can end in the projected context.
//   unfolded  each top-level Loop is unfolded once.  Association must
//             hold, and the context must be live.
//   widened   each Select gains a branch at the end, with a label that
//             no role offers.  Association must refuse.
//   narrowed  each Offer with two or more branches loses its last one.
//             Association must refuse.
//   swapped   each Offer with two or more branches swaps its first two.
//             Association must hold, and the context must be live.  A
//             PeerMsg names a label key, so the handle sends the label
//             word, and the peer enters the branch of that label wherever
//             it stands.  The explorer carries the label on the wire too.
//
// A refused rewrite also runs in the explorer.  The count of refused
// rewrites that the explorer shows faulty tells how often the refusal
// stops a real fault.
//
// The type rewrite feeds the association check.  The explorer applies the
// same rewrite to the node graph of the projected context, because each
// node of that graph is one occurrence of a combinator in the local type.
// Unfolding does not change the graph, so the unfolded rewrite reuses
// the projected context in the explorer.

using test_session_global_attack_roles::NeverSent;
using Checked = ::fixy::Tagged<int, ::fixy::tags::source::Sanitized>;

template <typename Policy, typename T>
struct Rewrite;
template <typename Policy>
struct Rewrite<Policy, s::End> {
    using type = s::End;
};
template <typename Policy>
struct Rewrite<Policy, s::Continue> {
    using type = s::Continue;
};
template <typename Policy, typename Body>
struct Rewrite<Policy, s::Loop<Body>> {
    using type = s::Loop<typename Rewrite<Policy, Body>::type>;
};
template <typename Policy, typename Q, typename L, typename P, typename K>
struct Rewrite<Policy, s::Send<s::PeerMsg<Q, L, P>, K>> {
    using type = s::Send<s::PeerMsg<Q, L, typename Policy::template sent<P>>, typename Rewrite<Policy, K>::type>;
};
template <typename Policy, typename Msg, typename K>
struct Rewrite<Policy, s::Recv<Msg, K>> {
    using type = s::Recv<Msg, typename Rewrite<Policy, K>::type>;
};
template <typename Policy, typename... Bs>
struct Rewrite<Policy, s::Select<Bs...>> {
    using type = typename Policy::template select<typename Rewrite<Policy, Bs>::type...>::type;
};
template <typename Policy, typename Q, typename... Bs>
struct Rewrite<Policy, s::Offer<s::Sender<Q>, Bs...>> {
    using type = typename Policy::template offer<Q, typename Rewrite<Policy, Bs>::type...>::type;
};

struct IdentityPolicy {
    template <typename P>
    using sent = P;
    template <typename... Bs>
    struct select {
        using type = s::Select<Bs...>;
    };
    template <typename Q, typename... Bs>
    struct offer {
        using type = s::Offer<s::Sender<Q>, Bs...>;
    };
};

struct SafePolicy : IdentityPolicy {
    template <typename P>
    using sent = std::conditional_t<std::is_same_v<P, int>, Checked, P>;
    template <typename First, typename... Bs>
    struct select {
        using type = s::Select<First>;
    };
    template <typename Q, typename... Bs>
    struct offer {
        using type = s::Offer<s::Sender<Q>, Bs..., s::Recv<s::PeerMsg<Q, NeverSent, int>, s::End>>;
    };
};

template <typename Branch>
struct send_peer;
template <typename Q, typename L, typename P, typename K>
struct send_peer<s::Send<s::PeerMsg<Q, L, P>, K>> {
    using type = Q;
};

struct WidenPolicy : IdentityPolicy {
    template <typename First, typename... Bs>
    struct select {
        using type =
            s::Select<First, Bs..., s::Send<s::PeerMsg<typename send_peer<First>::type, NeverSent, int>, s::End>>;
    };
};

template <typename Q, typename... Bs>
consteval auto offer_without_last() {
    if constexpr (sizeof...(Bs) < 2) {
        return std::type_identity<s::Offer<s::Sender<Q>, Bs...>>{};
    } else {
        return []<std::size_t... Index>(std::index_sequence<Index...>) {
            return std::type_identity<s::Offer<s::Sender<Q>, Bs...[Index]...>>{};
        }(std::make_index_sequence<sizeof...(Bs) - 1>{});
    }
}

struct NarrowPolicy : IdentityPolicy {
    template <typename Q, typename... Bs>
    struct offer {
        using type = typename decltype(offer_without_last<Q, Bs...>())::type;
    };
};

template <typename Q, typename... Bs>
struct offer_swapped {
    using type = s::Offer<s::Sender<Q>, Bs...>;
};
template <typename Q, typename First, typename Second, typename... Rest>
struct offer_swapped<Q, First, Second, Rest...> {
    using type = s::Offer<s::Sender<Q>, Second, First, Rest...>;
};

struct SwapPolicy : IdentityPolicy {
    template <typename Q, typename... Bs>
    struct offer {
        using type = typename offer_swapped<Q, Bs...>::type;
    };
};

template <typename T>
struct SafeRewrite : Rewrite<SafePolicy, T> {};
template <typename T>
struct WidenRewrite : Rewrite<WidenPolicy, T> {};
template <typename T>
struct NarrowRewrite : Rewrite<NarrowPolicy, T> {};
template <typename T>
struct SwapRewrite : Rewrite<SwapPolicy, T> {};

// One unfolding of a top-level Loop.  A Continue inside a nested Loop
// binds that Loop, so the substitution stops there.
template <typename T, typename Rep>
struct LocalSubst;
template <typename Rep>
struct LocalSubst<s::End, Rep> {
    using type = s::End;
};
template <typename Rep>
struct LocalSubst<s::Continue, Rep> {
    using type = Rep;
};
template <typename Body, typename Rep>
struct LocalSubst<s::Loop<Body>, Rep> {
    using type = s::Loop<Body>;
};
template <typename Msg, typename K, typename Rep>
struct LocalSubst<s::Send<Msg, K>, Rep> {
    using type = s::Send<Msg, typename LocalSubst<K, Rep>::type>;
};
template <typename Msg, typename K, typename Rep>
struct LocalSubst<s::Recv<Msg, K>, Rep> {
    using type = s::Recv<Msg, typename LocalSubst<K, Rep>::type>;
};
template <typename... Bs, typename Rep>
struct LocalSubst<s::Select<Bs...>, Rep> {
    using type = s::Select<typename LocalSubst<Bs, Rep>::type...>;
};
template <typename Q, typename... Bs, typename Rep>
struct LocalSubst<s::Offer<s::Sender<Q>, Bs...>, Rep> {
    using type = s::Offer<s::Sender<Q>, typename LocalSubst<Bs, Rep>::type...>;
};

template <typename T>
struct UnfoldTop {
    using type = T;
};
template <typename Body>
struct UnfoldTop<s::Loop<Body>> {
    using type = typename LocalSubst<Body, s::Loop<Body>>::type;
};

template <typename Ctx, template <typename> class Transform>
struct MapContext;
template <typename... Rs, typename... Qs, typename... Ts, template <typename> class Transform>
struct MapContext<s::TypingContext<s::RoleState<Rs, Qs, Ts>...>, Transform> {
    using type = s::TypingContext<s::RoleState<Rs, Qs, typename Transform<Ts>::type>...>;
};

enum class GraphRewrite : std::uint8_t {
    Safe,
    Widen,
    Narrow,
    Swap,
};

// The rewrite of the policies above, on the node graph.  Complexity:
// linear in the number of nodes and branches.
[[nodiscard]] System rewritten(const System& base, GraphRewrite kind);

struct AssociationCounts {
    std::size_t types = 0;
    std::size_t full_types = 0;
    std::size_t safe_live = 0;
    std::size_t safe_lost_exit = 0;
    std::size_t unfolded_live = 0;
    std::size_t refused = 0;
    std::size_t refused_faulty = 0;
    std::size_t swapped_live = 0;
};
extern AssociationCounts association_counts;

// A rewrite that association must accept: it must hold, and the context
// must be live.
void judge_accepted(std::string_view label, std::string_view rewrite, bool associated, const System& sys,
                    std::size_t& live);

// A narrowing that association accepts when it keeps each exit.  A
// refusal is right only when the explorer shows a state of the rewritten
// context that can never end, where each role stands at a node that can
// end in the projected context.
void judge_narrowed(std::string_view label, std::string_view rewrite, bool associated, const System& base,
                    const System& sys);

// A rewrite that association must refuse.  The explorer tells whether
// the refusal stopped a real fault.
void judge_refused(std::string_view label, std::string_view rewrite, bool associated, const System& sys,
                   std::size_t& refused, std::size_t& faulty);

// Full runs all five rewrites.  Otherwise only the safe and the widened
// rewrite run, which keeps the compile time of the large generated
// families bounded.
template <typename G, bool Full>
void check_association_step(std::string_view label) {
    using Ctx = s::projected_context_t<G>;
    ++association_counts.types;
    const System base = load_context<Ctx>();
    using Safe = typename MapContext<Ctx, SafeRewrite>::type;
    using Widened = typename MapContext<Ctx, WidenRewrite>::type;
    judge_narrowed(label, "safe rewrite", s::association_holds_v<Safe, G>, base, rewritten(base, GraphRewrite::Safe));
    if constexpr (!std::is_same_v<Widened, Ctx>) {
        judge_refused(label, "widened Select", s::association_holds_v<Widened, G>, rewritten(base, GraphRewrite::Widen),
                      association_counts.refused, association_counts.refused_faulty);
    }
    if constexpr (Full) {
        ++association_counts.full_types;
        using Unfolded = typename MapContext<Ctx, UnfoldTop>::type;
        using Narrowed = typename MapContext<Ctx, NarrowRewrite>::type;
        using Swapped = typename MapContext<Ctx, SwapRewrite>::type;
        judge_accepted(label, "unfolded", s::association_holds_v<Unfolded, G>, base, association_counts.unfolded_live);
        if constexpr (!std::is_same_v<Narrowed, Ctx>) {
            judge_refused(label, "narrowed Offer", s::association_holds_v<Narrowed, G>,
                          rewritten(base, GraphRewrite::Narrow), association_counts.refused,
                          association_counts.refused_faulty);
        }
        if constexpr (!std::is_same_v<Swapped, Ctx>) {
            judge_accepted(label, "swapped Offer", s::association_holds_v<Swapped, G>,
                           rewritten(base, GraphRewrite::Swap), association_counts.swapped_live);
        }
    }
}

// A global type that the gates accept must give a live context, and the
// association step must accept its safe rewrites and refuse its unsafe
// ones.
template <typename G>
void expect_live_global(std::string_view label) {
    static_assert(s::is_live_by_construction_v<G>);
    expect_context<s::projected_context_t<G>>(label, true);
    check_association_step<G, true>(label);
}

// ── Generated global types ───────────────────────────────────────────
//
// The generated families and the merge grid check each global type with
// check_generated.

// The en-route variant is the global type after the sender of the first
// transmission sent the label of the first branch.  A Rec at the head is
// unfolded first, which puts the en-route message outside the binder.
template <typename G, typename Rep>
struct GlobalSubst;
template <typename Rep>
struct GlobalSubst<g::End, Rep> {
    using type = g::End;
};
template <typename Rep>
struct GlobalSubst<g::Var, Rep> {
    using type = Rep;
};
template <typename Body, typename Rep>
struct GlobalSubst<g::Rec<Body>, Rep> {
    using type = g::Rec<Body>;
};
template <typename From, typename To, typename... Ls, typename... Ps, typename... Cs, typename Rep>
struct GlobalSubst<g::Comm<From, To, g::Branch<Ls, Ps, Cs>...>, Rep> {
    using type = g::Comm<From, To, g::Branch<Ls, Ps, typename GlobalSubst<Cs, Rep>::type>...>;
};
template <typename From, typename To, typename L, typename Pl, typename C, typename Rep>
struct GlobalSubst<g::EnRoute<From, To, L, Pl, C>, Rep> {
    using type = g::EnRoute<From, To, L, Pl, typename GlobalSubst<C, Rep>::type>;
};

template <typename G>
struct Unfold {
    using type = G;
};
template <typename Body>
struct Unfold<g::Rec<Body>> {
    using type = typename Unfold<typename GlobalSubst<Body, g::Rec<Body>>::type>::type;
};

template <typename G>
struct FirstSend {
    using type = void;
};
template <typename From, typename To, typename L, typename Pl, typename C, typename... Rest>
struct FirstSend<g::Comm<From, To, g::Branch<L, Pl, C>, Rest...>> {
    using type = g::EnRoute<From, To, L, Pl, C>;
};

template <typename G>
using en_route_variant_t = typename FirstSend<typename Unfold<G>::type>::type;

struct FamilyCounts {
    std::size_t generated = 0;
    std::size_t well_formed = 0;
    std::size_t balanced_plus = 0;
    std::size_t live_by_construction = 0;
    std::size_t projectable_but_unbalanced = 0;
    std::size_t variants = 0;
    std::size_t variants_live = 0;
};

// Every projection must be a well-formed local type of Protocol.h.
template <typename G, typename RL>
struct ProjectionsWellFormed;
template <typename G, typename... Rs>
struct ProjectionsWellFormed<G, g::Roles<Rs...>>
    : std::bool_constant<(s::is_well_formed_v<typename s::project_t<G, Rs>::local> && ...)> {};

// Run a context that the gates call live, and report each fault.
void check_system(const System& sys, std::string_view family, std::uint64_t seed);

template <typename G>
void check_live(std::string_view family, std::uint64_t seed) {
    static_assert(ProjectionsWellFormed<G, g::roles_t<G>>::value);
    check_system(load_context<s::projected_context_t<G>>(), family, seed);
}

void report_variant_not_live(std::string_view family, std::uint64_t seed);

template <typename G, bool FullAssociation>
void check_generated(FamilyCounts& counts, std::string_view family, std::uint64_t seed) {
    ++counts.generated;
    if constexpr (g::is_global_well_formed_v<G>) {
        ++counts.well_formed;
        if constexpr (g::is_balanced_plus_v<G>) ++counts.balanced_plus;
        constexpr bool projects_everywhere = s::detail::live::each_role_projects<G, g::roles_t<G>>::value;
        if constexpr (projects_everywhere && !g::is_balanced_v<G>) ++counts.projectable_but_unbalanced;
        if constexpr (s::is_live_by_construction_v<G>) {
            ++counts.live_by_construction;
            check_live<G>(family, seed);
            check_association_step<G, FullAssociation>(family);
            using Variant = en_route_variant_t<G>;
            if constexpr (!std::is_void_v<Variant>) {
                ++counts.variants;
                // Theorem 3: a transition keeps balanced+.
                static_assert(g::is_balanced_plus_v<Variant>);
                if constexpr (s::is_live_by_construction_v<Variant>) {
                    ++counts.variants_live;
                    check_live<Variant>(family, seed);
                } else {
                    report_variant_not_live(family, seed);
                }
            }
        }
    }
}

// A fixed-seed generator builds global types over four roles and three
// labels.

using test_session_global_attack_roles::GenA;
using test_session_global_attack_roles::GenB;
using test_session_global_attack_roles::GenC;
using test_session_global_attack_roles::GenD;
using test_session_global_attack_roles::GenL0;
using test_session_global_attack_roles::GenL1;
using test_session_global_attack_roles::GenL2;
using GenRoles = std::tuple<GenA, GenB, GenC, GenD>;
using GenLabels = std::tuple<GenL0, GenL1, GenL2>;

consteval std::uint64_t splitmix(std::uint64_t value) {
    value += 0x9e3779b97f4a7c15ULL;
    value = (value ^ (value >> 30U)) * 0xbf58476d1ce4e5b9ULL;
    value = (value ^ (value >> 27U)) * 0x94d049bb133111ebULL;
    return value ^ (value >> 31U);
}

template <std::uint64_t Seed, int Depth, bool InLoop, std::size_t RoleCount>
struct Gen;

template <std::uint64_t Seed, int Depth, bool InLoop, std::size_t RoleCount>
consteval auto gen_select() {
    constexpr std::uint64_t hash = splitmix(Seed);
    constexpr std::size_t from_index = hash % RoleCount;
    constexpr std::size_t to_index = (from_index + 1 + (hash >> 8U) % (RoleCount - 1)) % RoleCount;
    using From = std::tuple_element_t<from_index, GenRoles>;
    using To = std::tuple_element_t<to_index, GenRoles>;
    using Label = std::tuple_element_t<(hash >> 20U) % 3, GenLabels>;
    constexpr unsigned pick = static_cast<unsigned>((hash >> 32U) % 16);
    if constexpr (Depth <= 0) {
        if constexpr (InLoop && pick % 2 == 0) {
            return std::type_identity<g::Var>{};
        } else {
            return std::type_identity<g::End>{};
        }
    } else if constexpr (pick == 0) {
        return std::type_identity<g::End>{};
    } else if constexpr (pick < 3 && InLoop) {
        return std::type_identity<g::Var>{};
    } else if constexpr (pick < 9) {
        return std::type_identity<
            g::Msg<From, To, Label, int, typename Gen<Seed * 3 + 1, Depth - 1, InLoop, RoleCount>::type>>{};
    } else if constexpr (pick < 13) {
        return std::type_identity<
            g::Comm<From, To, g::Branch<GenL0, int, typename Gen<Seed * 5 + 2, Depth - 1, InLoop, RoleCount>::type>,
                    g::Branch<GenL1, int, typename Gen<Seed * 7 + 3, Depth - 1, InLoop, RoleCount>::type>>>{};
    } else {
        return std::type_identity<
            g::Rec<g::Msg<From, To, Label, int, typename Gen<Seed * 11 + 4, Depth - 1, true, RoleCount>::type>>>{};
    }
}

template <std::uint64_t Seed, int Depth, bool InLoop, std::size_t RoleCount>
struct Gen {
    using type = typename decltype(gen_select<Seed, Depth, InLoop, RoleCount>())::type;
};

// Half of the seeds start the type with a loop, so that balancedness and
// the merge of loop-backs are exercised.
template <std::uint64_t Seed, int Depth, std::size_t RoleCount>
using generated_t =
    std::conditional_t<Seed % 2 == 0, typename Gen<Seed, Depth, false, RoleCount>::type,
                       g::Rec<g::Msg<GenA, GenB, GenL0, int, typename Gen<Seed, Depth, true, RoleCount>::type>>>;

// Check the seeds Base + Index of one generated family.
template <std::size_t RoleCount, int Depth, std::uint64_t Base, bool FullAssociation, std::size_t... Index>
void check_family(FamilyCounts& counts, std::string_view family, std::index_sequence<Index...>) {
    (check_generated<generated_t<Base + Index, Depth, RoleCount>, FullAssociation>(counts, family, Base + Index), ...);
}

// The family of four roles at depth 4 is in a source file of its own.
inline constexpr std::string_view four_roles_family = "four roles, depth 4";
void check_four_roles(FamilyCounts& counts);

// The family of two roles at depth 6 checks all five rewrites of each
// live type of its 24 seeds.  Seven source files hold its parts: part k
// checks the seeds from two_roles_cuts[k] to two_roles_cuts[k + 1].  The
// association step of seed 1 and of seed 15 costs more compile time than
// the step of a few other seeds together, so each of the two is a part of
// its own.
inline constexpr std::string_view two_roles_family = "two roles, depth 6";
inline constexpr std::uint64_t two_roles_base = 9000;
inline constexpr std::array<std::size_t, 8> two_roles_cuts{0, 1, 2, 8, 15, 16, 19, 24};

template <std::size_t Part>
void check_two_roles_part(FamilyCounts& counts) {
    constexpr std::size_t first = two_roles_cuts[Part];
    check_family<2, 6, two_roles_base + first, true>(counts, two_roles_family,
                                                     std::make_index_sequence<two_roles_cuts[Part + 1] - first>{});
}

void check_two_roles_part_0(FamilyCounts& counts);
void check_two_roles_part_1(FamilyCounts& counts);
void check_two_roles_part_2(FamilyCounts& counts);
void check_two_roles_part_3(FamilyCounts& counts);
void check_two_roles_part_4(FamilyCounts& counts);
void check_two_roles_part_5(FamilyCounts& counts);
void check_two_roles_part_6(FamilyCounts& counts);

// ── The merge grid ───────────────────────────────────────────────────
//
// Each pair of continuations for a role C that does not see the choice
// between A and B.  The grid holds sends, receives, peers, labels and
// loops in different phases, so each merge rule and the loop unfolding
// of the merge meet each other.  Each pair that the gates accept must
// be live.

using Shapes = std::tuple<
    g::End, g::Msg<GenA, GenC, GenL0, int, g::End>, g::Msg<GenA, GenC, GenL1, int, g::End>,
    g::Msg<GenB, GenC, GenL0, int, g::End>, g::Msg<GenC, GenA, GenL0, int, g::End>,
    g::Msg<GenC, GenA, GenL1, int, g::End>, g::Rec<g::Msg<GenA, GenC, GenL0, int, g::Var>>,
    g::Msg<GenA, GenC, GenL0, int, g::Rec<g::Msg<GenA, GenC, GenL0, int, g::Var>>>,
    g::Rec<g::Msg<GenA, GenC, GenL0, int, g::Msg<GenC, GenB, GenL1, int, g::Var>>>,
    g::Msg<GenA, GenC, GenL1, int, g::Rec<g::Msg<GenA, GenC, GenL0, int, g::Var>>>,
    g::Rec<g::Msg<GenA, GenC, GenL0, int, g::Msg<GenA, GenC, GenL1, int, g::Var>>>,
    g::Msg<GenA, GenC, GenL1, int, g::Rec<g::Msg<GenA, GenC, GenL0, int, g::Msg<GenA, GenC, GenL1, int, g::Var>>>>>;

inline constexpr std::size_t shape_count = std::tuple_size_v<Shapes>;

template <std::size_t Cell>
using grid_t = g::Comm<GenA, GenB, g::Branch<GenL0, int, std::tuple_element_t<Cell / shape_count, Shapes>>,
                       g::Branch<GenL1, int, std::tuple_element_t<Cell % shape_count, Shapes>>>;

// Check the cells First + Offset of the grid.
template <std::size_t First, std::size_t... Offset>
void check_grid(FamilyCounts& counts, std::index_sequence<Offset...>) {
    (check_generated<grid_t<First + Offset>, true>(counts, "merge grid", First + Offset), ...);
}

// Seven source files hold the parts of the grid: part k checks the cells
// from grid_cuts[k] to grid_cuts[k + 1].  A row of the grid holds
// shape_count cells, and a later row holds a longer shape, so a later part
// holds fewer rows.
inline constexpr std::array<std::size_t, 8> grid_cuts{0, 36, 72, 96, 108, 120, 132, 144};

template <std::size_t Part>
void check_grid_part(FamilyCounts& counts) {
    check_grid<grid_cuts[Part]>(counts, std::make_index_sequence<grid_cuts[Part + 1] - grid_cuts[Part]>{});
}

void check_grid_part_0(FamilyCounts& counts);
void check_grid_part_1(FamilyCounts& counts);
void check_grid_part_2(FamilyCounts& counts);
void check_grid_part_3(FamilyCounts& counts);
void check_grid_part_4(FamilyCounts& counts);
void check_grid_part_5(FamilyCounts& counts);
void check_grid_part_6(FamilyCounts& counts);

// ── The groups of attacks ────────────────────────────────────────────
//
// main runs them in this order.

// Controls: the explorer finds the faults of Example 10.
void run_controls();
// The paper's accepted types are live.
void run_accepted();
// What each refusal prevents.
void run_refusals();
// Two sessions, each live, deadlock when interleaved.
void run_interleaving();
// Generated global types.
void run_generated();
// The merge grid.
void run_merge_grid();
// Known limitations: each attack must still succeed.
void run_ledger();

}  // namespace test_session_global_attack
