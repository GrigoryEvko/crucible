#pragma once

// ── crucible::topology::TopologyGraph — fleet-wide property graph ──
//
// GAPS-110 (#1209).  Closes the forward declaration of TopologyEdge in
// `cog/CogIdentity.h:107`.  CogIdentity already references the type via
// `std::span<const TopologyEdge> neighbors_l2` and `neighbors_l3`; this
// header provides the definition + the immutable arena-backed
// TopologyGraph carrier that owns the (nodes, edges) span pair.
//
// ── Why "immutable" — the SWMR split ────────────────────────────────
//
// The graph itself (which Cogs exist, which edges connect them) is
// built ONCE at fleet startup by Discovery (GAPS-111) and never
// mutated.  Per-edge LIVE measurements (current RTT, current dropped
// fraction, current congestion state) rotate continuously and live in
// a separate SwmrSession-published side channel that Telemetry
// (GAPS-112) owns.  Splitting the two layers — immutable structural
// graph vs. mutable live measurements — buys us:
//
//   * Cheap shared reads.  Mimic / Augur / Canopy each hold a `const
//     TopologyGraph&` and walk it without any synchronisation cost.
//   * Concentrated mutability.  All telemetry races land in one SwmrSession
//     surface (GAPS-112) instead of being scattered across edge fields.
//   * Federation hashing.  The structural graph is content-addressable
//     via fmix64 over its (nodes, edges) tuple; live measurements
//     (which would change every iteration) do not perturb the cache key.
//
// ── What this header DOES ship ──────────────────────────────────────
//
//   * `EdgeId` strong type (CRUCIBLE_STRONG_ID).
//   * `LinkKind`, `LinkLayer`, `CongestionState` scoped enums with
//     FOUND-I04 frozen ordinals.  Append-only Universe extension
//     applies — every existing value is pinned at the foot.
//   * `link_layer_for(LinkKind)` — consteval projection.
//   * `TopologyEdge` POD with NSDMI on every field; calibrated bandwidth
//     / RTT / drop-rate carry source::Calibrated provenance.
//   * `TopologyGraph` Pinned carrier (no copy/move; fleet-shared
//     identity); accessors `nodes()`, `edges()`, `node_count()`,
//     `edge_count()`, `edge_by_id()`.
//   * `mint_topology_graph<Ctx>(ctx, nodes, edges)` — Universal Mint
//     Pattern §XXI ctx-bound factory.  `CtxFitsTopologyGraph` requires
//     IsExecCtx + Init effect-row presence.
//
// ── What this header DOES NOT ship ──────────────────────────────────
//
//   * Live SWMR measurements (GAPS-112 / topology/Telemetry.h).
//   * Discovery harvest (GAPS-111 / topology/Discovery.h).
//   * Health aggregation (GAPS-113), Pingmesh (GAPS-134), Asymmetric
//     failure detection (GAPS-127), PTP (GAPS-129) — separate GAPS.
//   * NodeId-keyed fast lookup table.  Linear scan is O(n) but the
//     graph is built-once; index lands with GAPS-112 alongside the
//     live measurements that pay for it.
//
// ── Eight axioms ─────────────────────────────────────────────────────
//
//   InitSafe   — every TopologyEdge field has NSDMI + explicit padding;
//                default-constructed edge is the well-defined sentinel
//                (id.is_none(), kind=Unknown, peer=nullptr, all
//                calibrated values 0, state=Healthy).
//   TypeSafe   — EdgeId is a strong ID; LinkKind / LinkLayer /
//                CongestionState are scoped enums with explicit
//                underlying type; bandwidth/RTT/drop_rate carry
//                Tagged<source::Calibrated> provenance.
//   NullSafe   — peer is nullable by design (sentinel for "edge not
//                yet connected"); edge_by_id returns const T* (nullable
//                on miss); accessors that dereference assert.
//   MemSafe    — TopologyGraph copy + move = delete; spans are
//                non-owning views; arena owns the storage externally
//                (Discovery passes the arena-backed buffers in).
//   BorrowSafe — built once, read many; no edge-mutation API on the
//                graph; live mutations live in GAPS-112's separate
//                channel.
//   ThreadSafe — no atomics here; the graph is read-only after mint.
//   LeakSafe   — passive; no resources owned.
//   DetSafe    — link_layer_for is consteval-pure; identical inputs
//                always produce identical CongestionState mappings.
//
// ── Append-only Universe extension (FOUND-I04) ──────────────────────
//
// Existing LinkKind / LinkLayer / CongestionState values are FROZEN.
// Renumbering ANY of them is a federation-cache-key drift event —
// every Cipher checkpoint and federated topology snapshot that
// mentioned the affected atom silently re-keys.  Adding a new atom
// (e.g., LinkKind::Cxl3) MUST land at the next free underlying value
// without disturbing the existing pin lines below.

