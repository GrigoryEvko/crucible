#pragma once

// ── crucible::mimic::CogMimic — per-Cog Mimic instance scaffolding ──
//
// GAPS-188.  Per misc/03_05_2026_networking.md §3.7 and CRUCIBLE.md L2.
//
// Every L0/L1 substrate Cog runs its own Mimic instance.  The substrate
// universe is heterogeneous — GPU dies emit SASS / cubin; NIC ports emit
// RDMA verb sequences and AF_XDP packet templates; NvSwitch ports emit
// SHARP aggregation trees and multicast routing tables; DRAM channels
// emit prefetch / refresh schedules; CPU cores emit native ELF — but
// they share ONE structural pattern:
//
//   Mimic instance = (CogIdentity*, calibrated TargetCaps,
//                     OpcodeLatencyTable, federation+per-Cog cache keys)
//
// CogMimic<K> binds that abstract triple uniformly across all substrate
// kinds.  Per-vendor / per-substrate emitter shape (cubin / NEFF / WR-
// list / DMA-descriptor) is downstream of this carrier — different
// per K, but all consume the same triple.
//
// ── What this header IS ─────────────────────────────────────────────
//
//   * `CogMimic<K>` — value-type carrier binding the triple per-K.
//     Trivially destructible, no owned heap.  All composition is via
//     existing safety wrappers — Tagged provenance on calibrated caps,
//     Stale grade carried inside OpcodeLatencyTable, source::Vendor on
//     firmware/bios via the embedded CogIdentity*.
//   * `target_caps_class_hash()` — federation-cache key axis.  A
//     compiled binary at one Cog can be reused at another Cog if their
//     target_caps_class hashes agree (same family + load-bearing caps
//     projection).  EXCLUDES firmware/bios — binaries port across
//     firmware revisions in the same family.  Per-substrate-family
//     projection (Compute folds sm_version + sm_count + hbm_bytes;
//     Network folds link_layer + line_rate + max_qp_count; Memory
//     folds channel_width + speed_mts; ...).
//   * `cog_kernel_cache_key()` — per-Cog cache key.  Folds the
//     federation key with `content_hash(*identity)` so firmware/bios
//     drift invalidates this Cog's slot but not the federation slot.
//   * `family()` — `CogFamily` accessor exposing the substrate role.
//     Downstream consumers dispatch on family to pick the correct
//     emitter shape (Mimic-NV emits cubin for Compute; Mimic-NIC
//     emits WR-lists for Network; etc.).
//   * `mint_cog_mimic<K>(ctx, identity, caps, opcodes)` — Universal
//     Mint Pattern §XXI factory.  Single `CtxFitsCogMimic<Ctx, K>`
//     concept gate; concrete return type; contracts pre-validate
//     identity non-zero + identity.kind == K.
//
// ── What this header is NOT ─────────────────────────────────────────
//
// This is SCAFFOLDING.  The MAP-Elites kernel search, the per-vendor
// SASS / AMDGPU / NEFF / RDMA-WR / DMA-desc emitters, the cross-vendor
// numerics CI harness (MIMIC.md §41), and the runtime kernel-driver
// ioctl surface ALL live downstream of this header in their own GAPS
// tasks.  This header's job: bind the triple, expose the cache-key
// contract, expose the family axis, refuse non-substrate Cogs at
// compile time.
//
// Per-family CogMimic specialisations (e.g., a partial specialisation
// `CogMimic<K> requires (cog_family_v<K> == CogFamily::Network)` that
// carries an extra `qp_pool_strategy` field, or a Compute-family
// variant carrying a `mma_shape_preferred` field) are a future
// refinement.  At scaffolding tier, ONE primary template uniformly
// holds the triple across all substrate kinds.  Downstream code that
// needs family-specific dispatch reads `CogMimic<K>::family()` and
// branches; it does not depend on a particular partial-specialisation
// shape.
//
// ── Universe (the load-bearing soundness gate) ──────────────────────
//
// `CogMimic<K>` requires the conjunction
//
//   IsMimicSubstrate<K>   — K's family ∈ {Compute, Network, Memory,
//                           Bus} per cog/CogIdentity.h.  PsuRail (Power)
//                           / BmcSensor (Sensor) / Datacenter
//                           (Container) refuse here — they have no
//                           Mimic instance.
//   HasCaps<K>            — caps_for<K> specialised in
//                           cog/TargetCaps.h.  Operational filter:
//                           today admits {Gpu, CpuCore, CpuSocket,
//                           NicPort, NvSwitch, DramChannel}; widens
//                           as more substrate caps schemas land
//                           (PcieLaneGroup, NvmeNamespace, ...).
//   HasOpcodeTable<K>     — opcodes_for<K> specialised in
//                           cog/OpcodeLatencyTable.h.  Same six today.
//   has_cog_mimic_projection_v<K> — caps_class_projection<K>
//                           specialised in this header.  Same six
//                           today; extending to a new substrate
//                           requires adding one projection
//                           specialisation here and a caps_for
//                           specialisation in cog/TargetCaps.h.
//
// Today the gate admits exactly six substrates: Gpu, CpuCore, CpuSocket
// (Compute family); NicPort, NvSwitch (Network family); DramChannel
// (Memory family).  Future extensions:
//
//   * FPGA / NPU / TpuCore / NeuronCore — Compute family, get caps_for
//     specialisation + opcodes_for specialisation + projection here.
//   * OpticalTransceiver — Network family, same recipe.
//   * NvmeNamespace, CXL.mem device — Memory family, same recipe.
//   * PcieLaneGroup, CXL switch — Bus family, same recipe.
//
// HS14 fixture #1 (neg_cog_mimic_non_substrate.cpp) witnesses rejection
// on PsuRail (Power family) — non-substrate, non-schedulable.  HS14
// fixture #2 (neg_cog_mimic_ctx_row_missing.cpp) witnesses rejection on
// a Test ctx whose row carries neither Effect::Init nor Effect::Bg
// (CogMimic minting is permitted only in calibration-time setup or
// background recalibration during fleet operation).
//
// ── Append-only Universe extension (FOUND-I04) ──────────────────────
//
// Adding a substrate Cog (e.g., FPGA → CogKind::Fpga, Network family):
//
//   1. Add CogKind enumerator at next free underlying value (FOUND-I04
//      frozen-position discipline) in cog/CogIdentity.h.
//   2. Specialise cog_family_for<Fpga> = CogFamily::Compute (or pick
//      the right family).  Add static_assert pin in CogIdentity.h.
//   3. Specialise caps_for<Fpga> in cog/TargetCaps.h with the FPGA
//      capability schema.
//   4. Specialise opcodes_for<Fpga> in cog/OpcodeLatencyTable.h with
//      the FPGA opcode catalog (fabric LUT placement, BRAM allocation,
//      DSP-block scheduling, ...).
//   5. Specialise caps_class_projection<Fpga> in this header with the
//      load-bearing federation-cache projection.
//   6. (Optional) Specialise cog_max_capacity<Fpga> in cog/FitsCog.h
//      if the FPGA participates in row-typed budgeting.
//
// Existing CogMimic<K> instantiations continue to work; the federation-
// cache key for existing K values stays bit-identical (their kind
// underlying value is frozen, their caps schemas extend append-only,
// their projection folds only over already-shipped fields).
//
// ── Eight axioms ────────────────────────────────────────────────────
//
//   InitSafe: every member NSDMI; default CogMimic<K> is well-defined
//             zero state (identity=nullptr, caps=default, opcodes=
//             empty/uncalibrated).  cog_kernel_cache_key contract
//             refuses the call until identity is bound.
//   TypeSafe: K participates as a non-type template parameter; per-K
//             specialisations are distinct types so a GPU CogMimic
//             cannot accidentally accept a NicPort OpcodeLatencyTable
//             (mismatched type).  caps_for_t<K> + opcodes_for_t<K>
//             route through the existing kind→schema bindings.
//   NullSafe: identity is a nullable raw pointer; pre/post conditions
//             on the cache-key accessor force a nullptr check at the
//             boundary.  Inside the body, the pointer is treated as
//             non-null per contract.
//   MemSafe:  no owned heap.  Identity pointer references arena
//             storage owned by the topology graph (GAPS-110); caps
//             and opcodes are aggregates of trivially-copyable
//             wrappers and a non-owning span.
//   BorrowSafe: identity is a const-pointer borrow; review discipline
//             ensures the topology arena outlives every CogMimic
//             instance bound to it.  Concurrent mutation of the
//             topology graph is serialised through Canopy delta-apply
//             (GAPS-115).
//   ThreadSafe: passive carrier; no atomics.  Kernel search threads
//             see a stable snapshot for the lifetime of a single
//             search batch.
//   LeakSafe: trivially destructible.  No timers, no FDs, no callbacks.
//   DetSafe:  target_caps_class_hash and cog_kernel_cache_key are
//             pure consteval-eligible functions over POD-equivalent
//             fields.  Same input → same uint64_t on every supported
//             platform.
//
// ── References ──────────────────────────────────────────────────────
//
//   misc/03_05_2026_networking.md §3.7 (per-Cog Mimic ownership)
//   CRUCIBLE.md L2 (Forge / Mimic substrate schemas)
//   MIMIC.md §22 (calibration), §25 (determinism), §27 (effect tokens),
//                §41 (cross-vendor numerics CI)
//   25_04_2026.md §3.3 (Met(X) row machinery)

