#pragma once

// ── crucible::fixy::grant::syscall::ioctl — per-vendor + per-subsystem (V-099) ─
//
// Two parametric grant templates engaging `DimensionAxis::SyscallSurface`
// (V-097 dim 23, V-098 ships Family.h + Per.h precedents at the same
// axis) — refining the syscall taxonomy to the ioctl(2) sub-surface:
//
//     ioctl::vendor<IoctlVendor>      — vendor device-file ioctls
//                                       (/dev/nvidia*, /dev/kfd, /dev/dri/*,
//                                       /dev/accel*, /dev/neuron*, ...)
//     ioctl::subsystem<IoctlSubsystem> — kernel-subsystem ioctls
//                                       (DRM_IOCTL_*, KVM_*, BPF_*,
//                                       IORING_*, NETLINK_*, ...)
//
// Both surfaces ultimately route through the `ioctl(2)` syscall and
// therefore pin `family_tier_v<>` to `SyscallFamily::Privilege` (top of
// V-097's chain) — there is no cheaper kernel surface for raw ioctls.
// The audit value is NOT family-tier discrimination (every ioctl is
// Privilege) but the AUTHORING-SITE evidence about WHICH vendor /
// subsystem the binding actually touches.  V-100's bridge will read
// `family_tier_v<>` for the Met(X) effect-row lift; this header's
// per-vendor / per-subsystem identity is consumed by Mimic per-vendor
// backends (which need to know which kernel-driver ioctl set a kernel
// invokes) and by Cipher / forge phase E.RecipeSelect (which gate hot-
// path admission against specific kernel surfaces).
//
// ── Why TWO parametric grants rather than one ─────────────────────────
//
// Real-world ioctls compose along two orthogonal axes:
//
//   * VENDOR — "I touch the NVIDIA driver" / "I touch ROCm KFD" /
//              "I touch Intel Habana".  Identity is per-device-file
//              (the /dev/* node the binding opens).  This is the
//              identity Mimic per-vendor backends care about: a kernel
//              that touches `/dev/nvidia-uvm` lives on the NVIDIA
//              backend, never on AMD or Intel.
//
//   * SUBSYSTEM — "I use DRM ioctls" / "I use KVM ioctls" / "I use
//              io_uring control ioctls".  Identity is per-kernel-
//              subsystem; the same SUBSYSTEM ioctl number space can
//              run against many vendor devices (DRM_IOCTL_VERSION
//              works on any /dev/dri/* node regardless of vendor).
//              This is the identity Forge phase E.RecipeSelect cares
//              about: a kernel claiming "drm ioctls only" can run on
//              any DRM-compliant vendor; one claiming
//              "vendor::nvidia_uvm" is locked to NVIDIA.
//
// A binding picks ONE framing (the one its audit cares about); the
// duplicate-engagement gate (Reject.h `UniqueEngagementPerAxis`)
// catches stacking both because they both engage SyscallSurface.
//
// ── Catalog scope ──────────────────────────────────────────────────────
//
//   IoctlVendor — 12 enumerators covering shipped Mimic-backend
//                 device-file taxonomy (NVIDIA: ctl/dev/uvm/modeset,
//                 AMD: kfd/render, Intel: i915/xe/habana, plus Apple
//                 neural / Google TPU / AWS Trainium).
//
//   IoctlSubsystem — 15 enumerators covering shipped kernel-subsystem
//                    taxonomy (DRM, KVM, BPF, io_uring, netlink,
//                    perf_event, tty, generic file, IPMI, TUN/TAP,
//                    loop, block, VFIO-PCI, IOMMU, I2C).
//
// Both enums use `std::uint16_t` underlying to match V-098 Per.h's
// SyscallId — the catalog is APPEND-ONLY per FOUND-I04 (Universe
// extension rule).  Appending a new vendor / subsystem at the next
// free ordinal does NOT break stored row_hash (federation cache key);
// reordering or shrinking silently invalidates every cache slot.
//
// ── Family classification — uniformly Privilege ───────────────────────
//
// Every IoctlVendor and every IoctlSubsystem grant pins
// `family_tier_v<> == SyscallFamily::Privilege` — there is no other
// possible classification, because the only kernel surface ioctls
// reach is the raw `ioctl(2)` syscall (which lives in Privilege per
// V-097's chain).  Composition with a binding that ALSO declares a
// non-Privilege family (e.g., `family_file_mutation`) is rejected at
// the duplicate-engagement gate (both engage SyscallSurface).
//
// V-100's bridge consumes `family_tier_v<>` for the Met(X) effect-row
// lift; both `ioctl::vendor<V>` and `ioctl::subsystem<S>` participate
// transparently because they share the metadata channel with Family.h
// and Per.h.
//
// ── Self-test (compile-time) ─────────────────────────────────────────
//
// Seven layers, sampled across every vendor and every subsystem:
//
//   1. IsGrantTag<G>             — final + grant_base + cv-ref-free
//   2. sizeof(G) == 1            — EBO-collapsible empty marker
//   3. which_dim_v<G>            — routes to SyscallSurface
//   4. family_tier_v<G>          — pinned to Privilege uniformly
//   5. NTTP-distinctness         — distinct enumerators → distinct types
//   6. Cross-grant distinctness  — vendor<X> ≠ subsystem<Y> ≠ per<Z> ≠ family_*
//   7. Reflection-driven cardinality — both catalogs match their pinned size
//
// HS14 negative-compile fixtures live in test/fixy_neg/neg_fixy_v_099_*:
//   (a) wrong NTTP type — `vendor<IoctlSubsystem::drm>` or
//                         `subsystem<IoctlVendor::nvidia_ctl>` rejected
//                         at template-id formation (analogue of V-098's
//                         neg_fixy_v_098_wrong_axis.cpp).
//   (b) duplicate engagement — pack engages BOTH `ioctl::vendor<V>` and
//                         `ioctl::subsystem<S>` (or one of them + a
//                         Family.h family_* tag) — rejected by
//                         `UniqueEngagementPerAxis` (analogue of V-098's
//                         neg_fixy_v_098_family_per_duplicate.cpp).
//
// ── Cost ──────────────────────────────────────────────────────────────
//
// Zero.  Every `vendor<V>` / `subsystem<S>` is a 1-byte empty struct;
// `family_tier_v<>` resolves to a single `SyscallFamily::Privilege`
// integral_constant at compile time; `which_dim_v<>` likewise.

