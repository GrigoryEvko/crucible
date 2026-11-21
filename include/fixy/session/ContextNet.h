#pragma once

// The marked system of a typing context (foundation/algebra/net/Priority.h).
// Each role of a context is a process, and the check of the marked graph
// decides whether the waits of the roles can end, across all the sessions
// that the roles play.
//
//   ContextFault        the fault that leaves a context with no marked
//                       system: a code, the role and the type that it names
//   find_context_fault<Context>()
//                       the first fault of the context, or NoFault
//   marked_system_of_context<Context, Channel>()
//                       the marked system of a context with no fault
//   check_context_deadlock_freedom<Context, Channel>()
//                       the verdict: admitted, or a refusal with one line
//   ensure_context_deadlock_free<Context, Channel>()
//                       the door: a context that the check refuses does not
//                       compile
//
// WHY
//   Liveness by construction (fixy/session/Liveness.h) covers one session.
//   Two roles that each play two live sessions, and wait on them in opposite
//   orders, deadlock.  The marked graph reads the order of the waits of each
//   role, so it refuses that context, and it admits the same sessions in one
//   order.
//
// THE SYSTEM
//   Each role of the context is a process, in the order of the context, with
//   the name of the role.  Each ordered pair of roles that a message joins is
//   one channel: the queue from the sender to the receiver, as in the typing
//   contexts of fixy/session/Projection.h.  The channels stand in the order
//   of the sender, then in the order of the receiver, each in the order of
//   the context.  The name of a channel is 'P to Q'.  Each channel has the
//   capacity that the type Channel states (StatesChannelCapacity of
//   fixy/session/Subtype.h): the channel type of the transport, and never a
//   number of the caller.
//
//   The actions of a role are the steps of its local type, in order.  A Send
//   of a PeerMsg sends into the channel from the role to the peer of the
//   message.  A Recv of a PeerMsg receives from the channel from the peer to
//   the role.  The walk reads the type graph of fixy/session/Refinement.h, so
//   a named state reads as its protocol, and a keyed step reads as its step.
//
// THE MODEL OF THE CHECK
//   The check reads a local type as a sequence of Send and Recv steps of
//   PeerMsg messages, and the queue of each role is empty.  The sequence
//   runs one time to End, or it is the body of one Loop around the whole
//   local type, which reaches its Continue.  The body of such a loop is one
//   period of the role (THE MODEL of foundation/algebra/net/Priority.h).  A
//   Loop whose body reaches End and no Continue runs one time.
//
//   A role that loops for ever and a role that runs one time do not fit one
//   system: the counts of one period can agree, and the role that loops
//   then waits for ever on the role that ended.  So along each channel the
//   two roles loop, or the two run one time.  A context of roles that loop
//   is live when each period of the marked graph can start, and a context of
//   roles that run one time is live when each action can start.
//
//   Each other node is outside the model, and the context then has no marked
//   system: a Loop below the top of a local type, a loop with no action in
//   its period, a choice whose arms wait differently (THE UNION OF THE ARMS),
//   a step whose payload names no peer, a wrapper, a marker and a terminal
//   other than End.  find_context_fault names the first such node.
//
// THE UNION OF THE ARMS
//   A Select or an Offer of two or more branches is the union of its arms.
//   The walk follows each arm at the same time.  At each step, each arm sends
//   or receives on one channel with one move, or each arm reaches End, or
//   each arm reaches the Continue of the loop.  Then each run of the role
//   makes the same waits, whatever branch it takes, so one sequence of
//   actions stands for the role, and the labels change no wait.  The
//   projections of the ring of three roles meet this rule, with two labels
//   on each channel.  A choice whose arms wait differently, such as a loop
//   with an exit, needs one context for each set of arms that the roles take
//   together.
//
// THE LIMITS
//   The check reads the waits, not the labels and not the payloads.  The
//   safety of each message, whose label and payload must fit the receive, is
//   the job of the association of fixy/session/Projection.h.  A payload that
//   hands over an endpoint of another session adds the waits of that session,
//   which this context does not hold.  No mint calls the door.  A program
//   that composes sessions on handles is checked only by the watch of
//   fixy/session/Watch.h, at run time, on the run that waits.  A context
//   holds at most 32 roles, 256 actions and 64 channels (THE COSTS).
//
// THE COSTS
//   One walk of the type graph of each local type: linear in its distinct
//   nodes with no choice of two or more branches, and O(N^2) for N distinct
//   nodes at most with such choices, because each round of the walk reads
//   each node at most one time.  The channels cost O(R^2 + A) for R roles
//   and A actions, and the check of the system costs O(R + C + A).  A unit
//   reads each context one time (reading_of_), and each pair of a context
//   and a channel type keeps its verdict in the instance of the door.  A
//   name of a role or of a channel is a string in static storage, and each
//   new text of such a string costs a constant of its own.  So the door and
//   check_context_deadlock_freedom check a system whose processes and
//   channels have plain names, and they make the names of the roles only for
//   the text of a refusal.  marked_system_of_context gives the names of the
//   roles.
//
//   A reading holds fixed arrays of a stated bound: at most 32 roles, 256
//   actions and 64 channels in one context, about 5 KB.  Each reading has
//   one type, so each reader of a reading has one instance, and a context
//   past the bound has the fault PastTheBound.  A run in static storage
//   (define_static_array, reflect_constant_array) costs a constant of its
//   own for each new element type and each new content, and the first runs
//   of a reading in a unit cost about 200 M instructions.  So a reading
//   keeps no run in static storage: the template arguments stay transient
//   lists, and the reading looks up the payload rule of a shape one time for
//   each run of steps of that shape.

