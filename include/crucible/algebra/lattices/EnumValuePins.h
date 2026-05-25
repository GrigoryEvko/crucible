// SPDX-License-Identifier: Apache-2.0
#pragma once

// ─────────────────────────────────────────────────────────────────────
// FIXY-FOUND-046 — Per-enumerator value-pin sweep
// ─────────────────────────────────────────────────────────────────────
//
// Every lattice enum consumed by `safety::diag::row_hash_contribution<W>`
// (see RowHashFold.h §I02 specializations at lines 699-1144) folds its
// underlying value into the salt low-byte:
//
//     row_hash_contribution<W<Tier, Inner>>::value =
//         combine_ids(WRAPPER_TAG | static_cast<uint64_t>(Tier),
//                     row_hash_contribution_v<Inner>);
//
// If a lattice enum's enumerator drifts off its canonical underlying
// value — e.g. a future contributor inserts `NewTier = 1` before the
// existing `Tier::A = 1` enumerator, silently renumbering everything
// after it — the row_hash of EVERY wrapper instantiation that mentions
// the enum changes.  Federation cache slot IDs invalidate; cross-build
// determinism breaks; KernelCache misses cascade.
//
// FIXY-FOUND-051 pinned `effects::Effect` (the canonical example);
// FOUND-046 extends the discipline to ALL lattice enums consumed by
// row_hash.  Each pin block asserts:
//   (1) static_cast<U>(Enum::Member) == kCanonicalValue per enumerator
//   (2) Cardinality assertion against the enum's reflection-derived
//       count constexpr
//   (3) Underlying-type pin (= uint8_t) — ABI defense
//
// Adding a new enumerator MUST land at the next free underlying value
// (or, for trunk-packed enums, the next free slot within the
// appropriate trunk) AND requires extending the pin block below at
// the same time.  A drift between the enum definition and the pin
// block fires here.
//
// Out of scope for FOUND-046 (deferred to follow-up tasks):
//   - FpMode sub-axes (FpComplexLayout, FpLibmPolicy, FpRoundingMode,
//     FpDenormalHandling, ...) — pinned in their respective sub-axis
//     blocks within FpModeLattice.h
//   - SchedulerPolicy (SchedClassLattice — composite hash with
//     budget NTTPs; pin its enumerator base values when the
//     composite stabilizes)
//   - Per-axis tag types that are class-based (not enum-based) like
//     AllocClass storage tags carrying state
//
// Federation cache versioning: a deliberate value renumber requires
// the major-version migration ceremony (publish_l2/l3 cache
// invalidation, KernelCache rebuild, cross-build replay re-anchor).
// This pin block fires BEFORE such a change ships, surfacing the
// invalidation surface to the contributor.

#include <crucible/algebra/lattices/AllocClassLattice.h>
#include <crucible/algebra/lattices/BarrierStrengthLattice.h>
#include <crucible/algebra/lattices/CipherTierLattice.h>
#include <crucible/algebra/lattices/ClockSourceLattice.h>
#include <crucible/algebra/lattices/ConsistencyLattice.h>
#include <crucible/algebra/lattices/CrashLattice.h>
#include <crucible/algebra/lattices/DetSafeLattice.h>
#include <crucible/algebra/lattices/HotPathLattice.h>
#include <crucible/algebra/lattices/HwInstructionLattice.h>
#include <crucible/algebra/lattices/JoinPolicyLattice.h>
#include <crucible/algebra/lattices/MemOrderLattice.h>
#include <crucible/algebra/lattices/MemoryScopeLattice.h>
#include <crucible/algebra/lattices/ProgressLattice.h>
#include <crucible/algebra/lattices/ResidencyHeatLattice.h>
#include <crucible/algebra/lattices/SimdIsaLattice.h>
#include <crucible/algebra/lattices/SuspendBehaviorLattice.h>
#include <crucible/algebra/lattices/ToleranceLattice.h>
#include <crucible/algebra/lattices/VendorLattice.h>
#include <crucible/algebra/lattices/WaitLattice.h>
#include <crucible/algebra/lattices/WitnessLattice.h>

#include <cstdint>
#include <type_traits>

