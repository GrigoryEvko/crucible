#pragma once

// The net of a stage graph (foundation/algebra/net/Net.h).  The gate of a
// stage graph in fixy/concurrent/Pipeline.h stays the one check that a mint
// of a pipeline reads, with its record StageGraphCheck.  This header gives
// the same graph as one Net value, so the checks of the nets can read a
// pipeline: the dataflow, the forest, the vicious circles, the determinism
// and the canonical form.
//
// THE NET
//   Each stage is an agent, and its ports are its inputs and then its
//   outputs.  An input takes a value of its channel, and an output gives
//   one.  Each edge of the graph is one channel and one wire, from an output
//   of its stage to an input of a later stage.  The inputs of the first
//   stage and the outputs of the last stage are the ends of the graph.  Each
//   end is a boundary agent with one port and a channel of its own: an input
//   end gives, and an output end takes.
//
//   The agents are the stages in the order of the stage pack, then the input
//   ends, then the output ends.  The wires are the edges in the order of the
//   edge pack, then the wires of the input ends, then the wires of the
//   output ends.  Each channel is a type of its own, in the order of the
//   wires.
//
//   The net names each kind and each type by its role, and not by a C++
//   type: each stage kind is 'stage' with the ports 'in' and 'out', the two
//   end kinds are 'input' and 'output', and each type is 'channel'.  So the
//   canonical form gives the shape of the graph, and two graphs of one shape
//   are the same net, whatever their stages do.  The wiring check still
//   reads the channel of each wire, because each channel is its own type.
//
//   Each kind keeps a closed merge door, so determinism_of gives
//   Deterministic.  A stage that merges its inputs in the order of their
//   arrival states that in a net of its own.
//
// THE BOUNDS
//   A kind has at most max_agent_ports ports, so each stage has at most
//   eight ports.  The gate of the graph holds first, so each port of a stage
//   that is no end of the graph has exactly one edge.
//
// THE MARKED SYSTEM
//   marked_system_of_stage_graph gives the graph as a system of processes
//   (foundation/algebra/net/Priority.h), so the liveness check can read a
//   pipeline.  Each stage is a process that loops: in each period it
//   receives each input in port order, and then it sends each output in
//   port order, as the drain loop of a stage body does.  A source process
//   sends into each input of the first stage, and a sink process receives
//   each output of the last stage, each in port order.  The processes are
//   the stages in the order of the stage pack, then the source, then the
//   sink, named 'stage 0', 'source' and 'sink'.  The channels are the
//   channels of THE NET in the same order, named 'edge 0', 'input 0' and
//   'output 0'.
//
//   The capacity of a channel is the channel_capacity that the handle of
//   the graph on that channel states (StatesHandleCapacity): the output
//   handle of the stage before an edge or before an output end, and the input
//   handle of the first stage for an input end.  A handle that states no
//   capacity, such as the writer of a snapshot, reads as a rendezvous.  A
//   system that is live on rendezvous channels is live on channels of each
//   capacity, so the capacity 0 can refuse a live pipeline, and it never
//   admits a pipeline that deadlocks.  A capacity above the range of a
//   channel of the system reads as the top of the range, by the same rule.
//
//   The edges of a well-formed graph run forward.  With a capacity of one
//   message or more on each channel, each wait of a period then goes forward
//   in the order of the stages, so the marked system is live.  A rendezvous
//   joins a send and its receive into one node.  Then a stage that receives
//   its inputs in another order than the stages before it send them can wait
//   in a cycle: a fan-out that sends to input 1 of a fan-in first, while the
//   fan-in receives input 0 first through a second path
//   (test/fixy/test_pipeline_dag.cpp).  The check also gains its value when a
//   loop joins the graph, such as the loop of the Vigil from the foreground
//   through the pipeline and back to the foreground (THE LOOP).
//
// THE LOOP
//   A loop is a system of processes over typed ends (StageLoop).  Each
//   process is a LoopProcess: a type that names it, and its ends in the order
//   of one period of the process.  An end is one of these:
//     SendsOn<Channel>      one send into the channel
//     ReceivesOn<Channel>   one receive from the channel
//     InputsOf<Stage>       one receive from the channel of each input handle
//                           of the stage, in port order
//     OutputsOf<Stage>      one send into the channel of each output handle
//                           of the stage, in port order
//     StageEdges<Graph, S>  the actions of stage S of the graph on the edges
//                           of the graph, in the order of the marked system
//                           of the graph (THE MARKED SYSTEM): a send through
//                           the output handle of the edge, and a receive
//                           through its input handle.  The ends of the graph
//                           are no part of it.
//     WritesLock<Lock, Inside...>
//                           the bracket of the writer of a sequence lock, and
//                           the ends that the writer does inside it, in order
//     ReadsLock<Lock, Inside...>
//                           one read of a sequence lock, which waits while a
//                           bracket of the writer is open, and the ends that
//                           the reader does inside the read, in order
//   A channel is a type: the type that SendsOn or ReceivesOn names, or the
//   channel_type of a handle (fixy/concurrent/HandleTraits.h).  StageEdges
//   gives each edge the channel_type of its output handle.  Two ends that
//   name one type are two sides of one channel, so an end that names another
//   type names another channel.  The type tells two channels apart, and the
//   identity of an instance does not.  The channels stand in the order of
//   their first ends, with the names 'channel 0' and on.  Each end of a
//   channel can state the capacity of the channel: the channel_capacity of
//   the type of SendsOn or ReceivesOn, or of the handle of an end of InputsOf,
//   OutputsOf or StageEdges (StatesHandleCapacity).  The ends that state a
//   capacity state the same capacity, and the door refuses two ends of one
//   channel that state two capacities.  The capacity of the channel is the
//   capacity that its ends state.  A channel whose ends state none reads as a
//   rendezvous, by the rule of THE MARKED SYSTEM.  A lock is a type too,
//   and the locks stand in the order of their first ends, with the names
//   'lock 0' and on.  pipeline_graph_t gives the graph of a minted pipeline,
//   so StageEdges reads the stages of a pipeline as the gate of the pipeline
//   reads them.
//
//   marked_system_of_loop gives the processes in the order of the loop, each
//   with the actions of its channel ends in the order of its ends.  An end
//   inside a bracket stands after the end of its lock.  A lock end adds no
//   action (THE LOCKS).  The door ensure_loop_live checks the locks and the
//   capacities, and then it checks the liveness of the marked system.
//
// THE LOCKS
//   The check of the marked graph reads no bracket (THE LIMITS of
//   foundation/algebra/net/Priority.h).  So the door checks each lock by
//   three rules.  First, one process writes the lock, and one other process
//   reads it.  Second, a process that writes or reads the lock waits on
//   nothing before it leaves the bracket.  A send waits for a free slot, a
//   receive waits for its message, and a read of a lock waits while the
//   writer of that lock is in a bracket.  So a bracket holds none of them.  A
//   bracket of the writer of another lock waits on nothing, so it can stand
//   inside a bracket of a writer.  Third, no bracket of a writer stands inside
//   a read.  A read that meets a write runs again, and each end inside the
//   read runs again with it.  So a write inside a read runs two times.
//
//   Then the writer leaves each bracket with no wait, and each wait of a
//   reader on a bracket ends.  So a lock adds no wait to the marked system,
//   and the liveness of the marked system is the liveness of the loop.  The
//   second rule also holds for a read, because a read can run again.
//
// THE ARMS
//   A process that polls a channel and goes on when the channel is empty
//   does not wait on it.  At each poll, such a process takes one of some
//   arms, and a caller checks one loop for each arm.  The door refuses an arm
//   whose period sends into a channel more often or less often than it
//   receives from it.  So each period of an admitted arm gives back the
//   marking where it starts: each channel holds as many messages as at the
//   start, each bracket is closed, and each process stands at its first end.
//
//   Then a run that takes the admitted arms in any sequence is live, if three
//   conditions hold.  In each period, the processes take one arm together.
//   The arms agree on each channel: one process sends into it, one process
//   receives from it, and the capacity is the same.  The arms agree on the
//   writer and the reader of each lock.  The proof: the run is one marked
//   graph.  Inside one period, its edges with no token are the edges with no
//   token of the marked graph of the arm of that period, which hold no cycle.
//   Each period starts with as many sends as receives on each channel, so
//   each other edge goes from a period to a later period.  Thus the edges
//   with no token hold no cycle, each action has a finite count of actions
//   before it, and each action starts.
//
//   The door ensure_arms_live checks each arm with the door of a loop, and
//   then it checks the second and the third condition across the arms.  The
//   arms compare a process by the type of its name, so each process of an
//   arm has a name of its own.  They compare a channel and a lock by its type.
//   The sends into a channel stand in one process, and so do its receives.
//   The ends that state its capacity state one capacity.  An end that states
//   none agrees with each capacity: an arm whose channel reads as a
//   rendezvous is live with each capacity of that channel, because a slot
//   more only adds a token to the marked graph.  The writes of a lock stand in
//   one process, and so do its reads by a process that does not write it.  A
//   channel or a lock of one arm only agrees with each other arm.
//
//   The first condition is a condition of the run, and no door reads it, so
//   the caller states it.  A poll can let two processes take two arms in one
//   period, when a process serves a message in the period after the period of
//   its sender.  Then the caller checks the arm of that shift too, so that the
//   processes take one arm together in each period again.
//
// THE COSTS
//   One constant evaluation for each graph, linear in the stages, the ports
//   and the edges.  The reading of a loop of N ends is quadratic in N,
//   because each end looks for the first end of its channel or its lock.  The
//   wiring check of the locks reads the N ends for each lock, and it does two
//   scans of the N ends for each lock end, so it is O(N * K) for K lock ends.
//   The rules of the brackets and of the capacities, the marked system and
//   the shape of a reading are linear in N, and the liveness check is linear
//   in the processes, the channels and the actions.  A StageEdges end reads
//   the marked system of its graph.  So the check of a loop is quadratic in
//   its ends.  The door over the arms reads the ends of all the arms one more
//   time, and its check is quadratic in those ends and in their processes.

