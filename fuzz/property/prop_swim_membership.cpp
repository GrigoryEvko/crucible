// ═══════════════════════════════════════════════════════════════════
// prop_swim_membership.cpp — membership-monotonicity fuzzer for the
// canopy SWIM failure detector (canopy/Swim.h).
//
// SwimMembership::apply_gossip reconciles an incoming gossip event with
// the local record of a peer.  Its acceptance rule (event_newer_) is the
// load-bearing SWIM safety gate: a member's status may only move
// "forward" in the (incarnation, state) order — Alive(0) < Suspect(1) <
// Dead(2) within an incarnation, and any state at a strictly higher
// incarnation.  If this regresses, stale gossip can RESURRECT a dead
// node (Dead→Alive at equal incarnation) or re-kill a live one, and the
// fleet's membership view diverges.
//
// test_swim.cpp pins the headline scenarios by hand (inc 9 Dead accept,
// inc 8 reject, inc 10 revive).  This fuzzer generalises to random event
// sequences over a small shared peer set and re-derives the acceptance
// rule INDEPENDENTLY as a lexicographic tuple compare
// (incoming.inc, rank(incoming.state)) > (slot.inc, rank(slot.state)) —
// a different formulation from event_newer_'s if/else — so a divergence
// is a genuine bug.  After every apply_gossip it asserts:
//
//   * non-regression: the peer's (incarnation, state_rank) is
//     lexicographically >= its previous value (never moves backward —
//     no resurrection, no incarnation rollback)
//   * override-iff-strictly-newer: the slot equals the incoming event
//     exactly WHEN the incoming is lexicographically greater, and is
//     left unchanged otherwise (idempotent / stale-drop)
//   * first sighting auto-adds the peer verbatim
//
// incarnations are corner-biased to a small set (so equal-incarnation
// state competition fires densely) plus {0, 1, MAX-1, MAX}; states span
// all three; four shared UUIDs guarantee repeated reconciliation of the
// same slot.  The rule is correct; this is the full-domain safety net.
// ═══════════════════════════════════════════════════════════════════

#include "property_runner.h"

#include <crucible/canopy/Swim.h>

#include <array>
#include <cstdint>
#include <limits>

namespace {

namespace cc = crucible::canopy;
using crucible::fuzz::prop::Rng;

inline constexpr std::uint32_t kPeers = 4;
inline constexpr std::uint32_t kMaxEvents = 16;
inline constexpr std::uint64_t kIncMax = std::numeric_limits<std::uint64_t>::max();

// Independent oracle for SWIM state precedence (Das-Gupta-Motivala +
// Lifeguard): Alive < Suspect < Dead.  Re-derived here, NOT taken from
// the private Swim::state_rank_.
[[nodiscard]] std::uint8_t rank_of(cc::SwimState s) noexcept {
    switch (s) {
        case cc::SwimState::Alive:   return 0;
        case cc::SwimState::Suspect: return 1;
        case cc::SwimState::Dead:    return 2;
        default:                     return 2;
    }
}

struct EventSpec {
    std::uint8_t peer_idx = 0;     // [0, kPeers)
    std::uint8_t state = 0;        // 0=Alive 1=Suspect 2=Dead
    std::uint64_t incarnation = 0;
    std::uint32_t consecutive_misses = 0;
};

struct Spec {
    std::array<EventSpec, kMaxEvents> events{};
    std::uint32_t count = 0;
};

[[nodiscard]] std::uint64_t gen_inc(Rng& rng) noexcept {
    switch (rng.next_below(6u)) {
        case 0: return 0u;
        case 1: return 1u;
        case 2: return kIncMax;
        case 3: return kIncMax - 1u;
        // Small cluster — drives equal-incarnation state competition.
        default: return rng.next_below(4u);
    }
}

[[nodiscard]] cc::SwimState state_of(std::uint8_t raw) noexcept {
    switch (raw % 3u) {
        case 0: return cc::SwimState::Alive;
        case 1: return cc::SwimState::Suspect;
        default: return cc::SwimState::Dead;
    }
}

[[nodiscard]] crucible::cog::CogIdentity identity_of(std::uint8_t idx) noexcept {
    crucible::cog::CogIdentity id{};
    // Non-zero UUID (apply_gossip rejects zero); distinct per peer index.
    id.uuid = crucible::cog::Uuid{static_cast<std::uint64_t>(idx) + 1u,
                                  static_cast<std::uint64_t>(idx) + 101u};
    return id;
}

struct PeerRef {
    bool present = false;
    std::uint64_t inc = 0;
    std::uint8_t rank = 0;
};

}  // namespace

int main(int argc, char** argv) {
    using namespace crucible::fuzz::prop;

    Config cfg = parse_args(argc, argv);
    if (cfg.iterations > 2'000'000) cfg.iterations = 2'000'000;

    return run("swim_membership", cfg,
        [](Rng& rng) noexcept -> Spec {
            Spec spec{};
            spec.count = 1u + rng.next_below(kMaxEvents - 1u);
            for (std::uint32_t i = 0; i < spec.count; ++i) {
                spec.events[i] = EventSpec{
                    .peer_idx = static_cast<std::uint8_t>(rng.next_below(kPeers)),
                    .state = static_cast<std::uint8_t>(rng.next_below(3u)),
                    .incarnation = gen_inc(rng),
                    .consecutive_misses = rng.next_below(4u),
                };
            }
            return spec;
        },
        [](const Spec& spec) noexcept -> bool {
            cc::SwimMembership<8> membership;
            std::array<PeerRef, kPeers> refs{};

            for (std::uint32_t i = 0; i < spec.count; ++i) {
                const EventSpec& e = spec.events[i];
                const cc::SwimState in_state = state_of(e.state);
                const std::uint8_t in_rank = rank_of(in_state);
                const auto id = identity_of(e.peer_idx);

                const cc::SwimEvent se{
                    .peer = id,
                    .state = in_state,
                    .consecutive_misses = e.consecutive_misses,
                    .incarnation = e.incarnation,
                    .sequence = 0,
                };
                // Peer UUID is non-zero and the table (8) never fills with
                // only 4 peers, so the apply must always succeed.
                if (!membership.apply_gossip(
                        cc::GossipedSwimEvent{se},
                        static_cast<std::uint64_t>(i) * 1000u).has_value()) {
                    return false;
                }

                const cc::PeerHealth ph =
                    membership.health(id.uuid).peek();
                const std::uint8_t cur_rank = rank_of(ph.state);
                PeerRef& ref = refs[e.peer_idx];

                if (!ref.present) {
                    // First sighting auto-adds the peer verbatim.
                    if (ph.incarnation != e.incarnation || ph.state != in_state) {
                        return false;
                    }
                } else {
                    // Non-regression: (inc, rank) lexicographically >= prior.
                    if (ph.incarnation < ref.inc) return false;
                    if (ph.incarnation == ref.inc && cur_rank < ref.rank) return false;

                    // Override iff the incoming is strictly newer (lex).
                    const bool incoming_newer =
                        (e.incarnation > ref.inc) ||
                        (e.incarnation == ref.inc && in_rank > ref.rank);
                    if (incoming_newer) {
                        if (ph.incarnation != e.incarnation || ph.state != in_state) {
                            return false;
                        }
                    } else {
                        if (ph.incarnation != ref.inc || cur_rank != ref.rank) {
                            return false;
                        }
                    }
                }

                ref.present = true;
                ref.inc = ph.incarnation;
                ref.rank = cur_rank;
            }
            return true;
        });
}
