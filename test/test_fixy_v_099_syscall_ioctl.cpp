// Assertions embedded in a header are only verified under the
// project's warning flags when some translation unit includes that
// header.  This file exists to be that translation unit for the ioctl
// grant catalogs and everything their include graph pulls in.

#include <crucible/fixy/syscall/Family.h>
#include <crucible/fixy/syscall/Per.h>
#include <crucible/fixy/syscall/Ioctl.h>

#include <type_traits>
#include <utility>

namespace cg = ::crucible::fixy::grant;
namespace cgs = ::crucible::fixy::grant::syscall;
namespace cgi = ::crucible::fixy::grant::syscall::ioctl;
namespace cal = ::crucible::algebra::lattices;
namespace cfd = ::crucible::fixy::dim;

namespace {

// Every grant here pins the same family because every ioctl reaches
// the kernel through ioctl(2), which sits at the top of the chain.
// Vendor and subsystem change nothing about that.
static_assert(cg::family_tier_v<cgi::vendor<cgs::IoctlVendor::nvidia_ctl>> == cal::SyscallFamily::Privilege);
static_assert(cg::family_tier_v<cgi::vendor<cgs::IoctlVendor::nvidia_uvm>> == cal::SyscallFamily::Privilege);
static_assert(cg::family_tier_v<cgi::vendor<cgs::IoctlVendor::amd_kfd>> == cal::SyscallFamily::Privilege);
static_assert(cg::family_tier_v<cgi::vendor<cgs::IoctlVendor::amd_render>> == cal::SyscallFamily::Privilege);
static_assert(cg::family_tier_v<cgi::vendor<cgs::IoctlVendor::intel_i915>> == cal::SyscallFamily::Privilege);
static_assert(cg::family_tier_v<cgi::vendor<cgs::IoctlVendor::intel_xe>> == cal::SyscallFamily::Privilege);
static_assert(cg::family_tier_v<cgi::vendor<cgs::IoctlVendor::intel_habana>> == cal::SyscallFamily::Privilege);
static_assert(cg::family_tier_v<cgi::vendor<cgs::IoctlVendor::apple_neural>> == cal::SyscallFamily::Privilege);
static_assert(cg::family_tier_v<cgi::vendor<cgs::IoctlVendor::google_tpu>> == cal::SyscallFamily::Privilege);
static_assert(cg::family_tier_v<cgi::vendor<cgs::IoctlVendor::aws_trainium>> == cal::SyscallFamily::Privilege);

static_assert(cg::family_tier_v<cgi::subsystem<cgs::IoctlSubsystem::drm>> == cal::SyscallFamily::Privilege);
static_assert(cg::family_tier_v<cgi::subsystem<cgs::IoctlSubsystem::kvm>> == cal::SyscallFamily::Privilege);
static_assert(cg::family_tier_v<cgi::subsystem<cgs::IoctlSubsystem::bpf>> == cal::SyscallFamily::Privilege);
static_assert(cg::family_tier_v<cgi::subsystem<cgs::IoctlSubsystem::io_uring>> == cal::SyscallFamily::Privilege);
static_assert(cg::family_tier_v<cgi::subsystem<cgs::IoctlSubsystem::file_generic>> == cal::SyscallFamily::Privilege);
static_assert(cg::family_tier_v<cgi::subsystem<cgs::IoctlSubsystem::vfio_pci>> == cal::SyscallFamily::Privilege);

// Vendor, subsystem, family and per-syscall grants all engage one and
// the same axis.  That is what makes the duplicate-engagement gate
// non-trivial: a binding can stack any pair of those surfaces, and the
// gate has to catch every combination.
static_assert(cg::which_dim_v<cgi::vendor<cgs::IoctlVendor::nvidia_ctl>> == cfd::DimensionAxis::SyscallSurface);
static_assert(cg::which_dim_v<cgi::subsystem<cgs::IoctlSubsystem::drm>> == cfd::DimensionAxis::SyscallSurface);
static_assert(cg::which_dim_v<cgi::vendor<cgs::IoctlVendor::nvidia_ctl>>
              == cg::which_dim_v<cgi::subsystem<cgs::IoctlSubsystem::drm>>);
static_assert(cg::which_dim_v<cgi::vendor<cgs::IoctlVendor::nvidia_ctl>> == cg::which_dim_v<cgs::family_privilege>);
static_assert(cg::which_dim_v<cgi::vendor<cgs::IoctlVendor::nvidia_ctl>>
              == cg::which_dim_v<cgs::per<cgs::SyscallId::ptrace>>);

// `vendor<V>` and `subsystem<S>` stay distinct types even when their
// ordinals coincide.  Without that, the duplicate-engagement gate
// would collapse into silent same-type deduplication and stop
// rejecting anything.
static_assert(!std::is_same_v<cgi::vendor<cgs::IoctlVendor::nvidia_ctl>, cgi::subsystem<cgs::IoctlSubsystem::drm>>);
static_assert(!std::is_same_v<cgi::vendor<cgs::IoctlVendor::amd_kfd>, cgi::subsystem<cgs::IoctlSubsystem::kvm>>);
static_assert(
    !std::is_same_v<cgi::vendor<cgs::IoctlVendor::intel_habana>, cgi::subsystem<cgs::IoctlSubsystem::file_generic>>);

// The same argument runs across the coarse family tags and the
// per-syscall tags, so the gate can reject every pairwise combination
// of the three surfaces.

static_assert(!std::is_same_v<cgi::vendor<cgs::IoctlVendor::nvidia_ctl>, cgs::family_privilege>);
static_assert(!std::is_same_v<cgi::vendor<cgs::IoctlVendor::amd_kfd>, cgs::family_privilege>);

static_assert(!std::is_same_v<cgi::vendor<cgs::IoctlVendor::nvidia_ctl>, cgs::per<cgs::SyscallId::ptrace>>);
static_assert(!std::is_same_v<cgi::vendor<cgs::IoctlVendor::intel_i915>, cgs::per<cgs::SyscallId::capset>>);

static_assert(!std::is_same_v<cgi::subsystem<cgs::IoctlSubsystem::drm>, cgs::family_privilege>);
static_assert(!std::is_same_v<cgi::subsystem<cgs::IoctlSubsystem::io_uring>, cgs::family_file_mutation>);

static_assert(!std::is_same_v<cgi::subsystem<cgs::IoctlSubsystem::drm>, cgs::per<cgs::SyscallId::ptrace>>);
static_assert(!std::is_same_v<cgi::subsystem<cgs::IoctlSubsystem::file_generic>, cgs::per<cgs::SyscallId::pwrite>>);

// Adjacent pairs stand in for the full 66-cell distinctness matrix
// over the vendor catalog.  Adjacency is the ordering a reordering or
// a duplicated enumerator value disturbs first.
static_assert(!std::is_same_v<cgi::vendor<cgs::IoctlVendor::nvidia_ctl>, cgi::vendor<cgs::IoctlVendor::nvidia_dev>>);
static_assert(!std::is_same_v<cgi::vendor<cgs::IoctlVendor::nvidia_dev>, cgi::vendor<cgs::IoctlVendor::nvidia_uvm>>);
static_assert(
    !std::is_same_v<cgi::vendor<cgs::IoctlVendor::nvidia_uvm>, cgi::vendor<cgs::IoctlVendor::nvidia_modeset>>);
static_assert(!std::is_same_v<cgi::vendor<cgs::IoctlVendor::nvidia_modeset>, cgi::vendor<cgs::IoctlVendor::amd_kfd>>);
static_assert(!std::is_same_v<cgi::vendor<cgs::IoctlVendor::amd_kfd>, cgi::vendor<cgs::IoctlVendor::amd_render>>);
static_assert(!std::is_same_v<cgi::vendor<cgs::IoctlVendor::amd_render>, cgi::vendor<cgs::IoctlVendor::intel_i915>>);
static_assert(!std::is_same_v<cgi::vendor<cgs::IoctlVendor::intel_i915>, cgi::vendor<cgs::IoctlVendor::intel_xe>>);
static_assert(!std::is_same_v<cgi::vendor<cgs::IoctlVendor::intel_xe>, cgi::vendor<cgs::IoctlVendor::intel_habana>>);
static_assert(
    !std::is_same_v<cgi::vendor<cgs::IoctlVendor::intel_habana>, cgi::vendor<cgs::IoctlVendor::apple_neural>>);
static_assert(!std::is_same_v<cgi::vendor<cgs::IoctlVendor::apple_neural>, cgi::vendor<cgs::IoctlVendor::google_tpu>>);
static_assert(!std::is_same_v<cgi::vendor<cgs::IoctlVendor::google_tpu>, cgi::vendor<cgs::IoctlVendor::aws_trainium>>);

// The same adjacent-pair sweep over the subsystem catalog.
static_assert(!std::is_same_v<cgi::subsystem<cgs::IoctlSubsystem::drm>, cgi::subsystem<cgs::IoctlSubsystem::kvm>>);
static_assert(!std::is_same_v<cgi::subsystem<cgs::IoctlSubsystem::kvm>, cgi::subsystem<cgs::IoctlSubsystem::bpf>>);
static_assert(!std::is_same_v<cgi::subsystem<cgs::IoctlSubsystem::bpf>, cgi::subsystem<cgs::IoctlSubsystem::io_uring>>);
static_assert(
    !std::is_same_v<cgi::subsystem<cgs::IoctlSubsystem::io_uring>, cgi::subsystem<cgs::IoctlSubsystem::netlink>>);
static_assert(
    !std::is_same_v<cgi::subsystem<cgs::IoctlSubsystem::netlink>, cgi::subsystem<cgs::IoctlSubsystem::perf_event>>);
static_assert(
    !std::is_same_v<cgi::subsystem<cgs::IoctlSubsystem::perf_event>, cgi::subsystem<cgs::IoctlSubsystem::tty>>);
static_assert(
    !std::is_same_v<cgi::subsystem<cgs::IoctlSubsystem::tty>, cgi::subsystem<cgs::IoctlSubsystem::file_generic>>);
static_assert(
    !std::is_same_v<cgi::subsystem<cgs::IoctlSubsystem::file_generic>, cgi::subsystem<cgs::IoctlSubsystem::ipmi>>);
static_assert(!std::is_same_v<cgi::subsystem<cgs::IoctlSubsystem::ipmi>, cgi::subsystem<cgs::IoctlSubsystem::tun>>);
static_assert(!std::is_same_v<cgi::subsystem<cgs::IoctlSubsystem::tun>, cgi::subsystem<cgs::IoctlSubsystem::loop>>);
static_assert(!std::is_same_v<cgi::subsystem<cgs::IoctlSubsystem::loop>, cgi::subsystem<cgs::IoctlSubsystem::block>>);
static_assert(
    !std::is_same_v<cgi::subsystem<cgs::IoctlSubsystem::block>, cgi::subsystem<cgs::IoctlSubsystem::vfio_pci>>);
static_assert(
    !std::is_same_v<cgi::subsystem<cgs::IoctlSubsystem::vfio_pci>, cgi::subsystem<cgs::IoctlSubsystem::iommu>>);
static_assert(!std::is_same_v<cgi::subsystem<cgs::IoctlSubsystem::iommu>, cgi::subsystem<cgs::IoctlSubsystem::i2c>>);

// The vendor, subsystem and syscall-id catalogs all share one width,
// so a bridge can carry any of them in a single tagged-union envelope
// without branching on which catalog it holds.
static_assert(std::is_same_v<std::underlying_type_t<cgs::IoctlVendor>, std::uint16_t>);
static_assert(std::is_same_v<std::underlying_type_t<cgs::IoctlSubsystem>, std::uint16_t>);

// These counts are also pinned beside the catalogs themselves.
// Repeating them here reports a drift against the catalog as a plain
// failure, rather than as a cascade out of a template.
static_assert(::crucible::fixy::grant::detail::syscall_ioctl_grant_self_test::ioctl_vendor_count == 12);
static_assert(::crucible::fixy::grant::detail::syscall_ioctl_grant_self_test::ioctl_subsystem_count == 15);

}  // namespace

int main() { return 0; }
