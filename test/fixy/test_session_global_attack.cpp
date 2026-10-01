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
//
// The test is several source files of one executable, so that no
// translation unit holds every attack:
//
//   session_global_attack.h       the shared part
//   this file                     the explorer, the liveness check, the
//                                 report and main
//   ..._hand.cpp                  the controls, the accepted and refused
//                                 types, the interleaved sessions and the
//                                 ledger
//   ..._generated.cpp             the generated families, and the family
//                                 of three roles
//   ..._four_roles.cpp            the family of four roles
//   ..._two_roles_<k>.cpp         part k of the family of two roles
//   ..._grid_<k>.cpp              part k of the cells of the merge grid.
//                                 Part 0 also runs the grid

#include "session_global_attack.h"

#include <algorithm>
#include <csignal>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <initializer_list>
#include <map>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <unistd.h>

namespace test_session_global_attack {

std::vector<PayloadPair> payload_order;
AssociationCounts association_counts;

namespace {

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
                const auto match = std::ranges::find_if(node.branches, [&](const Branch& branch) {
                    return branch.peer == peer && sys.fits(head, branch.message);
                });
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
    int first = -1;  // L1: the sender; L2: the waiting role
    int second = -1;  // L1: the receiver; L2: the peer it waits on
    int message = -1;  // L1 only

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
            for (const int node : component)
                inside[static_cast<std::size_t>(node)] = 1;
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

// For each node, true when a path of its own local type leads from it to
// End.  One pass per level of loop-back.  Complexity: O(N·d) for N nodes
// and d levels of loops inside loops.
[[nodiscard]] std::vector<char> nodes_that_can_end(const System& sys) {
    std::vector<char> can_end(sys.nodes.size(), 0);
    for (bool is_changed = true; is_changed;) {
        is_changed = false;
        for (std::size_t index = 0; index < sys.nodes.size(); ++index) {
            if (can_end[index] != 0) continue;
            const Node& node = sys.nodes[index];
            bool reaches = node.kind == NodeKind::End;
            if (node.kind == NodeKind::Alias) reaches = can_end[static_cast<std::size_t>(sys.resolve(node.alias))] != 0;
            for (const Branch& branch : node.branches) {
                reaches = reaches || can_end[static_cast<std::size_t>(sys.resolve(branch.next))] != 0;
            }
            if (reaches) {
                can_end[index] = 1;
                is_changed = true;
            }
        }
    }
    return can_end;
}

// True when a reachable state of the rewritten context cannot reach a
// final state, while each role stands at a node that can end in the
// projected context.  The rewrite keeps the node indices of the
// projected context, so a node names the same position in both.  Such a
// state is an exit that the rewrite removed.  A backward search from the
// final states.  Complexity: linear in the number of states and edges.
[[nodiscard]] bool loses_an_exit(const System& projected, const System& sys, std::size_t capacity) {
    const Exploration ex = explore(sys, capacity);
    if (ex.capped) return false;
    const std::vector<char> could_end = nodes_that_can_end(projected);
    const std::size_t count = ex.states.size();
    std::vector<std::vector<int>> predecessors(count);
    for (const Edge& edge : ex.edges)
        predecessors[static_cast<std::size_t>(edge.to)].push_back(edge.from);
    std::vector<char> ends(count, 0);
    std::vector<int> frontier;
    for (std::size_t state = 0; state < count; ++state) {
        if (!is_final(sys, ex.states[state])) continue;
        ends[state] = 1;
        frontier.push_back(static_cast<int>(state));
    }
    while (!frontier.empty()) {
        const int state = frontier.back();
        frontier.pop_back();
        for (const int previous : predecessors[static_cast<std::size_t>(state)]) {
            if (ends[static_cast<std::size_t>(previous)] != 0) continue;
            ends[static_cast<std::size_t>(previous)] = 1;
            frontier.push_back(previous);
        }
    }
    for (std::size_t state = 0; state < count; ++state) {
        if (ends[state] != 0) continue;
        const bool each_could_end = std::ranges::all_of(ex.states[state].at, [&](int node) {
            return node >= 0 && static_cast<std::size_t>(node) < could_end.size()
                && could_end[static_cast<std::size_t>(node)] != 0;
        });
        if (each_could_end) return true;
    }
    return false;
}

// ── Reporting ────────────────────────────────────────────────────────

int failures = 0;

[[nodiscard]] bool is_clean_at_small_capacities(const System& sys) {
    return analyse(sys, 1).is_clean() && analyse(sys, 2).is_clean();
}

void report_association_step() {
    const AssociationCounts& c = association_counts;
    std::printf("association step over %zu accepted types, %zu of them with all five rewrites\n", c.types,
                c.full_types);
    std::printf("  safe rewrites associated and live: %zu, refused for a lost exit: %zu, unfolded associated and live: "
                "%zu\n",
                c.safe_live, c.safe_lost_exit, c.unfolded_live);
    std::printf("  widened or narrowed rewrites refused: %zu, of which the explorer shows faulty: %zu\n", c.refused,
                c.refused_faulty);
    std::printf("  swapped rewrites associated and live: %zu\n", c.swapped_live);
    expect(c.safe_live + c.safe_lost_exit == c.types && c.unfolded_live == c.full_types,
           "a safe rewrite failed the association step");
    expect(c.safe_live > 0, "no safe rewrite kept its exits, so the association of a narrowed Select is untested");
    expect(c.refused > 0 && c.refused_faulty > 0, "no refused rewrite showed a fault, so the step proves nothing");
    expect(c.swapped_live > 0, "no swapped rewrite was built, so the match by label has no test");
}

// The safe rewrite changes each int that a role sends into a Checked
// value.  The session layer must admit a Checked value where a branch
// offers an int.
static_assert(s::is_payload_subsort_v<Checked, int>);
static_assert(
    s::is_payload_subsort_v<s::PeerMsg<NeverSent, NeverSent, Checked>, s::PeerMsg<NeverSent, NeverSent, int>>);
static_assert(
    !s::is_payload_subsort_v<s::PeerMsg<NeverSent, NeverSent, int>, s::PeerMsg<NeverSent, NeverSent, Checked>>);

}  // namespace

void fail(std::string_view what) {
    std::fprintf(stderr, "test_session_global_attack: %.*s\n", static_cast<int>(what.size()), what.data());
    std::abort();
}

void set_branches(System& sys, int node, std::initializer_list<BranchSpec> specs) {
    std::vector<Branch> branches;
    for (const BranchSpec& spec : specs) {
        branches.push_back(Branch{sys.role_of(spec.peer), sys.message_of(spec.label, spec.payload), spec.next});
    }
    sys.nodes[static_cast<std::size_t>(node)].branches = std::move(branches);
}

void set_alias(System& sys, int node, int target) { sys.nodes[static_cast<std::size_t>(node)].alias = target; }

void add_queued(System& sys, std::string_view from, std::string_view to, std::string_view label,
                std::string_view payload) {
    sys.initial_queues[sys.pair_of(sys.role_of(from), sys.role_of(to))].push_back(sys.message_of(label, payload));
}

Verdict analyse(const System& sys, std::size_t capacity) {
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
                    obligations.push_back(
                        Obligation{true, static_cast<int>(from), static_cast<int>(to), queue.front()});
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
               line
                   + (expect_clean ? ": expected live, the explorer found a fault" : ": expected a fault, found none"));
    }
}