#include <crucible/cog/CogIdentity.h>
#include <crucible/cog/OpcodeLatencyTable.h>
#include <crucible/cog/TargetCaps.h>
#include <crucible/effects/Capabilities.h>
#include <crucible/effects/EffectRow.h>
#include <crucible/effects/ExecCtx.h>
#include <crucible/safety/Decide.h>
#include <crucible/safety/Pre.h>
#include <crucible/safety/Tagged.h>

#include <cstdint>
#include <type_traits>
#include <utility>

namespace crucible::mimic {

// ────────────────────────────────────────────────────────────────────
// Per-K caps-class projection  (federation-cache-key axis)
// ────────────────────────────────────────────────────────────────────
//
// `caps_class_projection<K>::fold(caps)` produces the load-bearing
// uint64_t fold over the fields of `caps_for_t<K>` that determine
// emitter-output binary compatibility across the federation.  Two Cogs
// whose projections agree may share compiled binaries; their projections
// disagreeing means a per-Cog re-compile is required.
//
// Per family the projection picks substrate-specific load-bearing fields:
//
//   Compute (Gpu / CpuCore / CpuSocket):
//     ISA-class fields (sm_version / clock / core_count) + capacity
//     (hbm_bytes / l2 / l3) + features bitmap.
//   Network (NicPort / NvSwitch):
//     link layer + line rate + queue ceilings + features bitmap.
//   Memory (DramChannel):
//     channel width + speed + features bitmap.
//
// Calibrated TFLOPS / measured throughput values are NOT folded — they
// vary 5-20% across same-SKU Cogs due to manufacturing variation, but
// emitted binaries compiled for one Cog still run on another in the
// same SKU class.  The partition optimiser (GAPS-810) reads the
// calibrated values separately when deciding placement, not when
// deciding cache reuse.
//
// Soundness: the primary template is INTENTIONALLY UNDEFINED.  Reaching
// here means CogMimic<K> was instantiated for a K that has no
// projection specialisation — the concept gate above admits only the
// six substrate kinds with shipped projections, so the only way to
// land in the primary is during future Universe extension before the
// projection ships.

namespace detail {

// fmix64 — xxHash final-mix.  Inlined here to avoid pulling in
// safety/diag/RowHashFold.h primitives from the mimic tree.  Bit-
// equality across platforms holds: input is uint64_t, output is
// uint64_t, only xor / shift / multiply with hex constants.
[[nodiscard]] constexpr std::uint64_t
cog_mimic_fmix64(std::uint64_t h) noexcept {
    h ^= h >> 33;
    h *= 0xFF51AFD7ED558CCDULL;
    h ^= h >> 33;
    h *= 0xC4CEB9FE1A85EC53ULL;
    h ^= h >> 33;
    return h;
}

// Seed the fold with the kind underlying value in the high byte so
// distinct kinds produce distinct hashes even if their per-K folds
// happen to collide on numeric content.
[[nodiscard]] constexpr std::uint64_t
cog_mimic_kind_seed(cog::CogKind K) noexcept {
    return static_cast<std::uint64_t>(K) << 56;
}

template <cog::CogKind K>
struct caps_class_projection;

// ── Compute family ─────────────────────────────────────────────────

template <>
struct caps_class_projection<cog::CogKind::Gpu> {
    [[nodiscard]] static constexpr std::uint64_t
    fold(cog::GpuTargetCaps const& c) noexcept {
        std::uint64_t h = cog_mimic_kind_seed(cog::CogKind::Gpu);
        h = cog_mimic_fmix64(h ^ static_cast<std::uint64_t>(
                                  c.sm_version.value()));
        h = cog_mimic_fmix64(h ^ static_cast<std::uint64_t>(
                                  c.sm_count.value()));
        h = cog_mimic_fmix64(h ^ c.hbm_bytes.value());
        h = cog_mimic_fmix64(h ^ static_cast<std::uint64_t>(
                                  c.features.raw()));
        return h;
    }
};

template <>
struct caps_class_projection<cog::CogKind::CpuCore> {
    [[nodiscard]] static constexpr std::uint64_t
    fold(cog::CpuCoreTargetCaps const& c) noexcept {
        std::uint64_t h = cog_mimic_kind_seed(cog::CogKind::CpuCore);
        h = cog_mimic_fmix64(h ^ static_cast<std::uint64_t>(
                                  c.base_clock_mhz.value()));
        h = cog_mimic_fmix64(h ^ static_cast<std::uint64_t>(
                                  c.l2_bytes.value()));
        h = cog_mimic_fmix64(h ^ static_cast<std::uint64_t>(
                                  c.features.raw()));
        return h;
    }
};

template <>
struct caps_class_projection<cog::CogKind::CpuSocket> {
    [[nodiscard]] static constexpr std::uint64_t
    fold(cog::CpuSocketTargetCaps const& c) noexcept {
        std::uint64_t h = cog_mimic_kind_seed(cog::CogKind::CpuSocket);
        h = cog_mimic_fmix64(h ^ static_cast<std::uint64_t>(
                                  c.core_count.value()));
        h = cog_mimic_fmix64(h ^ c.l3_bytes.value());
        h = cog_mimic_fmix64(h ^ static_cast<std::uint64_t>(
                                  c.features.raw()));
        return h;
    }
};

// ── Network family ─────────────────────────────────────────────────

template <>
struct caps_class_projection<cog::CogKind::NicPort> {
    [[nodiscard]] static constexpr std::uint64_t
    fold(cog::NicPortTargetCaps const& c) noexcept {
        // Federation-shareable for NIC kernels: link layer (different
        // ISA emitter for IB / Ethernet / RoCE / NVLink), line rate
        // (RDMA WR pacing strategy), max QP count (queue allocation
        // shape), MTU (segmentation policy), feature bitmap (TSO /
        // RoCE / GpuDirect / XdpNative / ...).
        std::uint64_t h = cog_mimic_kind_seed(cog::CogKind::NicPort);
        h = cog_mimic_fmix64(h ^ static_cast<std::uint64_t>(
                                  c.link_layer.value()));
        h = cog_mimic_fmix64(h ^ c.line_rate_bytes_per_sec.value());
        h = cog_mimic_fmix64(h ^ static_cast<std::uint64_t>(
                                  c.max_qp_count.value()));
        h = cog_mimic_fmix64(h ^ static_cast<std::uint64_t>(
                                  c.mtu_bytes.value()));
        h = cog_mimic_fmix64(h ^ static_cast<std::uint64_t>(
                                  c.features.raw()));
        return h;
    }
};

template <>
struct caps_class_projection<cog::CogKind::NvSwitch> {
    [[nodiscard]] static constexpr std::uint64_t
    fold(cog::NvSwitchTargetCaps const& c) noexcept {
        // Federation-shareable for switch fabric kernels: port count
        // (topology shape — fat-tree vs torus realisation), per-port
        // bandwidth (pacing), TCAM entries (ACL programmability),
        // feature bitmap (Sharp / P4 / AdaptiveRouting / Pfc / Ecn).
        std::uint64_t h = cog_mimic_kind_seed(cog::CogKind::NvSwitch);
        h = cog_mimic_fmix64(h ^ static_cast<std::uint64_t>(
                                  c.port_count.value()));
        h = cog_mimic_fmix64(h ^ c.per_port_bandwidth_bytes_per_sec.value());
        h = cog_mimic_fmix64(h ^ static_cast<std::uint64_t>(
                                  c.tcam_entries.value()));
        h = cog_mimic_fmix64(h ^ static_cast<std::uint64_t>(
                                  c.features.raw()));
        return h;
    }
};

// ── Memory family ──────────────────────────────────────────────────

template <>
struct caps_class_projection<cog::CogKind::DramChannel> {
    [[nodiscard]] static constexpr std::uint64_t
    fold(cog::DramChannelTargetCaps const& c) noexcept {
        // Federation-shareable for memory schedule kernels: channel
        // width (interleaving strategy), speed (refresh / activate
        // timing), capacity (page-allocation shape), feature bitmap
        // (Ecc / OnDieEcc / PowerDownIdle / Hbm).
        std::uint64_t h = cog_mimic_kind_seed(cog::CogKind::DramChannel);
        h = cog_mimic_fmix64(h ^ static_cast<std::uint64_t>(
                                  c.channel_width_bits.value()));
        h = cog_mimic_fmix64(h ^ static_cast<std::uint64_t>(
                                  c.speed_mts.value()));
        h = cog_mimic_fmix64(h ^ c.capacity_bytes.value());
        h = cog_mimic_fmix64(h ^ static_cast<std::uint64_t>(
                                  c.features.raw()));
        return h;
    }
};

// Detection trait — `caps_class_projection<K>` is "complete"
// (specialised) iff its fold member function exists.  Concept-style
// detection that triggers the substitution failure cleanly when
// CogMimic<K> instantiates for an unsupported K.
template <cog::CogKind K, class = void>
struct has_cog_mimic_projection_v_impl : std::false_type {};

template <cog::CogKind K>
struct has_cog_mimic_projection_v_impl<
    K,
    std::void_t<decltype(caps_class_projection<K>::fold(
        std::declval<cog::caps_for_t<K> const&>()))>>
    : std::true_type {};

template <cog::CogKind K>
inline constexpr bool has_cog_mimic_projection_v =
    has_cog_mimic_projection_v_impl<K>::value;

}  // namespace detail

// ────────────────────────────────────────────────────────────────────
// CogMimic<K> — per-Cog Mimic carrier
// ────────────────────────────────────────────────────────────────────

template <cog::CogKind K>
    requires cog::IsMimicSubstrate<K>
          && cog::HasCaps<K>
          && cog::HasOpcodeTable<K>
          && detail::has_cog_mimic_projection_v<K>
struct CogMimic {
    static constexpr cog::CogKind   kind   = K;
    static constexpr cog::CogFamily family = cog::cog_family_v<K>;

