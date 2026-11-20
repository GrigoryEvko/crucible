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
//   through the pipeline and back to the foreground.
//
// THE COSTS
//   One constant evaluation for each graph, linear in the stages, the ports
//   and the edges.

#include <fixy/concurrent/Pipeline.h>
#include <foundation/algebra/net/Net.h>
#include <foundation/algebra/net/Priority.h>

#include <cstddef>
#include <cstdint>

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
// system of a graph.
enum class LabelKind : std::uint8_t {
    Stage = 0,
    Edge = 1,
    Input = 2,
    Output = 3,
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
    char const* const word = Kind == LabelKind::Stage ? "stage "
                           : Kind == LabelKind::Edge  ? "edge "
                           : Kind == LabelKind::Input ? "input "
                                                      : "output ";
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

}  // namespace fixy::concurrent
