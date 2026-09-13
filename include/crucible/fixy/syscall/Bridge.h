#pragma once

#include <crucible/fixy/syscall/Family.h>
#include <crucible/fixy/syscall/Per.h>
#include <crucible/fixy/syscall/Ioctl.h>
#include <crucible/fixy/Grant.h>
#include <crucible/fixy/Fp.h>
#include <crucible/effects/Capabilities.h>
#include <crucible/effects/EffectRow.h>
#include <crucible/safety/DimensionTraits.h>
#include <crucible/algebra/lattices/SyscallFamilyLattice.h>

#include <meta>
#include <type_traits>

namespace crucible::fixy::syscall::bridge {

template <typename G>
concept IsSyscallGrantTag =
    ::crucible::fixy::grant::IsGrantTag<G>
    && (::crucible::fixy::grant::which_dim_v<G> == ::crucible::fixy::dim::DimensionAxis::SyscallSurface);

// Three rows are narrower than the family name suggests. A vDSO call resolves
// in user space and never enters the kernel, so VdsoOnly carries nothing.
// Establishing a mapping does not block, because the cost is paid later by
// page faults, so MemoryMapping carries IO alone. A futex wait or a yield
// blocks without touching a device, so ThreadSync carries Block alone.

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
    using type = ::crucible::effects::Row<::crucible::effects::Effect::IO, ::crucible::effects::Effect::Block>;
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
    using type = ::crucible::effects::Row<::crucible::effects::Effect::IO, ::crucible::effects::Effect::Block>;
};
template <>
struct row_for_family<::crucible::algebra::lattices::SyscallFamily::ProcessControl> {
    using type = ::crucible::effects::Row<::crucible::effects::Effect::IO, ::crucible::effects::Effect::Block>;
};
template <>
struct row_for_family<::crucible::algebra::lattices::SyscallFamily::Privilege> {
    using type = ::crucible::effects::Row<::crucible::effects::Effect::IO, ::crucible::effects::Effect::Block>;
};

template <::crucible::algebra::lattices::SyscallFamily F>
using row_for_family_t = typename row_for_family<F>::type;

template <typename G>
    requires IsSyscallGrantTag<G>
using lift_syscall_grant_row_t = row_for_family_t<::crucible::fixy::grant::family_tier_v<G>>;

