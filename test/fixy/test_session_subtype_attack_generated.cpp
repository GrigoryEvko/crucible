// Generated anticipations: mutations of finite protocols, the answers of
// the asynchronous check at each capacity, and a run of each admitted pair
// against the dual of its supertype.

#include "session_subtype_attack.h"

#include <array>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <meta>
#include <span>
#include <vector>

namespace test_session_subtype_attack_types {

// ── Generated anticipations ──────────────────────────────────────────
//
// Each case takes a finite protocol and mutates it: it swaps two
// adjacent actions, or it changes the message of one action.  A swap of
// a receive and a later send is the move that asynchronous subtyping
// admits.  Every other swap and every message change must be refused.
// The check answers for capacities 1 to 4, and main runs each admitted
// pair against the dual of the supertype on a channel of that capacity.
// A run that deadlocks, receives a wrong message or leaves a message in
// a buffer is an unsound answer.  The seed is fixed.

inline constexpr std::size_t max_script = 8;
inline constexpr std::size_t generated_count = 40;

struct generated_case {
    std::array<step, max_script> sub{};
    std::size_t sub_length = 0;
    std::array<step, max_script> peer{};
    std::size_t peer_length = 0;
    std::uint8_t admitted = 0;
    bool is_synchronous = false;
};

// True when the check admits the case at this capacity, 1 to 4.
constexpr bool admits_at(const generated_case& item, std::size_t capacity) {
    return ((static_cast<unsigned>(item.admitted) >> (capacity - 1)) & 1U) != 0;
}

consteval std::meta::info protocol_of(const std::vector<step>& actions) {
    std::meta::info protocol = ^^s::End;
    for (std::size_t index = actions.size(); index-- > 0;) {
        const std::meta::info message = actions[index].message == 1 ? ^^A : ^^B;
        protocol = std::meta::substitute(actions[index].is_send ? send_shape : recv_shape, {message, protocol});
    }
    return protocol;
}

consteval generated_case make_case(lcg& random) {
    std::vector<step> super_actions;
    const std::size_t length = 2 + random.below(5);
    for (std::size_t index = 0; index < length; ++index) {
        super_actions.push_back(step{random.below(2) == 0, 1 + static_cast<int>(random.below(2))});
    }
    std::vector<step> sub_actions = super_actions;
    const std::size_t mutations = 1 + random.below(3);
    for (std::size_t round = 0; round < mutations; ++round) {
        const std::size_t at = random.below(sub_actions.size() - 1);
        if (random.below(10) == 0) {
            sub_actions[at].message = 3 - sub_actions[at].message;
        } else {
            const step moved = sub_actions[at];
            sub_actions[at] = sub_actions[at + 1];
            sub_actions[at + 1] = moved;
        }
    }
    const std::meta::info sub = protocol_of(sub_actions);
    const std::meta::info super = protocol_of(super_actions);
    const std::vector<step> peer = script_steps(std::meta::dealias(std::meta::substitute(^^s::dual_of_t, {super})));
    generated_case out{};
    for (std::size_t index = 0; index < sub_actions.size(); ++index)
        out.sub[index] = sub_actions[index];
    out.sub_length = sub_actions.size();
    for (std::size_t index = 0; index < peer.size(); ++index)
        out.peer[index] = peer[index];
    out.peer_length = peer.size();
    for (std::size_t capacity = 1; capacity <= checked_capacity; ++capacity) {
        const std::meta::info channel = std::meta::substitute(^^ring, {std::meta::reflect_constant(capacity)});
        const bool admits =
            std::meta::extract<bool>(std::meta::substitute(^^s::is_subtype_async_v, {sub, super, channel}));
        if (admits) out.admitted = static_cast<std::uint8_t>(out.admitted | (1U << (capacity - 1)));
    }
    out.is_synchronous = std::meta::extract<bool>(std::meta::substitute(^^s::is_subtype_sync_v, {sub, super}));
    return out;
}

consteval std::vector<generated_case> make_cases() {
    lcg random{0x9e3779b97f4a7c15ULL};
    std::vector<generated_case> cases;
    for (std::size_t index = 0; index < generated_count; ++index)
        cases.push_back(make_case(random));
    return cases;
}

inline constexpr std::span<const generated_case> generated_cases = std::define_static_array(make_cases());

consteval std::size_t admitted_pairs() {
    std::size_t count = 0;
    for (const generated_case& item : generated_cases)
        count += static_cast<std::size_t>(std::popcount(item.admitted));
    return count;
}
consteval std::size_t refused_cases() {
    std::size_t count = 0;
    for (const generated_case& item : generated_cases)
        count += item.admitted == 0 ? 1 : 0;
    return count;
}
static_assert(generated_cases.size() == generated_count);
static_assert(admitted_pairs() > 0 && refused_cases() > 0,
              "the family must hold admitted pairs and refused cases, or it proves nothing about either");

// The check is monotone in the capacity: a pair it admits at capacity C
// it admits at each capacity above C.
consteval bool admitted_is_monotone() {
    for (const generated_case& item : generated_cases) {
        for (std::size_t capacity = 1; capacity < checked_capacity; ++capacity) {
            if (admits_at(item, capacity) && !admits_at(item, capacity + 1)) return false;
        }
    }
    return true;
}
static_assert(admitted_is_monotone());

// Runs every admitted pair.  A case that the check refuses at every
// capacity is run one time at the largest capacity, and a run that then
// completes counts as a pair the bounded check cannot prove.
generated_tally run_generated() {
    generated_tally tally{};
    for (std::size_t index = 0; index < generated_cases.size(); ++index) {
        const generated_case& item = generated_cases[index];
        const std::span<const step> sub{item.sub.data(), item.sub_length};
        const std::span<const step> peer{item.peer.data(), item.peer_length};
        for (std::size_t capacity = 1; capacity <= checked_capacity; ++capacity) {
            if (!admits_at(item, capacity)) continue;
            const outcome observed = run_pair(sub, peer, capacity);
            if (observed == outcome::completed) {
                ++tally.sound_runs;
            } else {
                ++tally.unsound_runs;
                std::fprintf(stderr, "generated case %zu at capacity %zu: the check admits, the run %.*s\n", index,
                             capacity, static_cast<int>(outcome_name(observed).size()), outcome_name(observed).data());
            }
        }
        if (item.is_synchronous && run_pair(sub, peer, 1) != outcome::completed) {
            ++tally.unsound_runs;
            std::fprintf(stderr, "generated case %zu: the synchronous relation admits, the run fails\n", index);
        }
        if (item.admitted == 0 && run_pair(sub, peer, checked_capacity) == outcome::completed) {
            ++tally.refused_but_completes;
        }
    }
    return tally;
}

std::size_t admitted_pair_count() { return admitted_pairs(); }

}  // namespace test_session_subtype_attack_types
