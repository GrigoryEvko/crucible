// The compile-time checks of crucible/cntp/RoceConfig.h.

#include <crucible/cntp/RoceConfig.h>

namespace crucible::cntp {

static_assert(dcqcn_state_to_bool(DcqcnState::Active).value() == true);
static_assert(dcqcn_state_to_bool(DcqcnState::Inactive).value() == false);
static_assert(!dcqcn_state_to_bool(DcqcnState::BackendUnavailable).has_value());
static_assert(dcqcn_state_to_bool(DcqcnState::BackendUnavailable).error() == RoceError::DcqcnStatusUnavailable);

static_assert(sizeof(PfcPriorityMask) == sizeof(std::uint8_t));
static_assert(sizeof(RoceDscp) == sizeof(std::uint8_t));
static_assert(sizeof(DcqcnAlphaPpm) == sizeof(std::uint32_t));
static_assert(sizeof(DcqcnTargetPackets) == sizeof(std::uint16_t));
static_assert(sizeof(DcqcnCeThresholdBytes) == sizeof(std::uint32_t));
static_assert(sizeof(DeclaredRoceConfig) == sizeof(RoceConfig));
// A refined member makes a config not trivially copyable, because no byte
// route may build a refined value.  A copy still costs what copying the
// bytes costs.
static_assert(std::is_trivially_copy_constructible_v<DcqcnParams> && std::is_trivially_destructible_v<DcqcnParams>);
static_assert(std::is_trivially_copy_constructible_v<RoceConfig> && std::is_trivially_destructible_v<RoceConfig>);
static_assert(std::is_trivially_copyable_v<PfcPauseStats>);
static_assert(ValidPfcPriorityMask<0b00001000>);
static_assert(!ValidPfcPriorityMask<0>);
static_assert(ValidRoceDscp<63>);
static_assert(!ValidRoceDscp<64>);
static_assert(parse_pfc_pause_counters(" 17\n", "23\n").value().tx_pause_frames == 23);
static_assert(parse_pfc_pause_counters("18446744073709551615", "0").value().rx_pause_frames
              == std::numeric_limits<std::uint64_t>::max());
static_assert(parse_pfc_pause_counters("18446744073709551616", "0").error() == RoceError::CounterParseFailed);
static_assert(parse_pfc_pause_counters("", "0").error() == RoceError::CounterParseFailed);
static_assert(::fixy::fs::CtxFitsFileMint<::fixy::InitLoadCtx, ProcFileReadMode>);
static_assert(!::fixy::fs::CtxFitsFileMint<::fixy::ColdInitCtx, ProcFileReadMode>);

}  // namespace crucible::cntp