#include <crucible/fixy/syscall/Family.h>   // family_tier primary + syscall ns
#include <crucible/fixy/Grant.h>            // grant_base + which_dim primary
#include <crucible/safety/DimensionTraits.h>// DimensionAxis::SyscallSurface
#include <crucible/algebra/lattices/SyscallFamilyLattice.h> // SyscallFamily

#include <cstdint>
#include <meta>
#include <type_traits>

namespace crucible::fixy::grant::syscall {

// ═════════════════════════════════════════════════════════════════════
// ── IoctlVendor — device-file vendor catalog ─────────────────────────
// ═════════════════════════════════════════════════════════════════════
//
// Each enumerator names ONE /dev/* device-file family that a binding
// might open + ioctl against.  Ordering groups by vendor brand (NVIDIA
// → AMD → Intel → ...) but ordinals are the authoritative key.
//
// FOUND-I04 append-only: new vendors arrive at the next free ordinal
// and the catalog size assertion below tracks the count.  Reordering or
// shrinking silently invalidates every row_hash federation cache key.
enum class IoctlVendor : std::uint16_t {
    // ── NVIDIA driver ────────────────────────────────────────────────
    nvidia_ctl       = 0,   // /dev/nvidiactl       — control device
    nvidia_dev       = 1,   // /dev/nvidia0..N      — per-GPU devices
    nvidia_uvm       = 2,   // /dev/nvidia-uvm      — Unified Virtual Memory
    nvidia_modeset   = 3,   // /dev/nvidia-modeset  — modeset / display

    // ── AMD driver ──────────────────────────────────────────────────
    amd_kfd          = 4,   // /dev/kfd             — Kernel Fusion Driver
    amd_render       = 5,   // /dev/dri/renderD*    — DRM render node (ROCm)

    // ── Intel drivers ───────────────────────────────────────────────
    intel_i915       = 6,   // /dev/dri/renderD*    — i915 GPU
    intel_xe         = 7,   // /dev/dri/renderD*    — Xe GPU (Arc, Battlemage)
    intel_habana     = 8,   // /dev/accel/accel*    — Gaudi accelerator

