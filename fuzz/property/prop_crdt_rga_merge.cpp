// ═══════════════════════════════════════════════════════════════════
// prop_crdt_rga_merge.cpp — merge semilattice-law + tie-break fuzzer for
// the canopy RGA sequence CRDT (canopy/Crdt.h RgaList).
//
// prop_crdt_rga covers materialize() (the document-order traversal).
// This covers the OTHER half: the state merge that makes RGA a CRDT.
// RgaList::merge unions the node sets by id; when both replicas hold a
// node with the same id, it keeps the one whose (after, value) is
// lexicographically smaller and OR-s the tombstone flag — a per-id
// join.  If that tie-break inverts, or the tombstone isn't monotone, two
// replicas that received the same edits in a different order keep
// different (after, value) for an element and render divergent
// documents forever.  These laws are the entire convergence contract;
// test_crdt.cpp pins hand-picked cases, no property fuzzer existed.
//
// States are built through the production insert_after / erase path
// (each id inserted once per state → unique ids; `after` is arbitrary
// since merge operates on the node set, not the tree).  The INDEPENDENT
// oracle models each state as a per-id map {after, value, tombstone} and
// merges two maps with the exact documented rule — per id present in
// either side: take the (after,value) of the side with the smaller
// (after,value) pair (ties keep the base/left), tombstone = OR — a
// formulation separate from production's upsert-into loop.  Per (A,B,C)
// it asserts:
//   * build faithfulness: each built state's node set equals its spec map
//   * merge oracle: node-set of RgaList::merge(A,B) equals the map merge
//   * commutativity:  merge(A,B) ≡ merge(B,A)
//   * idempotence:    merge(A,A) ≡ A
//   * associativity:  merge(merge(A,B),C) ≡ merge(A,merge(B,C))
//
// 6-id universe, after in [0,5), value in [0,4), ~25% tombstoned, ~60%
// present per side — so shared ids (the tie-break), one-sided ids, and
// tombstone-OR all fire densely; the union stays well under Capacity.
// ═══════════════════════════════════════════════════════════════════

#include "property_runner.h"

#include <crucible/canopy/Crdt.h>

#include <array>
#include <cstddef>
#include <cstdint>