#include <fixy/session/Projection.h>
#include <foundation/algebra/net/Priority.h>
#include <foundation/reflect/Anchor.h>

#include <cstddef>
#include <cstdint>
#include <meta>

namespace fixy::session {

// ── The fault ───────────────────────────────────────────────────────────

// The rule that leaves a context with no marked system.  Each code has one
// text, which check_context_deadlock_freedom gives.
enum class ContextFaultCode : std::uint8_t {
    NoFault = 0,
    // The fault names the type that is no TypingContext, or the entry of
    // the context that is no RoleState.
    NotAContext = 1,
    // The fault names the role, and the place of its second entry.
    RoleTwice = 2,
    // The fault names the role.
    QueuedMessage = 3,
    // The fault names the role, and the type of the node.
    NodeOutsideModel = 4,
    // The fault names the role, and the peer.
    PeerOutsideContext = 5,
    // The fault names the role, and the type of its loop.
    LoopNotWhole = 6,
    // The fault names the role that loops, and the role that runs one time.
    LoopMeetsRun = 7,
    // The fault names the role, and the type of its first choice of two or
    // more branches.
    ArmsWaitDifferently = 8,
    // The fault names the role whose entry, action or channel passes the
    // bound of a reading (THE COSTS), and the type of the role.
    PastTheBound = 9,
};

// The fault that find_context_fault finds.  The code tells which fields it
// sets.  A reflection is a field, so the fault is a value of a constant
// evaluation only.
struct ContextFault {
    ContextFaultCode code = ContextFaultCode::NoFault;
    ::foundation::algebra::net::ProcessId role{};
    std::meta::info type{};

    [[nodiscard]] consteval bool is_fault() const noexcept { return code != ContextFaultCode::NoFault; }
};

namespace detail::context_net {

namespace net = ::foundation::algebra::net;
namespace tr = ::fixy::session::transition;

// Each function below that calls a function template has the parameter
// Anchor, and each caller passes its own Anchor on.  A public template passes
// anchored_t<^^Context, void> of foundation/reflect/Anchor.h, which is void
// with a dependent spelling.  So a unit that only includes this header
// instantiates no function template of the reading, and the units that read
// contexts share one instance of each.

// The place of a role or of a channel that a reading does not hold.
inline constexpr std::uint32_t no_place = 0xFFFFFFFFu;

// One action of a role, as the walk reads it: the role, the sender and the
// receiver of its channel, and the move.
struct read_action {
    net::ProcessId process{};
    net::ProcessId sender{};
    net::ProcessId receiver{};
    net::Move move = net::Move::Receive;
    std::uint8_t reserved[3]{};
};

// One channel: the queue from the sender to the receiver.
struct read_channel {
    net::ProcessId sender{};
    net::ProcessId receiver{};
};

// How the local type of a role runs (THE MODEL OF THE CHECK).
enum class RunShape : std::uint8_t {
    NoAction = 0,
    RunsOnce = 1,
    Loops = 2,
};

// The most roles, actions and channels of one context (THE COSTS).  A
// context past one of them has the fault PastTheBound.
inline constexpr std::uint32_t role_bound = 32;
inline constexpr std::uint32_t action_bound = 256;
inline constexpr std::uint32_t channel_bound = 64;

// The first elements of a fixed array of Bound elements, read by the id of
// an element.  The caller asks is_full before a push.
template <class Element, class Id, std::uint32_t Bound>
class bounded_run {
    Element items_[Bound]{};
    std::uint32_t count_ = 0;

public:
    [[nodiscard]] constexpr Element const& operator[](Id id) const noexcept {
        CRUCIBLE_PRE(static_cast<std::uint32_t>(id) < count_);
        return items_[static_cast<std::uint32_t>(id)];
    }
    [[nodiscard]] constexpr std::uint32_t size() const noexcept { return count_; }
    [[nodiscard]] constexpr bool is_full() const noexcept { return count_ == Bound; }
    [[nodiscard]] constexpr Element const* begin() const noexcept { return items_; }
    [[nodiscard]] constexpr Element const* end() const noexcept { return items_ + count_; }

