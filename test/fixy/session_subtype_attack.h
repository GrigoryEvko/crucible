#pragma once

// The shared part of test_session_subtype_attack: the payloads, the roles,
// the labels, the family of anticipations, the scripts that a protocol
// type gives, the generator of the attacks, and the doors of the runner
// that main holds.  test_session_subtype_attack.cpp lists the source files
// of the test.

#include <fixy/session/Handle.h>
#include <fixy/session/Projection.h>
#include <fixy/session/Subtype.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <meta>
#include <span>
#include <string_view>
#include <utility>
#include <vector>

namespace test_session_subtype_attack_types {

namespace s = ::fixy::session;
namespace tr = ::foundation::algebra::transition;
namespace tags = ::fixy::tags;

struct A {};
struct B {};
struct C {};
struct Alice {};
struct Bob {};

using s::Continue;
using s::End;
using s::Loop;
using s::Offer;
using s::Recv;
using s::Select;
using s::Send;
using s::Sender;

// ── A family of anticipations ────────────────────────────────────────
//
// early<K> sends K messages and then receives K.  late<K> receives K
// and then sends K.  early<K> refines late<K> asynchronously only when
// the channel holds K messages in each direction: the subtype sends K
// ahead, and the peer, which speaks the dual of late<K>, sends K before
// it receives.

template <std::size_t N, class Message, class Rest>
struct sends {
    using type = Send<Message, typename sends<N - 1, Message, Rest>::type>;
};
template <class Message, class Rest>
struct sends<0, Message, Rest> {
    using type = Rest;
};
template <std::size_t N, class Message, class Rest>
struct receives {
    using type = Recv<Message, typename receives<N - 1, Message, Rest>::type>;
};
template <class Message, class Rest>
struct receives<0, Message, Rest> {
    using type = Rest;
};

template <std::size_t K>
using early = typename sends<K, A, typename receives<K, B, End>::type>::type;
template <std::size_t K>
using late = typename receives<K, B, typename sends<K, A, End>::type>::type;

// The end of a channel that holds C messages in each direction.  The
// check reads its capacity from this type, never from a number.
template <std::size_t C>
struct ring {
    static constexpr std::size_t channel_capacity = C;
};

// Two labels.  The choice group checks them, and the keyed group sends
// them on a wire.
struct L0 {};
struct L1 {};

// ── Scripts from protocol types ──────────────────────────────────────

struct step {
    bool is_send = false;
    int message = 0;
};

consteval int message_id(std::meta::info payload) {
    if (payload == ^^A) return 1;
    if (payload == ^^B) return 2;
    return 0;
}

consteval std::vector<step> script_steps(std::meta::info protocol) {
    std::vector<step> steps;
    std::meta::info current = std::meta::dealias(protocol);
    while (std::meta::has_template_arguments(current)) {
        const std::meta::info shape = std::meta::template_of(current);
        const std::vector<std::meta::info> arguments = std::meta::template_arguments_of(current);
        steps.push_back(step{shape == ^^s::Send, message_id(std::meta::dealias(arguments[0]))});
        current = std::meta::dealias(arguments[1]);
    }
    return steps;
}

template <class P>
inline constexpr std::size_t script_length_v = script_steps(^^P).size();

template <class P>
inline constexpr std::array<step, script_length_v<P>> script_v = [] {
    std::array<step, script_length_v<P>> out{};
    const std::vector<step> steps = script_steps(^^P);
    for (std::size_t index = 0; index < steps.size(); ++index)
        out[index] = steps[index];
    return out;
}();

// ── The runner ───────────────────────────────────────────────────────

enum class outcome : std::uint8_t {
    completed,
    deadlocked,
    wrong_message,
    orphan
};

constexpr std::string_view outcome_name(outcome value) {
    switch (value) {
        case outcome::completed:
            return "completed";
        case outcome::deadlocked:
            return "deadlocked";
        case outcome::wrong_message:
            return "received a wrong message";
        case outcome::orphan:
            return "left a message in a buffer";
        default:
            break;
    }
    return "unknown";
}

// Runs the two scripts against each other on two bounded queues of the
// capacity, with a watchdog (test_session_subtype_attack.cpp).
outcome run_pair(std::span<const step> left, std::span<const step> right, std::size_t capacity);

template <class Sub, class Super>
outcome run_against_dual(std::size_t capacity) {
    return run_pair(script_v<Sub>, script_v<s::dual_of_t<Super>>, capacity);
}

// Prints the message and counts a failure when the condition is false
// (test_session_subtype_attack.cpp).
void expect(bool condition, std::string_view what);

// ── The generator ────────────────────────────────────────────────────
//
// The recursion group and the generated anticipations build protocols
// from these shapes, with a linear congruential generator.

inline constexpr std::meta::info send_shape = ^^s::Send;
inline constexpr std::meta::info recv_shape = ^^s::Recv;
inline constexpr std::meta::info select_shape = ^^s::Select;
inline constexpr std::meta::info loop_shape = ^^s::Loop;

struct lcg {
    std::uint64_t state = 0x2545f4914f6cdd1dULL;
    consteval std::uint64_t next() {
        state = state * 6364136223846793005ULL + 1442695040888963407ULL;
        return state >> 33;
    }
    consteval std::size_t below(std::size_t bound) { return next() % bound; }
};

// ── The doors of the groups that run ─────────────────────────────────

inline constexpr std::size_t checked_capacity = 4;

struct generated_tally {
    std::size_t sound_runs = 0;
    std::size_t unsound_runs = 0;
    std::size_t refused_but_completes = 0;
};

// The generated anticipations (test_session_subtype_attack_generated.cpp):
// the runs of each generated case, and the number of pairs that the check
// admits.
generated_tally run_generated();
std::size_t admitted_pair_count();

// The keyed choices on a wire (test_session_subtype_attack_keyed.cpp).
[[nodiscard]] std::pair<int, int> labels_on_a_word_wire();
[[nodiscard]] int keyed_step_meets_wider_offer();

}  // namespace test_session_subtype_attack_types
