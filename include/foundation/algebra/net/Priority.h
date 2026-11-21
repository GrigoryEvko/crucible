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
//   LivenessFault             the fault that the check finds: a code, and the
//                             process, the channel, the action or the cycle
//                             that the code names
//   find_liveness_fault(system)
//                             the body of the check: the first fault, or the
//                             fault NoFault
//   SealedSystem<P, C, A>     a copy of a system that no caller can change,
//                             and the fault of its form, found one time
//   seal_system(system)       the sealed system of a system: its form checked
//                             one time.  find_liveness_fault on a sealed
//                             system runs no check of the form again
//   explain_liveness_fault(system, fault)
//                             the text of the refusal of a fault
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
// THE FAULT
//   find_liveness_fault is constexpr and makes no text, so one body serves
//   the consteval door check_liveness and a program at run time, as the find
//   functions of Net.h do.  explain_liveness_fault makes the text from the
//   fault and the system.  A run-time call keeps the arrays of the check on
//   the stack: about 32 bytes for each action, 20 for each channel and 4 for
//   each process.  The walk of a cycle adds about 9 bytes for each action in
//   a frame of its own.  A program calls it on a system that it holds as a
//   parameter or as a variable, never on a constant, because the compiler
//   folds a call on a constant into a second run of the check (THE RUN-TIME
//   FORM of foundation/algebra/net/Net.h).
//
// THE SEALED SYSTEM
//   The check first reads the form of the system: a name for each process
//   and each channel, each action inside the system, and one process for
//   each side of each channel.  A program that checks one system more than
//   one time does that work one time, at the boundary where the system comes
//   in: seal_system copies the system into a SealedSystem and finds the fault
//   of the form of the copy.  No caller can change the copy, so the fault
//   stays the fault of the system.  find_liveness_fault on a sealed system
//   gives that fault, or it runs the body of the check with no check of the
//   form, and it gives the fault of find_liveness_fault on the same system.
//   The door is seal_system and not a door mint_*, for the reason of seal_net
//   (THE SEALED NET of foundation/algebra/net/Net.h).
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
//   O(P + C + A).  The edges of a node come from the arrays of the steps, so
//   the check keeps no list of the edges.  The ring of 1,000 processes and
//   4,000 actions of test_algebra_net_scale.cpp checks in its unit.

#include <foundation/algebra/net/Net.h>

#include <cstddef>
#include <cstdint>

namespace foundation::algebra::net {

enum class ProcessId : std::uint32_t {
};
enum class ChannelId : std::uint32_t {
};

// The index of an action in the actions of a system.
enum class ActionId : std::uint32_t {
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

// ── The fault ───────────────────────────────────────────────────────────

// The rule that a system breaks.  Each code has one text, which
// explain_liveness_fault makes.
enum class LivenessFaultCode : std::uint8_t {
    NoFault = 0,
    // The fault names the process.
    ProcessWithoutName = 1,
    // The fault names the channel.
    ChannelWithoutName = 2,
    // The fault names the action.
    ActionPastSystem = 3,
    // The fault names the action of the second process, and the process
    // that held that side of the channel first.
    SideHeldTwice = 4,
    // The fault names the channel.
    SendsWithoutReceives = 5,
    // The fault names the first actions of the cycle in path, each after the
    // action that waits on it.
    CycleWithoutToken = 6,
};

// The most actions of a cycle that a fault names.  A longer cycle sets
// is_path_cut.
inline constexpr std::size_t max_liveness_path = 4;

// The fault that check_liveness finds, with no text.  The code tells which
// fields it sets.
struct LivenessFault {
    LivenessFaultCode code = LivenessFaultCode::NoFault;
    std::uint8_t path_count = 0;
    bool is_path_cut = false;
    std::uint8_t reserved = 0;
    ProcessId process{};
    ChannelId channel{};
    ActionId action{};
    ActionId path[max_liveness_path]{};

