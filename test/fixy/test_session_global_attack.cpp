// An adversarial campaign against the top-down chain: global types,
// balanced+, projection, association and liveness by construction.
//
// Each attack uses the public API correctly.  The question is whether a
// global type that the gates accept can still give processes that
// deadlock, starve a role, or receive a message they cannot handle.
//
// The processes are the local types of a typing context.  An explorer
// runs them under the asynchronous semantics of Pischke, Masters and
// Yoshida (arXiv 2505.17676 version 4, Definition 9): one FIFO queue for
// each ordered pair of roles, a send appends to the queue of its pair,
// and a receive takes the head.  Each queue has a capacity, and a send
// to a full queue waits.  The explorer visits every reachable state and
// reports:
//
//   - a deadlock: no transition, and a role is not at End or a queue is
//     not empty;
//   - an unsafe state: the head of a queue is a message that its receiver
//     does not offer (Definition 10);
//   - a starved obligation: a fair path on which a queued message is
//     never received (L1) or a waiting role never receives (L2).
//
// Fairness and liveness follow Definition 12.  A fair path takes each
// action class that it enables: a send from p to q (any label), or the
// receive of one label by q from p.  The check is the usual one for
// strong fairness on a finite graph: find a strongly connected set of
// states that keeps the obligation pending and that takes each action
// class it enables; remove the states that enable a class the set never
// takes, and look again.
//
// The explorer is checked first against the three non-live contexts of
// Example 10 of the paper.  A search that finds nothing is worth nothing
// if it cannot find a known fault.
//
// Attacks that succeed on purpose are pinned in known_limitations.  Each
// entry names the attack and the condition of the literature that it
// breaks, and the entry runs its attack each time: an entry whose attack
// stops to succeed is stale and fails this test.  The ledger can only
// shrink.

#include <fixy/session/Liveness.h>

#include <algorithm>
#include <csignal>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <initializer_list>
#include <map>
#include <meta>
#include <string>
#include <string_view>
#include <tuple>
#include <type_traits>
#include <utility>
#include <vector>

#include <unistd.h>

namespace {

namespace g = ::fixy::session::global;
namespace s = ::fixy::session;

// ── Watchdog ─────────────────────────────────────────────────────────
//
// The explorer has a state cap, so it always stops.  The watchdog is a
// second stop in case a change removes the cap.

constexpr unsigned watchdog_seconds = 600;

void on_watchdog(int) {
    constexpr char text[] = "test_session_global_attack: the watchdog stopped the test after 600 seconds\n";
    [[maybe_unused]] const auto written = ::write(2, text, sizeof text - 1);
    std::abort();
}

// ── Names ────────────────────────────────────────────────────────────

// The printable name of a type.  The explorer uses it as the identity of
// a role, a label or a payload.
template <typename T>
consteval std::string_view name_of() {
    return std::string_view{std::define_static_string(std::meta::display_string_of(^^T))};
}

[[noreturn]] void fail(std::string_view what) {
    std::fprintf(stderr, "test_session_global_attack: %.*s\n", static_cast<int>(what.size()), what.data());
    std::abort();
}

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
std::vector<PayloadPair> payload_order;

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

void set_branches(System& sys, int node, std::initializer_list<BranchSpec> specs) {
    std::vector<Branch> branches;
    for (const BranchSpec& spec : specs) {
        branches.push_back(Branch{sys.role_of(spec.peer), sys.message_of(spec.label, spec.payload), spec.next});
    }
    sys.nodes[static_cast<std::size_t>(node)].branches = std::move(branches);
}

void set_alias(System& sys, int node, int target) {
    sys.nodes[static_cast<std::size_t>(node)].alias = target;
}

void add_queued(System& sys, std::string_view from, std::string_view to, std::string_view label,
                std::string_view payload) {
    sys.initial_queues[sys.pair_of(sys.role_of(from), sys.role_of(to))].push_back(sys.message_of(label, payload));
}

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
        set_branches(sys, self, {BranchSpec{name_of<Q>(), name_of<L>(), name_of<P>(), LocalBuild<K>::build(sys, loop)}});
        return self;
    }
};

template <typename Q, typename L, typename P, typename K>
struct LocalBuild<s::Recv<s::PeerMsg<Q, L, P>, K>> {
    static int build(System& sys, int loop) {
        const int self = sys.add_node(NodeKind::External);
        set_branches(sys, self, {BranchSpec{name_of<Q>(), name_of<L>(), name_of<P>(), LocalBuild<K>::build(sys, loop)}});
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
        for (int& entry : sys.start) entry = sys.resolve(entry);
        (QueueLoad<Rs, Qs>::load(sys), ...);
        return sys;
    }
};

template <typename Ctx>
System load_context() {
    return ContextLoad<Ctx>::load();
}

// ── Exploration ──────────────────────────────────────────────────────

struct Action {
    bool is_send = false;
    int actor = -1;
    int peer = -1;
    int message = -1;
};

// The fairness class of an action: a send is one class per pair, a
// receive is one class per pair and label.
[[nodiscard]] std::uint64_t class_of(const Action& action) {
    const auto actor = static_cast<std::uint64_t>(action.actor);
    const auto peer = static_cast<std::uint64_t>(action.peer);
    if (action.is_send) return (actor << 40U) | (peer << 20U);
    return (std::uint64_t{1} << 62U) | (actor << 40U) | (peer << 20U) | static_cast<std::uint64_t>(action.message);
}

struct State {
    std::vector<int> at;
    std::vector<std::vector<int>> queues;

    [[nodiscard]] std::vector<int> key() const {
        std::vector<int> flat = at;
        for (const std::vector<int>& queue : queues) {
            flat.push_back(-1);
            flat.insert(flat.end(), queue.begin(), queue.end());
        }
        return flat;
    }
};

struct Edge {
    int from = -1;
    int to = -1;
    Action action;
};

struct Exploration {
    std::vector<State> states;
    std::vector<Edge> edges;
    std::vector<std::vector<std::uint64_t>> enabled;
    std::vector<int> deadlocks;
    std::vector<int> unsafe;
    bool capped = false;
};

constexpr std::size_t state_cap = 200000;

[[nodiscard]] bool is_final(const System& sys, const State& state) {
    for (const int node : state.at) {
        if (sys.nodes[static_cast<std::size_t>(node)].kind != NodeKind::End) return false;
    }
    return std::ranges::all_of(state.queues, [](const std::vector<int>& queue) { return queue.empty(); });
}