#include <crucible/Platform.h>
#include <crucible/cog/CogIdentity.h>
#include <crucible/effects/Capabilities.h>
#include <crucible/effects/EffectRow.h>
#include <crucible/effects/ExecCtx.h>
#include <crucible/safety/Tagged.h>

#include <bit>
#include <compare>
#include <cstddef>
#include <cstdint>
#include <meta>
#include <span>
#include <string_view>
#include <type_traits>
#include <utility>

namespace crucible::topology {

// ── EdgeId: strong ID for one edge in the topology graph ────────────
//
// Mirror of CRUCIBLE_STRONG_ID semantics inlined here so the topology
// tree does not pull the macro from include/crucible/Types.h.  The
// inline definition keeps the implementation close to its docstring.
struct EdgeId {
    std::uint32_t value_ = UINT32_MAX;

    constexpr EdgeId() noexcept = default;
    explicit constexpr EdgeId(std::uint32_t v) noexcept : value_{v} {}

    [[nodiscard]] constexpr std::uint32_t raw() const noexcept { return value_; }
    [[nodiscard]] constexpr bool          is_none() const noexcept { return value_ == UINT32_MAX; }
    [[nodiscard]] static constexpr EdgeId none() noexcept { return EdgeId{UINT32_MAX}; }

    constexpr auto operator<=>(EdgeId const&) const = default;
};

static_assert(sizeof(EdgeId) == sizeof(std::uint32_t),
    "EdgeId must collapse to a bare uint32_t at runtime — strong ID is "
    "phantom-typed at compile time only.");
static_assert(std::is_trivially_destructible_v<EdgeId>);
static_assert(std::is_trivially_copyable_v<EdgeId>);

// ── LinkKind: hardware/network link classification ──────────────────
//
// L2 = intra-node hardware (PCIe / NVLink / NVSwitch port / Infinity
// Fabric / CXL.mem / CXL.cache / QPI/UPI / etc.).  L3 = inter-node
// network (Ethernet / Infiniband / RoCEv2 / loopback / etc.).  The
// underlying value classifies whether the kind is L2 (1..15) or L3
// (16..31) by range — an invariant `link_layer_for` exploits to
// project Kind → Layer in O(1) without a switch.
//
// FOUND-I04 frozen ordinals — see the static_assert pin block at the
// foot of this header.  Reserved ranges:
//   * 0       — Unknown sentinel
//   * 1..15   — L2 hardware kinds (8 shipped, 7 reserved for growth)
//   * 16..31  — L3 network kinds (4 shipped, 12 reserved for growth)

enum class LinkKind : std::uint8_t {
    Unknown            =  0,
    // L2 — intra-node hardware
    PciE               =  1,
    NvLink             =  2,
    NvSwitchPort       =  3,
    AmdInfinityFabric  =  4,
    CxlMem             =  5,
    CxlCache           =  6,
    QpiUpi             =  7,
    Cxio               =  8,
    // 9..15 reserved for L2 growth
    // L3 — inter-node network
    Ethernet           = 16,
    Infiniband         = 17,
    RoceV2             = 18,
    Loopback           = 19,
    // 20..31 reserved for L3 growth
};

inline constexpr std::size_t link_kind_count = 13;

[[nodiscard]] constexpr std::string_view
link_kind_name(LinkKind K) noexcept {
    switch (K) {
        case LinkKind::Unknown:           return "Unknown";
        case LinkKind::PciE:              return "PciE";
        case LinkKind::NvLink:            return "NvLink";
        case LinkKind::NvSwitchPort:      return "NvSwitchPort";
        case LinkKind::AmdInfinityFabric: return "AmdInfinityFabric";
        case LinkKind::CxlMem:            return "CxlMem";
        case LinkKind::CxlCache:          return "CxlCache";
        case LinkKind::QpiUpi:            return "QpiUpi";
        case LinkKind::Cxio:              return "Cxio";
        case LinkKind::Ethernet:          return "Ethernet";
        case LinkKind::Infiniband:        return "Infiniband";
        case LinkKind::RoceV2:            return "RoceV2";
        case LinkKind::Loopback:          return "Loopback";
        default:                          return std::string_view{"<unknown LinkKind>"};
    }
}

// ── LinkLayer: L2 (intra-node) vs L3 (inter-node) ───────────────────
//
// Underlying values match the ISO OSI layer numbers (2 = data-link,
// 3 = network) so casting to int produces the natural interpretation.
// FOUND-I04 frozen.

enum class LinkLayer : std::uint8_t {
    Unknown = 0,
    L2      = 2,
    L3      = 3,
};

inline constexpr std::size_t link_layer_count = 3;

[[nodiscard]] constexpr std::string_view
link_layer_name(LinkLayer L) noexcept {
    switch (L) {
        case LinkLayer::Unknown: return "Unknown";
        case LinkLayer::L2:      return "L2";
        case LinkLayer::L3:      return "L3";
        default:                 return std::string_view{"<unknown LinkLayer>"};
    }
}

// O(1) projection — relies on the LinkKind ordinal partition (1..15 = L2,
// 16..31 = L3, 0 = Unknown).  Frozen by the FOUND-I04 pin block; if any
// LinkKind atom drifts across the boundary, the partition is wrong and
// downstream code that assumes layer ⇔ kind-range silently miscategorises.
[[nodiscard]] constexpr LinkLayer
link_layer_for(LinkKind K) noexcept {
    auto raw = static_cast<std::uint8_t>(K);
    if (raw == 0)                         return LinkLayer::Unknown;
    if (raw >= 1  && raw <= 15)           return LinkLayer::L2;
    if (raw >= 16 && raw <= 31)           return LinkLayer::L3;
    return LinkLayer::Unknown;
}

// ── CongestionState: per-edge health bucket ─────────────────────────
//
// Initial value (set by Discovery / Calibrate at mint time) is the
// CALIBRATED baseline — Healthy if the edge passed startup checks,
// Down if it failed.  Live updates rotate via GAPS-112 SWMR.
// FOUND-I04 frozen.

enum class CongestionState : std::uint8_t {
    Healthy   = 0,
    Mild      = 1,
    Severe    = 2,
    Saturated = 3,
    Down      = 4,
};

inline constexpr std::size_t congestion_state_count = 5;

[[nodiscard]] constexpr std::string_view
congestion_state_name(CongestionState C) noexcept {
    switch (C) {
        case CongestionState::Healthy:   return "Healthy";
        case CongestionState::Mild:      return "Mild";
        case CongestionState::Severe:    return "Severe";
        case CongestionState::Saturated: return "Saturated";
        case CongestionState::Down:      return "Down";
        default:                         return std::string_view{"<unknown CongestionState>"};
    }
}

// ── TopologyEdge: typed property bag for one edge ───────────────────
//
// One TopologyEdge entry per directed half-edge.  Half-edges (rather
// than undirected edges) are the canonical encoding so each side can
// observe an asymmetric measurement (e.g., A→B has 200 GB/s but B→A
// only achieves 180 GB/s due to an ECN-throttled queue on B's side).
// Discovery is responsible for pairing the two halves.
//
// Calibrated values carry source::Calibrated provenance — measured at
// startup (or on Calibrate.h re-measurement) by the per-Cog Mimic
// instance for `peer`.  Discovery / Calibrate are the only writers;
// every other consumer reads them via TopologyGraph const&.
//
// `peer == nullptr` is the "edge not yet connected" sentinel — a
// freshly-discovered Cog with no validated peer reachability.
//
// Layout: 64 bytes (one cache line), explicit padding for InitSafe.

struct TopologyEdge {
    EdgeId                                                       id{};                       //  4 B
    LinkKind                                                     kind{LinkKind::Unknown};    //  1 B
    CongestionState                                              state{CongestionState::Healthy}; // 1 B
    std::uint8_t                                                 pad1[2]{};                  //  2 B → align peer to 8B
    cog::CogIdentity const*                                      peer{nullptr};              //  8 B
    safety::Tagged<std::uint64_t, safety::source::Calibrated>    bandwidth_bytes_per_sec{0}; //  8 B
    safety::Tagged<std::uint64_t, safety::source::Calibrated>    rtt_ns_p50{0};              //  8 B
    safety::Tagged<std::uint64_t, safety::source::Calibrated>    rtt_ns_p99{0};              //  8 B
    safety::Tagged<float,         safety::source::Calibrated>    drop_rate{0.0f};            //  4 B
    std::uint8_t                                                 pad2[20]{};                 // 20 B → 64 total
};

static_assert(sizeof(TopologyEdge) == 64,
    "TopologyEdge must be exactly one cache line — adjust pad2 if a "
    "field's storage class changes.");
static_assert(alignof(TopologyEdge) == 8);
static_assert(std::is_trivially_destructible_v<TopologyEdge>,
    "TopologyEdge must be trivially destructible — passive POD.");
static_assert(std::is_trivially_copyable_v<TopologyEdge>,
    "TopologyEdge must be trivially copyable — value semantics for "
    "Discovery to construct in arena-backed buffers without per-edge "
    "constructor invocation.");
static_assert(std::is_standard_layout_v<TopologyEdge>,
    "TopologyEdge must be standard-layout for Cipher serialization.");

// ── CtxFitsTopologyGraph — Universal Mint Pattern fit gate ──────────
//
// Two conjuncts:
//   (1) IsExecCtx<Ctx>                 — must be a typed ExecCtx, not
//                                        a bare int / placeholder type.
//   (2) row_contains<row, Init>        — only Init contexts may build
//                                        the structural graph; once
//                                        built, it is immutable, so no
//                                        background-mutation context
//                                        (Bg) gets the right to mint
//                                        either.
//
// HS14 fixture #1 witnesses rejection on non-Ctx first-arg.
// HS14 fixture #2 witnesses rejection on a Test-row Ctx.

template <class Ctx>
concept CtxFitsTopologyGraph =
       effects::IsExecCtx<Ctx>
    && effects::row_contains_v<effects::row_type_of_t<Ctx>,
                               effects::Effect::Init>;

// ── TopologyGraph — Pinned immutable carrier ────────────────────────
//
// Holds non-owning spans into arena-backed (nodes, edges) buffers.
// Pinned (copy + move = delete) because the graph IS the canonical
// fleet topology — copies would mask staleness, moves would dangle
// any cached const-ref handles already passed to Mimic / Augur /
// Canopy.

class TopologyGraph {
public:
    constexpr TopologyGraph(TopologyGraph const&) = delete;
    constexpr TopologyGraph(TopologyGraph&&) = delete;
    TopologyGraph& operator=(TopologyGraph const&) = delete;
    TopologyGraph& operator=(TopologyGraph&&) = delete;
    ~TopologyGraph() = default;