    consteval void push(Element const& element) {
        CRUCIBLE_PRE(count_ < Bound);
        items_[count_] = element;
        ++count_;
    }
};

using role_run = bounded_run<std::meta::info, net::ProcessId, role_bound>;
using action_run = bounded_run<read_action, net::ActionId, action_bound>;
using channel_run = bounded_run<read_channel, net::ChannelId, channel_bound>;
using shape_run = bounded_run<RunShape, net::ProcessId, role_bound>;

// The reading of a context: the fault, the type of each role, the actions
// in the order of the roles, and the channels in their order (THE SYSTEM).
// Each reading has this one type, so each reader of a reading has one
// instance (THE COSTS).
struct context_reading {
    ContextFault fault{};
    role_run roles{};
    action_run actions{};
    channel_run channels{};
};

// True when the type, read through its aliases, is a specialization of the
// class template that Family reflects.
template <std::meta::info Family, class Anchor = void>
[[nodiscard]] consteval bool is_instance_of_(std::meta::info type) {
    if (!std::meta::is_type(type)) return false;
    std::meta::info const named = std::meta::dealias(type);
    return std::meta::has_template_arguments(named) && std::meta::template_of(named) == Family;
}

// The place of a role among the roles of a reading, or no_place.
// Complexity: linear in the roles.
template <class Anchor = void>
[[nodiscard]] consteval std::uint32_t place_of_role_(role_run const& roles, std::meta::info role) {
    for (std::uint32_t place = 0; place < roles.size(); ++place) {
        if (roles[net::ProcessId{place}] == role) return place;
    }
    return no_place;
}

// The rule of the last payload shape that a reading read: the place of the
// peer among the template arguments of a message of that shape, or npos.
// The steps of a context mostly share one shape, so a reading looks up the
// registry one time for each run of steps of one shape.
struct step_rule_memo {
    std::meta::info shape{};
    std::size_t peer_argument = tr::npos;
};

// The peer that the message of a step names, or the null reflection for a
// step whose payload names no peer.  Only a complete class has a payload
// rule (payload_rule_of of fixy/session/Classification.h).  The arguments
// stay a transient list, because a run in static storage costs a constant
// for each new payload (THE COSTS).  A rule that names an argument past the
// arguments of the payload stops the build, as rule_argument_of does.
template <class Anchor = void>
[[nodiscard]] consteval std::meta::info peer_of_step_(tr::graph_node const& step, step_rule_memo& memo) {
    std::meta::info const bare = std::meta::dealias(step.payload);
    bool const is_class = std::meta::is_class_type(bare) || std::meta::is_union_type(bare);
    if (!is_class || !std::meta::is_complete_type(bare)) return {};
    std::meta::info const shape = tr::shape_of(bare);
    if (shape != memo.shape) {
        tr::payload_lookup const rule = ::fixy::session::detail::message_rule_of(bare);
        memo = step_rule_memo{.shape = shape, .peer_argument = rule.is_found ? rule.entry.peer_argument : tr::npos};
    }
    if (memo.peer_argument == tr::npos) return {};
    if (std::meta::has_template_arguments(bare)) {
        std::size_t place = 0;
        for (std::meta::info const argument : std::meta::template_arguments_of(bare)) {
            if (place == memo.peer_argument) return std::meta::dealias(argument);
            ++place;
        }
    }
    ::fixy::session::detail::a_payload_rule_names_an_argument_that_the_payload_lacks();
    return {};
}

// A fault of the local type of a role.
[[nodiscard]] consteval ContextFault local_fault_(ContextFaultCode code, net::ProcessId role, std::meta::info type) {
    return ContextFault{.code = code, .role = role, .type = type};
}

// What the arm of a choice does next: an action, End, or the Continue of
// the loop around the whole local type.
enum class ArmStep : std::uint8_t {
    Acts = 0,
    Ends = 1,
    Continues = 2,
};

// The next step of one arm, and its action when it acts.
struct arm_head {
    ArmStep step = ArmStep::Acts;
    read_action act{};
};

// Two arms wait the same way when they end the same way, or when each acts
// on one channel with one move.  The two arguments can swap with no change
// of meaning.
[[nodiscard]] consteval bool is_same_wait_(arm_head const& left, arm_head const& right) {
    if (left.step != right.step) return false;
    if (left.step != ArmStep::Acts) return true;
    return left.act.sender == right.act.sender && left.act.receiver == right.act.receiver
        && left.act.move == right.act.move;
}

// The state of the walk of the arms of one local type (THE UNION OF THE
// ARMS).  cursors holds the node after the last action of each arm, and
// heads holds each node that a cursor reaches through its choices, one
// time.  seen holds the round in which the expansion last read each node.
// choice is the type of the first choice of two or more branches.
struct arm_walk {
    tr::stack<std::size_t> cursors{};
    tr::stack<std::size_t> heads{};
    tr::stack<std::size_t> pending{};
    tr::stack<std::uint32_t> seen{};
    std::uint32_t round = 0;
    std::meta::info choice{};
};

// Gives the heads of the arms: each node that a cursor reaches through the
// choices below it.  A choice of one branch is a keyed step, and a choice
// of two or more branches gives one arm for each branch.  Complexity:
// linear in the nodes that the cursors reach.
template <class Anchor = void>
consteval void expand_arms_(tr::graph_view const& graph, arm_walk& walk) {
    ++walk.round;
    walk.heads.top = 0;
    walk.pending.top = 0;
    for (std::size_t const cursor : walk.cursors)
        tr::push(walk.pending, cursor);
    while (walk.pending.top != 0) {
        --walk.pending.top;
        std::size_t const index = walk.pending.items[walk.pending.top];
        if (walk.seen.items[index] == walk.round) continue;
        walk.seen.items[index] = walk.round;
        tr::graph_node const& node = graph.nodes[index];
        if (node.entry.kind != tr::shape_kind::choice || node.child_count == 0) {
            tr::push(walk.heads, index);
            continue;
        }
        if (node.child_count > 1 && walk.choice == std::meta::info{}) walk.choice = node.type;
        for (std::size_t branch = 0; branch < node.child_count; ++branch)
            tr::push(walk.pending, graph.children[node.first_child + branch]);
    }
}

// The next step of the arm at a node, or the fault of a node that the walk
// cannot read (THE MODEL OF THE CHECK).  The root of a graph is its node 0,
// so a Loop around the whole local type is the binder of node 0, and its
// Continue is a back node to node 0.
template <class Anchor = void>
[[nodiscard]] consteval ContextFault head_of_arm_(tr::graph_view const& graph, tr::graph_node const& node,
                                                  net::ProcessId self, role_run const& roles, step_rule_memo& memo,
                                                  arm_head& head) {
    bool const is_loop_at_top = graph.nodes[0].entry.kind == tr::shape_kind::binder;
    switch (node.entry.kind) {
        case tr::shape_kind::terminal:
            if (node.entry.shape != ^^::fixy::session::End) {
                return local_fault_(ContextFaultCode::NodeOutsideModel, self, node.type);
            }
            head.step = ArmStep::Ends;
            return ContextFault{};
        case tr::shape_kind::back:
            if (!is_loop_at_top || node.next != 0) {
                return local_fault_(ContextFaultCode::LoopNotWhole, self, graph.nodes[0].type);
            }
            head.step = ArmStep::Continues;
            return ContextFault{};
        case tr::shape_kind::binder:
            return local_fault_(ContextFaultCode::LoopNotWhole, self, node.type);
        case tr::shape_kind::step:
            break;
        default:
            return local_fault_(ContextFaultCode::NodeOutsideModel, self, node.type);
    }
    std::meta::info const peer = peer_of_step_<Anchor>(node, memo);
    if (peer == std::meta::info{}) return local_fault_(ContextFaultCode::NodeOutsideModel, self, node.type);
    std::uint32_t const place = place_of_role_<Anchor>(roles, peer);
    if (place == no_place) return local_fault_(ContextFaultCode::PeerOutsideContext, self, peer);
    bool const is_send = node.entry.direction == tr::polarity::output;
    net::ProcessId const other{place};
    head.step = ArmStep::Acts;
    head.act = read_action{.process = self,
                           .sender = is_send ? self : other,
                           .receiver = is_send ? other : self,
                           .move = is_send ? net::Move::Send : net::Move::Receive};
    return ContextFault{};
}

// Pushes the actions of the local type of one role, after the actions of
// the roles before it, and sets the shape of the role, or gives the fault of
// the first node that the walk cannot read (THE MODEL OF THE CHECK, THE
// UNION OF THE ARMS), or the fault PastTheBound for an action past the
// bound.  Complexity: O(N^2) for N distinct nodes of the local type at most,
// because each round reads each node at most one time, and a round passes
// one action of each arm.
template <class Anchor = void>
[[nodiscard]] consteval ContextFault read_local_(std::meta::info local, net::ProcessId self, role_run const& roles,
                                                 step_rule_memo& memo, action_run& actions, RunShape& shape) {
    ::fixy::session::detail::subtype::require_registered_spine(local);
    if (!::fixy::session::detail::subtype::holds_for(^^::fixy::session::is_well_formed_v, local)) {
        return local_fault_(ContextFaultCode::NodeOutsideModel, self, local);
    }
    tr::graph_view const graph = tr::graph_of(::fixy::session::detail::protocol_registry, local);
    if (graph.unregistered != std::meta::info{}) {
        return local_fault_(ContextFaultCode::NodeOutsideModel, self, graph.unregistered);
    }
    std::uint32_t const first_action = actions.size();
    bool const is_loop_at_top = graph.nodes[0].entry.kind == tr::shape_kind::binder;
    arm_walk walk{};
    tr::make_room(walk.seen, graph.nodes.size());
    for (std::size_t index = 0; index < graph.nodes.size(); ++index)
        walk.seen.items[index] = 0;
    tr::push(walk.cursors, is_loop_at_top ? graph.nodes[0].next : std::size_t{0});
    // Each round passes one action, and no path through the actions of a
    // local type repeats a node before its back node, so the walk ends
    // within one round for each node.
    for (std::size_t rounds = 0; rounds <= graph.nodes.size(); ++rounds) {
        expand_arms_<Anchor>(graph, walk);
        arm_head first{};
        for (std::size_t place = 0; place < walk.heads.top; ++place) {
            arm_head head{};
            ContextFault const fault =
                head_of_arm_<Anchor>(graph, graph.nodes[walk.heads.items[place]], self, roles, memo, head);
            if (fault.is_fault()) return fault;
            if (place == 0) first = head;
            if (!is_same_wait_(head, first)) {
                std::meta::info const choice = walk.choice == std::meta::info{} ? local : walk.choice;
                return local_fault_(ContextFaultCode::ArmsWaitDifferently, self, choice);
            }
        }
        if (first.step == ArmStep::Ends) {
            shape = actions.size() == first_action ? RunShape::NoAction : RunShape::RunsOnce;
            return ContextFault{};
        }
        if (first.step == ArmStep::Continues) {
            if (actions.size() == first_action) {
                return local_fault_(ContextFaultCode::LoopNotWhole, self, graph.nodes[0].type);
            }
            shape = RunShape::Loops;
            return ContextFault{};
        }
        if (actions.is_full()) return local_fault_(ContextFaultCode::PastTheBound, self, roles[self]);
        actions.push(first.act);
        walk.cursors.top = 0;
        for (std::size_t place = 0; place < walk.heads.top; ++place)
            tr::push(walk.cursors, graph.nodes[walk.heads.items[place]].next);
    }
    return local_fault_(ContextFaultCode::NodeOutsideModel, self, local);
}

// The first action whose channel joins a role that loops and a role that
// runs one time, as the fault LoopMeetsRun, or NoFault (THE MODEL OF THE
// CHECK).  A role with no action holds no channel.  Complexity: O(A).
template <class Anchor = void>
[[nodiscard]] consteval ContextFault find_loop_meets_run_(role_run const& roles, action_run const& actions,
                                                          shape_run const& shapes) {
    for (read_action const& act : actions) {
        RunShape const sender = shapes[act.sender];
        RunShape const receiver = shapes[act.receiver];
        if (sender == receiver || sender == RunShape::NoAction || receiver == RunShape::NoAction) continue;
        net::ProcessId const looper = sender == RunShape::Loops ? act.sender : act.receiver;
        net::ProcessId const runner = sender == RunShape::Loops ? act.receiver : act.sender;
        return ContextFault{.code = ContextFaultCode::LoopMeetsRun, .role = looper, .type = roles[runner]};
    }
    return ContextFault{};
}

// Pushes the channels of the actions, in the order of the sender and then
// of the receiver (THE SYSTEM), or gives the fault PastTheBound for a
// channel past the bound.  Complexity: O(R^2 + A).
template <class Anchor = void>
[[nodiscard]] consteval ContextFault push_channels_(role_run const& roles, action_run const& actions,
                                                    channel_run& channels) {
    std::size_t const role_count = roles.size();
    tr::stack<bool> is_joined{};
    tr::make_room(is_joined, role_count * role_count);
    for (std::size_t slot = 0; slot < role_count * role_count; ++slot)
        is_joined.items[slot] = false;
    for (read_action const& act : actions) {
        is_joined
            .items[static_cast<std::uint32_t>(act.sender) * role_count + static_cast<std::uint32_t>(act.receiver)] =
            true;
    }
    for (std::uint32_t sender = 0; sender < role_count; ++sender) {
        for (std::uint32_t receiver = 0; receiver < role_count; ++receiver) {
            if (!is_joined.items[sender * role_count + receiver]) continue;
            net::ProcessId const from{sender};
            if (channels.is_full()) {
                return ContextFault{.code = ContextFaultCode::PastTheBound, .role = from, .type = roles[from]};
            }
            channels.push(read_channel{.sender = from, .receiver = net::ProcessId{receiver}});
        }
    }
    return ContextFault{};
}

// The reading of a context (THE SYSTEM), or its first fault.  The reading
// fills its fixed arrays in place, and it keeps no array in static storage
// (THE COSTS).  Complexity: linear in the distinct nodes of the local types,
// and O(R^2 + A).
template <class Anchor = void>
[[nodiscard]] consteval context_reading read_context(std::meta::info context) {
    context_reading reading{};
    if (!is_instance_of_<^^::fixy::session::TypingContext, Anchor>(context)) {
        reading.fault = ContextFault{.code = ContextFaultCode::NotAContext, .type = context};
        return reading;
    }
    // The template arguments stay transient lists, because a run in static
    // storage costs a constant for each new list (THE COSTS).  queues and
    // locals hold the queue and the local type of each role, at the place of
    // the role.
    role_run queues{};
    role_run locals{};
    for (std::meta::info const entry : std::meta::template_arguments_of(std::meta::dealias(context))) {
        if (!is_instance_of_<^^::fixy::session::RoleState, Anchor>(entry)) {
            reading.fault = ContextFault{.code = ContextFaultCode::NotAContext, .type = entry};
            return reading;
        }
        // A RoleState has three template arguments: the role, the queue and
        // the local type.
        std::meta::info parts[3]{};
        std::size_t part_place = 0;
        for (std::meta::info const part : std::meta::template_arguments_of(std::meta::dealias(entry))) {
            if (part_place < 3) parts[part_place] = part;
            ++part_place;
        }
        std::meta::info const role = std::meta::dealias(parts[0]);
        net::ProcessId const place{reading.roles.size()};
        if (place_of_role_<Anchor>(reading.roles, role) != no_place) {
            reading.fault = ContextFault{.code = ContextFaultCode::RoleTwice, .role = place, .type = role};
            return reading;
        }
        if (reading.roles.is_full()) {
            reading.fault = ContextFault{.code = ContextFaultCode::PastTheBound, .role = place, .type = role};
            return reading;
        }
        reading.roles.push(role);
        queues.push(parts[1]);
        locals.push(parts[2]);
    }
    shape_run shapes{};
    step_rule_memo memo{};
    for (std::uint32_t place = 0; place < reading.roles.size(); ++place) {
        net::ProcessId const self{place};
        if (std::meta::dealias(queues[self]) != ^^::fixy::session::OutQueue<>) {
            reading.fault = ContextFault{.code = ContextFaultCode::QueuedMessage, .role = self, .type = queues[self]};
            return reading;
        }
        RunShape shape = RunShape::NoAction;
        ContextFault const fault =
            read_local_<Anchor>(std::meta::dealias(locals[self]), self, reading.roles, memo, reading.actions, shape);
        if (fault.is_fault()) {
            reading.fault = fault;
            return reading;
        }
        shapes.push(shape);
    }
    reading.fault = find_loop_meets_run_<Anchor>(reading.roles, reading.actions, shapes);
    if (reading.fault.is_fault()) return reading;
    reading.fault = push_channels_<Anchor>(reading.roles, reading.actions, reading.channels);
    return reading;
}

// The reading of a context, one time in a unit.  find_context_fault, the
// system, the verdict and the door read this constant, so a door that also
// builds the system reads its context one time.
template <typename Context>
inline constexpr context_reading reading_of_ =
    read_context<::foundation::reflect::anchored_t<^^Context, void>>(^^Context);

// The name of a role: its identifier, or its display text for a role that
// has no identifier, in static storage.  The reflection has the anchored
// type, so the call depends on Anchor.
template <class Anchor = void>
[[nodiscard]] consteval net::NetName name_of_role_(std::meta::info role) {
    ::foundation::reflect::anchored_t<^^Anchor, std::meta::info> const reflected = role;
    if (std::meta::has_identifier(reflected)) {
        return net::NetName{std::define_static_string(std::meta::identifier_of(reflected))};
    }
    return net::NetName{std::define_static_string(std::meta::display_string_of(reflected))};
}

// Pushes the characters of a name.
template <class Anchor = void>
consteval void push_name_(tr::stack<char>& text, net::NetName name) {
    for (std::size_t index = 0; index < name.size(); ++index)
        tr::push(text, name.at(index));
}

// The name of the channel from the sender to the receiver: 'P to Q'.
template <class Anchor = void>
[[nodiscard]] consteval net::NetName name_of_channel_(context_reading const& reading, read_channel channel) {
    tr::stack<::foundation::reflect::anchored_t<^^Anchor, char>> text{};
    push_name_<Anchor>(text, name_of_role_<Anchor>(reading.roles[channel.sender]));
    push_name_<Anchor>(text, net::NetName{" to "});
    push_name_<Anchor>(text, name_of_role_<Anchor>(reading.roles[channel.receiver]));
    return net::NetName{std::define_static_string(text)};
}

// The place of the channel of an action among the channels of a reading.
// Complexity: linear in the channels.
template <class Anchor = void>
[[nodiscard]] consteval std::uint32_t place_of_channel_(context_reading const& reading, read_action act) {
    for (std::uint32_t place = 0; place < reading.channels.size(); ++place) {
        read_channel const& channel = reading.channels[net::ChannelId{place}];
        if (channel.sender == act.sender && channel.receiver == act.receiver) return place;
    }
    return no_place;
}

// The display text of a type, as a name in static storage.
template <class Anchor = void>
[[nodiscard]] consteval net::NetName name_of_type_(std::meta::info type) {
    ::foundation::reflect::anchored_t<^^Anchor, std::meta::info> const reflected = type;
    return net::NetName{std::define_static_string(std::meta::display_string_of(reflected))};
}

// The text of the refusal of the fault of a reading, or the admitted verdict
// for NoFault.
template <class Anchor = void>
[[nodiscard]] consteval net::NetVerdict explain_context_fault_(context_reading const& reading) {
    ContextFault const& fault = reading.fault;
    auto const role_name = [&]() consteval {
        return static_cast<std::uint32_t>(fault.role) < reading.roles.size()
                 ? name_of_role_<Anchor>(reading.roles[fault.role])
                 : name_of_role_<Anchor>(fault.type);
    };
    switch (fault.code) {
        case ContextFaultCode::NoFault:
            return net::NetVerdict{};
        case ContextFaultCode::NotAContext:
            return net::NetVerdict::refuse("context: the type is no TypingContext of RoleState entries: ")
                .put_name(name_of_type_<Anchor>(fault.type))
                .put(".");
        case ContextFaultCode::RoleTwice:
            return net::NetVerdict::refuse("context: the role '")
                .put_name(name_of_role_<Anchor>(fault.type))
                .put("' stands two times in the context.  Each role has one entry.");
        case ContextFaultCode::QueuedMessage:
            return net::NetVerdict::refuse("context: the role '")
                .put_name(role_name())
                .put("' holds a message in its queue.  The check reads a context whose queues are empty.");
        case ContextFaultCode::NodeOutsideModel:
            return net::NetVerdict::refuse("context: the local type of the role '")
                .put_name(role_name())
                .put("' holds a node that the check does not read: ")
                .put_name(name_of_type_<Anchor>(fault.type))
                .put(".  The check reads End, one Loop around the whole type with its Continue, the Send and Recv "
                     "steps of a PeerMsg, and a Select or an Offer whose arms wait the same way.");
        case ContextFaultCode::PeerOutsideContext:
            return net::NetVerdict::refuse("context: the local type of the role '")
                .put_name(role_name())
                .put("' names the peer '")
                .put_name(name_of_role_<Anchor>(fault.type))
                .put("', which is no role of the context.");
        case ContextFaultCode::LoopNotWhole:
            return net::NetVerdict::refuse("context: the local type of the role '")
                .put_name(role_name())
                .put("' holds a loop that is not one Loop around the whole type with an action in its period: ")
                .put_name(name_of_type_<Anchor>(fault.type))
                .put(".");
        case ContextFaultCode::LoopMeetsRun:
            return net::NetVerdict::refuse("context: the role '")
                .put_name(role_name())
                .put("' loops, the role '")
                .put_name(name_of_role_<Anchor>(fault.type))
                .put("' runs one time, and a channel joins them.  Along each channel the two roles loop, or the "
                     "two run one time.");
        case ContextFaultCode::ArmsWaitDifferently:
            return net::NetVerdict::refuse("context: a choice of the role '")
                .put_name(role_name())
                .put("' has arms that wait on other channels, or that end in other ways: ")
                .put_name(name_of_type_<Anchor>(fault.type))
                .put(".  Do a check of one context for each set of arms that the roles take together.");
        case ContextFaultCode::PastTheBound:
            return net::NetVerdict::refuse("context: the role '")
                .put_name(role_name())
                .put("' passes the bound of the check.  A context holds at most ")
                .put_number(role_bound)
                .put(" roles, ")
                .put_number(action_bound)
                .put(" actions and ")
                .put_number(channel_bound)
                .put(" channels.");
        default:
            break;
    }
    return net::NetVerdict::refuse("context: the fault has a code that no reading gives.");
}

// The most characters of the class of a refusal of a door.
inline constexpr std::size_t door_class_capacity = 64;

// The message of a refusal of a door: the class of the refusal, then the
// text of a verdict.  A static_assert reads data() and size().  The class
// and the text fit, so the message holds each character of the verdict.
struct door_text {
    char characters[door_class_capacity + net::net_verdict_capacity]{};
    std::size_t count = 0;

