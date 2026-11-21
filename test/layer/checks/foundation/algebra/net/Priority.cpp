// The compile-time checks of foundation/algebra/net/Priority.h.

#include <foundation/algebra/net/Priority.h>

namespace foundation::algebra::net {

namespace detail::priority_self_test {

// A send of the process into the channel.
[[nodiscard]] consteval Action send_on(ProcessId process, ChannelId channel) noexcept {
    return Action{.process = process, .channel = channel, .move = Move::Send};
}

// A receive of the process from the channel.
[[nodiscard]] consteval Action receive_on(ProcessId process, ChannelId channel) noexcept {
    return Action{.process = process, .channel = channel, .move = Move::Receive};
}

inline constexpr ProcessId first_process{0};
inline constexpr ProcessId second_process{1};
inline constexpr ProcessId third_process{2};
inline constexpr ProcessId fourth_process{3};
inline constexpr ProcessId fifth_process{4};
inline constexpr ChannelId first_channel{0};
inline constexpr ChannelId second_channel{1};
inline constexpr ChannelId third_channel{2};
inline constexpr ChannelId fourth_channel{3};
inline constexpr ChannelId fifth_channel{4};

// ── The pipeline with the loop of the Vigil ─────────────────────────────
//
// The Vigil pushes into the ring, and flush() waits on the count that
// publish advances.  Each process loops, and the array holds one period.

inline constexpr ProcessId vigil = first_process;
inline constexpr ProcessId drain = second_process;
inline constexpr ProcessId detect = third_process;
inline constexpr ProcessId build = fourth_process;
inline constexpr ProcessId publish = fifth_process;
inline constexpr ChannelId ring = first_channel;
inline constexpr ChannelId trace = second_channel;
inline constexpr ChannelId work = third_channel;
inline constexpr ChannelId graphs = fourth_channel;
inline constexpr ChannelId processed = fifth_channel;

inline constexpr ProcessSystem<5, 5, 10> pipeline_loop{
    .process_names = {"vigil", "drain", "detect", "build", "publish"},
    .channels = {{.name = "ring", .capacity = 64},
                 {.name = "trace", .capacity = 64},
                 {.name = "work", .capacity = 64},
                 {.name = "graphs", .capacity = 64},
                 {.name = "processed", .capacity = 1}},
    .actions = {send_on(vigil, ring), receive_on(vigil, processed), receive_on(drain, ring), send_on(drain, trace),
                receive_on(detect, trace), send_on(detect, work), receive_on(build, work), send_on(build, graphs),
                receive_on(publish, graphs), send_on(publish, processed)}};
static_assert(check_liveness(pipeline_loop).is_admitted());

// The same pipeline with a rendezvous on each channel.
inline constexpr auto pipeline_rendezvous = [] consteval {
    auto protocol = pipeline_loop;
    for (NetChannel& channel : protocol.channels)
        channel.capacity = 0;
    return protocol;
}();
static_assert(check_liveness(pipeline_rendezvous).is_admitted());

// The Vigil waits on the count before it pushes: the cycle through the five
// processes holds no token, whatever the capacities.
inline constexpr auto pipeline_waits_first = [] consteval {
    auto protocol = pipeline_loop;
    protocol.actions[0] = receive_on(vigil, processed);
    protocol.actions[1] = send_on(vigil, ring);
    return protocol;
}();
static_assert(
    check_liveness(pipeline_waits_first)
        .says("liveness: a cycle of waits holds no token: vigil receives 'processed' step 0 <- publish sends "
              "'processed' step 0 <- publish receives 'graphs' step 0 <- build sends 'graphs' step 0 <- ....  "
              "No action of the cycle can start."));

// ── Sessions ────────────────────────────────────────────────────────────

inline constexpr ProcessId process_p = first_process;
inline constexpr ProcessId process_q = second_process;
inline constexpr ProcessId process_r = third_process;
inline constexpr ChannelId s1 = first_channel;
inline constexpr ChannelId s2 = second_channel;

// CrossedSessions: P waits on s2 first, and Q waits on s1 first.  No
// capacity helps, because each process receives first.
inline constexpr ProcessSystem<2, 2, 4> crossed_sessions{
    .process_names = {"P", "Q"},
    .channels = {{.name = "s1"}, {.name = "s2"}},
    .actions = {receive_on(process_p, s2), send_on(process_p, s1), receive_on(process_q, s1), send_on(process_q, s2)}};
static_assert(check_liveness(crossed_sessions)
                  .says("liveness: a cycle of waits holds no token: 's1' step 0 from P to Q <- 's2' step 0 from Q to "
                        "P.  No action of the cycle can start."));
inline constexpr auto crossed_buffered_sessions = [] consteval {
    auto protocol = crossed_sessions;
    for (NetChannel& channel : protocol.channels)
        channel.capacity = 1;
    return protocol;
}();
static_assert(!check_liveness(crossed_buffered_sessions).is_admitted());

// OrderedSessions: P sends on s1 first.
inline constexpr ProcessSystem<2, 2, 4> ordered_sessions{
    .process_names = {"P", "Q"},
    .channels = {{.name = "s1"}, {.name = "s2"}},
    .actions = {send_on(process_p, s1), receive_on(process_p, s2), receive_on(process_q, s1), send_on(process_q, s2)}};
static_assert(check_liveness(ordered_sessions).is_admitted());

// CrossedRing: each process waits on its left neighbour first.
inline constexpr ChannelId pq = first_channel;
inline constexpr ChannelId qr = second_channel;
inline constexpr ChannelId rp = third_channel;
inline constexpr ProcessSystem<3, 3, 6> crossed_ring{.process_names = {"P", "Q", "R"},
                                                     .channels = {{.name = "pq"}, {.name = "qr"}, {.name = "rp"}},
                                                     .actions = {receive_on(process_p, rp), send_on(process_p, pq),
                                                                 receive_on(process_q, pq), send_on(process_q, qr),
                                                                 receive_on(process_r, qr), send_on(process_r, rp)}};
static_assert(check_liveness(crossed_ring)
                  .says("liveness: a cycle of waits holds no token: 'pq' step 0 from P to Q <- 'rp' step 0 from R to "
                        "P <- 'qr' step 0 from Q to R.  No action of the cycle can start."));

// The Ring global type over one rendezvous for each pair, for two
// iterations.  The tree checks refuse the triangle, and this check admits it.
inline constexpr ProcessSystem<3, 3, 12> ring_of_queues{
    .process_names = {"P", "Q", "R"},
    .channels = {{.name = "pq"}, {.name = "qr"}, {.name = "rp"}},
    .actions = {send_on(process_p, pq), receive_on(process_p, rp), send_on(process_p, pq), receive_on(process_p, rp),
                receive_on(process_q, pq), send_on(process_q, qr), receive_on(process_q, pq), send_on(process_q, qr),
                receive_on(process_r, qr), send_on(process_r, rp), receive_on(process_r, qr), send_on(process_r, rp)}};
static_assert(check_liveness(ring_of_queues).is_admitted());

// Two processes that send first: a rendezvous deadlocks, and a buffer of one
// message does not.
inline constexpr ProcessSystem<2, 2, 4> send_first_rendezvous{
    .process_names = {"P", "Q"},
    .channels = {{.name = "s1"}, {.name = "s2"}},
    .actions = {send_on(process_p, s1), receive_on(process_p, s2), send_on(process_q, s2), receive_on(process_q, s1)}};
inline constexpr auto send_first_buffered = [] consteval {
    auto protocol = send_first_rendezvous;
    for (NetChannel& channel : protocol.channels)
        channel.capacity = 1;
    return protocol;
}();
static_assert(!check_liveness(send_first_rendezvous).is_admitted());
static_assert(check_liveness(send_first_buffered).is_admitted());

// A ring of four ranks: each sends to its right, then receives from its
// left.  A buffer of one message admits it, and a rendezvous does not.
inline constexpr ProcessSystem<4, 4, 8> rank_ring_buffered{
    .process_names = {"rank0", "rank1", "rank2", "rank3"},
    .channels = {{.name = "r01", .capacity = 1},
                 {.name = "r12", .capacity = 1},
                 {.name = "r23", .capacity = 1},
                 {.name = "r30", .capacity = 1}},
    .actions = {send_on(first_process, first_channel), receive_on(first_process, fourth_channel),
                send_on(second_process, second_channel), receive_on(second_process, first_channel),
                send_on(third_process, third_channel), receive_on(third_process, second_channel),
                send_on(fourth_process, fourth_channel), receive_on(fourth_process, third_channel)}};
inline constexpr auto rank_ring_rendezvous = [] consteval {
    auto protocol = rank_ring_buffered;
    for (NetChannel& channel : protocol.channels)
        channel.capacity = 0;
    return protocol;
}();
static_assert(check_liveness(rank_ring_buffered).is_admitted());
static_assert(!check_liveness(rank_ring_rendezvous).is_admitted());

// P sends two messages on c and then one on d, and Q waits on d first.  A
// buffer of two messages on c admits it.  A buffer of one makes the second
// send wait for a receive that waits on d.
inline constexpr ChannelId channel_c = first_channel;
inline constexpr ChannelId channel_d = second_channel;
inline constexpr ProcessSystem<2, 2, 6> wide_buffer{
    .process_names = {"P", "Q"},
    .channels = {{.name = "c", .capacity = 2}, {.name = "d", .capacity = 1}},
    .actions = {send_on(process_p, channel_c), send_on(process_p, channel_c), send_on(process_p, channel_d),
                receive_on(process_q, channel_d), receive_on(process_q, channel_c), receive_on(process_q, channel_c)}};
inline constexpr auto full_buffer = [] consteval {
    auto protocol = wide_buffer;
    protocol.channels[0].capacity = 1;
    return protocol;
}();
static_assert(check_liveness(wide_buffer).is_admitted());
static_assert(check_liveness(full_buffer)
                  .says("liveness: a cycle of waits holds no token: P sends 'c' step 1 <- Q receives 'c' step 0 <- Q "
                        "receives 'd' step 0 <- P sends 'd' step 0.  No action of the cycle can start."));

// A process that sends on a rendezvous and then receives from it waits on
// itself.  A buffer of one message admits it.
inline constexpr ProcessSystem<1, 1, 2> self_rendezvous{
    .process_names = {"P"},
    .channels = {{.name = "c"}},
    .actions = {send_on(process_p, channel_c), receive_on(process_p, channel_c)}};
inline constexpr auto self_buffer = [] consteval {
    auto protocol = self_rendezvous;
    protocol.channels[0].capacity = 1;
    return protocol;
}();
static_assert(check_liveness(self_rendezvous)
                  .says("liveness: a cycle of waits holds no token: 'c' step 0 from P to P.  No action of the cycle "
                        "can start."));
static_assert(check_liveness(self_buffer).is_admitted());

// two_forked_sessions_in_a_cycle with the escaped endpoints: each left body
// waits on its own receive, and then sends on the endpoint of the other
// session.
inline constexpr ChannelId channel_a = first_channel;
inline constexpr ChannelId channel_b = second_channel;
inline constexpr ProcessSystem<2, 2, 4> escaped_endpoints{
    .process_names = {"left body of A", "left body of B"},
    .channels = {{.name = "a"}, {.name = "b"}},
    .actions = {receive_on(first_process, channel_a), send_on(first_process, channel_b),
                receive_on(second_process, channel_b), send_on(second_process, channel_a)}};
static_assert(!check_liveness(escaped_endpoints).is_admitted());

// P waits on a, then on b, then on a again, and Q sends in that order.  One
// priority for each session cannot order it, and one node for each step can.
inline constexpr ProcessSystem<2, 2, 6> interleaved_waits{
    .process_names = {"P", "Q"},
    .channels = {{.name = "a"}, {.name = "b"}},
    .actions = {receive_on(process_p, channel_a), receive_on(process_p, channel_b), receive_on(process_p, channel_a),
                send_on(process_q, channel_a), send_on(process_q, channel_b), send_on(process_q, channel_a)}};
static_assert(check_liveness(interleaved_waits).is_admitted());

// The actions of the processes can interleave in the array.
inline constexpr ProcessSystem<2, 2, 4> interleaved_array{
    .process_names = {"P", "Q"},
    .channels = {{.name = "s1"}, {.name = "s2"}},
    .actions = {send_on(process_p, s1), receive_on(process_q, s1), receive_on(process_p, s2), send_on(process_q, s2)}};
static_assert(check_liveness(interleaved_array).is_admitted());

// ── The refusals of a malformed system ──────────────────────────────────

inline constexpr ProcessSystem<2, 1, 2> shared_side{.process_names = {"P", "Q"},
                                                    .channels = {{.name = "s1"}},
                                                    .actions = {send_on(process_p, s1), send_on(process_q, s1)}};
static_assert(check_liveness(shared_side)
                  .says("liveness: the send side of channel 's1' is held by 'P' and by 'Q'.  One process holds each "
                        "side of a channel."));

inline constexpr ProcessSystem<2, 1, 3> uneven_sides{
    .process_names = {"P", "Q"},
    .channels = {{.name = "s1"}},
    .actions = {send_on(process_p, s1), send_on(process_p, s1), receive_on(process_q, s1)}};
static_assert(check_liveness(uneven_sides)
                  .says("liveness: channel 's1' has 2 sends and 1 receives.  Each send has its receive."));

inline constexpr ProcessSystem<2, 1, 2> unnamed_process{.process_names = {"P"},
                                                        .channels = {{.name = "s1"}},
                                                        .actions = {send_on(process_p, s1), receive_on(process_q, s1)}};
static_assert(check_liveness(unnamed_process).says("liveness: process 1 has no name."));

inline constexpr ProcessSystem<2, 2, 2> unnamed_channel{.process_names = {"P", "Q"},
                                                        .channels = {{.name = "s1"}},
                                                        .actions = {send_on(process_p, s1), receive_on(process_q, s1)}};
static_assert(check_liveness(unnamed_channel).says("liveness: channel 1 has no name."));

inline constexpr ProcessSystem<2, 1, 2> foreign_process{
    .process_names = {"P", "Q"},
    .channels = {{.name = "s1"}},
    .actions = {send_on(process_p, s1), receive_on(ProcessId{7}, s1)}};
static_assert(check_liveness(foreign_process)
                  .says("liveness: action 1 names a process, a channel or a move that the system does not have."));

// ── The sealed system ───────────────────────────────────────────────────

// True when two faults name the same code, process, channel, action and
// cycle.  The two arguments can swap with no change of the result.
[[nodiscard]] consteval bool is_same_liveness_fault(LivenessFault const& left, LivenessFault const& right) noexcept {
    if (left.code != right.code || left.process != right.process || left.channel != right.channel
        || left.action != right.action || left.path_count != right.path_count || left.is_path_cut != right.is_path_cut)
        return false;
    for (std::size_t index = 0; index < left.path_count && index < max_liveness_path; ++index) {
        if (left.path[index] != right.path[index]) return false;
    }
    return true;
}

// The check of the sealed system of a system gives the fault of the check of
// the system, and the seal holds a fault only for a fault of the form.
template <std::size_t Processes, std::size_t Channels, std::size_t Actions>
[[nodiscard]] consteval bool
does_seal_keep_the_fault(ProcessSystem<Processes, Channels, Actions> const& protocol) noexcept {
    SealedSystem<Processes, Channels, Actions> const sealed = seal_system(protocol);
    LivenessFault const fault = find_liveness_fault(protocol);
    bool const is_form_fault =
        fault.code == LivenessFaultCode::ProcessWithoutName || fault.code == LivenessFaultCode::ChannelWithoutName
        || fault.code == LivenessFaultCode::ActionPastSystem || fault.code == LivenessFaultCode::SideHeldTwice;
    LivenessFault const expected_form = is_form_fault ? fault : LivenessFault{};
    return is_same_liveness_fault(find_liveness_fault(sealed), fault)
        && is_same_liveness_fault(sealed.form_fault(), expected_form);
}

// Each code: no fault, a cycle with no token, two sends and one receive, a
// side that two processes hold, a process and a channel with no name, and an
// action past the system.
static_assert(does_seal_keep_the_fault(pipeline_loop) && does_seal_keep_the_fault(pipeline_waits_first));
static_assert(does_seal_keep_the_fault(crossed_sessions) && does_seal_keep_the_fault(crossed_ring));
static_assert(does_seal_keep_the_fault(uneven_sides) && does_seal_keep_the_fault(shared_side));
static_assert(does_seal_keep_the_fault(unnamed_process) && does_seal_keep_the_fault(unnamed_channel));
static_assert(does_seal_keep_the_fault(foreign_process) && does_seal_keep_the_fault(ring_of_queues));
static_assert(find_liveness_fault(seal_system(crossed_sessions)).code == LivenessFaultCode::CycleWithoutToken);
static_assert(seal_system(foreign_process).form_fault().code == LivenessFaultCode::ActionPastSystem);

}  // namespace detail::priority_self_test

}  // namespace foundation::algebra::net