    [[nodiscard]] constexpr std::span<const cog::CogIdentity>
    nodes() const noexcept { return nodes_; }

    [[nodiscard]] constexpr std::span<const TopologyEdge>
    edges() const noexcept { return edges_; }

    [[nodiscard]] constexpr std::size_t
    node_count() const noexcept { return nodes_.size(); }

    [[nodiscard]] constexpr std::size_t
    edge_count() const noexcept { return edges_.size(); }

    // Linear-scan lookup by EdgeId.  Sentinel `none()` is refused at
    // the precondition — callers that don't know whether they have a
    // valid id should branch on `id.is_none()` themselves before
    // calling.  Returns nullable pointer; nullptr = no match.
    [[nodiscard]] constexpr TopologyEdge const*
    edge_by_id(EdgeId id) const noexcept
        pre (!id.is_none())
    {
        for (auto const& e : edges_) {
            if (e.id == id) return &e;
        }
        return nullptr;
    }

    // Linear-scan lookup of edges incident on a given Cog.  Returns
    // the COUNT of incident edges and writes their pointers into
    // `out` up to `out.size()`; if more incidents exist than fit, the
    // excess is silently dropped (caller is expected to size `out`
    // against `node->neighbors_l2.size() + node->neighbors_l3.size()`
    // when the cached span is up-to-date, or against `edge_count()`
    // when it isn't).  No allocation.
    [[nodiscard]] constexpr std::size_t
    edges_incident_on(cog::CogIdentity const* node,
                      std::span<TopologyEdge const*> out) const noexcept
        pre (node != nullptr)
    {
        std::size_t found = 0;
        std::size_t written = 0;
        for (auto const& e : edges_) {
            if (e.peer == node) {
                if (written < out.size()) {
                    out[written] = &e;
                    ++written;
                }
                ++found;
            }
        }
        return found;
    }

private:
    template <effects::IsExecCtx Ctx>
        requires CtxFitsTopologyGraph<Ctx>
    friend constexpr TopologyGraph
    mint_topology_graph(Ctx const&,
                        std::span<const cog::CogIdentity>,
                        std::span<const TopologyEdge>) noexcept;

