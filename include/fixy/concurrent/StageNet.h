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
//     WritesLock<Lock>      the bracket of the writer of a sequence lock
//     ReadsLock<Lock>       one read of a sequence lock, which waits while a
//                           bracket of the writer is open
//   A channel is a type: the type that SendsOn or ReceivesOn names, or the
//   channel_type of a handle (fixy/concurrent/HandleTraits.h).  Two ends that
//   name one type are two sides of one channel, so an end that names another
//   type names another channel.  The type tells two channels apart, and the
//   identity of an instance does not.  The channels stand in the order of
//   their first ends, with the names 'channel 0' and on.  The capacity of a
//   channel is the channel_capacity that the type of its first send end
//   states (StatesHandleCapacity): the type of SendsOn, or the output handle
//   of OutputsOf.  A type that states none reads as a rendezvous, by the rule
//   of THE MARKED SYSTEM.  A lock is a type too, and the locks stand in the
//   order of their first ends, with the names 'lock 0' and on.
//
//   marked_system_of_loop gives the processes in the order of the loop, each
//   with the actions of its channel ends in the order of its ends.  A lock
//   end adds no action, because the check of the marked graph reads no
//   bracket (THE LIMITS of foundation/algebra/net/Priority.h).  So the door
//   ensure_loop_live checks the wiring of each lock: one process writes it,
//   and one other process reads it.  Then it checks the liveness of the
//   marked system.  The door reads no wait inside a bracket.
//
//   A process that polls a channel and goes on when the channel is empty
//   does not wait on it.  At each poll, such a process takes one of two arms,
//   and a caller checks one loop for each arm.
//
// THE COSTS
//   One constant evaluation for each graph, linear in the stages, the ports
//   and the edges.  The reading of a loop is quadratic in its ends, and its
//   check is linear in the ends.

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

// The bracket of the writer of a sequence lock.
template <class Lock>
struct WritesLock {};

// One read of a sequence lock, which waits while a bracket of the writer is
// open.
template <class Lock>
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
template <class Lock>
inline constexpr bool is_loop_end_v<WritesLock<Lock>> = true;
template <class Lock>
inline constexpr bool is_loop_end_v<ReadsLock<Lock>> = true;

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