    using CapsType    = cog::caps_for_t<K>;
    using OpcodeTable = cog::OpcodeLatencyTable<K>;

    // Identity pointer.  Borrowed from the topology arena owned by
    // GAPS-110 TopologyGraph.  Default-constructed CogMimic has
    // identity == nullptr — the cache-key accessors refuse the call
    // until the field is bound (mint-time contract).
    cog::CogIdentity const* identity = nullptr;

    // Calibrated target caps.  source::Calibrated provenance pinned at
    // the Tagged level — a downstream consumer that demands "must be
    // measured, not vendor spec sheet" gets the structural guarantee.
    safety::Tagged<CapsType, safety::source::Calibrated>
        calibrated_caps{CapsType{}};

    // Calibrated opcode latency table.  Default-constructed has empty
    // entries span + Stale<double>::at_infinity grade — the table
    // SAYS "uncalibrated" until GAPS-196 Calibrate.h writes real
    // measurements.
    OpcodeTable opcode_latency_table{};

    // ── Federation cache-key axis ──────────────────────────────────
    //
    // Folds (kind, load-bearing caps projection).  Excludes
    // firmware/bios — emitted binaries compiled at a Cog with
    // firmware revision 1.2.3 still run at a same-SKU Cog with
    // revision 1.2.4 unless the firmware breaks ABI (extremely rare;
    // when it happens, the caller bumps target_caps_class explicitly
    // via re-calibration that updates a folded field).
    //
    // The fold is a pure function over POD-equivalent fields — no
    // pointer dereference, no syscall, no global state.  Two Cogs
    // with byte-identical calibrated caps produce byte-identical
    // hashes on every supported platform (DetSafe).
    [[nodiscard]] constexpr std::uint64_t
    target_caps_class_hash() const noexcept {
        return detail::caps_class_projection<K>::fold(
            calibrated_caps.value());
    }

