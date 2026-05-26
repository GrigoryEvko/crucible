// ═══════════════════════════════════════════════════════════════════
// prop_crdt_orset.cpp — semilattice-law + add-wins fuzzer for the canopy
// observed-remove set CRDT (canopy/Crdt.h OrSet / BoundedTaggedState).
//
// OrSet is the harder, more-bug-prone sibling of GSet: every element
// carries a set of unique add-tags, remove tombstones the currently
// observed tags, and merge unions the (value,tag) entries OR-ing the
// removed flag per key — so a concurrent add (new tag) wins over a
// concurrent remove (add-wins).  Canopy replicas converge only if this
// merge is a join-semilattice op (commutative / associative /
// idempotent / LUB); a single asymmetry or a dropped tag splits the
// fleet's gossip state permanently.  test_crdt.cpp pins hand-picked
// cases; no property fuzzer existed.
//
// The state is modelled INDEPENDENTLY as a 16-key universe — value ∈
// [0,4) × tag ∈ [0,4) — with a present-mask and a removed-mask (a
// completely different representation from the open OrSetEntry array
// under test).  The merge oracle is the exact upsert rule: merged
// present = pa|pb; merged removed per key = (present_a ∧ removed_a) ∨
// (present_b ∧ removed_b).  BoundedTaggedState has NO operator==, so
// state equality is defined here as canonical-key-set equivalence
// (count + per-(value,tag) removed match), which correctly ignores the
// arbitrary internal append order that merge(a,b) vs merge(b,a) produce.
//
// Per (a,b,c) it asserts:
//   * merge oracle: OrSet::merge(a,b) matches the mask-level upsert for
//     EVERY key (present⇔mask, removed flag exact, absent stays absent)
//   * add-wins visibility: a value is visible iff some tag is present &
//     not removed — so a fresh add survives a concurrent remove
//   * commutativity:  merge(a,b) ≡ merge(b,a)
//   * idempotence:    merge(a,a) ≡ a
//   * associativity:  merge(merge(a,b),c) ≡ merge(a,merge(b,c))
//
// Universe is 16 keys; Capacity is OrSet's default 128 > 16, so merge
// never overflows (the only path returning the left operand instead of
// the union).  The laws hold across the domain; the convergence-safety
// net the unit test lacked, now for the add-wins CRDT too.
// ═══════════════════════════════════════════════════════════════════

#include "property_runner.h"

#include <crucible/canopy/Crdt.h>

#include <cstdint>
#include <optional>

