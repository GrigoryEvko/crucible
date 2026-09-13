#pragma once

// Two grant templates split the ioctl(2) surface along orthogonal axes.
// vendor<V> names the /dev/* node a binding opens, which is the identity a
// per-vendor backend needs: a binding that touches /dev/nvidia-uvm runs
// against one driver and no other.  subsystem<S> names the ioctl number
// space, which is the identity a portability check needs: a DRM ioctl works
// on any DRM-compliant node whatever the vendor.  A binding picks the
// framing its audit cares about.  Both engage the same axis, so declaring
// both is rejected as a duplicate engagement.
//
// The ordinals of both catalogs are frozen.  A new vendor or subsystem
// appends at the next free ordinal.  Reusing or reordering one silently
// changes every stored row hash, which is a federation cache key, and
// nothing diagnoses it.

#include <crucible/fixy/syscall/Family.h>
#include <crucible/fixy/Grant.h>
#include <crucible/safety/DimensionTraits.h>
#include <crucible/algebra/lattices/SyscallFamilyLattice.h>

#include <cstdint>
#include <meta>
#include <type_traits>

namespace crucible::fixy::grant::syscall {

enum class IoctlVendor : std::uint16_t {
    nvidia_ctl = 0,  // /dev/nvidiactl       — control device
    nvidia_dev = 1,  // /dev/nvidia0..N      — per-GPU devices
    nvidia_uvm = 2,  // /dev/nvidia-uvm      — Unified Virtual Memory
    nvidia_modeset = 3,  // /dev/nvidia-modeset  — modeset / display

    amd_kfd = 4,  // /dev/kfd             — Kernel Fusion Driver
    amd_render = 5,  // /dev/dri/renderD*    — DRM render node (ROCm)

    intel_i915 = 6,  // /dev/dri/renderD*    — i915 GPU
    intel_xe = 7,  // /dev/dri/renderD*    — Xe GPU (Arc, Battlemage)
    intel_habana = 8,  // /dev/accel/accel*    — Gaudi accelerator

    apple_neural = 9,  // /dev/aneuralengine   — Apple ANE
    google_tpu = 10,  // /dev/accel0..N       — Cloud TPU
    aws_trainium = 11,  // /dev/neuron*         — Trainium / Inferentia
};

// Each enumerator names one kernel subsystem with its own ioctl magic
// number and command-id space.
enum class IoctlSubsystem : std::uint16_t {
    drm = 0,  // DRM_IOCTL_* (Direct Rendering Manager)
    kvm = 1,  // KVM_*       (Kernel-based Virtual Machine)
    bpf = 2,  // BPF_*       (eBPF map / program load)
    io_uring = 3,  // IORING_*    (io_uring control)
    netlink = 4,  // NETLINK_*   (sock configuration)
    perf_event = 5,  // PERF_EVENT_IOC_* (perf event control)
    tty = 6,  // TIO* / TCGETS / TIOCGWINSZ
    file_generic = 7,  // FIONREAD / FIONBIO / FICLONE / FIDEDUPERANGE
    ipmi = 8,  // IPMI_*      (Intelligent Platform Management)
    tun = 9,  // TUNSETIFF / TUNSETPERSIST (TUN/TAP)
    loop = 10,  // LOOP_*      (loopback device)
    block = 11,  // BLK*        (block device — BLKGETSIZE etc.)
    vfio_pci = 12,  // VFIO_*      (PCIe passthrough — SR-IOV, GPU virt)
    iommu = 13,  // IOMMU_*     (IOMMU control)
    i2c = 14,  // I2C_*       (smbus / sensor)
};

namespace ioctl {

template <IoctlVendor V>
struct vendor final : grant_base {};

template <IoctlSubsystem S>
struct subsystem final : grant_base {};

}  // namespace ioctl

}  // namespace crucible::fixy::grant::syscall