// Visit every reachable state, breadth first.  Complexity: linear in the
// number of states and edges, with a logarithmic factor for the visited
// map.  Stops at state_cap and sets capped.
[[nodiscard]] Exploration explore(const System& sys, std::size_t capacity) {
    Exploration out;
    std::map<std::vector<int>, int> seen;
    State initial{sys.start, sys.initial_queues};
    seen.emplace(initial.key(), 0);
    out.states.push_back(std::move(initial));

    const auto visit = [&](State next) -> int {
        auto key = next.key();
        if (const auto found = seen.find(key); found != seen.end()) return found->second;
        const int index = static_cast<int>(out.states.size());
        seen.emplace(std::move(key), index);
        out.states.push_back(std::move(next));
        return index;
    };

    for (std::size_t current = 0; current < out.states.size(); ++current) {
        if (out.states.size() > state_cap) {
            out.capped = true;
            break;
        }
        out.enabled.emplace_back();
        bool moved = false;
        bool bad = false;
        for (std::size_t role = 0; role < sys.roles.size(); ++role) {
            const State here = out.states[current];
            const Node& node = sys.nodes[static_cast<std::size_t>(here.at[role])];
            if (node.kind == NodeKind::Internal) {
                for (const Branch& branch : node.branches) {
                    const std::size_t pair = sys.pair_of(static_cast<int>(role), branch.peer);
                    if (here.queues[pair].size() >= capacity) continue;
                    State next = here;
                    next.at[role] = sys.resolve(branch.next);
                    next.queues[pair].push_back(branch.message);
                    const Action action{true, static_cast<int>(role), branch.peer, branch.message};
                    out.enabled[current].push_back(class_of(action));
                    const int target = visit(std::move(next));
                    out.edges.push_back(Edge{static_cast<int>(current), target, action});
                    moved = true;
                }
            } else if (node.kind == NodeKind::External) {
                const int peer = node.branches.front().peer;
                const std::size_t pair = sys.pair_of(peer, static_cast<int>(role));
                if (here.queues[pair].empty()) continue;
                const int head = here.queues[pair].front();
                const auto match = std::ranges::find_if(
                    node.branches, [&](const Branch& branch) { return branch.peer == peer && sys.fits(head, branch.message); });
                if (match == node.branches.end()) {
                    bad = true;
                    continue;
                }
                State next = here;
                next.at[role] = sys.resolve(match->next);
                next.queues[pair].erase(next.queues[pair].begin());
                const Action action{false, static_cast<int>(role), peer, head};
                out.enabled[current].push_back(class_of(action));
                const int target = visit(std::move(next));
                out.edges.push_back(Edge{static_cast<int>(current), target, action});
                moved = true;
            }
        }
        if (bad) out.unsafe.push_back(static_cast<int>(current));
        if (!moved && !is_final(sys, out.states[current])) out.deadlocks.push_back(static_cast<int>(current));
    }
    out.enabled.resize(out.states.size());
    return out;
}

// ── Liveness (Definition 12) ─────────────────────────────────────────

struct Obligation {
    bool is_queue = false;  // L1 when true, L2 when false
    int first = -1;         // L1: the sender; L2: the waiting role
    int second = -1;        // L1: the receiver; L2: the peer it waits on
    int message = -1;       // L1 only

    auto operator<=>(const Obligation&) const = default;
};

[[nodiscard]] bool is_pending(const System& sys, const State& state, const Obligation& ob) {
    if (ob.is_queue) {
        const std::vector<int>& queue = state.queues[sys.pair_of(ob.first, ob.second)];
        return !queue.empty() && queue.front() == ob.message;
    }
    const Node& node = sys.nodes[static_cast<std::size_t>(state.at[static_cast<std::size_t>(ob.first)])];
    return node.kind == NodeKind::External && node.branches.front().peer == ob.second;
}

[[nodiscard]] bool discharges(const Action& action, const Obligation& ob) {
    if (action.is_send) return false;
    if (ob.is_queue) return action.actor == ob.second && action.peer == ob.first && action.message == ob.message;
    return action.actor == ob.first && action.peer == ob.second;
}

// Tarjan's algorithm over the member states and the given edges.
// Complexity: linear in states and edges.
[[nodiscard]] std::vector<std::vector<int>> components(const std::vector<std::vector<int>>& adjacency,
                                                       const std::vector<char>& member) {
    const std::size_t count = adjacency.size();
    std::vector<int> index(count, -1);
    std::vector<int> low(count, 0);
    std::vector<char> on_stack(count, 0);
    std::vector<int> stack;
    std::vector<std::vector<int>> result;
    int counter = 0;
    struct Frame {
        int node;
        std::size_t next_edge;
    };
    for (std::size_t root = 0; root < count; ++root) {
        if (!member[root] || index[root] >= 0) continue;
        std::vector<Frame> frames{Frame{static_cast<int>(root), 0}};
        index[root] = low[root] = counter++;
        stack.push_back(static_cast<int>(root));
        on_stack[root] = 1;
        while (!frames.empty()) {
            Frame& frame = frames.back();
            const auto node = static_cast<std::size_t>(frame.node);
            if (frame.next_edge < adjacency[node].size()) {
                const auto target = static_cast<std::size_t>(adjacency[node][frame.next_edge++]);
                if (!member[target]) continue;
                if (index[target] < 0) {
                    index[target] = low[target] = counter++;
                    stack.push_back(static_cast<int>(target));
                    on_stack[target] = 1;
                    frames.push_back(Frame{static_cast<int>(target), 0});
                } else if (on_stack[target]) {
                    low[node] = std::min(low[node], index[target]);
                }
                continue;
            }
            if (low[node] == index[node]) {
                std::vector<int> component;
                for (;;) {
                    const int top = stack.back();
                    stack.pop_back();
                    on_stack[static_cast<std::size_t>(top)] = 0;
                    component.push_back(top);
                    if (top == frame.node) break;
                }
                result.push_back(std::move(component));
            }
            const int finished = frame.node;
            frames.pop_back();
            if (!frames.empty()) {
                const auto parent = static_cast<std::size_t>(frames.back().node);
                low[parent] = std::min(low[parent], low[static_cast<std::size_t>(finished)]);
            }
        }
    }
    return result;
}

