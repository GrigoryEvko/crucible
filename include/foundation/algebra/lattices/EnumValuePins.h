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
//
// Only enums whose lattice was extracted to foundation are pinned here.
// The roster is the set of pinned enums itself; a new lattice enum whose
// value reaches a cache key adds its own pins below.

#include <foundation/algebra/lattices/AllocClassLattice.h>
#include <foundation/algebra/lattices/BarrierStrengthLattice.h>
#include <foundation/algebra/lattices/CipherTierLattice.h>
#include <foundation/algebra/lattices/ClockSourceLattice.h>
#include <foundation/algebra/lattices/DetSafeLattice.h>
#include <foundation/algebra/lattices/HotPathLattice.h>
#include <foundation/algebra/lattices/MemoryScopeLattice.h>
#include <foundation/algebra/lattices/SuspendBehaviorLattice.h>
#include <foundation/algebra/lattices/ToleranceLattice.h>
#include <foundation/algebra/lattices/VendorLattice.h>
#include <foundation/algebra/lattices/WaitLattice.h>

#include <cstdint>
#include <type_traits>

namespace foundation::algebra::lattices::detail::found_046_enum_value_pins {

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

static_assert(std::is_same_v<std::underlying_type_t<BarrierStrength>, std::uint8_t>,
              "BarrierStrength underlying type drifted from uint8_t.");
static_assert(::foundation::algebra::lattices::detail::barrier_strength_lattice_self_test::barrier_strength_count == 7,
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

static_assert(std::is_same_v<std::underlying_type_t<SuspendBehavior>, std::uint8_t>,
              "SuspendBehavior underlying type drifted from uint8_t.");
static_assert(suspend_behavior_count == 3, "SuspendBehavior cardinality drifted from 3.");
static_assert(static_cast<std::uint8_t>(SuspendBehavior::Unknown) == 0, "SuspendBehavior::Unknown drifted.");
static_assert(static_cast<std::uint8_t>(SuspendBehavior::PausesOnSuspend) == 1,
              "SuspendBehavior::PausesOnSuspend drifted.");
static_assert(static_cast<std::uint8_t>(SuspendBehavior::KeepsTicking) == 2, "SuspendBehavior::KeepsTicking drifted.");

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

}  // namespace foundation::algebra::lattices::detail::found_046_enum_value_pins