namespace detail::syscall_bridge_self_test {

namespace gr = ::crucible::fixy::grant;
namespace sc = ::crucible::fixy::grant::syscall;
namespace sci = ::crucible::fixy::grant::syscall::ioctl;
namespace al = ::crucible::algebra::lattices;
namespace fe = ::crucible::effects;
using SF = al::SyscallFamily;
using SI = sc::SyscallId;
using IV = sc::IoctlVendor;
using IS = sc::IoctlSubsystem;

static_assert(std::is_same_v<row_for_family_t<SF::NoSyscall>, fe::Row<>>);
static_assert(std::is_same_v<row_for_family_t<SF::VdsoOnly>, fe::Row<>>);
static_assert(std::is_same_v<row_for_family_t<SF::ReadOnlyState>, fe::Row<fe::Effect::IO>>);
static_assert(std::is_same_v<row_for_family_t<SF::FileMutation>, fe::Row<fe::Effect::IO, fe::Effect::Block>>);
static_assert(std::is_same_v<row_for_family_t<SF::MemoryMapping>, fe::Row<fe::Effect::IO>>);
static_assert(std::is_same_v<row_for_family_t<SF::ThreadSync>, fe::Row<fe::Effect::Block>>);
static_assert(std::is_same_v<row_for_family_t<SF::NetworkIo>, fe::Row<fe::Effect::IO, fe::Effect::Block>>);
static_assert(std::is_same_v<row_for_family_t<SF::ProcessControl>, fe::Row<fe::Effect::IO, fe::Effect::Block>>);
static_assert(std::is_same_v<row_for_family_t<SF::Privilege>, fe::Row<fe::Effect::IO, fe::Effect::Block>>);

static_assert(std::is_same_v<lift_syscall_grant_row_t<sc::family_no_syscall>, fe::Row<>>);
static_assert(std::is_same_v<lift_syscall_grant_row_t<sc::family_vdso_only>, fe::Row<>>);
static_assert(std::is_same_v<lift_syscall_grant_row_t<sc::family_read_only_state>, fe::Row<fe::Effect::IO>>);
static_assert(
    std::is_same_v<lift_syscall_grant_row_t<sc::family_file_mutation>, fe::Row<fe::Effect::IO, fe::Effect::Block>>);
static_assert(std::is_same_v<lift_syscall_grant_row_t<sc::family_memory_mapping>, fe::Row<fe::Effect::IO>>);
static_assert(std::is_same_v<lift_syscall_grant_row_t<sc::family_thread_sync>, fe::Row<fe::Effect::Block>>);
static_assert(
    std::is_same_v<lift_syscall_grant_row_t<sc::family_network_io>, fe::Row<fe::Effect::IO, fe::Effect::Block>>);
static_assert(
    std::is_same_v<lift_syscall_grant_row_t<sc::family_process_control>, fe::Row<fe::Effect::IO, fe::Effect::Block>>);
static_assert(
    std::is_same_v<lift_syscall_grant_row_t<sc::family_privilege>, fe::Row<fe::Effect::IO, fe::Effect::Block>>);

static_assert(std::is_same_v<lift_syscall_grant_row_t<sc::per<SI::clock_gettime>>,
                             fe::Row<>>);  // VdsoOnly
static_assert(std::is_same_v<lift_syscall_grant_row_t<sc::per<SI::getpid>>,
                             fe::Row<fe::Effect::IO>>);  // ReadOnlyState
static_assert(std::is_same_v<lift_syscall_grant_row_t<sc::per<SI::write>>,
                             fe::Row<fe::Effect::IO, fe::Effect::Block>>);  // FileMutation
static_assert(std::is_same_v<lift_syscall_grant_row_t<sc::per<SI::pwrite>>,
                             fe::Row<fe::Effect::IO, fe::Effect::Block>>);  // FileMutation
static_assert(std::is_same_v<lift_syscall_grant_row_t<sc::per<SI::mmap>>,
                             fe::Row<fe::Effect::IO>>);  // MemoryMapping
static_assert(std::is_same_v<lift_syscall_grant_row_t<sc::per<SI::futex>>,
                             fe::Row<fe::Effect::Block>>);  // ThreadSync
static_assert(std::is_same_v<lift_syscall_grant_row_t<sc::per<SI::socket>>,
                             fe::Row<fe::Effect::IO, fe::Effect::Block>>);  // NetworkIo
static_assert(std::is_same_v<lift_syscall_grant_row_t<sc::per<SI::clone>>,
                             fe::Row<fe::Effect::IO, fe::Effect::Block>>);  // ProcessControl
static_assert(std::is_same_v<lift_syscall_grant_row_t<sc::per<SI::ptrace>>,
                             fe::Row<fe::Effect::IO, fe::Effect::Block>>);  // Privilege

// Every ioctl grant pins its family tier to Privilege, so the lift is uniform
// across vendor and subsystem.
static_assert(
    std::is_same_v<lift_syscall_grant_row_t<sci::vendor<IV::nvidia_ctl>>, fe::Row<fe::Effect::IO, fe::Effect::Block>>);
static_assert(
    std::is_same_v<lift_syscall_grant_row_t<sci::vendor<IV::nvidia_uvm>>, fe::Row<fe::Effect::IO, fe::Effect::Block>>);
static_assert(
    std::is_same_v<lift_syscall_grant_row_t<sci::vendor<IV::amd_kfd>>, fe::Row<fe::Effect::IO, fe::Effect::Block>>);
static_assert(std::is_same_v<lift_syscall_grant_row_t<sci::vendor<IV::intel_habana>>,
                             fe::Row<fe::Effect::IO, fe::Effect::Block>>);
static_assert(
    std::is_same_v<lift_syscall_grant_row_t<sci::vendor<IV::google_tpu>>, fe::Row<fe::Effect::IO, fe::Effect::Block>>);
static_assert(std::is_same_v<lift_syscall_grant_row_t<sci::vendor<IV::aws_trainium>>,
                             fe::Row<fe::Effect::IO, fe::Effect::Block>>);

static_assert(
    std::is_same_v<lift_syscall_grant_row_t<sci::subsystem<IS::drm>>, fe::Row<fe::Effect::IO, fe::Effect::Block>>);
static_assert(
    std::is_same_v<lift_syscall_grant_row_t<sci::subsystem<IS::kvm>>, fe::Row<fe::Effect::IO, fe::Effect::Block>>);
static_assert(
    std::is_same_v<lift_syscall_grant_row_t<sci::subsystem<IS::bpf>>, fe::Row<fe::Effect::IO, fe::Effect::Block>>);
static_assert(
    std::is_same_v<lift_syscall_grant_row_t<sci::subsystem<IS::io_uring>>, fe::Row<fe::Effect::IO, fe::Effect::Block>>);
static_assert(
    std::is_same_v<lift_syscall_grant_row_t<sci::subsystem<IS::vfio_pci>>, fe::Row<fe::Effect::IO, fe::Effect::Block>>);

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

static_assert(!IsSyscallGrantTag<int>);
static_assert(!IsSyscallGrantTag<void>);
static_assert(!IsSyscallGrantTag<double>);
static_assert(!IsSyscallGrantTag<std::nullptr_t>);

static_assert(gr::IsGrantTag<gr::fp_strict_ieee>);
static_assert(gr::which_dim_v<gr::fp_strict_ieee> != ::crucible::fixy::dim::DimensionAxis::SyscallSurface);
static_assert(!IsSyscallGrantTag<gr::fp_strict_ieee>);

// The effect-row carrier ships no public row predicate, so this trait stays
// local to the witness below.
template <typename T>
struct is_row : std::false_type {};
template <::crucible::effects::Effect... Es>
struct is_row<::crucible::effects::Row<Es...>> : std::true_type {};
template <typename T>
inline constexpr bool is_row_v = is_row<T>::value;

[[nodiscard]] consteval bool every_syscall_family_lifted() noexcept {
    static constexpr auto enumerators = std::define_static_array(std::meta::enumerators_of(^^SF));
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wshadow"
    template for (constexpr auto en : enumerators) {
        // The alias instantiation is itself the exhaustiveness test. An
        // enumerator with no specialization fails here on an incomplete type,
        // before the assertion below runs.
        // A splice in template-argument position requires the parentheses.
        using row_t = row_for_family_t<([:en:])>;
        static_assert(is_row_v<row_t>, "row_for_family<F> produced a non-Row type for some "
                                       "SyscallFamily enumerator — bridge map is structurally wrong.");
    }
#pragma GCC diagnostic pop
    return true;
}
static_assert(every_syscall_family_lifted(), "at least one SyscallFamily enumerator lacks a "
                                             "row_for_family<> specialization OR produces a non-Row type.  "
                                             "If you added a tier to SyscallFamily, extend "
                                             "row_for_family<> here.");

static_assert(::crucible::algebra::lattices::detail::syscall_family_lattice_self_test::family_count == 9,
              "the per-family Row map depends on the 9-tier syscall-family "
              "chain.  If the lattice grew an enumerator, row_for_family "
              "is incomplete and the every_syscall_family_lifted() witness fires.");

}  // namespace detail::syscall_bridge_self_test

}  // namespace crucible::fixy::syscall::bridge
