// FIXY-V-180 sentinel TU: warden/Hardening.h's mint_hardening_syscall_grants
// — the type-level audit-trail declaring every privileged Linux syscall
// the warden hardening path issues.  Forces every header-embedded
// static_assert through project warnings-as-errors per
// feedback_header_only_static_assert_blind_spot.
//
// V-180 extends V-098's SyscallId catalog (36 → 41) with five new
// enumerators covering the warden surface: sched_setattr, mlock2,
// mlock, munlock, prctl.  The annotation lives on mint_hardening
// as a `using mint_hardening_syscall_grants = std::tuple<...>` type-
// level declaration whose family_tier_v entries are pinned at parse.
//
// HS14 floor: 2 neg-compile fixtures at test/fixy_neg/:
//   - neg_fixy_v_180_hardening_bg_drain_reject.cpp (Bg-cap → not Init)
//   - neg_fixy_v_180_hardening_hot_fg_reject.cpp  (Fg-cap → not Init)

#include <crucible/warden/Hardening.h>
#include <crucible/fixy/syscall/Bridge.h>

#include <cstdint>
#include <tuple>
#include <type_traits>

namespace {

namespace fsc = ::crucible::fixy::grant::syscall;
namespace fll = ::crucible::algebra::lattices;
namespace eff = ::crucible::effects;
namespace fg  = ::crucible::fixy::grant;

// ── SyscallId universe extension — append-only ordinals ─────────────
//
// V-180's five additions sit at ordinals 36..40, immediately after
// V-098's ptrace=34, capset=35.  The append-only invariant: every
// existing ordinal (0..35) keeps its value forever; the federation
// cache key for any consumer that hashed a SyscallId never drifts
// across V-180.
static_assert(static_cast<std::uint16_t>(fsc::SyscallId::sched_setattr) == 36);
static_assert(static_cast<std::uint16_t>(fsc::SyscallId::mlock2)        == 37);
static_assert(static_cast<std::uint16_t>(fsc::SyscallId::mlock)         == 38);
static_assert(static_cast<std::uint16_t>(fsc::SyscallId::munlock)       == 39);
static_assert(static_cast<std::uint16_t>(fsc::SyscallId::prctl)         == 40);

// ── family_of classifier — every new enumerator routed correctly ────
static_assert(fsc::family_of(fsc::SyscallId::sched_setattr) == fll::SyscallFamily::ThreadSync);
static_assert(fsc::family_of(fsc::SyscallId::mlock2)        == fll::SyscallFamily::MemoryMapping);
static_assert(fsc::family_of(fsc::SyscallId::mlock)         == fll::SyscallFamily::MemoryMapping);
static_assert(fsc::family_of(fsc::SyscallId::munlock)       == fll::SyscallFamily::MemoryMapping);
static_assert(fsc::family_of(fsc::SyscallId::prctl)         == fll::SyscallFamily::Privilege);

// Cross-check that pre-existing enumerators madvise / sched_setaffinity
// still classify correctly (V-180 must not regress V-098's surface).
static_assert(fsc::family_of(fsc::SyscallId::madvise)           == fll::SyscallFamily::MemoryMapping);
static_assert(fsc::family_of(fsc::SyscallId::sched_setaffinity) == fll::SyscallFamily::ThreadSync);

// ── mint_hardening_syscall_grants — 7-tuple structural witness ──────
using HG = ::crucible::warden::mint_hardening_syscall_grants;

static_assert(std::tuple_size_v<HG> == 7,
    "FIXY-V-180: warden's mint_hardening_syscall_grants must enumerate "
    "exactly the 7 syscalls Hardening::apply() issues: "
    "sched_setaffinity, sched_setattr, mlock, mlock2, munlock, madvise, "
    "prctl.  Any drift between the declared set and the actual syscall "
    "surface is a permission/admission soundness regression.");

// Verify each tuple slot's grant type matches the expected per<Id>.
static_assert(std::is_same_v<std::tuple_element_t<0, HG>,
              fsc::per<fsc::SyscallId::sched_setaffinity>>);
static_assert(std::is_same_v<std::tuple_element_t<1, HG>,
              fsc::per<fsc::SyscallId::sched_setattr>>);
static_assert(std::is_same_v<std::tuple_element_t<2, HG>,
              fsc::per<fsc::SyscallId::mlock>>);
static_assert(std::is_same_v<std::tuple_element_t<3, HG>,
              fsc::per<fsc::SyscallId::mlock2>>);
static_assert(std::is_same_v<std::tuple_element_t<4, HG>,
              fsc::per<fsc::SyscallId::munlock>>);
static_assert(std::is_same_v<std::tuple_element_t<5, HG>,
              fsc::per<fsc::SyscallId::madvise>>);
static_assert(std::is_same_v<std::tuple_element_t<6, HG>,
              fsc::per<fsc::SyscallId::prctl>>);

// ── V-100 Bridge — every grant lifts to its documented Row ──────────
//
// Cross-reference the doc-block in Hardening.h's mint_hardening_syscall_grants
// declaration.  Lift map (per V-100 Bridge.h):
//   ThreadSync     → Row<Block>
//   MemoryMapping  → Row<IO>
//   Privilege      → Row<IO, Block>
using ThreadSyncRow    = ::crucible::effects::Row<::crucible::effects::Effect::Block>;
using MemoryMappingRow = ::crucible::effects::Row<::crucible::effects::Effect::IO>;
using PrivilegeRow     = ::crucible::effects::Row<
    ::crucible::effects::Effect::IO,
    ::crucible::effects::Effect::Block>;

namespace fxbr = ::crucible::fixy::syscall::bridge;

static_assert(std::is_same_v<
    fxbr::lift_syscall_grant_row_t<fsc::per<fsc::SyscallId::sched_setattr>>,
    ThreadSyncRow>);
static_assert(std::is_same_v<
    fxbr::lift_syscall_grant_row_t<fsc::per<fsc::SyscallId::sched_setaffinity>>,
    ThreadSyncRow>);
static_assert(std::is_same_v<
    fxbr::lift_syscall_grant_row_t<fsc::per<fsc::SyscallId::mlock>>,
    MemoryMappingRow>);
static_assert(std::is_same_v<
    fxbr::lift_syscall_grant_row_t<fsc::per<fsc::SyscallId::mlock2>>,
    MemoryMappingRow>);
static_assert(std::is_same_v<
    fxbr::lift_syscall_grant_row_t<fsc::per<fsc::SyscallId::munlock>>,
    MemoryMappingRow>);
static_assert(std::is_same_v<
    fxbr::lift_syscall_grant_row_t<fsc::per<fsc::SyscallId::madvise>>,
    MemoryMappingRow>);
static_assert(std::is_same_v<
    fxbr::lift_syscall_grant_row_t<fsc::per<fsc::SyscallId::prctl>>,
    PrivilegeRow>);

// ── mint_hardening positive — ColdInitCtx admits the mint ───────────
//
// The existing CtxFitsHardeningMint concept gates on Effect::Init;
// the V-180 syscall-grant declaration is a CLASSIFICATION annotation
// that lives ALONGSIDE the gate, not as a row-subrow tightening.
// Verifying the mint instantiates is the positive HS14 sentinel.
static_assert(::crucible::warden::CtxFitsHardeningMint<eff::ColdInitCtx>);
static_assert(!::crucible::warden::CtxFitsHardeningMint<eff::HotFgCtx>);
static_assert(!::crucible::warden::CtxFitsHardeningMint<eff::BgDrainCtx>);

// ── Distinct ordinals → distinct per<> types ────────────────────────
// Every pair of V-180 additions must be a distinct type (NTTP
// discrimination); a federation cache slot per syscall.
static_assert(!std::is_same_v<
    fsc::per<fsc::SyscallId::sched_setattr>,
    fsc::per<fsc::SyscallId::sched_setaffinity>>);
static_assert(!std::is_same_v<
    fsc::per<fsc::SyscallId::mlock>,
    fsc::per<fsc::SyscallId::mlock2>>);
static_assert(!std::is_same_v<
    fsc::per<fsc::SyscallId::mlock>,
    fsc::per<fsc::SyscallId::munlock>>);
static_assert(!std::is_same_v<
    fsc::per<fsc::SyscallId::prctl>,
    fsc::per<fsc::SyscallId::ptrace>>);

}  // namespace

int main() {
    // Compile-time-only surface; the sentinel TU verifies header-embedded
    // static_asserts under project warnings.  No runtime observable
    // behavior (Hardening::apply() requires CAP_SYS_NICE / CAP_IPC_LOCK
    // / CAP_SYS_RESOURCE / etc. so can't be exercised in tests).
    return 0;
}
