#pragma once

// ── crucible::fixy::syscall::bridge — SyscallSurface → Met(X) (V-100) ─
//
// The structural payoff of the V-097/V-098/V-099 syscall-axis stack:
// a concept-gated metafunction that LIFTS any SyscallSurface-routing
// grant tag into its corresponding Met(X) effect row, automatically.
//
// Inputs:
//   * V-097 — `DimensionAxis::SyscallSurface` + `SyscallFamilyLattice`
//   * V-098 — Family.h's 9 family-tier grants + Per.h's per<SyscallId>
//             parametric over 36 enumerators
//   * V-099 — Ioctl.h's ioctl::vendor<IoctlVendor> + ioctl::subsystem<
//             IoctlSubsystem> parametric grants
//
// All three V-098/V-099 grant families share the `family_tier_v<G>`
// metadata channel Family.h installed (the channel V-098's doc-block
// promises "V-100's bridge will read to lift the family into the
// Met(X) effect row").  Bridge.h reads `family_tier_v<G>` ONCE per
// grant and dispatches through a single per-SyscallFamily Row map:
//
//   NoSyscall      → Row<>                          (pure compute)
//   VdsoOnly       → Row<>                          (no kernel transition)
//   ReadOnlyState  → Row<IO>                        (kernel read, no block)
//   FileMutation   → Row<IO, Block>                 (disk writes block)
//   MemoryMapping  → Row<IO>                        (mmap path-fault driven)
//   ThreadSync     → Row<Block>                     (futex/yield IS block)
//   NetworkIo      → Row<IO, Block>                 (socket I/O blocks)
//   ProcessControl → Row<IO, Block>                 (fork/exec/wait heavy)
//   Privilege      → Row<IO, Block>                 (raw ioctl / ptrace)
//
// V-099's `ioctl::vendor<V>` and `ioctl::subsystem<S>` are uniformly
// pinned to SyscallFamily::Privilege (per V-099 Ioctl.h's `family_tier`
// specializations), so they all lift to `Row<IO, Block>` regardless of
// V / S — the per-vendor / per-subsystem identity is preserved AT THE
// GRANT TYPE level for downstream consumers (Mimic per-vendor backends,
// Forge phase E.RecipeSelect) but the OS-effect row is uniform.
//
// ── Why a separate header (rather than extending Family.h) ────────────
//
// Family.h holds the family_* tags themselves AND their family_tier_v
// specializations.  Per.h holds the per<> parametric AND its
// derive-via-family_of specialization.  Ioctl.h holds the ioctl::vendor
// + ioctl::subsystem parametrics AND their uniform-Privilege
// specializations.  All three are AUTHORING surfaces.
//
// Bridge.h is the CONSUMER surface — it READS family_tier_v<G> across
// every grant family AND maps the result into `effects::Row<...>`.
// Keeping the consumer surface in its own header gives the federation
// cache a stable grep target (`grep "syscall::bridge::"` finds every
// row-lift site) and parallels the V-093 (FpMode canonicalize) /
// V-100 (this header) pattern: per-axis "lift" headers live separate
// from per-axis "tag" headers.
//
// ── Substrate consumed ────────────────────────────────────────────────
//
//   crucible::fixy::grant::IsGrantTag        — structural tag concept
//   crucible::fixy::grant::which_dim_v       — axis routing
//   crucible::fixy::grant::family_tier_v     — V-098 metadata channel
//   crucible::fixy::dim::DimensionAxis       — SyscallSurface routing
//   crucible::algebra::lattices::SyscallFamily — V-097 chain enum
//   crucible::effects::Row<Es...>            — Met(X) effect-row carrier
//   crucible::effects::Effect                — Effect atom enum
//
// ── Substrate added by this header ────────────────────────────────────
//
//   * IsSyscallGrantTag<G>                   — concept gate
//   * row_for_family<F>::type                — per-family Row map
//   * row_for_family_t<F>                    — alias for the map
//   * lift_syscall_grant_row_t<G>            — concept-gated per-grant lift
//
// No new lattice, no new wrapper, no new mint factory.  Bridge.h
// composes existing substrate; it does not introduce new authoring
// surfaces.
//
// ── Self-test ─────────────────────────────────────────────────────────
//
// Seven layers:
//   1. row_for_family<F>::type for every SyscallFamily enumerator
//      matches the doc-block map exactly.
//   2. lift_syscall_grant_row_t<G> equals the family map for every
//      shipped family_* grant (V-098 Family.h, 9 tags).
//   3. lift_syscall_grant_row_t<G> equals the family map (via
//      family_of(Id)) for every shipped per<Id> grant — sampled at
//      every family-tier (V-098 Per.h, 36 enumerators).
//   4. lift_syscall_grant_row_t<G> equals Row<IO, Block> for every
//      shipped ioctl::vendor<V> + ioctl::subsystem<S> grant
//      (V-099 Ioctl.h, 12 + 15 enumerators).
//   5. IsSyscallGrantTag<G> admits every V-098/V-099 grant family.
//   6. IsSyscallGrantTag<G> REJECTS non-grant types (raw int) AND
//      grants on other axes (V-092 FpMode's fp_strict_ieee).
//   7. row_for_family<F> is exhaustive — every SyscallFamily
//      enumerator has a specialization.
//
// HS14 negative-compile fixtures (test/fixy_neg/neg_fixy_v_100_*):
//   (a) `lift_syscall_grant_row_t<int>` — rejected at the
//        IsSyscallGrantTag concept's IsGrantTag arm.
//   (b) `lift_syscall_grant_row_t<fp_strict_ieee>` — rejected at the
//        concept's `which_dim_v == SyscallSurface` arm (the grant IS
//        a well-formed grant but routes to FpMode, not SyscallSurface).
//
// ── Cost ──────────────────────────────────────────────────────────────
//
// Zero.  `row_for_family<F>::type` is a partial specialization match
// resolved at compile time.  `lift_syscall_grant_row_t<G>` is one
// alias-template instantiation per use site.  No runtime state, no
// runtime branch.