    constexpr TopologyGraph(std::span<const cog::CogIdentity> nodes,
                            std::span<const TopologyEdge>     edges) noexcept
        : nodes_{nodes}, edges_{edges} {}

    std::span<const cog::CogIdentity> nodes_{};
    std::span<const TopologyEdge>     edges_{};
};

// ── mint_topology_graph<Ctx>(ctx, nodes, edges) ─────────────────────
//
// Universal Mint Pattern §XXI ctx-bound mint.  Single
// CtxFitsTopologyGraph concept gate.  Pre-conditions enforce the
// data invariants Discovery is responsible for:
//
//   pre (every edge.peer ∈ nodes ∨ edge.peer == nullptr) —
//        an edge that points outside the node set is a dangling
//        reference, almost always a Discovery bug.  We do NOT check
//        this at runtime in the constexpr fast path (linear over
//        every edge × node, O(|E|·|V|)) because pre-clauses with
//        non-trivial cost get stripped under
//        `-fcontract-evaluation-semantic=ignore` on hot TUs.  The
//        runtime smoke test in the sentinel TU exercises the
//        well-formed paths; future GAPS-110-AUDIT-2 may add an
//        opt-in `verify_well_formed` debug helper.
//
//   pre (every edge.id is unique within edges)  —
//        same trade-off, deferred to debug-only sweep.
//
// Returns TopologyGraph by value despite Pinned, because constexpr
// guaranteed-copy-elision (P0135) means the returned prvalue
// constructs in-place at the destination — no actual copy happens.
// The deleted copy/move only forbid LATER copies; the initial
// constructor call site is fine.

template <effects::IsExecCtx Ctx>
    requires CtxFitsTopologyGraph<Ctx>
[[nodiscard]] constexpr TopologyGraph
mint_topology_graph(Ctx const& /* ctx */,
                    std::span<const cog::CogIdentity> nodes,
                    std::span<const TopologyEdge>     edges) noexcept
{
    return TopologyGraph{nodes, edges};
}

}  // namespace crucible::topology