namespace crucible::fixy::grant {

template <syscall::IoctlVendor V>
struct which_dim<syscall::ioctl::vendor<V>>
    : std::integral_constant<dim::DimensionAxis, dim::DimensionAxis::SyscallSurface> {};

template <syscall::IoctlSubsystem S>
struct which_dim<syscall::ioctl::subsystem<S>>
    : std::integral_constant<dim::DimensionAxis, dim::DimensionAxis::SyscallSurface> {};

// The tier is Privilege for every V and every S.  The only kernel surface a
// raw ioctl reaches is ioctl(2), which sits at the top of the family chain,
// so the tier carries no discrimination here.  A consumer that needs to know
// which driver or subsystem a binding touches reads the grant type instead.
template <syscall::IoctlVendor V>
struct family_tier<syscall::ioctl::vendor<V>>
    : std::integral_constant<::crucible::algebra::lattices::SyscallFamily,
                             ::crucible::algebra::lattices::SyscallFamily::Privilege> {};

template <syscall::IoctlSubsystem S>
struct family_tier<syscall::ioctl::subsystem<S>>
    : std::integral_constant<::crucible::algebra::lattices::SyscallFamily,
                             ::crucible::algebra::lattices::SyscallFamily::Privilege> {};

namespace detail::syscall_ioctl_grant_self_test {

namespace sc = syscall;
namespace al = ::crucible::algebra::lattices;
using D = dim::DimensionAxis;
using SF = al::SyscallFamily;
using IV = sc::IoctlVendor;
using IS = sc::IoctlSubsystem;

static_assert(IsGrantTag<sc::ioctl::vendor<IV::nvidia_ctl>>);
static_assert(IsGrantTag<sc::ioctl::vendor<IV::nvidia_uvm>>);
static_assert(IsGrantTag<sc::ioctl::vendor<IV::amd_kfd>>);
static_assert(IsGrantTag<sc::ioctl::vendor<IV::intel_i915>>);
static_assert(IsGrantTag<sc::ioctl::vendor<IV::intel_habana>>);
static_assert(IsGrantTag<sc::ioctl::vendor<IV::google_tpu>>);
static_assert(IsGrantTag<sc::ioctl::vendor<IV::aws_trainium>>);

static_assert(IsGrantTag<sc::ioctl::subsystem<IS::drm>>);
static_assert(IsGrantTag<sc::ioctl::subsystem<IS::kvm>>);
static_assert(IsGrantTag<sc::ioctl::subsystem<IS::bpf>>);
static_assert(IsGrantTag<sc::ioctl::subsystem<IS::io_uring>>);
static_assert(IsGrantTag<sc::ioctl::subsystem<IS::perf_event>>);
static_assert(IsGrantTag<sc::ioctl::subsystem<IS::vfio_pci>>);

static_assert(sizeof(sc::ioctl::vendor<IV::nvidia_ctl>) == 1);
static_assert(sizeof(sc::ioctl::vendor<IV::amd_kfd>) == 1);
static_assert(sizeof(sc::ioctl::vendor<IV::intel_habana>) == 1);
static_assert(sizeof(sc::ioctl::vendor<IV::google_tpu>) == 1);
static_assert(sizeof(sc::ioctl::subsystem<IS::drm>) == 1);
static_assert(sizeof(sc::ioctl::subsystem<IS::kvm>) == 1);
static_assert(sizeof(sc::ioctl::subsystem<IS::io_uring>) == 1);
static_assert(sizeof(sc::ioctl::subsystem<IS::vfio_pci>) == 1);

static_assert(which_dim_v<sc::ioctl::vendor<IV::nvidia_ctl>> == D::SyscallSurface);
static_assert(which_dim_v<sc::ioctl::vendor<IV::nvidia_uvm>> == D::SyscallSurface);
static_assert(which_dim_v<sc::ioctl::vendor<IV::amd_kfd>> == D::SyscallSurface);
static_assert(which_dim_v<sc::ioctl::vendor<IV::amd_render>> == D::SyscallSurface);
static_assert(which_dim_v<sc::ioctl::vendor<IV::intel_i915>> == D::SyscallSurface);
static_assert(which_dim_v<sc::ioctl::vendor<IV::intel_xe>> == D::SyscallSurface);
static_assert(which_dim_v<sc::ioctl::vendor<IV::intel_habana>> == D::SyscallSurface);
static_assert(which_dim_v<sc::ioctl::vendor<IV::apple_neural>> == D::SyscallSurface);
static_assert(which_dim_v<sc::ioctl::vendor<IV::google_tpu>> == D::SyscallSurface);
static_assert(which_dim_v<sc::ioctl::vendor<IV::aws_trainium>> == D::SyscallSurface);

static_assert(which_dim_v<sc::ioctl::subsystem<IS::drm>> == D::SyscallSurface);
static_assert(which_dim_v<sc::ioctl::subsystem<IS::kvm>> == D::SyscallSurface);
static_assert(which_dim_v<sc::ioctl::subsystem<IS::bpf>> == D::SyscallSurface);
static_assert(which_dim_v<sc::ioctl::subsystem<IS::io_uring>> == D::SyscallSurface);
static_assert(which_dim_v<sc::ioctl::subsystem<IS::netlink>> == D::SyscallSurface);
static_assert(which_dim_v<sc::ioctl::subsystem<IS::perf_event>> == D::SyscallSurface);
static_assert(which_dim_v<sc::ioctl::subsystem<IS::file_generic>> == D::SyscallSurface);
static_assert(which_dim_v<sc::ioctl::subsystem<IS::vfio_pci>> == D::SyscallSurface);

static_assert(family_tier_v<sc::ioctl::vendor<IV::nvidia_ctl>> == SF::Privilege);
static_assert(family_tier_v<sc::ioctl::vendor<IV::nvidia_uvm>> == SF::Privilege);
static_assert(family_tier_v<sc::ioctl::vendor<IV::amd_kfd>> == SF::Privilege);
static_assert(family_tier_v<sc::ioctl::vendor<IV::amd_render>> == SF::Privilege);
static_assert(family_tier_v<sc::ioctl::vendor<IV::intel_i915>> == SF::Privilege);
static_assert(family_tier_v<sc::ioctl::vendor<IV::intel_xe>> == SF::Privilege);
static_assert(family_tier_v<sc::ioctl::vendor<IV::intel_habana>> == SF::Privilege);
static_assert(family_tier_v<sc::ioctl::vendor<IV::apple_neural>> == SF::Privilege);
static_assert(family_tier_v<sc::ioctl::vendor<IV::google_tpu>> == SF::Privilege);
static_assert(family_tier_v<sc::ioctl::vendor<IV::aws_trainium>> == SF::Privilege);

static_assert(family_tier_v<sc::ioctl::subsystem<IS::drm>> == SF::Privilege);
static_assert(family_tier_v<sc::ioctl::subsystem<IS::kvm>> == SF::Privilege);
static_assert(family_tier_v<sc::ioctl::subsystem<IS::bpf>> == SF::Privilege);
static_assert(family_tier_v<sc::ioctl::subsystem<IS::io_uring>> == SF::Privilege);
static_assert(family_tier_v<sc::ioctl::subsystem<IS::netlink>> == SF::Privilege);
static_assert(family_tier_v<sc::ioctl::subsystem<IS::perf_event>> == SF::Privilege);
static_assert(family_tier_v<sc::ioctl::subsystem<IS::tty>> == SF::Privilege);
static_assert(family_tier_v<sc::ioctl::subsystem<IS::file_generic>> == SF::Privilege);
static_assert(family_tier_v<sc::ioctl::subsystem<IS::vfio_pci>> == SF::Privilege);
static_assert(family_tier_v<sc::ioctl::subsystem<IS::iommu>> == SF::Privilege);

static_assert(!std::is_same_v<sc::ioctl::vendor<IV::nvidia_ctl>, sc::ioctl::vendor<IV::nvidia_dev>>);
static_assert(!std::is_same_v<sc::ioctl::vendor<IV::nvidia_dev>, sc::ioctl::vendor<IV::nvidia_uvm>>);
static_assert(!std::is_same_v<sc::ioctl::vendor<IV::amd_kfd>, sc::ioctl::vendor<IV::amd_render>>);
static_assert(!std::is_same_v<sc::ioctl::vendor<IV::intel_i915>, sc::ioctl::vendor<IV::intel_xe>>);
static_assert(!std::is_same_v<sc::ioctl::vendor<IV::nvidia_ctl>, sc::ioctl::vendor<IV::amd_kfd>>);
static_assert(!std::is_same_v<sc::ioctl::vendor<IV::google_tpu>, sc::ioctl::vendor<IV::aws_trainium>>);

static_assert(!std::is_same_v<sc::ioctl::subsystem<IS::drm>, sc::ioctl::subsystem<IS::kvm>>);
static_assert(!std::is_same_v<sc::ioctl::subsystem<IS::kvm>, sc::ioctl::subsystem<IS::bpf>>);
static_assert(!std::is_same_v<sc::ioctl::subsystem<IS::io_uring>, sc::ioctl::subsystem<IS::netlink>>);
static_assert(!std::is_same_v<sc::ioctl::subsystem<IS::perf_event>, sc::ioctl::subsystem<IS::vfio_pci>>);

// The distinctness below is what the per-axis duplicate-engagement gate
// consumes.  Any two of these tags in one binding engage SyscallSurface
// twice and are rejected.
static_assert(!std::is_same_v<sc::ioctl::vendor<IV::nvidia_ctl>, sc::ioctl::subsystem<IS::drm>>);
static_assert(!std::is_same_v<sc::ioctl::vendor<IV::amd_kfd>, sc::ioctl::subsystem<IS::kvm>>);
static_assert(!std::is_same_v<sc::ioctl::vendor<IV::intel_i915>, sc::ioctl::subsystem<IS::file_generic>>);

static_assert(!std::is_same_v<sc::ioctl::vendor<IV::nvidia_ctl>, sc::family_privilege>);
static_assert(!std::is_same_v<sc::ioctl::vendor<IV::amd_kfd>, sc::family_privilege>);

static_assert(!std::is_same_v<sc::ioctl::vendor<IV::nvidia_ctl>, sc::per<sc::SyscallId::ptrace>>);
static_assert(!std::is_same_v<sc::ioctl::vendor<IV::amd_render>, sc::per<sc::SyscallId::capset>>);

static_assert(!std::is_same_v<sc::ioctl::subsystem<IS::drm>, sc::family_privilege>);
static_assert(!std::is_same_v<sc::ioctl::subsystem<IS::io_uring>, sc::family_file_mutation>);

static_assert(!std::is_same_v<sc::ioctl::subsystem<IS::drm>, sc::per<sc::SyscallId::ptrace>>);
static_assert(!std::is_same_v<sc::ioctl::subsystem<IS::file_generic>, sc::per<sc::SyscallId::pwrite>>);

inline constexpr std::size_t ioctl_vendor_count = std::meta::enumerators_of(^^IV).size();
static_assert(ioctl_vendor_count == 12, "IoctlVendor catalog drifted from the 12-enumerator shipped "
                                        "surface.  A new device-file family appends at the next free "
                                        "ordinal and extends the self-test arms above.  Reordering or "
                                        "shrinking the enum silently invalidates every stored row hash, "
                                        "which is a federation cache key.");

inline constexpr std::size_t ioctl_subsystem_count = std::meta::enumerators_of(^^IS).size();
static_assert(ioctl_subsystem_count == 15, "IoctlSubsystem catalog drifted from the 15-enumerator shipped "
                                           "surface.  A new kernel-subsystem ioctl namespace appends at the "
                                           "next free ordinal and extends the self-test arms above.  "
                                           "Reordering or shrinking the enum silently invalidates every "
                                           "stored row hash, which is a federation cache key.");

static_assert(::crucible::algebra::lattices::detail::syscall_family_lattice_self_test::family_count == 9,
              "The family_tier specializations above pin Privilege as the top of a "
              "nine-tier chain.  A tenth tier leaves every one of them stale.");

}  // namespace detail::syscall_ioctl_grant_self_test

}  // namespace crucible::fixy::grant