    // ── Other accelerator families ──────────────────────────────────
    apple_neural     = 9,   // /dev/aneuralengine   — Apple ANE
    google_tpu       = 10,  // /dev/accel0..N       — Cloud TPU
    aws_trainium     = 11,  // /dev/neuron*         — Trainium / Inferentia
};

// ═════════════════════════════════════════════════════════════════════
// ── IoctlSubsystem — kernel-subsystem ioctl-number-space catalog ─────
// ═════════════════════════════════════════════════════════════════════
//
// Each enumerator names ONE Linux kernel subsystem with a well-defined
// ioctl number space (`<ioctl>` magic numbers + per-subsystem command
// IDs).  A binding declares this when it cares about portability across
// vendor devices that share the subsystem (DRM ioctls work on every
// DRM-compliant vendor regardless of which /dev/dri/* node is opened).
enum class IoctlSubsystem : std::uint16_t {
    drm              = 0,   // DRM_IOCTL_* (Direct Rendering Manager)
    kvm              = 1,   // KVM_*       (Kernel-based Virtual Machine)
    bpf              = 2,   // BPF_*       (eBPF map / program load)
    io_uring         = 3,   // IORING_*    (io_uring control)
    netlink          = 4,   // NETLINK_*   (sock configuration)
    perf_event       = 5,   // PERF_EVENT_IOC_* (perf event control)
    tty              = 6,   // TIO* / TCGETS / TIOCGWINSZ
    file_generic     = 7,   // FIONREAD / FIONBIO / FICLONE / FIDEDUPERANGE
    ipmi             = 8,   // IPMI_*      (Intelligent Platform Management)
    tun              = 9,   // TUNSETIFF / TUNSETPERSIST (TUN/TAP)
    loop             = 10,  // LOOP_*      (loopback device)
    block            = 11,  // BLK*        (block device — BLKGETSIZE etc.)
    vfio_pci         = 12,  // VFIO_*      (PCIe passthrough — SR-IOV, GPU virt)
    iommu            = 13,  // IOMMU_*     (IOMMU control)
    i2c              = 14,  // I2C_*       (smbus / sensor)
};

namespace ioctl {

// ═════════════════════════════════════════════════════════════════════
// ── vendor<V> — parametric per-device-file grant ─────────────────────
// ═════════════════════════════════════════════════════════════════════
//
// One distinct empty marker per IoctlVendor enumerator.  Final +
// grant_base — passes IsGrantTag.  Instantiations with different V's
// are distinct types (NTTP discrimination); a pack engaging BOTH
// `vendor<NvidiaCtl>` and `vendor<AmdKfd>` correctly witnesses TWO
// distinct grants on SyscallSurface and trips
// `FixyDuplicate_SyscallSurface` (Reject.h's per-axis duplicate gate).
template <IoctlVendor V>
struct vendor final : grant_base {};

// ═════════════════════════════════════════════════════════════════════
// ── subsystem<S> — parametric per-kernel-subsystem grant ─────────────
// ═════════════════════════════════════════════════════════════════════
//
// One distinct empty marker per IoctlSubsystem enumerator.  Same
// structural shape as `vendor<V>`; the two templates produce
// STRUCTURALLY DISTINCT types even when the enumerator ordinals would
// match (`vendor<IoctlVendor{0}>` ≠ `subsystem<IoctlSubsystem{0}>` is
// guaranteed by the different class-template identities).
template <IoctlSubsystem S>
struct subsystem final : grant_base {};

}  // namespace ioctl

}  // namespace crucible::fixy::grant::syscall

