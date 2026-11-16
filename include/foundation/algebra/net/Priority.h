#pragma once

// The deadlock check of a system of processes and channels: the marked graph
// of the system, with the capacity of each channel.  It admits a safe cycle
// of channels.
//
//   Action                    one send or one receive of a process on a
//                             channel
//   NetChannel                the name and the capacity of a channel
//   ProcessSystem<P, C, A>    the names of P processes, C channels, and A
//                             actions
//   check_liveness(system)    each cycle of waits holds a token, so no
//                             action of the system waits for ever
//
// THE MODEL
//   Each process is a sequence of actions, in the order of the actions of
//   that process in the array.  The sequence is the whole body of the
//   process, or one period of its loop.  An action sends into a channel or
//   receives from it.  A channel has one sender process, one receiver
//   process, and a capacity: the count of the messages that it holds.  A
//   receive waits for its message.  A send waits for a free slot.  A channel
//   of capacity 0 is a rendezvous: each send waits for its receive.
//
//   Each action is a node of a marked graph (Commoner, Holt, Even and Pnueli
//   1971).  An edge goes from an action to an action that waits on it:
//     - from an action to the next action of its process
//     - from send s of a channel to receive s of that channel
//     - from receive s - k to send s, for a channel of capacity k of 1 or more
//       and s of k or more: the send waits for the slot of that receive
//   Send s and receive s of a rendezvous are one node.  These edges hold no
//   token.  The edges that hold a token cross into a later period: the edge
//   from the last action of a loop to its first action, and the edge from a
//   receive of the period before to send s of a channel of capacity k, for s
//   below k.  A marked graph is live exactly when each directed cycle holds a
//   token.  So the system is live exactly when the edges with no token form
//   no cycle, and the check is one topological sort.  A system that runs one
//   time obeys the same rule: then a topological order is a run in which
//   each action starts.
//
//   The tree checks of foundation/algebra/net/Net.h refuse each cycle of
//   channels.  This check admits a safe cycle: two sessions in a fixed order,
//   a ring of queues, the loop between the Vigil and the pipeline, and a
//   process that waits on a, then b, then a again.  The run-time check of
//   fixy/session/Watch.h keeps one priority for each session, so it refuses
//   that last case.
//
// THE LIMITS
//   A branch and a choice have no form here: a caller checks one system for
//   each arm, or for each set of arms that the processes choose together.  A
//   lock needs an acquire and a release bracket, which the check does not
//   read.  A channel with two senders or two receivers is no marked graph,
//   because the order of its messages depends on the schedule, so the check
//   refuses it.  A session that sends in two directions is two channels, one
//   for each direction.  The check reads a park on a futex word as a wait on
//   the channel, which holds only while each push wakes its parked consumer.
//
// THE COSTS
//   O(P + C + A), with arrays of at most 2A edges.  The ring of 1,000
//   processes and 4,000 actions of test_algebra_net_scale.cpp checks in its
//   unit.

#include <foundation/algebra/net/Net.h>

#include <cstddef>
#include <cstdint>