// True when a fair infinite path stays in the member states and never
// discharges the obligation.  The obligation stays pending along each
// edge that does not discharge it, so such a path starves it.
[[nodiscard]] bool fair_starvation(const Exploration& ex, const Obligation& ob, std::vector<char> member) {
    const std::size_t count = ex.states.size();
    std::vector<std::vector<int>> adjacency(count);
    std::vector<std::vector<std::uint64_t>> edge_class(count);
    for (const Edge& edge : ex.edges) {
        const auto from = static_cast<std::size_t>(edge.from);
        const auto to = static_cast<std::size_t>(edge.to);
        if (!member[from] || discharges(edge.action, ob)) continue;
        if (!member[to]) fail("an edge that does not discharge an obligation leaves the states that hold it");
        adjacency[from].push_back(edge.to);
        edge_class[from].push_back(class_of(edge.action));
    }
    std::vector<std::vector<char>> work{std::move(member)};
    while (!work.empty()) {
        const std::vector<char> current = std::move(work.back());
        work.pop_back();
        for (const std::vector<int>& component : components(adjacency, current)) {
            std::vector<char> inside(count, 0);
            for (const int node : component) inside[static_cast<std::size_t>(node)] = 1;
            std::vector<std::uint64_t> taken;
            bool has_cycle = false;
            for (const int node : component) {
                const auto from = static_cast<std::size_t>(node);
                for (std::size_t edge = 0; edge < adjacency[from].size(); ++edge) {
                    if (!inside[static_cast<std::size_t>(adjacency[from][edge])]) continue;
                    has_cycle = true;
                    taken.push_back(edge_class[from][edge]);
                }
            }
            if (!has_cycle) continue;
            std::vector<char> keep(count, 0);
            bool removed_one = false;
            for (const int node : component) {
                const auto state = static_cast<std::size_t>(node);
                const bool enables_untaken = std::ranges::any_of(ex.enabled[state], [&](std::uint64_t enabled_class) {
                    return std::ranges::find(taken, enabled_class) == taken.end();
                });
                if (enables_untaken) {
                    removed_one = true;
                } else {
                    keep[state] = 1;
                }
            }
            if (!removed_one) return true;
            if (std::ranges::any_of(keep, [](char flag) { return flag != 0; })) work.push_back(std::move(keep));
        }
    }
    return false;
}

struct Verdict {
    std::size_t states = 0;
    std::size_t deadlocks = 0;
    std::size_t unsafe = 0;
    std::size_t starved = 0;
    bool capped = false;

    [[nodiscard]] bool is_clean() const { return deadlocks == 0 && unsafe == 0 && starved == 0 && !capped; }
};

[[nodiscard]] Verdict analyse(const System& sys, std::size_t capacity) {
    const Exploration ex = explore(sys, capacity);
    Verdict verdict;
    verdict.states = ex.states.size();
    verdict.deadlocks = ex.deadlocks.size();
    verdict.unsafe = ex.unsafe.size();
    verdict.capped = ex.capped;
    if (ex.capped) return verdict;
    std::vector<Obligation> obligations;
    for (const State& state : ex.states) {
        for (std::size_t from = 0; from < sys.roles.size(); ++from) {
            for (std::size_t to = 0; to < sys.roles.size(); ++to) {
                const std::vector<int>& queue = state.queues[sys.pair_of(static_cast<int>(from), static_cast<int>(to))];
                if (!queue.empty()) {
                    obligations.push_back(Obligation{true, static_cast<int>(from), static_cast<int>(to), queue.front()});
                }
            }
            const Node& node = sys.nodes[static_cast<std::size_t>(state.at[from])];
            if (node.kind == NodeKind::External) {
                obligations.push_back(Obligation{false, static_cast<int>(from), node.branches.front().peer, -1});
            }
        }
    }
    std::ranges::sort(obligations);
    const auto [first_duplicate, last] = std::ranges::unique(obligations);
    obligations.erase(first_duplicate, last);
    for (const Obligation& ob : obligations) {
        std::vector<char> member(ex.states.size(), 0);
        for (std::size_t state = 0; state < ex.states.size(); ++state) {
            member[state] = is_pending(sys, ex.states[state], ob) ? 1 : 0;
        }
        if (fair_starvation(ex, ob, std::move(member))) ++verdict.starved;
    }
    return verdict;
}

// ── Reporting ────────────────────────────────────────────────────────

int failures = 0;

void expect(bool holds, std::string_view what) {
    if (holds) return;
    ++failures;
    std::fprintf(stderr, "FAIL: %.*s\n", static_cast<int>(what.size()), what.data());
}

void print_verdict(std::string_view label, const Verdict& verdict) {
    std::printf("  %-58.*s states=%-6zu deadlock=%zu unsafe=%zu starved=%zu%s\n", static_cast<int>(label.size()),
                label.data(), verdict.states, verdict.deadlocks, verdict.unsafe, verdict.starved,
                verdict.capped ? " CAPPED" : "");
}

// Run the context at each capacity and require the expected cleanliness.
void run_context(const System& sys, std::string_view label, bool expect_clean) {
    for (const std::size_t capacity : {std::size_t{1}, std::size_t{2}, std::size_t{3}}) {
        const Verdict verdict = analyse(sys, capacity);
        std::string line{label};
        line += " (capacity ";
        line += std::to_string(capacity);
        line += ")";
        print_verdict(line, verdict);
        expect(!verdict.capped, line + ": the state cap stopped the explorer");
        expect(verdict.is_clean() == expect_clean,
               line + (expect_clean ? ": expected live, the explorer found a fault" : ": expected a fault, found none"));
    }
}

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
//             context must be live.
//   unfolded  each top-level Loop is unfolded once.  Association must
//             hold, and the context must be live.
//   widened   each Select gains a branch at the end, with a label that
//             no role offers.  Association must refuse.
//   narrowed  each Offer with two or more branches loses its last one.
//             Association must refuse.
//   swapped   each Offer with two or more branches swaps its first two.
//             Association must refuse, because branches match by
//             position.  The explorer shows such a context safe, so this
//             refusal costs completeness, not safety.
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