namespace crucible::fixy::grant {

// ═════════════════════════════════════════════════════════════════════
// ── which_dim — every vendor<V> + subsystem<S> → SyscallSurface ──────
// ═════════════════════════════════════════════════════════════════════
template <syscall::IoctlVendor V>
struct which_dim<syscall::ioctl::vendor<V>>
    : std::integral_constant<dim::DimensionAxis,
                             dim::DimensionAxis::SyscallSurface> {};

template <syscall::IoctlSubsystem S>
struct which_dim<syscall::ioctl::subsystem<S>>
    : std::integral_constant<dim::DimensionAxis,
                             dim::DimensionAxis::SyscallSurface> {};

// ═════════════════════════════════════════════════════════════════════
// ── family_tier — uniformly Privilege (top of V-097's chain) ─────────
// ═════════════════════════════════════════════════════════════════════
//
// Every ioctl grant pins Privilege regardless of V or S.  V-100's
// bridge will read this to lift into Met(X) `effects::IO` (Privilege
// ioctls always touch the kernel and may block, so the bridge adds
// IO + Block at minimum).  Per-vendor backends and forge phase
// E.RecipeSelect read the per-V / per-S identity directly (not via
// family_tier) — the type discrimination is the load-bearing channel
// there, not the tier.
template <syscall::IoctlVendor V>
struct family_tier<syscall::ioctl::vendor<V>>
    : std::integral_constant<::crucible::algebra::lattices::SyscallFamily,
                             ::crucible::algebra::lattices::SyscallFamily::Privilege> {};

template <syscall::IoctlSubsystem S>
struct family_tier<syscall::ioctl::subsystem<S>>
    : std::integral_constant<::crucible::algebra::lattices::SyscallFamily,
                             ::crucible::algebra::lattices::SyscallFamily::Privilege> {};

// ═════════════════════════════════════════════════════════════════════
// ── Self-test (compile-time) ─────────────────────────────────────────
// ═════════════════════════════════════════════════════════════════════

namespace detail::syscall_ioctl_grant_self_test {

namespace sc = syscall;
namespace al = ::crucible::algebra::lattices;
using D      = dim::DimensionAxis;
using SF     = al::SyscallFamily;
using IV     = sc::IoctlVendor;
using IS     = sc::IoctlSubsystem;

// ── Layer 1: IsGrantTag — sampled across every vendor + subsystem ───
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

// ── Layer 2: sizeof — 1 byte standalone, EBO-collapsible ─────────────
static_assert(sizeof(sc::ioctl::vendor<IV::nvidia_ctl>)        == 1);
static_assert(sizeof(sc::ioctl::vendor<IV::amd_kfd>)           == 1);
static_assert(sizeof(sc::ioctl::vendor<IV::intel_habana>)      == 1);
static_assert(sizeof(sc::ioctl::vendor<IV::google_tpu>)        == 1);
static_assert(sizeof(sc::ioctl::subsystem<IS::drm>)            == 1);
static_assert(sizeof(sc::ioctl::subsystem<IS::kvm>)            == 1);
static_assert(sizeof(sc::ioctl::subsystem<IS::io_uring>)       == 1);
static_assert(sizeof(sc::ioctl::subsystem<IS::vfio_pci>)       == 1);

// ── Layer 3: which_dim routing — every grant → SyscallSurface ────────
static_assert(which_dim_v<sc::ioctl::vendor<IV::nvidia_ctl>>    == D::SyscallSurface);
static_assert(which_dim_v<sc::ioctl::vendor<IV::nvidia_uvm>>    == D::SyscallSurface);
static_assert(which_dim_v<sc::ioctl::vendor<IV::amd_kfd>>       == D::SyscallSurface);
static_assert(which_dim_v<sc::ioctl::vendor<IV::amd_render>>    == D::SyscallSurface);
static_assert(which_dim_v<sc::ioctl::vendor<IV::intel_i915>>    == D::SyscallSurface);
static_assert(which_dim_v<sc::ioctl::vendor<IV::intel_xe>>      == D::SyscallSurface);
static_assert(which_dim_v<sc::ioctl::vendor<IV::intel_habana>>  == D::SyscallSurface);
static_assert(which_dim_v<sc::ioctl::vendor<IV::apple_neural>>  == D::SyscallSurface);
static_assert(which_dim_v<sc::ioctl::vendor<IV::google_tpu>>    == D::SyscallSurface);
static_assert(which_dim_v<sc::ioctl::vendor<IV::aws_trainium>>  == D::SyscallSurface);

static_assert(which_dim_v<sc::ioctl::subsystem<IS::drm>>            == D::SyscallSurface);
static_assert(which_dim_v<sc::ioctl::subsystem<IS::kvm>>            == D::SyscallSurface);
static_assert(which_dim_v<sc::ioctl::subsystem<IS::bpf>>            == D::SyscallSurface);
static_assert(which_dim_v<sc::ioctl::subsystem<IS::io_uring>>       == D::SyscallSurface);
static_assert(which_dim_v<sc::ioctl::subsystem<IS::netlink>>        == D::SyscallSurface);
static_assert(which_dim_v<sc::ioctl::subsystem<IS::perf_event>>     == D::SyscallSurface);
static_assert(which_dim_v<sc::ioctl::subsystem<IS::file_generic>>   == D::SyscallSurface);
static_assert(which_dim_v<sc::ioctl::subsystem<IS::vfio_pci>>       == D::SyscallSurface);

// ── Layer 4: family_tier — Privilege uniformly ───────────────────────
static_assert(family_tier_v<sc::ioctl::vendor<IV::nvidia_ctl>>       == SF::Privilege);
static_assert(family_tier_v<sc::ioctl::vendor<IV::nvidia_uvm>>       == SF::Privilege);
static_assert(family_tier_v<sc::ioctl::vendor<IV::amd_kfd>>          == SF::Privilege);
static_assert(family_tier_v<sc::ioctl::vendor<IV::amd_render>>       == SF::Privilege);
static_assert(family_tier_v<sc::ioctl::vendor<IV::intel_i915>>       == SF::Privilege);
static_assert(family_tier_v<sc::ioctl::vendor<IV::intel_xe>>         == SF::Privilege);
static_assert(family_tier_v<sc::ioctl::vendor<IV::intel_habana>>     == SF::Privilege);
static_assert(family_tier_v<sc::ioctl::vendor<IV::apple_neural>>     == SF::Privilege);
static_assert(family_tier_v<sc::ioctl::vendor<IV::google_tpu>>       == SF::Privilege);
static_assert(family_tier_v<sc::ioctl::vendor<IV::aws_trainium>>     == SF::Privilege);

static_assert(family_tier_v<sc::ioctl::subsystem<IS::drm>>           == SF::Privilege);
static_assert(family_tier_v<sc::ioctl::subsystem<IS::kvm>>           == SF::Privilege);
static_assert(family_tier_v<sc::ioctl::subsystem<IS::bpf>>           == SF::Privilege);
static_assert(family_tier_v<sc::ioctl::subsystem<IS::io_uring>>      == SF::Privilege);
static_assert(family_tier_v<sc::ioctl::subsystem<IS::netlink>>       == SF::Privilege);
static_assert(family_tier_v<sc::ioctl::subsystem<IS::perf_event>>    == SF::Privilege);
static_assert(family_tier_v<sc::ioctl::subsystem<IS::tty>>           == SF::Privilege);
static_assert(family_tier_v<sc::ioctl::subsystem<IS::file_generic>>  == SF::Privilege);
static_assert(family_tier_v<sc::ioctl::subsystem<IS::vfio_pci>>      == SF::Privilege);
static_assert(family_tier_v<sc::ioctl::subsystem<IS::iommu>>         == SF::Privilege);

// ── Layer 5: NTTP-distinctness — distinct enumerators → distinct types
// Sampled across every vendor adjacency + every subsystem adjacency;
// the full distinctness matrix is the (12 × 11)/2 + (15 × 14)/2 = 171-cell
// surface that any contributor adding an enumerator implicitly inherits.
static_assert(!std::is_same_v<sc::ioctl::vendor<IV::nvidia_ctl>,    sc::ioctl::vendor<IV::nvidia_dev>>);
static_assert(!std::is_same_v<sc::ioctl::vendor<IV::nvidia_dev>,    sc::ioctl::vendor<IV::nvidia_uvm>>);
static_assert(!std::is_same_v<sc::ioctl::vendor<IV::amd_kfd>,       sc::ioctl::vendor<IV::amd_render>>);
static_assert(!std::is_same_v<sc::ioctl::vendor<IV::intel_i915>,    sc::ioctl::vendor<IV::intel_xe>>);
static_assert(!std::is_same_v<sc::ioctl::vendor<IV::nvidia_ctl>,    sc::ioctl::vendor<IV::amd_kfd>>);
static_assert(!std::is_same_v<sc::ioctl::vendor<IV::google_tpu>,    sc::ioctl::vendor<IV::aws_trainium>>);

static_assert(!std::is_same_v<sc::ioctl::subsystem<IS::drm>,        sc::ioctl::subsystem<IS::kvm>>);
static_assert(!std::is_same_v<sc::ioctl::subsystem<IS::kvm>,        sc::ioctl::subsystem<IS::bpf>>);
static_assert(!std::is_same_v<sc::ioctl::subsystem<IS::io_uring>,   sc::ioctl::subsystem<IS::netlink>>);
static_assert(!std::is_same_v<sc::ioctl::subsystem<IS::perf_event>, sc::ioctl::subsystem<IS::vfio_pci>>);

// ── Layer 6: cross-grant distinctness ────────────────────────────────
// `vendor<V>` and `subsystem<S>` are DIFFERENT class templates — their
// instantiations are STRUCTURALLY distinct types even when the
// enumerator ordinals match.  Together with cross-axis distinctness
// against `family_*` (Family.h) and `per<>` (Per.h), this is what the
// duplicate-engagement gate needs to reject all six combinations:
//   (a) ioctl::vendor + ioctl::vendor      (two distinct ioctls)
//   (b) ioctl::vendor + ioctl::subsystem   (vendor + portability framing)
//   (c) ioctl::vendor + family_*           (cross-tier coarse + ioctl)
//   (d) ioctl::vendor + per<>              (cross-tier syscall + ioctl)
//   (e) ioctl::subsystem + family_*        (cross-tier coarse + ioctl)
//   (f) ioctl::subsystem + per<>           (cross-tier syscall + ioctl)
// All six trip `FixyDuplicate_SyscallSurface`.

// (b) — ioctl::vendor ≠ ioctl::subsystem at every ordinal
static_assert(!std::is_same_v<sc::ioctl::vendor<IV::nvidia_ctl>,
                              sc::ioctl::subsystem<IS::drm>>);
static_assert(!std::is_same_v<sc::ioctl::vendor<IV::amd_kfd>,
                              sc::ioctl::subsystem<IS::kvm>>);
static_assert(!std::is_same_v<sc::ioctl::vendor<IV::intel_i915>,
                              sc::ioctl::subsystem<IS::file_generic>>);

// (c) — ioctl::vendor ≠ family_*
static_assert(!std::is_same_v<sc::ioctl::vendor<IV::nvidia_ctl>,
                              sc::family_privilege>);
static_assert(!std::is_same_v<sc::ioctl::vendor<IV::amd_kfd>,
                              sc::family_privilege>);

// (d) — ioctl::vendor ≠ per<>
static_assert(!std::is_same_v<sc::ioctl::vendor<IV::nvidia_ctl>,
                              sc::per<sc::SyscallId::ptrace>>);
static_assert(!std::is_same_v<sc::ioctl::vendor<IV::amd_render>,
                              sc::per<sc::SyscallId::capset>>);

// (e) — ioctl::subsystem ≠ family_*
static_assert(!std::is_same_v<sc::ioctl::subsystem<IS::drm>,
                              sc::family_privilege>);
static_assert(!std::is_same_v<sc::ioctl::subsystem<IS::io_uring>,
                              sc::family_file_mutation>);

// (f) — ioctl::subsystem ≠ per<>
static_assert(!std::is_same_v<sc::ioctl::subsystem<IS::drm>,
                              sc::per<sc::SyscallId::ptrace>>);
static_assert(!std::is_same_v<sc::ioctl::subsystem<IS::file_generic>,
                              sc::per<sc::SyscallId::pwrite>>);

// ── Layer 7: reflection-driven cardinality — append-only witness ────
//
// Both catalogs are FOUND-I04 append-only; the shipped surface is
// pinned here.  A contributor adding an enumerator MUST (a) append at
// the next free ordinal, (b) extend the cardinality below, (c) extend
// the layered-tests above to sample the new enumerator at every layer.
inline constexpr std::size_t ioctl_vendor_count =
    std::meta::enumerators_of(^^IV).size();
static_assert(ioctl_vendor_count == 12,
    "FIXY-V-099: IoctlVendor catalog drifted from the 12-enumerator "
    "shipped surface.  If you're adding a new device-file family, "
    "append it at the next free ordinal AND extend the self-test arms "
    "above.  Reordering / shrinking the enum silently invalidates "
    "every stored row_hash (federation cache key).");

inline constexpr std::size_t ioctl_subsystem_count =
    std::meta::enumerators_of(^^IS).size();
static_assert(ioctl_subsystem_count == 15,
    "FIXY-V-099: IoctlSubsystem catalog drifted from the 15-enumerator "
    "shipped surface.  If you're adding a new kernel-subsystem ioctl "
    "namespace, append it at the next free ordinal AND extend the "
    "self-test arms above.  Reordering / shrinking the enum silently "
    "invalidates every stored row_hash (federation cache key).");

// Cross-check against V-097's 9-tier lattice cardinality — ensures
// V-099 stays consistent if the lattice grows.
static_assert(
    ::crucible::algebra::lattices::detail::syscall_family_lattice_self_test::family_count == 9,
    "FIXY-V-099: Ioctl.h depends on V-097's 9-tier chain (every ioctl "
    "pins Privilege = top).  If the lattice gained a 10th tier, every "
    "family_tier specialization above is stale.");

}  // namespace detail::syscall_ioctl_grant_self_test

}  // namespace crucible::fixy::grant