    [[nodiscard]] constexpr bool is_fault() const noexcept { return code != LivenessFaultCode::NoFault; }
};

namespace detail {

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

// The marked graph of a system whose processes, channels and actions passed
// the checks of their form (THE MODEL), as arrays over its actions.
template <std::size_t Channels, std::size_t Actions>
struct MarkedGraph {
    // The step of each action among the sends, or among the receives, of its
    // channel.
    std::uint32_t step_of[Actions]{};
    // The first slot of the steps of each channel in send_at and receive_at.
    std::uint32_t step_first[Channels]{};
    // The send and the receive of each step of each channel.
    std::uint32_t send_at[Actions]{};
    std::uint32_t receive_at[Actions]{};
    // The node of each action: the action, or the send of the receive of a
    // rendezvous.
    std::uint32_t node_of[Actions]{};
    // The count of the edges with no token into each node.  After the sort, a
    // node that the sort ordered holds 0, and each other node holds the count
    // of its edges from the nodes that the sort left.
    std::uint32_t indegree[Actions]{};
};

// The fault of a cycle that the sort left (THE FAULT).  Each node that the
// sort left waits on another such node, so a walk back along these waits
// repeats a node.  Each node keeps the last of its edges from a node that the
// sort left, in the order of the actions, with the edge of the process before
// the edge of the channel.  The walk starts at the first node that the sort
// left.  Complexity: O(P + A).
template <std::size_t Processes, std::size_t Channels, std::size_t Actions>
[[nodiscard, gnu::cold, gnu::noinline]] constexpr LivenessFault
find_liveness_cycle_(ProcessSystem<Processes, Channels, Actions> const& protocol,
                     MarkedGraph<Channels, Actions> const& graph) noexcept {
    constexpr std::uint32_t none = no_index;
    std::uint32_t wait_of[Actions]{};
    for (std::uint32_t& slot : wait_of)
        slot = none;
    // An edge with no token: from the node that goes first to the node that
    // waits on it.
    struct WaitEdge {
        std::uint32_t from = 0;
        std::uint32_t to = 0;
    };
    // Keeps an edge between two nodes that the sort left.
    auto const keep = [&](WaitEdge edge) constexpr noexcept {
        if (graph.indegree[edge.from] != 0u && graph.indegree[edge.to] != 0u) wait_of[edge.to] = edge.from;
    };
    std::uint32_t last_of_process[Processes]{};
    for (std::uint32_t& slot : last_of_process)
        slot = none;
    for (std::uint32_t index = 0; index < Actions; ++index) {
        Action const& act = protocol.actions[index];
        auto const process = static_cast<std::uint32_t>(act.process);
        auto const channel = static_cast<std::uint32_t>(act.channel);
        std::uint32_t const capacity = protocol.channels[channel].capacity;
        std::uint32_t const step = graph.step_of[index];
        std::uint32_t const first = graph.step_first[channel];
        if (last_of_process[process] != none) {
            keep({.from = graph.node_of[last_of_process[process]], .to = graph.node_of[index]});
        }
        last_of_process[process] = index;
        if (act.move == Move::Receive && capacity != 0u) keep({.from = graph.send_at[first + step], .to = index});
        if (act.move == Move::Send && capacity != 0u && step >= capacity) {
            keep({.from = graph.receive_at[first + step - capacity], .to = index});
        }
    }
    std::uint32_t walk = none;
    for (std::uint32_t index = 0; index < Actions; ++index) {
        if (graph.node_of[index] == index && graph.indegree[index] != 0u) {
            walk = index;
            break;
        }
    }
    bool is_seen[Actions]{};
    while (!is_seen[walk]) {
        is_seen[walk] = true;
        walk = wait_of[walk];
    }
    LivenessFault fault{.code = LivenessFaultCode::CycleWithoutToken};
    std::uint32_t member = walk;
    do {
        fault.path[fault.path_count] = ActionId{member};
        ++fault.path_count;
        member = wait_of[member];
    } while (member != walk && fault.path_count < max_liveness_path);
    fault.is_path_cut = member != walk;
    return fault;
}

// Whether the body of the liveness check runs the checks of the form of the
// system, or reads a system whose seal holds the fault of its form (THE
// SEALED SYSTEM).
enum class FormCheck : std::uint8_t {
    Run = 0,
    Sealed = 1,
};

// The first process and then the first channel with no name, or NoFault.
// Complexity: O(P + C).
template <std::size_t Processes, std::size_t Channels, std::size_t Actions>
[[nodiscard]] constexpr LivenessFault
find_name_fault_(ProcessSystem<Processes, Channels, Actions> const& protocol) noexcept {
    for (std::uint32_t process = 0; process < Processes; ++process) {
        if (protocol.process_names[process].is_empty()) {
            return LivenessFault{.code = LivenessFaultCode::ProcessWithoutName, .process = ProcessId{process}};
        }
    }
    for (std::uint32_t channel = 0; channel < Channels; ++channel) {
        if (protocol.channels[channel].name.is_empty()) {
            return LivenessFault{.code = LivenessFaultCode::ChannelWithoutName, .channel = ChannelId{channel}};
        }
    }
    return LivenessFault{};
}

// The process that holds each side of each channel, while the checks of the
// form read the actions.  Side 0 of channel c is the slot 2c, and side 1 is
// 2c + 1.  A slot holds the index of its process, and no_index for a side
// that no action holds yet: the check fills the array with no_index first.
// The sealed form reads no side, so it holds no slot.
template <std::size_t Channels, FormCheck Form>
struct side_holders {
    std::uint32_t process_of[2 * Channels]{};
};
template <std::size_t Channels>
struct side_holders<Channels, FormCheck::Sealed> {};

// The fault of the form of a system (THE SEALED SYSTEM): the first process
// and then the first channel with no name, and then, in the order of the
// actions, the first action past the system or on a side that two processes
// hold.  These are the faults that find_liveness_fault finds before it counts
// the steps, in the same order.  The loop of the plain form holds the same
// checks of an action inline (find_liveness_fault_), and the check file of
// this header pins that the two forms give one fault for each code.
// Complexity: O(P + C + A).
template <std::size_t Processes, std::size_t Channels, std::size_t Actions>
[[nodiscard]] constexpr LivenessFault
find_form_fault_(ProcessSystem<Processes, Channels, Actions> const& protocol) noexcept {
    LivenessFault const named = find_name_fault_(protocol);
    if (named.is_fault()) return named;
    side_holders<Channels, FormCheck::Run> holders{};
    for (std::uint32_t& slot : holders.process_of)
        slot = no_index;
    for (std::uint32_t index = 0; index < Actions; ++index) {
        Action const& act = protocol.actions[index];
        auto const process = static_cast<std::uint32_t>(act.process);
        auto const channel = static_cast<std::uint32_t>(act.channel);
        if (process >= Processes || channel >= Channels || (act.move != Move::Send && act.move != Move::Receive)) {
            return LivenessFault{.code = LivenessFaultCode::ActionPastSystem, .action = ActionId{index}};
        }
        std::uint32_t const slot = 2 * channel + (act.move == Move::Send ? 0u : 1u);
        if (holders.process_of[slot] == no_index) holders.process_of[slot] = process;
        if (holders.process_of[slot] != process) {
            return LivenessFault{.code = LivenessFaultCode::SideHeldTwice,
                                 .process = ProcessId{holders.process_of[slot]},
                                 .channel = act.channel,
                                 .action = ActionId{index}};
        }
    }
    return LivenessFault{};
}

// The body of the liveness check (THE MODEL), with the checks of the form, or
// on a system whose seal found no fault of its form.  Complexity: O(P + C +
// A).
template <FormCheck Form, std::size_t Processes, std::size_t Channels, std::size_t Actions>
[[nodiscard]] constexpr LivenessFault
find_liveness_fault_(ProcessSystem<Processes, Channels, Actions> const& protocol) noexcept {
    constexpr std::uint32_t none = no_index;
    if constexpr (Form == FormCheck::Run) {
        LivenessFault const named = find_name_fault_(protocol);
        if (named.is_fault()) return named;
    }

    // The step of each action on its side of its channel, and the count of
    // the sends and of the receives of each channel.  Side 0 of channel c is
    // the slot 2c, and side 1 is 2c + 1.  The plain form checks the form of
    // each action in the same loop, with the checks of find_form_fault_
    // inline: a code that a helper gives back costs one more hardened compare
    // for each action under -fharden-compares, about 6% of the check.  The
    // loop reads the move for the slot only after the checks of the form.  A
    // read before them lets GCC make the check of the move a value, and
    // -fharden-compares then hardens that value: 5 more instructions for each
    // action.
    MarkedGraph<Channels, Actions> graph{};
    std::uint32_t step_count[2 * Channels]{};
    [[maybe_unused]] side_holders<Channels, Form> holders{};
    if constexpr (Form == FormCheck::Run) {
        for (std::uint32_t& slot : holders.process_of)
            slot = none;
    }
    for (std::uint32_t index = 0; index < Actions; ++index) {
        Action const& act = protocol.actions[index];
        auto const channel = static_cast<std::uint32_t>(act.channel);
        [[maybe_unused]] auto const process = static_cast<std::uint32_t>(act.process);
        if constexpr (Form == FormCheck::Run) {
            if (process >= Processes || channel >= Channels || (act.move != Move::Send && act.move != Move::Receive)) {
                return LivenessFault{.code = LivenessFaultCode::ActionPastSystem, .action = ActionId{index}};
            }
        }
        std::uint32_t const slot = 2 * channel + (act.move == Move::Send ? 0u : 1u);
        if constexpr (Form == FormCheck::Run) {
            if (holders.process_of[slot] == none) holders.process_of[slot] = process;
            if (holders.process_of[slot] != process) {
                return LivenessFault{.code = LivenessFaultCode::SideHeldTwice,
                                     .process = ProcessId{holders.process_of[slot]},
                                     .channel = act.channel,
                                     .action = ActionId{index}};
            }
        }
        graph.step_of[index] = step_count[slot];
        ++step_count[slot];
    }

    // The action of each step: the steps of channel c are at step_first[c].
    std::uint32_t steps = 0;
    for (std::uint32_t channel = 0; channel < Channels; ++channel) {
        if (step_count[2 * channel] != step_count[2 * channel + 1]) {
            return LivenessFault{.code = LivenessFaultCode::SendsWithoutReceives, .channel = ChannelId{channel}};
        }
        graph.step_first[channel] = steps;
        steps += step_count[2 * channel];
    }
    for (std::uint32_t index = 0; index < Actions; ++index) {
        Action const& act = protocol.actions[index];
        std::uint32_t const step = graph.step_first[static_cast<std::uint32_t>(act.channel)] + graph.step_of[index];
        if (act.move == Move::Send) {
            graph.send_at[step] = index;
        } else {
            graph.receive_at[step] = index;
        }
    }

    // The node of each action, and the count of the edges with no token into
    // each node.  The receive of a rendezvous joins the node of its send.  An
    // edge of the order of a process can join a node to itself, which is a
    // cycle.  process_target holds the node of the next action of the
    // process of each action.  channel_target holds, for an action on a
    // channel of capacity k, the action that waits on it: receive s for send
    // s, and send s + k for receive s.  For the send of a rendezvous, it holds
    // the receive of its node, whose process goes on after the node.
    std::uint32_t process_target[Actions]{};
    std::uint32_t channel_target[Actions]{};
    std::uint32_t last_of_process[Processes]{};
    for (std::uint32_t& slot : last_of_process)
        slot = none;
    std::uint32_t node_count = 0;
    for (std::uint32_t index = 0; index < Actions; ++index) {
        Action const& act = protocol.actions[index];
        auto const process = static_cast<std::uint32_t>(act.process);
        auto const channel = static_cast<std::uint32_t>(act.channel);
        std::uint32_t const capacity = protocol.channels[channel].capacity;
        std::uint32_t const step = graph.step_of[index];
        std::uint32_t const slot = graph.step_first[channel] + step;
        bool const is_send = act.move == Move::Send;
        bool const is_joined = !is_send && capacity == 0u;
        std::uint32_t const node = is_joined ? graph.send_at[slot] : index;
        graph.node_of[index] = node;
        if (!is_joined) ++node_count;
        process_target[index] = none;
        if (last_of_process[process] != none) {
            process_target[last_of_process[process]] = node;
            ++graph.indegree[node];
        }
        last_of_process[process] = index;
        if (capacity == 0u) {
            channel_target[index] = is_send ? graph.receive_at[slot] : none;
            continue;
        }
        if (!is_send || step >= capacity) ++graph.indegree[index];
        if (is_send) {
            channel_target[index] = graph.receive_at[slot];
        } else {
            channel_target[index] = capacity < step_count[2 * channel] - step ? graph.send_at[slot + capacity] : none;
        }
    }

    // The sort of Kahn over the nodes.  A node releases the node of the next
    // action of each of its processes, and the action that waits on it on a
    // channel with a capacity.
    std::uint32_t order[Actions]{};
    std::uint32_t order_count = 0;
    for (std::uint32_t index = 0; index < Actions; ++index) {
        if (graph.node_of[index] == index && graph.indegree[index] == 0u) {
            order[order_count] = index;
            ++order_count;
        }
    }
    auto const release = [&](std::uint32_t node) constexpr noexcept {
        if (node == none) return;
        --graph.indegree[node];
        if (graph.indegree[node] == 0u) {
            order[order_count] = node;
            ++order_count;
        }
    };
    for (std::uint32_t read = 0; read < order_count; ++read) {
        std::uint32_t const node = order[read];
        release(process_target[node]);
        std::uint32_t const waiting = channel_target[node];
        if (waiting == none) continue;
        if (graph.node_of[waiting] == node) {
            release(process_target[waiting]);
        } else {
            release(waiting);
        }
    }
    if (order_count == node_count) return LivenessFault{};
    return find_liveness_cycle_(protocol, graph);
}

}  // namespace detail

// The first fault of the marked graph of the system (THE MODEL), or
// NoFault.  The edges with no token leave each action to at most two
// actions: the next action of its process, and the action of its channel that
// waits on it.  So the sort of Kahn reads the edges of a node from the arrays
// of the steps, with no list of the edges.  Complexity: O(P + C + A).
template <std::size_t Processes, std::size_t Channels, std::size_t Actions>
[[nodiscard]] constexpr LivenessFault
find_liveness_fault(ProcessSystem<Processes, Channels, Actions> const& protocol) noexcept {
    return detail::find_liveness_fault_<detail::FormCheck::Run>(protocol);
}

// ── The sealed system ───────────────────────────────────────────────────

template <std::size_t Processes, std::size_t Channels, std::size_t Actions>
class SealedSystem;

// Seals a system (THE SEALED SYSTEM): copies it, and finds the fault of the
// form of the copy one time.  Complexity: O(P + C + A), the copy and the
// checks of the form.
template <std::size_t Processes, std::size_t Channels, std::size_t Actions>
[[nodiscard]] constexpr auto seal_system(ProcessSystem<Processes, Channels, Actions> const& protocol) noexcept
    -> SealedSystem<Processes, Channels, Actions>;

// A system checked one time at a boundary (THE SEALED SYSTEM): a copy of the
// system, which no caller can change, and the fault of its form.  seal_system
// is the only door.  A copy of a sealed system is a sealed system, because the
// two fields copy together.  The seal closes the two byte routes, std::bit_cast
// and a lifetime start over bytes, so no image of bytes makes a sealed system
// with a fault that is not the fault of its system.
template <std::size_t Processes, std::size_t Channels, std::size_t Actions>
class SealedSystem {
    ProcessSystem<Processes, Channels, Actions> system_{};
    LivenessFault form_fault_{};
    [[no_unique_address]] ::foundation::lifetime::byte_seal seal_{};

