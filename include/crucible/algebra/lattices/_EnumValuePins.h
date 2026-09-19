// SPDX-License-Identifier: Apache-2.0
#pragma once

// The underlying value of each enumerator below is folded into the hash that
// keys a shared cache slot, so those values are part of a persisted format
// rather than an implementation detail.  Inserting an enumerator anywhere but
// at the end silently renumbers the ones after it, which changes the hash of
// every instantiation that mentions the enum, invalidates the slots those
// hashes name, and breaks the match between builds.
//
// So a new enumerator takes the next free value, or for the enums that pack a
// group into the high nibble the next free value within its group, and extends
// the pins here in the same change.  A deliberate renumber is a format
// migration, and these assertions are what force that conversation before it
// ships.

#include <crucible/algebra/lattices/_AllocClassLattice.h>
#include <crucible/algebra/lattices/_BarrierStrengthLattice.h>
#include <crucible/algebra/lattices/_CipherTierLattice.h>
#include <crucible/algebra/lattices/_ClockSourceLattice.h>
#include <crucible/algebra/lattices/ConsistencyLattice.h>
#include <crucible/algebra/lattices/CrashLattice.h>
#include <crucible/algebra/lattices/_DetSafeLattice.h>
#include <crucible/algebra/lattices/_HotPathLattice.h>
#include <crucible/algebra/lattices/HwInstructionLattice.h>
#include <crucible/algebra/lattices/JoinPolicyLattice.h>
#include <crucible/algebra/lattices/MemOrderLattice.h>
#include <crucible/algebra/lattices/_MemoryScopeLattice.h>
#include <crucible/algebra/lattices/ProgressLattice.h>
#include <crucible/algebra/lattices/ResidencyHeatLattice.h>
#include <crucible/algebra/lattices/SimdIsaLattice.h>
#include <crucible/algebra/lattices/_SuspendBehaviorLattice.h>
#include <crucible/algebra/lattices/_ToleranceLattice.h>
#include <crucible/algebra/lattices/_VendorLattice.h>
#include <crucible/algebra/lattices/_WaitLattice.h>
#include <crucible/algebra/lattices/WitnessLattice.h>

#include <cstdint>
#include <type_traits>