#include <crucible/fixy/syscall/Family.h>   // family_tier primary + family_* tags
#include <crucible/fixy/syscall/Per.h>      // per<SyscallId>
#include <crucible/fixy/syscall/Ioctl.h>    // ioctl::vendor + ioctl::subsystem
#include <crucible/fixy/Grant.h>            // IsGrantTag + which_dim_v
#include <crucible/fixy/Fp.h>               // fp_strict_ieee (off-axis witness for Layer 6)
#include <crucible/effects/Capabilities.h>  // Effect enum
#include <crucible/effects/EffectRow.h>     // Row<Es...>
#include <crucible/safety/DimensionTraits.h>// DimensionAxis::SyscallSurface
#include <crucible/algebra/lattices/SyscallFamilyLattice.h> // SyscallFamily

#include <meta>
#include <type_traits>

namespace crucible::fixy::syscall::bridge {

// ═════════════════════════════════════════════════════════════════════
// ── IsSyscallGrantTag — concept gate ─────────────────────────────────
// ═════════════════════════════════════════════════════════════════════
//
// G is admitted if (a) it satisfies the structural IsGrantTag concept
// (final + grant_base + cv-ref-free, from Grant.h) AND (b) its
// which_dim_v routes to SyscallSurface.  The split into two clauses
// makes HS14 fixture diagnostics name the correct mismatch class:
// non-grants fail at IsGrantTag; off-axis grants fail at the dim arm.
template <typename G>
concept IsSyscallGrantTag =
    ::crucible::fixy::grant::IsGrantTag<G> &&
    (::crucible::fixy::grant::which_dim_v<G>
     == ::crucible::fixy::dim::DimensionAxis::SyscallSurface);

// ═════════════════════════════════════════════════════════════════════
// ── row_for_family<F> — per-SyscallFamily Row map ────────────────────
// ═════════════════════════════════════════════════════════════════════
//
// One specialization per SyscallFamily enumerator.  No primary
// definition — passing a value not in the enum (e.g., a forgotten
// arm via direct enum-construction) results in a hard substitution
// failure at instantiation.

template <::crucible::algebra::lattices::SyscallFamily F>
struct row_for_family;

template <>
struct row_for_family<::crucible::algebra::lattices::SyscallFamily::NoSyscall> {
    using type = ::crucible::effects::Row<>;
};
template <>
struct row_for_family<::crucible::algebra::lattices::SyscallFamily::VdsoOnly> {
    using type = ::crucible::effects::Row<>;
};
template <>
struct row_for_family<::crucible::algebra::lattices::SyscallFamily::ReadOnlyState> {
    using type = ::crucible::effects::Row<::crucible::effects::Effect::IO>;
};
template <>
struct row_for_family<::crucible::algebra::lattices::SyscallFamily::FileMutation> {
    using type = ::crucible::effects::Row<::crucible::effects::Effect::IO,
                                          ::crucible::effects::Effect::Block>;
};
template <>
struct row_for_family<::crucible::algebra::lattices::SyscallFamily::MemoryMapping> {
    using type = ::crucible::effects::Row<::crucible::effects::Effect::IO>;
};
template <>
struct row_for_family<::crucible::algebra::lattices::SyscallFamily::ThreadSync> {
    using type = ::crucible::effects::Row<::crucible::effects::Effect::Block>;
};
template <>
struct row_for_family<::crucible::algebra::lattices::SyscallFamily::NetworkIo> {
    using type = ::crucible::effects::Row<::crucible::effects::Effect::IO,
                                          ::crucible::effects::Effect::Block>;
};
template <>
struct row_for_family<::crucible::algebra::lattices::SyscallFamily::ProcessControl> {
    using type = ::crucible::effects::Row<::crucible::effects::Effect::IO,
                                          ::crucible::effects::Effect::Block>;
};
template <>
struct row_for_family<::crucible::algebra::lattices::SyscallFamily::Privilege> {
    using type = ::crucible::effects::Row<::crucible::effects::Effect::IO,
                                          ::crucible::effects::Effect::Block>;
};

template <::crucible::algebra::lattices::SyscallFamily F>
using row_for_family_t = typename row_for_family<F>::type;

// ═════════════════════════════════════════════════════════════════════
// ── lift_syscall_grant_row_t<G> — concept-gated per-grant lift ───────
// ═════════════════════════════════════════════════════════════════════
//
// Reads family_tier_v<G> (V-098 metadata channel; defined for every
// family_*, per<Id>, ioctl::vendor<V>, ioctl::subsystem<S>) and
// dispatches through row_for_family_t.  Concept gate enforces the
// caller hands in a SyscallSurface-routing grant; non-grants and off-
// axis grants are rejected with named-concept diagnostics rather than
// raw "undefined family_tier" template errors.
template <typename G>
    requires IsSyscallGrantTag<G>
using lift_syscall_grant_row_t =
    row_for_family_t<::crucible::fixy::grant::family_tier_v<G>>;

// ═════════════════════════════════════════════════════════════════════
// ── Self-test (compile-time) ─────────────────────────────────────────
// ═════════════════════════════════════════════════════════════════════

namespace detail::syscall_bridge_self_test {

namespace gr  = ::crucible::fixy::grant;
namespace sc  = ::crucible::fixy::grant::syscall;
namespace sci = ::crucible::fixy::grant::syscall::ioctl;
namespace al  = ::crucible::algebra::lattices;
namespace fe  = ::crucible::effects;
using SF = al::SyscallFamily;
using SI = sc::SyscallId;
using IV = sc::IoctlVendor;
using IS = sc::IoctlSubsystem;

// ── Layer 1: per-family Row map matches the doc-block exactly ───────
static_assert(std::is_same_v<row_for_family_t<SF::NoSyscall>,
                             fe::Row<>>);
static_assert(std::is_same_v<row_for_family_t<SF::VdsoOnly>,
                             fe::Row<>>);
static_assert(std::is_same_v<row_for_family_t<SF::ReadOnlyState>,
                             fe::Row<fe::Effect::IO>>);
static_assert(std::is_same_v<row_for_family_t<SF::FileMutation>,
                             fe::Row<fe::Effect::IO, fe::Effect::Block>>);
static_assert(std::is_same_v<row_for_family_t<SF::MemoryMapping>,
                             fe::Row<fe::Effect::IO>>);
static_assert(std::is_same_v<row_for_family_t<SF::ThreadSync>,
                             fe::Row<fe::Effect::Block>>);
static_assert(std::is_same_v<row_for_family_t<SF::NetworkIo>,
                             fe::Row<fe::Effect::IO, fe::Effect::Block>>);
static_assert(std::is_same_v<row_for_family_t<SF::ProcessControl>,
                             fe::Row<fe::Effect::IO, fe::Effect::Block>>);
static_assert(std::is_same_v<row_for_family_t<SF::Privilege>,
                             fe::Row<fe::Effect::IO, fe::Effect::Block>>);

// ── Layer 2: V-098 Family.h grants — all 9 tags lift correctly ──────
static_assert(std::is_same_v<lift_syscall_grant_row_t<sc::family_no_syscall>,
                             fe::Row<>>);
static_assert(std::is_same_v<lift_syscall_grant_row_t<sc::family_vdso_only>,
                             fe::Row<>>);
static_assert(std::is_same_v<lift_syscall_grant_row_t<sc::family_read_only_state>,
                             fe::Row<fe::Effect::IO>>);
static_assert(std::is_same_v<lift_syscall_grant_row_t<sc::family_file_mutation>,
                             fe::Row<fe::Effect::IO, fe::Effect::Block>>);
static_assert(std::is_same_v<lift_syscall_grant_row_t<sc::family_memory_mapping>,
                             fe::Row<fe::Effect::IO>>);
static_assert(std::is_same_v<lift_syscall_grant_row_t<sc::family_thread_sync>,
                             fe::Row<fe::Effect::Block>>);
static_assert(std::is_same_v<lift_syscall_grant_row_t<sc::family_network_io>,
                             fe::Row<fe::Effect::IO, fe::Effect::Block>>);
static_assert(std::is_same_v<lift_syscall_grant_row_t<sc::family_process_control>,
                             fe::Row<fe::Effect::IO, fe::Effect::Block>>);
static_assert(std::is_same_v<lift_syscall_grant_row_t<sc::family_privilege>,
                             fe::Row<fe::Effect::IO, fe::Effect::Block>>);

// ── Layer 3: V-098 Per.h per<Id> — sampled at every family-tier ─────
// Each sample exercises one arm of family_of(SyscallId)'s switch.
static_assert(std::is_same_v<lift_syscall_grant_row_t<sc::per<SI::clock_gettime>>,
                             fe::Row<>>);                                      // VdsoOnly
static_assert(std::is_same_v<lift_syscall_grant_row_t<sc::per<SI::getpid>>,
                             fe::Row<fe::Effect::IO>>);                        // ReadOnlyState
static_assert(std::is_same_v<lift_syscall_grant_row_t<sc::per<SI::write>>,
                             fe::Row<fe::Effect::IO, fe::Effect::Block>>);     // FileMutation
static_assert(std::is_same_v<lift_syscall_grant_row_t<sc::per<SI::pwrite>>,
                             fe::Row<fe::Effect::IO, fe::Effect::Block>>);     // FileMutation
static_assert(std::is_same_v<lift_syscall_grant_row_t<sc::per<SI::mmap>>,
                             fe::Row<fe::Effect::IO>>);                        // MemoryMapping
static_assert(std::is_same_v<lift_syscall_grant_row_t<sc::per<SI::futex>>,
                             fe::Row<fe::Effect::Block>>);                     // ThreadSync
static_assert(std::is_same_v<lift_syscall_grant_row_t<sc::per<SI::socket>>,
                             fe::Row<fe::Effect::IO, fe::Effect::Block>>);     // NetworkIo
static_assert(std::is_same_v<lift_syscall_grant_row_t<sc::per<SI::clone>>,
                             fe::Row<fe::Effect::IO, fe::Effect::Block>>);     // ProcessControl
static_assert(std::is_same_v<lift_syscall_grant_row_t<sc::per<SI::ptrace>>,
                             fe::Row<fe::Effect::IO, fe::Effect::Block>>);     // Privilege

// ── Layer 4: V-099 Ioctl.h — vendor + subsystem all lift to {IO, Block}
// Every ioctl grant pins family_tier == Privilege per Ioctl.h's
// specialization; the lift is uniform.  We sample one vendor + one
// subsystem per major brand / subsystem class to witness the bridge
// reaches every shipped V-099 surface.
static_assert(std::is_same_v<lift_syscall_grant_row_t<sci::vendor<IV::nvidia_ctl>>,
                             fe::Row<fe::Effect::IO, fe::Effect::Block>>);
static_assert(std::is_same_v<lift_syscall_grant_row_t<sci::vendor<IV::nvidia_uvm>>,
                             fe::Row<fe::Effect::IO, fe::Effect::Block>>);
static_assert(std::is_same_v<lift_syscall_grant_row_t<sci::vendor<IV::amd_kfd>>,
                             fe::Row<fe::Effect::IO, fe::Effect::Block>>);
static_assert(std::is_same_v<lift_syscall_grant_row_t<sci::vendor<IV::intel_habana>>,
                             fe::Row<fe::Effect::IO, fe::Effect::Block>>);
static_assert(std::is_same_v<lift_syscall_grant_row_t<sci::vendor<IV::google_tpu>>,
                             fe::Row<fe::Effect::IO, fe::Effect::Block>>);
static_assert(std::is_same_v<lift_syscall_grant_row_t<sci::vendor<IV::aws_trainium>>,
                             fe::Row<fe::Effect::IO, fe::Effect::Block>>);

static_assert(std::is_same_v<lift_syscall_grant_row_t<sci::subsystem<IS::drm>>,
                             fe::Row<fe::Effect::IO, fe::Effect::Block>>);
static_assert(std::is_same_v<lift_syscall_grant_row_t<sci::subsystem<IS::kvm>>,
                             fe::Row<fe::Effect::IO, fe::Effect::Block>>);
static_assert(std::is_same_v<lift_syscall_grant_row_t<sci::subsystem<IS::bpf>>,
                             fe::Row<fe::Effect::IO, fe::Effect::Block>>);
static_assert(std::is_same_v<lift_syscall_grant_row_t<sci::subsystem<IS::io_uring>>,
                             fe::Row<fe::Effect::IO, fe::Effect::Block>>);
static_assert(std::is_same_v<lift_syscall_grant_row_t<sci::subsystem<IS::vfio_pci>>,
                             fe::Row<fe::Effect::IO, fe::Effect::Block>>);

// ── Layer 5: IsSyscallGrantTag admits every shipped V-098/V-099 grant
static_assert(IsSyscallGrantTag<sc::family_no_syscall>);
static_assert(IsSyscallGrantTag<sc::family_vdso_only>);
static_assert(IsSyscallGrantTag<sc::family_file_mutation>);
static_assert(IsSyscallGrantTag<sc::family_privilege>);
static_assert(IsSyscallGrantTag<sc::per<SI::clock_gettime>>);
static_assert(IsSyscallGrantTag<sc::per<SI::write>>);
static_assert(IsSyscallGrantTag<sc::per<SI::futex>>);
static_assert(IsSyscallGrantTag<sci::vendor<IV::nvidia_ctl>>);
static_assert(IsSyscallGrantTag<sci::vendor<IV::amd_kfd>>);
static_assert(IsSyscallGrantTag<sci::subsystem<IS::drm>>);
static_assert(IsSyscallGrantTag<sci::subsystem<IS::io_uring>>);

// ── Layer 6: IsSyscallGrantTag REJECTS non-grants + off-axis grants ─
// Witness via negation; HS14 fixtures witness the diagnostic-emission
// behavior at the requires-clause boundary.

// Non-grant types: IsGrantTag fails first; sub-concept short-circuits.
static_assert(!IsSyscallGrantTag<int>);
static_assert(!IsSyscallGrantTag<void>);
static_assert(!IsSyscallGrantTag<double>);
static_assert(!IsSyscallGrantTag<std::nullptr_t>);

// Off-axis grants: IsGrantTag passes, but which_dim ≠ SyscallSurface.
// fp_strict_ieee routes to DimensionAxis::FpMode (V-092); this is the
// canonical "well-formed grant but wrong axis" specimen.
static_assert(gr::IsGrantTag<gr::fp_strict_ieee>);
static_assert(gr::which_dim_v<gr::fp_strict_ieee>
              != ::crucible::fixy::dim::DimensionAxis::SyscallSurface);
static_assert(!IsSyscallGrantTag<gr::fp_strict_ieee>);

// ── Layer 7: row_for_family<F> exhaustive — reflection-driven witness
// Force-touch every SyscallFamily enumerator through row_for_family<>
// so the partial specialization match is hit for every value in the
// V-097 9-element catalog.  If a contributor adds a 10th tier to
// SyscallFamily without extending the row_for_family<> spec set, this
// expansion-statement instantiation fires at the new arm with a
// "incomplete row_for_family<...>" diagnostic.
// Local trait — `T == Row<Es...>` for some pack.  We don't export a
// concept (effects/EffectRow.h has no public `IsRow`); a private
// trait keeps the witness predicate self-contained.
template <typename T>
struct is_row : std::false_type {};
template <::crucible::effects::Effect... Es>
struct is_row<::crucible::effects::Row<Es...>> : std::true_type {};
template <typename T>
inline constexpr bool is_row_v = is_row<T>::value;

[[nodiscard]] consteval bool every_syscall_family_lifted() noexcept {
    static constexpr auto enumerators =
        std::define_static_array(std::meta::enumerators_of(^^SF));
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wshadow"
    template for (constexpr auto en : enumerators) {
        // Per-arm: instantiate row_for_family_t<F>.  The fact that this
        // line type-checks under expansion proves the spec set is
        // exhaustive (an unspecialized F would emit an "incomplete
        // type" error inside the alias template).
        // GCC 16: splice in template-arg position requires parens.
        using row_t = row_for_family_t<([:en:])>;
        static_assert(is_row_v<row_t>,
            "row_for_family<F> produced a non-Row type for some "
            "SyscallFamily enumerator — bridge map is structurally wrong.");
    }
#pragma GCC diagnostic pop
    return true;
}
static_assert(every_syscall_family_lifted(),
    "FIXY-V-100: at least one SyscallFamily enumerator lacks a "
    "row_for_family<> specialization OR produces a non-Row type.  "
    "If you added a tier to SyscallFamily (V-097), extend "
    "row_for_family<> in fixy/syscall/Bridge.h.");

// Cross-check against V-097's 9-tier lattice cardinality.
static_assert(
    ::crucible::algebra::lattices::detail::syscall_family_lattice_self_test::family_count == 9,
    "FIXY-V-100: Bridge.h's per-family Row map depends on V-097's "
    "9-tier chain.  If the lattice grew an enumerator, row_for_family "
    "is incomplete and the every_syscall_family_lifted() witness fires.");

}  // namespace detail::syscall_bridge_self_test

}  // namespace crucible::fixy::syscall::bridge