    constexpr explicit SealedSystem(ProcessSystem<Processes, Channels, Actions> const& protocol) noexcept
        : system_{protocol}, form_fault_{detail::find_form_fault_(system_)} {}

    template <std::size_t SealedProcesses, std::size_t SealedChannels, std::size_t SealedActions>
    friend constexpr auto
    seal_system(ProcessSystem<SealedProcesses, SealedChannels, SealedActions> const& protocol) noexcept
        -> SealedSystem<SealedProcesses, SealedChannels, SealedActions>;

public:
    // The system that the seal holds.  The reference is const, so no caller
    // changes the system that the fault describes.
    [[nodiscard]] constexpr ProcessSystem<Processes, Channels, Actions> const& value() const noexcept {
        return system_;
    }

    // The fault of the form of the system, or NoFault.
    [[nodiscard]] constexpr LivenessFault form_fault() const noexcept { return form_fault_; }
};

template <std::size_t Processes, std::size_t Channels, std::size_t Actions>
[[nodiscard]] constexpr auto seal_system(ProcessSystem<Processes, Channels, Actions> const& protocol) noexcept
    -> SealedSystem<Processes, Channels, Actions> {
    return SealedSystem<Processes, Channels, Actions>{protocol};
}

// The first fault of the marked graph of a sealed system: the fault of its
// form that the seal holds, or the body of the check with no check of the
// form.  It gives the fault of find_liveness_fault on the system of the seal.
// Complexity: O(P + C + A).
template <std::size_t Processes, std::size_t Channels, std::size_t Actions>
[[nodiscard]] constexpr LivenessFault
find_liveness_fault(SealedSystem<Processes, Channels, Actions> const& sealed) noexcept {
    LivenessFault const form = sealed.form_fault();
    if (form.is_fault()) return form;
    return detail::find_liveness_fault_<detail::FormCheck::Sealed>(sealed.value());
}

namespace detail {

// The step of an action on its side of its channel: the count of the earlier
// actions with its channel and its move.  Complexity: O(A).
template <std::size_t Processes, std::size_t Channels, std::size_t Actions>
[[nodiscard]] constexpr ChannelStep step_at_(ProcessSystem<Processes, Channels, Actions> const& protocol,
                                             ActionId action) noexcept {
    Action const& act = protocol.actions[static_cast<std::uint32_t>(action)];
    std::uint32_t step = 0;
    for (std::uint32_t index = 0; index < static_cast<std::uint32_t>(action); ++index) {
        Action const& earlier = protocol.actions[index];
        if (earlier.channel == act.channel && earlier.move == act.move) ++step;
    }
    return ChannelStep{step};
}

// The process that receives from the channel, or process 0 for a channel
// with no receive.  Complexity: O(A).
template <std::size_t Processes, std::size_t Channels, std::size_t Actions>
[[nodiscard]] constexpr ProcessId receiver_of_(ProcessSystem<Processes, Channels, Actions> const& protocol,
                                               ChannelId channel) noexcept {
    for (Action const& act : protocol.actions) {
        if (act.channel == channel && act.move == Move::Receive) return act.process;
    }
    return ProcessId{0};
}

}  // namespace detail

// The text of the refusal of a fault that find_liveness_fault gave for this
// system, or the admitted verdict for NoFault.  A fault of another system
// reads no array past its end.  Complexity: O(A) and the names.
template <std::size_t Processes, std::size_t Channels, std::size_t Actions>
[[nodiscard, gnu::cold]] constexpr NetVerdict
explain_liveness_fault(ProcessSystem<Processes, Channels, Actions> const& protocol,
                       LivenessFault const& fault) noexcept {
    auto const action_index = static_cast<std::uint32_t>(fault.action);
    auto const channel_index = static_cast<std::uint32_t>(fault.channel);
    Action const act = action_index < Actions ? protocol.actions[action_index] : Action{};
    NetName const channel_name = channel_index < Channels ? protocol.channels[channel_index].name : NetName{};
    auto const process_name = [&](ProcessId process) constexpr noexcept {
        auto const index = static_cast<std::uint32_t>(process);
        return index < Processes ? protocol.process_names[index] : NetName{};
    };
    switch (fault.code) {
        case LivenessFaultCode::NoFault:
            return NetVerdict{};
        case LivenessFaultCode::ProcessWithoutName:
            return NetVerdict::refuse("liveness: process ")
                .put_number(static_cast<std::uint32_t>(fault.process))
                .put(" has no name.");
        case LivenessFaultCode::ChannelWithoutName:
            return NetVerdict::refuse("liveness: channel ").put_number(channel_index).put(" has no name.");
        case LivenessFaultCode::ActionPastSystem:
            return NetVerdict::refuse("liveness: action ")
                .put_number(action_index)
                .put(" names a process, a channel or a move that the system does not have.");
        case LivenessFaultCode::SideHeldTwice:
            return NetVerdict::refuse("liveness: the ")
                .put(act.move == Move::Send ? "send" : "receive")
                .put(" side of channel '")
                .put_name(channel_name)
                .put("' is held by '")
                .put_name(process_name(fault.process))
                .put("' and by '")
                .put_name(process_name(act.process))
                .put("'.  One process holds each side of a channel.");
        case LivenessFaultCode::SendsWithoutReceives: {
            std::uint32_t sends = 0;
            std::uint32_t receives = 0;
            for (Action const& each : protocol.actions) {
                if (each.channel != fault.channel) continue;
                if (each.move == Move::Send) ++sends;
                if (each.move == Move::Receive) ++receives;
            }
            return NetVerdict::refuse("liveness: channel '")
                .put_name(channel_name)
                .put("' has ")
                .put_number(sends)
                .put(" sends and ")
                .put_number(receives)
                .put(" receives.  Each send has its receive.");
        }
        case LivenessFaultCode::CycleWithoutToken: {
            NetVerdict refusal = NetVerdict::refuse("liveness: a cycle of waits holds no token: ");
            for (std::uint32_t shown = 0; shown < fault.path_count && shown < max_liveness_path; ++shown) {
                if (shown != 0u) refusal.put(" <- ");
                auto const member = static_cast<std::uint32_t>(fault.path[shown]);
                if (member >= Actions) continue;
                Action const& wait = protocol.actions[member];
                detail::put_wait_(refusal, protocol, wait, detail::step_at_(protocol, fault.path[shown]),
                                  detail::receiver_of_(protocol, wait.channel));
            }
            if (fault.is_path_cut) refusal.put(" <- ...");
            refusal.put(".  No action of the cycle can start.");
            return refusal;
        }
        default:
            break;
    }
    return NetVerdict::refuse("liveness: the fault has a code that no check gives.");
}

// Each cycle of waits of the marked graph of the system holds a token
// (THE MODEL).  The refusal of a cycle names its first nodes, each after the
// node that waits on it.  Complexity: O(P + C + A).
template <std::size_t Processes, std::size_t Channels, std::size_t Actions>
[[nodiscard]] consteval NetVerdict
check_liveness(ProcessSystem<Processes, Channels, Actions> const& protocol) noexcept {
    return explain_liveness_fault(protocol, find_liveness_fault(protocol));
}

}  // namespace foundation::algebra::net