// One end of a loop, as the reading holds it: the process, the first end of
// its channel or of its lock, its kind, and the capacity that the type of the
// end states.
struct loop_end {
    ::foundation::algebra::net::ProcessId process{};
    LoopEndId first{};
    LoopMove move = LoopMove::Send;
    std::uint32_t capacity = 0;
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
// Port on.
template <class Stage, std::size_t Port, std::size_t Count>
consteval void put_stage_inputs_(loop_scan<Count>& scan, ::foundation::algebra::net::ProcessId process) noexcept {
    if constexpr (Port < stage_input_count_v<Stage>) {
        using handle = stage_input_handle_t<Stage, Port>;
        if constexpr (!NamesItsChannel<handle>) {
            static_assert(NamesItsChannel<handle>,
                          "a loop reads an input handle that names no channel: each handle of a loop states its "
                          "channel_type");
        } else {
            put_loop_end_(
                scan,
                loop_end{.process = process, .move = LoopMove::Receive, .capacity = capacity_of_handle_<handle>()},
                key_of_<handle_channel_t<handle>>());
            put_stage_inputs_<Stage, Port + 1>(scan, process);
        }
    }
}

// Puts the send into the channel of each output handle of the stage, from
// Port on.
template <class Stage, std::size_t Port, std::size_t Count>
consteval void put_stage_outputs_(loop_scan<Count>& scan, ::foundation::algebra::net::ProcessId process) noexcept {
    if constexpr (Port < stage_output_count_v<Stage>) {
        using handle = stage_output_handle_t<Stage, Port>;
        if constexpr (!NamesItsChannel<handle>) {
            static_assert(NamesItsChannel<handle>,
                          "a loop reads an output handle that names no channel: each handle of a loop states its "
                          "channel_type");
        } else {
            put_loop_end_(
                scan, loop_end{.process = process, .move = LoopMove::Send, .capacity = capacity_of_handle_<handle>()},
                key_of_<handle_channel_t<handle>>());
            put_stage_outputs_<Stage, Port + 1>(scan, process);
        }
    }
}

// Puts the ends that one end of a process gives.
template <std::size_t Count, class Channel>
consteval void put_end_(loop_scan<Count>& scan, ::foundation::algebra::net::ProcessId process,
                        SendsOn<Channel>) noexcept {
    put_loop_end_(scan,
                  loop_end{.process = process, .move = LoopMove::Send, .capacity = capacity_of_handle_<Channel>()},
                  key_of_<Channel>());
}
template <std::size_t Count, class Channel>
consteval void put_end_(loop_scan<Count>& scan, ::foundation::algebra::net::ProcessId process,
                        ReceivesOn<Channel>) noexcept {
    put_loop_end_(scan,
                  loop_end{.process = process, .move = LoopMove::Receive, .capacity = capacity_of_handle_<Channel>()},
                  key_of_<Channel>());
}
template <std::size_t Count, class Stage>
consteval void put_end_(loop_scan<Count>& scan, ::foundation::algebra::net::ProcessId process,
                        InputsOf<Stage>) noexcept {
    put_stage_inputs_<Stage, 0>(scan, process);
}
template <std::size_t Count, class Stage>
consteval void put_end_(loop_scan<Count>& scan, ::foundation::algebra::net::ProcessId process,
                        OutputsOf<Stage>) noexcept {
    put_stage_outputs_<Stage, 0>(scan, process);
}
template <std::size_t Count, class Lock>
consteval void put_end_(loop_scan<Count>& scan, ::foundation::algebra::net::ProcessId process,
                        WritesLock<Lock>) noexcept {
    put_loop_end_(scan, loop_end{.process = process, .move = LoopMove::WriteLock}, key_of_<Lock>());
}
template <std::size_t Count, class Lock>
consteval void put_end_(loop_scan<Count>& scan, ::foundation::algebra::net::ProcessId process,
                        ReadsLock<Lock>) noexcept {
    put_loop_end_(scan, loop_end{.process = process, .move = LoopMove::ReadLock}, key_of_<Lock>());
}

// Puts the ends of a process, in the order of its ends.
template <std::size_t Count, class Name, class... Ends>
consteval void put_process_(loop_scan<Count>& scan, ::foundation::algebra::net::ProcessId process,
                            LoopProcess<Name, Ends...>) noexcept {
    (put_end_(scan, process, Ends{}), ...);
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

// The shape of a reading.  The capacity of a channel is the capacity of its
// first send end.  A channel with no send end keeps the capacity of its first
// end, and the liveness check refuses that channel.  Complexity: linear in
// the ends.
template <std::size_t Count>
[[nodiscard]] consteval loop_shape<Count> shape_of_loop_(loop_reading<Count> const& reading) noexcept {
    loop_shape<Count> shape{};
    bool has_send[Count == 0 ? 1 : Count]{};
    for (std::size_t index = 0; index < reading.count; ++index) {
        loop_end const& end = reading.ends[index];
        bool const is_channel = is_channel_move_(end.move);
        auto const first = static_cast<std::size_t>(end.first);
        if (first != index) {
            shape.place_of[index] = shape.place_of[first];
        } else if (is_channel) {
            shape.place_of[index] = shape.channel_count;
            shape.capacity_of[shape.channel_count] = end.capacity;
            ++shape.channel_count;
        } else {
            shape.place_of[index] = shape.lock_count;
            ++shape.lock_count;
        }
        if (!is_channel) continue;
        ++shape.action_count;
        std::uint32_t const channel = shape.place_of[index];
        if (end.move == LoopMove::Send && !has_send[channel]) {
            has_send[channel] = true;
            shape.capacity_of[channel] = end.capacity;
        }
    }
    return shape;
}

// The rule that the locks of a loop break.
enum class LoopFaultCode : std::uint8_t {
    NoFault = 0,
    // The fault names the lock, the count of the processes that write it,
    // and the count of the processes that read it and do not write it.
    LockUnwired = 1,
};

// The fault of the locks of a loop.
struct LoopFault {
    LoopFaultCode code = LoopFaultCode::NoFault;
    std::uint32_t lock = 0;
    std::uint32_t writers = 0;
    std::uint32_t readers = 0;
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
// does not read, as the fault LockUnwired, or NoFault (THE LOOP).
// Complexity: quadratic in the ends for each lock.
template <std::size_t Count>
[[nodiscard]] consteval LoopFault find_lock_fault_(loop_reading<Count> const& reading,
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
// process does not write or that one other process does not read, the
// verdict of check_liveness on its marked system, or the admitted verdict for
// a loop with no end of a channel.  Complexity: quadratic in the ends.
template <class Loop>
    requires IsStageLoop<Loop>
[[nodiscard]] consteval ::foundation::algebra::net::NetVerdict check_loop() noexcept {
    namespace net = ::foundation::algebra::net;
    constexpr auto reading = detail::read_loop_(Loop{});
    constexpr auto shape = detail::shape_of_loop_(reading);
    constexpr detail::LoopFault fault = detail::find_lock_fault_(reading, shape);
    if constexpr (fault.code != detail::LoopFaultCode::NoFault) {
        return net::NetVerdict::refuse("loop: the processes that write the lock '")
            .put_name(net::NetName{detail::index_label_v<detail::LabelKind::Lock, fault.lock>.text})
            .put("': ")
            .put_number(fault.writers)
            .put(", the processes that read it and do not write it: ")
            .put_number(fault.readers)
            .put(".  One process writes each lock, and one other process reads it.");
    } else if constexpr (shape.channel_count == 0) {
        return net::NetVerdict{};
    } else {
        return net::check_liveness(detail::marked_system_of_loop_(Loop{}));
    }
}

// The door: a loop whose locks are not wired, or whose marked system holds a
// cycle of waits with no token, does not compile.  One static_assert fires,
// with the text of the verdict.
template <class Loop>
    requires IsStageLoop<Loop>
consteval void ensure_loop_live() noexcept {
    constexpr ::foundation::algebra::net::NetVerdict verdict = check_loop<Loop>();
    static_assert(verdict.is_admitted(), verdict);
}

}  // namespace fixy::concurrent
