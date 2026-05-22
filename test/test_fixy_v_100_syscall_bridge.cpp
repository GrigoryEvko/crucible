// FIXY-V-100 sentinel TU: fixy/syscall/Bridge.h.
//
// V-100 ships the SyscallSurface → Met(X) effect-row bridge:
//   * IsSyscallGrantTag<G>            — concept gate (admits V-098
//                                       Family.h + Per.h + V-099 Ioctl.h
//                                       grants; rejects non-grants and
//                                       off-axis grants).
//   * row_for_family<F>::type         — per-SyscallFamily Row<...> map.
//   * lift_syscall_grant_row_t<G>     — concept-gated per-grant lift
//                                       reading family_tier_v<G>.
//
// The V-097/V-098/V-099/V-100 chain is now structurally closed: a
// binding declaring a syscall grant (via any of the three V-098/V-099
// authoring surfaces) automatically receives the matching effect-row
// contribution at compile time, with ZERO runtime cost.  This sentinel
// TU witnesses cross-Header end-to-end consistency.
//
// Why a sentinel TU vs header-only static_asserts: per
// feedback_header_only_static_assert_blind_spot — headers shipped with
// embedded static_asserts aren't verified under project warning flags
// unless a .cpp TU includes them.  This TU forces Bridge.h (and
// transitively Family.h + Per.h + Ioctl.h + EffectRow.h) through the
// project's default compile preset.

#include <crucible/fixy/syscall/Family.h>
#include <crucible/fixy/syscall/Per.h>
#include <crucible/fixy/syscall/Ioctl.h>
#include <crucible/fixy/syscall/Bridge.h>
#include <crucible/effects/Capabilities.h>
#include <crucible/effects/EffectRow.h>

#include <type_traits>

namespace cg  = ::crucible::fixy::grant;
namespace cgs = ::crucible::fixy::grant::syscall;
namespace cgi = ::crucible::fixy::grant::syscall::ioctl;
namespace cb  = ::crucible::fixy::syscall::bridge;
namespace cal = ::crucible::algebra::lattices;
namespace cfe = ::crucible::effects;