#include <fixy/concurrent/HandleTraits.h>
#include <fixy/concurrent/Pipeline.h>
#include <foundation/algebra/net/Net.h>
#include <foundation/algebra/net/Priority.h>

#include <cstddef>
#include <cstdint>
#include <meta>

namespace fixy::concurrent {

// True when a handle type states the capacity of its channel: the count of
// the messages that the channel holds.
template <class Handle>
concept StatesHandleCapacity = requires { static_cast<std::size_t>(Handle::channel_capacity); };

namespace detail {

// The channel of each port of each stage of a graph whose gate holds: the
// edge of the port, or the channel of its end (THE NET).  Port P of stage S
// is the slot S * max_agent_ports + P, the inputs before the outputs.
template <std::size_t Slots>
struct stage_channel_map {
    std::size_t channel_of[Slots]{};
};

// The channel map of a graph.  The bound of the ports is a check in the body
// and no gate, so the header adds no gate that reads the traits of a stage.
// Complexity: linear in the stages, the ports and the edges.
template <class... Stages, class... Edges>
[[nodiscard]] consteval auto stage_channel_map_(StagePack<Stages...>, EdgePack<Edges...>) noexcept {
    namespace net = ::foundation::algebra::net;
    static_assert(((stage_input_count_v<Stages> + stage_output_count_v<Stages> <= net::max_agent_ports) && ...),
                  "a stage of more than eight ports has no net: a kind of a net has at most eight ports");
    constexpr std::size_t ports = net::max_agent_ports;
    constexpr std::size_t stage_count = sizeof...(Stages);
    constexpr std::size_t edge_count = sizeof...(Edges);
    constexpr std::size_t inputs[] = {stage_input_count_v<Stages>...};
    constexpr std::size_t outputs[] = {stage_output_count_v<Stages>...};
    constexpr std::size_t last = stage_count - 1;
    constexpr std::size_t input_ends = inputs[0];
    constexpr std::size_t output_ends = outputs[last];
    stage_channel_map<stage_count * ports> map{};
    std::size_t edge = 0;
    ((map.channel_of[Edges::from * ports + inputs[Edges::from] + Edges::from_output] = edge,
      map.channel_of[Edges::to * ports + Edges::to_input] = edge, ++edge),
     ...);
    for (std::size_t end = 0; end < input_ends; ++end)
        map.channel_of[end] = edge_count + end;
    for (std::size_t end = 0; end < output_ends; ++end)
        map.channel_of[last * ports + inputs[last] + end] = edge_count + input_ends + end;
    return map;
}

// The net of a stage graph whose gate holds (THE NET).  Complexity: linear in
// the stages, the ports and the edges.
template <class... Stages, class... Edges>
[[nodiscard]] consteval auto net_of_stage_graph_(StagePack<Stages...> stages, EdgePack<Edges...> edges) noexcept {
    namespace net = ::foundation::algebra::net;
    constexpr std::size_t ports = net::max_agent_ports;
    constexpr std::size_t stage_count = sizeof...(Stages);
    constexpr std::size_t edge_count = sizeof...(Edges);
    constexpr std::size_t inputs[] = {stage_input_count_v<Stages>...};
    constexpr std::size_t outputs[] = {stage_output_count_v<Stages>...};
    constexpr std::size_t last = stage_count - 1;
    constexpr std::size_t input_ends = inputs[0];
    constexpr std::size_t output_ends = outputs[last];
    constexpr std::size_t channels = edge_count + input_ends + output_ends;
    constexpr std::size_t agents = stage_count + input_ends + output_ends;
    net::Net<channels, agents, agents, channels> graph{};
    auto const map = stage_channel_map_(stages, edges);

    auto const type_at = [](std::size_t channel) consteval noexcept {
        return net::TypeId{static_cast<std::uint16_t>(channel)};
    };
    for (std::size_t channel = 0; channel < channels; ++channel)
        graph.signature.type_names[channel] = net::NetName{"channel"};
    for (std::size_t stage = 0; stage < stage_count; ++stage) {
        net::AgentKind& kind = graph.signature.kinds[stage];
        kind.name = net::NetName{"stage"};
        for (std::size_t port = 0; port < inputs[stage]; ++port) {
            kind.port_names[port] = net::NetName{"in"};
            kind.port_types[port] = net::take(type_at(map.channel_of[stage * ports + port]));
        }
        for (std::size_t port = inputs[stage]; port < inputs[stage] + outputs[stage]; ++port) {
            kind.port_names[port] = net::NetName{"out"};
            kind.port_types[port] = net::give(type_at(map.channel_of[stage * ports + port]));
        }
        graph.agents[stage] = net::KindId{static_cast<std::uint32_t>(stage)};
    }
    for (std::size_t end = 0; end < input_ends + output_ends; ++end) {
        bool const is_input = end < input_ends;
        net::AgentKind& kind = graph.signature.kinds[stage_count + end];
        kind.name = is_input ? net::NetName{"input"} : net::NetName{"output"};
        kind.port_names[0] = net::NetName{"value"};
        kind.port_types[0] = is_input ? net::give(type_at(edge_count + end)) : net::take(type_at(edge_count + end));
        kind.is_boundary = true;
        graph.agents[stage_count + end] = net::KindId{static_cast<std::uint32_t>(stage_count + end)};
    }

    auto const agent_at = [](std::size_t agent) consteval noexcept {
        return net::AgentId{static_cast<std::uint32_t>(agent)};
    };
    auto const port_of = [](std::size_t port) consteval noexcept { return static_cast<std::uint32_t>(port); };
    std::size_t wire = 0;
    ((graph.wires[wire] =
          net::Wire{net::PortRef{agent_at(Edges::from), port_of(inputs[Edges::from] + Edges::from_output)},
                    net::PortRef{agent_at(Edges::to), port_of(Edges::to_input)}},
      ++wire),
     ...);
    for (std::size_t end = 0; end < input_ends; ++end) {
        graph.wires[edge_count + end] =
            net::Wire{net::PortRef{agent_at(stage_count + end), 0}, net::PortRef{agent_at(0), port_of(end)}};
    }
    for (std::size_t end = 0; end < output_ends; ++end) {
        graph.wires[edge_count + input_ends + end] =
            net::Wire{net::PortRef{agent_at(last), port_of(inputs[last] + end)},
                      net::PortRef{agent_at(stage_count + input_ends + end), 0}};
    }
    return graph;
}

// The capacity of the channel of a handle (THE MARKED SYSTEM): the capacity
// that it states, cut at the top of the range of a channel of a system, or
// 0 for a handle that states none.
template <class Handle>
[[nodiscard]] consteval std::uint32_t capacity_of_handle_() noexcept {
    if constexpr (StatesHandleCapacity<Handle>) {
        constexpr auto stated = static_cast<std::size_t>(Handle::channel_capacity);
        return stated < 0xFFFFFFFFu ? static_cast<std::uint32_t>(stated) : 0xFFFFFFFFu;
    } else {
        return 0;
    }
}

// Whether the type of an end of a loop states the capacity of its channel.
enum class CapacityStatement : std::uint8_t {
    None = 0,
    Stated = 1,
};

// The capacity statement of a handle or of a channel type.
template <class Handle>
[[nodiscard]] consteval CapacityStatement capacity_statement_of_() noexcept {
    return StatesHandleCapacity<Handle> ? CapacityStatement::Stated : CapacityStatement::None;
}

// The kinds of the names of the processes and the channels of the marked
// system of a graph, and of the channels and the locks of a loop.
enum class LabelKind : std::uint8_t {
    Stage = 0,
    Edge = 1,
    Input = 2,
    Output = 3,
    Channel = 4,
    Lock = 5,
};

// The characters of a name of a process or of a channel, with a zero after
// the last character.
struct index_label {
    char text[32]{};
};

// The label of an index: the word of the kind, a space, and the decimal
// digits of the index.
template <LabelKind Kind, std::size_t Index>
[[nodiscard]] consteval index_label label_of_index_() noexcept {
    index_label label{};
    char const* const word = Kind == LabelKind::Stage   ? "stage "
                           : Kind == LabelKind::Edge    ? "edge "
                           : Kind == LabelKind::Input   ? "input "
                           : Kind == LabelKind::Output  ? "output "
                           : Kind == LabelKind::Channel ? "channel "
                                                        : "lock ";
    std::size_t size = 0;
    for (; word[size] != '\0'; ++size)
        label.text[size] = word[size];
    char digits[20]{};
    std::size_t count = 0;
    std::size_t value = Index;
    do {
        digits[count] = static_cast<char>('0' + static_cast<int>(value % 10u));
        ++count;
        value /= 10u;
    } while (value != 0u);
    while (count != 0u) {
        --count;
        label.text[size] = digits[count];
        ++size;
    }
    return label;
}

// The label of an index in static storage, so a name can borrow its text.
template <LabelKind Kind, std::size_t Index>
inline constexpr index_label index_label_v = label_of_index_<Kind, Index>();

// Names the processes of the stages from Index to Count: 'stage Index' and
// on.  Complexity: one step for each stage.
template <std::size_t Index, std::size_t Count, class System>
consteval void name_stages_(System& marked) noexcept {
    if constexpr (Index < Count) {
        marked.process_names[Index] = ::foundation::algebra::net::NetName{index_label_v<LabelKind::Stage, Index>.text};
        name_stages_<Index + 1, Count>(marked);
    }
}

// Names the channels from First + Index to First + Count with the labels of
// the kind, from 'Index' on.  Complexity: one step for each channel.
template <LabelKind Kind, std::size_t First, std::size_t Index, std::size_t Count, class System>
consteval void name_channels_(System& marked) noexcept {
    if constexpr (Index < Count) {
        marked.channels[First + Index].name = ::foundation::algebra::net::NetName{index_label_v<Kind, Index>.text};
        name_channels_<Kind, First, Index + 1, Count>(marked);
    }
}

// Gives the channels of the input ends, from First on, the capacity of the
// input handle of each port of the first stage from Port on.
template <class Stage, std::size_t First, std::size_t Port, class System>
consteval void size_input_ends_(System& marked) noexcept {
    if constexpr (Port < stage_input_count_v<Stage>) {
        marked.channels[First + Port].capacity = capacity_of_handle_<stage_input_handle_t<Stage, Port>>();
        size_input_ends_<Stage, First, Port + 1>(marked);
    }
}

// Gives the channels of the output ends, from First on, the capacity of the
// output handle of each port of the last stage from Port on.
template <class Stage, std::size_t First, std::size_t Port, class System>
consteval void size_output_ends_(System& marked) noexcept {
    if constexpr (Port < stage_output_count_v<Stage>) {
        marked.channels[First + Port].capacity = capacity_of_handle_<stage_output_handle_t<Stage, Port>>();
        size_output_ends_<Stage, First, Port + 1>(marked);
    }
}

// The marked system of a stage graph whose gate holds (THE MARKED SYSTEM).
// Complexity: linear in the stages, the ports and the edges.
template <class... Stages, class... Edges>
[[nodiscard]] consteval auto marked_system_of_stage_graph_(StagePack<Stages...> stages,
                                                           EdgePack<Edges...> edges) noexcept {
    namespace net = ::foundation::algebra::net;
    constexpr std::size_t ports = net::max_agent_ports;
    constexpr std::size_t stage_count = sizeof...(Stages);
    constexpr std::size_t edge_count = sizeof...(Edges);
    constexpr std::size_t inputs[] = {stage_input_count_v<Stages>...};
    constexpr std::size_t outputs[] = {stage_output_count_v<Stages>...};
    constexpr std::size_t last = stage_count - 1;
    constexpr std::size_t input_ends = inputs[0];
    constexpr std::size_t output_ends = outputs[last];
    constexpr std::size_t channels = edge_count + input_ends + output_ends;
    constexpr std::size_t actions =
        ((stage_input_count_v<Stages> + stage_output_count_v<Stages>)+... + 0) + input_ends + output_ends;
    net::ProcessSystem<stage_count + 2, channels, actions> marked{};
    auto const map = stage_channel_map_(stages, edges);

    name_stages_<0, stage_count>(marked);
    marked.process_names[stage_count] = net::NetName{"source"};
    marked.process_names[stage_count + 1] = net::NetName{"sink"};
    name_channels_<LabelKind::Edge, 0, 0, edge_count>(marked);
    name_channels_<LabelKind::Input, edge_count, 0, input_ends>(marked);
    name_channels_<LabelKind::Output, edge_count + input_ends, 0, output_ends>(marked);

    std::size_t edge = 0;
    ((marked.channels[edge].capacity =
          capacity_of_handle_<stage_output_handle_t<Stages...[Edges::from], Edges::from_output>>(),
      ++edge),
     ...);
    size_input_ends_<Stages...[0], edge_count, 0>(marked);
    size_output_ends_<Stages...[last], edge_count + input_ends, 0>(marked);

    std::size_t next = 0;
    for (std::uint32_t stage = 0; stage < stage_count; ++stage) {
        for (std::size_t port = 0; port < inputs[stage] + outputs[stage]; ++port) {
            marked.actions[next] =
                net::Action{.process = net::ProcessId{stage},
                            .channel = net::ChannelId{static_cast<std::uint32_t>(map.channel_of[stage * ports + port])},
                            .move = port < inputs[stage] ? net::Move::Receive : net::Move::Send};
            ++next;
        }
    }
    constexpr net::ProcessId source{static_cast<std::uint32_t>(stage_count)};
    constexpr net::ProcessId sink{static_cast<std::uint32_t>(stage_count + 1)};
    for (std::uint32_t end = 0; end < input_ends; ++end) {
        marked.actions[next] = net::Action{.process = source,
                                           .channel = net::ChannelId{static_cast<std::uint32_t>(edge_count) + end},
                                           .move = net::Move::Send};
        ++next;
    }
    for (std::uint32_t end = 0; end < output_ends; ++end) {
        marked.actions[next] =
            net::Action{.process = sink,
                        .channel = net::ChannelId{static_cast<std::uint32_t>(edge_count + input_ends) + end},
                        .move = net::Move::Receive};
        ++next;
    }
    return marked;
}

}  // namespace detail

// The net of a stage graph (THE NET), for a graph whose gate holds and whose
// stages each have at most eight ports.
template <class Graph>
    requires StageGraphWellFormed<Graph>
[[nodiscard]] consteval auto net_of_stage_graph() noexcept {
    using traits = detail::stage_graph_traits<Graph>;
    return detail::net_of_stage_graph_(typename traits::stage_pack_type{}, typename traits::edge_pack_type{});
}

// The marked system of a stage graph (THE MARKED SYSTEM), for a graph whose
// gate holds and whose stages each have at most eight ports.
template <class Graph>
    requires StageGraphWellFormed<Graph>
[[nodiscard]] consteval auto marked_system_of_stage_graph() noexcept {
    using traits = detail::stage_graph_traits<Graph>;
    return detail::marked_system_of_stage_graph_(typename traits::stage_pack_type{}, typename traits::edge_pack_type{});
}

namespace detail {

// The edges of a chain from the stage Link to the stage Last: an edge from
// each stage to the next.  Complexity: one step for each edge.
template <std::size_t Link, std::size_t Last, class... Edges>
struct chain_edges {
    using type = typename chain_edges<Link + 1, Last, Edges..., StageEdge<Link, Link + 1>>::type;
};
template <std::size_t Last, class... Edges>
struct chain_edges<Last, Last, Edges...> {
    using type = EdgePack<Edges...>;
};

// The stage graph of a pipeline (pipeline_graph_t).
template <class Runner>
struct pipeline_graph {};
template <class... Stages>
struct pipeline_graph<Pipeline<Stages...>> {
    using type = StageGraph<StagePack<Stages...>, typename chain_edges<0, sizeof...(Stages) - 1>::type>;
};
template <class Runner>
    requires requires { typename Runner::graph_type; }
struct pipeline_graph<Runner> {
    using type = typename Runner::graph_type;
};

}  // namespace detail

// The stage graph of a minted pipeline: the graph of a PipelineDag, and for a
// Pipeline the chain of its stages, with an edge from each stage to the next.
// The gate of a Pipeline admits each link of the chain, so the gate of the
// stage graph holds for the chain too.
template <class Runner>
using pipeline_graph_t = typename detail::pipeline_graph<Runner>::type;

// ── The loop ────────────────────────────────────────────────────────────

// One send into the channel (THE LOOP).
template <class Channel>
struct SendsOn {};

// One receive from the channel.
template <class Channel>
struct ReceivesOn {};

// One receive from the channel of each input handle of the stage, in port
// order.
template <class Stage>
struct InputsOf {};

// One send into the channel of each output handle of the stage, in port
// order.
template <class Stage>
struct OutputsOf {};

// The actions of stage Stage of a stage graph on the edges of the graph, in
// the order of the marked system of the graph.  Stage is the place of the
// stage in the stage pack.
template <class Graph, std::size_t Stage>
struct StageEdges {};

// The bracket of the writer of a sequence lock, and the ends that the writer
// does inside the bracket, in order (THE LOCKS).
template <class Lock, class... Inside>
struct WritesLock {};

// One read of a sequence lock, which waits while a bracket of the writer is
// open, and the ends that the reader does inside the read, in order.
template <class Lock, class... Inside>
struct ReadsLock {};

// A process of a loop: the type that names it, and its ends in the order of
// one period of the process.
template <class Name, class... Ends>
struct LoopProcess {};

// A loop: its processes, in order.
template <class... Processes>
struct StageLoop {};

// True when a type names a process of a loop: its member process_name is a
// NetName.
template <class Name>
concept NamesLoopProcess =
    requires { requires __is_same(decltype(Name::process_name), ::foundation::algebra::net::NetName const); };

namespace detail {

// The kind of an end of a loop, as the reading of the loop holds it.
enum class LoopMove : std::uint8_t {
    Send = 0,
    Receive = 1,
    WriteLock = 2,
    ReadLock = 3,
};

// True for an end of a channel, and false for an end of a lock.
[[nodiscard]] consteval bool is_channel_move_(LoopMove move) noexcept {
    return move == LoopMove::Send || move == LoopMove::Receive;
}

// True when the type is an end of a loop (THE LOOP).
template <class End>
inline constexpr bool is_loop_end_v = false;
template <class Channel>
inline constexpr bool is_loop_end_v<SendsOn<Channel>> = true;
template <class Channel>
inline constexpr bool is_loop_end_v<ReceivesOn<Channel>> = true;
template <class Stage>
inline constexpr bool is_loop_end_v<InputsOf<Stage>> = IsStage<Stage>;
template <class Stage>
inline constexpr bool is_loop_end_v<OutputsOf<Stage>> = IsStage<Stage>;
template <class Lock, class... Inside>
inline constexpr bool is_loop_end_v<WritesLock<Lock, Inside...>> = (is_loop_end_v<Inside> && ...);
template <class Lock, class... Inside>
inline constexpr bool is_loop_end_v<ReadsLock<Lock, Inside...>> = (is_loop_end_v<Inside> && ...);

// The count of the stages of a stage graph.
template <class Graph>
inline constexpr std::size_t graph_stage_count_v = 0;
template <class... Stages, class... Edges>
inline constexpr std::size_t graph_stage_count_v<StageGraph<StagePack<Stages...>, EdgePack<Edges...>>> =
    sizeof...(Stages);

// The count of the edges that leave or enter stage Stage of a stage graph.
template <class Graph, std::size_t Stage>
inline constexpr std::size_t stage_edge_count_v = 0;
template <class... Stages, class... Edges, std::size_t Stage>
inline constexpr std::size_t stage_edge_count_v<StageGraph<StagePack<Stages...>, EdgePack<Edges...>>, Stage> =
    (((Edges::from == Stage ? std::size_t{1} : std::size_t{0}) + (Edges::to == Stage ? std::size_t{1} : std::size_t{0}))
     + ... + std::size_t{0});

template <class Graph, std::size_t Stage>
inline constexpr bool is_loop_end_v<StageEdges<Graph, Stage>> =
    StageGraphWellFormed<Graph> && Stage < graph_stage_count_v<Graph>;

// True when the type is a process of a loop.
template <class Process>
inline constexpr bool is_loop_process_v = false;
template <class Name, class... Ends>
inline constexpr bool is_loop_process_v<LoopProcess<Name, Ends...>> =
    NamesLoopProcess<Name> && (is_loop_end_v<Ends> && ...);

// True when the type is a loop of one process or more.
template <class Loop>
inline constexpr bool is_stage_loop_v = false;
template <class... Processes>
inline constexpr bool is_stage_loop_v<StageLoop<Processes...>> =
    sizeof...(Processes) != 0 && (is_loop_process_v<Processes> && ...);

// The count of the ends of the reading that one end of a process gives.
template <class End>
inline constexpr std::size_t loop_end_count_v = 1;
template <class Stage>
inline constexpr std::size_t loop_end_count_v<InputsOf<Stage>> = stage_input_count_v<Stage>;
template <class Stage>
inline constexpr std::size_t loop_end_count_v<OutputsOf<Stage>> = stage_output_count_v<Stage>;
template <class Graph, std::size_t Stage>
inline constexpr std::size_t loop_end_count_v<StageEdges<Graph, Stage>> = stage_edge_count_v<Graph, Stage>;
template <class Lock, class... Inside>
inline constexpr std::size_t loop_end_count_v<WritesLock<Lock, Inside...>> = (loop_end_count_v<Inside> + ... + 1);
template <class Lock, class... Inside>
inline constexpr std::size_t loop_end_count_v<ReadsLock<Lock, Inside...>> = (loop_end_count_v<Inside> + ... + 1);

// The count of the ends of the reading that a process gives.
template <class Process>
inline constexpr std::size_t loop_process_end_count_v = 0;
template <class Name, class... Ends>
inline constexpr std::size_t loop_process_end_count_v<LoopProcess<Name, Ends...>> = (loop_end_count_v<Ends> + ... + 0);

// The key of a type that names a channel or a lock: its reflection, read
// through its aliases.  Two ends name one channel exactly when they name one
// type.
template <class Key>
[[nodiscard]] consteval std::meta::info key_of_() noexcept {
    return std::meta::dealias(^^Key);
}

// The place of an end in a reading.
enum class LoopEndId : std::uint32_t {
};

// The bracket of an end that stands inside no bracket.
inline constexpr LoopEndId no_bracket{0xFFFFFFFFu};

// One end of a loop, as the reading holds it: the process, the first end of
// its channel or of its lock, the lock end of the innermost bracket that holds
// it, the capacity that the type of the end states, whether the type states
// one, and its kind.
struct loop_end {
    ::foundation::algebra::net::ProcessId process{};
    LoopEndId first{};
    LoopEndId bracket = no_bracket;
    std::uint32_t capacity = 0;
    LoopMove move = LoopMove::Send;
    CapacityStatement statement = CapacityStatement::None;
    std::uint8_t reserved[2]{};
};

// The ends of a loop, in the order of the processes and of their ends.
template <std::size_t Count>
struct loop_reading {
    loop_end ends[Count == 0 ? 1 : Count]{};
    std::size_t count = 0;
};

// The ends of a loop and the key of each end, while the reading runs.  GCC
// gives no constant of an array of reflections whose equal elements it folds
// into one range, so only this local holds the keys, and the reading holds
// the first end of each key.
template <std::size_t Count>
struct loop_scan {
    loop_end ends[Count == 0 ? 1 : Count]{};
    std::meta::info keys[Count == 0 ? 1 : Count]{};
    std::size_t count = 0;
};

// Puts one end and its key at the end of the scan.
template <std::size_t Count>
consteval void put_loop_end_(loop_scan<Count>& scan, loop_end const& end, std::meta::info key) noexcept {
    scan.ends[scan.count] = end;
    scan.keys[scan.count] = key;
    ++scan.count;
}

// Puts the receive of the channel of each input handle of the stage, from
// Port on, inside the bracket.
template <class Stage, std::size_t Port, std::size_t Count>
consteval void put_stage_inputs_(loop_scan<Count>& scan, ::foundation::algebra::net::ProcessId process,
                                 LoopEndId bracket) noexcept {
    if constexpr (Port < stage_input_count_v<Stage>) {
        using handle = stage_input_handle_t<Stage, Port>;
        if constexpr (!NamesItsChannel<handle>) {
            static_assert(NamesItsChannel<handle>,
                          "a loop reads an input handle that names no channel: each handle of a loop states its "
                          "channel_type");
        } else {
            put_loop_end_(scan,
                          loop_end{.process = process,
                                   .bracket = bracket,
                                   .capacity = capacity_of_handle_<handle>(),
                                   .move = LoopMove::Receive,
                                   .statement = capacity_statement_of_<handle>()},
                          key_of_<handle_channel_t<handle>>());
            put_stage_inputs_<Stage, Port + 1>(scan, process, bracket);
        }
    }
}

// Puts the send into the channel of each output handle of the stage, from
// Port on, inside the bracket.
template <class Stage, std::size_t Port, std::size_t Count>
consteval void put_stage_outputs_(loop_scan<Count>& scan, ::foundation::algebra::net::ProcessId process,
                                  LoopEndId bracket) noexcept {
    if constexpr (Port < stage_output_count_v<Stage>) {
        using handle = stage_output_handle_t<Stage, Port>;
        if constexpr (!NamesItsChannel<handle>) {
            static_assert(NamesItsChannel<handle>,
                          "a loop reads an output handle that names no channel: each handle of a loop states its "
                          "channel_type");
        } else {
            put_loop_end_(scan,
                          loop_end{.process = process,
                                   .bracket = bracket,
                                   .capacity = capacity_of_handle_<handle>(),
                                   .move = LoopMove::Send,
                                   .statement = capacity_statement_of_<handle>()},
                          key_of_<handle_channel_t<handle>>());
            put_stage_outputs_<Stage, Port + 1>(scan, process, bracket);
        }
    }
}

// The marked system of a stage graph, one time in a unit.  Each reading of a
// loop with a StageEdges end of the graph reads this one constant.
template <class Graph>
inline constexpr auto stage_graph_system_v = marked_system_of_stage_graph<Graph>();

// Puts the action of stage Stage on each edge of a graph whose gate holds,
// in the order of the marked system of the graph, inside the bracket.  The
// key of an edge is the channel_type of its output handle, and the gate of
// the graph proves that the input handle names the same channel.  A send
// takes the capacity that the output handle of the edge states, and a
// receive the capacity that its input handle states.  Complexity: linear in
// the actions of the marked system of the graph.
template <class Graph, std::size_t Stage, std::size_t Count, class... Stages, class... Edges>
consteval void put_stage_edges_(loop_scan<Count>& scan, ::foundation::algebra::net::ProcessId process,
                                LoopEndId bracket, StagePack<Stages...>, EdgePack<Edges...>) noexcept {
    namespace net = ::foundation::algebra::net;
    constexpr std::size_t edge_count = sizeof...(Edges);
    if constexpr (edge_count != 0) {
        constexpr auto const& marked = stage_graph_system_v<Graph>;
        std::meta::info const keys[] = {
            key_of_<handle_channel_t<stage_output_handle_t<Stages...[Edges::from], Edges::from_output>>>()...};
        constexpr std::uint32_t input_capacities[] = {
            capacity_of_handle_<stage_input_handle_t<Stages...[Edges::to], Edges::to_input>>()...};
        constexpr CapacityStatement output_statements[] = {
            capacity_statement_of_<stage_output_handle_t<Stages...[Edges::from], Edges::from_output>>()...};
        constexpr CapacityStatement input_statements[] = {
            capacity_statement_of_<stage_input_handle_t<Stages...[Edges::to], Edges::to_input>>()...};
        for (net::Action const& action : marked.actions) {
            auto const channel = static_cast<std::uint32_t>(action.channel);
            if (action.process != net::ProcessId{static_cast<std::uint32_t>(Stage)} || channel >= edge_count) continue;
            bool const is_send = action.move == net::Move::Send;
            put_loop_end_(scan,
                          loop_end{.process = process,
                                   .bracket = bracket,
                                   .capacity = is_send ? marked.channels[channel].capacity : input_capacities[channel],
                                   .move = is_send ? LoopMove::Send : LoopMove::Receive,
                                   .statement = is_send ? output_statements[channel] : input_statements[channel]},
                          keys[channel]);
        }
    }
}

// Puts the ends that one end of a process gives, inside the bracket.
template <std::size_t Count, class Channel>
consteval void put_end_(loop_scan<Count>& scan, ::foundation::algebra::net::ProcessId process, LoopEndId bracket,
                        SendsOn<Channel>) noexcept {
    put_loop_end_(scan,
                  loop_end{.process = process,
                           .bracket = bracket,
                           .capacity = capacity_of_handle_<Channel>(),
                           .move = LoopMove::Send,
                           .statement = capacity_statement_of_<Channel>()},
                  key_of_<Channel>());
}
template <std::size_t Count, class Channel>
consteval void put_end_(loop_scan<Count>& scan, ::foundation::algebra::net::ProcessId process, LoopEndId bracket,
                        ReceivesOn<Channel>) noexcept {
    put_loop_end_(scan,
                  loop_end{.process = process,
                           .bracket = bracket,
                           .capacity = capacity_of_handle_<Channel>(),
                           .move = LoopMove::Receive,
                           .statement = capacity_statement_of_<Channel>()},
                  key_of_<Channel>());
}
template <std::size_t Count, class Stage>
consteval void put_end_(loop_scan<Count>& scan, ::foundation::algebra::net::ProcessId process, LoopEndId bracket,
                        InputsOf<Stage>) noexcept {
    put_stage_inputs_<Stage, 0>(scan, process, bracket);
}
template <std::size_t Count, class Stage>
consteval void put_end_(loop_scan<Count>& scan, ::foundation::algebra::net::ProcessId process, LoopEndId bracket,
                        OutputsOf<Stage>) noexcept {
    put_stage_outputs_<Stage, 0>(scan, process, bracket);
}
template <std::size_t Count, class Graph, std::size_t Stage>
consteval void put_end_(loop_scan<Count>& scan, ::foundation::algebra::net::ProcessId process, LoopEndId bracket,
                        StageEdges<Graph, Stage>) noexcept {
    using traits = stage_graph_traits<Graph>;
    put_stage_edges_<Graph, Stage>(scan, process, bracket, typename traits::stage_pack_type{},
                                   typename traits::edge_pack_type{});
}
// The end of the lock, and then each end inside the bracket, which names this
// lock end as its bracket.
template <std::size_t Count, class Lock, class... Inside>
consteval void put_end_(loop_scan<Count>& scan, ::foundation::algebra::net::ProcessId process, LoopEndId bracket,
                        WritesLock<Lock, Inside...>) noexcept {
    [[maybe_unused]] LoopEndId const opened{static_cast<std::uint32_t>(scan.count)};
    put_loop_end_(scan, loop_end{.process = process, .bracket = bracket, .move = LoopMove::WriteLock}, key_of_<Lock>());
    (put_end_(scan, process, opened, Inside{}), ...);
}
template <std::size_t Count, class Lock, class... Inside>
consteval void put_end_(loop_scan<Count>& scan, ::foundation::algebra::net::ProcessId process, LoopEndId bracket,
                        ReadsLock<Lock, Inside...>) noexcept {
    [[maybe_unused]] LoopEndId const opened{static_cast<std::uint32_t>(scan.count)};
    put_loop_end_(scan, loop_end{.process = process, .bracket = bracket, .move = LoopMove::ReadLock}, key_of_<Lock>());
    (put_end_(scan, process, opened, Inside{}), ...);
}

// Puts the ends of a process, in the order of its ends.
template <std::size_t Count, class Name, class... Ends>
consteval void put_process_(loop_scan<Count>& scan, ::foundation::algebra::net::ProcessId process,
                            LoopProcess<Name, Ends...>) noexcept {
    (put_end_(scan, process, no_bracket, Ends{}), ...);
}

// The name of a process.
template <class Name, class... Ends>
[[nodiscard]] consteval ::foundation::algebra::net::NetName name_of_loop_process_(LoopProcess<Name, Ends...>) noexcept {
    return Name::process_name;
}

// The reading of a loop (THE LOOP).  The first end of an end is the first end
// whose key is its key and whose kind names a channel when its kind does.
// Complexity: quadratic in the ends.
template <class... Processes>
[[nodiscard]] consteval auto read_loop_(StageLoop<Processes...>) noexcept {
    constexpr std::size_t count = (loop_process_end_count_v<Processes> + ... + 0);
    loop_scan<count> scan{};
    std::uint32_t place = 0;
    ((put_process_(scan, ::foundation::algebra::net::ProcessId{place}, Processes{}), ++place), ...);
    loop_reading<count> reading{};
    reading.count = scan.count;
    for (std::uint32_t index = 0; index < scan.count; ++index) {
        bool const is_channel = is_channel_move_(scan.ends[index].move);
        std::uint32_t first = 0;
        while (scan.keys[first] != scan.keys[index] || is_channel_move_(scan.ends[first].move) != is_channel)
            ++first;
        reading.ends[index] = scan.ends[index];
        reading.ends[index].first = LoopEndId{first};
    }
    return reading;
}

// The channels and the locks of a loop (THE LOOP): the channel of each end of
// a channel and the lock of each end of a lock, each in the order of the
// first ends, and the capacity of each channel.
template <std::size_t Count>
struct loop_shape {
    std::uint32_t place_of[Count == 0 ? 1 : Count]{};
    std::uint32_t capacity_of[Count == 0 ? 1 : Count]{};
    std::uint32_t channel_count = 0;
    std::uint32_t lock_count = 0;
    std::uint32_t action_count = 0;
};

// The shape of a reading.  The capacity of a channel is the capacity of the
// first of its ends that states one, or 0 for a channel whose ends state
// none (THE LOOP).  Complexity: linear in the ends.
template <std::size_t Count>
[[nodiscard]] consteval loop_shape<Count> shape_of_loop_(loop_reading<Count> const& reading) noexcept {
    loop_shape<Count> shape{};
    bool is_stated[Count == 0 ? 1 : Count]{};
    for (std::size_t index = 0; index < reading.count; ++index) {
        loop_end const& end = reading.ends[index];
        bool const is_channel = is_channel_move_(end.move);
        auto const first = static_cast<std::size_t>(end.first);
        if (first != index) {
            shape.place_of[index] = shape.place_of[first];
        } else if (is_channel) {
            shape.place_of[index] = shape.channel_count;
            ++shape.channel_count;
        } else {
            shape.place_of[index] = shape.lock_count;
            ++shape.lock_count;
        }
        if (!is_channel) continue;
        ++shape.action_count;
        std::uint32_t const channel = shape.place_of[index];
        if (end.statement == CapacityStatement::Stated && !is_stated[channel]) {
            is_stated[channel] = true;
            shape.capacity_of[channel] = end.capacity;
        }
    }
    return shape;
}

// The rule that the ends of a loop break (THE LOCKS, THE LOOP).
enum class LoopFaultCode : std::uint8_t {
    NoFault = 0,
    // The fault names the lock, the count of the processes that write it,
    // and the count of the processes that read it and do not write it.
    LockUnwired = 1,
    // The fault names the process, the lock of the innermost bracket, and the
    // kind and the place of the first end inside it that waits: a channel, or
    // a lock that the process reads.
    WaitInBracket = 2,
    // The fault names the process, the lock of the read, and the place of the
    // lock of the bracket of the writer inside the read.
    WriteInRead = 3,
    // The fault names the channel, the capacity of the channel, and the other
    // capacity that an end of the channel states.
    CapacityDisagrees = 4,
};

// The fault of the ends of a loop.
struct LoopFault {
    LoopFaultCode code = LoopFaultCode::NoFault;
    LoopMove inside = LoopMove::Send;
    std::uint8_t reserved[2]{};
    ::foundation::algebra::net::ProcessId process{};
    std::uint32_t lock = 0;
    std::uint32_t place = 0;
    std::uint32_t writers = 0;
    std::uint32_t readers = 0;
    std::uint32_t channel_capacity = 0;
    std::uint32_t end_capacity = 0;
};

// True when no end before index has the process, the lock and the kind of the
// end at index.  Complexity: linear in the ends.
template <std::size_t Count>
[[nodiscard]] consteval bool is_first_side_end_(loop_reading<Count> const& reading, loop_shape<Count> const& shape,
                                                LoopEndId index) noexcept {
    auto const at = static_cast<std::uint32_t>(index);
    loop_end const& end = reading.ends[at];
    for (std::uint32_t earlier = 0; earlier < at; ++earlier) {
        loop_end const& other = reading.ends[earlier];
        if (other.move == end.move && other.process == end.process && shape.place_of[earlier] == shape.place_of[at])
            return false;
    }
    return true;
}

// True when the process writes the lock.  Complexity: linear in the ends.
template <std::size_t Count>
[[nodiscard]] consteval bool writes_lock_(loop_reading<Count> const& reading, loop_shape<Count> const& shape,
                                          LoopEndId index) noexcept {
    auto const at = static_cast<std::uint32_t>(index);
    for (std::uint32_t other = 0; other < reading.count; ++other) {
        if (reading.ends[other].move == LoopMove::WriteLock && reading.ends[other].process == reading.ends[at].process
            && shape.place_of[other] == shape.place_of[at])
            return true;
    }
    return false;
}

// The first lock that one process does not write, or that one other process
// does not read, as the fault LockUnwired, or NoFault (THE LOCKS).
// Complexity: O(N * K) for N ends and K lock ends.  Each lock reads the N
// ends, and each of its ends does two scans of the N ends.
template <std::size_t Count>
[[nodiscard]] consteval LoopFault find_lock_wiring_fault_(loop_reading<Count> const& reading,
                                                          loop_shape<Count> const& shape) noexcept {
    for (std::uint32_t lock = 0; lock < shape.lock_count; ++lock) {
        std::uint32_t writers = 0;
        std::uint32_t readers = 0;
        for (std::uint32_t index = 0; index < reading.count; ++index) {
            loop_end const& end = reading.ends[index];
            if (is_channel_move_(end.move) || shape.place_of[index] != lock) continue;
            LoopEndId const at{index};
            if (!is_first_side_end_(reading, shape, at)) continue;
            if (end.move == LoopMove::WriteLock) {
                ++writers;
            } else if (!writes_lock_(reading, shape, at)) {
                ++readers;
            }
        }
        if (writers != 1 || readers != 1) {
            return LoopFault{.code = LoopFaultCode::LockUnwired, .lock = lock, .writers = writers, .readers = readers};
        }
    }
    return LoopFault{};
}

// The first end inside a bracket that waits, as the fault WaitInBracket, or
// the first bracket of a writer inside a read, as the fault WriteInRead, or
// NoFault (THE LOCKS).  An end of a channel and a read of a lock wait, and a
// bracket of the writer of a lock does not.  The scan reads the ends in the
// order of the reading, and a bracket stands before each end inside it.  So
// the first write inside a read stands directly inside that read, or a wait
// inside a bracket stands before it.  Complexity: linear in the ends.
template <std::size_t Count>
[[nodiscard]] consteval LoopFault find_bracket_fault_(loop_reading<Count> const& reading,
                                                      loop_shape<Count> const& shape) noexcept {
    for (std::uint32_t index = 0; index < reading.count; ++index) {
        loop_end const& end = reading.ends[index];
        if (end.bracket == no_bracket) continue;
        auto const bracket = static_cast<std::uint32_t>(end.bracket);
        if (end.move == LoopMove::WriteLock) {
            if (reading.ends[bracket].move != LoopMove::ReadLock) continue;
            return LoopFault{.code = LoopFaultCode::WriteInRead,
                             .inside = end.move,
                             .process = end.process,
                             .lock = shape.place_of[bracket],
                             .place = shape.place_of[index]};
        }
        return LoopFault{.code = LoopFaultCode::WaitInBracket,
                         .inside = end.move,
                         .process = end.process,
                         .lock = shape.place_of[bracket],
                         .place = shape.place_of[index]};
    }
    return LoopFault{};
}

// The first end of a channel that states another capacity than the capacity
// of its channel, as the fault CapacityDisagrees, or NoFault (THE LOOP).  The
// capacity of the channel is the capacity of its first end that states one.
// Complexity: linear in the ends.
template <std::size_t Count>
[[nodiscard]] consteval LoopFault find_capacity_fault_(loop_reading<Count> const& reading,
                                                       loop_shape<Count> const& shape) noexcept {
    for (std::uint32_t index = 0; index < reading.count; ++index) {
        loop_end const& end = reading.ends[index];
        if (!is_channel_move_(end.move) || end.statement != CapacityStatement::Stated) continue;
        std::uint32_t const channel = shape.place_of[index];
        if (end.capacity == shape.capacity_of[channel]) continue;
        return LoopFault{.code = LoopFaultCode::CapacityDisagrees,
                         .inside = end.move,
                         .process = end.process,
                         .place = channel,
                         .channel_capacity = shape.capacity_of[channel],
                         .end_capacity = end.capacity};
    }
    return LoopFault{};
}

// The first fault of the ends of a loop: the wiring of the locks, the ends
// inside the brackets (THE LOCKS), and then the capacities that the ends of
// each channel state (THE LOOP).  Complexity: O(N * K) for N ends and K lock
// ends.
template <std::size_t Count>
[[nodiscard]] consteval LoopFault find_loop_fault_(loop_reading<Count> const& reading,
                                                   loop_shape<Count> const& shape) noexcept {
    LoopFault const wiring = find_lock_wiring_fault_(reading, shape);
    if (wiring.code != LoopFaultCode::NoFault) return wiring;
    LoopFault const bracket = find_bracket_fault_(reading, shape);
    return bracket.code != LoopFaultCode::NoFault ? bracket : find_capacity_fault_(reading, shape);
}

// The name of the process at a place of a loop.
template <class... Processes>
[[nodiscard]] consteval ::foundation::algebra::net::NetName
name_of_process_at_(StageLoop<Processes...>, ::foundation::algebra::net::ProcessId process) noexcept {
    ::foundation::algebra::net::NetName const names[] = {name_of_loop_process_(Processes{})...};
    return names[static_cast<std::uint32_t>(process)];
}

// The count of the actions with the move on a channel of a system.
// Complexity: linear in the actions.
template <std::size_t Processes, std::size_t Channels, std::size_t Actions>
[[nodiscard]] consteval std::uint32_t
count_moves_(::foundation::algebra::net::ProcessSystem<Processes, Channels, Actions> const& marked,
             ::foundation::algebra::net::ChannelId channel, ::foundation::algebra::net::Move move) noexcept {
    std::uint32_t count = 0;
    for (::foundation::algebra::net::Action const& action : marked.actions) {
        if (action.channel == channel && action.move == move) ++count;
    }
    return count;
}

// The marked system of a loop with an end of a channel (THE LOOP).
// Complexity: quadratic in the ends.
template <class... Processes>
[[nodiscard]] consteval auto marked_system_of_loop_(StageLoop<Processes...>) noexcept {
    namespace net = ::foundation::algebra::net;
    constexpr auto reading = read_loop_(StageLoop<Processes...>{});
    constexpr auto shape = shape_of_loop_(reading);
    if constexpr (shape.channel_count == 0) {
        static_assert(shape.channel_count != 0,
                      "a loop with no end of a channel has no marked system: its waits end at once");
    } else {
        net::ProcessSystem<sizeof...(Processes), shape.channel_count, shape.action_count> marked{};
        std::uint32_t place = 0;
        ((marked.process_names[place] = name_of_loop_process_(Processes{}), ++place), ...);
        name_channels_<LabelKind::Channel, 0, 0, shape.channel_count>(marked);
        for (std::uint32_t channel = 0; channel < shape.channel_count; ++channel)
            marked.channels[channel].capacity = shape.capacity_of[channel];
        std::size_t next = 0;
        for (std::size_t index = 0; index < reading.count; ++index) {
            loop_end const& end = reading.ends[index];
            if (!is_channel_move_(end.move)) continue;
            marked.actions[next] =
                net::Action{.process = end.process,
                            .channel = net::ChannelId{shape.place_of[index]},
                            .move = end.move == LoopMove::Send ? net::Move::Send : net::Move::Receive};
            ++next;
        }
        return marked;
    }
}

// ── The arms ────────────────────────────────────────────────────────────

// The place of an arm in the arms of a loop.
enum class ArmId : std::uint32_t {
};

// The count of the processes of a loop, and the count of the ends of its
// reading.
template <class Loop>
inline constexpr std::size_t loop_process_count_v = 0;
template <class... Processes>
inline constexpr std::size_t loop_process_count_v<StageLoop<Processes...>> = sizeof...(Processes);
template <class Loop>
inline constexpr std::size_t loop_end_total_v = 0;
template <class... Processes>
inline constexpr std::size_t loop_end_total_v<StageLoop<Processes...>> =
    (loop_process_end_count_v<Processes> + ... + 0);

// The ends of all the arms of a loop, while the check of the arms runs (THE
// ARMS).  Each end has its key, its arm and its process, which is a place in
// the processes of all the arms, in the order of the arms.  Each process has
// its arm, the key of the type of its name, and its name.  As in loop_scan,
// only this local holds the keys.
template <std::size_t Count, std::size_t Processes>
struct arms_scan {
    loop_scan<Count> ends{};
    ArmId arm_of[Count == 0 ? 1 : Count]{};
    std::uint32_t process_of[Count == 0 ? 1 : Count]{};
    ArmId arm_of_process[Processes]{};
    std::meta::info name_keys[Processes]{};
    ::foundation::algebra::net::NetName names[Processes]{};
    std::uint32_t process_count = 0;
};

// Puts the ends of one process of an arm.
template <std::size_t Count, std::size_t Processes, class Name, class... Ends>
consteval void put_arm_process_(arms_scan<Count, Processes>& scan, ArmId arm,
                                ::foundation::algebra::net::ProcessId process,
                                LoopProcess<Name, Ends...> named) noexcept {
    std::size_t const start = scan.ends.count;
    put_process_(scan.ends, process, named);
    std::uint32_t const place = scan.process_count;
    scan.arm_of_process[place] = arm;
    scan.name_keys[place] = key_of_<Name>();
    scan.names[place] = Name::process_name;
    ++scan.process_count;
    for (std::size_t index = start; index < scan.ends.count; ++index) {
        scan.arm_of[index] = arm;
        scan.process_of[index] = place;
    }
}

// Puts the ends of each process of an arm, in the order of the processes.
template <std::size_t Count, std::size_t Processes, class... Members>
consteval void put_arm_(arms_scan<Count, Processes>& scan, ArmId arm, StageLoop<Members...>) noexcept {
    std::uint32_t place = 0;
    ((put_arm_process_(scan, arm, ::foundation::algebra::net::ProcessId{place}, Members{}), ++place), ...);
}

// True when the end at index is the first end of its key and of its kind of
// key (a channel or a lock) in its arm.  Complexity: linear in the ends.
template <std::size_t Count, std::size_t Processes>
[[nodiscard]] consteval bool is_first_in_arm_(arms_scan<Count, Processes> const& scan, LoopEndId index) noexcept {
    auto const at = static_cast<std::uint32_t>(index);
    bool const is_channel = is_channel_move_(scan.ends.ends[at].move);
    for (std::uint32_t earlier = 0; earlier < at; ++earlier) {
        if (scan.arm_of[earlier] == scan.arm_of[at] && scan.ends.keys[earlier] == scan.ends.keys[at]
            && is_channel_move_(scan.ends.ends[earlier].move) == is_channel)
            return false;
    }
    return true;
}

// The place of the channel or the lock of the end at index in its arm: the
// count of the channels or the locks whose first end in the arm stands before
// the first end of this one, as the names 'channel 0' and 'lock 0' of the
// arm count them.  Complexity: quadratic in the ends.
template <std::size_t Count, std::size_t Processes>
[[nodiscard]] consteval std::uint32_t place_in_arm_(arms_scan<Count, Processes> const& scan, LoopEndId index) noexcept {
    auto const at = static_cast<std::uint32_t>(index);
    bool const is_channel = is_channel_move_(scan.ends.ends[at].move);
    std::uint32_t place = 0;
    for (std::uint32_t earlier = 0; earlier < at; ++earlier) {
        if (scan.arm_of[earlier] != scan.arm_of[at] || is_channel_move_(scan.ends.ends[earlier].move) != is_channel)
            continue;
        if (scan.ends.keys[earlier] == scan.ends.keys[at]) return place;
        if (is_first_in_arm_(scan, LoopEndId{earlier})) ++place;
    }
    return place;
}

// True when the process of the end at index writes the lock of that end in
// its arm.  Complexity: linear in the ends.
template <std::size_t Count, std::size_t Processes>
[[nodiscard]] consteval bool writes_own_lock_(arms_scan<Count, Processes> const& scan, LoopEndId index) noexcept {
    auto const at = static_cast<std::uint32_t>(index);
    for (std::uint32_t other = 0; other < scan.ends.count; ++other) {
        if (scan.ends.ends[other].move == LoopMove::WriteLock && scan.process_of[other] == scan.process_of[at]
            && scan.ends.keys[other] == scan.ends.keys[at])
            return true;
    }
    return false;
}

// The rule that the arms of a loop break (THE ARMS).
enum class ArmsFaultCode : std::uint8_t {
    NoFault = 0,
    // The fault names the arm and the name of its two processes.
    NameTwice = 1,
    // The fault names the move, the channel in the first arm that holds it,
    // the two arms, and the two processes that hold that side.
    SideDisagrees = 2,
    // The fault names the channel in the first arm that states its capacity,
    // the two arms, and the two capacities.
    CapacityDisagrees = 3,
    // The fault names the move (a write or a read), the lock in the first arm
    // that holds it, the two arms, and the two processes.
    LockSideDisagrees = 4,
};

// The fault of the arms of a loop.  The first arm, the first name and the
// channel capacity are of the earlier end, and the arm, the name and the end
// capacity are of the end that disagrees with it.
struct ArmsFault {
    ArmsFaultCode code = ArmsFaultCode::NoFault;
    LoopMove move = LoopMove::Send;
    std::uint8_t reserved[2]{};
    ArmId first_arm{};
    ArmId arm{};
    std::uint32_t place = 0;
    std::uint32_t channel_capacity = 0;
    std::uint32_t end_capacity = 0;
    ::foundation::algebra::net::NetName first_name{};
    ::foundation::algebra::net::NetName name{};
};

// The first earlier end of the channel of the end at index that disagrees
// with it, as a fault, or NoFault: a send or a receive of another process,
// or a capacity that another arm states (THE ARMS).  Complexity: linear in
// the ends.
template <std::size_t Count, std::size_t Processes>
[[nodiscard]] consteval ArmsFault find_channel_disagreement_(arms_scan<Count, Processes> const& scan,
                                                             LoopEndId index) noexcept {
    auto const at = static_cast<std::uint32_t>(index);
    loop_end const& end = scan.ends.ends[at];
    for (std::uint32_t earlier = 0; earlier < at; ++earlier) {
        loop_end const& other = scan.ends.ends[earlier];
        if (!is_channel_move_(other.move) || scan.ends.keys[earlier] != scan.ends.keys[at]) continue;
        bool const is_other_side =
            other.move == end.move && scan.name_keys[scan.process_of[earlier]] != scan.name_keys[scan.process_of[at]];
        bool const is_other_capacity = other.statement == CapacityStatement::Stated
                                    && end.statement == CapacityStatement::Stated && other.capacity != end.capacity;
        if (!is_other_side && !is_other_capacity) continue;
        return ArmsFault{.code = is_other_side ? ArmsFaultCode::SideDisagrees : ArmsFaultCode::CapacityDisagrees,
                         .move = end.move,
                         .first_arm = scan.arm_of[earlier],
                         .arm = scan.arm_of[at],
                         .place = place_in_arm_(scan, LoopEndId{earlier}),
                         .channel_capacity = other.capacity,
                         .end_capacity = end.capacity,
                         .first_name = scan.names[scan.process_of[earlier]],
                         .name = scan.names[scan.process_of[at]]};
    }
    return ArmsFault{};
}

// The first earlier end of the lock of the end at index that holds the same
// side in another process, as the fault LockSideDisagrees, or NoFault (THE
// ARMS).  A read by a process that writes the lock holds no side.
// Complexity: quadratic in the ends.
template <std::size_t Count, std::size_t Processes>
[[nodiscard]] consteval ArmsFault find_lock_disagreement_(arms_scan<Count, Processes> const& scan,
                                                          LoopEndId index) noexcept {
    auto const at = static_cast<std::uint32_t>(index);
    loop_end const& end = scan.ends.ends[at];
    if (end.move == LoopMove::ReadLock && writes_own_lock_(scan, index)) return ArmsFault{};
    for (std::uint32_t earlier = 0; earlier < at; ++earlier) {
        loop_end const& other = scan.ends.ends[earlier];
        if (other.move != end.move || scan.ends.keys[earlier] != scan.ends.keys[at]) continue;
        if (other.move == LoopMove::ReadLock && writes_own_lock_(scan, LoopEndId{earlier})) continue;
        if (scan.name_keys[scan.process_of[earlier]] == scan.name_keys[scan.process_of[at]]) return ArmsFault{};
        return ArmsFault{.code = ArmsFaultCode::LockSideDisagrees,
                         .move = end.move,
                         .first_arm = scan.arm_of[earlier],
                         .arm = scan.arm_of[at],
                         .place = place_in_arm_(scan, LoopEndId{earlier}),
                         .first_name = scan.names[scan.process_of[earlier]],
                         .name = scan.names[scan.process_of[at]]};
    }
    return ArmsFault{};
}

// The first fault of the arms of a loop (THE ARMS): two processes of one arm
// with one name, and then the first end that disagrees with an earlier end of
// its channel or its lock.  Complexity: quadratic in the ends and in the
// processes of all the arms.
template <class... Arms>
[[nodiscard]] consteval ArmsFault find_arms_fault_() noexcept {
    constexpr std::size_t count = (loop_end_total_v<Arms> + ... + 0);
    constexpr std::size_t processes = (loop_process_count_v<Arms> + ... + 0);
    arms_scan<count, processes> scan{};
    std::uint32_t arm = 0;
    ((put_arm_(scan, ArmId{arm}, Arms{}), ++arm), ...);
    for (std::uint32_t process = 0; process < scan.process_count; ++process) {
        for (std::uint32_t earlier = 0; earlier < process; ++earlier) {
            if (scan.arm_of_process[earlier] != scan.arm_of_process[process]
                || scan.name_keys[earlier] != scan.name_keys[process])
                continue;
            return ArmsFault{.code = ArmsFaultCode::NameTwice,
                             .first_arm = scan.arm_of_process[process],
                             .arm = scan.arm_of_process[process],
                             .first_name = scan.names[process],
                             .name = scan.names[process]};
        }
    }
    for (std::uint32_t index = 0; index < scan.ends.count; ++index) {
        LoopEndId const at{index};
        ArmsFault const fault = is_channel_move_(scan.ends.ends[index].move) ? find_channel_disagreement_(scan, at)
                                                                             : find_lock_disagreement_(scan, at);
        if (fault.code != ArmsFaultCode::NoFault) return fault;
    }
    return ArmsFault{};
}

}  // namespace detail

// True when the type is a loop (THE LOOP): a StageLoop of one process or
// more, each a LoopProcess whose name type states its name and whose ends are
// each an end of a loop.
template <class Loop>
concept IsStageLoop = detail::is_stage_loop_v<Loop>;

// The marked system of a loop (THE LOOP), for a loop with an end of a
// channel.  Complexity: quadratic in the ends.
template <class Loop>
    requires IsStageLoop<Loop>
[[nodiscard]] consteval auto marked_system_of_loop() noexcept {
    return detail::marked_system_of_loop_(Loop{});
}

// The verdict on a loop (THE LOOP): the refusal of its first lock that one
// process does not write or that one other process does not read, of its
// first wait inside a bracket, or of its first bracket of a writer inside a
// read (THE LOCKS).  Then the refusal of the first end of a channel that
// states another capacity than an earlier end of that channel, the refusal of
// a channel whose period does not give back its marking (THE ARMS), the
// verdict of check_liveness on its marked system, or the admitted verdict for
// a loop with no end of a channel.  The marked graph finds each channel whose
// sends and receives differ in count before it looks for a cycle, so the
// verdict of that fault of the marked graph is the refusal of the marking.
// Complexity: quadratic in the ends.
template <class Loop>
    requires IsStageLoop<Loop>
[[nodiscard]] consteval ::foundation::algebra::net::NetVerdict check_loop() noexcept {
    namespace net = ::foundation::algebra::net;
    using detail::LabelKind;
    using detail::LoopFaultCode;
    constexpr auto reading = detail::read_loop_(Loop{});
    constexpr auto shape = detail::shape_of_loop_(reading);
    constexpr detail::LoopFault fault = detail::find_loop_fault_(reading, shape);
    if constexpr (fault.code == LoopFaultCode::LockUnwired) {
        return net::NetVerdict::refuse("loop: the processes that write the lock '")
            .put_name(net::NetName{detail::index_label_v<LabelKind::Lock, fault.lock>.text})
            .put("': ")
            .put_number(fault.writers)
            .put(", the processes that read it and do not write it: ")
            .put_number(fault.readers)
            .put(".  One process writes each lock, and one other process reads it.");
    } else if constexpr (fault.code == LoopFaultCode::WaitInBracket) {
        constexpr bool is_lock_read = fault.inside == detail::LoopMove::ReadLock;
        return net::NetVerdict::refuse("loop: '")
            .put_name(detail::name_of_process_at_(Loop{}, fault.process))
            .put("' waits on '")
            .put_name(is_lock_read ? net::NetName{detail::index_label_v<LabelKind::Lock, fault.place>.text}
                                   : net::NetName{detail::index_label_v<LabelKind::Channel, fault.place>.text})
            .put("' before it leaves its bracket of '")
            .put_name(net::NetName{detail::index_label_v<LabelKind::Lock, fault.lock>.text})
            .put("'.  A process that writes or reads a lock waits on nothing before it leaves the bracket.");
    } else if constexpr (fault.code == LoopFaultCode::WriteInRead) {
        return net::NetVerdict::refuse("loop: '")
            .put_name(detail::name_of_process_at_(Loop{}, fault.process))
            .put("' writes '")
            .put_name(net::NetName{detail::index_label_v<LabelKind::Lock, fault.place>.text})
            .put("' inside its read of '")
            .put_name(net::NetName{detail::index_label_v<LabelKind::Lock, fault.lock>.text})
            .put("'.  A read of a sequence lock can run again, and then each write inside it runs two times.");
    } else if constexpr (fault.code == LoopFaultCode::CapacityDisagrees) {
        return net::NetVerdict::refuse("loop: the ends of '")
            .put_name(net::NetName{detail::index_label_v<LabelKind::Channel, fault.place>.text})
            .put("' state two capacities: ")
            .put_number(fault.channel_capacity)
            .put(" and ")
            .put_number(fault.end_capacity)
            .put(".  Each end that states the capacity of a channel states the same capacity.");
    } else if constexpr (shape.channel_count == 0) {
        return net::NetVerdict{};
    } else {
        constexpr auto marked = detail::marked_system_of_loop_(Loop{});
        constexpr net::LivenessFault liveness = net::find_liveness_fault(marked);
        if constexpr (liveness.code == net::LivenessFaultCode::SendsWithoutReceives) {
            return net::NetVerdict::refuse("loop: one period changes the count of the messages in '")
                .put_name(marked.channels[static_cast<std::uint32_t>(liveness.channel)].name)
                .put("': ")
                .put_number(detail::count_moves_(marked, liveness.channel, net::Move::Send))
                .put(" sends and ")
                .put_number(detail::count_moves_(marked, liveness.channel, net::Move::Receive))
                .put(" receives.  Each period gives back the marking that it starts from, so each sequence of arms "
                     "stays live.");
        } else {
            return net::explain_liveness_fault(marked, liveness);
        }
    }
}

// The door: a loop whose locks are not wired, whose process waits inside a
// bracket, whose period changes the count of the messages in a channel, or
// whose marked system holds a cycle of waits with no token, does not compile.
// One static_assert fires, with the text of the verdict.
template <class Loop>
    requires IsStageLoop<Loop>
consteval void ensure_loop_live() noexcept {
    constexpr ::foundation::algebra::net::NetVerdict verdict = check_loop<Loop>();
    static_assert(verdict.is_admitted(), verdict);
}

namespace detail {

// The place of the first arm that check_loop refuses, or the count of the
// arms when it admits each arm.
template <class... Arms>
[[nodiscard]] consteval std::size_t first_refused_arm_() noexcept {
    bool const is_admitted[] = {check_loop<Arms>().is_admitted()...};
    for (std::size_t arm = 0; arm < sizeof...(Arms); ++arm) {
        if (!is_admitted[arm]) return arm;
    }
    return sizeof...(Arms);
}

}  // namespace detail

// The verdict on the arms of a loop (THE ARMS): the verdict of check_loop on
// the first arm that it refuses, after the place of the arm.  Then the
// refusal of an arm with two processes of one name, of a channel whose
// sends or whose receives stand in two processes, of a channel whose ends
// state two capacities, or of a lock whose writes or whose reads stand in two
// processes, across the arms.  The arms name their processes by the type of
// the name, and their channels and locks by type.  Complexity: quadratic in
// the ends of each arm, and quadratic in the ends of all the arms.
template <class... Arms>
    requires(sizeof...(Arms) != 0 && (IsStageLoop<Arms> && ...))
[[nodiscard]] consteval ::foundation::algebra::net::NetVerdict check_arms() noexcept {
    namespace net = ::foundation::algebra::net;
    using detail::ArmsFaultCode;
    using detail::LabelKind;
    constexpr std::size_t refused = detail::first_refused_arm_<Arms...>();
    if constexpr (refused < sizeof...(Arms)) {
        constexpr net::NetVerdict verdict = check_loop<Arms...[refused]>();
        return net::NetVerdict::refuse("arms: arm ").put_number(refused).put(": ").put(verdict.data());
    } else {
        constexpr detail::ArmsFault fault = detail::find_arms_fault_<Arms...>();
        constexpr auto first_arm = static_cast<std::uint32_t>(fault.first_arm);
        constexpr auto arm = static_cast<std::uint32_t>(fault.arm);
        if constexpr (fault.code == ArmsFaultCode::NameTwice) {
            return net::NetVerdict::refuse("arms: arm ")
                .put_number(arm)
                .put(" holds two processes with the name '")
                .put_name(fault.name)
                .put("'.  The arms compare their processes by the type of the name, so each process of an arm has a "
                     "name of its own.");
        } else if constexpr (fault.code == ArmsFaultCode::SideDisagrees) {
            constexpr bool is_send = fault.move == detail::LoopMove::Send;
            return net::NetVerdict::refuse("arms: in arm ")
                .put_number(first_arm)
                .put(" '")
                .put_name(fault.first_name)
                .put(is_send ? "' sends into '" : "' receives from '")
                .put_name(net::NetName{detail::index_label_v<LabelKind::Channel, fault.place>.text})
                .put("', and in arm ")
                .put_number(arm)
                .put(" '")
                .put_name(fault.name)
                .put(is_send ? "' sends into it." : "' receives from it.")
                .put("  The arms agree on each channel: one process sends into it, and one process receives from it.");
        } else if constexpr (fault.code == ArmsFaultCode::CapacityDisagrees) {
            return net::NetVerdict::refuse("arms: in arm ")
                .put_number(first_arm)
                .put(" an end of '")
                .put_name(net::NetName{detail::index_label_v<LabelKind::Channel, fault.place>.text})
                .put("' states the capacity ")
                .put_number(fault.channel_capacity)
                .put(", and in arm ")
                .put_number(arm)
                .put(" an end of it states ")
                .put_number(fault.end_capacity)
                .put(".  The arms agree on the capacity of each channel.");
        } else if constexpr (fault.code == ArmsFaultCode::LockSideDisagrees) {
            constexpr bool is_write = fault.move == detail::LoopMove::WriteLock;
            return net::NetVerdict::refuse("arms: in arm ")
                .put_number(first_arm)
                .put(" '")
                .put_name(fault.first_name)
                .put(is_write ? "' writes '" : "' reads '")
                .put_name(net::NetName{detail::index_label_v<LabelKind::Lock, fault.place>.text})
                .put("', and in arm ")
                .put_number(arm)
                .put(" '")
                .put_name(fault.name)
                .put(is_write ? "' writes it." : "' reads it.")
                .put("  The arms agree on the writer and the reader of each lock.");
        } else {
            return net::NetVerdict{};
        }
    }
}

// The door over the arms of a loop: an arm that the door of a loop refuses,
// or arms that disagree on a name, a side or a capacity, do not compile (THE
// ARMS).  One static_assert fires, with the text of the verdict.
template <class... Arms>
    requires(sizeof...(Arms) != 0 && (IsStageLoop<Arms> && ...))
consteval void ensure_arms_live() noexcept {
    constexpr ::foundation::algebra::net::NetVerdict verdict = check_arms<Arms...>();
    static_assert(verdict.is_admitted(), verdict);
}

}  // namespace fixy::concurrent
