// ═══════════════════════════════════════════════════════════════════
// prop_scuttlebutt_compare.cpp — reconciliation-diff fuzzer for the
// canopy Scuttlebutt anti-entropy compare (ScuttlebuttSync::compare_digest).
//
// compare_digest is the heart of canopy's gossip reconciliation: given a
// remote peer's digest (its claimed (origin,key)→version map), it
// produces a ScuttlebuttDiff{requests, offers} —
//   • requests: cells where the REMOTE version is newer than mine (I
//     fetch from the peer), built by walking the remote digest entries
//     and keeping those with entry.version > my local_version;
//   • offers:   cells where MY version is newer than the remote's (I
//     push to the peer), built by walking my whole version matrix and
//     keeping cells where version_in_digest(remote) < my local_version
//     (absent-in-remote counts as version 0 → always offered).
// If the request/offer split inverts a comparison, drops a cell, or
// mishandles the absent-as-zero rule, two replicas exchange the wrong
// deltas and the fleet's CRDT state diverges silently.  prop_scuttlebutt_
// digest covers the push layer; this covers the compare layer.  No
// property fuzzer existed for either before this pass.
//
// The version matrix is built through the PRODUCTION public API
// (apply_delta populates any peer's row when the incoming version is
// newer; GCounter merge always succeeds), and the remote digest through
// push — but the INDEPENDENT oracle compares against two matrices I
// track myself (mat = my local versions, rmat = remote digest versions),
// NOT production's versions_/digest().  Per (mat, rmat) it asserts:
//   • compare_digest succeeds (all peers/keys registered, digest
//     well-formed → no error path)
//   • requests set == { (p,k) : rmat>0 ∧ rmat>mat }, each at version rmat
//   • offers   set == { (p,k) : mat>0  ∧ rmat<mat }, each at version mat
// (request and offer are mutually exclusive by the strict < / > split.)
//
// 3 peers × 3 keys; versions drawn from [1,4] so the equal / newer /
// older crossings and the present/absent combinations all fire densely.
// ═══════════════════════════════════════════════════════════════════

#include "property_runner.h"

#include <crucible/canopy/Crdt.h>
#include <crucible/canopy/Scuttlebutt.h>
#include <crucible/canopy/Swim.h>
#include <crucible/cog/CogIdentity.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>

namespace {

namespace cc = crucible::canopy;
using crucible::fuzz::prop::Rng;
using Uuid = crucible::cog::Uuid;
using CogId = crucible::cog::CogIdentity;

inline constexpr std::uint32_t kPeers = 3;
inline constexpr std::uint32_t kKeys = 3;
inline constexpr std::uint32_t kCells = kPeers * kKeys;

using Sync = cc::ScuttlebuttSync<kPeers, kKeys>;
using GC = cc::GCounter<2>;
using GCState = GC::state_type;
using Key = cc::ScuttlebuttKey;
using Entry = cc::ScuttlebuttVersionEntry;
using Digest = cc::ScuttlebuttDigest<kPeers, kKeys>;
using GDelta = cc::GossipedScuttlebuttDelta<GCState>;
using GDigest = cc::GossipedScuttlebuttDigest<kPeers, kKeys>;
using LKey = cc::LocalScuttlebuttKey;
using Peer = cc::SwimPeer;

[[nodiscard]] Uuid peer_uuid(std::uint32_t p) noexcept {
    return Uuid{static_cast<std::uint64_t>(p) + 1u,
                static_cast<std::uint64_t>(p) + 101u};
}
[[nodiscard]] Peer make_peer(std::uint32_t p) noexcept {
    CogId id{};
    id.uuid = peer_uuid(p);
    return cc::admit_swim_peer(id);
}
[[nodiscard]] Key make_key(std::uint32_t k) noexcept {
    return Key{.hash = static_cast<std::uint64_t>(k) + 1u, .length = 1u};
}

struct CellSpec {
    std::uint8_t mat_present = 0;
    std::uint8_t mat_version = 1;   // [1,4]
    std::uint8_t remote_present = 0;
    std::uint8_t remote_version = 1;
};
struct Spec {
    std::array<CellSpec, kCells> cells{};
};

}  // namespace