namespace {

namespace cc = crucible::canopy;
using crucible::fuzz::prop::Rng;

using OS = cc::OrSet<std::uint32_t, std::uint32_t>;  // Capacity defaults to 128
using State = OS::state_type;
using Entry = OS::entry_type;

inline constexpr std::uint32_t kKeys = 16;  // value[0,4) x tag[0,4); < 128
inline constexpr std::uint32_t kKeyMask = (1u << kKeys) - 1u;

[[nodiscard]] std::uint32_t key_value(std::uint32_t k) noexcept { return k >> 2; }
[[nodiscard]] std::uint32_t key_tag(std::uint32_t k) noexcept { return k & 3u; }

struct Spec {
    std::uint32_t pa = 0, ra = 0;  // present / removed masks for a
    std::uint32_t pb = 0, rb = 0;
    std::uint32_t pc = 0, rc = 0;
};

[[nodiscard]] std::uint32_t gen_mask(Rng& rng) noexcept {
    switch (rng.next_below(5u)) {
        case 0: return 0u;
        case 1: return kKeyMask;
        case 2: return (1u << rng.next_below(kKeys)) & kKeyMask;  // single key
        case 3: return rng.next32() & rng.next32() & kKeyMask;    // sparse
        default: return rng.next32() & kKeyMask;
    }
}

// Build a canonical state (unique (value,tag) keys) from masks.
[[nodiscard]] State build(std::uint32_t present, std::uint32_t removed) noexcept {
    State s{};
    for (std::uint32_t k = 0; k < kKeys; ++k) {
        if ((present >> k) & 1u) {
            s.entries[s.count] = Entry{
                .value = key_value(k),
                .tag = key_tag(k),
                .removed = ((removed >> k) & 1u) != 0u,
            };
            ++s.count;
        }
    }
    return s;
}

// Look up a (value,tag) key in a state → its removed flag, or nullopt if absent.
[[nodiscard]] std::optional<bool>
lookup(State const& s, std::uint32_t value, std::uint32_t tag) noexcept {
    for (std::uint16_t i = 0; i < s.count; ++i) {
        if (s.entries[i].value == value && s.entries[i].tag == tag) {
            return s.entries[i].removed;
        }
    }
    return std::nullopt;
}

// Canonical-key-set equivalence (BoundedTaggedState has no operator==).
[[nodiscard]] bool equiv(State const& a, State const& b) noexcept {
    if (a.count != b.count) return false;
    for (std::uint16_t i = 0; i < a.count; ++i) {
        const auto rb = lookup(b, a.entries[i].value, a.entries[i].tag);
        if (!rb || *rb != a.entries[i].removed) return false;
    }
    return true;
}

// A value is visible iff some tag is present and not removed (add-wins).
[[nodiscard]] bool visible(State const& s, std::uint32_t value) noexcept {
    for (std::uint16_t i = 0; i < s.count; ++i) {
        if (s.entries[i].value == value && !s.entries[i].removed) return true;
    }
    return false;
}

}  // namespace

int main(int argc, char** argv) {
    using namespace crucible::fuzz::prop;

    Config cfg = parse_args(argc, argv);
    if (cfg.iterations > 2'000'000) cfg.iterations = 2'000'000;

    return run("crdt_orset", cfg,
        [](Rng& rng) noexcept -> Spec {
            return Spec{gen_mask(rng), gen_mask(rng), gen_mask(rng),
                        gen_mask(rng), gen_mask(rng), gen_mask(rng)};
        },
        [](const Spec& spec) noexcept -> bool {
            const State a = build(spec.pa, spec.ra);
            const State b = build(spec.pb, spec.rb);
            const State c = build(spec.pc, spec.rc);

            const State ab = OS::merge(a, b);

            // ── independent mask-level merge oracle ──
            const std::uint32_t mp = spec.pa | spec.pb;
            for (std::uint32_t k = 0; k < kKeys; ++k) {
                const bool want_present = ((mp >> k) & 1u) != 0u;
                const bool want_removed =
                    (((spec.pa >> k) & (spec.ra >> k) & 1u) != 0u) ||
                    (((spec.pb >> k) & (spec.rb >> k) & 1u) != 0u);
                const auto got = lookup(ab, key_value(k), key_tag(k));
                if (want_present) {
                    if (!got || *got != want_removed) return false;
                } else if (got) {
                    return false;  // absent key must not appear
                }
            }

            // ── add-wins visibility ──
            for (std::uint32_t v = 0; v < 4u; ++v) {
                bool want_visible = false;
                for (std::uint32_t t = 0; t < 4u; ++t) {
                    const std::uint32_t k = (v << 2) | t;
                    const bool present = ((mp >> k) & 1u) != 0u;
                    const bool removed =
                        (((spec.pa >> k) & (spec.ra >> k) & 1u) != 0u) ||
                        (((spec.pb >> k) & (spec.rb >> k) & 1u) != 0u);
                    if (present && !removed) want_visible = true;
                }
                if (visible(ab, v) != want_visible) return false;
            }

            // ── semilattice laws (set-equivalence) ──
            if (!equiv(ab, OS::merge(b, a))) return false;            // commutativity
            if (!equiv(OS::merge(a, a), a)) return false;             // idempotence
            const State left = OS::merge(OS::merge(a, b), c);
            const State right = OS::merge(a, OS::merge(b, c));
            if (!equiv(left, right)) return false;                    // associativity

            return true;
        });
}
