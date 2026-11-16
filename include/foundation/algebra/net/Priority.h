#pragma once

// The priority check of a system of processes and channels: the deadlock
// check that admits a safe cycle.
//
//   Action                     one step of a process on one side of a channel
//   ProcessSystem<P, C, A>     the names of P processes and C channels, and
//                              A actions
//   check_priorities(system)   a priority for each step exists, so the system
//                              does not deadlock
//
// THE MODEL
//   Each process is a sequence of actions, in the order of the actions of
//   that process in the array.  An action names a channel, the side of the
//   channel that the process holds, whether it sends, and whether it waits.
//   A receive waits, and a send waits when the channel has no room, as a
//   synchronous send does.  The k-th action of the first side of a channel
//   and the k-th action of its second side are one event: one of them sends
//   and the other one receives, and the two get one priority.
//
//   The constraint: an action that waits has a priority below each later
//   action of its process.  A priority order exists exactly when the graph of
//   these constraints has no cycle, so the check is a topological sort.  This
//   is the simplification of Padovani 2014 that the PCP of Dardha and Gay 2018
//   uses: one priority for each action, and not the two levels of Kobayashi
//   2006.  The soundness is rule A of Kobayashi 2006: in a cycle of waits, each
//   priority must be below the one before it, which no order gives.
//
//   The tree checks of foundation/algebra/net/Net.h refuse each cycle of
//   channels.  This check admits a safe cycle: two sessions in a fixed order,
//   a ring of queues, the loop between the Vigil and the pipeline, and a
//   process that waits on a, then b, then a again (probe E4).  The run-time
//   check of fixy/session/Watch.h keeps one priority for each session, so it
//   refuses that last case.
//
// THE LIMITS
//   A branch, a choice and a loop need more rules.  A loop body is checked as
//   one unrolling.  The wait flag of a send comes from the caller, and a real
//   check takes it from the capacity of the channel.  A lock needs an acquire
//   and a release bracket.
//
// THE COSTS
//   O(P + C + A).  Probe E4 measured 0.88 G compile instructions for a ring of
//   1,000 processes and 4,000 actions.

#include <foundation/algebra/net/Net.h>

#include <cstddef>
#include <cstdint>

