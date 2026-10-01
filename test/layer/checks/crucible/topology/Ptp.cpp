// The compile-time checks of crucible/topology/Ptp.h.

#include <crucible/topology/Ptp.h>

namespace crucible::topology {

// On an ISA that lacks the required instruction the standard library
// substitutes a mutex-backed atomic without saying so.  A hidden mutex inside
// the status publication would serialize every reader against the servo update
// loop, so the build refuses such a target instead of regressing quietly.
static_assert(std::atomic<std::uint8_t>::is_always_lock_free, "std::atomic<uint8_t> must be lock-free on this target");
static_assert(std::atomic<std::uint64_t>::is_always_lock_free,
              "std::atomic<uint64_t> must be lock-free on this target");
static_assert(std::atomic<std::int64_t>::is_always_lock_free, "std::atomic<int64_t> must be lock-free on this target");
static_assert(std::atomic<bool>::is_always_lock_free, "std::atomic<bool> must be lock-free on this target");

static_assert(sizeof(PtpClockFd) == sizeof(int));
static_assert(sizeof(PtpTimestampNs) == sizeof(std::uint64_t));
static_assert(sizeof(PtpDeviceIndex) == sizeof(std::uint16_t));
static_assert(sizeof(DeclaredPtpDaemonReport) == sizeof(PtpDaemonReport));
static_assert(sizeof(DeclaredPtpDiagnostic) == sizeof(PtpDiagnostic));
static_assert(std::is_trivially_copyable_v<PtpClockCaps>);
// A refined field keeps no byte route into it, so neither record is
// trivially copyable.  Their copies stay trivial.
static_assert(std::is_trivially_copy_constructible_v<PtpDaemonReport>
              && std::is_trivially_destructible_v<PtpDaemonReport>);
static_assert(std::is_trivially_copy_constructible_v<PtpDiagnostic> && std::is_trivially_destructible_v<PtpDiagnostic>);
static_assert(!CtxFitsPtpMint<::fixy::BgDrainCtx>);
static_assert(CtxFitsPtpMint<::fixy::ColdInitCtx>);
static_assert(!CtxFitsPtpRecord<::fixy::HotFgCtx>);
static_assert(CtxFitsPtpRecord<::fixy::BgDrainCtx>);
static_assert(std::is_base_of_v<::foundation::Pinned<PtpHandle>, PtpHandle>);
static_assert(!std::is_constructible_v<PtpHandle, cog::CogIdentity, PtpClockFd, PtpStatus const&>,
              "a PTP handle is reached only through mint_ptp_handle");

}  // namespace crucible::topology