    // ── Per-Cog cache-key axis ─────────────────────────────────────
    //
    // Folds the federation key with content_hash(*identity).
    // content_hash includes uuid + firmware_revision + bios_revision —
    // so this Cog's cache slot rotates on firmware/bios drift while
    // the federation slot stays stable.
    //
    // Pre-condition: identity != nullptr AND identity->uuid is non-
    // zero.  Default-constructed CogMimic has identity = nullptr;
    // calling this accessor on a default-constructed instance is
    // structurally a misuse — caught by the contract precondition,
    // not a silent UB ride through a null-pointer dereference.
    [[nodiscard]] constexpr std::uint64_t
    cog_kernel_cache_key() const noexcept
    {
        // CRUCIBLE_PRE rather than P2900 `pre()` clauses: the pre on
        // !identity->uuid.is_zero() needs a deref through the struct
        // pointer, which GCC 16.1.1 cannot constant-fold cleanly when
        // this method is called from a consteval context (see safety/
        // Pre.h for the diagnosis).  The macro fires at consteval AND
        // (debug-only) runtime, zero-cost in NDEBUG.
        //
        // The structural-non-zero clause cites the named
        // `decide::is_non_zero` predicate rather than the hand-rolled
        // `!identity->uuid.is_zero()` form.  Cohort-discharged HS14
        // fixtures (neg_decide_is_non_zero_{integer,aggregate}_zero)
        // pin ALWAYS-ACCEPT / INVERTED-SENSE / FIELD-MYOPIC bug classes
        // once for the whole codebase; the local cite reads as a single
        // verification-condition discharge.  The null-check stays as
        // its own CRUCIBLE_PRE because pointer-non-null and structural-
        // non-zero are orthogonal: a non-null pointer to a zero-UUID
        // is a different bug from a null pointer.
        CRUCIBLE_PRE(identity != nullptr);
        CRUCIBLE_PRE(crucible::decide::is_non_zero(identity->uuid));
        std::uint64_t federation = target_caps_class_hash();
        std::uint64_t cog_local  = cog::content_hash(*identity);
        return detail::cog_mimic_fmix64(federation ^ cog_local);
    }