// ────────────────────────────────────────────────────────────────────
// In-header self-test block.  Fires under any TU that includes
// TopologyGraph.h (per feedback_header_only_static_assert_blind_spot:
// the sentinel test/test_topology_graph.cpp pulls this header so
// every default build exercises the static_asserts below).
// ────────────────────────────────────────────────────────────────────

namespace crucible::topology::detail::topology_graph_self_test {

// ── Reflection-driven name coverage ─────────────────────────────────

[[nodiscard]] consteval bool every_link_kind_has_name() noexcept {
    static constexpr auto enumerators =
        std::define_static_array(std::meta::enumerators_of(^^LinkKind));
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wshadow"
    template for (constexpr auto en : enumerators) {
        if (link_kind_name([:en:]) == std::string_view{"<unknown LinkKind>"}) {
            return false;
        }
    }
#pragma GCC diagnostic pop
    return true;
}
static_assert(every_link_kind_has_name(),
    "link_kind_name() switch is missing an arm for at least one "
    "LinkKind atom.");

[[nodiscard]] consteval bool every_link_layer_has_name() noexcept {
    static constexpr auto enumerators =
        std::define_static_array(std::meta::enumerators_of(^^LinkLayer));
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wshadow"
    template for (constexpr auto en : enumerators) {
        if (link_layer_name([:en:]) == std::string_view{"<unknown LinkLayer>"}) {
            return false;
        }
    }
#pragma GCC diagnostic pop
    return true;
}
static_assert(every_link_layer_has_name());

[[nodiscard]] consteval bool every_congestion_state_has_name() noexcept {
    static constexpr auto enumerators =
        std::define_static_array(std::meta::enumerators_of(^^CongestionState));
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wshadow"
    template for (constexpr auto en : enumerators) {
        if (congestion_state_name([:en:]) == std::string_view{"<unknown CongestionState>"}) {
            return false;
        }
    }
#pragma GCC diagnostic pop
    return true;
}
static_assert(every_congestion_state_has_name());

// ── link_layer_for projection sanity ────────────────────────────────

static_assert(link_layer_for(LinkKind::Unknown)           == LinkLayer::Unknown);
static_assert(link_layer_for(LinkKind::PciE)              == LinkLayer::L2);
static_assert(link_layer_for(LinkKind::NvLink)            == LinkLayer::L2);
static_assert(link_layer_for(LinkKind::NvSwitchPort)      == LinkLayer::L2);
static_assert(link_layer_for(LinkKind::AmdInfinityFabric) == LinkLayer::L2);
static_assert(link_layer_for(LinkKind::CxlMem)            == LinkLayer::L2);
static_assert(link_layer_for(LinkKind::CxlCache)          == LinkLayer::L2);
static_assert(link_layer_for(LinkKind::QpiUpi)            == LinkLayer::L2);
static_assert(link_layer_for(LinkKind::Cxio)              == LinkLayer::L2);
static_assert(link_layer_for(LinkKind::Ethernet)          == LinkLayer::L3);
static_assert(link_layer_for(LinkKind::Infiniband)        == LinkLayer::L3);
static_assert(link_layer_for(LinkKind::RoceV2)            == LinkLayer::L3);
static_assert(link_layer_for(LinkKind::Loopback)          == LinkLayer::L3);

// ── EdgeId basic semantics ──────────────────────────────────────────

static_assert(EdgeId{}.is_none());
static_assert(!EdgeId{0}.is_none(), "EdgeId{0} must be a real ID, "
              "not the sentinel — UINT32_MAX is the sentinel.");
static_assert(EdgeId::none().raw() == UINT32_MAX);
static_assert(EdgeId{42}.raw() == 42);
static_assert(EdgeId{42} == EdgeId{42});
static_assert(EdgeId{1}  <  EdgeId{2});

// ── Default TopologyEdge is a fully-specified zero ──────────────────

static_assert([] {
    TopologyEdge e{};
    return e.id.is_none()
        && e.kind == LinkKind::Unknown
        && e.state == CongestionState::Healthy
        && e.peer == nullptr
        && e.bandwidth_bytes_per_sec.value() == 0
        && e.rtt_ns_p50.value() == 0
        && e.rtt_ns_p99.value() == 0
        // Bit-equality on +0.0f (per CLAUDE.md §VI -Werror=float-equal).
        && std::bit_cast<std::uint32_t>(e.drop_rate.value()) == 0u;
}(), "Default TopologyEdge state drifted from the zero specification.");

// ── TopologyGraph default-constructed via mint is empty ─────────────

static_assert([] {
    using InitCtx = effects::ExecCtx<
        effects::Init,
        effects::ctx_numa::Any,
        effects::ctx_alloc::Unbound,
        effects::ctx_heat::Cold,
        effects::ctx_resid::DRAM,
        effects::Row<effects::Effect::Init>,
        effects::ctx_workload::Unspecified>;
    InitCtx ctx{};
    auto g = mint_topology_graph(
        ctx,
        std::span<const cog::CogIdentity>{},
        std::span<const TopologyEdge>{});
    return g.node_count() == 0 && g.edge_count() == 0;
}(), "Default-minted empty TopologyGraph reports non-zero counts — "
     "span size projection broken.");

// ── CtxFitsTopologyGraph — production-ctx fit ───────────────────────

using InitCtx = effects::ExecCtx<
    effects::Init,
    effects::ctx_numa::Any,
    effects::ctx_alloc::Unbound,
    effects::ctx_heat::Cold,
    effects::ctx_resid::DRAM,
    effects::Row<effects::Effect::Init>,
    effects::ctx_workload::Unspecified>;
static_assert(CtxFitsTopologyGraph<InitCtx>);

// Bg-only ctx — REFUSED.  TopologyGraph is built once at Init; a
// background context attempting to mint would either be racing
// Discovery (BorrowSafe violation) or rebuilding under live traffic
// (which we explicitly forbid; resharding goes through a fresh Init
// after Cipher cold-tier promotion).
using BgCtx = effects::ExecCtx<
    effects::Bg,
    effects::ctx_numa::Any,
    effects::ctx_alloc::Arena,
    effects::ctx_heat::Warm,
    effects::ctx_resid::L3,
    effects::Row<effects::Effect::Bg, effects::Effect::Alloc>,
    effects::ctx_workload::Unspecified>;
static_assert(!CtxFitsTopologyGraph<BgCtx>);

// Test ctx — REFUSED.
using TestCtx = effects::ExecCtx<
    effects::Test,
    effects::ctx_numa::Any,
    effects::ctx_alloc::Unbound,
    effects::ctx_heat::Cold,
    effects::ctx_resid::DRAM,
    effects::Row<effects::Effect::Test>,
    effects::ctx_workload::Unspecified>;
static_assert(!CtxFitsTopologyGraph<TestCtx>);

// Bare int — REFUSED at IsExecCtx conjunct.
static_assert(!CtxFitsTopologyGraph<int>);

// ── Append-only Universe pin (FOUND-I04) ────────────────────────────
//
// Renumbering ANY of the following is a federation-cache-key drift
// event — every Cipher checkpoint and federated topology snapshot
// silently re-keys.  Adding a new atom MUST land at the next free
// underlying value in its range without disturbing existing pins.

// LinkKind ordinal pins (13 atoms).
static_assert(static_cast<std::uint8_t>(LinkKind::Unknown)            ==  0,
    "LinkKind::Unknown drifted — federation row_hash invalidated.");
static_assert(static_cast<std::uint8_t>(LinkKind::PciE)               ==  1);
static_assert(static_cast<std::uint8_t>(LinkKind::NvLink)             ==  2);
static_assert(static_cast<std::uint8_t>(LinkKind::NvSwitchPort)       ==  3);
static_assert(static_cast<std::uint8_t>(LinkKind::AmdInfinityFabric)  ==  4);
static_assert(static_cast<std::uint8_t>(LinkKind::CxlMem)             ==  5);
static_assert(static_cast<std::uint8_t>(LinkKind::CxlCache)           ==  6);
static_assert(static_cast<std::uint8_t>(LinkKind::QpiUpi)             ==  7);
static_assert(static_cast<std::uint8_t>(LinkKind::Cxio)               ==  8);
static_assert(static_cast<std::uint8_t>(LinkKind::Ethernet)           == 16);
static_assert(static_cast<std::uint8_t>(LinkKind::Infiniband)         == 17);
static_assert(static_cast<std::uint8_t>(LinkKind::RoceV2)             == 18);
static_assert(static_cast<std::uint8_t>(LinkKind::Loopback)           == 19);

static_assert(std::is_same_v<std::underlying_type_t<LinkKind>, std::uint8_t>,
    "LinkKind underlying type drifted from uint8_t — ABI change.");

// LinkLayer ordinal pins (3 atoms — values match OSI layer numbers).
static_assert(static_cast<std::uint8_t>(LinkLayer::Unknown) == 0);
static_assert(static_cast<std::uint8_t>(LinkLayer::L2)      == 2);
static_assert(static_cast<std::uint8_t>(LinkLayer::L3)      == 3);

static_assert(std::is_same_v<std::underlying_type_t<LinkLayer>, std::uint8_t>);

// CongestionState ordinal pins (5 atoms).
static_assert(static_cast<std::uint8_t>(CongestionState::Healthy)   == 0);
static_assert(static_cast<std::uint8_t>(CongestionState::Mild)      == 1);
static_assert(static_cast<std::uint8_t>(CongestionState::Severe)    == 2);
static_assert(static_cast<std::uint8_t>(CongestionState::Saturated) == 3);
static_assert(static_cast<std::uint8_t>(CongestionState::Down)      == 4);

static_assert(std::is_same_v<std::underlying_type_t<CongestionState>, std::uint8_t>);

// LinkKind partition sanity — UnknownM=0, L2=1..15, L3=16..31.  The
// `link_layer_for` projection above relies on this; if a future
// LinkKind atom strays into the wrong range, the projection silently
// miscategorises.  The static check below sweeps every LinkKind atom
// via reflection.
[[nodiscard]] consteval bool link_kind_partition_sound() noexcept {
    static constexpr auto enumerators =
        std::define_static_array(std::meta::enumerators_of(^^LinkKind));
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wshadow"
    template for (constexpr auto en : enumerators) {
        constexpr auto k = [:en:];
        constexpr auto raw = static_cast<std::uint8_t>(k);
        constexpr auto layer = link_layer_for(k);
        if (raw == 0) {
            if (layer != LinkLayer::Unknown) return false;
        } else if (raw >= 1 && raw <= 15) {
            if (layer != LinkLayer::L2) return false;
        } else if (raw >= 16 && raw <= 31) {
            if (layer != LinkLayer::L3) return false;
        } else {
            return false;  // outside the partition
        }
    }
#pragma GCC diagnostic pop
    return true;
}
static_assert(link_kind_partition_sound(),
    "A LinkKind atom strays outside the {0=Unknown, 1..15=L2, 16..31=L3} "
    "partition — link_layer_for projection silently miscategorises.  "
    "Adjust either the atom's underlying value or the partition.");

}  // namespace crucible::topology::detail::topology_graph_self_test
