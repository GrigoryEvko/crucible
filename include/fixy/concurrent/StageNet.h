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
// THE COSTS
//   One constant evaluation for each graph, linear in the stages, the ports
//   and the edges.

#include <fixy/concurrent/Pipeline.h>
#include <foundation/algebra/net/Net.h>

#include <cstddef>
#include <cstdint>

namespace fixy::concurrent {

namespace detail {

// The net of a stage graph whose gate holds (THE NET).  The bound of the
// ports is a check in the body and no gate, so the header adds no gate that
// reads the traits of a stage.  Complexity: linear in the stages, the ports
// and the edges.
template <class... Stages, class... Edges>
[[nodiscard]] consteval auto net_of_stage_graph_(StagePack<Stages...>, EdgePack<Edges...>) noexcept {
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
    constexpr std::size_t channels = edge_count + input_ends + output_ends;
    constexpr std::size_t agents = stage_count + input_ends + output_ends;
    net::Net<channels, agents, agents, channels> graph{};

    // The channel of each port of each stage: the edge of the port, or the
    // channel of its end.
    std::size_t channel_of[stage_count * ports]{};
    std::size_t edge = 0;
    ((channel_of[Edges::from * ports + inputs[Edges::from] + Edges::from_output] = edge,
      channel_of[Edges::to * ports + Edges::to_input] = edge, ++edge),
     ...);
    for (std::size_t end = 0; end < input_ends; ++end)
        channel_of[end] = edge_count + end;
    for (std::size_t end = 0; end < output_ends; ++end)
        channel_of[last * ports + inputs[last] + end] = edge_count + input_ends + end;

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
            kind.port_types[port] = net::take(type_at(channel_of[stage * ports + port]));
        }
        for (std::size_t port = inputs[stage]; port < inputs[stage] + outputs[stage]; ++port) {
            kind.port_names[port] = net::NetName{"out"};
            kind.port_types[port] = net::give(type_at(channel_of[stage * ports + port]));
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

}  // namespace detail

// The net of a stage graph (THE NET), for a graph whose gate holds and whose
// stages each have at most eight ports.
template <class Graph>
    requires StageGraphWellFormed<Graph>
[[nodiscard]] consteval auto net_of_stage_graph() noexcept {
    using traits = detail::stage_graph_traits<Graph>;
    return detail::net_of_stage_graph_(typename traits::stage_pack_type{}, typename traits::edge_pack_type{});
}

}  // namespace fixy::concurrent