namespace {

namespace cc = crucible::canopy;
using crucible::fuzz::prop::Rng;

inline constexpr std::uint32_t kIds = 6;       // ids 1..6
inline constexpr std::size_t kSlots = kIds + 1;  // index by id; slot 0 unused

using Rga = cc::RgaList<std::uint32_t, std::uint32_t, 16>;
using State = Rga::state_type;
using Insert = Rga::insert_type;
using LocalInsert = Rga::local_insert_type;
using LocalErase = Rga::local_erase_type;

// Per-id node model for the independent oracle.
struct Node {
    std::uint32_t after = 0;
    std::uint32_t value = 0;
    bool tombstone = false;
    bool present = false;
};
using Map = std::array<Node, kSlots>;

struct CellSpec {
    std::uint8_t present = 0;
    std::uint8_t after = 0;
    std::uint8_t value = 0;
    std::uint8_t tombstone = 0;
};
struct Spec {
    std::array<CellSpec, 3u * kIds> cells{};  // [side*kIds + (id-1)]
};

[[nodiscard]] const CellSpec& cell(const Spec& s, std::uint32_t side, std::uint32_t id) noexcept {
    return s.cells[side * kIds + (id - 1u)];
}

// Build the spec's per-id map for one side.
[[nodiscard]] Map map_from_spec(const Spec& s, std::uint32_t side) noexcept {
    Map m{};
    for (std::uint32_t id = 1; id <= kIds; ++id) {
        const CellSpec& c = cell(s, side, id);
        if (c.present != 0u) {
            m[id] = Node{c.after, c.value, c.tombstone != 0u, true};
        }
    }
    return m;
}

// Build a production state by inserting present nodes (unique ids) then
// erasing the tombstoned ones — exercises insert_after + erase + state().
[[nodiscard]] State build_state(const Spec& s, std::uint32_t side) noexcept {
    Rga rga;
    for (std::uint32_t id = 1; id <= kIds; ++id) {
        const CellSpec& c = cell(s, side, id);
        if (c.present == 0u) continue;
        if (!rga.insert_after(LocalInsert{Insert{
                .id = id,
                .after = static_cast<std::uint32_t>(c.after),
                .value = static_cast<std::uint32_t>(c.value),
            }})) {
            return State{};
        }
        if (c.tombstone != 0u) {
            (void)rga.erase(LocalErase{id});
        }
    }
    return rga.state();
}

[[nodiscard]] Map map_from_state(const State& s) noexcept {
    Map m{};
    for (std::uint16_t i = 0; i < s.count; ++i) {
        const auto& e = s.entries[static_cast<std::size_t>(i)];
        if (e.id >= 1u && e.id <= kIds) {
            m[e.id] = Node{e.after, e.value, e.tombstone, true};
        }
    }
    return m;
}

// Independent per-id merge: smaller (after,value) wins (ties keep a),
// tombstone OR.
[[nodiscard]] Map oracle_merge(const Map& a, const Map& b) noexcept {
    Map m{};
    for (std::uint32_t id = 1; id <= kIds; ++id) {
        const Node& na = a[id];
        const Node& nb = b[id];
        if (na.present && nb.present) {
            const bool b_smaller =
                nb.after < na.after ||
                (nb.after == na.after && nb.value < na.value);
            m[id] = Node{
                b_smaller ? nb.after : na.after,
                b_smaller ? nb.value : na.value,
                na.tombstone || nb.tombstone,
                true,
            };
        } else if (na.present) {
            m[id] = na;
        } else if (nb.present) {
            m[id] = nb;
        }
    }
    return m;
}

[[nodiscard]] bool map_eq(const Map& a, const Map& b) noexcept {
    for (std::uint32_t id = 1; id <= kIds; ++id) {
        if (a[id].present != b[id].present) return false;
        if (!a[id].present) continue;
        if (a[id].after != b[id].after) return false;
        if (a[id].value != b[id].value) return false;
        if (a[id].tombstone != b[id].tombstone) return false;
    }
    return true;
}

}  // namespace

int main(int argc, char** argv) {
    using namespace crucible::fuzz::prop;

    Config cfg = parse_args(argc, argv);
    if (cfg.iterations > 2'000'000) cfg.iterations = 2'000'000;

    return run("crdt_rga_merge", cfg,
        [](Rng& rng) noexcept -> Spec {
            Spec spec{};
            for (std::uint32_t i = 0; i < 3u * kIds; ++i) {
                spec.cells[i] = CellSpec{
                    .present = static_cast<std::uint8_t>(rng.next_below(5u) < 3u ? 1u : 0u),
                    .after = static_cast<std::uint8_t>(rng.next_below(5u)),
                    .value = static_cast<std::uint8_t>(rng.next_below(4u)),
                    .tombstone = static_cast<std::uint8_t>(rng.next_below(4u) == 0u ? 1u : 0u),
                };
            }
            return spec;
        },
        [](const Spec& spec) noexcept -> bool {
            const State sa = build_state(spec, 0u);
            const State sb = build_state(spec, 1u);
            const State sc = build_state(spec, 2u);

            const Map ma = map_from_spec(spec, 0u);
            const Map mb = map_from_spec(spec, 1u);
            const Map mc = map_from_spec(spec, 2u);

            // Build faithfulness: each state matches its spec map.
            if (!map_eq(map_from_state(sa), ma)) return false;
            if (!map_eq(map_from_state(sb), mb)) return false;
            if (!map_eq(map_from_state(sc), mc)) return false;

            const State ab = Rga::merge(sa, sb);

            // Merge oracle.
            if (!map_eq(map_from_state(ab), oracle_merge(ma, mb))) return false;

            // Commutativity.
            if (!map_eq(map_from_state(ab), map_from_state(Rga::merge(sb, sa)))) {
                return false;
            }
            // Idempotence.
            if (!map_eq(map_from_state(Rga::merge(sa, sa)), ma)) return false;
            // Associativity.
            const State left = Rga::merge(Rga::merge(sa, sb), sc);
            const State right = Rga::merge(sa, Rga::merge(sb, sc));
            if (!map_eq(map_from_state(left), map_from_state(right))) return false;

            return true;
        });
}