int main(int argc, char** argv) {
    using namespace crucible::fuzz::prop;

    Config cfg = parse_args(argc, argv);
    if (cfg.iterations > 2'000'000) cfg.iterations = 2'000'000;

    return run("scuttlebutt_compare", cfg,
        [](Rng& rng) noexcept -> Spec {
            Spec spec{};
            for (std::uint32_t i = 0; i < kCells; ++i) {
                spec.cells[i] = CellSpec{
                    .mat_present = static_cast<std::uint8_t>(rng.next_below(2u)),
                    .mat_version = static_cast<std::uint8_t>(1u + rng.next_below(4u)),
                    .remote_present = static_cast<std::uint8_t>(rng.next_below(2u)),
                    .remote_version = static_cast<std::uint8_t>(1u + rng.next_below(4u)),
                };
            }
            return spec;
        },
        [](const Spec& spec) noexcept -> bool {
            // Build the sync with 3 peers (index 0 = local).
            std::array<Peer, 2> initial{make_peer(1), make_peer(2)};
            Sync sync{make_peer(0), std::span<const Peer>{initial}};

            GC gc{};
            for (std::uint32_t k = 0; k < kKeys; ++k) {
                if (!sync.register_state<GC>(LKey{make_key(k)}, gc).has_value()) {
                    return false;
                }
            }

            // Independently-tracked matrices: mat = local versions,
            // rmat = remote-digest versions (0 = absent).
            std::array<std::uint64_t, kCells> mat{};
            std::array<std::uint64_t, kCells> rmat{};
            Digest remote{};

            for (std::uint32_t p = 0; p < kPeers; ++p) {
                for (std::uint32_t k = 0; k < kKeys; ++k) {
                    const CellSpec& c = spec.cells[p * kKeys + k];
                    if (c.mat_present != 0u) {
                        const std::uint64_t version = c.mat_version;
                        GDelta delta{cc::ScuttlebuttDelta<GCState>{
                            .origin = peer_uuid(p),
                            .key = make_key(k),
                            .version = version,
                            .state = gc.state(),
                        }};
                        // Fresh cell (current 0) + version>0 → must set.
                        const auto applied = sync.apply_delta<GC>(delta, gc);
                        if (!applied.has_value() || !*applied) return false;
                        mat[p * kKeys + k] = version;
                    }
                    if (c.remote_present != 0u) {
                        const std::uint64_t version = c.remote_version;
                        (void)remote.push(Entry{
                            .origin = peer_uuid(p),
                            .key = make_key(k),
                            .version = version,
                        });
                        rmat[p * kKeys + k] = version;
                    }
                }
            }

            const auto diff = sync.compare_digest(GDigest{remote});
            if (!diff.has_value()) return false;   // no error path is reachable here

            // Reconstruct (p,k) from a result entry and verify it is an
            // expected cell at the expected version.
            const auto reconstruct_ok =
                [&](const Entry& e, std::uint32_t& p, std::uint32_t& k) noexcept -> bool {
                    if (e.origin.hi < 1u || e.origin.hi > kPeers) return false;
                    if (e.key.hash < 1u || e.key.hash > kKeys) return false;
                    p = static_cast<std::uint32_t>(e.origin.hi - 1u);
                    k = static_cast<std::uint32_t>(e.key.hash - 1u);
                    return e.origin == peer_uuid(p) && e.key == make_key(k);
                };

            // ── requests: rmat>0 ∧ rmat>mat, carried at version rmat ──
            std::size_t want_req = 0;
            for (std::uint32_t i = 0; i < kCells; ++i) {
                if (rmat[i] != 0u && rmat[i] > mat[i]) ++want_req;
            }
            if (static_cast<std::size_t>(diff->requests.count) != want_req) return false;
            for (std::uint16_t i = 0; i < diff->requests.count; ++i) {
                const Entry& e = diff->requests.entries[static_cast<std::size_t>(i)];
                std::uint32_t p = 0;
                std::uint32_t k = 0;
                if (!reconstruct_ok(e, p, k)) return false;
                const std::size_t idx = p * kKeys + k;
                if (!(rmat[idx] != 0u && rmat[idx] > mat[idx])) return false;
                if (e.version != rmat[idx]) return false;
            }

            // ── offers: mat>0 ∧ rmat<mat, carried at version mat ──
            std::size_t want_off = 0;
            for (std::uint32_t i = 0; i < kCells; ++i) {
                if (mat[i] != 0u && rmat[i] < mat[i]) ++want_off;
            }
            if (static_cast<std::size_t>(diff->offers.count) != want_off) return false;
            for (std::uint16_t i = 0; i < diff->offers.count; ++i) {
                const Entry& e = diff->offers.entries[static_cast<std::size_t>(i)];
                std::uint32_t p = 0;
                std::uint32_t k = 0;
                if (!reconstruct_ok(e, p, k)) return false;
                const std::size_t idx = p * kKeys + k;
                if (!(mat[idx] != 0u && rmat[idx] < mat[idx])) return false;
                if (e.version != mat[idx]) return false;
            }

            return true;
        });
}