namespace foundation::algebra::net {

enum class ProcessId : std::uint32_t {
};
enum class ChannelId : std::uint32_t {
};

// The two moves of an action on its channel.
enum class Move : std::uint8_t {
    Send = 0,
    Receive = 1,
};

// One step of a process: a send into a channel, or a receive from it.
struct Action {
    ProcessId process{};
    ChannelId channel{};
    Move move = Move::Receive;
    std::uint8_t reserved[3]{};
};

// A channel of a system: its name, and the count of the messages that it
// holds.  A capacity of 0 is a rendezvous.
struct NetChannel {
    NetName name{};
    std::uint32_t capacity = 0;
    std::uint32_t reserved = 0;
};

// A system: the names of the processes, the channels, and the actions.
template <std::size_t Processes, std::size_t Channels, std::size_t Actions>
    requires(Processes >= 1 && Channels >= 1 && Actions >= 1 && Processes < 0x10000000u && Channels < 0x10000000u
             && Actions < 0x10000000u)
struct ProcessSystem {
    NetName process_names[Processes]{};
    NetChannel channels[Channels]{};
    Action actions[Actions]{};
};

namespace detail {

// The most nodes of a cycle that a refusal of check_liveness names.
inline constexpr std::uint32_t max_named_waits = 4;

// The index of an action among the sends, or among the receives, of its
// channel.
enum class ChannelStep : std::uint32_t {
};

// "P sends 'c' step s", "Q receives 'c' step s", or for a rendezvous "'c'
// step s from P to Q".
template <std::size_t Processes, std::size_t Channels, std::size_t Actions>
constexpr void put_wait_(NetVerdict& text, ProcessSystem<Processes, Channels, Actions> const& protocol,
                         Action const& act, ChannelStep step, ProcessId receiver) noexcept {
    NetChannel const& channel = protocol.channels[static_cast<std::uint32_t>(act.channel)];
    NetName const actor = protocol.process_names[static_cast<std::uint32_t>(act.process)];
    if (channel.capacity == 0u) {
        text.put("'")
            .put_name(channel.name)
            .put("' step ")
            .put_number(static_cast<std::uint32_t>(step))
            .put(" from ")
            .put_name(actor)
            .put(" to ")
            .put_name(protocol.process_names[static_cast<std::uint32_t>(receiver)]);
        return;
    }
    text.put_name(actor)
        .put(act.move == Move::Send ? " sends '" : " receives '")
        .put_name(channel.name)
        .put("' step ")
        .put_number(static_cast<std::uint32_t>(step));
}

}  // namespace detail

// Each cycle of waits of the marked graph of the system holds a token
// (THE MODEL).  The refusal of a cycle names its first nodes, each after the
// node that waits on it.  Complexity: O(P + C + A).
template <std::size_t Processes, std::size_t Channels, std::size_t Actions>
[[nodiscard]] consteval NetVerdict
check_liveness(ProcessSystem<Processes, Channels, Actions> const& protocol) noexcept {
    constexpr std::uint32_t none = detail::no_index;
    for (std::uint32_t process = 0; process < Processes; ++process) {
        if (protocol.process_names[process].is_empty()) {
            return NetVerdict::refuse("liveness: process ").put_number(process).put(" has no name.");
        }
    }
    for (std::uint32_t channel = 0; channel < Channels; ++channel) {
        if (protocol.channels[channel].name.is_empty()) {
            return NetVerdict::refuse("liveness: channel ").put_number(channel).put(" has no name.");
        }
    }

    // The step of each action on its side of its channel, the count of the
    // sends and of the receives of each channel, and the process that holds
    // each side.  Side 0 of channel c is the slot 2c, and side 1 is 2c + 1.
    std::uint32_t step_of[Actions]{};
    std::uint32_t step_count[2 * Channels]{};
    std::uint32_t holder[2 * Channels]{};
    for (std::uint32_t& slot : holder)
        slot = none;
    for (std::uint32_t index = 0; index < Actions; ++index) {
        Action const& act = protocol.actions[index];
        auto const process = static_cast<std::uint32_t>(act.process);
        auto const channel = static_cast<std::uint32_t>(act.channel);
        if (process >= Processes || channel >= Channels || (act.move != Move::Send && act.move != Move::Receive)) {
            return NetVerdict::refuse("liveness: action ")
                .put_number(index)
                .put(" names a process, a channel or a move that the system does not have.");
        }
        std::uint32_t const slot = 2 * channel + (act.move == Move::Send ? 0u : 1u);
        if (holder[slot] == none) holder[slot] = process;
        if (holder[slot] != process) {
            return NetVerdict::refuse("liveness: the ")
                .put(act.move == Move::Send ? "send" : "receive")
                .put(" side of channel '")
                .put_name(protocol.channels[channel].name)
                .put("' is held by '")
                .put_name(protocol.process_names[holder[slot]])
                .put("' and by '")
                .put_name(protocol.process_names[process])
                .put("'.  One process holds each side of a channel.");
        }
        step_of[index] = step_count[slot];
        ++step_count[slot];
    }

    // The action of each step: the steps of channel c are at step_first[c].
    std::uint32_t step_first[Channels]{};
    std::uint32_t steps = 0;
    for (std::uint32_t channel = 0; channel < Channels; ++channel) {
        if (step_count[2 * channel] != step_count[2 * channel + 1]) {
            return NetVerdict::refuse("liveness: channel '")
                .put_name(protocol.channels[channel].name)
                .put("' has ")
                .put_number(step_count[2 * channel])
                .put(" sends and ")
                .put_number(step_count[2 * channel + 1])
                .put(" receives.  Each send has its receive.");
        }
        step_first[channel] = steps;
        steps += step_count[2 * channel];
    }
    std::uint32_t send_at[Actions]{};
    std::uint32_t receive_at[Actions]{};
    for (std::uint32_t index = 0; index < Actions; ++index) {
        Action const& act = protocol.actions[index];
        std::uint32_t const step = step_first[static_cast<std::uint32_t>(act.channel)] + step_of[index];
        if (act.move == Move::Send) {
            send_at[step] = index;
        } else {
            receive_at[step] = index;
        }
    }

    // The node of each action.  The receive of a rendezvous joins the node of
    // its send.
    std::uint32_t node_of[Actions]{};
    for (std::uint32_t index = 0; index < Actions; ++index) {
        Action const& act = protocol.actions[index];
        bool const is_joined =
            act.move == Move::Receive && protocol.channels[static_cast<std::uint32_t>(act.channel)].capacity == 0u;
        node_of[index] =
            is_joined ? send_at[step_first[static_cast<std::uint32_t>(act.channel)] + step_of[index]] : index;
    }

    // The edges with no token: at most one from the order of the process and
    // one from the channel for each action.  An edge of the order of a
    // process can join a node to itself, which is a cycle.
    std::uint32_t edge_from[2 * Actions]{};
    std::uint32_t edge_to[2 * Actions]{};
    std::uint32_t edge_count = 0;
    std::uint32_t last_of_process[Processes]{};
    for (std::uint32_t& slot : last_of_process)
        slot = none;
    for (std::uint32_t index = 0; index < Actions; ++index) {
        Action const& act = protocol.actions[index];
        auto const process = static_cast<std::uint32_t>(act.process);
        auto const channel = static_cast<std::uint32_t>(act.channel);
        std::uint32_t const capacity = protocol.channels[channel].capacity;
        std::uint32_t const step = step_of[index];
        if (last_of_process[process] != none) {
            edge_from[edge_count] = node_of[last_of_process[process]];
            edge_to[edge_count] = node_of[index];
            ++edge_count;
        }
        last_of_process[process] = index;
        if (act.move == Move::Receive && capacity != 0u) {
            edge_from[edge_count] = send_at[step_first[channel] + step];
            edge_to[edge_count] = index;
            ++edge_count;
        }
        if (act.move == Move::Send && capacity != 0u && step >= capacity) {
            edge_from[edge_count] = receive_at[step_first[channel] + step - capacity];
            edge_to[edge_count] = index;
            ++edge_count;
        }
    }

    // The sort of Kahn over the nodes, with a compressed list of the edges.
    std::uint32_t indegree[Actions]{};
    std::uint32_t out_first[Actions + 1]{};
    for (std::uint32_t edge = 0; edge < edge_count; ++edge) {
        ++indegree[edge_to[edge]];
        ++out_first[edge_from[edge] + 1];
    }
    for (std::uint32_t node = 0; node < Actions; ++node)
        out_first[node + 1] += out_first[node];
    std::uint32_t fill[Actions]{};
    for (std::uint32_t node = 0; node < Actions; ++node)
        fill[node] = out_first[node];
    std::uint32_t out_edge[2 * Actions]{};
    for (std::uint32_t edge = 0; edge < edge_count; ++edge) {
        out_edge[fill[edge_from[edge]]] = edge;
        ++fill[edge_from[edge]];
    }
    std::uint32_t order[Actions]{};
    std::uint32_t order_count = 0;
    std::uint32_t node_count = 0;
    for (std::uint32_t index = 0; index < Actions; ++index) {
        if (node_of[index] != index) continue;
        ++node_count;
        if (indegree[index] == 0u) {
            order[order_count] = index;
            ++order_count;
        }
    }
    for (std::uint32_t read = 0; read < order_count; ++read) {
        std::uint32_t const node = order[read];
        for (std::uint32_t slot = out_first[node]; slot < out_first[node + 1]; ++slot) {
            std::uint32_t const target = edge_to[out_edge[slot]];
            --indegree[target];
            if (indegree[target] == 0u) {
                order[order_count] = target;
                ++order_count;
            }
        }
    }
    if (order_count == node_count) return NetVerdict{};

    // A cycle remains.  Each node that the sort left waits on another such
    // node, so a walk back along these waits repeats a node.
    std::uint32_t into[Actions]{};
    for (std::uint32_t& slot : into)
        slot = none;
    for (std::uint32_t edge = 0; edge < edge_count; ++edge) {
        if (indegree[edge_from[edge]] != 0u && indegree[edge_to[edge]] != 0u) into[edge_to[edge]] = edge;
    }
    std::uint32_t walk = none;
    for (std::uint32_t index = 0; index < Actions; ++index) {
        if (node_of[index] == index && indegree[index] != 0u) {
            walk = index;
            break;
        }
    }
    bool is_seen[Actions]{};
    while (!is_seen[walk]) {
        is_seen[walk] = true;
        walk = edge_from[into[walk]];
    }
    NetVerdict refusal = NetVerdict::refuse("liveness: a cycle of waits holds no token: ");
    std::uint32_t member = walk;
    std::uint32_t shown = 0;
    do {
        if (shown != 0u) refusal.put(" <- ");
        Action const& act = protocol.actions[member];
        detail::put_wait_(refusal, protocol, act, detail::ChannelStep{step_of[member]},
                          ProcessId{holder[2 * static_cast<std::uint32_t>(act.channel) + 1]});
        member = edge_from[into[member]];
        ++shown;
    } while (member != walk && shown < detail::max_named_waits);
    if (member != walk) refusal.put(" <- ...");
    refusal.put(".  No action of the cycle can start.");
    return refusal;
}

}  // namespace foundation::algebra::net