namespace foundation::algebra::net {

enum class ProcessId : std::uint32_t {
};
enum class ChannelId : std::uint32_t {
};

// The two sides of a binary channel.
enum class ChannelSide : std::uint8_t {
    First = 0,
    Second = 1,
};

// One step of a process on one side of a channel.
struct Action {
    ProcessId process{};
    ChannelId channel{};
    ChannelSide side = ChannelSide::First;
    bool does_send = false;
    bool does_wait = true;
    std::uint8_t reserved = 0;
};

// A system: the names of the processes and the channels, and the actions.
template <std::size_t Processes, std::size_t Channels, std::size_t Actions>
    requires(Processes >= 1 && Channels >= 1 && Actions >= 1 && Processes < 0x10000000u && Channels < 0x10000000u
             && Actions < 0x10000000u)
struct ProcessSystem {
    NetName process_names[Processes]{};
    NetName channel_names[Channels]{};
    Action actions[Actions]{};
};

// A priority for each event exists, with each waiting action below each
// later action of its process.  The refusal of a cycle names each process,
// each channel and each step on it.  Complexity: O(P + C + A).
template <std::size_t Processes, std::size_t Channels, std::size_t Actions>
[[nodiscard]] consteval NetVerdict
check_priorities(ProcessSystem<Processes, Channels, Actions> const& protocol) noexcept {
    constexpr std::uint32_t none = detail::no_index;
    std::uint32_t last_wait[Processes]{};
    for (std::uint32_t process = 0; process < Processes; ++process) {
        if (protocol.process_names[process].is_empty()) {
            return NetVerdict::refuse("priorities: process ").put_number(process).put(" has no name.");
        }
        last_wait[process] = none;
    }
    // The count of the steps of each side of each channel, and the process
    // that holds each side.  Side s of channel c is the slot 2c + s.
    std::uint32_t step_count[2 * Channels]{};
    std::uint32_t holder[2 * Channels]{};
    for (std::uint32_t channel = 0; channel < Channels; ++channel) {
        if (protocol.channel_names[channel].is_empty()) {
            return NetVerdict::refuse("priorities: channel ").put_number(channel).put(" has no name.");
        }
        holder[2 * channel] = none;
        holder[2 * channel + 1] = none;
    }
    std::uint32_t step_of[Actions]{};
    for (std::uint32_t index = 0; index < Actions; ++index) {
        Action const& act = protocol.actions[index];
        auto const process = static_cast<std::uint32_t>(act.process);
        auto const channel = static_cast<std::uint32_t>(act.channel);
        auto const side = static_cast<std::uint32_t>(act.side);
        if (process >= Processes || channel >= Channels || side > 1u) {
            return NetVerdict::refuse("priorities: action ")
                .put_number(index)
                .put(" names a process, a channel or a side that the system does not have.");
        }
        std::uint32_t const slot = 2 * channel + side;
        if (holder[slot] == none) holder[slot] = process;
        if (holder[slot] != process) {
            return NetVerdict::refuse("priorities: the ")
                .put(side == 0u ? "first" : "second")
                .put(" side of channel '")
                .put_name(protocol.channel_names[channel])
                .put("' is held by '")
                .put_name(protocol.process_names[holder[slot]])
                .put("' and by '")
                .put_name(protocol.process_names[process])
                .put("'.  One process holds each side of a channel.");
        }
        step_of[index] = step_count[slot];
        ++step_count[slot];
    }
    std::uint32_t event_first[Channels]{};
    std::uint32_t events = 0;
    for (std::uint32_t channel = 0; channel < Channels; ++channel) {
        if (step_count[2 * channel] != step_count[2 * channel + 1]) {
            return NetVerdict::refuse("priorities: channel '")
                .put_name(protocol.channel_names[channel])
                .put("' has ")
                .put_number(step_count[2 * channel])
                .put(" actions on its first side and ")
                .put_number(step_count[2 * channel + 1])
                .put(" on its second side.  The two sides are not dual.");
        }
        event_first[channel] = events;
        events += step_count[2 * channel];
    }
    // In one pass in the order of the array: the event of each action, the
    // check that the two actions of an event go in opposite directions, and
    // an edge from the last waiting action of the process to the action.  The
    // edges go from that last wait to each later action, so the graph stays
    // linear and the order transitive.
    std::uint32_t event_of[Actions]{};
    std::uint8_t first_side_direction[Actions]{};
    std::uint32_t edge_from[Actions]{};
    std::uint32_t edge_to[Actions]{};
    std::uint32_t edge_process[Actions]{};
    std::uint32_t edge_count = 0;
    for (std::uint32_t index = 0; index < Actions; ++index) {
        Action const& act = protocol.actions[index];
        auto const process = static_cast<std::uint32_t>(act.process);
        auto const channel = static_cast<std::uint32_t>(act.channel);
        std::uint32_t const event = event_first[channel] + step_of[index];
        event_of[index] = event;
        bool const sends_from_first = act.side == ChannelSide::First ? act.does_send : !act.does_send;
        std::uint8_t const direction = sends_from_first ? 2 : 1;
        if (first_side_direction[event] != 0u && first_side_direction[event] != direction) {
            return NetVerdict::refuse("priorities: step ")
                .put_number(step_of[index])
                .put(" of channel '")
                .put_name(protocol.channel_names[channel])
                .put("' sends on both sides or receives on both sides.");
        }
        first_side_direction[event] = direction;
        std::uint32_t const waited = last_wait[process];
        if (waited != none && event_of[waited] != event) {
            edge_from[edge_count] = event_of[waited];
            edge_to[edge_count] = event;
            edge_process[edge_count] = process;
            ++edge_count;
        }
        if (act.does_wait) last_wait[process] = index;
    }
    // The sort of Kahn over the events, with a compressed list of the edges.
    std::uint32_t indegree[Actions]{};
    std::uint32_t out_first[Actions + 1]{};
    for (std::uint32_t edge = 0; edge < edge_count; ++edge) {
        ++indegree[edge_to[edge]];
        ++out_first[edge_from[edge] + 1];
    }
    for (std::uint32_t event = 0; event < events; ++event)
        out_first[event + 1] += out_first[event];
    std::uint32_t fill[Actions + 1]{};
    for (std::uint32_t event = 0; event <= events; ++event)
        fill[event] = out_first[event];
    std::uint32_t out_edge[Actions]{};
    for (std::uint32_t edge = 0; edge < edge_count; ++edge) {
        out_edge[fill[edge_from[edge]]] = edge;
        ++fill[edge_from[edge]];
    }
    std::uint32_t order[Actions]{};
    std::uint32_t order_count = 0;
    for (std::uint32_t event = 0; event < events; ++event) {
        if (indegree[event] == 0u) {
            order[order_count] = event;
            ++order_count;
        }
    }
    for (std::uint32_t read = 0; read < order_count; ++read) {
        std::uint32_t const event = order[read];
        for (std::uint32_t slot = out_first[event]; slot < out_first[event + 1]; ++slot) {
            std::uint32_t const target = edge_to[out_edge[slot]];
            --indegree[target];
            if (indegree[target] == 0u) {
                order[order_count] = target;
                ++order_count;
            }
        }
    }
    if (order_count == events) return NetVerdict{};

    // A cycle remains.  Walk back along the edges between events that the
    // sort left, until an event repeats, and name each wait on the cycle.
    std::uint32_t channel_of_event[Actions]{};
    std::uint32_t step_of_event[Actions]{};
    for (std::uint32_t index = 0; index < Actions; ++index) {
        channel_of_event[event_of[index]] = static_cast<std::uint32_t>(protocol.actions[index].channel);
        step_of_event[event_of[index]] = step_of[index];
    }
    std::uint32_t into[Actions]{};
    for (std::uint32_t& slot : into)
        slot = none;
    for (std::uint32_t edge = 0; edge < edge_count; ++edge) {
        if (indegree[edge_to[edge]] != 0u && indegree[edge_from[edge]] != 0u) into[edge_to[edge]] = edge;
    }
    std::uint32_t walk = none;
    for (std::uint32_t event = 0; event < events; ++event) {
        if (indegree[event] != 0u) {
            walk = event;
            break;
        }
    }
    bool is_seen[Actions]{};
    while (!is_seen[walk]) {
        is_seen[walk] = true;
        walk = edge_from[into[walk]];
    }
    NetVerdict refusal = NetVerdict::refuse("priorities: a cycle of waits: ");
    std::uint32_t member = walk;
    std::uint32_t shown = 0;
    do {
        std::uint32_t const edge = into[member];
        if (shown != 0u) refusal.put(", ");
        refusal.put_name(protocol.process_names[edge_process[edge]])
            .put(" waits on '")
            .put_name(protocol.channel_names[channel_of_event[edge_from[edge]]])
            .put("' step ")
            .put_number(step_of_event[edge_from[edge]])
            .put(" before '")
            .put_name(protocol.channel_names[channel_of_event[member]])
            .put("' step ")
            .put_number(step_of_event[member]);
        member = edge_from[edge];
        ++shown;
    } while (member != walk && shown < 4u);
    if (member != walk) refusal.put(", ...");
    refusal.put(".  No priority order exists.");
    return refusal;
}

}  // namespace foundation::algebra::net