namespace crucible::algebra::lattices::detail::found_046_enum_value_pins {

// ── (1) HotPathTier — 3 tiers, trivial declaration-order layout ───
static_assert(std::is_same_v<std::underlying_type_t<HotPathTier>,
                             std::uint8_t>,
    "FIXY-FOUND-046: HotPathTier underlying type drifted from uint8_t.");
static_assert(hot_path_tier_count == 3,
    "FIXY-FOUND-046: HotPathTier cardinality drifted from 3.");
static_assert(static_cast<std::uint8_t>(HotPathTier::Cold) == 0,
    "FIXY-FOUND-046: HotPathTier::Cold drifted — federation row_hash invalidated.");
static_assert(static_cast<std::uint8_t>(HotPathTier::Warm) == 1,
    "FIXY-FOUND-046: HotPathTier::Warm drifted.");
static_assert(static_cast<std::uint8_t>(HotPathTier::Hot)  == 2,
    "FIXY-FOUND-046: HotPathTier::Hot drifted.");

// ── (2) DetSafeTier — 7 tiers, trivial layout ─────────────────────
static_assert(std::is_same_v<std::underlying_type_t<DetSafeTier>,
                             std::uint8_t>,
    "FIXY-FOUND-046: DetSafeTier underlying type drifted from uint8_t.");
static_assert(det_safe_tier_count == 7,
    "FIXY-FOUND-046: DetSafeTier cardinality drifted from 7.");
static_assert(static_cast<std::uint8_t>(DetSafeTier::NonDeterministicSyscall) == 0,
    "FIXY-FOUND-046: DetSafeTier::NonDeterministicSyscall drifted.");
static_assert(static_cast<std::uint8_t>(DetSafeTier::FilesystemMtime)         == 1,
    "FIXY-FOUND-046: DetSafeTier::FilesystemMtime drifted.");
static_assert(static_cast<std::uint8_t>(DetSafeTier::EntropyRead)             == 2,
    "FIXY-FOUND-046: DetSafeTier::EntropyRead drifted.");
static_assert(static_cast<std::uint8_t>(DetSafeTier::WallClockRead)           == 3,
    "FIXY-FOUND-046: DetSafeTier::WallClockRead drifted.");
static_assert(static_cast<std::uint8_t>(DetSafeTier::MonotonicClockRead)      == 4,
    "FIXY-FOUND-046: DetSafeTier::MonotonicClockRead drifted.");
static_assert(static_cast<std::uint8_t>(DetSafeTier::PhiloxRng)               == 5,
    "FIXY-FOUND-046: DetSafeTier::PhiloxRng drifted.");
static_assert(static_cast<std::uint8_t>(DetSafeTier::Pure)                    == 6,
    "FIXY-FOUND-046: DetSafeTier::Pure drifted.");

// ── (3) Tolerance — 7 numerical tiers, trivial layout ──────────────
static_assert(std::is_same_v<std::underlying_type_t<Tolerance>, std::uint8_t>,
    "FIXY-FOUND-046: Tolerance underlying type drifted from uint8_t.");
static_assert(tolerance_count == 7,
    "FIXY-FOUND-046: Tolerance cardinality drifted from 7.");
static_assert(static_cast<std::uint8_t>(Tolerance::RELAXED)  == 0,
    "FIXY-FOUND-046: Tolerance::RELAXED drifted.");
static_assert(static_cast<std::uint8_t>(Tolerance::ULP_INT8) == 1,
    "FIXY-FOUND-046: Tolerance::ULP_INT8 drifted.");
static_assert(static_cast<std::uint8_t>(Tolerance::ULP_FP8)  == 2,
    "FIXY-FOUND-046: Tolerance::ULP_FP8 drifted.");
static_assert(static_cast<std::uint8_t>(Tolerance::ULP_FP16) == 3,
    "FIXY-FOUND-046: Tolerance::ULP_FP16 drifted.");
static_assert(static_cast<std::uint8_t>(Tolerance::ULP_FP32) == 4,
    "FIXY-FOUND-046: Tolerance::ULP_FP32 drifted.");
static_assert(static_cast<std::uint8_t>(Tolerance::ULP_FP64) == 5,
    "FIXY-FOUND-046: Tolerance::ULP_FP64 drifted.");
static_assert(static_cast<std::uint8_t>(Tolerance::BITEXACT) == 6,
    "FIXY-FOUND-046: Tolerance::BITEXACT drifted.");

// ── (4) VendorBackend — 8 enumerators, Portable=255 sentinel ───────
static_assert(std::is_same_v<std::underlying_type_t<VendorBackend>,
                             std::uint8_t>,
    "FIXY-FOUND-046: VendorBackend underlying type drifted from uint8_t.");
static_assert(vendor_backend_count == 8,
    "FIXY-FOUND-046: VendorBackend cardinality drifted from 8.");
static_assert(static_cast<std::uint8_t>(VendorBackend::None)     == 0,
    "FIXY-FOUND-046: VendorBackend::None drifted (⊥ sentinel).");
static_assert(static_cast<std::uint8_t>(VendorBackend::CPU)      == 1,
    "FIXY-FOUND-046: VendorBackend::CPU drifted.");
static_assert(static_cast<std::uint8_t>(VendorBackend::NV)       == 2,
    "FIXY-FOUND-046: VendorBackend::NV drifted.");
static_assert(static_cast<std::uint8_t>(VendorBackend::AMD)      == 3,
    "FIXY-FOUND-046: VendorBackend::AMD drifted.");
static_assert(static_cast<std::uint8_t>(VendorBackend::TPU)      == 4,
    "FIXY-FOUND-046: VendorBackend::TPU drifted.");
static_assert(static_cast<std::uint8_t>(VendorBackend::TRN)      == 5,
    "FIXY-FOUND-046: VendorBackend::TRN drifted.");
static_assert(static_cast<std::uint8_t>(VendorBackend::CER)      == 6,
    "FIXY-FOUND-046: VendorBackend::CER drifted.");
static_assert(static_cast<std::uint8_t>(VendorBackend::Portable) == 255,
    "FIXY-FOUND-046: VendorBackend::Portable drifted (⊤ sentinel).");

// ── (5) HwInstruction — 5 tiers, trivial layout ────────────────────
static_assert(std::is_same_v<std::underlying_type_t<HwInstruction>,
                             std::uint8_t>,
    "FIXY-FOUND-046: HwInstruction underlying type drifted from uint8_t.");
static_assert(static_cast<std::uint8_t>(HwInstruction::NoneAllowed)         == 0,
    "FIXY-FOUND-046: HwInstruction::NoneAllowed drifted.");
static_assert(static_cast<std::uint8_t>(HwInstruction::Scalar)              == 1,
    "FIXY-FOUND-046: HwInstruction::Scalar drifted.");
static_assert(static_cast<std::uint8_t>(HwInstruction::Vectorizable)        == 2,
    "FIXY-FOUND-046: HwInstruction::Vectorizable drifted.");
static_assert(static_cast<std::uint8_t>(HwInstruction::NonDeterministicTsc) == 3,
    "FIXY-FOUND-046: HwInstruction::NonDeterministicTsc drifted.");
static_assert(static_cast<std::uint8_t>(HwInstruction::PrivilegedMsr)       == 4,
    "FIXY-FOUND-046: HwInstruction::PrivilegedMsr drifted.");

// ── (6) BarrierStrength — 7 tiers, trivial layout ──────────────────
static_assert(std::is_same_v<std::underlying_type_t<BarrierStrength>,
                             std::uint8_t>,
    "FIXY-FOUND-046: BarrierStrength underlying type drifted from uint8_t.");
static_assert(static_cast<std::uint8_t>(BarrierStrength::None)            == 0,
    "FIXY-FOUND-046: BarrierStrength::None drifted.");
static_assert(static_cast<std::uint8_t>(BarrierStrength::CompilerBarrier) == 1,
    "FIXY-FOUND-046: BarrierStrength::CompilerBarrier drifted.");
static_assert(static_cast<std::uint8_t>(BarrierStrength::AcquireLoad)     == 2,
    "FIXY-FOUND-046: BarrierStrength::AcquireLoad drifted.");
static_assert(static_cast<std::uint8_t>(BarrierStrength::ReleaseStore)    == 3,
    "FIXY-FOUND-046: BarrierStrength::ReleaseStore drifted.");
static_assert(static_cast<std::uint8_t>(BarrierStrength::AcqRel)          == 4,
    "FIXY-FOUND-046: BarrierStrength::AcqRel drifted.");
static_assert(static_cast<std::uint8_t>(BarrierStrength::SeqCst)          == 5,
    "FIXY-FOUND-046: BarrierStrength::SeqCst drifted.");
static_assert(static_cast<std::uint8_t>(BarrierStrength::FullFence)       == 6,
    "FIXY-FOUND-046: BarrierStrength::FullFence drifted.");

// ── (7) MemoryScope — 9 enumerators, trunk-packed layout ───────────
//
// HIGH NIBBLE encodes the trunk (0x0 = sentinel, 0x1 = GPU,
// 0x2 = ARM-host); LOW NIBBLE encodes the within-trunk rank.
// Cross-trunk values intentionally do NOT compose (V401 collision
// rule).  Each enumerator's underlying value is part of the wire
// format — drift requires major-version migration.
static_assert(std::is_same_v<std::underlying_type_t<MemoryScope>,
                             std::uint8_t>,
    "FIXY-FOUND-046: MemoryScope underlying type drifted from uint8_t.");
static_assert(static_cast<std::uint8_t>(MemoryScope::Thread)  == 0x00,
    "FIXY-FOUND-046: MemoryScope::Thread drifted from 0x00 (⊥ sentinel).");
static_assert(static_cast<std::uint8_t>(MemoryScope::Warp)    == 0x10,
    "FIXY-FOUND-046: MemoryScope::Warp drifted from 0x10 (GPU trunk).");
static_assert(static_cast<std::uint8_t>(MemoryScope::Cta)     == 0x11,
    "FIXY-FOUND-046: MemoryScope::Cta drifted from 0x11.");
static_assert(static_cast<std::uint8_t>(MemoryScope::Cluster) == 0x12,
    "FIXY-FOUND-046: MemoryScope::Cluster drifted from 0x12.");
static_assert(static_cast<std::uint8_t>(MemoryScope::Gpu)     == 0x13,
    "FIXY-FOUND-046: MemoryScope::Gpu drifted from 0x13.");
static_assert(static_cast<std::uint8_t>(MemoryScope::Inner)   == 0x20,
    "FIXY-FOUND-046: MemoryScope::Inner drifted from 0x20 (ARM-host trunk).");
static_assert(static_cast<std::uint8_t>(MemoryScope::Outer)   == 0x21,
    "FIXY-FOUND-046: MemoryScope::Outer drifted from 0x21.");
static_assert(static_cast<std::uint8_t>(MemoryScope::System)  == 0xFF,
    "FIXY-FOUND-046: MemoryScope::System drifted from 0xFF (⊤ sentinel).");

// ── (8) SimdIsa — 14 enumerators, trunk-packed layout ──────────────
//
// HIGH NIBBLE: 0x0 = Scalar, 0x1 = x86, 0x2 = ARM, 0xF = Portable.
// LOW NIBBLE: within-trunk rank.  Wire format — drift requires
// major-version migration.
static_assert(std::is_same_v<std::underlying_type_t<SimdIsa>,
                             std::uint8_t>,
    "FIXY-FOUND-046: SimdIsa underlying type drifted from uint8_t.");
static_assert(simd_isa_count == 15,
    "FIXY-FOUND-046: SimdIsa cardinality drifted from 15.");
static_assert(static_cast<std::uint8_t>(SimdIsa::Scalar)         == 0x00,
    "FIXY-FOUND-046: SimdIsa::Scalar drifted from 0x00 (⊥ sentinel).");
static_assert(static_cast<std::uint8_t>(SimdIsa::Sse2)           == 0x10,
    "FIXY-FOUND-046: SimdIsa::Sse2 drifted from 0x10 (x86 trunk).");
static_assert(static_cast<std::uint8_t>(SimdIsa::Sse3)           == 0x11,
    "FIXY-FOUND-046: SimdIsa::Sse3 drifted from 0x11.");
static_assert(static_cast<std::uint8_t>(SimdIsa::Ssse3)          == 0x12,
    "FIXY-FOUND-046: SimdIsa::Ssse3 drifted from 0x12.");
static_assert(static_cast<std::uint8_t>(SimdIsa::Sse41)          == 0x13,
    "FIXY-FOUND-046: SimdIsa::Sse41 drifted from 0x13.");
static_assert(static_cast<std::uint8_t>(SimdIsa::Sse42)          == 0x14,
    "FIXY-FOUND-046: SimdIsa::Sse42 drifted from 0x14.");
static_assert(static_cast<std::uint8_t>(SimdIsa::Avx2)           == 0x15,
    "FIXY-FOUND-046: SimdIsa::Avx2 drifted from 0x15.");
static_assert(static_cast<std::uint8_t>(SimdIsa::Avx512F)        == 0x16,
    "FIXY-FOUND-046: SimdIsa::Avx512F drifted from 0x16.");
static_assert(static_cast<std::uint8_t>(SimdIsa::Avx512Bw)       == 0x17,
    "FIXY-FOUND-046: SimdIsa::Avx512Bw drifted from 0x17.");
static_assert(static_cast<std::uint8_t>(SimdIsa::Neon)           == 0x20,
    "FIXY-FOUND-046: SimdIsa::Neon drifted from 0x20 (ARM trunk).");
static_assert(static_cast<std::uint8_t>(SimdIsa::NeonFp16)       == 0x21,
    "FIXY-FOUND-046: SimdIsa::NeonFp16 drifted from 0x21.");
static_assert(static_cast<std::uint8_t>(SimdIsa::NeonDotProduct) == 0x22,
    "FIXY-FOUND-046: SimdIsa::NeonDotProduct drifted from 0x22.");
static_assert(static_cast<std::uint8_t>(SimdIsa::Sve)            == 0x23,
    "FIXY-FOUND-046: SimdIsa::Sve drifted from 0x23.");
static_assert(static_cast<std::uint8_t>(SimdIsa::Sve2)           == 0x24,
    "FIXY-FOUND-046: SimdIsa::Sve2 drifted from 0x24.");
static_assert(static_cast<std::uint8_t>(SimdIsa::Portable)       == 0xFF,
    "FIXY-FOUND-046: SimdIsa::Portable drifted from 0xFF (⊤ sentinel).");

// ── (9) ResidencyHeatTag — 3 tiers, trivial layout ─────────────────
static_assert(std::is_same_v<std::underlying_type_t<ResidencyHeatTag>,
                             std::uint8_t>,
    "FIXY-FOUND-046: ResidencyHeatTag underlying type drifted from uint8_t.");
static_assert(residency_heat_tag_count == 3,
    "FIXY-FOUND-046: ResidencyHeatTag cardinality drifted from 3.");
static_assert(static_cast<std::uint8_t>(ResidencyHeatTag::Cold) == 0,
    "FIXY-FOUND-046: ResidencyHeatTag::Cold drifted.");
static_assert(static_cast<std::uint8_t>(ResidencyHeatTag::Warm) == 1,
    "FIXY-FOUND-046: ResidencyHeatTag::Warm drifted.");
static_assert(static_cast<std::uint8_t>(ResidencyHeatTag::Hot)  == 2,
    "FIXY-FOUND-046: ResidencyHeatTag::Hot drifted.");

// ── (10) CipherTierTag — 3 tiers, trivial layout ───────────────────
static_assert(std::is_same_v<std::underlying_type_t<CipherTierTag>,
                             std::uint8_t>,
    "FIXY-FOUND-046: CipherTierTag underlying type drifted from uint8_t.");
static_assert(cipher_tier_tag_count == 3,
    "FIXY-FOUND-046: CipherTierTag cardinality drifted from 3.");
static_assert(static_cast<std::uint8_t>(CipherTierTag::Cold) == 0,
    "FIXY-FOUND-046: CipherTierTag::Cold drifted.");
static_assert(static_cast<std::uint8_t>(CipherTierTag::Warm) == 1,
    "FIXY-FOUND-046: CipherTierTag::Warm drifted.");
static_assert(static_cast<std::uint8_t>(CipherTierTag::Hot)  == 2,
    "FIXY-FOUND-046: CipherTierTag::Hot drifted.");

// ── (11) AllocClassTag — 6 tiers, trivial layout ───────────────────
static_assert(std::is_same_v<std::underlying_type_t<AllocClassTag>,
                             std::uint8_t>,
    "FIXY-FOUND-046: AllocClassTag underlying type drifted from uint8_t.");
static_assert(alloc_class_tag_count == 6,
    "FIXY-FOUND-046: AllocClassTag cardinality drifted from 6.");
static_assert(static_cast<std::uint8_t>(AllocClassTag::HugePage) == 0,
    "FIXY-FOUND-046: AllocClassTag::HugePage drifted.");
static_assert(static_cast<std::uint8_t>(AllocClassTag::Mmap)     == 1,
    "FIXY-FOUND-046: AllocClassTag::Mmap drifted.");
static_assert(static_cast<std::uint8_t>(AllocClassTag::Heap)     == 2,
    "FIXY-FOUND-046: AllocClassTag::Heap drifted.");
static_assert(static_cast<std::uint8_t>(AllocClassTag::Arena)    == 3,
    "FIXY-FOUND-046: AllocClassTag::Arena drifted.");
static_assert(static_cast<std::uint8_t>(AllocClassTag::Pool)     == 4,
    "FIXY-FOUND-046: AllocClassTag::Pool drifted.");
static_assert(static_cast<std::uint8_t>(AllocClassTag::Stack)    == 5,
    "FIXY-FOUND-046: AllocClassTag::Stack drifted.");

// ── (12) WaitStrategy — 6 tiers, trivial layout ────────────────────
static_assert(std::is_same_v<std::underlying_type_t<WaitStrategy>,
                             std::uint8_t>,
    "FIXY-FOUND-046: WaitStrategy underlying type drifted from uint8_t.");
static_assert(wait_strategy_count == 6,
    "FIXY-FOUND-046: WaitStrategy cardinality drifted from 6.");
static_assert(static_cast<std::uint8_t>(WaitStrategy::Block)       == 0,
    "FIXY-FOUND-046: WaitStrategy::Block drifted.");
static_assert(static_cast<std::uint8_t>(WaitStrategy::Park)        == 1,
    "FIXY-FOUND-046: WaitStrategy::Park drifted.");
static_assert(static_cast<std::uint8_t>(WaitStrategy::AcquireWait) == 2,
    "FIXY-FOUND-046: WaitStrategy::AcquireWait drifted.");
static_assert(static_cast<std::uint8_t>(WaitStrategy::UmwaitC01)   == 3,
    "FIXY-FOUND-046: WaitStrategy::UmwaitC01 drifted.");
static_assert(static_cast<std::uint8_t>(WaitStrategy::BoundedSpin) == 4,
    "FIXY-FOUND-046: WaitStrategy::BoundedSpin drifted.");
static_assert(static_cast<std::uint8_t>(WaitStrategy::SpinPause)   == 5,
    "FIXY-FOUND-046: WaitStrategy::SpinPause drifted.");

// ── (13) MemOrderTag — 5 tiers, trivial layout ─────────────────────
static_assert(std::is_same_v<std::underlying_type_t<MemOrderTag>,
                             std::uint8_t>,
    "FIXY-FOUND-046: MemOrderTag underlying type drifted from uint8_t.");
static_assert(mem_order_tag_count == 5,
    "FIXY-FOUND-046: MemOrderTag cardinality drifted from 5.");
static_assert(static_cast<std::uint8_t>(MemOrderTag::SeqCst)  == 0,
    "FIXY-FOUND-046: MemOrderTag::SeqCst drifted.");
static_assert(static_cast<std::uint8_t>(MemOrderTag::AcqRel)  == 1,
    "FIXY-FOUND-046: MemOrderTag::AcqRel drifted.");
static_assert(static_cast<std::uint8_t>(MemOrderTag::Release) == 2,
    "FIXY-FOUND-046: MemOrderTag::Release drifted.");
static_assert(static_cast<std::uint8_t>(MemOrderTag::Acquire) == 3,
    "FIXY-FOUND-046: MemOrderTag::Acquire drifted.");
static_assert(static_cast<std::uint8_t>(MemOrderTag::Relaxed) == 4,
    "FIXY-FOUND-046: MemOrderTag::Relaxed drifted.");

// ── (14) ProgressClass — 4 tiers, trivial layout ───────────────────
static_assert(std::is_same_v<std::underlying_type_t<ProgressClass>,
                             std::uint8_t>,
    "FIXY-FOUND-046: ProgressClass underlying type drifted from uint8_t.");
static_assert(progress_class_count == 4,
    "FIXY-FOUND-046: ProgressClass cardinality drifted from 4.");
static_assert(static_cast<std::uint8_t>(ProgressClass::MayDiverge)  == 0,
    "FIXY-FOUND-046: ProgressClass::MayDiverge drifted.");
static_assert(static_cast<std::uint8_t>(ProgressClass::Terminating) == 1,
    "FIXY-FOUND-046: ProgressClass::Terminating drifted.");
static_assert(static_cast<std::uint8_t>(ProgressClass::Productive)  == 2,
    "FIXY-FOUND-046: ProgressClass::Productive drifted.");
static_assert(static_cast<std::uint8_t>(ProgressClass::Bounded)     == 3,
    "FIXY-FOUND-046: ProgressClass::Bounded drifted.");

// ── (15) CrashClass — 4 tiers, trivial layout ──────────────────────
static_assert(std::is_same_v<std::underlying_type_t<CrashClass>,
                             std::uint8_t>,
    "FIXY-FOUND-046: CrashClass underlying type drifted from uint8_t.");
static_assert(crash_class_count == 4,
    "FIXY-FOUND-046: CrashClass cardinality drifted from 4.");
static_assert(static_cast<std::uint8_t>(CrashClass::Abort)       == 0,
    "FIXY-FOUND-046: CrashClass::Abort drifted.");
static_assert(static_cast<std::uint8_t>(CrashClass::Throw)       == 1,
    "FIXY-FOUND-046: CrashClass::Throw drifted.");
static_assert(static_cast<std::uint8_t>(CrashClass::ErrorReturn) == 2,
    "FIXY-FOUND-046: CrashClass::ErrorReturn drifted.");
static_assert(static_cast<std::uint8_t>(CrashClass::NoThrow)     == 3,
    "FIXY-FOUND-046: CrashClass::NoThrow drifted.");

// ── (16) SuspendBehavior — 3 tiers, trivial layout ─────────────────
static_assert(std::is_same_v<std::underlying_type_t<SuspendBehavior>,
                             std::uint8_t>,
    "FIXY-FOUND-046: SuspendBehavior underlying type drifted from uint8_t.");
static_assert(suspend_behavior_count == 3,
    "FIXY-FOUND-046: SuspendBehavior cardinality drifted from 3.");
static_assert(static_cast<std::uint8_t>(SuspendBehavior::Unknown)         == 0,
    "FIXY-FOUND-046: SuspendBehavior::Unknown drifted.");
static_assert(static_cast<std::uint8_t>(SuspendBehavior::PausesOnSuspend) == 1,
    "FIXY-FOUND-046: SuspendBehavior::PausesOnSuspend drifted.");
static_assert(static_cast<std::uint8_t>(SuspendBehavior::KeepsTicking)    == 2,
    "FIXY-FOUND-046: SuspendBehavior::KeepsTicking drifted.");

// ── (17) JoinPolicy — 6 tiers, trivial layout ──────────────────────
static_assert(std::is_same_v<std::underlying_type_t<JoinPolicy>,
                             std::uint8_t>,
    "FIXY-FOUND-046: JoinPolicy underlying type drifted from uint8_t.");
static_assert(static_cast<std::uint8_t>(JoinPolicy::FORGET)        == 0,
    "FIXY-FOUND-046: JoinPolicy::FORGET drifted.");
static_assert(static_cast<std::uint8_t>(JoinPolicy::DETACH)        == 1,
    "FIXY-FOUND-046: JoinPolicy::DETACH drifted.");
static_assert(static_cast<std::uint8_t>(JoinPolicy::ABANDON)       == 2,
    "FIXY-FOUND-046: JoinPolicy::ABANDON drifted.");
static_assert(static_cast<std::uint8_t>(JoinPolicy::CANCEL)        == 3,
    "FIXY-FOUND-046: JoinPolicy::CANCEL drifted.");
static_assert(static_cast<std::uint8_t>(JoinPolicy::WAIT_DEADLINE) == 4,
    "FIXY-FOUND-046: JoinPolicy::WAIT_DEADLINE drifted.");
static_assert(static_cast<std::uint8_t>(JoinPolicy::JOIN_ALL)      == 5,
    "FIXY-FOUND-046: JoinPolicy::JOIN_ALL drifted.");

// ── (18) Consistency — 5 tiers, trivial layout ─────────────────────
static_assert(std::is_same_v<std::underlying_type_t<Consistency>,
                             std::uint8_t>,
    "FIXY-FOUND-046: Consistency underlying type drifted from uint8_t.");
static_assert(consistency_count == 5,
    "FIXY-FOUND-046: Consistency cardinality drifted from 5.");
static_assert(static_cast<std::uint8_t>(Consistency::EVENTUAL)          == 0,
    "FIXY-FOUND-046: Consistency::EVENTUAL drifted.");
static_assert(static_cast<std::uint8_t>(Consistency::READ_YOUR_WRITES)  == 1,
    "FIXY-FOUND-046: Consistency::READ_YOUR_WRITES drifted.");
static_assert(static_cast<std::uint8_t>(Consistency::CAUSAL_PREFIX)     == 2,
    "FIXY-FOUND-046: Consistency::CAUSAL_PREFIX drifted.");
static_assert(static_cast<std::uint8_t>(Consistency::BOUNDED_STALENESS) == 3,
    "FIXY-FOUND-046: Consistency::BOUNDED_STALENESS drifted.");
static_assert(static_cast<std::uint8_t>(Consistency::STRONG)            == 4,
    "FIXY-FOUND-046: Consistency::STRONG drifted.");

// ── (19) Witness — 4 tiers, trivial layout ─────────────────────────
static_assert(std::is_same_v<std::underlying_type_t<Witness>,
                             std::uint8_t>,
    "FIXY-FOUND-046: Witness underlying type drifted from uint8_t.");
static_assert(static_cast<std::uint8_t>(Witness::UNWITNESSED)       == 0,
    "FIXY-FOUND-046: Witness::UNWITNESSED drifted.");
static_assert(static_cast<std::uint8_t>(Witness::TYPE_CHECKED)      == 1,
    "FIXY-FOUND-046: Witness::TYPE_CHECKED drifted.");
static_assert(static_cast<std::uint8_t>(Witness::TEST_PASSED)       == 2,
    "FIXY-FOUND-046: Witness::TEST_PASSED drifted.");
static_assert(static_cast<std::uint8_t>(Witness::FORMALLY_VERIFIED) == 3,
    "FIXY-FOUND-046: Witness::FORMALLY_VERIFIED drifted.");

// ── (20) ClockSource — 10 tiers, trivial layout ────────────────────
static_assert(std::is_same_v<std::underlying_type_t<ClockSource>,
                             std::uint8_t>,
    "FIXY-FOUND-046: ClockSource underlying type drifted from uint8_t.");
static_assert(static_cast<std::uint8_t>(ClockSource::Realtime)      == 0,
    "FIXY-FOUND-046: ClockSource::Realtime drifted.");
static_assert(static_cast<std::uint8_t>(ClockSource::Monotonic)     == 1,
    "FIXY-FOUND-046: ClockSource::Monotonic drifted.");
static_assert(static_cast<std::uint8_t>(ClockSource::MonotonicRaw)  == 2,
    "FIXY-FOUND-046: ClockSource::MonotonicRaw drifted.");
static_assert(static_cast<std::uint8_t>(ClockSource::Boot)          == 3,
    "FIXY-FOUND-046: ClockSource::Boot drifted.");
static_assert(static_cast<std::uint8_t>(ClockSource::ThreadCpu)     == 4,
    "FIXY-FOUND-046: ClockSource::ThreadCpu drifted.");
static_assert(static_cast<std::uint8_t>(ClockSource::ProcessCpu)    == 5,
    "FIXY-FOUND-046: ClockSource::ProcessCpu drifted.");
static_assert(static_cast<std::uint8_t>(ClockSource::TscRaw)        == 6,
    "FIXY-FOUND-046: ClockSource::TscRaw drifted.");
static_assert(static_cast<std::uint8_t>(ClockSource::TscSerialized) == 7,
    "FIXY-FOUND-046: ClockSource::TscSerialized drifted.");
static_assert(static_cast<std::uint8_t>(ClockSource::PmuCounter)    == 8,
    "FIXY-FOUND-046: ClockSource::PmuCounter drifted.");
static_assert(static_cast<std::uint8_t>(ClockSource::PtpHwClock)    == 9,
    "FIXY-FOUND-046: ClockSource::PtpHwClock drifted (FIXY-V-201).");

// ── Roster cardinality pin ────────────────────────────────────────
//
// 20 lattice enums pinned in this file.  Adding a new lattice enum
// consumed by row_hash_contribution_v requires (a) adding its pin
// block above AND (b) bumping this literal.  Bumping or shrinking
// the pin block without bumping the literal fires here — defense
// against drift between the roster and the documented count.
//
// Each pin block carries: underlying-type assert + cardinality
// assert (where reflection count is available) + per-enumerator
// value assert.
inline constexpr std::size_t kFound046PinnedEnumCount = 20;
static_assert(kFound046PinnedEnumCount == 20,
    "FIXY-FOUND-046 roster cardinality pin: 20 lattice enums pinned. "
    "Adding a new lattice enum consumed by row_hash requires extending "
    "the pin block AND bumping this literal.");

}  // namespace crucible::algebra::lattices::detail::found_046_enum_value_pins