System rewritten(const System& base, GraphRewrite kind) {
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

void judge_narrowed(std::string_view label, std::string_view rewrite, bool associated, const System& base,
                    const System& sys) {
    if (associated) {
        judge_accepted(label, rewrite, associated, sys, association_counts.safe_live);
        return;
    }
    std::string what{label};
    what += ", ";
    what += rewrite;
    const bool lost_exit = loses_an_exit(base, sys, 1);
    expect(lost_exit, what + ": association refused a safe subtype of the projection that keeps each exit");
    if (lost_exit) ++association_counts.safe_lost_exit;
}

void judge_refused(std::string_view label, std::string_view rewrite, bool associated, const System& sys,
                   std::size_t& refused, std::size_t& faulty) {
    std::string what{label};
    what += ", ";
    what += rewrite;
    expect(!associated, what + ": association accepted an entry that does not refine its projection");
    if (associated) return;
    ++refused;
    if (!is_clean_at_small_capacities(sys)) ++faulty;
}

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

void report_variant_not_live(std::string_view family, std::uint64_t seed) {
    std::string what{family};
    what += ": the first send of a live type is not live by construction, seed ";
    what += std::to_string(seed);
    expect(false, what);
}

}  // namespace test_session_global_attack

int main() {
    using namespace ::test_session_global_attack;
    std::signal(SIGALRM, on_watchdog);
    ::alarm(watchdog_seconds);
    payload_order.push_back(PayloadPair{name_of<Checked>(), name_of<int>()});
    run_controls();
    run_accepted();
    run_refusals();
    run_interleaving();
    run_generated();
    run_merge_grid();
    report_association_step();
    run_ledger();
    if (failures != 0) {
        std::fprintf(stderr, "test_session_global_attack: %d failure(s)\n", failures);
        return 1;
    }
    std::printf("test_session_global_attack: all attacks refused or pinned\n");
    return 0;
}
