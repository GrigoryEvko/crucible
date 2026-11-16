// The compile-time checks of foundation/algebra/net/Priority.h.

#include <foundation/algebra/net/Priority.h>

#include <cstdint>

namespace foundation::algebra::net {

namespace detail::priority_self_test {

inline constexpr ChannelSide first_side = ChannelSide::First;
inline constexpr ChannelSide second_side = ChannelSide::Second;

// A send that waits, as a synchronous send does.
[[nodiscard]] consteval Action send_on(std::uint32_t process, std::uint32_t channel, ChannelSide side) noexcept {
    return Action{ProcessId{process}, ChannelId{channel}, side, true, true, 0};
}

// A send into a buffer with room, which does not wait.
[[nodiscard]] consteval Action send_buffered_on(std::uint32_t process, std::uint32_t channel,
                                                ChannelSide side) noexcept {
    return Action{ProcessId{process}, ChannelId{channel}, side, true, false, 0};
}

[[nodiscard]] consteval Action receive_on(std::uint32_t process, std::uint32_t channel, ChannelSide side) noexcept {
    return Action{ProcessId{process}, ChannelId{channel}, side, false, true, 0};
}

// The pipeline with the loop of the Vigil: the Vigil pushes into the ring,
// and flush() waits on the count that publish advances.  Each action waits.
inline constexpr ProcessSystem<5, 5, 10> pipeline_loop{
    .process_names = {"vigil", "drain", "detect", "build", "publish"},
    .channel_names = {"ring", "trace", "work", "graphs", "processed"},
    .actions = {send_on(0, 0, first_side), receive_on(0, 4, second_side), receive_on(1, 0, second_side),
                send_on(1, 1, first_side), receive_on(2, 1, second_side), send_on(2, 2, first_side),
                receive_on(3, 2, second_side), send_on(3, 3, first_side), receive_on(4, 3, second_side),
                send_on(4, 4, first_side)}};
static_assert(check_priorities(pipeline_loop).is_admitted());

// CrossedSessions: P waits on s2 first, and Q waits on s1 first.
inline constexpr ProcessSystem<2, 2, 4> crossed_sessions{
    .process_names = {"P", "Q"},
    .channel_names = {"s1", "s2"},
    .actions = {receive_on(0, 1, second_side), send_on(0, 0, first_side), receive_on(1, 0, second_side),
                send_on(1, 1, first_side)}};
static_assert(check_priorities(crossed_sessions)
                  .says("priorities: a cycle of waits: P waits on 's2' step 0 before 's1' step 0, Q waits on 's1' "
                        "step 0 before 's2' step 0.  No priority order exists."));

// OrderedSessions: P sends on s1 first.
inline constexpr ProcessSystem<2, 2, 4> ordered_sessions{
    .process_names = {"P", "Q"},
    .channel_names = {"s1", "s2"},
    .actions = {send_on(0, 0, first_side), receive_on(0, 1, second_side), receive_on(1, 0, second_side),
                send_on(1, 1, first_side)}};
static_assert(check_priorities(ordered_sessions).is_admitted());

// CrossedRing: each process waits on its left neighbour first.
inline constexpr ProcessSystem<3, 3, 6> crossed_ring{
    .process_names = {"P", "Q", "R"},
    .channel_names = {"pq", "qr", "rp"},
    .actions = {receive_on(0, 2, second_side), send_on(0, 0, first_side), receive_on(1, 0, second_side),
                send_on(1, 1, first_side), receive_on(2, 1, second_side), send_on(2, 2, first_side)}};
static_assert(check_priorities(crossed_ring)
                  .says("priorities: a cycle of waits: P waits on 'rp' step 0 before 'pq' step 0, R waits on 'qr' "
                        "step 0 before 'rp' step 0, Q waits on 'pq' step 0 before 'qr' step 0.  No priority order "
                        "exists."));

// The Ring global type over one queue for each pair, for two iterations.
// The tree checks refuse the triangle, and the priorities admit it.
inline constexpr ProcessSystem<3, 3, 12> ring_of_queues{
    .process_names = {"P", "Q", "R"},
    .channel_names = {"pq", "qr", "rp"},
    .actions = {send_on(0, 0, first_side), receive_on(0, 2, second_side), send_on(0, 0, first_side),
                receive_on(0, 2, second_side), receive_on(1, 0, second_side), send_on(1, 1, first_side),
                receive_on(1, 0, second_side), send_on(1, 1, first_side), receive_on(2, 1, second_side),
                send_on(2, 2, first_side), receive_on(2, 1, second_side), send_on(2, 2, first_side)}};
static_assert(check_priorities(ring_of_queues).is_admitted());

// Two processes that send first: synchronous sends deadlock, and buffered
// sends do not.
inline constexpr ProcessSystem<2, 2, 4> send_first_synchronous{
    .process_names = {"P", "Q"},
    .channel_names = {"s1", "s2"},
    .actions = {send_on(0, 0, first_side), receive_on(0, 1, second_side), send_on(1, 1, first_side),
                receive_on(1, 0, second_side)}};
inline constexpr ProcessSystem<2, 2, 4> send_first_buffered{
    .process_names = {"P", "Q"},
    .channel_names = {"s1", "s2"},
    .actions = {send_buffered_on(0, 0, first_side), receive_on(0, 1, second_side), send_buffered_on(1, 1, first_side),
                receive_on(1, 0, second_side)}};
static_assert(!check_priorities(send_first_synchronous).is_admitted());
static_assert(check_priorities(send_first_buffered).is_admitted());

// two_forked_sessions_in_a_cycle with the escaped endpoints: each left body
// waits on its own receive, and then sends on the endpoint of the other
// session.
inline constexpr ProcessSystem<2, 2, 4> escaped_endpoints{
    .process_names = {"left body of A", "left body of B"},
    .channel_names = {"a", "b"},
    .actions = {receive_on(0, 0, second_side), send_on(0, 1, first_side), receive_on(1, 1, second_side),
                send_on(1, 0, first_side)}};
static_assert(!check_priorities(escaped_endpoints).is_admitted());

// P waits on a, then on b, then on a again, and Q sends in that order.  One
// priority for each session cannot order it, and one for each step can.
inline constexpr ProcessSystem<2, 2, 6> interleaved_waits{
    .process_names = {"P", "Q"},
    .channel_names = {"a", "b"},
    .actions = {receive_on(0, 0, second_side), receive_on(0, 1, second_side), receive_on(0, 0, second_side),
                send_on(1, 0, first_side), send_on(1, 1, first_side), send_on(1, 0, first_side)}};
static_assert(check_priorities(interleaved_waits).is_admitted());

// The actions of the processes can interleave in the array.
inline constexpr ProcessSystem<2, 2, 4> interleaved_array{
    .process_names = {"P", "Q"},
    .channel_names = {"s1", "s2"},
    .actions = {send_on(0, 0, first_side), receive_on(1, 0, second_side), receive_on(0, 1, second_side),
                send_on(1, 1, first_side)}};
static_assert(check_priorities(interleaved_array).is_admitted());

// ── The refusals of a malformed system ──────────────────────────────────

inline constexpr ProcessSystem<2, 1, 2> shared_side{.process_names = {"P", "Q"},
                                                    .channel_names = {"s1"},
                                                    .actions = {send_on(0, 0, first_side), send_on(1, 0, first_side)}};
static_assert(check_priorities(shared_side)
                  .says("priorities: the first side of channel 's1' is held by 'P' and by 'Q'.  One process holds "
                        "each side of a channel."));

inline constexpr ProcessSystem<2, 1, 3> uneven_sides{
    .process_names = {"P", "Q"},
    .channel_names = {"s1"},
    .actions = {send_on(0, 0, first_side), send_on(0, 0, first_side), receive_on(1, 0, second_side)}};
static_assert(check_priorities(uneven_sides)
                  .says("priorities: channel 's1' has 2 actions on its first side and 1 on its second side.  The two "
                        "sides are not dual."));

inline constexpr ProcessSystem<2, 1, 2> two_senders{.process_names = {"P", "Q"},
                                                    .channel_names = {"s1"},
                                                    .actions = {send_on(0, 0, first_side), send_on(1, 0, second_side)}};
static_assert(check_priorities(two_senders)
                  .says("priorities: step 0 of channel 's1' sends on both sides or receives on both sides."));

}  // namespace detail::priority_self_test

}  // namespace foundation::algebra::net
