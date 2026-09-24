// A header's embedded static_asserts are only checked under the project
// warning flags when a translation unit in the build graph includes it.
// The includes below are what force the whole grant-to-row chain
// through the default compile preset.

#include <crucible/fixy/syscall/Family.h>
#include <crucible/fixy/syscall/Per.h>
#include <crucible/fixy/syscall/Ioctl.h>
#include <crucible/fixy/syscall/Bridge.h>
#include <crucible/effects/_Capabilities.h>
#include <crucible/effects/_EffectRow.h>

#include <type_traits>

namespace cg = ::crucible::fixy::grant;
namespace cgs = ::crucible::fixy::grant::syscall;
namespace cgi = ::crucible::fixy::grant::syscall::ioctl;
namespace cb = ::crucible::fixy::syscall::bridge;
namespace cal = ::crucible::algebra::lattices;
namespace cfe = ::crucible::effects;

namespace {

static_assert(std::is_same_v<cb::lift_syscall_grant_row_t<cgs::family_no_syscall>, cfe::Row<>>);
static_assert(std::is_same_v<cb::lift_syscall_grant_row_t<cgs::family_vdso_only>, cfe::Row<>>);
static_assert(std::is_same_v<cb::lift_syscall_grant_row_t<cgs::family_read_only_state>, cfe::Row<cfe::Effect::IO>>);
static_assert(std::is_same_v<cb::lift_syscall_grant_row_t<cgs::family_file_mutation>,
                             cfe::Row<cfe::Effect::IO, cfe::Effect::Block>>);
static_assert(std::is_same_v<cb::lift_syscall_grant_row_t<cgs::family_memory_mapping>, cfe::Row<cfe::Effect::IO>>);
static_assert(std::is_same_v<cb::lift_syscall_grant_row_t<cgs::family_thread_sync>, cfe::Row<cfe::Effect::Block>>);
static_assert(std::is_same_v<cb::lift_syscall_grant_row_t<cgs::family_network_io>,
                             cfe::Row<cfe::Effect::IO, cfe::Effect::Block>>);
static_assert(std::is_same_v<cb::lift_syscall_grant_row_t<cgs::family_process_control>,
                             cfe::Row<cfe::Effect::IO, cfe::Effect::Block>>);
static_assert(
    std::is_same_v<cb::lift_syscall_grant_row_t<cgs::family_privilege>, cfe::Row<cfe::Effect::IO, cfe::Effect::Block>>);

// A whole-family grant and a per-call grant that classify into the same
// family must lift to the same row.  Otherwise two bindings expressing
// one permission would land in different federation cache slots.
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

// Every ioctl grant is pinned to the privilege family, so vendor and
// subsystem grants alike collapse onto one row whatever their parameter.
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

// These two enumerators are both ordinal zero in their own catalogs,
// and the catalogs share an underlying width.  Their rows must still
// differ, which is what shows the lift keys on the family tier rather
// than on the raw enumerator value.
static_assert(!std::is_same_v<cb::lift_syscall_grant_row_t<cgs::per<cgs::SyscallId::clock_gettime>>,
                              cb::lift_syscall_grant_row_t<cgi::vendor<cgs::IoctlVendor::nvidia_ctl>>>);
static_assert(std::is_same_v<cb::lift_syscall_grant_row_t<cgs::per<cgs::SyscallId::clock_gettime>>,
                             cfe::Row<>>);  // VdsoOnly
static_assert(std::is_same_v<cb::lift_syscall_grant_row_t<cgi::vendor<cgs::IoctlVendor::nvidia_ctl>>,
                             cfe::Row<cfe::Effect::IO, cfe::Effect::Block>>);  // Privilege

static_assert(cb::IsSyscallGrantTag<cgs::family_no_syscall>);
static_assert(cb::IsSyscallGrantTag<cgs::family_privilege>);
static_assert(cb::IsSyscallGrantTag<cgs::per<cgs::SyscallId::write>>);
static_assert(cb::IsSyscallGrantTag<cgs::per<cgs::SyscallId::futex>>);
static_assert(cb::IsSyscallGrantTag<cgi::vendor<cgs::IoctlVendor::nvidia_ctl>>);
static_assert(cb::IsSyscallGrantTag<cgi::subsystem<cgs::IoctlSubsystem::drm>>);

// A plain type fails on the first arm, for not being a grant at all.
static_assert(!cb::IsSyscallGrantTag<int>);
static_assert(!cb::IsSyscallGrantTag<void>);
static_assert(!cb::IsSyscallGrantTag<double>);

// This one is a grant, so it passes the first arm and fails the second:
// it belongs to a different dimension.
static_assert(!cb::IsSyscallGrantTag<cg::fp_strict_ieee>);

}  // namespace

int main() { return 0; }