struct NeverSent {};
using Checked = ::fixy::Tagged<int, ::fixy::tags::source::Sanitized>;
static_assert(s::is_payload_subsort_v<Checked, int>);
static_assert(s::is_payload_subsort_v<s::PeerMsg<NeverSent, NeverSent, Checked>, s::PeerMsg<NeverSent, NeverSent, int>>);
static_assert(!s::is_payload_subsort_v<s::PeerMsg<NeverSent, NeverSent, int>, s::PeerMsg<NeverSent, NeverSent, Checked>>);

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
        using type = s::Select<First, Bs..., s::Send<s::PeerMsg<typename send_peer<First>::type, NeverSent, int>, s::End>>;
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
[[nodiscard]] System rewritten(const System& base, GraphRewrite kind) {
    System sys = base;
    const std::string_view never_sent = name_of<NeverSent>();
    const std::string_view plain = name_of<int>();
    const std::string_view checked = name_of<Checked>();
    const std::size_t original = sys.nodes.size();
    for (std::size_t index = 0; index < original; ++index) {
        const NodeKind kind_of_node = sys.nodes[index].kind;
        if (kind_of_node == NodeKind::Internal) {
            if (kind == GraphRewrite::Safe) {
                sys.nodes[index].branches.resize(1);
                Branch& kept = sys.nodes[index].branches.front();
                const Message sent = sys.messages[static_cast<std::size_t>(kept.message)];
                if (sent.payload == plain) kept.message = sys.message_of(sent.label, checked);
            } else if (kind == GraphRewrite::Widen) {
                const int peer = sys.nodes[index].branches.front().peer;
                const int message = sys.message_of(never_sent, plain);
                const int end = sys.add_node(NodeKind::End);
                sys.nodes[index].branches.push_back(Branch{peer, message, end});
            }
        } else if (kind_of_node == NodeKind::External) {
            std::vector<Branch>& branches = sys.nodes[index].branches;
            if (kind == GraphRewrite::Safe) {
                const int peer = branches.front().peer;
                const int message = sys.message_of(never_sent, plain);
                const int end = sys.add_node(NodeKind::End);
                sys.nodes[index].branches.push_back(Branch{peer, message, end});
            } else if (kind == GraphRewrite::Narrow && branches.size() >= 2) {
                branches.pop_back();
            } else if (kind == GraphRewrite::Swap && branches.size() >= 2) {
                std::swap(branches[0], branches[1]);
            }
        }
    }
    return sys;
}

struct AssociationCounts {
    std::size_t types = 0;
    std::size_t full_types = 0;
    std::size_t safe_live = 0;
    std::size_t unfolded_live = 0;
    std::size_t refused = 0;
    std::size_t refused_faulty = 0;
    std::size_t swapped_refused = 0;
    std::size_t swapped_safe = 0;
};
AssociationCounts association_counts;

[[nodiscard]] bool is_clean_at_small_capacities(const System& sys) {
    return analyse(sys, 1).is_clean() && analyse(sys, 2).is_clean();
}

// A rewrite that association must accept: it must hold, and the context
// must be live.
void judge_accepted(std::string_view label, std::string_view rewrite, bool associated, const System& sys,
                    std::size_t& live) {
    std::string what{label};
    what += ", ";
    what += rewrite;
    expect(associated, what + ": association refused a safe subtype of the projection");
    if (!associated) return;
    const bool clean = is_clean_at_small_capacities(sys);
    expect(clean, what + ": association accepted the context, and the explorer found a fault");
    if (clean) ++live;
}