    // ── Default-emptiness predicate ─────────────────────────────────
    //
    // True iff the carrier is in its post-default-construction state
    // — identity unbound AND opcode table never calibrated.  Used by
    // diagnostic surfaces (e.g., a future fleet-wide "uncalibrated
    // Cogs" report) to distinguish "default constructed, awaiting
    // calibration" from "calibrated, in use".  Cheap fast-path: short-
    // circuit on identity == nullptr; only reach into opcode_latency_
    // table.empty() when identity is bound.
    [[nodiscard]] constexpr bool is_uncalibrated() const noexcept {
        return identity == nullptr || opcode_latency_table.empty();
    }
};

// ────────────────────────────────────────────────────────────────────
// CtxFitsCogMimic<Ctx, K>  — Universal Mint Pattern fit gate
// ────────────────────────────────────────────────────────────────────
//
// The single concept that mint_cog_mimic refuses-on.  Conjuncts:
//
//   1. `IsExecCtx<Ctx>`             — Ctx is a real ExecCtx, not a
//                                     bare argument that the caller
//                                     forgot to wrap.
//   2. `IsMimicSubstrate<K>`        — K's family is one of {Compute,
//                                     Network, Memory, Bus}.  Power /
//                                     Sensor / Container kinds refuse.
//   3. `HasCaps<K>`                 — K publishes a TargetCaps schema.
//   4. `HasOpcodeTable<K>`          — K publishes opcodes.
//   5. `has_cog_mimic_projection_v` — K admits a federation-fold
//                                     specialisation.
//   6. Ctx::row_type admits         — minting a CogMimic instance is
//      Row<Init> OR Row<Bg>.          either calibration-time (Init)
//                                     or background recalibration
//                                     during fleet operation (Bg).
//                                     Pure / Test / Fg contexts
//                                     are refused.
//
// HS14 fixture #2 witnesses rejection on a row-missing Ctx.
template <class Ctx, cog::CogKind K>
concept CtxFitsCogMimic =
       effects::IsExecCtx<Ctx>
    && cog::IsMimicSubstrate<K>
    && cog::HasCaps<K>
    && cog::HasOpcodeTable<K>
    && detail::has_cog_mimic_projection_v<K>
    && (crucible::decide::row_subset<
            effects::Row<effects::Effect::Init>,
            effects::row_type_of_t<Ctx>>()
       || crucible::decide::row_subset<
            effects::Row<effects::Effect::Bg>,
            effects::row_type_of_t<Ctx>>());

// ────────────────────────────────────────────────────────────────────
// mint_cog_mimic<K>(ctx, identity, caps, opcodes)
// ────────────────────────────────────────────────────────────────────
//
// Universal Mint Pattern §XXI ctx-bound mint.  Single CtxFitsCogMimic
// requires-clause — every multi-conjunct check lives inside the
// concept, leaving the call-site signature one line.
//
// Contracts (CRUCIBLE_PRE rather than P2900 `pre()` clauses — same
// GCC 16.1.1 by-const-ref-struct consteval-bypass that affects
// cog::content_hash, see safety/Pre.h):
//
//   CRUCIBLE_PRE(decide::is_non_zero(identity.uuid))
//                                    — refuses zero-UUID at construct
//                                      time so cog_kernel_cache_key
//                                      cannot be called on a hashable
//                                      garbage Cog.  Cites the named
//                                      decide:: predicate; HS14
//                                      fixtures pin orthogonal bug-
//                                      class buckets cohort-wide.
//   CRUCIBLE_PRE(identity.kind == K)
//                                    — refuses identity-kind / template-
//                                      kind mismatch.  A future bug
//                                      where calibrate.cpp passes a
//                                      Gpu identity to mint_cog_mimic
//                                      <CpuSocket> fires here, not
//                                      150 lines downstream.  No
//                                      dedicated decide:: predicate
//                                      yet — kind-equality is a single-
//                                      use comparison without recurring
//                                      structural shape across the
//                                      codebase.
//
// Returns CogMimic<K> by value — concrete type, no auto-erasure, no
// std::variant tag.  Concept-overloaded specialisation downstream
// depends on the concrete type.
template <cog::CogKind K, effects::IsExecCtx Ctx>
    requires CtxFitsCogMimic<Ctx, K>
[[nodiscard]] constexpr CogMimic<K>
mint_cog_mimic(Ctx const& /* ctx */,
               cog::CogIdentity const&            identity,
               cog::caps_for_t<K>                 calibrated_caps,
               cog::OpcodeLatencyTable<K>         opcodes) noexcept
{
    CRUCIBLE_PRE(crucible::decide::is_non_zero(identity.uuid));
    CRUCIBLE_PRE(identity.kind == K);
    return CogMimic<K>{
        &identity,
        safety::Tagged<cog::caps_for_t<K>, safety::source::Calibrated>{
            std::move(calibrated_caps)},
        std::move(opcodes),
    };
}

// ────────────────────────────────────────────────────────────────────
// Self-test block
// ────────────────────────────────────────────────────────────────────

namespace detail::cog_mimic_self_test {

// ── Concept gate: per-substrate admission sweep ─────────────────────
//
// All six current substrates admit.  Power / Sensor / Container kinds
// refuse at the IsMimicSubstrate conjunct.  HS14 fixture #1 witnesses.
static_assert( cog::IsMimicSubstrate<cog::CogKind::Gpu>);
static_assert( cog::IsMimicSubstrate<cog::CogKind::CpuCore>);
static_assert( cog::IsMimicSubstrate<cog::CogKind::CpuSocket>);
static_assert( cog::IsMimicSubstrate<cog::CogKind::NicPort>);
static_assert( cog::IsMimicSubstrate<cog::CogKind::NvSwitch>);
static_assert( cog::IsMimicSubstrate<cog::CogKind::DramChannel>);

// All six have caps_for + opcodes_for shipped today.
static_assert( cog::HasCaps<cog::CogKind::Gpu>);
static_assert( cog::HasCaps<cog::CogKind::CpuCore>);
static_assert( cog::HasCaps<cog::CogKind::CpuSocket>);
static_assert( cog::HasCaps<cog::CogKind::NicPort>);
static_assert( cog::HasCaps<cog::CogKind::NvSwitch>);
static_assert( cog::HasCaps<cog::CogKind::DramChannel>);
static_assert( cog::HasOpcodeTable<cog::CogKind::Gpu>);
static_assert( cog::HasOpcodeTable<cog::CogKind::CpuCore>);
static_assert( cog::HasOpcodeTable<cog::CogKind::CpuSocket>);
static_assert( cog::HasOpcodeTable<cog::CogKind::NicPort>);
static_assert( cog::HasOpcodeTable<cog::CogKind::NvSwitch>);
static_assert( cog::HasOpcodeTable<cog::CogKind::DramChannel>);

// All six have projections shipped here.
static_assert(detail::has_cog_mimic_projection_v<cog::CogKind::Gpu>);
static_assert(detail::has_cog_mimic_projection_v<cog::CogKind::CpuCore>);
static_assert(detail::has_cog_mimic_projection_v<cog::CogKind::CpuSocket>);
static_assert(detail::has_cog_mimic_projection_v<cog::CogKind::NicPort>);
static_assert(detail::has_cog_mimic_projection_v<cog::CogKind::NvSwitch>);
static_assert(detail::has_cog_mimic_projection_v<cog::CogKind::DramChannel>);

// Non-substrate kinds refuse.  PsuRail = Power family, BmcSensor =
// Sensor family, Datacenter = Container family.
static_assert(!cog::IsMimicSubstrate<cog::CogKind::PsuRail>);
static_assert(!cog::IsMimicSubstrate<cog::CogKind::RackPsu>);
static_assert(!cog::IsMimicSubstrate<cog::CogKind::BmcSensor>);
static_assert(!cog::IsMimicSubstrate<cog::CogKind::Datacenter>);
static_assert(!cog::IsMimicSubstrate<cog::CogKind::Server>);
static_assert(!cog::IsMimicSubstrate<cog::CogKind::Rack>);

// Family axis is exposed correctly through CogMimic<K>::family.
static_assert(CogMimic<cog::CogKind::Gpu>::family         == cog::CogFamily::Compute);
static_assert(CogMimic<cog::CogKind::CpuCore>::family     == cog::CogFamily::Compute);
static_assert(CogMimic<cog::CogKind::CpuSocket>::family   == cog::CogFamily::Compute);
static_assert(CogMimic<cog::CogKind::NicPort>::family     == cog::CogFamily::Network);
static_assert(CogMimic<cog::CogKind::NvSwitch>::family    == cog::CogFamily::Network);
static_assert(CogMimic<cog::CogKind::DramChannel>::family == cog::CogFamily::Memory);

// ── Trivially-destructible carrier ──────────────────────────────────
static_assert(std::is_trivially_destructible_v<CogMimic<cog::CogKind::Gpu>>,
    "CogMimic<Gpu> must be trivially destructible — no owned heap.");
static_assert(std::is_trivially_destructible_v<CogMimic<cog::CogKind::CpuCore>>);
static_assert(std::is_trivially_destructible_v<CogMimic<cog::CogKind::CpuSocket>>);
static_assert(std::is_trivially_destructible_v<CogMimic<cog::CogKind::NicPort>>);
static_assert(std::is_trivially_destructible_v<CogMimic<cog::CogKind::NvSwitch>>);
static_assert(std::is_trivially_destructible_v<CogMimic<cog::CogKind::DramChannel>>);

// ── Default-state semantics ─────────────────────────────────────────
static_assert([] {
    CogMimic<cog::CogKind::Gpu> m{};
    return m.identity == nullptr
        && m.opcode_latency_table.empty()
        && m.is_uncalibrated();
}(),
    "Default CogMimic<Gpu> must be uncalibrated and identity-unbound.");

static_assert([] {
    CogMimic<cog::CogKind::NicPort> m{};
    return m.identity == nullptr
        && m.is_uncalibrated();
}(),
    "Default CogMimic<NicPort> must be uncalibrated.");

// ── target_caps_class_hash determinism ──────────────────────────────
static_assert([] {
    CogMimic<cog::CogKind::Gpu> a{};
    CogMimic<cog::CogKind::Gpu> b{};
    return a.target_caps_class_hash() == b.target_caps_class_hash();
}(),
    "CogMimic<Gpu>::target_caps_class_hash diverged for identical "
    "default caps — DetSafe violation.");

// Same for NIC.
static_assert([] {
    CogMimic<cog::CogKind::NicPort> a{};
    CogMimic<cog::CogKind::NicPort> b{};
    return a.target_caps_class_hash() == b.target_caps_class_hash();
}(),
    "CogMimic<NicPort>::target_caps_class_hash diverged for identical "
    "default caps — DetSafe violation.");

// Different K → different hash.  Sweep all six pairs.
static_assert([] {
    CogMimic<cog::CogKind::Gpu>         g{};
    CogMimic<cog::CogKind::CpuCore>     c{};
    CogMimic<cog::CogKind::CpuSocket>   s{};
    CogMimic<cog::CogKind::NicPort>     n{};
    CogMimic<cog::CogKind::NvSwitch>    sw{};
    CogMimic<cog::CogKind::DramChannel> d{};
    auto hg = g.target_caps_class_hash();
    auto hc = c.target_caps_class_hash();
    auto hs = s.target_caps_class_hash();
    auto hn = n.target_caps_class_hash();
    auto hsw = sw.target_caps_class_hash();
    auto hd = d.target_caps_class_hash();
    return hg != hc && hg != hs && hg != hn && hg != hsw && hg != hd
        && hc != hs && hc != hn && hc != hsw && hc != hd
        && hs != hn && hs != hsw && hs != hd
        && hn != hsw && hn != hd
        && hsw != hd;
}(),
    "Distinct CogKinds collided in target_caps_class_hash — federation "
    "cache would alias kernels across substrates.");

// SM-version drift discrimination on GPU (load-bearing federation field).
static_assert([] {
    CogMimic<cog::CogKind::Gpu> hopper{};
    hopper.calibrated_caps.value_mut().sm_version =
        safety::Tagged<std::uint16_t, safety::source::Vendor>{
            std::uint16_t{90}};

    CogMimic<cog::CogKind::Gpu> blackwell{};
    blackwell.calibrated_caps.value_mut().sm_version =
        safety::Tagged<std::uint16_t, safety::source::Vendor>{
            std::uint16_t{100}};
    return hopper.target_caps_class_hash() !=
           blackwell.target_caps_class_hash();
}(),
    "SM-version drift collapsed in target_caps_class_hash — kernel "
    "cache would silently reuse Hopper kernels at Blackwell.");

// Link-layer drift discrimination on NIC (load-bearing for network
// kernel emit shape — RDMA verbs differ across IB / Ethernet / RoCE).
static_assert([] {
    CogMimic<cog::CogKind::NicPort> infiniband{};
    infiniband.calibrated_caps.value_mut().link_layer =
        safety::Tagged<cog::LinkLayer, safety::source::Vendor>{
            cog::LinkLayer::Infiniband};

    CogMimic<cog::CogKind::NicPort> ethernet{};
    ethernet.calibrated_caps.value_mut().link_layer =
        safety::Tagged<cog::LinkLayer, safety::source::Vendor>{
            cog::LinkLayer::Ethernet};
    return infiniband.target_caps_class_hash() !=
           ethernet.target_caps_class_hash();
}(),
    "Link-layer drift collapsed in NIC target_caps_class_hash — IB and "
    "Ethernet kernels would silently alias.");

// ── cog_kernel_cache_key firmware/bios rotation ─────────────────────
static_assert([] {
    cog::CogIdentity id_a{};
    id_a.uuid              = cog::Uuid{0xDEAD0001ULL, 0xCAFE0002ULL};
    id_a.kind              = cog::CogKind::Gpu;
    id_a.firmware_revision = safety::Tagged<std::uint64_t,
                                            safety::source::Vendor>{1};

    cog::CogIdentity id_b = id_a;
    id_b.firmware_revision = safety::Tagged<std::uint64_t,
                                            safety::source::Vendor>{2};

    CogMimic<cog::CogKind::Gpu> a{};
    a.identity = &id_a;
    CogMimic<cog::CogKind::Gpu> b{};
    b.identity = &id_b;
    return a.cog_kernel_cache_key() != b.cog_kernel_cache_key()
        && a.target_caps_class_hash() == b.target_caps_class_hash();
}(),
    "Firmware drift folded into cog_kernel_cache_key BUT target_caps_"
    "class_hash stayed stable — federation reuse + per-Cog rotation "
    "contract from §3.7.");

// ── CtxFitsCogMimic — production-ctx fit ────────────────────────────
//
// Init / Bg ctx fits across all six substrates.
using InitCtx = effects::ExecCtx<
    effects::Init,
    effects::ctx_numa::Any,
    effects::ctx_alloc::Unbound,
    effects::ctx_heat::Cold,
    effects::ctx_resid::DRAM,
    effects::Row<effects::Effect::Init>,
    effects::ctx_workload::Unspecified>;

using BgCtx = effects::ExecCtx<
    effects::Bg,
    effects::ctx_numa::Any,
    effects::ctx_alloc::Arena,
    effects::ctx_heat::Warm,
    effects::ctx_resid::L3,
    effects::Row<effects::Effect::Bg, effects::Effect::Alloc>,
    effects::ctx_workload::Unspecified>;

static_assert(CtxFitsCogMimic<InitCtx, cog::CogKind::Gpu>);
static_assert(CtxFitsCogMimic<InitCtx, cog::CogKind::CpuCore>);
static_assert(CtxFitsCogMimic<InitCtx, cog::CogKind::CpuSocket>);
static_assert(CtxFitsCogMimic<InitCtx, cog::CogKind::NicPort>);
static_assert(CtxFitsCogMimic<InitCtx, cog::CogKind::NvSwitch>);
static_assert(CtxFitsCogMimic<InitCtx, cog::CogKind::DramChannel>);

static_assert(CtxFitsCogMimic<BgCtx, cog::CogKind::Gpu>);
static_assert(CtxFitsCogMimic<BgCtx, cog::CogKind::NicPort>);
static_assert(CtxFitsCogMimic<BgCtx, cog::CogKind::DramChannel>);

// Note: a single ctx cannot carry BOTH Init AND Bg in its effect row
// — ExecCtx<Init, ..., Row<Init, Bg>, ...> fails ExecCtx's own
// cap_permitted_row check (Init's permitted row is {Init, Alloc, IO},
// not {Bg}; Bg's permitted row is {Bg, Alloc, IO, Block}, not {Init}).
// The disjunctive `||` in CtxFitsCogMimic admits Init OR Bg (different
// ctxs, different lifecycle phases), not both in one ctx.

// Foreground / Test / Pure (empty) row — REFUSED at the row conjunct.
// HS14 fixture #2 witnesses the call-site rejection.
using FgCtx = effects::ExecCtx<
    effects::ctx_cap::Fg,
    effects::ctx_numa::Any,
    effects::ctx_alloc::Stack,
    effects::ctx_heat::Hot,
    effects::ctx_resid::L1,
    effects::Row<>,
    effects::ctx_workload::Unspecified>;
static_assert(!CtxFitsCogMimic<FgCtx, cog::CogKind::Gpu>);

using TestCtx = effects::ExecCtx<
    effects::Test,
    effects::ctx_numa::Any,
    effects::ctx_alloc::Stack,
    effects::ctx_heat::Cold,
    effects::ctx_resid::DRAM,
    effects::Row<effects::Effect::Test>,
    effects::ctx_workload::Unspecified>;
static_assert(!CtxFitsCogMimic<TestCtx, cog::CogKind::Gpu>);

// Non-substrate kind — refused at the IsMimicSubstrate conjunct, even
// with a fitting Init ctx.  HS14 fixture #1 witnesses.
static_assert(!CtxFitsCogMimic<InitCtx, cog::CogKind::PsuRail>);
static_assert(!CtxFitsCogMimic<InitCtx, cog::CogKind::BmcSensor>);
static_assert(!CtxFitsCogMimic<InitCtx, cog::CogKind::Datacenter>);

// Non-Ctx first arg — refused at IsExecCtx conjunct (a bare int
// being passed as Ctx, e.g. via implicit conversion accident).
static_assert(!CtxFitsCogMimic<int, cog::CogKind::Gpu>);

// ── mint_cog_mimic — round-trip semantics ───────────────────────────
//
// Mint a Network-family Cog (NicPort) — proves the factory works
// uniformly across families, not just compute.
static_assert([] {
    cog::CogIdentity id{};
    id.uuid = cog::Uuid{0xAA0001ULL, 0xBB0002ULL};
    id.kind = cog::CogKind::NicPort;
    id.firmware_revision = safety::Tagged<std::uint64_t,
                                          safety::source::Vendor>{42};

    cog::NicPortTargetCaps caps{};
    caps.link_layer = safety::Tagged<cog::LinkLayer,
                                     safety::source::Vendor>{
        cog::LinkLayer::Roce};
    caps.line_rate_bytes_per_sec =
        safety::Tagged<std::uint64_t, safety::source::Vendor>{
            std::uint64_t{50ULL} * 1024 * 1024 * 1024 / 8};

    cog::OpcodeLatencyTable<cog::CogKind::NicPort> tbl{};

    InitCtx ctx{};
    auto m = mint_cog_mimic<cog::CogKind::NicPort>(ctx, id, caps, tbl);

    return m.identity == &id
        && m.identity->kind == cog::CogKind::NicPort
        && m.calibrated_caps.value().link_layer.value() == cog::LinkLayer::Roce
        && m.family == cog::CogFamily::Network
        && m.is_uncalibrated();   // table still empty
}(),
    "mint_cog_mimic round-trip lost identity / caps / opcodes for NIC — "
    "Universal Mint Pattern §XXI semantics broken on Network family.");

// And a Compute-family Cog (Gpu) for symmetric coverage.
static_assert([] {
    cog::CogIdentity id{};
    id.uuid = cog::Uuid{0xAA0001ULL, 0xBB0002ULL};
    id.kind = cog::CogKind::Gpu;

    cog::GpuTargetCaps caps{};
    caps.sm_version = safety::Tagged<std::uint16_t,
                                     safety::source::Vendor>{
        std::uint16_t{90}};

    cog::OpcodeLatencyTable<cog::CogKind::Gpu> tbl{};

    InitCtx ctx{};
    auto m = mint_cog_mimic<cog::CogKind::Gpu>(ctx, id, caps, tbl);

    return m.identity == &id
        && m.calibrated_caps.value().sm_version.value() == 90
        && m.family == cog::CogFamily::Compute;
}(),
    "mint_cog_mimic round-trip lost identity / caps for Compute family.");

// ── Inferring K from the CogMimic carrier ──────────────────────────
static_assert(CogMimic<cog::CogKind::Gpu>::kind         == cog::CogKind::Gpu);
static_assert(CogMimic<cog::CogKind::CpuCore>::kind     == cog::CogKind::CpuCore);
static_assert(CogMimic<cog::CogKind::CpuSocket>::kind   == cog::CogKind::CpuSocket);
static_assert(CogMimic<cog::CogKind::NicPort>::kind     == cog::CogKind::NicPort);
static_assert(CogMimic<cog::CogKind::NvSwitch>::kind    == cog::CogKind::NvSwitch);
static_assert(CogMimic<cog::CogKind::DramChannel>::kind == cog::CogKind::DramChannel);

}  // namespace detail::cog_mimic_self_test

}  // namespace crucible::mimic
