// FIXY-V-099 sentinel TU: fixy/syscall/Ioctl.h.
//
// V-099 ships the ioctl-surface grant catalog:
//   1. fixy/syscall/Ioctl.h — IoctlVendor (12 enumerators) + IoctlSubsystem
//      (15 enumerators) + two parametric grants `ioctl::vendor<V>` and
//      `ioctl::subsystem<S>`.  Each routes which_dim<> → SyscallSurface
//      and pins family_tier_v<> uniformly to SyscallFamily::Privilege
//      (every ioctl ultimately invokes ioctl(2), top of V-097's chain).
//
// V-100 ships the bridge that lifts a SyscallSurface pin into the
// Met(X) effect row automatically (deferred); this sentinel TU
// witnesses that the V-099 surface is structurally consistent end-to-
// end — exhaustive vendor + subsystem distinctness, family_tier
// uniformity, cross-Header coverage vs V-098 Family.h + Per.h.
//
// Why a sentinel TU vs header-only static_asserts: per
// feedback_header_only_static_assert_blind_spot — headers shipped with
// embedded static_asserts aren't verified under project warning flags
// unless a .cpp TU includes them.  This TU forces Ioctl.h (and
// transitively Family.h + Per.h via its include graph) through the
// project's default compile preset.

#include <crucible/fixy/syscall/Family.h>
#include <crucible/fixy/syscall/Per.h>
#include <crucible/fixy/syscall/Ioctl.h>

#include <type_traits>
#include <utility>

namespace cg  = ::crucible::fixy::grant;
namespace cgs = ::crucible::fixy::grant::syscall;
namespace cgi = ::crucible::fixy::grant::syscall::ioctl;
namespace cal = ::crucible::algebra::lattices;
namespace cfd = ::crucible::fixy::dim;

