// ═══════════════════════════════════════════════════════════════════
// prop_crdt_rga.cpp — materialize-order fuzzer for the canopy RGA
// (Replicated Growable Array) sequence CRDT (canopy/Crdt.h RgaList).
//
// RGA is the hardest canopy CRDT: an ordered, collaboratively-edited
// sequence.  Each element is a node {id, after, value, tombstone}; the
// visible list is produced by materialize(), a preorder DFS rooted at
// the sentinel id 0 — among the unvisited nodes whose `after` equals the
// current parent, it visits the SMALLEST id first, emits the value
// (unless tombstoned), then recurses into that node's subtree before the
// next sibling.  This traversal IS the document order; if it picks
// siblings in the wrong order, skips a live element, or emits a
// tombstoned one, every replica renders a different document.
// test_crdt.cpp pins hand-picked cases; no property fuzzer existed, and
// the other six canopy CRDTs (sets / register / counters / multi-value)
// are already fuzzed — this closes the sequence CRDT.
//
// The list is built through the production insert_after / erase path
// (well-formed trees only: every node's `after` is the root or an
// already-inserted node with a smaller id, ids unique — exactly what
// insert_after produces in practice, so no orphan/cycle corner).  The
// INDEPENDENT oracle re-derives the document order a different way: for
// each node it extracts the root→node PATH of ids (walking the `after`
// chain up to 0), sorts all nodes lexicographically by that path key,
// and emits the non-tombstoned values in that order.  Lexicographic
// order of root-paths is provably the same preorder the recursive DFS
// produces (ancestors are prefixes; siblings diverge at their own id),
// but the computation — path extraction + lex sort — shares nothing with
// production's visited-guarded recursion, so a divergence is a real bug.
// Per generated list it asserts materialize()'s value sequence equals
// the oracle's exactly (length + element-by-element).
//
// Capacity 16; up to 8 nodes; values in [0,5); ~1/3 of nodes attach to
// the root and ~1/4 are tombstoned so deep chains, wide fan-out, and
// tombstone-skip-but-traverse-subtree all fire densely.
// ═══════════════════════════════════════════════════════════════════

#include "property_runner.h"

#include <crucible/canopy/Crdt.h>

#include <array>
#include <cstddef>
#include <cstdint>

namespace {

namespace cc = crucible::canopy;
using crucible::fuzz::prop::Rng;

inline constexpr std::size_t kCapacity = 16;
inline constexpr std::uint32_t kMaxNodes = 8;

using Rga = cc::RgaList<std::uint32_t, std::uint32_t, kCapacity>;
using Insert = Rga::insert_type;
using LocalInsert = Rga::local_insert_type;
using LocalErase = Rga::local_erase_type;

struct NodeSpec {
    std::uint32_t after = 0;   // 0 = root, else a prior node's id
    std::uint32_t value = 0;   // [0,5)
    std::uint8_t tombstone = 0;
};
struct Spec {
    std::uint32_t count = 0;   // [1, kMaxNodes]
    std::array<NodeSpec, kMaxNodes> nodes{};
};

// ── independent oracle: lexicographic root-path preorder ──
//
// Node i has id i+1; its `after` is 0 (root) or a prior id (< i+1), so
// the after-chain strictly decreases and terminates at the root with no
// cycle.  Sorting nodes by their root→node path key reproduces the
// preorder DFS (different computation from RgaList's recursion).

struct OracleResult {
    std::array<std::uint32_t, kMaxNodes> values{};
    std::size_t count = 0;
};

[[nodiscard]] OracleResult oracle_materialize(const Spec& spec) noexcept {
    const std::uint32_t n = spec.count;

    // path[i] = root→node-i id sequence (root excluded), plen[i] its length.
    std::array<std::array<std::uint32_t, kMaxNodes>, kMaxNodes> path{};
    std::array<std::size_t, kMaxNodes> plen{};
    for (std::uint32_t i = 0; i < n; ++i) {
        // Collect ids walking up: [self, parent, ..., top]; then reverse.
        std::array<std::uint32_t, kMaxNodes> up{};
        std::size_t len = 0;
        std::uint32_t cur = i;  // index of node with id cur+1
        for (;;) {
            up[len] = cur + 1u;  // this node's id
            ++len;
            const std::uint32_t after = spec.nodes[cur].after;
            if (after == 0u) break;
            cur = after - 1u;    // parent index (after is a prior id ≥ 1)
        }
        for (std::size_t j = 0; j < len; ++j) {
            path[i][j] = up[len - 1u - j];   // reverse → root→node
        }
        plen[i] = len;
    }

    // Lexicographic compare of two path keys.
    const auto lex_less = [&](std::uint32_t a, std::uint32_t b) noexcept -> bool {
        const std::size_t la = plen[a];
        const std::size_t lb = plen[b];
        const std::size_t m = la < lb ? la : lb;
        for (std::size_t j = 0; j < m; ++j) {
            if (path[a][j] != path[b][j]) return path[a][j] < path[b][j];
        }
        return la < lb;
    };

    // Insertion sort of node indices by path key.
    std::array<std::uint32_t, kMaxNodes> order{};
    for (std::uint32_t i = 0; i < n; ++i) order[i] = i;
    for (std::uint32_t i = 1; i < n; ++i) {
        const std::uint32_t key = order[i];
        std::uint32_t j = i;
        while (j > 0 && lex_less(key, order[j - 1u])) {
            order[j] = order[j - 1u];
            --j;
        }
        order[j] = key;
    }

    OracleResult out{};
    for (std::uint32_t i = 0; i < n; ++i) {
        const std::uint32_t idx = order[i];
        if (spec.nodes[idx].tombstone == 0u) {
            out.values[out.count] = spec.nodes[idx].value;
            ++out.count;
        }
    }
    return out;
}

}  // namespace

int main(int argc, char** argv) {
    using namespace crucible::fuzz::prop;

    Config cfg = parse_args(argc, argv);
    if (cfg.iterations > 2'000'000) cfg.iterations = 2'000'000;

    return run("crdt_rga", cfg,
        [](Rng& rng) noexcept -> Spec {
            Spec spec{};
            spec.count = 1u + rng.next_below(kMaxNodes);  // 1..8
            for (std::uint32_t i = 0; i < spec.count; ++i) {
                const std::uint32_t after =
                    (i == 0u || rng.next_below(3u) == 0u)
                        ? 0u
                        : 1u + rng.next_below(i);   // a prior id in [1, i]
                spec.nodes[i] = NodeSpec{
                    .after = after,
                    .value = rng.next_below(5u),
                    .tombstone = static_cast<std::uint8_t>(
                        rng.next_below(4u) == 0u ? 1u : 0u),
                };
            }
            return spec;
        },
        [](const Spec& spec) noexcept -> bool {
            Rga rga;
            // Insert in id order (i → id i+1); `after` always already present.
            for (std::uint32_t i = 0; i < spec.count; ++i) {
                if (!rga.insert_after(LocalInsert{Insert{
                        .id = i + 1u,
                        .after = spec.nodes[i].after,
                        .value = spec.nodes[i].value,
                    }})) {
                    return false;
                }
            }
            for (std::uint32_t i = 0; i < spec.count; ++i) {
                if (spec.nodes[i].tombstone != 0u) {
                    if (!rga.erase(LocalErase{i + 1u})) return false;
                }
            }

            const auto got = rga.materialize();
            const OracleResult want = oracle_materialize(spec);

            if (static_cast<std::size_t>(got.count) != want.count) return false;
            for (std::size_t i = 0; i < want.count; ++i) {
                if (got.values[i] != want.values[i]) return false;
            }
            return true;
        });
}