// A rewrite that association must refuse.  The explorer tells whether
// the refusal stopped a real fault.
void judge_refused(std::string_view label, std::string_view rewrite, bool associated, const System& sys,
                   std::size_t& refused, std::size_t& faulty_or_safe, bool count_faulty) {
    std::string what{label};
    what += ", ";
    what += rewrite;
    expect(!associated, what + ": association accepted an entry that does not refine its projection");
    if (associated) return;
    ++refused;
    const bool clean = is_clean_at_small_capacities(sys);
    if (count_faulty ? !clean : clean) ++faulty_or_safe;
}

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
    judge_accepted(label, "safe rewrite", s::association_holds_v<Safe, G>, rewritten(base, GraphRewrite::Safe),
                   association_counts.safe_live);
    if constexpr (!std::is_same_v<Widened, Ctx>) {
        judge_refused(label, "widened Select", s::association_holds_v<Widened, G>,
                      rewritten(base, GraphRewrite::Widen), association_counts.refused,
                      association_counts.refused_faulty, true);
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
                          association_counts.refused_faulty, true);
        }
        if constexpr (!std::is_same_v<Swapped, Ctx>) {
            judge_refused(label, "swapped Offer", s::association_holds_v<Swapped, G>,
                          rewritten(base, GraphRewrite::Swap), association_counts.swapped_refused,
                          association_counts.swapped_safe, false);
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

// ── Roles, labels and payloads ───────────────────────────────────────

struct P {};
struct Q {};
struct R {};
struct S {};
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

template <typename Peer, typename Label, typename Cont>
using Out = s::Send<s::PeerMsg<Peer, Label, int>, Cont>;
template <typename Peer, typename Label, typename Cont>
using In = s::Recv<s::PeerMsg<Peer, Label, int>, Cont>;
template <typename Role, typename Local, typename Queue = s::OutQueue<>>
using At = s::RoleState<Role, Queue, Local>;

// ── 1. Controls: the explorer finds the faults of Example 10 ─────────

using Ex10Unsafe = s::TypingContext<At<P, s::End, s::OutQueue<s::Queued<Q, M, int>>>, At<Q, In<P, M1, s::End>>>;
using Ex10Deadlocked = s::TypingContext<At<P, In<Q, M, s::End>>, At<Q, s::End>>;
using Ex10Livelocked = s::TypingContext<At<P, s::Loop<Out<Q, M, s::Continue>>>, At<Q, s::Loop<In<P, M, s::Continue>>>,
                                        At<R, s::End, s::OutQueue<s::Queued<P, M1, int>>>>;

void run_controls() {
    std::printf("controls (Example 10: each must show a fault)\n");
    const System unsafe = load_context<Ex10Unsafe>();
    const Verdict unsafe_verdict = analyse(unsafe, 1);
    print_verdict("Example 10, unsafe", unsafe_verdict);
    expect(unsafe_verdict.unsafe > 0, "the explorer did not find the unsafe state of Example 10");
    const System deadlocked = load_context<Ex10Deadlocked>();
    const Verdict deadlocked_verdict = analyse(deadlocked, 1);
    print_verdict("Example 10, deadlocked", deadlocked_verdict);
    expect(deadlocked_verdict.deadlocks > 0, "the explorer did not find the deadlock of Example 10");
    const System livelocked = load_context<Ex10Livelocked>();
    const Verdict livelocked_verdict = analyse(livelocked, 2);
    print_verdict("Example 10, livelocked", livelocked_verdict);
    expect(livelocked_verdict.deadlocks == 0 && livelocked_verdict.starved > 0,
           "the explorer did not find the starved message of Example 10");
}

// ── 2. The paper's accepted types are live ───────────────────────────

using Ring = g::Rec<g::Msg<P, Q, Add, int,
                           g::Comm<Q, R, g::Branch<Add, int, g::Msg<R, P, Add, int, g::Var>>,
                                   g::Branch<Sub, int, g::Msg<R, P, Sub, int, g::Var>>>>>;
using RingAfterAdd = g::EnRoute<P, Q, Add, int,
                                g::Comm<Q, R, g::Branch<Add, int, g::Msg<R, P, Add, int, Ring>>,
                                        g::Branch<Sub, int, g::Msg<R, P, Sub, int, Ring>>>>;
using Loop1 = g::Rec<g::Msg<P, Q, M1, int, g::Var>>;
using Ex4 = g::Comm<P, R, g::Branch<M1, int, g::EnRoute<Q, P, M, int, Loop1>>,
                    g::Branch<M2, int, g::EnRoute<Q, P, M, int, g::Msg<P, Q, M2, int, Loop1>>>>;
using Tirore3 = g::Msg<P, Q, Kk, int, g::Rec<g::Comm<R, S, g::Branch<L1, int, g::End>, g::Branch<L2, int, g::Var>>>>;
// Equation (5) loops back to an outer binder past an inner one that binds
// nothing, so its nearest-binder form leaves the inner binder out.
// Equation (7), whose inner loop continues both loops, has no form here:
// a Var reaches only the nearest Rec.  That limit costs expressiveness,
// not safety.
using Tirore5 = g::Rec<g::Msg<P, Q, Kk, int, g::Msg<R, S, Kk, int, g::Var>>>;
using Nested = g::Rec<g::Msg<P, Q, M, int, g::Rec<g::Comm<Q, R, g::Branch<L1, int, g::Var>, g::Branch<L2, int, g::End>>>>>;
using FanIn = g::Rec<g::Msg<P, S, X, int, g::Msg<Q, S, Y, int, g::Msg<R, S, Z, int, g::Msg<S, P, M, int, g::Var>>>>>;
using RunAhead = g::Rec<g::Msg<P, Q, X, int, g::Msg<P, R, Y, int, g::Var>>>;
using SentOnly = g::EnRoute<P, Q, M, int, g::End>;
using AfterChoice = g::Comm<P, Q, g::Branch<M1, int, g::Msg<Q, R, M1, int, g::End>>,
                            g::Branch<M2, int, g::Msg<Q, R, M2, int, g::End>>>;
// A merge whose union hides nothing: R learns the branch from P's label.
using MergedUnion = g::Comm<P, Q, g::Branch<L1, int, g::Msg<P, R, M1, int, g::Msg<R, Q, X, int, g::End>>>,
                            g::Branch<L2, int, g::Msg<P, R, M2, int, g::Msg<Q, R, Y, int, g::End>>>>;
// A branch that leaves the outer loop for an inner loop of the same
// roles.
using InnerOnly = g::Rec<g::Comm<P, Q, g::Branch<L1, int, g::Msg<Q, P, X, int, g::Var>>,
                                 g::Branch<L2, int, g::Msg<Q, P, Y, int, g::Rec<g::Msg<P, Q, Z, int, g::Var>>>>>>;
// The same shape with new roles in the inner loop: R and S are roles of
// the outer loop body, and the L1 path loops back without them.
using InnerNewRoles = g::Rec<g::Comm<P, Q, g::Branch<L1, int, g::Msg<Q, P, X, int, g::Var>>,
                                     g::Branch<L2, int, g::Msg<Q, R, Y, int, g::Rec<g::Msg<R, S, Z, int, g::Var>>>>>>;
static_assert(!g::is_balanced_v<InnerNewRoles>);
static_assert(!s::is_live_by_construction_v<InnerNewRoles>);

void run_accepted() {
    std::printf("accepted types (each must be live)\n");
    expect_live_global<Ring>("ring (section 1.2)");
    expect_live_global<RingAfterAdd>("ring after one send (Example 13)");
    expect_live_global<Ex4>("Example 4 with full merge");
    expect_live_global<Tirore3>("Tirore et al. ITP 2023, equation (3)");
    expect_live_global<Tirore5>("Tirore et al. ITP 2023, equation (5)");
    expect_live_global<Nested>("nested loop, outer loop never returns");
    expect_live_global<FanIn>("fan-in of three senders");
    expect_live_global<RunAhead>("one sender runs ahead of two receivers");
    expect_live_global<SentOnly>("a role that only has a queued message");
    expect_live_global<AfterChoice>("a role that appears after a choice");
    expect_live_global<MergedUnion>("a merge that takes the union of labels");
    expect_live_global<InnerOnly>("a branch that enters an inner loop");
}

// ── 3. What each refusal prevents ────────────────────────────────────
//
// For each refused type, a context that a weaker checker would give is
// run in the explorer.  Each must show a fault, so each refusal is
// needed.

// PMY25 Example 12, G1: the coinductive projection types R, but P can
// choose M0 for ever and R starves.
using Ex12G1 = g::Rec<g::Comm<P, Q, g::Branch<M0, int, g::Var>, g::Branch<M1, int, g::Msg<P, R, M, int, g::End>>>>;
static_assert(!g::is_balanced_v<Ex12G1>);
static_assert(!s::projects_v<Ex12G1, R>);
using Ex12G1Coinductive = s::TypingContext<
    At<P, s::Loop<s::Select<Out<Q, M0, s::Continue>, Out<Q, M1, Out<R, M, s::End>>>>>,
    At<Q, s::Loop<s::Offer<s::Sender<P>, In<P, M0, s::Continue>, In<P, M1, s::End>>>>, At<R, In<P, M, s::End>>>;

// PMY25 equation (49): an en-route message behind a transmission of the
// same pair.  The projection that ignores the count is unsafe.
using Ex49 = g::Msg<P, Q, M, int, g::EnRoute<P, Q, M1, int, g::End>>;
static_assert(!g::is_balanced_plus_v<Ex49>);
using Ex49Naive = s::TypingContext<At<P, Out<Q, M, s::End>, s::OutQueue<s::Queued<Q, M1, int>>>, At<Q, In<P, M, In<P, M1, s::End>>>>;

// A role in one branch only: plain merge would need End = Recv.
using OneBranchOnly = g::Comm<P, Q, g::Branch<M1, int, g::Msg<Q, R, M1, int, g::End>>, g::Branch<M2, int, g::End>>;
static_assert(!s::projects_v<OneBranchOnly, R>);
using OneBranchGuess = s::TypingContext<At<P, s::Select<Out<Q, M1, s::End>, Out<Q, M2, s::End>>>,
                                        At<Q, s::Offer<s::Sender<P>, In<P, M1, Out<R, M1, s::End>>, In<P, M2, s::End>>>,
                                        At<R, In<Q, M1, s::End>>>;

// Tirore et al., ECOOP 2025, equation (1).  With a shared queue R reads
// "channel 2" whoever wrote it.  With one queue per pair R must name the
// peer, and a projection that drops the peer guesses one.
using TiroreShared = g::Comm<P, Q, g::Branch<L1, int, g::Msg<Q, R, Kk, bool, g::End>>,
                             g::Branch<L2, int, g::Msg<P, R, Kk, bool, g::End>>>;
static_assert(!s::projects_v<TiroreShared, R>);
using TiroreGuess = s::TypingContext<
    At<P, s::Select<Out<Q, L1, s::End>, Out<Q, L2, s::Send<s::PeerMsg<R, Kk, bool>, s::End>>>>,
    At<Q, s::Offer<s::Sender<P>, In<P, L1, s::Send<s::PeerMsg<R, Kk, bool>, s::End>>, In<P, L2, s::End>>>,
    At<R, s::Recv<s::PeerMsg<Q, Kk, bool>, s::End>>>;

// An internal choice that a role must make without seeing the choice.
using BlindSender = g::Comm<P, Q, g::Branch<L1, int, g::Msg<R, S, X, int, g::End>>,
                            g::Branch<L2, int, g::Msg<R, S, Y, int, g::End>>>;
static_assert(!s::projects_v<BlindSender, R>);
using BlindGuess = s::TypingContext<At<P, s::Select<Out<Q, L1, s::End>, Out<Q, L2, s::End>>>,
                                    At<Q, s::Offer<s::Sender<P>, In<P, L1, s::End>, In<P, L2, s::End>>>,
                                    At<R, Out<S, X, s::End>>, At<S, s::Offer<s::Sender<R>, In<R, Y, s::End>>>>;

// A merge that would hide a deadlock: R sends in one branch and receives
// in the other, from the same peer.
using CrossedMerge = g::Comm<P, Q, g::Branch<L1, int, g::Msg<R, S, X, int, g::End>>,
                             g::Branch<L2, int, g::Msg<S, R, Y, int, g::End>>>;
static_assert(std::is_same_v<s::project_t<CrossedMerge, R>, s::NotProjectable<s::projection_failure::MergeShapeMismatch>>);
using CrossedGuess = s::TypingContext<At<P, s::Select<Out<Q, L1, s::End>, Out<Q, L2, s::End>>>,
                                      At<Q, s::Offer<s::Sender<P>, In<P, L1, s::End>, In<P, L2, s::End>>>,
                                      At<R, In<S, Y, s::End>>, At<S, In<R, X, s::End>>>;

// A role in one branch of an inner loop: unbalanced.
using InnerBranchOnly =
    g::Rec<g::Comm<P, Q, g::Branch<L1, int, g::Msg<Q, R, X, int, g::Var>>, g::Branch<L2, int, g::Var>>>;
static_assert(!g::is_balanced_v<InnerBranchOnly>);
static_assert(!s::is_live_by_construction_v<InnerBranchOnly>);
using InnerBranchCoinductive = s::TypingContext<
    At<P, s::Loop<s::Select<Out<Q, L1, s::Continue>, Out<Q, L2, s::Continue>>>>,
    At<Q, s::Loop<s::Offer<s::Sender<P>, In<P, L1, Out<R, X, s::Continue>>, In<P, L2, s::Continue>>>>,
    At<R, s::Loop<In<Q, X, s::Continue>>>>;

// The old tree projected a role absent from a loop to Loop<Continue>.
// That type spins without an action, and the explorer refuses it.
static_assert(std::is_same_v<typename s::project_t<g::Msg<P, R, M, int, g::Rec<g::Msg<P, Q, M, int, g::Var>>>, R>::local,
                             In<P, M, s::End>>);

// PMY25 Example 12, G2: refused as unbalanced, although its coinductive
// context is live.  The paper refuses it because the global type does not
// follow the context: S can send before P chooses M1.  The refusal costs
// completeness, not safety.
using Ex12G2 = g::Rec<g::Comm<P, Q, g::Branch<M0, int, g::Var>, g::Branch<M1, int, g::Msg<S, R, M, int, g::End>>>>;
static_assert(!s::is_live_by_construction_v<Ex12G2>);
using Ex12G2Coinductive = s::TypingContext<
    At<P, s::Loop<s::Select<Out<Q, M0, s::Continue>, Out<Q, M1, s::End>>>>,
    At<Q, s::Loop<s::Offer<s::Sender<P>, In<P, M0, s::Continue>, In<P, M1, s::End>>>>, At<R, In<S, M, s::End>>,
    At<S, Out<R, M, s::End>>>;

void run_refusals() {
    std::printf("refused types (each weaker context must show a fault)\n");
    expect_context<Ex12G2Coinductive>("Example 12 G2 (refused, but its context is live)", true);
    expect_context<Ex12G1Coinductive>("Example 12 G1, coinductive projection", false);
    expect_context<Ex49Naive>("equation (49), count ignored", false);
    expect_context<OneBranchGuess>("a role in one branch only", false);
    expect_context<TiroreGuess>("ECOOP 2025 equation (1), peer guessed", false);
    expect_context<BlindGuess>("a blind internal choice", false);
    expect_context<CrossedGuess>("a crossed merge", false);
    expect_context<InnerBranchCoinductive>("a role in one branch of a loop", false);
}

// ── 4. Two sessions, each live, deadlock when interleaved ────────────
//
// Liveness by construction holds for one session.  Two processes that
// each play one role in two sessions, and wait on the sessions in
// opposite orders, deadlock.  Each session alone is live.

using SessionOne = g::Msg<P, Q, X, int, g::End>;
using SessionTwo = g::Msg<Q, P, Y, int, g::End>;
static_assert(s::is_live_by_construction_v<SessionOne> && s::is_live_by_construction_v<SessionTwo>);

// Opposite orders: P waits on session two first, Q on session one.
using ProcessP = s::compose_t<typename s::project_t<SessionTwo, P>::local, typename s::project_t<SessionOne, P>::local>;
using ProcessQ = s::compose_t<typename s::project_t<SessionOne, Q>::local, typename s::project_t<SessionTwo, Q>::local>;
using CrossedSessions = s::TypingContext<At<P, ProcessP>, At<Q, ProcessQ>>;

// One priority order: both processes use session one before session two.
using OrderedP = s::compose_t<typename s::project_t<SessionOne, P>::local, typename s::project_t<SessionTwo, P>::local>;
using OrderedSessions = s::TypingContext<At<P, OrderedP>, At<Q, ProcessQ>>;

// Three processes in a ring of three sessions, each waiting on its left
// neighbour first.
using SessionPQ = g::Msg<P, Q, X, int, g::End>;
using SessionQR = g::Msg<Q, R, X, int, g::End>;
using SessionRP = g::Msg<R, P, X, int, g::End>;
using RingP = s::compose_t<typename s::project_t<SessionRP, P>::local, typename s::project_t<SessionPQ, P>::local>;
using RingQ = s::compose_t<typename s::project_t<SessionPQ, Q>::local, typename s::project_t<SessionQR, Q>::local>;
using RingR = s::compose_t<typename s::project_t<SessionQR, R>::local, typename s::project_t<SessionRP, R>::local>;
using CrossedRing = s::TypingContext<At<P, RingP>, At<Q, RingQ>, At<R, RingR>>;

[[nodiscard]] bool crossed_sessions_deadlock() {
    const System two = load_context<CrossedSessions>();
    const System three = load_context<CrossedRing>();
    return analyse(two, 1).deadlocks > 0 && analyse(three, 1).deadlocks > 0;
}

void run_interleaving() {
    std::printf("two sessions, each live alone\n");
    expect_live_global<SessionOne>("session one alone");
    expect_live_global<SessionTwo>("session two alone");
    const System crossed = load_context<CrossedSessions>();
    print_verdict("both processes, opposite orders", analyse(crossed, 1));
    const System ordered = load_context<OrderedSessions>();
    const Verdict ordered_verdict = analyse(ordered, 1);
    print_verdict("both processes, one priority order", ordered_verdict);
    expect(ordered_verdict.is_clean(), "two sessions used in one priority order must not deadlock");
    const System ring = load_context<CrossedRing>();
    print_verdict("three processes, three sessions in a ring", analyse(ring, 1));
}

// ── 5. Two gaps that this campaign found, and their repairs ──────────

// Association used to require only well-formedness.  The projected
// context of equation (49) matches each projection exactly, so it was
// associated, and the explorer shows it unsafe.  Association now asks
// for balanced+.
static_assert(g::is_global_well_formed_v<Ex49>);
static_assert(std::is_same_v<s::projected_context_t<Ex49>, Ex49Naive>);
static_assert(!s::association_holds_v<Ex49Naive, Ex49>);

// The binary view used to accept a local type with two peers.  The
// ring's P sends to Q and receives from R, and one binary channel would
// carry both.  The view now requires one peer.
template <typename Local>
concept admits_binary_view = requires { typename s::strip_peers_t<Local>; };
using RingProjectionP = typename s::project_t<Ring, P>::local;
static_assert(std::is_same_v<s::local_peers_t<RingProjectionP>, g::Roles<Q, R>>);
static_assert(!admits_binary_view<RingProjectionP>);
static_assert(admits_binary_view<typename s::project_t<SessionOne, P>::local>);

// ── 6. Generated global types ────────────────────────────────────────
//
// A fixed-seed generator builds global types over four roles and three
// labels.  Each type that the gates accept must give a live context at
// each capacity.  Most generated types are refused, and the counts are
// printed.

struct GenA {};
struct GenB {};
struct GenC {};
struct GenD {};
struct GenL0 {};
struct GenL1 {};
struct GenL2 {};
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
        return std::type_identity<g::Msg<From, To, Label, int, typename Gen<Seed * 3 + 1, Depth - 1, InLoop, RoleCount>::type>>{};
    } else if constexpr (pick < 13) {
        return std::type_identity<g::Comm<From, To, g::Branch<GenL0, int, typename Gen<Seed * 5 + 2, Depth - 1, InLoop, RoleCount>::type>,
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

// ── En-route variants: the first send of a generated type ────────────
//
// The variant is the global type after the sender of the first
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
void check_system(const System& sys, std::string_view family, std::uint64_t seed) {
    for (const std::size_t capacity : {std::size_t{1}, std::size_t{2}}) {
        const Verdict verdict = analyse(sys, capacity);
        if (verdict.is_clean()) continue;
        std::string what{family};
        what += ": seed ";
        what += std::to_string(seed);
        what += " is live by construction, but the explorer found a fault at capacity ";
        what += std::to_string(capacity);
        expect(false, what);
        print_verdict(what, verdict);
    }
}

template <typename G>
void check_live(std::string_view family, std::uint64_t seed) {
    static_assert(ProjectionsWellFormed<G, g::roles_t<G>>::value);
    check_system(load_context<s::projected_context_t<G>>(), family, seed);
}

void report_variant_not_live(std::string_view family, std::uint64_t seed) {
    std::string what{family};
    what += ": the first send of a live type is not live by construction, seed ";
    what += std::to_string(seed);
    expect(false, what);
}

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

// Half of the seeds start the type with a loop, so that balancedness and
// the merge of loop-backs are exercised.
template <std::uint64_t Seed, int Depth, std::size_t RoleCount>
using generated_t = std::conditional_t<Seed % 2 == 0, typename Gen<Seed, Depth, false, RoleCount>::type,
                                       g::Rec<g::Msg<GenA, GenB, GenL0, int, typename Gen<Seed, Depth, true, RoleCount>::type>>>;

template <std::size_t RoleCount, int Depth, std::uint64_t Base, bool FullAssociation, std::size_t... Index>
void run_family(std::string_view family, std::index_sequence<Index...>) {
    FamilyCounts counts;
    (check_generated<generated_t<Base + Index, Depth, RoleCount>, FullAssociation>(counts, family, Base + Index), ...);
    std::printf("  %-34.*s generated=%zu well_formed=%zu balanced_plus=%zu live=%zu variants_live=%zu/%zu "
                "projectable_but_unbalanced=%zu\n",
                static_cast<int>(family.size()), family.data(), counts.generated, counts.well_formed,
                counts.balanced_plus, counts.live_by_construction, counts.variants_live, counts.variants,
                counts.projectable_but_unbalanced);
    expect(counts.live_by_construction > 0, std::string{family} + ": the generator produced no live type");
    expect(counts.projectable_but_unbalanced == 0,
           std::string{family} + ": a type projects onto every role but is not balanced");
}

void run_generated() {
    std::printf("generated global types (fixed seeds)\n");
    run_family<3, 4, 1000, false>("three roles, depth 4", std::make_index_sequence<48>{});
    run_family<4, 4, 5000, false>("four roles, depth 4", std::make_index_sequence<48>{});
    run_family<2, 6, 9000, true>("two roles, depth 6", std::make_index_sequence<24>{});
}

// ── The merge grid ───────────────────────────────────────────────────
//
// Each pair of continuations for a role C that does not see the choice
// between A and B.  The grid holds sends, receives, peers, labels and
// loops in different phases, so each merge rule and the loop unfolding
// of the merge meet each other.  Each pair that the gates accept must
// be live.

using Shapes = std::tuple<
    g::End,
    g::Msg<GenA, GenC, GenL0, int, g::End>,
    g::Msg<GenA, GenC, GenL1, int, g::End>,
    g::Msg<GenB, GenC, GenL0, int, g::End>,
    g::Msg<GenC, GenA, GenL0, int, g::End>,
    g::Msg<GenC, GenA, GenL1, int, g::End>,
    g::Rec<g::Msg<GenA, GenC, GenL0, int, g::Var>>,
    g::Msg<GenA, GenC, GenL0, int, g::Rec<g::Msg<GenA, GenC, GenL0, int, g::Var>>>,
    g::Rec<g::Msg<GenA, GenC, GenL0, int, g::Msg<GenC, GenB, GenL1, int, g::Var>>>,
    g::Msg<GenA, GenC, GenL1, int, g::Rec<g::Msg<GenA, GenC, GenL0, int, g::Var>>>,
    g::Rec<g::Msg<GenA, GenC, GenL0, int, g::Msg<GenA, GenC, GenL1, int, g::Var>>>,
    g::Msg<GenA, GenC, GenL1, int, g::Rec<g::Msg<GenA, GenC, GenL0, int, g::Msg<GenA, GenC, GenL1, int, g::Var>>>>>;

constexpr std::size_t shape_count = std::tuple_size_v<Shapes>;

template <std::size_t Cell>
using grid_t = g::Comm<GenA, GenB, g::Branch<GenL0, int, std::tuple_element_t<Cell / shape_count, Shapes>>,
                       g::Branch<GenL1, int, std::tuple_element_t<Cell % shape_count, Shapes>>>;

template <std::size_t... Cell>
void run_grid(std::index_sequence<Cell...>) {
    FamilyCounts counts;
    (check_generated<grid_t<Cell>, true>(counts, "merge grid", Cell), ...);
    std::printf("  %-34s generated=%zu live=%zu variants_live=%zu/%zu projectable_but_unbalanced=%zu\n", "merge grid",
                counts.generated, counts.live_by_construction, counts.variants_live, counts.variants,
                counts.projectable_but_unbalanced);
    expect(counts.live_by_construction > shape_count, "the merge grid accepted too few pairs to mean anything");
}

// ── Known limitations ────────────────────────────────────────────────

struct KnownLimitation {
    std::string_view attack;
    std::string_view broken_condition;
    bool (*still_succeeds)();
};

constexpr KnownLimitation known_limitations[] = {
    {"Two processes use two live sessions in opposite orders, and three processes use three sessions in a ring; "
     "each deadlocks.",
     "Liveness by construction covers one session (Pischke, Masters, Yoshida, Theorem 13; Pischke, Yoshida, "
     "Top-down = Bottom-up, p. 25).  Freedom from deadlock across sessions needs an acyclic ownership of channels, "
     "where a channel is made with the peer that holds its other end (LinearActris, POPL 2024), or a priority order "
     "on sessions (Dardha and Gay, Prioritised GV).  This header set has neither.",
     &crossed_sessions_deadlock},
};

// The ledger only shrinks.  Raise this bound only with a new attack that
// no gate can refuse, and name it above.
static_assert(std::size(known_limitations) <= 1);

void run_ledger() {
    std::printf("known limitations (each attack must still succeed)\n");
    for (const KnownLimitation& entry : known_limitations) {
        const bool succeeds = entry.still_succeeds();
        std::printf("  %s %.*s\n", succeeds ? "PINNED" : "STALE ", static_cast<int>(entry.attack.size()),
                    entry.attack.data());
        expect(succeeds, std::string{"stale ledger entry, remove it: "} + std::string{entry.attack});
    }
}

void report_association_step() {
    const AssociationCounts& c = association_counts;
    std::printf("association step over %zu accepted types, %zu of them with all five rewrites\n", c.types,
                c.full_types);
    std::printf("  safe rewrites associated and live: %zu, unfolded associated and live: %zu\n", c.safe_live,
                c.unfolded_live);
    std::printf("  widened or narrowed rewrites refused: %zu, of which the explorer shows faulty: %zu\n", c.refused,
                c.refused_faulty);
    std::printf("  swapped rewrites refused: %zu, of which the explorer shows safe: %zu\n", c.swapped_refused,
                c.swapped_safe);
    expect(c.safe_live == c.types && c.unfolded_live == c.full_types, "a safe rewrite failed the association step");
    expect(c.refused > 0 && c.refused_faulty > 0, "no refused rewrite showed a fault, so the step proves nothing");
    expect(c.swapped_refused > 0, "no swapped rewrite was built, so the position rule is untested");
}

}  // namespace

int main() {
    std::signal(SIGALRM, on_watchdog);
    ::alarm(watchdog_seconds);
    payload_order.push_back(PayloadPair{name_of<Checked>(), name_of<int>()});
    run_controls();
    run_accepted();
    run_refusals();
    run_interleaving();
    run_generated();
    std::printf("merge grid (%zu pairs)\n", shape_count * shape_count);
    run_grid(std::make_index_sequence<shape_count * shape_count>{});
    report_association_step();
    run_ledger();
    if (failures != 0) {
        std::fprintf(stderr, "test_session_global_attack: %d failure(s)\n", failures);
        return 1;
    }
    std::printf("test_session_global_attack: all attacks refused or pinned\n");
    return 0;
}