namespace {

// ── Layer 1: end-to-end V-098 family_* → Met(X) row ──────────────────
// Every family_* grant lifts to exactly the doc-block row.  This is
// the canonical bridge use case — a binding declares
// `family_file_mutation` and the row contribution is automatically
// `Row<IO, Block>` at compile time.
static_assert(std::is_same_v<cb::lift_syscall_grant_row_t<cgs::family_no_syscall>,
                             cfe::Row<>>);
static_assert(std::is_same_v<cb::lift_syscall_grant_row_t<cgs::family_vdso_only>,
                             cfe::Row<>>);
static_assert(std::is_same_v<cb::lift_syscall_grant_row_t<cgs::family_read_only_state>,
                             cfe::Row<cfe::Effect::IO>>);
static_assert(std::is_same_v<cb::lift_syscall_grant_row_t<cgs::family_file_mutation>,
                             cfe::Row<cfe::Effect::IO, cfe::Effect::Block>>);
static_assert(std::is_same_v<cb::lift_syscall_grant_row_t<cgs::family_memory_mapping>,
                             cfe::Row<cfe::Effect::IO>>);
static_assert(std::is_same_v<cb::lift_syscall_grant_row_t<cgs::family_thread_sync>,
                             cfe::Row<cfe::Effect::Block>>);
static_assert(std::is_same_v<cb::lift_syscall_grant_row_t<cgs::family_network_io>,
                             cfe::Row<cfe::Effect::IO, cfe::Effect::Block>>);
static_assert(std::is_same_v<cb::lift_syscall_grant_row_t<cgs::family_process_control>,
                             cfe::Row<cfe::Effect::IO, cfe::Effect::Block>>);
static_assert(std::is_same_v<cb::lift_syscall_grant_row_t<cgs::family_privilege>,
                             cfe::Row<cfe::Effect::IO, cfe::Effect::Block>>);

// ── Layer 2: V-098 per<Id> coherence — family_* and per<Id> on the
// SAME family lift to the SAME Row.  This is the load-bearing
// federation cache property: a binding declaring
// `family_file_mutation` vs `per<SyscallId::pwrite>` (both classify
// as FileMutation) produces the same Met(X) row — V-100's bridge is
// the unification point.
static_assert(std::is_same_v<cb::lift_syscall_grant_row_t<cgs::family_file_mutation>,
                             cb::lift_syscall_grant_row_t<cgs::per<cgs::SyscallId::write>>>);
static_assert(std::is_same_v<cb::lift_syscall_grant_row_t<cgs::family_file_mutation>,
                             cb::lift_syscall_grant_row_t<cgs::per<cgs::SyscallId::pwrite>>>);
static_assert(std::is_same_v<cb::lift_syscall_grant_row_t<cgs::family_file_mutation>,
                             cb::lift_syscall_grant_row_t<cgs::per<cgs::SyscallId::fdatasync>>>);
static_assert(std::is_same_v<cb::lift_syscall_grant_row_t<cgs::family_thread_sync>,
                             cb::lift_syscall_grant_row_t<cgs::per<cgs::SyscallId::futex>>>);
static_assert(std::is_same_v<cb::lift_syscall_grant_row_t<cgs::family_network_io>,
                             cb::lift_syscall_grant_row_t<cgs::per<cgs::SyscallId::sendmsg>>>);
static_assert(std::is_same_v<cb::lift_syscall_grant_row_t<cgs::family_memory_mapping>,
                             cb::lift_syscall_grant_row_t<cgs::per<cgs::SyscallId::mmap>>>);
static_assert(std::is_same_v<cb::lift_syscall_grant_row_t<cgs::family_process_control>,
                             cb::lift_syscall_grant_row_t<cgs::per<cgs::SyscallId::clone>>>);
static_assert(std::is_same_v<cb::lift_syscall_grant_row_t<cgs::family_privilege>,
                             cb::lift_syscall_grant_row_t<cgs::per<cgs::SyscallId::ptrace>>>);
static_assert(std::is_same_v<cb::lift_syscall_grant_row_t<cgs::family_vdso_only>,
                             cb::lift_syscall_grant_row_t<cgs::per<cgs::SyscallId::clock_gettime>>>);

// ── Layer 3: V-099 Ioctl.h — every ioctl grant collapses to {IO, Block}
// ioctl::vendor<V> and ioctl::subsystem<S> are uniformly Privilege-
// pinned per Ioctl.h's `family_tier` specialization, so every
// instantiation lifts to the same Row regardless of V / S.
static_assert(std::is_same_v<cb::lift_syscall_grant_row_t<cgi::vendor<cgs::IoctlVendor::nvidia_uvm>>,
                             cb::lift_syscall_grant_row_t<cgs::family_privilege>>);
static_assert(std::is_same_v<cb::lift_syscall_grant_row_t<cgi::vendor<cgs::IoctlVendor::amd_kfd>>,
                             cb::lift_syscall_grant_row_t<cgs::family_privilege>>);
static_assert(std::is_same_v<cb::lift_syscall_grant_row_t<cgi::vendor<cgs::IoctlVendor::intel_habana>>,
                             cb::lift_syscall_grant_row_t<cgs::family_privilege>>);
static_assert(std::is_same_v<cb::lift_syscall_grant_row_t<cgi::subsystem<cgs::IoctlSubsystem::drm>>,
                             cb::lift_syscall_grant_row_t<cgs::family_privilege>>);
static_assert(std::is_same_v<cb::lift_syscall_grant_row_t<cgi::subsystem<cgs::IoctlSubsystem::kvm>>,
                             cb::lift_syscall_grant_row_t<cgs::family_privilege>>);
static_assert(std::is_same_v<cb::lift_syscall_grant_row_t<cgi::subsystem<cgs::IoctlSubsystem::io_uring>>,
                             cb::lift_syscall_grant_row_t<cgs::family_privilege>>);

// ── Layer 4: V-098 Per.h SyscallId-vs-IoctlVendor sanity — the two
// 16-bit ABI catalogs share underlying width but PRODUCE DIFFERENT
// row lifts when their family classifications differ.  This is the
// load-bearing distinctness witness: a `per<SyscallId::clock_gettime>`
// (VdsoOnly → empty row) and an `ioctl::vendor<IoctlVendor::nvidia_ctl>`
// (Privilege → {IO, Block}) lift to DISTINCT rows even though both
// catalogs have ordinal 0.  Confirms the bridge keys on
// family_tier_v<G>, not on raw enumerator value.
static_assert(!std::is_same_v<cb::lift_syscall_grant_row_t<cgs::per<cgs::SyscallId::clock_gettime>>,
                              cb::lift_syscall_grant_row_t<cgi::vendor<cgs::IoctlVendor::nvidia_ctl>>>);
static_assert(std::is_same_v<cb::lift_syscall_grant_row_t<cgs::per<cgs::SyscallId::clock_gettime>>,
                             cfe::Row<>>);  // VdsoOnly
static_assert(std::is_same_v<cb::lift_syscall_grant_row_t<cgi::vendor<cgs::IoctlVendor::nvidia_ctl>>,
                             cfe::Row<cfe::Effect::IO, cfe::Effect::Block>>);  // Privilege

// ── Layer 5: concept admits / rejects boundary witness ──────────────
// Bridge.h's IsSyscallGrantTag<G> is the structural gate every
// SyscallSurface-routing consumer should use.  Test it admits each
// V-098/V-099 grant family AND rejects non-grants + off-axis grants.

// Admits — sampled across all 4 grant families (family_*, per<>,
// ioctl::vendor, ioctl::subsystem).
static_assert(cb::IsSyscallGrantTag<cgs::family_no_syscall>);
static_assert(cb::IsSyscallGrantTag<cgs::family_privilege>);
static_assert(cb::IsSyscallGrantTag<cgs::per<cgs::SyscallId::write>>);
static_assert(cb::IsSyscallGrantTag<cgs::per<cgs::SyscallId::futex>>);
static_assert(cb::IsSyscallGrantTag<cgi::vendor<cgs::IoctlVendor::nvidia_ctl>>);
static_assert(cb::IsSyscallGrantTag<cgi::subsystem<cgs::IoctlSubsystem::drm>>);

// Rejects — non-grant types fail at IsGrantTag arm.
static_assert(!cb::IsSyscallGrantTag<int>);
static_assert(!cb::IsSyscallGrantTag<void>);
static_assert(!cb::IsSyscallGrantTag<double>);

// Rejects — off-axis grant (V-092 fp_strict_ieee routes to FpMode).
// The IsGrantTag arm PASSES, but the which_dim arm FAILS.  This is
// the structurally-distinct mismatch class HS14 fixture (b) exploits.
static_assert(!cb::IsSyscallGrantTag<cg::fp_strict_ieee>);

}  // namespace

int main() { return 0; }