namespace crucible::algebra::lattices::detail::found_046_enum_value_pins {

static_assert(std::is_same_v<std::underlying_type_t<HotPathTier>, std::uint8_t>,
              "HotPathTier underlying type drifted from uint8_t.");
static_assert(hot_path_tier_count == 3, "HotPathTier cardinality drifted from 3.");
static_assert(static_cast<std::uint8_t>(HotPathTier::Cold) == 0,
              "HotPathTier::Cold drifted, which invalidates every cache slot keyed on it.");
static_assert(static_cast<std::uint8_t>(HotPathTier::Warm) == 1, "HotPathTier::Warm drifted.");
static_assert(static_cast<std::uint8_t>(HotPathTier::Hot) == 2, "HotPathTier::Hot drifted.");

static_assert(std::is_same_v<std::underlying_type_t<DetSafeTier>, std::uint8_t>,
              "DetSafeTier underlying type drifted from uint8_t.");
static_assert(det_safe_tier_count == 7, "DetSafeTier cardinality drifted from 7.");
static_assert(static_cast<std::uint8_t>(DetSafeTier::NonDeterministicSyscall) == 0,
              "DetSafeTier::NonDeterministicSyscall drifted.");
static_assert(static_cast<std::uint8_t>(DetSafeTier::FilesystemMtime) == 1, "DetSafeTier::FilesystemMtime drifted.");
static_assert(static_cast<std::uint8_t>(DetSafeTier::EntropyRead) == 2, "DetSafeTier::EntropyRead drifted.");
static_assert(static_cast<std::uint8_t>(DetSafeTier::WallClockRead) == 3, "DetSafeTier::WallClockRead drifted.");
static_assert(static_cast<std::uint8_t>(DetSafeTier::MonotonicClockRead) == 4,
              "DetSafeTier::MonotonicClockRead drifted.");
static_assert(static_cast<std::uint8_t>(DetSafeTier::PhiloxRng) == 5, "DetSafeTier::PhiloxRng drifted.");
static_assert(static_cast<std::uint8_t>(DetSafeTier::Pure) == 6, "DetSafeTier::Pure drifted.");

static_assert(std::is_same_v<std::underlying_type_t<Tolerance>, std::uint8_t>,
              "Tolerance underlying type drifted from uint8_t.");
static_assert(tolerance_count == 7, "Tolerance cardinality drifted from 7.");
static_assert(static_cast<std::uint8_t>(Tolerance::RELAXED) == 0, "Tolerance::RELAXED drifted.");
static_assert(static_cast<std::uint8_t>(Tolerance::ULP_INT8) == 1, "Tolerance::ULP_INT8 drifted.");
static_assert(static_cast<std::uint8_t>(Tolerance::ULP_FP8) == 2, "Tolerance::ULP_FP8 drifted.");
static_assert(static_cast<std::uint8_t>(Tolerance::ULP_FP16) == 3, "Tolerance::ULP_FP16 drifted.");
static_assert(static_cast<std::uint8_t>(Tolerance::ULP_FP32) == 4, "Tolerance::ULP_FP32 drifted.");
static_assert(static_cast<std::uint8_t>(Tolerance::ULP_FP64) == 5, "Tolerance::ULP_FP64 drifted.");
static_assert(static_cast<std::uint8_t>(Tolerance::BITEXACT) == 6, "Tolerance::BITEXACT drifted.");

static_assert(std::is_same_v<std::underlying_type_t<VendorBackend>, std::uint8_t>,
              "VendorBackend underlying type drifted from uint8_t.");
static_assert(vendor_backend_count == 8, "VendorBackend cardinality drifted from 8.");
static_assert(static_cast<std::uint8_t>(VendorBackend::None) == 0, "VendorBackend::None drifted (⊥ sentinel).");
static_assert(static_cast<std::uint8_t>(VendorBackend::CPU) == 1, "VendorBackend::CPU drifted.");
static_assert(static_cast<std::uint8_t>(VendorBackend::NV) == 2, "VendorBackend::NV drifted.");
static_assert(static_cast<std::uint8_t>(VendorBackend::AMD) == 3, "VendorBackend::AMD drifted.");
static_assert(static_cast<std::uint8_t>(VendorBackend::TPU) == 4, "VendorBackend::TPU drifted.");
static_assert(static_cast<std::uint8_t>(VendorBackend::TRN) == 5, "VendorBackend::TRN drifted.");
static_assert(static_cast<std::uint8_t>(VendorBackend::CER) == 6, "VendorBackend::CER drifted.");
static_assert(static_cast<std::uint8_t>(VendorBackend::Portable) == 255,
              "VendorBackend::Portable drifted (⊤ sentinel).");

static_assert(std::is_same_v<std::underlying_type_t<HwInstruction>, std::uint8_t>,
              "HwInstruction underlying type drifted from uint8_t.");
static_assert(::crucible::algebra::lattices::detail::hw_instruction_lattice_self_test::hw_instruction_count == 5,
              "HwInstruction cardinality drifted from 5.");
static_assert(static_cast<std::uint8_t>(HwInstruction::NoneAllowed) == 0, "HwInstruction::NoneAllowed drifted.");
static_assert(static_cast<std::uint8_t>(HwInstruction::Scalar) == 1, "HwInstruction::Scalar drifted.");
static_assert(static_cast<std::uint8_t>(HwInstruction::Vectorizable) == 2, "HwInstruction::Vectorizable drifted.");
static_assert(static_cast<std::uint8_t>(HwInstruction::NonDeterministicTsc) == 3,
              "HwInstruction::NonDeterministicTsc drifted.");
static_assert(static_cast<std::uint8_t>(HwInstruction::PrivilegedMsr) == 4, "HwInstruction::PrivilegedMsr drifted.");

static_assert(std::is_same_v<std::underlying_type_t<BarrierStrength>, std::uint8_t>,
              "BarrierStrength underlying type drifted from uint8_t.");
static_assert(::crucible::algebra::lattices::detail::barrier_strength_lattice_self_test::barrier_strength_count == 7,
              "BarrierStrength cardinality drifted from 7.");
static_assert(static_cast<std::uint8_t>(BarrierStrength::None) == 0, "BarrierStrength::None drifted.");
static_assert(static_cast<std::uint8_t>(BarrierStrength::CompilerBarrier) == 1,
              "BarrierStrength::CompilerBarrier drifted.");
static_assert(static_cast<std::uint8_t>(BarrierStrength::AcquireLoad) == 2, "BarrierStrength::AcquireLoad drifted.");
static_assert(static_cast<std::uint8_t>(BarrierStrength::ReleaseStore) == 3, "BarrierStrength::ReleaseStore drifted.");
static_assert(static_cast<std::uint8_t>(BarrierStrength::AcqRel) == 4, "BarrierStrength::AcqRel drifted.");
static_assert(static_cast<std::uint8_t>(BarrierStrength::SeqCst) == 5, "BarrierStrength::SeqCst drifted.");
static_assert(static_cast<std::uint8_t>(BarrierStrength::FullFence) == 6, "BarrierStrength::FullFence drifted.");

static_assert(std::is_same_v<std::underlying_type_t<MemoryScope>, std::uint8_t>,
              "MemoryScope underlying type drifted from uint8_t.");
static_assert(memory_scope_count == 8, "MemoryScope cardinality drifted from 8.");
static_assert(static_cast<std::uint8_t>(MemoryScope::Thread) == 0x00,
              "MemoryScope::Thread drifted from 0x00 (⊥ sentinel).");
static_assert(static_cast<std::uint8_t>(MemoryScope::Warp) == 0x10, "MemoryScope::Warp drifted from 0x10 (GPU trunk).");
static_assert(static_cast<std::uint8_t>(MemoryScope::Cta) == 0x11, "MemoryScope::Cta drifted from 0x11.");
static_assert(static_cast<std::uint8_t>(MemoryScope::Cluster) == 0x12, "MemoryScope::Cluster drifted from 0x12.");
static_assert(static_cast<std::uint8_t>(MemoryScope::Gpu) == 0x13, "MemoryScope::Gpu drifted from 0x13.");
static_assert(static_cast<std::uint8_t>(MemoryScope::Inner) == 0x20,
              "MemoryScope::Inner drifted from 0x20 (ARM-host trunk).");
static_assert(static_cast<std::uint8_t>(MemoryScope::Outer) == 0x21, "MemoryScope::Outer drifted from 0x21.");
static_assert(static_cast<std::uint8_t>(MemoryScope::System) == 0xFF,
              "MemoryScope::System drifted from 0xFF (⊤ sentinel).");

static_assert(std::is_same_v<std::underlying_type_t<SimdIsa>, std::uint8_t>,
              "SimdIsa underlying type drifted from uint8_t.");
static_assert(simd_isa_count == 15, "SimdIsa cardinality drifted from 15.");
static_assert(static_cast<std::uint8_t>(SimdIsa::Scalar) == 0x00, "SimdIsa::Scalar drifted from 0x00 (⊥ sentinel).");
static_assert(static_cast<std::uint8_t>(SimdIsa::Sse2) == 0x10, "SimdIsa::Sse2 drifted from 0x10 (x86 trunk).");
static_assert(static_cast<std::uint8_t>(SimdIsa::Sse3) == 0x11, "SimdIsa::Sse3 drifted from 0x11.");
static_assert(static_cast<std::uint8_t>(SimdIsa::Ssse3) == 0x12, "SimdIsa::Ssse3 drifted from 0x12.");
static_assert(static_cast<std::uint8_t>(SimdIsa::Sse41) == 0x13, "SimdIsa::Sse41 drifted from 0x13.");
static_assert(static_cast<std::uint8_t>(SimdIsa::Sse42) == 0x14, "SimdIsa::Sse42 drifted from 0x14.");
static_assert(static_cast<std::uint8_t>(SimdIsa::Avx2) == 0x15, "SimdIsa::Avx2 drifted from 0x15.");
static_assert(static_cast<std::uint8_t>(SimdIsa::Avx512F) == 0x16, "SimdIsa::Avx512F drifted from 0x16.");
static_assert(static_cast<std::uint8_t>(SimdIsa::Avx512Bw) == 0x17, "SimdIsa::Avx512Bw drifted from 0x17.");
static_assert(static_cast<std::uint8_t>(SimdIsa::Neon) == 0x20, "SimdIsa::Neon drifted from 0x20 (ARM trunk).");
static_assert(static_cast<std::uint8_t>(SimdIsa::NeonFp16) == 0x21, "SimdIsa::NeonFp16 drifted from 0x21.");
static_assert(static_cast<std::uint8_t>(SimdIsa::NeonDotProduct) == 0x22, "SimdIsa::NeonDotProduct drifted from 0x22.");
static_assert(static_cast<std::uint8_t>(SimdIsa::Sve) == 0x23, "SimdIsa::Sve drifted from 0x23.");
static_assert(static_cast<std::uint8_t>(SimdIsa::Sve2) == 0x24, "SimdIsa::Sve2 drifted from 0x24.");
static_assert(static_cast<std::uint8_t>(SimdIsa::Portable) == 0xFF,
              "SimdIsa::Portable drifted from 0xFF (⊤ sentinel).");

static_assert(std::is_same_v<std::underlying_type_t<ResidencyHeatTag>, std::uint8_t>,
              "ResidencyHeatTag underlying type drifted from uint8_t.");
static_assert(residency_heat_tag_count == 3, "ResidencyHeatTag cardinality drifted from 3.");
static_assert(static_cast<std::uint8_t>(ResidencyHeatTag::Cold) == 0, "ResidencyHeatTag::Cold drifted.");
static_assert(static_cast<std::uint8_t>(ResidencyHeatTag::Warm) == 1, "ResidencyHeatTag::Warm drifted.");
static_assert(static_cast<std::uint8_t>(ResidencyHeatTag::Hot) == 2, "ResidencyHeatTag::Hot drifted.");

static_assert(std::is_same_v<std::underlying_type_t<CipherTierTag>, std::uint8_t>,
              "CipherTierTag underlying type drifted from uint8_t.");
static_assert(cipher_tier_tag_count == 3, "CipherTierTag cardinality drifted from 3.");
static_assert(static_cast<std::uint8_t>(CipherTierTag::Cold) == 0, "CipherTierTag::Cold drifted.");
static_assert(static_cast<std::uint8_t>(CipherTierTag::Warm) == 1, "CipherTierTag::Warm drifted.");
static_assert(static_cast<std::uint8_t>(CipherTierTag::Hot) == 2, "CipherTierTag::Hot drifted.");

static_assert(std::is_same_v<std::underlying_type_t<AllocClassTag>, std::uint8_t>,
              "AllocClassTag underlying type drifted from uint8_t.");
static_assert(alloc_class_tag_count == 6, "AllocClassTag cardinality drifted from 6.");
static_assert(static_cast<std::uint8_t>(AllocClassTag::HugePage) == 0, "AllocClassTag::HugePage drifted.");
static_assert(static_cast<std::uint8_t>(AllocClassTag::Mmap) == 1, "AllocClassTag::Mmap drifted.");
static_assert(static_cast<std::uint8_t>(AllocClassTag::Heap) == 2, "AllocClassTag::Heap drifted.");
static_assert(static_cast<std::uint8_t>(AllocClassTag::Arena) == 3, "AllocClassTag::Arena drifted.");
static_assert(static_cast<std::uint8_t>(AllocClassTag::Pool) == 4, "AllocClassTag::Pool drifted.");
static_assert(static_cast<std::uint8_t>(AllocClassTag::Stack) == 5, "AllocClassTag::Stack drifted.");

static_assert(std::is_same_v<std::underlying_type_t<WaitStrategy>, std::uint8_t>,
              "WaitStrategy underlying type drifted from uint8_t.");
static_assert(wait_strategy_count == 6, "WaitStrategy cardinality drifted from 6.");
static_assert(static_cast<std::uint8_t>(WaitStrategy::Block) == 0, "WaitStrategy::Block drifted.");
static_assert(static_cast<std::uint8_t>(WaitStrategy::Park) == 1, "WaitStrategy::Park drifted.");
static_assert(static_cast<std::uint8_t>(WaitStrategy::AcquireWait) == 2, "WaitStrategy::AcquireWait drifted.");
static_assert(static_cast<std::uint8_t>(WaitStrategy::UmwaitC01) == 3, "WaitStrategy::UmwaitC01 drifted.");
static_assert(static_cast<std::uint8_t>(WaitStrategy::BoundedSpin) == 4, "WaitStrategy::BoundedSpin drifted.");
static_assert(static_cast<std::uint8_t>(WaitStrategy::SpinPause) == 5, "WaitStrategy::SpinPause drifted.");

static_assert(std::is_same_v<std::underlying_type_t<MemOrderTag>, std::uint8_t>,
              "MemOrderTag underlying type drifted from uint8_t.");
static_assert(mem_order_tag_count == 5, "MemOrderTag cardinality drifted from 5.");
static_assert(static_cast<std::uint8_t>(MemOrderTag::SeqCst) == 0, "MemOrderTag::SeqCst drifted.");
static_assert(static_cast<std::uint8_t>(MemOrderTag::AcqRel) == 1, "MemOrderTag::AcqRel drifted.");
static_assert(static_cast<std::uint8_t>(MemOrderTag::Release) == 2, "MemOrderTag::Release drifted.");
static_assert(static_cast<std::uint8_t>(MemOrderTag::Acquire) == 3, "MemOrderTag::Acquire drifted.");
static_assert(static_cast<std::uint8_t>(MemOrderTag::Relaxed) == 4, "MemOrderTag::Relaxed drifted.");

static_assert(std::is_same_v<std::underlying_type_t<ProgressClass>, std::uint8_t>,
              "ProgressClass underlying type drifted from uint8_t.");
static_assert(progress_class_count == 4, "ProgressClass cardinality drifted from 4.");
static_assert(static_cast<std::uint8_t>(ProgressClass::MayDiverge) == 0, "ProgressClass::MayDiverge drifted.");
static_assert(static_cast<std::uint8_t>(ProgressClass::Terminating) == 1, "ProgressClass::Terminating drifted.");
static_assert(static_cast<std::uint8_t>(ProgressClass::Productive) == 2, "ProgressClass::Productive drifted.");
static_assert(static_cast<std::uint8_t>(ProgressClass::Bounded) == 3, "ProgressClass::Bounded drifted.");

static_assert(std::is_same_v<std::underlying_type_t<CrashClass>, std::uint8_t>,
              "CrashClass underlying type drifted from uint8_t.");
static_assert(crash_class_count == 4, "CrashClass cardinality drifted from 4.");
static_assert(static_cast<std::uint8_t>(CrashClass::Abort) == 0, "CrashClass::Abort drifted.");
static_assert(static_cast<std::uint8_t>(CrashClass::Throw) == 1, "CrashClass::Throw drifted.");
static_assert(static_cast<std::uint8_t>(CrashClass::ErrorReturn) == 2, "CrashClass::ErrorReturn drifted.");
static_assert(static_cast<std::uint8_t>(CrashClass::NoThrow) == 3, "CrashClass::NoThrow drifted.");

static_assert(std::is_same_v<std::underlying_type_t<SuspendBehavior>, std::uint8_t>,
              "SuspendBehavior underlying type drifted from uint8_t.");
static_assert(suspend_behavior_count == 3, "SuspendBehavior cardinality drifted from 3.");
static_assert(static_cast<std::uint8_t>(SuspendBehavior::Unknown) == 0, "SuspendBehavior::Unknown drifted.");
static_assert(static_cast<std::uint8_t>(SuspendBehavior::PausesOnSuspend) == 1,
              "SuspendBehavior::PausesOnSuspend drifted.");
static_assert(static_cast<std::uint8_t>(SuspendBehavior::KeepsTicking) == 2, "SuspendBehavior::KeepsTicking drifted.");

static_assert(std::is_same_v<std::underlying_type_t<JoinPolicy>, std::uint8_t>,
              "JoinPolicy underlying type drifted from uint8_t.");
static_assert(join_policy_count == 6, "JoinPolicy cardinality drifted from 6.");
static_assert(static_cast<std::uint8_t>(JoinPolicy::FORGET) == 0, "JoinPolicy::FORGET drifted.");
static_assert(static_cast<std::uint8_t>(JoinPolicy::DETACH) == 1, "JoinPolicy::DETACH drifted.");
static_assert(static_cast<std::uint8_t>(JoinPolicy::ABANDON) == 2, "JoinPolicy::ABANDON drifted.");
static_assert(static_cast<std::uint8_t>(JoinPolicy::CANCEL) == 3, "JoinPolicy::CANCEL drifted.");
static_assert(static_cast<std::uint8_t>(JoinPolicy::WAIT_DEADLINE) == 4, "JoinPolicy::WAIT_DEADLINE drifted.");
static_assert(static_cast<std::uint8_t>(JoinPolicy::JOIN_ALL) == 5, "JoinPolicy::JOIN_ALL drifted.");

static_assert(std::is_same_v<std::underlying_type_t<Consistency>, std::uint8_t>,
              "Consistency underlying type drifted from uint8_t.");
static_assert(consistency_count == 5, "Consistency cardinality drifted from 5.");
static_assert(static_cast<std::uint8_t>(Consistency::EVENTUAL) == 0, "Consistency::EVENTUAL drifted.");
static_assert(static_cast<std::uint8_t>(Consistency::READ_YOUR_WRITES) == 1, "Consistency::READ_YOUR_WRITES drifted.");
static_assert(static_cast<std::uint8_t>(Consistency::CAUSAL_PREFIX) == 2, "Consistency::CAUSAL_PREFIX drifted.");
static_assert(static_cast<std::uint8_t>(Consistency::BOUNDED_STALENESS) == 3,
              "Consistency::BOUNDED_STALENESS drifted.");
static_assert(static_cast<std::uint8_t>(Consistency::STRONG) == 4, "Consistency::STRONG drifted.");

static_assert(std::is_same_v<std::underlying_type_t<Witness>, std::uint8_t>,
              "Witness underlying type drifted from uint8_t.");
static_assert(witness_count == 4, "Witness cardinality drifted from 4.");
static_assert(static_cast<std::uint8_t>(Witness::UNWITNESSED) == 0, "Witness::UNWITNESSED drifted.");
static_assert(static_cast<std::uint8_t>(Witness::TYPE_CHECKED) == 1, "Witness::TYPE_CHECKED drifted.");
static_assert(static_cast<std::uint8_t>(Witness::TEST_PASSED) == 2, "Witness::TEST_PASSED drifted.");
static_assert(static_cast<std::uint8_t>(Witness::FORMALLY_VERIFIED) == 3, "Witness::FORMALLY_VERIFIED drifted.");

static_assert(std::is_same_v<std::underlying_type_t<ClockSource>, std::uint8_t>,
              "ClockSource underlying type drifted from uint8_t.");
static_assert(clock_source_count == 10, "ClockSource cardinality drifted from 10.");
static_assert(static_cast<std::uint8_t>(ClockSource::Realtime) == 0, "ClockSource::Realtime drifted.");
static_assert(static_cast<std::uint8_t>(ClockSource::Monotonic) == 1, "ClockSource::Monotonic drifted.");
static_assert(static_cast<std::uint8_t>(ClockSource::MonotonicRaw) == 2, "ClockSource::MonotonicRaw drifted.");
static_assert(static_cast<std::uint8_t>(ClockSource::Boot) == 3, "ClockSource::Boot drifted.");
static_assert(static_cast<std::uint8_t>(ClockSource::ThreadCpu) == 4, "ClockSource::ThreadCpu drifted.");
static_assert(static_cast<std::uint8_t>(ClockSource::ProcessCpu) == 5, "ClockSource::ProcessCpu drifted.");
static_assert(static_cast<std::uint8_t>(ClockSource::TscRaw) == 6, "ClockSource::TscRaw drifted.");
static_assert(static_cast<std::uint8_t>(ClockSource::TscSerialized) == 7, "ClockSource::TscSerialized drifted.");
static_assert(static_cast<std::uint8_t>(ClockSource::PmuCounter) == 8, "ClockSource::PmuCounter drifted.");
static_assert(static_cast<std::uint8_t>(ClockSource::PtpHwClock) == 9, "ClockSource::PtpHwClock drifted.");

inline constexpr std::size_t kFound046PinnedEnumCount = 20;
static_assert(kFound046PinnedEnumCount == 20, "The roster pins 20 lattice enums.  A new lattice enum whose value "
                                              "reaches a cache key needs its own pins above and this literal "
                                              "bumped.");

}  // namespace crucible::algebra::lattices::detail::found_046_enum_value_pins