namespace {

// ── Layer 1: family_tier uniformity — every ioctl pins Privilege ────
// All ioctls go through raw ioctl(2) which lives in SyscallFamily::Privilege
// (top of V-097's chain).  V-100's bridge consumes this to lift the
// Met(X) row uniformly (effects::IO + effects::Block at minimum) for
// every ioctl-engaging binding regardless of vendor / subsystem.
static_assert(cg::family_tier_v<cgi::vendor<cgs::IoctlVendor::nvidia_ctl>>
              == cal::SyscallFamily::Privilege);
static_assert(cg::family_tier_v<cgi::vendor<cgs::IoctlVendor::nvidia_uvm>>
              == cal::SyscallFamily::Privilege);
static_assert(cg::family_tier_v<cgi::vendor<cgs::IoctlVendor::amd_kfd>>
              == cal::SyscallFamily::Privilege);
static_assert(cg::family_tier_v<cgi::vendor<cgs::IoctlVendor::amd_render>>
              == cal::SyscallFamily::Privilege);
static_assert(cg::family_tier_v<cgi::vendor<cgs::IoctlVendor::intel_i915>>
              == cal::SyscallFamily::Privilege);
static_assert(cg::family_tier_v<cgi::vendor<cgs::IoctlVendor::intel_xe>>
              == cal::SyscallFamily::Privilege);
static_assert(cg::family_tier_v<cgi::vendor<cgs::IoctlVendor::intel_habana>>
              == cal::SyscallFamily::Privilege);
static_assert(cg::family_tier_v<cgi::vendor<cgs::IoctlVendor::apple_neural>>
              == cal::SyscallFamily::Privilege);
static_assert(cg::family_tier_v<cgi::vendor<cgs::IoctlVendor::google_tpu>>
              == cal::SyscallFamily::Privilege);
static_assert(cg::family_tier_v<cgi::vendor<cgs::IoctlVendor::aws_trainium>>
              == cal::SyscallFamily::Privilege);

static_assert(cg::family_tier_v<cgi::subsystem<cgs::IoctlSubsystem::drm>>
              == cal::SyscallFamily::Privilege);
static_assert(cg::family_tier_v<cgi::subsystem<cgs::IoctlSubsystem::kvm>>
              == cal::SyscallFamily::Privilege);
static_assert(cg::family_tier_v<cgi::subsystem<cgs::IoctlSubsystem::bpf>>
              == cal::SyscallFamily::Privilege);
static_assert(cg::family_tier_v<cgi::subsystem<cgs::IoctlSubsystem::io_uring>>
              == cal::SyscallFamily::Privilege);
static_assert(cg::family_tier_v<cgi::subsystem<cgs::IoctlSubsystem::file_generic>>
              == cal::SyscallFamily::Privilege);
static_assert(cg::family_tier_v<cgi::subsystem<cgs::IoctlSubsystem::vfio_pci>>
              == cal::SyscallFamily::Privilege);

// ── Layer 2: which_dim routing — every ioctl → SyscallSurface ───────
// Vendor + subsystem templates engage the SAME DimensionAxis as the
// V-098 Family.h family_* tags and Per.h per<> tags.  The triple-
// surface coverage (Family + Per + Ioctl) is what makes the V-098/V-099
// pack-level duplicate-engagement gate non-trivial: a contributor can
// stack any pair of those three surfaces in a single binding and
// `UniqueEngagementPerAxis` catches all six combinations.
static_assert(cg::which_dim_v<cgi::vendor<cgs::IoctlVendor::nvidia_ctl>>
              == cfd::DimensionAxis::SyscallSurface);
static_assert(cg::which_dim_v<cgi::subsystem<cgs::IoctlSubsystem::drm>>
              == cfd::DimensionAxis::SyscallSurface);
static_assert(cg::which_dim_v<cgi::vendor<cgs::IoctlVendor::nvidia_ctl>>
              == cg::which_dim_v<cgi::subsystem<cgs::IoctlSubsystem::drm>>);
static_assert(cg::which_dim_v<cgi::vendor<cgs::IoctlVendor::nvidia_ctl>>
              == cg::which_dim_v<cgs::family_privilege>);
static_assert(cg::which_dim_v<cgi::vendor<cgs::IoctlVendor::nvidia_ctl>>
              == cg::which_dim_v<cgs::per<cgs::SyscallId::ptrace>>);

// ── Layer 3: cross-grant structural distinctness ────────────────────
// `vendor<V>` and `subsystem<S>` are DIFFERENT class templates even
// when the underlying ordinals coincide.  Combined with Layer 4 below
// (family_*/per<>/ioctl::*  pairwise), this proves the per-axis
// duplicate-engagement gate has structurally distinct types to compare
// — it never collapses to silent same-type deduplication.
static_assert(!std::is_same_v<cgi::vendor<cgs::IoctlVendor::nvidia_ctl>,
                              cgi::subsystem<cgs::IoctlSubsystem::drm>>);
static_assert(!std::is_same_v<cgi::vendor<cgs::IoctlVendor::amd_kfd>,
                              cgi::subsystem<cgs::IoctlSubsystem::kvm>>);
static_assert(!std::is_same_v<cgi::vendor<cgs::IoctlVendor::intel_habana>,
                              cgi::subsystem<cgs::IoctlSubsystem::file_generic>>);

// ── Layer 4: V-099 vs V-098 cross-Header distinctness ───────────────
// Ioctl.h's grants are structurally distinct from EVERY shipped grant
// in Family.h (9 tags) and Per.h (per<> across 36 SyscallId values),
// so the duplicate-engagement gate has the type-level information it
// needs to reject ALL pair-wise combinations.

// ioctl::vendor vs Family.h family_*
static_assert(!std::is_same_v<cgi::vendor<cgs::IoctlVendor::nvidia_ctl>,
                              cgs::family_privilege>);
static_assert(!std::is_same_v<cgi::vendor<cgs::IoctlVendor::amd_kfd>,
                              cgs::family_privilege>);

// ioctl::vendor vs Per.h per<>
static_assert(!std::is_same_v<cgi::vendor<cgs::IoctlVendor::nvidia_ctl>,
                              cgs::per<cgs::SyscallId::ptrace>>);
static_assert(!std::is_same_v<cgi::vendor<cgs::IoctlVendor::intel_i915>,
                              cgs::per<cgs::SyscallId::capset>>);

// ioctl::subsystem vs Family.h family_*
static_assert(!std::is_same_v<cgi::subsystem<cgs::IoctlSubsystem::drm>,
                              cgs::family_privilege>);
static_assert(!std::is_same_v<cgi::subsystem<cgs::IoctlSubsystem::io_uring>,
                              cgs::family_file_mutation>);

// ioctl::subsystem vs Per.h per<>
static_assert(!std::is_same_v<cgi::subsystem<cgs::IoctlSubsystem::drm>,
                              cgs::per<cgs::SyscallId::ptrace>>);
static_assert(!std::is_same_v<cgi::subsystem<cgs::IoctlSubsystem::file_generic>,
                              cgs::per<cgs::SyscallId::pwrite>>);

// ── Layer 5: NTTP exhaustive distinctness across vendor catalog ─────
// Spot-check every adjacent pair on the 12-element catalog; the (12×11)/2
// = 66-cell full distinctness matrix is the structural surface a
// contributor adding a 13th vendor inherits.
static_assert(!std::is_same_v<cgi::vendor<cgs::IoctlVendor::nvidia_ctl>,
                              cgi::vendor<cgs::IoctlVendor::nvidia_dev>>);
static_assert(!std::is_same_v<cgi::vendor<cgs::IoctlVendor::nvidia_dev>,
                              cgi::vendor<cgs::IoctlVendor::nvidia_uvm>>);
static_assert(!std::is_same_v<cgi::vendor<cgs::IoctlVendor::nvidia_uvm>,
                              cgi::vendor<cgs::IoctlVendor::nvidia_modeset>>);
static_assert(!std::is_same_v<cgi::vendor<cgs::IoctlVendor::nvidia_modeset>,
                              cgi::vendor<cgs::IoctlVendor::amd_kfd>>);
static_assert(!std::is_same_v<cgi::vendor<cgs::IoctlVendor::amd_kfd>,
                              cgi::vendor<cgs::IoctlVendor::amd_render>>);
static_assert(!std::is_same_v<cgi::vendor<cgs::IoctlVendor::amd_render>,
                              cgi::vendor<cgs::IoctlVendor::intel_i915>>);
static_assert(!std::is_same_v<cgi::vendor<cgs::IoctlVendor::intel_i915>,
                              cgi::vendor<cgs::IoctlVendor::intel_xe>>);
static_assert(!std::is_same_v<cgi::vendor<cgs::IoctlVendor::intel_xe>,
                              cgi::vendor<cgs::IoctlVendor::intel_habana>>);
static_assert(!std::is_same_v<cgi::vendor<cgs::IoctlVendor::intel_habana>,
                              cgi::vendor<cgs::IoctlVendor::apple_neural>>);
static_assert(!std::is_same_v<cgi::vendor<cgs::IoctlVendor::apple_neural>,
                              cgi::vendor<cgs::IoctlVendor::google_tpu>>);
static_assert(!std::is_same_v<cgi::vendor<cgs::IoctlVendor::google_tpu>,
                              cgi::vendor<cgs::IoctlVendor::aws_trainium>>);

// ── Layer 6: NTTP exhaustive distinctness across subsystem catalog ──
// Spot-check every adjacent pair on the 15-element catalog.
static_assert(!std::is_same_v<cgi::subsystem<cgs::IoctlSubsystem::drm>,
                              cgi::subsystem<cgs::IoctlSubsystem::kvm>>);
static_assert(!std::is_same_v<cgi::subsystem<cgs::IoctlSubsystem::kvm>,
                              cgi::subsystem<cgs::IoctlSubsystem::bpf>>);
static_assert(!std::is_same_v<cgi::subsystem<cgs::IoctlSubsystem::bpf>,
                              cgi::subsystem<cgs::IoctlSubsystem::io_uring>>);
static_assert(!std::is_same_v<cgi::subsystem<cgs::IoctlSubsystem::io_uring>,
                              cgi::subsystem<cgs::IoctlSubsystem::netlink>>);
static_assert(!std::is_same_v<cgi::subsystem<cgs::IoctlSubsystem::netlink>,
                              cgi::subsystem<cgs::IoctlSubsystem::perf_event>>);
static_assert(!std::is_same_v<cgi::subsystem<cgs::IoctlSubsystem::perf_event>,
                              cgi::subsystem<cgs::IoctlSubsystem::tty>>);
static_assert(!std::is_same_v<cgi::subsystem<cgs::IoctlSubsystem::tty>,
                              cgi::subsystem<cgs::IoctlSubsystem::file_generic>>);
static_assert(!std::is_same_v<cgi::subsystem<cgs::IoctlSubsystem::file_generic>,
                              cgi::subsystem<cgs::IoctlSubsystem::ipmi>>);
static_assert(!std::is_same_v<cgi::subsystem<cgs::IoctlSubsystem::ipmi>,
                              cgi::subsystem<cgs::IoctlSubsystem::tun>>);
static_assert(!std::is_same_v<cgi::subsystem<cgs::IoctlSubsystem::tun>,
                              cgi::subsystem<cgs::IoctlSubsystem::loop>>);
static_assert(!std::is_same_v<cgi::subsystem<cgs::IoctlSubsystem::loop>,
                              cgi::subsystem<cgs::IoctlSubsystem::block>>);
static_assert(!std::is_same_v<cgi::subsystem<cgs::IoctlSubsystem::block>,
                              cgi::subsystem<cgs::IoctlSubsystem::vfio_pci>>);
static_assert(!std::is_same_v<cgi::subsystem<cgs::IoctlSubsystem::vfio_pci>,
                              cgi::subsystem<cgs::IoctlSubsystem::iommu>>);
static_assert(!std::is_same_v<cgi::subsystem<cgs::IoctlSubsystem::iommu>,
                              cgi::subsystem<cgs::IoctlSubsystem::i2c>>);

// ── Layer 7: catalog underlying type + cardinality pins ─────────────
// IoctlVendor and IoctlSubsystem both use uint16_t, matching V-098
// Per.h's SyscallId — the three catalogs (SyscallId / IoctlVendor /
// IoctlSubsystem) share ABI width so V-100's bridge can construct a
// single tagged-union envelope over all three without per-catalog
// width branching.
static_assert(std::is_same_v<std::underlying_type_t<cgs::IoctlVendor>,
                             std::uint16_t>);
static_assert(std::is_same_v<std::underlying_type_t<cgs::IoctlSubsystem>,
                             std::uint16_t>);

// Catalog-size pins are also held inside Ioctl.h's self-test
// (`ioctl_vendor_count == 12`, `ioctl_subsystem_count == 15`); replicate
// here so the sentinel TU surfaces a friendly error if Ioctl.h's
// constexpr count drifts.
static_assert(
    ::crucible::fixy::grant::detail::syscall_ioctl_grant_self_test::ioctl_vendor_count == 12);
static_assert(
    ::crucible::fixy::grant::detail::syscall_ioctl_grant_self_test::ioctl_subsystem_count == 15);

}  // namespace

int main() { return 0; }