    [[nodiscard]] constexpr char const* data() const noexcept { return characters; }
    [[nodiscard]] constexpr std::size_t size() const noexcept { return count; }
};

// The message of a door for a verdict.  A class longer than
// door_class_capacity is cut.
[[nodiscard]] consteval door_text door_text_of_(net::NetName opening, net::NetVerdict const& verdict) {
    door_text text{};
    for (std::size_t index = 0; index < opening.size() && index < door_class_capacity; ++index) {
        text.characters[text.count] = opening.at(index);
        ++text.count;
    }
    for (std::size_t index = 0; index < verdict.size(); ++index) {
        text.characters[text.count] = verdict.data()[index];
        ++text.count;
    }
    return text;
}

inline constexpr net::NetName outside_model_class{"fixy::session::diagnostic [Context_Outside_Marked_Model]: "};
inline constexpr net::NetName not_live_class{"fixy::session::diagnostic [Context_Not_Live]: "};

}  // namespace detail::context_net

// The first fault that leaves the context with no marked system, or NoFault
// (THE MODEL OF THE CHECK).  Complexity: linear in the distinct nodes of the
// local types, and O(R^2 + A).
template <typename Context>
[[nodiscard]] consteval ContextFault find_context_fault() noexcept {
    return detail::context_net::reading_of_<Context>.fault;
}

namespace detail::context_net {

// The names of the processes and the channels of a marked system of a
// context.  The liveness check reads a name only for the text of a refusal.
// A name of a role costs a string in static storage, and a new text costs a
// constant of its own (THE COSTS).  So the door checks the system with the
// plain names, and it makes the names of the roles only for the text of a
// refusal.
enum class SystemNames : std::uint8_t {
    // Each process has the name 'role', and each channel the name 'channel'.
    Plain = 0,
    // Each process has the name of its role, and each channel the names of
    // its two roles: 'P to Q'.
    OfRoles = 1,
};

// The marked system of a context with no fault and with an action (THE
// SYSTEM), with the names of the form.  Each channel has the capacity that
// Channel states.  Complexity: O(C) for each action.
template <typename Context, typename Channel, SystemNames Names>
[[nodiscard]] consteval auto marked_system_of_context_() noexcept {
    namespace cn = detail::context_net;
    using anchor = ::foundation::reflect::anchored_t<^^Context, void>;
    constexpr cn::context_reading const& reading = cn::reading_of_<Context>;
    if constexpr (reading.fault.is_fault()) {
        static_assert(!reading.fault.is_fault(),
                      cn::door_text_of_(cn::outside_model_class, cn::explain_context_fault_<anchor>(reading)));
    } else if constexpr (reading.actions.size() == 0) {
        static_assert(reading.actions.size() != 0,
                      "fixy::session::diagnostic [Context_Outside_Marked_Model]: the context has no action, so it "
                      "has no marked system.  Its waits end at once.");
    } else {
        constexpr std::size_t process_count = reading.roles.size();
        constexpr std::size_t channel_count = reading.channels.size();
        constexpr std::size_t action_count = reading.actions.size();
        net::ProcessSystem<process_count, channel_count, action_count> marked{};
        for (std::uint32_t place = 0; place < process_count; ++place) {
            if constexpr (Names == SystemNames::OfRoles) {
                marked.process_names[place] = cn::name_of_role_<anchor>(reading.roles[net::ProcessId{place}]);
            } else {
                marked.process_names[place] = net::NetName{"role"};
            }
        }
        for (std::uint32_t place = 0; place < channel_count; ++place) {
            net::NetName name{"channel"};
            if constexpr (Names == SystemNames::OfRoles) {
                name = cn::name_of_channel_<anchor>(reading, reading.channels[net::ChannelId{place}]);
            }
            marked.channels[place] =
                net::NetChannel{.name = name, .capacity = static_cast<std::uint32_t>(channel_capacity_v<Channel>)};
        }
        for (std::uint32_t place = 0; place < action_count; ++place) {
            cn::read_action const act = reading.actions[net::ActionId{place}];
            marked.actions[place] = net::Action{.process = act.process,
                                                .channel = net::ChannelId{cn::place_of_channel_<anchor>(reading, act)},
                                                .move = act.move};
        }
        return marked;
    }
}

// True when the marked system of a context with the plain names holds no
// liveness fault.  The names change no step of the check, and no name of
// either form is empty, so the system with the names of the roles holds the
// same fault.
template <typename Context, typename Channel>
inline constexpr bool is_context_live_v =
    net::find_liveness_fault(marked_system_of_context_<Context, Channel, SystemNames::Plain>()).code
    == net::LivenessFaultCode::NoFault;

}  // namespace detail::context_net

// The marked system of a context with no fault and with an action (THE
// SYSTEM), with the names of its roles.  Each channel has the capacity that
// Channel states.  Complexity: O(C) for each action.
template <typename Context, typename Channel>
    requires StatesChannelCapacity<Channel>
[[nodiscard]] consteval auto marked_system_of_context() noexcept {
    namespace cn = detail::context_net;
    return cn::marked_system_of_context_<Context, Channel, cn::SystemNames::OfRoles>();
}

// The verdict on the waits of a context on channels of the capacity that
// Channel states: the refusal of its fault (THE MODEL OF THE CHECK), the
// verdict of check_liveness on its marked system, or the admitted verdict
// for a context with no action.  Complexity: the reading, and O(R + C + A).
template <typename Context, typename Channel>
    requires StatesChannelCapacity<Channel>
[[nodiscard]] consteval ::foundation::algebra::net::NetVerdict check_context_deadlock_freedom() noexcept {
    using anchor = ::foundation::reflect::anchored_t<^^Context, void>;
    constexpr detail::context_net::context_reading const& reading = detail::context_net::reading_of_<Context>;
    if constexpr (reading.fault.is_fault()) {
        return detail::context_net::explain_context_fault_<anchor>(reading);
    } else if constexpr (reading.actions.size() == 0) {
        return ::foundation::algebra::net::NetVerdict{};
    } else if constexpr (detail::context_net::is_context_live_v<Context, Channel>) {
        return ::foundation::algebra::net::NetVerdict{};
    } else {
        return ::foundation::algebra::net::check_liveness(marked_system_of_context<Context, Channel>());
    }
}

// The door: a context with a fault, or whose marked system holds a cycle of
// waits with no token, does not compile.  One static_assert fires, with the
// class of the refusal and the text of the verdict.
template <typename Context, typename Channel>
    requires StatesChannelCapacity<Channel>
consteval void ensure_context_deadlock_free() noexcept {
    namespace cn = detail::context_net;
    using anchor = ::foundation::reflect::anchored_t<^^Context, void>;
    constexpr cn::context_reading const& reading = cn::reading_of_<Context>;
    static_assert(!reading.fault.is_fault(),
                  cn::door_text_of_(cn::outside_model_class, cn::explain_context_fault_<anchor>(reading)));
    if constexpr (!reading.fault.is_fault() && reading.actions.size() != 0) {
        constexpr bool is_live = cn::is_context_live_v<Context, Channel>;
        if constexpr (!is_live) {
            static_assert(is_live,
                          cn::door_text_of_(cn::not_live_class, ::foundation::algebra::net::check_liveness(
                                                                    marked_system_of_context<Context, Channel>())));
        }
    }
}

}  // namespace fixy::session
