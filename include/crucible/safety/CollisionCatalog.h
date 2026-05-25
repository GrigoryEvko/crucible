#ifndef CRUCIBLE_SAFETY_FN_COLLISION_CATALOG_INTEGRATION
#include <crucible/safety/Fn.h>
#else
#ifndef CRUCIBLE_SAFETY_COLLISION_CATALOG_BODY
#define CRUCIBLE_SAFETY_COLLISION_CATALOG_BODY

// ── crucible::safety — CollisionCatalog.h (GAPS-005..018) ───────────
//
// Compile-time collision rules for safety::fn::Fn<...>.  Fn is the
// 19-axis product surface; this catalog rejects cross-axis
// compositions that are unsound even when each axis is individually
// well-formed.
//
// The current C++ substrate does not yet carry a full Fixy body IR, so
// flow-sensitive rules are represented by explicit opt-in marker
// traits (`marks_async`, `marks_fail`, `marks_runtime_ghost_use`, ...).
// That keeps Phase 0 honest: source-visible annotations can trigger
// the rejection today, while future compiler passes can specialize the
// same traits from analyzed bodies without changing Fn's ABI.

#include <crucible/algebra/lattices/BarrierStrengthLattice.h>  // FIXY-V-260 V301 BarrierStrength tier
#include <crucible/algebra/lattices/ControlFlowLattice.h>  // FIXY-V-243 C001/L006/P003 ControlFlow tier
#include <crucible/algebra/lattices/HwInstructionLattice.h>    // FIXY-V-260 V201..V203 HwInstruction tier
#include <crucible/algebra/lattices/SimdIsaLattice.h>          // FIXY-V-260 V101 SimdIsa tier
#include <crucible/algebra/lattices/MemoryScopeLattice.h>      // FIXY-V-268 V401/V402 MemoryScope tier + trunk classifiers
#include <crucible/algebra/lattices/StdioLattice.h>        // FIXY-V-243 S001 Stdio tier
#include <crucible/algebra/lattices/WaitLattice.h>
#include <crucible/effects/EffectRow.h>
#include <crucible/safety/Borrowed.h>
#include <crucible/safety/Diagnostic.h>
#include <crucible/safety/FpMode.h>          // FIXY-V-091 F-family detectors
#include <crucible/safety/source/Arch.h>     // FIXY-V-268 V402 arch_pin_v<F::source_t> host-arch reader

#include <array>
#include <cstdint>
#include <meta>            // FIXY-FOUND-134: reflection-driven rule_bijection fold
#include <string_view>
#include <tuple>
#include <type_traits>
#include <utility>

// Forward-declaration of safety::Wait<Strategy, T> — V-081 detects this
// type pattern from F::type_t without pulling in the full safety/Wait.h
// header (which would create a header cycle with Wait → DimensionTraits
// → Witness → Fn).  Partial specialization needs only the class-template
// declaration.
namespace crucible::safety {
    template <::crucible::algebra::lattices::WaitStrategy Strategy, typename T>
    class Wait;
}

// Forward-declaration of safety::FpModePinned<auto Mode, T> — FIXY-V-091
// detects this type pattern from F::type_t for the F-family FP-mode
// cross-axis rules.  Including safety/FpMode.h here would pull in
// algebra/lattices/FpModeLattice.h and the full 11-axis composite which
// the catalog header has no other reason to depend on.  RowHashFold.h
// already uses the same forward-decl pattern (the row-hash specialization
// for FpModePinned has to fwd-decl the class for the same reason).
//
// The 11 FP-mode enum classes are forward-declared as opaque
// `enum class : std::uint8_t` here; the partial spec of fp_axis_mode_of<>
// below binds to FpModePinned<Mode, U> with `AxisEnum Mode` as a
// constrained NTTP — substitution rejects when Mode's type doesn't match
// AxisEnum, so a single generic detector covers all 11 sub-axes.
namespace crucible::safety {
    enum class FpRounding         : std::uint8_t;
    enum class FpFtz              : std::uint8_t;
    enum class FpContract         : std::uint8_t;
    enum class FpTrapMask         : std::uint8_t;
    enum class FpDenormalInput    : std::uint8_t;
    enum class FpNanPolicy        : std::uint8_t;
    enum class FpInfPolicy        : std::uint8_t;
    enum class FpComplexLayout    : std::uint8_t;
    enum class FpLibmPolicy       : std::uint8_t;
    enum class FpReassociate      : std::uint8_t;
    enum class FpConstantRounding : std::uint8_t;
    template <auto Mode, typename T> class FpModePinned;
}

// Forward-declaration of the V-242 hazard-axis Graded carriers
// safety::ControlFlowPinned<Tier, T> and safety::StdioPinned<Tier, T> —
// FIXY-V-243 detects these type patterns from F::type_t for the
// ControlFlow-tier (C001/L006/P003) and Stdio-tier (S001) rules without
// pulling in the full safety/ControlFlow.h + safety/Stdio.h wrapper
// headers (which would re-enter Graded → DimensionTraits → Witness → Fn
// and create a header cycle).  The ControlFlow / Stdio enums themselves
// arrive via the lattice headers included above (the same shape as the
// WaitLattice.h include that W001 uses), so the NTTP enum types are
// complete here; only the wrapper class templates are forward-declared.
namespace crucible::safety {
    template <::crucible::algebra::lattices::ControlFlow Tier, typename T>
    class ControlFlowPinned;
    template <::crucible::algebra::lattices::Stdio Tier, typename T>
    class StdioPinned;
}

// Forward-declaration of the V-254/V-255/V-256 hardware-axis Graded
// carriers — FIXY-V-260 reads their pinned tier off F::type_t for the
// V101 (SimdIsa) / V201..V203 (HwInstruction) / V301 (BarrierStrength)
// rules without pulling in the full safety/{Hw,BarrierGuarded,
// SimdWidthPinned}.h wrapper headers (which would re-enter Graded →
// DimensionTraits → Witness → Fn and create a header cycle, the same
// reason the ControlFlowPinned / StdioPinned block above forward-decls).
// The three lattice enums arrive complete via the lattice headers
// included above; only the wrapper class templates are forward-declared.
namespace crucible::safety {
    template <::crucible::algebra::lattices::HwInstruction Tier, typename T>
    class Hw;
    template <::crucible::algebra::lattices::BarrierStrength Tier, typename T>
    class BarrierGuarded;
    template <::crucible::algebra::lattices::SimdIsa W, typename T>
    class SimdWidthPinned;
    // FIXY-V-268: the V-267 ScopedFence carrier — V401 reads its MemoryScope
    // tier off F::type_t (composed with a BarrierGuarded tier) for the
    // scope×strength sufficiency rule, V402 for the scope×arch cross-trunk
    // rule.  Forward-declared (not #include ScopedFence.h) for the same
    // header-cycle reason the Hw/BarrierGuarded/SimdWidthPinned block above
    // forward-decls; the MemoryScope enum + lattice arrive complete via the
    // MemoryScopeLattice.h include above.
    template <::crucible::algebra::lattices::MemoryScope S, typename T>
    class ScopedFence;
}

namespace crucible::safety::fn::collision {

enum class RuleCode : std::uint8_t {
    // ── 12 §6.8 rules shipped Phase 0 (GAPS-005..018) ────────────────
    I002 = 0,
    L002 = 1,
    E044 = 2,
    I003 = 3,
    M012 = 4,
    P002 = 5,
    I004 = 6,
    N002 = 7,
    L003 = 8,
    M011 = 9,
    S010 = 10,
    S011 = 11,
    // ── 8 NEW rules shipped Phase B per misc/16_05_2026_fixy.md §4 ──
    //
    // Per the Phase-B substrate-grows-by-200-LoC plan, these eight
    // rules live alongside the §6.8 rules in the substrate catalog so
    // that direct `safety::Fn<...>` instantiation is gated identically
    // to the `fixy::fn<...>` aggregator (no fixy-only rule machinery).
    // `fixy::rule::R013..R020` are one-line aliases over them (shipped
    // in fixy/Rules.h, Phase B).
    L004 = 12,  // Linear x lifetime_region without Permission token
    B001 = 13,  // Bg observable surface without bounded-resource decl
    H001 = 14,  // HotPath with Unstated or Unbounded Cost
    H002 = 15,  // HotPath without refinement witness floor
    L005 = 16,  // Linear aliasing — two Linears sharing one region tag
    F001 = 17,  // Frame manifesto: axis values disagree across pack
    H003 = 18,  // HotPath row contains Alloc or IO without budget
    F002 = 19,  // Federation peer without terminating budget
    // ── FIXY-V-081 Phase C/W-family (Wait-strategy cross-axis) ─────────
    //
    // W-family rules guard the Synchronization axis (DimensionAxis::20)
    // against compositions that violate latency or scheduling discipline.
    // W001 is the first such rule: HotPath × syscall-tier Wait rejects
    // functions marked hot-path that wrap their return/parameter type
    // in any kernel-crossing Wait strategy — {Block, Park, AcquireWait,
    // UmwaitC01}.  All four involve a syscall or kernel-mediated
    // transition with latency 100 ns – 20 µs, incompatible with the
    // hot-path budget (≤ 40 ns intra-socket per CLAUDE.md §IX latency
    // hierarchy).  Only BoundedSpin and SpinPause are admissible.
    // FIXY-FOUND-061 widened the rejected set from {Block, Park} to all
    // four syscall-tier strategies; the diagnostic message and
    // remediation text already named the wider set.
    W001 = 20,  // HotPath × Wait<{Block,Park,AcquireWait,UmwaitC01}> (syscall-tier blocker on hot path)
    // ── FIXY-V-082 Phase C/W-family — Bg × active-spin Wait ───────────
    //
    // W002 is the dual of W001 on the Synchronization axis: a Bg-context
    // function MUST NOT wrap its return/parameter type in an active-spin
    // Wait strategy.  SpinPause (pure `_mm_pause` loop, never blocks) and
    // BoundedSpin (deadline-bounded spin) both monopolize the hosting
    // core for the duration of the wait.  Bg threads are by contract
    // permitted to block — they SHOULD use Park/Block/AcquireWait/Umwait
    // so the scheduler can do useful work elsewhere.  Active-spin in a
    // Bg row is the back-pressure-trap shape B001 catches one axis over.
    W002 = 21,  // Bg-row × Wait<SpinPause> or Wait<BoundedSpin> (active-spin in cold thread)
    // ── FIXY-V-091 F-family — FP-mode cross-axis rules ─────────────────
    //
    // V-090 ships 11 per-axis FP-mode wrappers (FpReassociatePinned,
    // FpContractPinned, FpFtzPinned, FpDenormalInputPinned, ...).  Each
    // pins a sub-axis at the type level.  The F-family rules guard
    // cross-axis compositions where pinning one FP sub-axis defeats a
    // load-bearing property on a DIFFERENT Fn axis:
    //
    //   F101: marks_replay_required × FpReassociate<non-strict>
    //         FOUND-074: F101 rejects both UnrestrictedRewrite AND
    //         BoundedTreeDepth.  UnrestrictedRewrite reorders FP additions
    //         freely (compiler-scheduler-dependent).  BoundedTreeDepth
    //         pins a log-N tree DEPTH but lets vendors pick the per-level
    //         LANE ASSIGNMENT (NVIDIA warp-shuffle, AMD wavefront, Intel
    //         SVE differ on operand ordering within the same tree shape);
    //         the bit pattern diverges across the cross-vendor numerics
    //         CI matrix.  Bit-exact replay requires Forbidden — the IEEE
    //         754 default, the ONLY setting compatible with replay.
    //
    //   F102: marks_replay_required × FpContract<Fast>
    //         `-ffp-contract=fast` allows cross-statement FMA folding.
    //         NVIDIA SASS / AMD CDNA / Intel SPR contract at different
    //         expression boundaries; same source → different bits.
    //
    //   F103: has_ct × FpReassociate<UnrestrictedRewrite>
    //         Reassociation introduces data-dependent reduction-tree
    //         topology (the compiler may pick different orderings
    //         based on operand magnitudes), violating CT timing
    //         independence.
    //
    //   F104: has_ct × FpDenormalInput<HonorDenormals>
    //         DAZ=0 on x86 introduces a 30-100× slowdown when the
    //         input IS denormal — textbook FP timing side-channel.
    //         Crypto and CT paths PIN DAZ=1 (DenormalsAreZero) to make
    //         denormal-vs-normal-input timing identical.
    //
    //   F105: has_ct × FpFtz<PreserveSubnormals>
    //         FTZ=0 introduces the same 30-100× slowdown PRODUCING
    //         denormal outputs (output side; F104 is input side).  CT
    //         paths PIN FlushToZero so the result-magnitude doesn't
    //         leak through cycle count.
    F101 = 22,  // Replay-required × FpReassociate non-strict (Unrestricted OR BoundedTreeDepth)  FOUND-074
    F102 = 23,  // Replay-required × FpContract<Fast>
    F103 = 24,  // CT × FpReassociate<UnrestrictedRewrite>
    F104 = 25,  // CT × FpDenormalInput<HonorDenormals>
    F105 = 26,  // CT × FpFtz<PreserveSubnormals>
    // ── FIXY-V-234 M-family — mmap-syscall-surface cross-axis rules ───
    //
    // M001 names the collision class Agent 9 Bug 5 surfaced on the
    // SenseHub MAP_SHARED reader: a future "release pressure on cold
    // gauges" change that called `madvise(MADV_DONTNEED)` on the same
    // region a concurrent `__atomic_load_n` reader was sampling would
    // zero the pages mid-load, returning a bogus counter value.  The
    // rule pins the disjoint-routing discipline V-225 + V-234 shipped:
    //
    //   safe surface  (`fixy::mmap::advise<Advice>`)
    //                  refuses `is_dangerous_advice_v<Advice>` Advice;
    //                  callers needing DontNeed/Free MUST switch to
    //                  the release-aware surface.
    //
    //   release-aware (`fixy::mmap::advise_release_aware<Advice,
    //                                                    RegionTag>`)
    //                  takes a `Permission<RegionTag> const&` borrow
    //                  proof so the type system witnesses unique-
    //                  exclusive-access; combined with
    //                  `SharedPermissionPool<RegionTag>::try_upgrade()`
    //                  the runtime state machine guarantees no live
    //                  shared reader.
    //
    // Routing collision — `advise<DontNeed>` on the safe surface — is
    // rejected by `CtxFitsSafeAdvise` (one half of M001).  Calling
    // `advise_release_aware<HugePage>` (non-dangerous advice on the
    // release-aware surface) is rejected by `CtxFitsReleaseAwareAdvise`
    // (the other half).  Together: dangerous Advice flows EXCLUSIVELY
    // through the Permission-witnessed surface.
    //
    // FIXY-FOUND-013 audit: M001's enforcement model is CONCEPT-GATED at
    // the production-callsite of `fixy::wrap::mmap::advise<>` and
    // `fixy::wrap::mmap::advise_release_aware<>` (Mmap.h ~L721 / ~L771),
    // NOT a `CollisionRules<Fn>::validate()` pack walk like the W-family.
    // Both polarities are pinned at fixy/Mmap.h:903-910 (substrate side)
    // AND test/test_fixy_v_234_release_aware_permission.cpp (catalog
    // side, 4 static_asserts) so renaming/removing either concept reds
    // both directions.  Negative fixtures:
    //   test/fixy_neg/neg_fixy_v_234_advise_release_aware_missing_permission.cpp
    //   test/fixy_neg/neg_fixy_v_234_advise_release_aware_cross_tag_permission.cpp
    M001 = 27,  // mmap.advise<DontNeed> without release_aware<RegionTag>
    // ── FIXY-V-243 hazard-axis cross-axis rules (Agent 10 §4) ──────────
    //
    // V-242 ships 5 hazard-axis Graded carriers (ControlFlowPinned,
    // CallShapePinned, StackUsePinned, GlobalStatePinned, StdioPinned).
    // These eight rules gate cross-axis compositions where a hazard
    // declaration is inconsistent with another Fn axis.  Per the catalog
    // decoupling discipline (W-family / F-family precedent), each rule
    // reads either a SHIPPED V-242 wrapper tier off `F::type_t` (C001 /
    // L006 / P003 / S001 — testable today) or an opt-in marker trait that
    // the V-244/V-245/V-246 grant headers will specialize once they land
    // (D001 / D002 / G001 / S004 — default-SAFE until a grant opts in).
    //
    //   C001: marks_aborts × ControlFlow tier < AbortOnly.  A function
    //         declaring it may abort (grant::ctrl::abort<Rationale>, V-244)
    //         MUST carry a ControlFlow tier ≥ AbortOnly — the type-level
    //         witness of the escape.  Claiming abort while typed Pure is
    //         the ControlFlow↔escape inconsistency Agent 10 §4 names.
    //   D001: marks_indirect_call_not_noexcept.  An indirect-call grant
    //         (grant::dispatch::indirect_call<Family>, V-245) whose RunFn
    //         type is NOT noexcept reds — closes Scenario A
    //         (BackgroundThread::RegionReadyCallback::Fn missing noexcept).
    //   D002: marks_recurses_unbounded.  grant::dispatch::recurses<> (V-245)
    //         without an NTTP MaxDepth reds — the implicit-recursion-bound
    //         anti-pattern.
    //   G001: marks_thread_local_untagged.  grant::global::thread_local_<>
    //         (V-246) without a TLSTag NTTP reds.
    //   L006: Usage::Linear × ControlFlow tier ≥ MayLongjmp (or the
    //         marks_longjmp_unsafe marker).  longjmp SKIPS destructors —
    //         a Linear resource in scope would leak / dangle across the
    //         non-local jump.  The TYPE-LEVEL companion to the C++ rule
    //         that already rejects goto across a destructor scope.
    //   P003: marks_fork_worker × ControlFlow tier ≥ ThrowOnly (or the
    //         marks_throws marker).  A throwing permission_fork worker
    //         body terminates the hosting jthread (no exception crosses
    //         the thread boundary; -fno-exceptions makes throw == abort).
    //         The catalog-level codification of the V-087 fork-body
    //         static_assert.
    //   S001: marks_hot_path × Stdio tier ≥ BufferedWrite.  A hot-path
    //         function (TraceRing / Arena / KernelCache) MUST NOT do stdio
    //         — format parsing ≥ 100 ns, output syscalls flush buffers
    //         (CLAUDE.md §XII).
    //   S004: marks_singleton_init_cycle.  The V-248 tag-graph closure
    //         walk over registered Meyers-singleton tags detects a cycle
    //         at consteval and flags the participating Fn.  The reusable
    //         cycle detector ships here as pack::singleton_init_acyclic<>.
    //   G002: marks_thread_local_atomic.  A grant::global::thread_local_<Tag>
    //         (V-246) PAIRED with an atomic memory-order wrapper
    //         (safety::MemOrder<*, std::atomic<T>>) is nonsensical: an atomic
    //         op on a per-thread object orders against no peer (one instance
    //         per thread).  Either the atomic is redundant (misleading) or
    //         the thread_local was a typo for process-wide `static`.  Closes
    //         Scenario E (bench_smoke.cpp:78 thread_local std::atomic counter).
    C001 = 28,  // marks_aborts × ControlFlow tier < AbortOnly (escape unwitnessed)
    D001 = 29,  // indirect_call grant with non-noexcept callable
    D002 = 30,  // recurses grant without a bounded MaxDepth
    G001 = 31,  // thread_local_ grant without a TLSTag
    L006 = 32,  // Linear × ControlFlow tier ≥ MayLongjmp (destructor-skipping jump)
    P003 = 33,  // fork-worker × ControlFlow tier ≥ ThrowOnly (throw terminates jthread)
    S001 = 34,  // HotPath × Stdio tier ≥ BufferedWrite (stdio on hot path)
    S004 = 35,  // Meyers-singleton init-dependency cycle
    G002 = 36,  // thread_local_ grant × atomic MemOrder (per-thread atomic nonsensical)
    // ── FIXY-V-260 V-family — hardware-axis cross-axis rules (Agent 11 §3.6) ──
    //
    // V-254/V-255/V-256 ship three hardware-band Graded carriers:
    // Hw<HwInstruction, T> (the instruction-capability ceiling),
    // BarrierGuarded<BarrierStrength, T> (the memory-fence strength), and
    // SimdWidthPinned<SimdIsa, T> (the pinned vector ISA).  V-258/V-259
    // ship the vendor::intrinsic<V, I> and simd::width<W> GRANTS.  These
    // eight rules guard cross-axis compositions where a hardware
    // declaration is unsound against another Fn axis.  Per the catalog
    // decoupling discipline (W-/F-family precedent), each rule reads
    // either a SHIPPED V-254/255/256 wrapper tier off F::type_t (V101 /
    // V201 / V202 / V203 / V301 — testable today) or an opt-in marker
    // trait that the V-258/V-259 grant-pack analysis specializes once it
    // lands (V001 / V002 / V102 — default-SAFE until a grant opts in).
    //
    // The V band is FREE (no V### existed); the eight rules re-home Agent
    // 11 §3.6's V001/V002/S001/S002/H001/H002/H003/B001 onto collision-
    // free codes because S001/H001/H002/H003/B001 are already taken by the
    // V-243 Stdio / Phase-B HotPath / Bg rules.  Number ranges encode the
    // sub-axis exactly like the F-family (Frame F0xx + Fp F1xx): vendor
    // V0xx, SimdIsa V1xx, HwInstruction V2xx, BarrierStrength V3xx.
    //
    //   V001: marks_vendor_isa_inconsistent.  The pack declares
    //         vendor::intrinsic<V, I> grants whose (V, I) disagree across
    //         bindings (one binding pins NV, another an x86 ISA family) —
    //         the grant-pack version of the per-grant
    //         vendor_isa_consistent_v<V, I> gate V-258 enforces on a
    //         SINGLE intrinsic.  Default-SAFE marker.
    //   V002: marks_vendor_cross_arch.  A single binding composes
    //         intrinsics from incompatible architecture trunks (an x86
    //         intrinsic AND an ARM intrinsic) — the catalog companion to
    //         the V-261 source::ArchPinned<Arch> cross-arch gate.  An
    //         x86+ARM kernel can never run on either; the binary would
    //         #UD on whichever ISA it lands.  Default-SAFE marker.
    //   V101: marks_replay_required × SimdWidthPinned tier ∉ {Scalar,
    //         Portable}.  A replay-required function MUST NOT pin a
    //         specific vector ISA: AVX-512 and NEON have different lane
    //         counts, so the same IR produces a different FP-reduction
    //         tree and the bit pattern diverges across the cross-vendor
    //         CI matrix (CLAUDE.md DetSafe: FP reductions are forbidden
    //         because chunked-fold reorders).  Scalar (no SIMD) and
    //         Portable (⊤, identical on every ISA) are replay-safe.
    //         Type-readable off F::type_t today.
    //   V102: marks_simd_width_exceeds_isa.  A simd::width<W> grant whose
    //         W exceeds the declared vendor ISA family's native width —
    //         the marquee "width<512> on AVX2 family" reject the V-259
    //         sentinel reserved for this rule (AVX2 tops out at 256-bit;
    //         pinning a 512-bit width on an AVX2 binding would emit
    //         instructions the target #UDs on).  Reasons about cross-grant
    //         VALUE compatibility, so it ships as a default-SAFE marker
    //         the V-258/V-259 grant-pack analysis specializes.
    //   V201: marks_hot_path × Hw tier ≥ NonDeterministicTsc.  A hot-path
    //         function (TraceRing / Arena / KernelCache) MUST NOT carry a
    //         Hw<NonDeterministicTsc> or Hw<PrivilegedMsr> tier: rdtsc /
    //         rdtscp are serializing (≈ 20-40 cycles), rdmsr / wrmsr are
    //         privileged ring-0 traps — both blow the ≤ 40 ns intra-socket
    //         hot budget (CLAUDE.md §IX).  Type-readable.
    //   V202: Hw tier == PrivilegedMsr WITHOUT effect_row ⊇ Effect::Init.
    //         rdmsr / wrmsr / IN / OUT require ring 0 and a Permission
    //         proof; the HwInstructionLattice doc pins them to the Init
    //         context (one-shot privileged setup, never the steady state).
    //         A PrivilegedMsr tier on a non-Init row is the unguarded-
    //         privilege shape.  Type + row-readable.
    //   V203: marks_replay_required × Hw tier ≥ NonDeterministicTsc.  rdtsc
    //         is hardware-dependent (different cycle base / invariant-TSC
    //         behavior on H100 vs 3090 hosts), so a replay-required body
    //         reading it diverges across reincarnation hardware — the
    //         instruction-axis dual of the F101 FP-replay rule.
    //         Type-readable.
    //   V301: marks_hot_path × BarrierStrength tier ≥ SeqCst.  A hot-path
    //         function MUST NOT carry a BarrierGuarded<SeqCst> or
    //         BarrierGuarded<FullFence> tier: a full fence (mfence /
    //         lock-prefixed) drains the store buffer (≈ 20-40+ cycles) —
    //         CLAUDE.md §IX mandates acquire/release only on the hot path
    //         (free on x86 TSO).  Type-readable.
    V001 = 37,  // vendor::intrinsic pack with inconsistent (V, I)
    V002 = 38,  // single binding composes cross-arch intrinsics (x86 + ARM)
    V101 = 39,  // replay-required × SimdWidthPinned pins a specific vector ISA
    V102 = 40,  // simd::width<W> exceeds the declared ISA family native width
    V201 = 41,  // HotPath × Hw tier ≥ NonDeterministicTsc (rdtsc / privileged)
    V202 = 42,  // Hw tier == PrivilegedMsr without an Init-context row
    V203 = 43,  // replay-required × Hw tier ≥ NonDeterministicTsc (rdtsc nondeterminism)
    V301 = 44,  // HotPath × BarrierStrength tier ≥ SeqCst (full fence on hot path)
    // ── FIXY-V-268 V4xx-family — memory-scope cross-axis rules (Agent WMEM) ──
    //
    // V-265 ships the MemoryScopeLattice (the two-trunk partial order:
    // accel Warp⊑Cta⊑Cluster⊑Gpu × ARM Inner⊑Outer, joined at Thread/⊥ and
    // System/⊤); V-267 ships the ScopedFence<Scope, T> carrier that pins a
    // publish scope at the type level.  These two rules guard cross-axis
    // compositions where a memory-scope declaration is unsound against the
    // BarrierStrength axis (V-252 / V-255 BarrierGuarded) or the host-arch
    // pin (V-261 source::ArchPinned).  The V4xx band is FREE; both rules are
    // TYPE-READABLE off F::type_t + F::source_t today (no grant needed), like
    // V101/V201/V301 — the V402 marker is the second, grant-driven path for
    // the cross-grant case a single type read cannot express.
    //
    //   V401: marks scope ⊒ Cluster × BarrierStrength ⊏ AcqRel.  A value
    //         published at cross-CTA-or-wider visibility (Cluster / Gpu /
    //         System) needs at least acquire-release ordering to make the
    //         cross-CTA / cross-cluster / cross-device writes actually
    //         visible; a Cluster/Gpu-scope publication guarded only by None /
    //         CompilerBarrier / AcquireLoad / ReleaseStore is a silent weak-
    //         memory race (the fence widens visibility but the barrier
    //         never establishes the two-sided ordering cross-CTA readers
    //         require).  FIXY-FOUND-062 widened the threshold from Gpu to
    //         Cluster — Hopper's thread-block cluster (PTX `.cluster`,
    //         distributed shared memory across up to 8 CTAs, cluster.sync)
    //         crosses CTA boundaries with the same silent-race profile as
    //         a `.gpu` publish.  scope_at_or_above_v<Cluster, type_t> reads
    //         the ScopedFence tier; barrier_at_or_above_v<AcqRel, type_t>
    //         reads the BarrierGuarded tier from the SAME composed type_t
    //         (the two detectors pierce each other).  Type-readable.
    //   V402: marks scope-trunk × host-arch CROSS-TRUNK incoherence.  An
    //         accel-trunk (GPU device) scope pinned to a CPU-host arch
    //         (source::ArchPinned<Arm> or <X86> — the fence dialect is
    //         mfence / DMB, which cannot realize a PTX `.cta`/`.gpu` scope),
    //         OR an ARM-shareability scope (Inner/Outer = DMB ISH/OSH) pinned
    //         to a non-ARM host (X86 has no ISH/OSH domain).  The mirror of
    //         V002 marks_vendor_cross_arch on the memory-scope axis.  Reads
    //         the ScopedFence trunk (V-265 mem_scope_is_accel / mem_scope_is_arm
    //         classifiers) against arch_pin_v<F::source_t> (V-261).  Type +
    //         source-readable; the marks_scope_arch_cross_trunk marker is the
    //         grant-driven path for the nested-cross-trunk-scope case a single
    //         type read cannot express (two ScopedFence layers from different
    //         trunks composed in one binding).
    V401 = 45,  // scope ⊒ Cluster × BarrierStrength ⊏ AcqRel (cross-CTA publish under-fenced)
    V402 = 46,  // scope-trunk × host-arch cross-trunk (GPU scope on CPU host / ARM scope on x86)
    // ── FIXY-FOUND-063 H-family extension — context-row contradictions ─
    //
    // H010 closes a gap among the existing H001/H002/H003 rules: those
    // catch HotPath × {Unbounded cost, trivial refinement, Alloc/IO with
    // unbounded cost}, but NONE of them catches the direct contextual
    // contradiction `HotPath × Row<Bg>`.  A Bg-row function declares
    // "I run in background context (allocations / IO / blocking allowed,
    // milliseconds latency)"; a HotPath function declares "I run on the
    // ≤40 ns intra-socket hot path".  These are mutually exclusive
    // contexts per CLAUDE.md §IX latency hierarchy.  Marking both is a
    // structural contradiction the type system must reject — closing
    // the gap left by H001/H003 (which depend on cost / Alloc/IO carriers
    // and miss HotPath × Bg × cost::Constant × no-Alloc/IO).
    H010 = 47,  // HotPath × Row<Bg> (mutually exclusive context markers)
    // ── FIXY-FOUND-064 P-family extension — Ghost erasure contract ─────
    //
    // P010 closes a gap left by P002.  P002 catches `UsageMode::Ghost ×
    // marks_runtime_ghost_use<F>::value` — a marker-driven detection
    // that fires only when downstream code SPECIALIZES the trait on
    // the offending Fn (FIXY-FOUND-067 dormant-marker family).  But a
    // Ghost binding declared with a runtime-observable effect atom in
    // its effect row (Alloc / IO / Block) is structurally a contradiction
    // independent of any marker: Ghost values are erased at codegen
    // (no emitted instructions, no register pressure, no stack
    // footprint) — yet Alloc emits heap-touching code, IO emits
    // syscall / kernel-mediated traffic, and Block emits blocking
    // primitives.  ALL THREE require emitted instructions; Ghost
    // contractually forbids them.  P010 catches the type-readable
    // case (the effect-row direct read) where P002 catches the
    // grant-driven `marks_runtime_ghost_use` case.  Structurally
    // parallel to H010 (HotPath × Bg) but on the Usage axis instead
    // of the HotPath marker — the two rules pin the same shape on
    // orthogonal axes.
    P010 = 48,  // Ghost × Row<Alloc|IO|Block> (erasure contract violation)
    // ── FIXY-FOUND-065 L-family extension — borrow × Bg-row lifetime ───
    //
    // L007 closes a gap left by L002 / L003.  L002 catches `borrow_capture
    // × marks_async`; L003 catches `borrow_capture × marks_unscoped_spawn`.
    // Both rely on a marker-trait specialization that downstream code must
    // hand-roll on the offending Fn (FIXY-FOUND-067 dormant-marker
    // family).  But a borrow_capture × Row<Bg> binding declares "this
    // function takes a borrowed reference AND runs in background-thread
    // context" — and the Bg-row carrier directly implies cross-thread
    // execution where the caller's stack may have unwound by the time
    // the background thread executes the body, leaving the borrow
    // dangling.  Structurally the same lifetime hazard L003 targets,
    // but readable from the EffectRow alone with no marker required.
    // Parallel to P010 (Ghost × Row<observable-effects>) on the Usage
    // axis but on the Borrow side of the Borrow/Ghost split.
    L007 = 49,  // borrow_capture × Row<Bg> (cross-thread borrow lifetime hazard)
    // ── FIXY-FOUND-070: Trust-axis rule (catalog grown 50 → 51) ───────
    //
    // Trust grades the binding's provenance: Verified > Tested > Unverified
    // (FOUND-034 set the default to Unverified, the Biba-safe bottom).  Before
    // FOUND-070 the Trust axis was destructured by Fn but read by ZERO §6.8
    // collision rules — every binding could declare any UsageMode regardless
    // of provenance, including UsageMode::Capability which mints a non-
    // revocable authorization token.  A Capability from an Unverified call
    // path is the canonical privilege-escalation shape: untrusted code mints
    // an authority token that downstream consumers treat as legitimate.
    // T001 closes the gap structurally — any Capability binding must declare
    // its provenance Verified or Tested.
    T001 = 50,  // Trust::Unverified × UsageMode::Capability (capability minted from untrusted provenance)
    None = 255,
};

struct I002_ClassifiedFailPayload : diag::tag_base {
    static constexpr std::string_view name = "I002_ClassifiedFailPayload";
};
struct L002_BorrowAsync : diag::tag_base {
    static constexpr std::string_view name = "L002_BorrowAsync";
};
struct E044_ConstantTimeAsync : diag::tag_base {
    static constexpr std::string_view name = "E044_ConstantTimeAsync";
};
struct I003_ConstantTimeFailOnSecret : diag::tag_base {
    static constexpr std::string_view name = "I003_ConstantTimeFailOnSecret";
};
struct M012_MonotonicConcurrentNoAtomic : diag::tag_base {
    static constexpr std::string_view name = "M012_MonotonicConcurrentNoAtomic";
};
struct P002_GhostRuntimeUse : diag::tag_base {
    static constexpr std::string_view name = "P002_GhostRuntimeUse";
};
struct I004_ClassifiedAsyncSession : diag::tag_base {
    static constexpr std::string_view name = "I004_ClassifiedAsyncSession";
};
struct N002_DecimalOverflowWrap : diag::tag_base {
    static constexpr std::string_view name = "N002_DecimalOverflowWrap";
};
struct L003_BorrowUnscopedSpawn : diag::tag_base {
    static constexpr std::string_view name = "L003_BorrowUnscopedSpawn";
};
struct M011_LinearFailNoCleanup : diag::tag_base {
    static constexpr std::string_view name = "M011_LinearFailNoCleanup";
};
struct S010_StalenessConstantTime : diag::tag_base {
    static constexpr std::string_view name = "S010_StalenessConstantTime";
};
struct S011_CapabilityReplay : diag::tag_base {
    static constexpr std::string_view name = "S011_CapabilityReplay";
};

// ── 8 NEW rule tags (Phase B per misc/16_05_2026_fixy.md §4) ────────

struct L004_LinearLifetimeNeedsPermission : diag::tag_base {
    static constexpr std::string_view name = "L004_LinearLifetimeNeedsPermission";
};
struct B001_BgObservableBoundedResource : diag::tag_base {
    static constexpr std::string_view name = "B001_BgObservableBoundedResource";
};
struct H001_HotPathBoundedCost : diag::tag_base {
    static constexpr std::string_view name = "H001_HotPathBoundedCost";
};
struct H002_HotPathWitnessFloor : diag::tag_base {
    static constexpr std::string_view name = "H002_HotPathWitnessFloor";
};
struct L005_LinearAliasSameRegionTag : diag::tag_base {
    static constexpr std::string_view name = "L005_LinearAliasSameRegionTag";
};
struct F001_FrameDeclaresAxisCollision : diag::tag_base {
    static constexpr std::string_view name = "F001_FrameDeclaresAxisCollision";
};
struct H003_HotPathTerminatingAllocIo : diag::tag_base {
    static constexpr std::string_view name = "H003_HotPathTerminatingAllocIo";
};
struct F002_FederationPeerTerminatingBudget : diag::tag_base {
    static constexpr std::string_view name = "F002_FederationPeerTerminatingBudget";
};

// ── FIXY-V-081 W-family (Wait-strategy cross-axis) ──────────────────
struct W001_HotPathWaitParkOrBlocker : diag::tag_base {
    static constexpr std::string_view name = "W001_HotPathWaitParkOrBlocker";
};

// ── FIXY-V-082 W-family (Wait-strategy × Bg cold-thread axis) ──────
struct W002_BgWaitActiveSpin : diag::tag_base {
    static constexpr std::string_view name = "W002_BgWaitActiveSpin";
};

// ── FIXY-V-091 F-family (FP-mode cross-axis) ────────────────────────
struct F101_ReplayFpReassocPermitted : diag::tag_base {
    static constexpr std::string_view name = "F101_ReplayFpReassocPermitted";
};
struct F102_ReplayFpContractFast : diag::tag_base {
    static constexpr std::string_view name = "F102_ReplayFpContractFast";
};
struct F103_CtFpReassocPermitted : diag::tag_base {
    static constexpr std::string_view name = "F103_CtFpReassocPermitted";
};
struct F104_CtFpDenormalInputHonored : diag::tag_base {
    static constexpr std::string_view name = "F104_CtFpDenormalInputHonored";
};
struct F105_CtFpFtzPreserved : diag::tag_base {
    static constexpr std::string_view name = "F105_CtFpFtzPreserved";
};

// ── FIXY-V-234 M-family (mmap-syscall-surface cross-axis) ──────────
struct M001_DontNeedRequiresReleaseAware : diag::tag_base {
    static constexpr std::string_view name = "M001_DontNeedRequiresReleaseAware";
};

// ── FIXY-V-243 hazard-axis cross-axis rule tags (Agent 10 §4) ──────
struct C001_AbortRequiresControlFlowWitness : diag::tag_base {
    static constexpr std::string_view name = "C001_AbortRequiresControlFlowWitness";
};
struct D001_IndirectCallNoexcept : diag::tag_base {
    static constexpr std::string_view name = "D001_IndirectCallNoexcept";
};
struct D002_RecursionRequiresBound : diag::tag_base {
    static constexpr std::string_view name = "D002_RecursionRequiresBound";
};
struct G001_ThreadLocalNeedsTag : diag::tag_base {
    static constexpr std::string_view name = "G001_ThreadLocalNeedsTag";
};
struct L006_NoLongjmpAcrossLinear : diag::tag_base {
    static constexpr std::string_view name = "L006_NoLongjmpAcrossLinear";
};
struct P003_ForkBodyNoThrows : diag::tag_base {
    static constexpr std::string_view name = "P003_ForkBodyNoThrows";
};
struct S001_StdioForbiddenOnHotPath : diag::tag_base {
    static constexpr std::string_view name = "S001_StdioForbiddenOnHotPath";
};
struct S004_MeyersSingletonInitCycle : diag::tag_base {
    static constexpr std::string_view name = "S004_MeyersSingletonInitCycle";
};
struct G002_ThreadLocalAtomicNonsensical : diag::tag_base {
    static constexpr std::string_view name = "G002_ThreadLocalAtomicNonsensical";
};

// ── FIXY-V-260 hardware-axis cross-axis rule tags (Agent 11 §3.6) ──
struct V001_VendorIsaInconsistent : diag::tag_base {
    static constexpr std::string_view name = "V001_VendorIsaInconsistent";
};
struct V002_VendorCrossArch : diag::tag_base {
    static constexpr std::string_view name = "V002_VendorCrossArch";
};
struct V101_ReplaySimdIsaPinned : diag::tag_base {
    static constexpr std::string_view name = "V101_ReplaySimdIsaPinned";
};
struct V102_SimdWidthExceedsIsa : diag::tag_base {
    static constexpr std::string_view name = "V102_SimdWidthExceedsIsa";
};
struct V201_HotPathNondetTscOrPrivileged : diag::tag_base {
    static constexpr std::string_view name = "V201_HotPathNondetTscOrPrivileged";
};
struct V202_PrivilegedMsrNeedsInit : diag::tag_base {
    static constexpr std::string_view name = "V202_PrivilegedMsrNeedsInit";
};
struct V203_ReplayNondetTsc : diag::tag_base {
    static constexpr std::string_view name = "V203_ReplayNondetTsc";
};
struct V301_HotPathFullFence : diag::tag_base {
    static constexpr std::string_view name = "V301_HotPathFullFence";
};

// ── FIXY-V-268 memory-scope cross-axis rule tags (Agent WMEM §3.6) ──
struct V401_ScopeStrengthInsufficient : diag::tag_base {
    static constexpr std::string_view name = "V401_ScopeStrengthInsufficient";
};
struct V402_ScopeArchCrossTrunk : diag::tag_base {
    static constexpr std::string_view name = "V402_ScopeArchCrossTrunk";
};

// ── FIXY-FOUND-063 H010 — HotPath × Bg row contradiction ────────────
struct H010_HotPathBgContradiction : diag::tag_base {
    static constexpr std::string_view name = "H010_HotPathBgContradiction";
};

// ── FIXY-FOUND-064 P010 — Ghost × runtime-effect erasure violation ──
struct P010_GhostNonErasable : diag::tag_base {
    static constexpr std::string_view name = "P010_GhostNonErasable";
};

// ── FIXY-FOUND-065 L007 — borrow × Bg-row cross-thread lifetime ─────
struct L007_BorrowBgRow : diag::tag_base {
    static constexpr std::string_view name = "L007_BorrowBgRow";
};

// ── FIXY-FOUND-070 T001 — Trust::Unverified × UsageMode::Capability ─
struct T001_UnverifiedCapability : diag::tag_base {
    static constexpr std::string_view name = "T001_UnverifiedCapability";
};

using Catalog = std::tuple<
    I002_ClassifiedFailPayload,
    L002_BorrowAsync,
    E044_ConstantTimeAsync,
    I003_ConstantTimeFailOnSecret,
    M012_MonotonicConcurrentNoAtomic,
    P002_GhostRuntimeUse,
    I004_ClassifiedAsyncSession,
    N002_DecimalOverflowWrap,
    L003_BorrowUnscopedSpawn,
    M011_LinearFailNoCleanup,
    S010_StalenessConstantTime,
    S011_CapabilityReplay,
    L004_LinearLifetimeNeedsPermission,
    B001_BgObservableBoundedResource,
    H001_HotPathBoundedCost,
    H002_HotPathWitnessFloor,
    L005_LinearAliasSameRegionTag,
    F001_FrameDeclaresAxisCollision,
    H003_HotPathTerminatingAllocIo,
    F002_FederationPeerTerminatingBudget,
    W001_HotPathWaitParkOrBlocker,
    W002_BgWaitActiveSpin,
    F101_ReplayFpReassocPermitted,
    F102_ReplayFpContractFast,
    F103_CtFpReassocPermitted,
    F104_CtFpDenormalInputHonored,
    F105_CtFpFtzPreserved,
    M001_DontNeedRequiresReleaseAware,
    C001_AbortRequiresControlFlowWitness,
    D001_IndirectCallNoexcept,
    D002_RecursionRequiresBound,
    G001_ThreadLocalNeedsTag,
    L006_NoLongjmpAcrossLinear,
    P003_ForkBodyNoThrows,
    S001_StdioForbiddenOnHotPath,
    S004_MeyersSingletonInitCycle,
    G002_ThreadLocalAtomicNonsensical,
    V001_VendorIsaInconsistent,
    V002_VendorCrossArch,
    V101_ReplaySimdIsaPinned,
    V102_SimdWidthExceedsIsa,
    V201_HotPathNondetTscOrPrivileged,
    V202_PrivilegedMsrNeedsInit,
    V203_ReplayNondetTsc,
    V301_HotPathFullFence,
    V401_ScopeStrengthInsufficient,
    V402_ScopeArchCrossTrunk,
    H010_HotPathBgContradiction,
    P010_GhostNonErasable,
    L007_BorrowBgRow,
    T001_UnverifiedCapability
>;

inline constexpr std::size_t catalog_size = std::tuple_size_v<Catalog>;
static_assert(catalog_size == 51);

// FIXY-FOUND-139: reflection-derived RuleCode enum ceiling.
//
// `rule_bijection_v` (declared above near rule_code_of_v) maps each
// RuleCode enumerator to its tag, and the FOUND-134 reflection
// fold (see end of this file, ~line 2820) asserts every non-None
// enumerator is bijective.  But neither of those checks pins the
// RuleCode enum CARDINALITY against the Catalog tuple — a future
// RuleCode value added without appending the catalog entry would
// NOT trip the existing bijection check (the new enumerator would
// also lack a rule_tag specialization, so the fold would just
// trigger an instantiation error at the missing-spec site, NOT a
// clean cardinality mismatch).
//
// This ceiling reflects directly over the enum.  The +1 accounts
// for the `None = 255` sentinel which has no Catalog entry.
inline constexpr std::size_t rule_code_count =
    std::meta::enumerators_of(^^RuleCode).size();

static_assert(rule_code_count == catalog_size + 1,
    "FIXY-FOUND-139: RuleCode enum cardinality and Catalog tuple "
    "size diverged.  The expected relationship is rule_code_count "
    "== catalog_size + 1 (the +1 is the `None = 255` sentinel which "
    "has no Catalog entry).  Likely cause: a new RuleCode value was "
    "added without appending the rule struct to Catalog, OR a rule "
    "was appended to Catalog without minting a RuleCode enumerator. "
    " This reflection-derived pin is independent of catalog_size's "
    "hand-pinned 50 — both must agree.");

template <RuleCode R>
struct rule_tag;

template <> struct rule_tag<RuleCode::I002> { using type = I002_ClassifiedFailPayload; };
template <> struct rule_tag<RuleCode::L002> { using type = L002_BorrowAsync; };
template <> struct rule_tag<RuleCode::E044> { using type = E044_ConstantTimeAsync; };
template <> struct rule_tag<RuleCode::I003> { using type = I003_ConstantTimeFailOnSecret; };
template <> struct rule_tag<RuleCode::M012> { using type = M012_MonotonicConcurrentNoAtomic; };
template <> struct rule_tag<RuleCode::P002> { using type = P002_GhostRuntimeUse; };
template <> struct rule_tag<RuleCode::I004> { using type = I004_ClassifiedAsyncSession; };
template <> struct rule_tag<RuleCode::N002> { using type = N002_DecimalOverflowWrap; };
template <> struct rule_tag<RuleCode::L003> { using type = L003_BorrowUnscopedSpawn; };
template <> struct rule_tag<RuleCode::M011> { using type = M011_LinearFailNoCleanup; };
template <> struct rule_tag<RuleCode::S010> { using type = S010_StalenessConstantTime; };
template <> struct rule_tag<RuleCode::S011> { using type = S011_CapabilityReplay; };
template <> struct rule_tag<RuleCode::L004> { using type = L004_LinearLifetimeNeedsPermission; };
template <> struct rule_tag<RuleCode::B001> { using type = B001_BgObservableBoundedResource; };
template <> struct rule_tag<RuleCode::H001> { using type = H001_HotPathBoundedCost; };
template <> struct rule_tag<RuleCode::H002> { using type = H002_HotPathWitnessFloor; };
template <> struct rule_tag<RuleCode::L005> { using type = L005_LinearAliasSameRegionTag; };
template <> struct rule_tag<RuleCode::F001> { using type = F001_FrameDeclaresAxisCollision; };
template <> struct rule_tag<RuleCode::H003> { using type = H003_HotPathTerminatingAllocIo; };
template <> struct rule_tag<RuleCode::F002> { using type = F002_FederationPeerTerminatingBudget; };
template <> struct rule_tag<RuleCode::W001> { using type = W001_HotPathWaitParkOrBlocker; };
template <> struct rule_tag<RuleCode::W002> { using type = W002_BgWaitActiveSpin; };
template <> struct rule_tag<RuleCode::F101> { using type = F101_ReplayFpReassocPermitted; };
template <> struct rule_tag<RuleCode::F102> { using type = F102_ReplayFpContractFast; };
template <> struct rule_tag<RuleCode::F103> { using type = F103_CtFpReassocPermitted; };
template <> struct rule_tag<RuleCode::F104> { using type = F104_CtFpDenormalInputHonored; };
template <> struct rule_tag<RuleCode::F105> { using type = F105_CtFpFtzPreserved; };
template <> struct rule_tag<RuleCode::M001> { using type = M001_DontNeedRequiresReleaseAware; };
template <> struct rule_tag<RuleCode::C001> { using type = C001_AbortRequiresControlFlowWitness; };
template <> struct rule_tag<RuleCode::D001> { using type = D001_IndirectCallNoexcept; };
template <> struct rule_tag<RuleCode::D002> { using type = D002_RecursionRequiresBound; };
template <> struct rule_tag<RuleCode::G001> { using type = G001_ThreadLocalNeedsTag; };
template <> struct rule_tag<RuleCode::L006> { using type = L006_NoLongjmpAcrossLinear; };
template <> struct rule_tag<RuleCode::P003> { using type = P003_ForkBodyNoThrows; };
template <> struct rule_tag<RuleCode::S001> { using type = S001_StdioForbiddenOnHotPath; };
template <> struct rule_tag<RuleCode::S004> { using type = S004_MeyersSingletonInitCycle; };
template <> struct rule_tag<RuleCode::G002> { using type = G002_ThreadLocalAtomicNonsensical; };
template <> struct rule_tag<RuleCode::V001> { using type = V001_VendorIsaInconsistent; };
template <> struct rule_tag<RuleCode::V002> { using type = V002_VendorCrossArch; };
template <> struct rule_tag<RuleCode::V101> { using type = V101_ReplaySimdIsaPinned; };
template <> struct rule_tag<RuleCode::V102> { using type = V102_SimdWidthExceedsIsa; };
template <> struct rule_tag<RuleCode::V201> { using type = V201_HotPathNondetTscOrPrivileged; };
template <> struct rule_tag<RuleCode::V202> { using type = V202_PrivilegedMsrNeedsInit; };
template <> struct rule_tag<RuleCode::V203> { using type = V203_ReplayNondetTsc; };
template <> struct rule_tag<RuleCode::V301> { using type = V301_HotPathFullFence; };
template <> struct rule_tag<RuleCode::V401> { using type = V401_ScopeStrengthInsufficient; };
template <> struct rule_tag<RuleCode::V402> { using type = V402_ScopeArchCrossTrunk; };
template <> struct rule_tag<RuleCode::H010> { using type = H010_HotPathBgContradiction; };
template <> struct rule_tag<RuleCode::P010> { using type = P010_GhostNonErasable; };
template <> struct rule_tag<RuleCode::L007> { using type = L007_BorrowBgRow; };
template <> struct rule_tag<RuleCode::T001> { using type = T001_UnverifiedCapability; };

template <RuleCode R>
using rule_tag_t = typename rule_tag<R>::type;

template <typename Tag>
struct rule_code_of;

template <> struct rule_code_of<I002_ClassifiedFailPayload> {
    static constexpr RuleCode value = RuleCode::I002;
};
template <> struct rule_code_of<L002_BorrowAsync> {
    static constexpr RuleCode value = RuleCode::L002;
};
template <> struct rule_code_of<E044_ConstantTimeAsync> {
    static constexpr RuleCode value = RuleCode::E044;
};
template <> struct rule_code_of<I003_ConstantTimeFailOnSecret> {
    static constexpr RuleCode value = RuleCode::I003;
};
template <> struct rule_code_of<M012_MonotonicConcurrentNoAtomic> {
    static constexpr RuleCode value = RuleCode::M012;
};
template <> struct rule_code_of<P002_GhostRuntimeUse> {
    static constexpr RuleCode value = RuleCode::P002;
};
template <> struct rule_code_of<I004_ClassifiedAsyncSession> {
    static constexpr RuleCode value = RuleCode::I004;
};
template <> struct rule_code_of<N002_DecimalOverflowWrap> {
    static constexpr RuleCode value = RuleCode::N002;
};
template <> struct rule_code_of<L003_BorrowUnscopedSpawn> {
    static constexpr RuleCode value = RuleCode::L003;
};
template <> struct rule_code_of<M011_LinearFailNoCleanup> {
    static constexpr RuleCode value = RuleCode::M011;
};
template <> struct rule_code_of<S010_StalenessConstantTime> {
    static constexpr RuleCode value = RuleCode::S010;
};
template <> struct rule_code_of<S011_CapabilityReplay> {
    static constexpr RuleCode value = RuleCode::S011;
};
template <> struct rule_code_of<L004_LinearLifetimeNeedsPermission> {
    static constexpr RuleCode value = RuleCode::L004;
};
template <> struct rule_code_of<B001_BgObservableBoundedResource> {
    static constexpr RuleCode value = RuleCode::B001;
};
template <> struct rule_code_of<H001_HotPathBoundedCost> {
    static constexpr RuleCode value = RuleCode::H001;
};
template <> struct rule_code_of<H002_HotPathWitnessFloor> {
    static constexpr RuleCode value = RuleCode::H002;
};
template <> struct rule_code_of<L005_LinearAliasSameRegionTag> {
    static constexpr RuleCode value = RuleCode::L005;
};
template <> struct rule_code_of<F001_FrameDeclaresAxisCollision> {
    static constexpr RuleCode value = RuleCode::F001;
};
template <> struct rule_code_of<H003_HotPathTerminatingAllocIo> {
    static constexpr RuleCode value = RuleCode::H003;
};
template <> struct rule_code_of<F002_FederationPeerTerminatingBudget> {
    static constexpr RuleCode value = RuleCode::F002;
};
template <> struct rule_code_of<W001_HotPathWaitParkOrBlocker> {
    static constexpr RuleCode value = RuleCode::W001;
};
template <> struct rule_code_of<W002_BgWaitActiveSpin> {
    static constexpr RuleCode value = RuleCode::W002;
};
template <> struct rule_code_of<F101_ReplayFpReassocPermitted> {
    static constexpr RuleCode value = RuleCode::F101;
};
template <> struct rule_code_of<F102_ReplayFpContractFast> {
    static constexpr RuleCode value = RuleCode::F102;
};
template <> struct rule_code_of<F103_CtFpReassocPermitted> {
    static constexpr RuleCode value = RuleCode::F103;
};
template <> struct rule_code_of<F104_CtFpDenormalInputHonored> {
    static constexpr RuleCode value = RuleCode::F104;
};
template <> struct rule_code_of<F105_CtFpFtzPreserved> {
    static constexpr RuleCode value = RuleCode::F105;
};
template <> struct rule_code_of<M001_DontNeedRequiresReleaseAware> {
    static constexpr RuleCode value = RuleCode::M001;
};
template <> struct rule_code_of<C001_AbortRequiresControlFlowWitness> {
    static constexpr RuleCode value = RuleCode::C001;
};
template <> struct rule_code_of<D001_IndirectCallNoexcept> {
    static constexpr RuleCode value = RuleCode::D001;
};
template <> struct rule_code_of<D002_RecursionRequiresBound> {
    static constexpr RuleCode value = RuleCode::D002;
};
template <> struct rule_code_of<G001_ThreadLocalNeedsTag> {
    static constexpr RuleCode value = RuleCode::G001;
};
template <> struct rule_code_of<L006_NoLongjmpAcrossLinear> {
    static constexpr RuleCode value = RuleCode::L006;
};
template <> struct rule_code_of<P003_ForkBodyNoThrows> {
    static constexpr RuleCode value = RuleCode::P003;
};
template <> struct rule_code_of<S001_StdioForbiddenOnHotPath> {
    static constexpr RuleCode value = RuleCode::S001;
};
template <> struct rule_code_of<S004_MeyersSingletonInitCycle> {
    static constexpr RuleCode value = RuleCode::S004;
};
template <> struct rule_code_of<G002_ThreadLocalAtomicNonsensical> {
    static constexpr RuleCode value = RuleCode::G002;
};
template <> struct rule_code_of<V001_VendorIsaInconsistent> {
    static constexpr RuleCode value = RuleCode::V001;
};
template <> struct rule_code_of<V002_VendorCrossArch> {
    static constexpr RuleCode value = RuleCode::V002;
};
template <> struct rule_code_of<V101_ReplaySimdIsaPinned> {
    static constexpr RuleCode value = RuleCode::V101;
};
template <> struct rule_code_of<V102_SimdWidthExceedsIsa> {
    static constexpr RuleCode value = RuleCode::V102;
};
template <> struct rule_code_of<V201_HotPathNondetTscOrPrivileged> {
    static constexpr RuleCode value = RuleCode::V201;
};
template <> struct rule_code_of<V202_PrivilegedMsrNeedsInit> {
    static constexpr RuleCode value = RuleCode::V202;
};
template <> struct rule_code_of<V203_ReplayNondetTsc> {
    static constexpr RuleCode value = RuleCode::V203;
};
template <> struct rule_code_of<V301_HotPathFullFence> {
    static constexpr RuleCode value = RuleCode::V301;
};
template <> struct rule_code_of<V401_ScopeStrengthInsufficient> {
    static constexpr RuleCode value = RuleCode::V401;
};
template <> struct rule_code_of<V402_ScopeArchCrossTrunk> {
    static constexpr RuleCode value = RuleCode::V402;
};
template <> struct rule_code_of<H010_HotPathBgContradiction> {
    static constexpr RuleCode value = RuleCode::H010;
};
template <> struct rule_code_of<P010_GhostNonErasable> {
    static constexpr RuleCode value = RuleCode::P010;
};
template <> struct rule_code_of<L007_BorrowBgRow> {
    static constexpr RuleCode value = RuleCode::L007;
};
template <> struct rule_code_of<T001_UnverifiedCapability> {
    static constexpr RuleCode value = RuleCode::T001;
};

template <typename Tag>
inline constexpr RuleCode rule_code_of_v = rule_code_of<Tag>::value;

template <RuleCode R>
inline constexpr bool rule_bijection_v = rule_code_of_v<rule_tag_t<R>> == R;

template <typename F, RuleCode R>
struct CollisionDiagnosticByRule;

template <typename F>
struct CollisionDiagnosticByRule<F, RuleCode::None> {
    [[nodiscard]] static consteval RuleCode category() noexcept { return RuleCode::None; }
    [[nodiscard]] static consteval std::string_view rule_code() noexcept { return "OK"; }
    [[nodiscard]] static consteval std::string_view goal() noexcept {
        return "Fn grade composition satisfies every registered collision rule";
    }
    [[nodiscard]] static consteval std::string_view gap() noexcept {
        return "none";
    }
    [[nodiscard]] static consteval std::string_view suggestion() noexcept {
        return "no remediation required";
    }
    [[nodiscard]] static consteval std::string_view reference() noexcept {
        return "fixy.md §24.2";
    }
};

#define CRUCIBLE_COLLISION_DIAGNOSTIC(rule, code_text, goal_text, gap_text, suggestion_text, ref_text) \
    template <typename F>                                                                             \
    struct CollisionDiagnosticByRule<F, RuleCode::rule> {                                             \
        [[nodiscard]] static consteval RuleCode category() noexcept { return RuleCode::rule; }         \
        [[nodiscard]] static consteval std::string_view rule_code() noexcept { return code_text; }     \
        [[nodiscard]] static consteval std::string_view goal() noexcept { return goal_text; }          \
        [[nodiscard]] static consteval std::string_view gap() noexcept { return gap_text; }            \
        [[nodiscard]] static consteval std::string_view suggestion() noexcept { return suggestion_text; } \
        [[nodiscard]] static consteval std::string_view reference() noexcept { return ref_text; }      \
    }

CRUCIBLE_COLLISION_DIAGNOSTIC(I002, "I002", "classified error flow preserves secrecy",
    "classified value flows through Fail(E) with a non-secret error payload",
    "declare Fail(secret E), declassify explicitly, or remove Fail from the classified region",
    "fixy.md §24.2 I002");
CRUCIBLE_COLLISION_DIAGNOSTIC(L002, "L002", "borrow lifetime does not bridge await",
    "borrow capture is combined with async suspension",
    "scope the borrow before await, capture by value, or use structured tasks",
    "fixy.md §24.2 L002");
CRUCIBLE_COLLISION_DIAGNOSTIC(E044, "E044", "constant-time region has deterministic timing",
    "constant-time marker is combined with async scheduling",
    "run the CT core synchronously and wrap only the boundary in async",
    "fixy.md §24.2 E044");
CRUCIBLE_COLLISION_DIAGNOSTIC(I003, "I003", "secret-dependent failure is not observable",
    "constant-time function can fail on a secret-dependent condition",
    "use ct_select inside the secret region and fail after declassification",
    "fixy.md §24.2 I003");
CRUCIBLE_COLLISION_DIAGNOSTIC(M012, "M012", "concurrent monotonic update is atomic or merged",
    "monotonic mutation is used in a concurrent context without atomic representation",
    "use ReprKind::Atomic or safety::AtomicMonotonic<T, Cmp>",
    "fixy.md §24.2 M012");
CRUCIBLE_COLLISION_DIAGNOSTIC(P002, "P002", "ghost data stays erased",
    "ghost value is used by runtime code",
    "compute a runtime value separately or move the whole branch into ghost code",
    "fixy.md §24.2 P002");
CRUCIBLE_COLLISION_DIAGNOSTIC(I004, "I004", "classified session sends do not leak timing",
    "classified async session is declared without CT discipline",
    "wrap the classified send in a synchronous CT region or declassify before send",
    "fixy.md §24.2 I004");
CRUCIBLE_COLLISION_DIAGNOSTIC(N002, "N002", "decimal overflow mode is meaningful",
    "exact decimal type is combined with modular wrap overflow",
    "use trap, saturate, or widen for exact decimal arithmetic",
    "fixy.md §24.2 N002");
CRUCIBLE_COLLISION_DIAGNOSTIC(L003, "L003", "borrowed captures cannot outlive their scope",
    "borrow capture is combined with unscoped spawn",
    "use task_group, permission_fork, or move ownership into the closure",
    "fixy.md §24.2 L003");
CRUCIBLE_COLLISION_DIAGNOSTIC(M011, "M011", "linear resources are cleaned on fail paths",
    "linear value is live across Fail without cleanup",
    "register defer/errdefer, use RAII cleanup, or fail before acquiring the resource",
    "fixy.md §24.2 M011");
CRUCIBLE_COLLISION_DIAGNOSTIC(S010, "S010", "constant-time code has no freshness branch",
    "non-fresh staleness policy is combined with CT",
    "require stale::Fresh in CT code or remove the CT guarantee",
    "fixy.md §24.2 S010");
CRUCIBLE_COLLISION_DIAGNOSTIC(S011, "S011", "replay-stable code has reconstructable resources",
    "ephemeral capability is used in replay-required code without a stable handle",
    "use content-addressed handles or remove replay eligibility",
    "fixy.md §24.2 S011");
CRUCIBLE_COLLISION_DIAGNOSTIC(L004, "L004", "linear values in region lifetime carry Permission proof",
    "Usage::Linear with lifetime::In<Tag> declared without a Permission token marker",
    "thread Permission<Tag> through the call (permissions/Permission.h) or move Usage out of Linear",
    "fixy.md §24.2 L004");
CRUCIBLE_COLLISION_DIAGNOSTIC(B001, "B001", "Bg observable surface declares bounded resource",
    "row contains Effect::Bg AND the function is externally observable but resource budget is Unstated",
    "declare space::Bounded<N> + cost::Linear<N> (or stricter); a Bg-observable surface that may run unbounded is a back-pressure trap",
    "fixy.md §24.2 B001");
CRUCIBLE_COLLISION_DIAGNOSTIC(H001, "H001", "HotPath functions declare bounded cost",
    "marks_hot_path with cost_t == cost::Unstated or cost::Unbounded",
    "declare cost::Constant or cost::Linear<N>; the hot path must justify its compute envelope",
    "fixy.md §24.2 H001");
CRUCIBLE_COLLISION_DIAGNOSTIC(H002, "H002", "HotPath functions carry a refinement witness floor",
    "marks_hot_path with refinement_t == pred::True (no witness)",
    "attach a Refined<predicate, Type> input that proves an invariant the hot body assumes (e.g., aligned, in-range, non-zero); pred::True is review-rejected on hot paths",
    "fixy.md §24.2 H002");
CRUCIBLE_COLLISION_DIAGNOSTIC(L005, "L005", "linear bindings in same region tag do not alias",
    "two or more Linear bindings in a pack share the same lifetime::In<Tag>",
    "split the region into distinct tags or convert one of the aliasing linears to Borrow",
    "fixy.md §24.2 L005");
CRUCIBLE_COLLISION_DIAGNOSTIC(F001, "F001", "frame manifesto axes agree across the pack",
    "two or more bindings in a frame manifesto declare conflicting values for the same axis",
    "reconcile the conflicting axis values or split the frame; F001 is the multi-binding version of TypeSafe at the discipline layer",
    "fixy.md §24.2 F001");
CRUCIBLE_COLLISION_DIAGNOSTIC(H003, "H003", "HotPath does not perform unbounded Alloc or IO",
    "marks_hot_path with row containing Effect::Alloc or Effect::IO without bounded budget",
    "move Alloc/IO outside the hot path or attach an Init/Bg context that owns the unbounded surface",
    "fixy.md §24.2 H003");
CRUCIBLE_COLLISION_DIAGNOSTIC(F002, "F002", "federation peers terminate within a budget",
    "marks_federation_peer is true but cost_t == cost::Unstated or cost::Unbounded",
    "attach a wall-clock budget (cost::Linear<N>) and a terminating bound; federation peers cannot run forever",
    "fixy.md §24.2 F002");
CRUCIBLE_COLLISION_DIAGNOSTIC(W001, "W001",
    "HotPath functions do not wrap their return/parameter type in a syscall-blocking Wait strategy",
    "marks_hot_path is true AND F::type_t is safety::Wait<Park, U> or safety::Wait<Block, U> "
    "(strategies that involve futex / condvar / poll syscalls, 1-5 µs latency)",
    // FIXY-FOUND-124: previous remediation also listed Wait<UmwaitC01> and
    // Wait<AcquireWait> — both are explicitly contraindicated for hot path by
    // CLAUDE.md §IX (UMWAIT: 'Not applicable on our hot path'; futex / atomic::
    // wait: 'BANNED on hot path').  Recommend only the two §IX-admissible hot-
    // path strategies; see safety/Wait.h `is_hot_path_waiter_admissible` for
    // the strict admissibility gate.
    "drop the Wait wrapper from the hot path, switch to a non-blocking strategy "
    "(Wait<SpinPause> — the §IX default, 10-40 ns intra-socket; or Wait<BoundedSpin> "
    "for unknown-delay signals), or move the blocking call into an Init/Bg context "
    "that owns the syscall cost. Wait<UmwaitC01> and Wait<AcquireWait> are NOT "
    "valid hot-path replacements (CLAUDE.md §IX latency hierarchy)",
    "fixy.md §24.2 W001; CLAUDE.md §IX");
CRUCIBLE_COLLISION_DIAGNOSTIC(W002, "W002",
    "Bg-row functions do not wrap their return/parameter type in an active-spin Wait strategy",
    "effect_row contains Effect::Bg AND F::type_t is safety::Wait<SpinPause, U> or "
    "safety::Wait<BoundedSpin, U> (strategies that monopolize the hosting core with "
    "an `_mm_pause` loop, 100% CPU until the wait resolves)",
    "switch to a yielding strategy on the Bg path — Wait<UmwaitC01> (power-aware), "
    "Wait<AcquireWait> (futex), Wait<Park> (condvar), or Wait<Block> (poll/epoll). "
    "Active-spin in a Bg row is the back-pressure-trap shape — Bg threads are by "
    "contract permitted to block so the scheduler can do useful work elsewhere",
    "fixy.md §24.2 W002");
// FIXY-V-091 F-family diagnostics (5 FP-mode cross-axis rules).
CRUCIBLE_COLLISION_DIAGNOSTIC(F101, "F101",
    "Replay-required functions do not wrap return/parameter type in "
    "FpReassociatePinned<UnrestrictedRewrite>",
    "marks_replay_required is true AND F::type_t is "
    "safety::FpReassociatePinned<FpReassociate::UnrestrictedRewrite, U>. "
    "Algebraic rewrite (-fassociative-math) reorders FP additions; "
    "the bit pattern depends on the compiler's instruction-scheduler "
    "micro-state and diverges across vendors, defeating bit-exact replay",
    "drop the FpReassociatePinned wrapper on the replay-required path, "
    "switch to FpReassociatePinned<Forbidden> (IEEE 754 default) or "
    "FpReassociatePinned<BoundedTreeDepth> with a canonical reduction "
    "topology, or move the rewrite-eligible work outside the replay region",
    "fixy.md §24.2 F101 (V-091)");
CRUCIBLE_COLLISION_DIAGNOSTIC(F102, "F102",
    "Replay-required functions do not wrap return/parameter type in "
    "FpContractPinned<Fast>",
    "marks_replay_required is true AND F::type_t is "
    "safety::FpContractPinned<FpContract::Fast, U>. "
    "Cross-statement FMA folding (-ffp-contract=fast) lets the compiler "
    "fuse `a*b + c` boundaries that differ per vendor (NVIDIA SASS, "
    "AMD CDNA, Intel SPR contract at different expression boundaries); "
    "same source → different bit patterns",
    "drop the FpContractPinned wrapper or switch to FpContractPinned<Off> "
    "/ FpContractPinned<OnInExpr> (within-statement FMA only, IEEE 754-2008 "
    "default — bit-equivalent across vendors)",
    "fixy.md §24.2 F102 (V-091)");
CRUCIBLE_COLLISION_DIAGNOSTIC(F103, "F103",
    "Constant-time functions do not wrap return/parameter type in "
    "FpReassociatePinned<UnrestrictedRewrite>",
    "marks_ct is true AND F::type_t is "
    "safety::FpReassociatePinned<FpReassociate::UnrestrictedRewrite, U>. "
    "Reassociation introduces data-dependent reduction-tree topology — "
    "the compiler may pick different orderings based on operand magnitudes "
    "or constant-foldability, violating the timing-independence guarantee",
    "drop the FpReassociatePinned wrapper on the CT path or switch to "
    "FpReassociatePinned<Forbidden>; reassociation in a CT region must "
    "either be eliminated or constrained to a topology-pinned tree",
    "fixy.md §24.2 F103 (V-091)");
CRUCIBLE_COLLISION_DIAGNOSTIC(F104, "F104",
    "Constant-time functions do not wrap return/parameter type in "
    "FpDenormalInputPinned<HonorDenormals>",
    "marks_ct is true AND F::type_t is "
    "safety::FpDenormalInputPinned<FpDenormalInput::HonorDenormals, U>. "
    "DAZ=0 (denormal inputs are honored) introduces a 30-100× slowdown "
    "on x86 / ARM when the input IS denormal — textbook FP timing side-channel. "
    "Crypto and CT paths PIN DenormalsAreZero (DAZ=1) so denormal-vs-normal "
    "input cycle counts are identical",
    "switch to FpDenormalInputPinned<DenormalsAreZero> on the CT path "
    "(MXCSR.DAZ / FPCR.FZ bit on x86 / ARM), or move the denormal-honoring "
    "code outside the constant-time region",
    "fixy.md §24.2 F104 (V-091)");
CRUCIBLE_COLLISION_DIAGNOSTIC(F105, "F105",
    "Constant-time functions do not wrap return/parameter type in "
    "FpFtzPinned<PreserveSubnormals>",
    "marks_ct is true AND F::type_t is "
    "safety::FpFtzPinned<FpFtz::PreserveSubnormals, U>. "
    "FTZ=0 (subnormal outputs are preserved) introduces a 30-100× slowdown "
    "PRODUCING denormal outputs (output side; F104 catches the input side). "
    "Result-magnitude can leak through cycle count, defeating CT timing",
    "switch to FpFtzPinned<FlushToZero> on the CT path so the output is "
    "always a normal value or ±0.0 in constant time, or move the "
    "subnormal-preserving code outside the constant-time region",
    "fixy.md §24.2 F105 (V-091)");
// FIXY-V-243 hazard-axis diagnostics (8 control-flow / dispatch / global / stdio rules).
CRUCIBLE_COLLISION_DIAGNOSTIC(C001, "C001",
    "abort-declaring functions witness the escape in their ControlFlow tier",
    "marks_aborts is true (grant::ctrl::abort<Rationale>) AND F::type_t carries "
    "no ControlFlowPinned tier >= AbortOnly. Declaring a function may std::abort "
    "while its ControlFlow axis says Pure is the escape inconsistency Agent 10 §4 names",
    "wrap the result in ControlFlowPinned<AbortOnly, U> (or a higher tier), or remove "
    "the abort grant if the function genuinely always returns",
    "fixy.md §24.2 C001 (V-243)");
CRUCIBLE_COLLISION_DIAGNOSTIC(D001, "D001",
    "indirect-call grants carry a noexcept callable",
    "grant::dispatch::indirect_call<FnPtrFamily> where the family's RunFn type is "
    "NOT noexcept. An indirect call that may throw across a -fno-exceptions boundary "
    "terminates (Scenario A: BackgroundThread::RegionReadyCallback::Fn missing noexcept)",
    "add noexcept to the callable family's RunFn signature, or move the throwing call "
    "behind a noexcept trampoline that converts the failure into std::expected",
    "fixy.md §24.2 D001 (V-243)");
CRUCIBLE_COLLISION_DIAGNOSTIC(D002, "D002",
    "recursion grants declare a static depth bound",
    "grant::dispatch::recurses<> declared without an NTTP MaxDepth — the implicit-"
    "recursion-bound anti-pattern that risks unbounded stack growth",
    "declare grant::dispatch::recurses<MaxDepth> with the proven worst-case depth, "
    "or convert the recursion to an explicit bounded iterative loop",
    "fixy.md §24.2 D002 (V-243)");
CRUCIBLE_COLLISION_DIAGNOSTIC(G001, "G001",
    "thread_local grants carry an identity tag",
    "grant::global::thread_local_<> declared without a TLSTag NTTP — an untagged "
    "thread_local cannot be distinguished in the federation cache key or audited "
    "for per-thread-singleton init-order hazards",
    "declare grant::global::thread_local_<TLSTag> with a unique phantom tag identifying "
    "the storage (mirrors the safety::ThreadLocalRef<Tag, T> discipline)",
    "fixy.md §24.2 G001 (V-243)");
CRUCIBLE_COLLISION_DIAGNOSTIC(L006, "L006",
    "Linear resources are not held across a destructor-skipping non-local jump",
    "Usage::Linear AND F::type_t carries a ControlFlowPinned tier >= MayLongjmp "
    "(or the marks_longjmp_unsafe marker). longjmp SKIPS destructors — a Linear "
    "resource in scope would leak / dangle across the jump",
    "lower the ControlFlow tier below MayLongjmp (longjmp is RAII-unsafe with a "
    "Linear in scope), release the Linear before the jump, or convert Usage out of Linear",
    "fixy.md §24.2 L006 (V-243)");
CRUCIBLE_COLLISION_DIAGNOSTIC(P003, "P003",
    "permission_fork worker bodies do not throw",
    "marks_fork_worker AND F::type_t carries a ControlFlowPinned tier >= ThrowOnly "
    "(or the marks_throws marker). A throw inside a jthread fork body crosses no thread "
    "boundary; under -fno-exceptions it is std::terminate",
    "lower the ControlFlow tier below ThrowOnly on the fork body, convert the failure "
    "into a std::expected return, or handle the error inside the worker before it escapes",
    "fixy.md §24.2 P003 (V-243)");
CRUCIBLE_COLLISION_DIAGNOSTIC(S001, "S001",
    "hot-path functions perform no stdio",
    "marks_hot_path AND F::type_t carries a StdioPinned tier >= BufferedWrite. "
    "TraceRing / Arena / KernelCache MUST NOT do stdio — format parsing >= 100 ns "
    "and output syscalls flush buffers, blowing the hot-path budget (CLAUDE.md §XII)",
    "drop the StdioPinned wrapper from the hot path (use StdioPinned<NoStdio>), push "
    "structured events to an SPSC ring for bg drain, or move the stdio into a Bg context",
    "fixy.md §24.2 S001 (V-243)");
CRUCIBLE_COLLISION_DIAGNOSTIC(S004, "S004",
    "registered Meyers-singletons have an acyclic init-dependency graph",
    "marks_singleton_init_cycle is true — the V-248 tag-graph closure walk over "
    "registered grant::global::singleton<Tag> annotations detected a cycle, the "
    "static-initialization-order fiasco in its most subtle (lazy-init) form",
    "break the init-dependency cycle (inject the dependency, or merge the singletons "
    "into one initialization unit); see pack::singleton_init_acyclic for the detector",
    "fixy.md §24.2 S004 (V-243)");
CRUCIBLE_COLLISION_DIAGNOSTIC(G002, "G002",
    "thread_local storage is never combined with atomic synchronization",
    "marks_thread_local_atomic is true — a grant::global::thread_local_<Tag> is "
    "paired with an atomic memory-order wrapper. An atomic op on a per-thread "
    "object orders against no peer (one instance per thread), so the atomic is "
    "either redundant (and misleading) or the thread_local was a typo for a "
    "process-wide static (a silent correctness gap, e.g. bench_smoke.cpp:78)",
    "decide intent: drop thread_local for a process-wide `static std::atomic<T>` "
    "if cross-thread, or drop the atomic for a plain thread_local (keeping the "
    "grant::global::thread_local_<Tag>) if genuinely per-thread",
    "fixy.md §24.2 G002 (V-249)");
// FIXY-V-260 V-family diagnostics (8 hardware-axis cross-axis rules).
CRUCIBLE_COLLISION_DIAGNOSTIC(V001, "V001",
    "vendor::intrinsic grants in a pack declare a consistent (vendor, ISA-family)",
    "marks_vendor_isa_inconsistent — the binding pack declares vendor::intrinsic<V, I> "
    "grants whose (V, I) disagree (one pins NV, another an x86 ISA family). The "
    "per-grant vendor_isa_consistent_v<V, I> gate (V-258) only checks a single "
    "intrinsic; the pack can still mix incompatible vendors",
    "pin one vendor for the whole binding, or split the multi-vendor work into "
    "separate per-vendor Fn signatures (the Mimic per-vendor backend pattern)",
    "fixy.md §24.2 V001 (V-260)");
CRUCIBLE_COLLISION_DIAGNOSTIC(V002, "V002",
    "a single binding does not compose intrinsics from incompatible arch trunks",
    "marks_vendor_cross_arch — one binding composes an x86-trunk intrinsic AND an "
    "ARM-trunk intrinsic. The emitted binary would #UD on whichever ISA it lands; "
    "x86 code never runs on ARM and vice versa (SimdIsaLattice cross-trunk leq = false)",
    "select the architecture at the dispatch boundary and keep each arch's intrinsics "
    "in its own single-target binary (CLAUDE.md §VIII single-target rule); see the "
    "V-261 source::ArchPinned<Arch> gate",
    "fixy.md §24.2 V002 (V-260)");
CRUCIBLE_COLLISION_DIAGNOSTIC(V101, "V101",
    "replay-required functions do not pin a specific vector ISA",
    "marks_replay_required is true AND F::type_t is SimdWidthPinned<W, U> for a W "
    "that is neither Scalar nor Portable. AVX-512 and NEON have different lane counts, "
    "so the same IR produces a different FP-reduction tree and the bit pattern "
    "diverges across the cross-vendor CI matrix (CLAUDE.md DetSafe: FP reductions "
    "reorder under chunked fold)",
    "pin SimdIsa::Scalar (no SIMD) or SimdIsa::Portable (⊤, identical on every ISA) "
    "for replay-required bodies, or drop replay eligibility and let Mimic pick the "
    "per-vendor vector kernel",
    "fixy.md §24.2 V101 (V-260)");
CRUCIBLE_COLLISION_DIAGNOSTIC(V102, "V102",
    "a simd::width<W> grant does not exceed the declared ISA family native width",
    "marks_simd_width_exceeds_isa — a simd::width<W> grant pins a register width "
    "wider than the bound vendor ISA family supports (the marquee width<512> on an "
    "AVX2 binding: AVX2 tops out at 256-bit, so a 512-bit width would emit "
    "instructions the target #UDs on)",
    "lower the pinned width to the ISA family's native maximum, or raise the ISA "
    "family (vendor::avx512bw_intrinsic) so the width is representable",
    "fixy.md §24.2 V102 (V-260)");
CRUCIBLE_COLLISION_DIAGNOSTIC(V201, "V201",
    "HotPath functions do not carry a serializing or privileged Hw instruction tier",
    "marks_hot_path is true AND F::type_t is Hw<NonDeterministicTsc, U> or "
    "Hw<PrivilegedMsr, U>. rdtsc / rdtscp are serializing (≈ 20-40 cycles); "
    "rdmsr / wrmsr are ring-0 privileged traps — both blow the ≤ 40 ns intra-socket "
    "hot budget (CLAUDE.md §IX)",
    "move the timestamp / MSR read into an Init or Bg context that owns the "
    "instruction cost, or drop the Hw tier to Hw<Vectorizable> / Hw<Scalar> on the "
    "hot path",
    "fixy.md §24.2 V201 (V-260)");
CRUCIBLE_COLLISION_DIAGNOSTIC(V202, "V202",
    "a PrivilegedMsr Hw tier is carried only inside an Init-context row",
    "F::type_t is Hw<PrivilegedMsr, U> but the effect_row does NOT contain "
    "Effect::Init. rdmsr / wrmsr / IN / OUT require ring 0 and a Permission proof; "
    "the HwInstructionLattice doc pins them to one-shot privileged Init setup, never "
    "the steady state",
    "thread the privileged MSR access through an Init context (effects::Init) that "
    "owns the ring-0 capability, or drop the PrivilegedMsr tier",
    "fixy.md §24.2 V202 (V-260)");
CRUCIBLE_COLLISION_DIAGNOSTIC(V203, "V203",
    "replay-required functions do not read a non-deterministic timestamp counter",
    "marks_replay_required is true AND F::type_t is Hw<NonDeterministicTsc, U> or "
    "Hw<PrivilegedMsr, U>. rdtsc is hardware-dependent (different cycle base / "
    "invariant-TSC behavior on H100 vs 3090 hosts), so a replay body reading it "
    "diverges across reincarnation hardware — the instruction-axis dual of F101",
    "use the deterministic Philox / logical-clock source instead of rdtsc in replay "
    "bodies, or drop replay eligibility",
    "fixy.md §24.2 V203 (V-260)");
CRUCIBLE_COLLISION_DIAGNOSTIC(V301, "V301",
    "HotPath functions do not carry a full-fence barrier strength",
    "marks_hot_path is true AND F::type_t is BarrierGuarded<SeqCst, U> or "
    "BarrierGuarded<FullFence, U>. A full fence (mfence / lock-prefixed) drains the "
    "store buffer (≈ 20-40+ cycles) — CLAUDE.md §IX mandates acquire/release only on "
    "the hot path (free on x86 TSO)",
    "use BarrierGuarded<AcquireLoad> / <ReleaseStore> / <AcqRel> on the hot path; "
    "reserve SeqCst / FullFence for Init or Bg sequencing",
    "fixy.md §24.2 V301 (V-260)");
// FIXY-V-268 V4xx-family diagnostics (2 memory-scope cross-axis rules).
CRUCIBLE_COLLISION_DIAGNOSTIC(V401, "V401",
    "cross-CTA-or-wider memory-scope publications carry at least acquire-release ordering",
    "F::type_t composes a ScopedFence tier ⊒ Cluster (cross-CTA / device / system "
    "visibility) with a BarrierGuarded tier ⊏ AcqRel (None / CompilerBarrier / AcquireLoad "
    "/ ReleaseStore). A `.cluster`/`.gpu`/`.sys`-scope publication widens VISIBILITY but "
    "a sub-AcqRel barrier never establishes the two-sided ordering cross-CTA / cross-"
    "cluster / cross-device readers require — a silent weak-memory race (the MemoryScope "
    "axis widens reach; the BarrierStrength axis must independently establish ordering). "
    "FIXY-FOUND-062 widened the catch set from {Gpu, System} to {Cluster, Gpu, System} "
    "— Hopper thread-block clusters cross CTA boundaries via distributed shared memory",
    "raise the barrier to BarrierGuarded<AcqRel> (or SeqCst / FullFence) on the cross-CTA-"
    "scope publication, or narrow the ScopedFence scope to Cta / Warp where the weaker "
    "barrier suffices",
    "fixy.md §24.2 V401 (V-268, V-FOUND-062)");
CRUCIBLE_COLLISION_DIAGNOSTIC(H010, "H010",
    "HotPath functions do not also claim Bg-context membership",
    "marks_hot_path AND F::effect_row_t contains effects::Effect::Bg — a function "
    "cannot be BOTH on the hot path (≤40 ns intra-socket per CLAUDE.md §IX) AND "
    "in background context (Alloc / IO / Block / millisecond latency allowed). The "
    "two contexts are mutually exclusive by design. H001 catches HotPath × unbounded "
    "cost; H003 catches HotPath × Alloc/IO × unbounded cost; but a HotPath × Row<Bg> "
    "with cost::Constant and no Alloc/IO/Block atoms slips both of those rules — "
    "yet is structurally still a context contradiction",
    "drop the HotPath marker (the function is genuinely Bg / not on the hot path), "
    "OR drop the Bg atom from the effect row (the function is genuinely hot-path — "
    "if it really does need an Alloc/IO/Block effect, use the appropriate atom "
    "alongside a bounded cost and route it through H001/H003 instead)",
    "fixy.md §24.2 H010 (V-FOUND-063)");
CRUCIBLE_COLLISION_DIAGNOSTIC(P010, "P010",
    "Ghost-usage functions carry no runtime-observable effects in their effect row",
    "F::usage_v == UsageMode::Ghost AND F::effect_row_t contains any of "
    "{effects::Effect::Alloc, effects::Effect::IO, effects::Effect::Block} — a "
    "Ghost binding is erased at codegen (no emitted instructions, no register "
    "pressure, no stack footprint), but Alloc emits heap-touching code, IO emits "
    "syscall / kernel-mediated traffic, and Block emits blocking primitives. All "
    "three observable effects REQUIRE emitted instructions; a Ghost binding "
    "contractually forbids them. P002 catches the grant-driven variant "
    "(marks_runtime_ghost_use trait specialization); P010 catches the structural "
    "effect-row variant. Structurally parallel to H010 (HotPath × Bg) but on the "
    "Usage axis instead of the HotPath marker — the two rules pin the same "
    "erased-vs-emitted contradiction shape on orthogonal axes",
    "drop UsageMode::Ghost (the function genuinely runs and produces the declared "
    "effects — pick Linear / Borrow / Capability as the actual usage), OR drop the "
    "observable effect atom from the effect row (the function is genuinely ghost; "
    "if a downstream caller needs the Alloc/IO/Block surface, wrap the runtime "
    "implementation in a sibling non-Ghost binding and let the Ghost binding "
    "stay pure)",
    "fixy.md §24.2 P010 (V-FOUND-064)");
CRUCIBLE_COLLISION_DIAGNOSTIC(L007, "L007",
    "borrow-capture functions do not also claim Bg-row context membership",
    "has_borrow_capture_v<F> (Usage == UsageMode::Borrow, or F::type_t is a "
    "borrowed carrier, or marks_borrow_capture is engaged) AND F::effect_row_t "
    "contains effects::Effect::Bg. A borrow_capture function declares 'I take a "
    "borrowed reference whose lifetime is tied to the caller'; a Bg-row function "
    "declares 'I execute in background-thread context'. When the background "
    "thread runs the body, the caller's stack may have unwound, leaving the "
    "borrow dangling — the same cross-thread lifetime hazard L003 targets, but "
    "readable from the EffectRow alone (no marks_unscoped_spawn specialization "
    "required). L002 covers borrow x async-suspension via the marks_async "
    "marker; L003 covers borrow x unscoped-spawn via the marks_unscoped_spawn "
    "marker; L007 covers the marker-free Bg-row direct-read case",
    "drop UsageMode::Borrow / the borrowed carrier (move ownership into the "
    "Bg-context closure via Linear / Capability / Owned), OR drop the Bg atom "
    "from the effect row (the function does not actually execute in background "
    "context — pick Foreground / Init / Test as appropriate). For genuine "
    "fork-join patterns where the borrow IS lifetime-scoped via "
    "permission_fork, use the explicit fork API instead of a bare "
    "borrow_capture x Row<Bg> signature",
    "fixy.md §24.2 L007 (V-FOUND-065)");
CRUCIBLE_COLLISION_DIAGNOSTIC(V402, "V402",
    "a memory-scope trunk is coherent with the binding's pinned host architecture and "
    "every nested ScopedFence shares the outer scope's trunk",
    "marks_scope_arch_cross_trunk OR F::type_t pins a ScopedFence whose trunk contradicts "
    "arch_pin_v<F::source_t> OR F::type_t pins NESTED ScopedFence layers whose trunks "
    "contradict each other (FIXY-FOUND-073): an accel (GPU device — Warp..Gpu) scope on a "
    "CPU-host arch pin (ArchPinned<Arm> or <X86>, whose fence dialect is DMB / mfence and "
    "cannot realize a PTX `.cta`/`.gpu` scope), OR an ARM-shareability scope (Inner/Outer = "
    "DMB ISH/OSH) on a non-ARM host (X86 has no ISH/OSH domain), OR a `ScopedFence<Gpu, "
    "ScopedFence<Inner, T>>`-shaped composition that mixes accel and ARM trunks within one "
    "binding (no host can realize both `.gpu` and `ish` simultaneously). The MemoryScope-axis "
    "mirror of V002's cross-arch intrinsic mixing (MemoryScopeLattice cross-trunk leq = false)",
    "select the architecture at the dispatch boundary and keep each arch's scoped fences in "
    "its own single-target binary (CLAUDE.md §VIII); pin a host arch whose trunk matches the "
    "scope (ArchPinned<Arm> for Inner/Outer; leave the host pin Portable for a GPU-device "
    "scope), drop the contradicting ScopedFence scope, OR flatten the nested ScopedFence "
    "stack to a single trunk-coherent scope",
    "fixy.md §24.2 V402 (V-268, FOUND-073)");
CRUCIBLE_COLLISION_DIAGNOSTIC(T001, "T001",
    "every Capability-usage binding declares Verified or Tested provenance",
    "F::usage_v == UsageMode::Capability AND F::trust_t is trust::Unverified "
    "(the FOUND-034 Biba-safe default).  UsageMode::Capability mints a "
    "non-revocable authorization token that downstream consumers treat as "
    "legitimate proof of authority; a binding whose provenance is Unverified "
    "cannot establish that authority chain — the binding's call path may have "
    "originated from untrusted code that constructed the capability shape "
    "without earning the underlying authorization.  This is the canonical "
    "privilege-escalation pattern: untrusted code mints an authority token, "
    "downstream consumers honor it.  The Trust axis was unread by any §6.8 "
    "rule before FOUND-070, so every Capability binding silently passed "
    "regardless of provenance",
    "engage grant::trust_verified at the binding's mint site (capabilities "
    "minted from cryptographically-verified or proof-witnessed call paths) "
    "OR grant::trust_tested (capabilities exercised under test isolation "
    "with measured behavior).  If the binding genuinely should not assert "
    "any authority, drop UsageMode::Capability and pick Linear / Borrow / "
    "Ghost as appropriate.  Never elevate an Unverified binding to "
    "Capability without engaging Trust at the same site",
    "fixy.md §24.2 T001 (V-FOUND-070)");

#undef CRUCIBLE_COLLISION_DIAGNOSTIC

template <typename F> struct marks_async                  : std::false_type {};
template <typename F> struct marks_ct                     : std::false_type {};
template <typename F> struct marks_fail                   : std::false_type {};
template <typename F> struct marks_fail_error_secret      : std::false_type {};
template <typename F> struct marks_fail_on_secret         : std::false_type {};
template <typename F> struct marks_concurrent_context     : std::false_type {};
template <typename F> struct marks_runtime_ghost_use      : std::false_type {};
template <typename F> struct marks_borrow_capture         : std::false_type {};
template <typename F> struct marks_unscoped_spawn         : std::false_type {};
template <typename F> struct marks_linear_uncleaned_fail  : std::false_type {};
template <typename F> struct marks_replay_required        : std::false_type {};
template <typename F> struct marks_replay_stable          : std::false_type {};

// ── Phase B marker traits (8 new rules) ─────────────────────────────
//
// Source-visible opt-ins.  A binding marks itself as "hot path",
// "federation peer", or "externally observable" via specialization,
// and the corresponding §6.8-family rule fires when the marker is
// combined with an unsound axis value.  This keeps Phase B honest:
// the source-of-truth is the marker, not a hidden compiler pass.
//
// `marks_lifetime_region_unprotected` is the L004 anti-marker — it
// defaults TRUE (the unprotected state).  A binding that has plumbed a
// Permission<Tag> through its call signature specializes the trait to
// `std::false_type`, asserting "I have the proof token".  L004 then
// fires only when (a) Usage::Linear, (b) lifetime is region-tagged,
// AND (c) the binding has NOT specialized away the unprotected
// default.  This makes the rule "opt-out of unprotected" rather than
// "opt-in to protected" — the safer default for a freshly-written
// Fn<...> that no one has reviewed yet.
template <typename F> struct marks_lifetime_region_unprotected : std::true_type {};
template <typename F> struct marks_hot_path                    : std::false_type {};
template <typename F> struct marks_externally_observable       : std::false_type {};
template <typename F> struct marks_federation_peer             : std::false_type {};

// ── FIXY-V-243 hazard-axis marker traits (Agent 10 §4) ──────────────
//
// Default-SAFE opt-ins, specialized by the V-244/V-245/V-246 grant
// headers (and the V-248 singleton walk) when they land.  Until a grant
// opts in, every one defaults to the non-toxic value so no existing
// Fn<...> instantiation reds.  The C001/L006/P003/S001 rules ALSO read a
// shipped V-242 wrapper tier off F::type_t (see the detectors below), so
// they are triggerable today without any grant — the marker is the
// second, grant-driven trigger path.
//
//   marks_aborts                       — grant::ctrl::abort<Rationale> (V-244)
//   marks_indirect_call_not_noexcept   — grant::dispatch::indirect_call<NonNoexcept> (V-245)
//   marks_recurses_unbounded           — grant::dispatch::recurses<> w/o MaxDepth (V-245)
//   marks_thread_local_untagged        — grant::global::thread_local_<> w/o TLSTag (V-246)
//   marks_longjmp_unsafe               — grant::ctrl::longjmp_unsafe<Rationale> (V-244)
//   marks_fork_worker                  — permission_fork worker body (V-087 / V-245)
//   marks_throws                       — grant::ctrl::throws<Family> (V-244)
//   marks_singleton_init_cycle         — V-248 tag-graph closure walk verdict
//   marks_thread_local_atomic          — grant::global::thread_local_<Tag> ×
//                                        atomic MemOrder wrapper (V-249)
template <typename F> struct marks_aborts                     : std::false_type {};
template <typename F> struct marks_indirect_call_not_noexcept : std::false_type {};
template <typename F> struct marks_recurses_unbounded         : std::false_type {};
template <typename F> struct marks_thread_local_untagged      : std::false_type {};
template <typename F> struct marks_longjmp_unsafe             : std::false_type {};
template <typename F> struct marks_fork_worker                : std::false_type {};
template <typename F> struct marks_throws                     : std::false_type {};
template <typename F> struct marks_singleton_init_cycle       : std::false_type {};
template <typename F> struct marks_thread_local_atomic        : std::false_type {};

// ── FIXY-V-260 hardware-axis marker traits (Agent 11 §3.6) ──────────
//
// Default-SAFE opt-ins, specialized by the V-258/V-259 grant-pack
// analysis (and the V-261 source::ArchPinned cross-arch gate) when it
// lands.  Until a grant opts in, every one defaults to the non-toxic
// value so no existing Fn<...> instantiation reds.  The V101 / V201 /
// V202 / V203 / V301 rules ALSO read a shipped V-254/255/256 wrapper
// tier off F::type_t (see the hardware detectors below), so they are
// triggerable today without any grant — these three markers are the
// grant-driven trigger path for the three rules that reason about
// cross-grant VALUE compatibility (which a single type_t wrapper read
// cannot express):
//
//   marks_vendor_isa_inconsistent  — vendor::intrinsic<V, I> pack with
//                                     disagreeing (V, I) (V-258 pack)
//   marks_vendor_cross_arch        — x86-trunk + ARM-trunk intrinsics in
//                                     one binding (V-261 ArchPinned gate)
//   marks_simd_width_exceeds_isa   — simd::width<W> exceeds the bound ISA
//                                     family native width (V-259 pack)
template <typename F> struct marks_vendor_isa_inconsistent    : std::false_type {};
template <typename F> struct marks_vendor_cross_arch          : std::false_type {};
template <typename F> struct marks_simd_width_exceeds_isa     : std::false_type {};

// ── FIXY-V-268 memory-scope-axis marker trait (Agent WMEM §3.6) ─────
//
// Default-SAFE opt-in for V402's grant-driven path.  The V402 rule ALSO
// reads a SHIPPED V-267 ScopedFence trunk off F::type_t against
// arch_pin_v<F::source_t> (the type-readable path, triggerable today —
// see the scope detectors below), so this marker is the SECOND trigger:
// the grant-pack analysis specializes it for the nested-cross-trunk-scope
// case a single type_t read cannot express (two ScopedFence layers from
// DIFFERENT MemoryScope trunks composed in one binding — an accel scope
// AND an ARM-shareability scope on the same value, which has no coherent
// realization on any single architecture).
//
//   marks_scope_arch_cross_trunk — accel-trunk scope AND ARM-trunk scope
//                                   composed in one binding, OR a scope
//                                   whose trunk the grant pack proves
//                                   contradicts the target arch.
template <typename F> struct marks_scope_arch_cross_trunk     : std::false_type {};

template <typename T> struct is_exact_decimal             : std::false_type {};

template <typename T> struct is_borrowed_carrier          : std::false_type {};
template <typename T> struct is_borrowed_carrier<BorrowedRef<T>> : std::true_type {};
template <typename T, typename Source>
struct is_borrowed_carrier<Borrowed<T, Source>> : std::true_type {};

template <typename Row, effects::Effect E>
inline constexpr bool row_has_effect_v = effects::row_contains_v<Row, E>;  // ROW-CONTAINS-OK: generic <Row, E> membership alias, not a Ctx capability check

template <typename F>
inline constexpr bool has_async_v = marks_async<F>::value;

template <typename F>
inline constexpr bool has_ct_v = marks_ct<F>::value;

template <typename F>
inline constexpr bool has_fail_v = marks_fail<F>::value;

template <typename F>
inline constexpr bool fail_error_secret_v = marks_fail_error_secret<F>::value;

template <typename F>
inline constexpr bool classified_v =
    F::security_v == SecLevel::Classified || F::security_v == SecLevel::Secret;

template <typename F>
inline constexpr bool has_borrow_capture_v =
    F::usage_v == UsageMode::Borrow ||
    is_borrowed_carrier<std::remove_cvref_t<typename F::type_t>>::value ||
    marks_borrow_capture<F>::value;

template <typename F>
inline constexpr bool concurrent_context_v =
    has_async_v<F> ||
    row_has_effect_v<typename F::effect_row_t, effects::Effect::Bg> ||
    marks_concurrent_context<F>::value;

template <typename F>
inline constexpr bool session_protocol_v =
    !std::is_same_v<typename F::protocol_t, proto::None>;

template <typename F>
concept I002_OK = !(classified_v<F> && has_fail_v<F> && !fail_error_secret_v<F>);

template <typename F>
concept L002_OK = !(has_borrow_capture_v<F> && has_async_v<F>);

template <typename F>
concept E044_OK = !(has_ct_v<F> && has_async_v<F>);

template <typename F>
concept I003_OK = !(has_ct_v<F> && has_fail_v<F> && marks_fail_on_secret<F>::value);

template <typename F>
concept M012_OK = !(F::mutation_v == MutationMode::Monotonic &&
                    concurrent_context_v<F> &&
                    F::repr_v != ReprKind::Atomic);

template <typename F>
concept P002_OK = !(F::usage_v == UsageMode::Ghost && marks_runtime_ghost_use<F>::value);

template <typename F>
concept I004_OK = !(classified_v<F> && has_async_v<F> &&
                    session_protocol_v<F> && !has_ct_v<F>);

template <typename F>
concept N002_OK = !(is_exact_decimal<std::remove_cvref_t<typename F::type_t>>::value &&
                    F::overflow_v == OverflowMode::Wrap);

template <typename F>
concept L003_OK = !(has_borrow_capture_v<F> && marks_unscoped_spawn<F>::value);

template <typename F>
concept M011_OK = !(F::usage_v == UsageMode::Linear &&
                    has_fail_v<F> &&
                    marks_linear_uncleaned_fail<F>::value);

template <typename F>
concept S010_OK = !(has_ct_v<F> &&
                    !std::is_same_v<typename F::staleness_t, stale::Fresh>);

template <typename F>
concept S011_OK = !(F::usage_v == UsageMode::Capability &&
                    marks_replay_required<F>::value &&
                    !marks_replay_stable<F>::value);

// ── Phase B helper traits ───────────────────────────────────────────

// `lifetime::In<RegionTag>` detector — used by L004.  `lifetime::Static`
// is the program-wide default; any region-tagged lifetime needs proof.
template <typename L> struct is_region_lifetime : std::false_type {};
template <auto RegionTag>
struct is_region_lifetime<lifetime::In<RegionTag>> : std::true_type {};

// `cost::Unstated` / `cost::Unbounded` detector — used by H001/F002.
template <typename C> struct is_unbounded_cost : std::false_type {};
template <> struct is_unbounded_cost<cost::Unstated>  : std::true_type {};
template <> struct is_unbounded_cost<cost::Unbounded> : std::true_type {};

// `pred::True` (no witness) detector — used by H002.  A trivial-true
// refinement on a hot path is the no-witness case the rule catches.
template <typename R> struct is_trivial_refinement : std::false_type {};
template <> struct is_trivial_refinement<pred::True> : std::true_type {};

// ── W001 helper detectors (Phase C wait-strategy axis) ──────────────
//
// `wait_strategy_of<T>` extracts the WaitStrategy enumerator pinned by
// a safety::Wait<Strategy, U> wrapper around T.  For non-Wait types the
// detector reports `has_wait = false` (rule trivially passes).
//
// `is_kernel_wait_v<S>` consults the WaitLattice chain: every strategy
// AT OR BELOW UmwaitC01 involves a kernel/syscall round-trip and
// exceeds the hot-path latency budget (CLAUDE.md §IX latency
// hierarchy):
//   - Block       (0): poll / epoll_wait — 5-20 µs.
//   - Park        (1): pthread_cond_wait / condvar — 3-10 µs.
//   - AcquireWait (2): std::atomic::wait / futex — 1-5 µs.
//   - UmwaitC01   (3): UMWAIT (WAITPKG) — 100-500 ns + wait time;
//                      power-aware but explicitly contraindicated for
//                      hot path per CLAUDE.md §IX ("Not applicable on
//                      our hot path").
// Admissible on the hot path (≤ 40 ns intra-socket budget):
//   - BoundedSpin (4): SpinPause + exponential backoff.
//   - SpinPause   (5): _mm_pause / yield on acquire-load.
//
// FIXY-FOUND-061: pre-fix this used `leq(S, Park)` and under-rejected
// AcquireWait + UmwaitC01 — both syscall-tier strategies that CLAUDE.md
// §IX bans on the hot path.  The W001 diagnostic message and remediation
// text already named all four, but the rule itself caught only two.
// Predicate widened to `leq(S, UmwaitC01)` to match the documented
// contract.
template <typename T> struct wait_strategy_of {
    static constexpr bool has_wait = false;
    // Sentinel value present on the primary template too.  W001/W002
    // concept atoms reference `::value` as a non-type template argument
    // to `is_kernel_wait_v` / `is_active_spin_v`; even though the
    // outer conjunction short-circuits at runtime when `has_wait` is
    // false, GCC 16 concept normalization eagerly substitutes the
    // template argument list of the right-hand variable template under
    // certain instantiation contexts (sentinel-TU re-instantiation in
    // particular).  Providing a sentinel `value` makes the substitution
    // well-formed; the predicate result is irrelevant because the
    // short-circuit chops it off before validate() inspects it.
    static constexpr ::crucible::algebra::lattices::WaitStrategy value =
        ::crucible::algebra::lattices::WaitStrategy::SpinPause;
};
template <::crucible::algebra::lattices::WaitStrategy S, typename U>
struct wait_strategy_of<::crucible::safety::Wait<S, U>> {
    static constexpr bool has_wait = true;
    static constexpr ::crucible::algebra::lattices::WaitStrategy value = S;
};
// Also pierce reference / cv qualifiers — a hot-path return of
// `Wait<Park, T> const&` is just as toxic as a bare `Wait<Park, T>`.
template <typename T>
struct wait_strategy_of<T&> : wait_strategy_of<T> {};
template <typename T>
struct wait_strategy_of<T const> : wait_strategy_of<T> {};
template <typename T>
struct wait_strategy_of<T const&> : wait_strategy_of<T> {};

// WaitLattice ordinal convention (WaitLattice.h L160-162):
//   Block       = 0  (bottom — blockiest)
//   Park        = 1
//   AcquireWait = 2
//   UmwaitC01   = 3
//   BoundedSpin = 4
//   SpinPause   = 5  (top — never blocks)
// `leq(W, UmwaitC01)` is true iff W is at-or-below UmwaitC01 in the
// lattice = W ∈ {Block, Park, AcquireWait, UmwaitC01}.  Those are the
// four tiers that cross a syscall/kernel boundary and exceed the hot-
// path latency budget (CLAUDE.md §IX).
template <::crucible::algebra::lattices::WaitStrategy S>
inline constexpr bool is_kernel_wait_v =
    ::crucible::algebra::lattices::WaitLattice::leq(
        S, ::crucible::algebra::lattices::WaitStrategy::UmwaitC01);

// `is_active_spin_v<S>` is the symmetric counterpart at the TOP of the
// chain.  `leq(BoundedSpin, S)` is true iff BoundedSpin is at-or-below
// S in the lattice = S ∈ {BoundedSpin, SpinPause}.  Those are the two
// strategies that occupy 100% of a CPU core for the duration of the
// wait — pure spin-pause loops with no kernel involvement.  On a Bg
// thread (cold path, scheduler-yielding), an active-spin is the back-
// pressure trap: the core stays busy while no useful work happens, and
// the kernel cannot schedule another runnable thread onto it.
template <::crucible::algebra::lattices::WaitStrategy S>
inline constexpr bool is_active_spin_v =
    ::crucible::algebra::lattices::WaitLattice::leq(
        ::crucible::algebra::lattices::WaitStrategy::BoundedSpin, S);

// ── Phase B per-Fn rule concepts (6 of 8) ────────────────────────────
//
// L005 (alias of two Linears on one region tag) and F001 (frame axis
// disagreement) are PACK-LEVEL rules — they read across a Grants pack,
// not across a single Fn's axes.  Phase B ships them as pack
// metafunctions further below; the Fn-level concept gate trivially
// passes for those two (a single Fn cannot alias itself, and a single
// Fn cannot disagree on a single axis).

template <typename F>
concept L004_OK = !(F::usage_v == UsageMode::Linear &&
                    is_region_lifetime<typename F::lifetime_t>::value &&
                    marks_lifetime_region_unprotected<F>::value);

template <typename F>
concept B001_OK = !(row_has_effect_v<typename F::effect_row_t, effects::Effect::Bg> &&
                    marks_externally_observable<F>::value &&
                    is_unbounded_cost<typename F::cost_t>::value);

template <typename F>
concept H001_OK = !(marks_hot_path<F>::value &&
                    is_unbounded_cost<typename F::cost_t>::value);

template <typename F>
concept H002_OK = !(marks_hot_path<F>::value &&
                    is_trivial_refinement<typename F::refinement_t>::value);

template <typename F>
concept L005_OK = true;  // pack-level; see pack::no_linear_region_alias_v

template <typename F>
concept F001_OK = true;  // pack-level; see pack::frame_axis_consistent_v

template <typename F>
concept H003_OK = !(marks_hot_path<F>::value &&
                    (row_has_effect_v<typename F::effect_row_t, effects::Effect::Alloc> ||
                     row_has_effect_v<typename F::effect_row_t, effects::Effect::IO>) &&
                    is_unbounded_cost<typename F::cost_t>::value);

// H010: HotPath × Row<Bg> contradiction (FIXY-FOUND-063).  HotPath
// declares "≤40 ns intra-socket per CLAUDE.md §IX"; Bg declares
// "background context, Alloc/IO/Block allowed, millisecond latency".
// These are mutually exclusive contexts; marking both is a structural
// contradiction.  H001 / H003 cover the unbounded-cost and Alloc/IO
// subcases, but a HotPath × Bg × cost::Constant × no-Alloc/IO/Block
// Fn slips both — yet is still a context contradiction the type
// system must reject.
template <typename F>
concept H010_OK = !(marks_hot_path<F>::value &&
                    row_has_effect_v<typename F::effect_row_t,
                                     effects::Effect::Bg>);

// P010: Ghost × Row<Alloc|IO|Block> erasure-contract violation (FIXY-FOUND-064).
// Ghost values are erased at codegen — no emitted instructions.  A Ghost
// binding declared with any of the three observable runtime-effect atoms
// (Alloc / IO / Block) contradicts the erasure contract: Alloc emits
// heap-touching code, IO emits syscalls / external observers, Block emits
// blocking primitives.  P002 catches the marker-driven variant
// (marks_runtime_ghost_use specialization); P010 catches the structural
// effect-row read where no marker is engaged.  Structurally parallel to
// H010 (HotPath × Bg) but on the Usage axis instead of the HotPath
// marker — the two rules pin the same erased-vs-emitted shape on
// orthogonal axes.  Bg is deliberately EXCLUDED from P010's catch set —
// Bg classification interacts with HotPath (caught by H010) on a
// different contradiction shape; Ghost × Bg is a separable concern
// reserved for a future P011 if a real production case emerges.
template <typename F>
concept P010_OK = !(F::usage_v == UsageMode::Ghost &&
                    (row_has_effect_v<typename F::effect_row_t,
                                      effects::Effect::Alloc> ||
                     row_has_effect_v<typename F::effect_row_t,
                                      effects::Effect::IO> ||
                     row_has_effect_v<typename F::effect_row_t,
                                      effects::Effect::Block>));

// L007: borrow_capture × Row<Bg> cross-thread lifetime hazard
// (FIXY-FOUND-065). A borrow-capture function takes a borrowed reference
// whose lifetime is tied to the caller's stack; Bg-row implies cross-
// thread execution where the caller's stack may unwind before the
// background thread runs the body. The borrow then dangles.  L002 / L003
// catch the marker-driven variants (marks_async / marks_unscoped_spawn);
// L007 catches the structural effect-row direct-read where no marker is
// engaged. Structurally parallel to L003 on the Bg-row side.  Note that
// the EXISTING legitimate borrow-into-Bg pattern is `permission_fork`
// (CSL parallel rule, fork-join lifetime-scoped) — code wanting that
// pattern uses the explicit fork API, NOT a bare borrow_capture × Bg
// signature.
template <typename F>
concept L007_OK = !(has_borrow_capture_v<F> &&
                    row_has_effect_v<typename F::effect_row_t,
                                     effects::Effect::Bg>);

// T001: Trust::Unverified × UsageMode::Capability rejected (FIXY-FOUND-070).
// A Capability binding mints a non-revocable authorization token; a binding
// whose provenance is Unverified cannot establish the authority chain.
// Closed the Trust-axis no-op gap — before FOUND-070 the Trust axis was
// destructured by Fn but read by ZERO §6.8 rules.  The rule is asymmetric
// (only Unverified rejected; Tested and Verified both establish the
// authority chain — Tested via measured behavior under test isolation,
// Verified via cryptographic / proof-witnessed mint).
template <typename F>
concept T001_OK = !(F::usage_v == UsageMode::Capability &&
                    std::is_same_v<typename F::trust_t,
                                   ::crucible::safety::trust::Unverified>);

template <typename F>
concept F002_OK = !(marks_federation_peer<F>::value &&
                    is_unbounded_cost<typename F::cost_t>::value);

// W001: HotPath × syscall-tier Wait rejected.  Functions marked
// hot-path MUST NOT wrap their type_t in any Wait strategy that
// crosses a kernel/syscall boundary — the rejected set is {Block,
// Park, AcquireWait, UmwaitC01}, all of which exceed the hot-path
// budget (≤ 40 ns intra-socket per CLAUDE.md §IX latency hierarchy).
//
// FIXY-FOUND-061: pre-fix the predicate used `leq(S, Park)` and
// under-rejected AcquireWait + UmwaitC01.  Widened to `leq(S,
// UmwaitC01)` to match the documented contract and the diagnostic
// remediation text.
template <typename F>
concept W001_OK = !(marks_hot_path<F>::value &&
                    wait_strategy_of<typename F::type_t>::has_wait &&
                    is_kernel_wait_v<wait_strategy_of<typename F::type_t>::value>);

// W002: Bg-row × Wait<SpinPause> or Wait<BoundedSpin> rejected.
// Functions whose effect_row carries Effect::Bg MUST NOT wrap their
// type_t in an active-spin Wait strategy.  SpinPause and BoundedSpin
// occupy 100% of a CPU core — the Bg thread is by contract permitted
// to block, so the kernel should be free to schedule another runnable
// thread onto the core while the Bg wait is pending.  Active-spin in
// a Bg row is the back-pressure-trap shape B001 catches one axis over.
template <typename F>
concept W002_OK = !(row_has_effect_v<typename F::effect_row_t, effects::Effect::Bg> &&
                    wait_strategy_of<typename F::type_t>::has_wait &&
                    is_active_spin_v<wait_strategy_of<typename F::type_t>::value>);

// ── FIXY-V-091 F-family detector — FpModePinned wrapper inspection ──
//
// `wraps_fp_axis_mode<AxisMode, T>` is true iff T is structurally
// `safety::FpModePinned<AxisMode, U>` for some U.  ONE generic detector
// covers all 11 FP sub-axes because `auto AxisMode` binds to the actual
// enum type at the call site — a partial-spec match requires the
// FpModePinned's first template arg to have the SAME enum type AND
// the SAME value as the AxisMode supplied at the consumer site.
//
// Probing for `<FpReassociate::UnrestrictedRewrite, FpRoundingPinned<...>>`
// falls back to the false_type primary template — the inner FpRoundingPinned
// pins `Mode` of type FpRounding, not FpReassociate, so the partial spec
// substitution rejects.  No per-axis specialization needed.
template <auto AxisMode, typename T>
struct wraps_fp_axis_mode : std::false_type {};

template <auto AxisMode, typename U>
struct wraps_fp_axis_mode<AxisMode,
    ::crucible::safety::FpModePinned<AxisMode, U>> : std::true_type {};

// Pierce reference / cv qualifiers — a hot-path return of
// `FpReassociatePinned<UnrestrictedRewrite, T> const&` is just as toxic
// as the bare wrapper (mirrors wait_strategy_of's CV-piercing).
template <auto AxisMode, typename T>
struct wraps_fp_axis_mode<AxisMode, T&>
    : wraps_fp_axis_mode<AxisMode, T> {};
template <auto AxisMode, typename T>
struct wraps_fp_axis_mode<AxisMode, T const>
    : wraps_fp_axis_mode<AxisMode, T> {};
template <auto AxisMode, typename T>
struct wraps_fp_axis_mode<AxisMode, T const&>
    : wraps_fp_axis_mode<AxisMode, T> {};

template <auto AxisMode, typename T>
inline constexpr bool wraps_fp_axis_mode_v = wraps_fp_axis_mode<AxisMode, T>::value;

// F101: Replay-required × FpReassociate non-strict rejected (FOUND-074).
// IEEE 754 default (Forbidden) is the only setting compatible with bit-
// exact replay across the cross-vendor CI matrix.  Pre-FOUND-074 the
// rule rejected ONLY UnrestrictedRewrite; BoundedTreeDepth slipped
// through despite being equally non-deterministic across vendors —
// vendors agree on the log-N tree DEPTH but disagree on the per-level
// LANE ASSIGNMENTS (NVIDIA warp-shuffle reductions and AMD wavefront
// reductions use different operand orderings within the same tree
// shape).  Same source → different FP bits across vendors, defeating
// bit-exact replay.  F101 now rejects ANY non-Forbidden setting under
// replay_required; F103 (CT-axis) remains narrow because BoundedTreeDepth
// has data-independent topology and is CT-safe.
template <typename F>
concept F101_OK = !(marks_replay_required<F>::value &&
                    (wraps_fp_axis_mode_v<
                        ::crucible::safety::FpReassociate::UnrestrictedRewrite,
                        typename F::type_t> ||
                     wraps_fp_axis_mode_v<
                        ::crucible::safety::FpReassociate::BoundedTreeDepth,
                        typename F::type_t>));

// F102: Replay-required × FpContract<Fast> rejected.  Cross-statement
// FMA folding picks DIFFERENT contraction boundaries per vendor; same
// source → different bits across the cross-vendor numerics CI matrix.
template <typename F>
concept F102_OK = !(marks_replay_required<F>::value &&
                    wraps_fp_axis_mode_v<
                        ::crucible::safety::FpContract::Fast,
                        typename F::type_t>);

// F103: CT × FpReassociate<UnrestrictedRewrite> rejected.  Reassociation
// introduces data-dependent reduction-tree topology (compiler picks the
// tree based on operand magnitudes / constant-foldability), violating
// timing-independence in a constant-time region.
template <typename F>
concept F103_OK = !(has_ct_v<F> &&
                    wraps_fp_axis_mode_v<
                        ::crucible::safety::FpReassociate::UnrestrictedRewrite,
                        typename F::type_t>);

// F104: CT × FpDenormalInput<HonorDenormals> rejected.  DAZ=0 introduces
// a 30-100× cycle-count delta when the input IS denormal — textbook FP
// timing side-channel.  Crypto / CT paths PIN DenormalsAreZero so the
// cycle count is data-independent.
template <typename F>
concept F104_OK = !(has_ct_v<F> &&
                    wraps_fp_axis_mode_v<
                        ::crucible::safety::FpDenormalInput::HonorDenormals,
                        typename F::type_t>);

// F105: CT × FpFtz<PreserveSubnormals> rejected.  Output-side dual of
// F104 — FTZ=0 introduces the same 30-100× slowdown PRODUCING denormal
// outputs.  Result-magnitude leaks through cycle count.
template <typename F>
concept F105_OK = !(has_ct_v<F> &&
                    wraps_fp_axis_mode_v<
                        ::crucible::safety::FpFtz::PreserveSubnormals,
                        typename F::type_t>);

// ── FIXY-V-243 hazard-axis detectors (ControlFlow / Stdio tiers) ────
//
// control_flow_tier_of<T> / stdio_tier_of<T> extract the tier pinned by
// a shipped V-242 ControlFlowPinned<Tier, U> / StdioPinned<Tier, U>
// wrapper around T — mirrors wait_strategy_of's design (sentinel value on
// the primary template + CV/reference piercing).  For a non-wrapper T,
// `has_*` is false and the rule reading the tier trivially passes via the
// `&&` short-circuit in cf_at_or_above_v / stdio_at_or_above_v.  The
// sentinel `value` lives on the primary template too, so GCC-16 concept
// normalization can substitute the NTTP argument of *_at_or_above_v even
// when the short-circuit chops the predicate off (the same eager-
// substitution workaround wait_strategy_of documents).
template <typename T> struct control_flow_tier_of {
    static constexpr bool has_cf = false;
    static constexpr ::crucible::algebra::lattices::ControlFlow value =
        ::crucible::algebra::lattices::ControlFlow::Pure;
};
template <::crucible::algebra::lattices::ControlFlow Tier, typename U>
struct control_flow_tier_of<::crucible::safety::ControlFlowPinned<Tier, U>> {
    static constexpr bool has_cf = true;
    static constexpr ::crucible::algebra::lattices::ControlFlow value = Tier;
};
template <typename T> struct control_flow_tier_of<T&>       : control_flow_tier_of<T> {};
template <typename T> struct control_flow_tier_of<T const>  : control_flow_tier_of<T> {};
template <typename T> struct control_flow_tier_of<T const&> : control_flow_tier_of<T> {};

template <typename T> struct stdio_tier_of {
    static constexpr bool has_stdio = false;
    static constexpr ::crucible::algebra::lattices::Stdio value =
        ::crucible::algebra::lattices::Stdio::NoStdio;
};
template <::crucible::algebra::lattices::Stdio Tier, typename U>
struct stdio_tier_of<::crucible::safety::StdioPinned<Tier, U>> {
    static constexpr bool has_stdio = true;
    static constexpr ::crucible::algebra::lattices::Stdio value = Tier;
};
template <typename T> struct stdio_tier_of<T&>       : stdio_tier_of<T> {};
template <typename T> struct stdio_tier_of<T const>  : stdio_tier_of<T> {};
template <typename T> struct stdio_tier_of<T const&> : stdio_tier_of<T> {};

// cf_at_or_above_v<Floor, T>: wrapper present AND its tier is at-or-above
// `Floor` on the ControlFlow chain (leq(Floor, tier) ⇔ Floor ⊑ tier).
// ControlFlow is a capability-CEILING axis: higher ordinal = MORE
// hazardous, so "tier ≥ Floor" reads as "at least this dangerous".
template <::crucible::algebra::lattices::ControlFlow Floor, typename T>
inline constexpr bool cf_at_or_above_v =
    control_flow_tier_of<T>::has_cf &&
    ::crucible::algebra::lattices::ControlFlowLattice::leq(
        Floor, control_flow_tier_of<T>::value);

template <::crucible::algebra::lattices::Stdio Floor, typename T>
inline constexpr bool stdio_at_or_above_v =
    stdio_tier_of<T>::has_stdio &&
    ::crucible::algebra::lattices::StdioLattice::leq(
        Floor, stdio_tier_of<T>::value);

// ── FIXY-V-243 hazard-axis rule concepts (8 of 8) ───────────────────

// C001: marks_aborts × ControlFlow tier < AbortOnly.  A function
// declaring it may abort (grant::ctrl::abort<Rationale>) MUST carry a
// ControlFlow tier that witnesses the escape (≥ AbortOnly).  "Claims
// abort but typed Pure" is the ControlFlow↔escape inconsistency.
template <typename F>
concept C001_OK = !(marks_aborts<F>::value &&
                    !cf_at_or_above_v<
                        ::crucible::algebra::lattices::ControlFlow::AbortOnly,
                        typename F::type_t>);

// D001: indirect-call grant whose callable is NOT noexcept (Scenario A).
template <typename F>
concept D001_OK = !marks_indirect_call_not_noexcept<F>::value;

// D002: recursion grant declared without a bounded MaxDepth.
template <typename F>
concept D002_OK = !marks_recurses_unbounded<F>::value;

// G001: thread_local grant declared without a TLSTag.
template <typename F>
concept G001_OK = !marks_thread_local_untagged<F>::value;

// L006: Usage::Linear × ControlFlow tier ≥ MayLongjmp (or the
// marks_longjmp_unsafe marker).  longjmp SKIPS destructors; a Linear
// resource in scope would leak / dangle across the non-local jump.
template <typename F>
concept L006_OK = !(F::usage_v == UsageMode::Linear &&
                    (marks_longjmp_unsafe<F>::value ||
                     cf_at_or_above_v<
                         ::crucible::algebra::lattices::ControlFlow::MayLongjmp,
                         typename F::type_t>));

// P003: marks_fork_worker × ControlFlow tier ≥ ThrowOnly (or the
// marks_throws marker).  A throw inside a jthread fork body crosses no
// thread boundary; under -fno-exceptions it is std::terminate.
template <typename F>
concept P003_OK = !(marks_fork_worker<F>::value &&
                    (marks_throws<F>::value ||
                     cf_at_or_above_v<
                         ::crucible::algebra::lattices::ControlFlow::ThrowOnly,
                         typename F::type_t>));

// S001: marks_hot_path × Stdio tier ≥ BufferedWrite.  Hot-path code
// (TraceRing / Arena / KernelCache) MUST NOT do stdio (CLAUDE.md §XII).
template <typename F>
concept S001_OK = !(marks_hot_path<F>::value &&
                    stdio_at_or_above_v<
                        ::crucible::algebra::lattices::Stdio::BufferedWrite,
                        typename F::type_t>);

// S004: Meyers-singleton init-dependency cycle (V-248 walk verdict).
template <typename F>
concept S004_OK = !marks_singleton_init_cycle<F>::value;

// G002: thread_local storage paired with an atomic memory-order wrapper —
// a per-thread atomic orders against no peer, so the combination is a
// category error (V-249 / Scenario E).
template <typename F>
concept G002_OK = !marks_thread_local_atomic<F>::value;

// ── FIXY-V-260 hardware-axis detectors (Hw / BarrierGuarded /
//    SimdWidthPinned tier extraction off F::type_t) ──────────────────
//
// Mirror the control_flow_tier_of / stdio_tier_of design: a sentinel
// value on the primary template + CV / reference piercing, plus a
// partial spec that reads the pinned tier from the V-254/255/256
// wrapper.  For a non-wrapper T, has_* is false and the at_or_above
// predicate short-circuits to false (rule trivially passes).
template <typename T> struct hw_tier_of {
    static constexpr bool has_hw = false;
    static constexpr ::crucible::algebra::lattices::HwInstruction value =
        ::crucible::algebra::lattices::HwInstruction::NoneAllowed;
};
template <::crucible::algebra::lattices::HwInstruction Tier, typename U>
struct hw_tier_of<::crucible::safety::Hw<Tier, U>> {
    static constexpr bool has_hw = true;
    static constexpr ::crucible::algebra::lattices::HwInstruction value = Tier;
};
template <typename T> struct hw_tier_of<T&>       : hw_tier_of<T> {};
template <typename T> struct hw_tier_of<T const>  : hw_tier_of<T> {};
template <typename T> struct hw_tier_of<T const&> : hw_tier_of<T> {};

template <typename T> struct barrier_tier_of {
    static constexpr bool has_barrier = false;
    static constexpr ::crucible::algebra::lattices::BarrierStrength value =
        ::crucible::algebra::lattices::BarrierStrength::None;
};
template <::crucible::algebra::lattices::BarrierStrength Tier, typename U>
struct barrier_tier_of<::crucible::safety::BarrierGuarded<Tier, U>> {
    static constexpr bool has_barrier = true;
    static constexpr ::crucible::algebra::lattices::BarrierStrength value = Tier;
};
// FIXY-V-268: pierce a ScopedFence sibling wrapper so the V401 scope×strength
// rule finds a nested BarrierGuarded tier regardless of nesting order
// (`ScopedFence<Gpu, BarrierGuarded<AcqRel, T>>` reads AcqRel, not has_barrier
// = false).  Safe for the existing V301 rule: a bare `BarrierGuarded<SeqCst,T>`
// still matches the BarrierGuarded partial spec above, not this one.
template <::crucible::algebra::lattices::MemoryScope S, typename U>
struct barrier_tier_of<::crucible::safety::ScopedFence<S, U>> : barrier_tier_of<U> {};
template <typename T> struct barrier_tier_of<T&>       : barrier_tier_of<T> {};
template <typename T> struct barrier_tier_of<T const>  : barrier_tier_of<T> {};
template <typename T> struct barrier_tier_of<T const&> : barrier_tier_of<T> {};

template <typename T> struct simd_isa_of {
    static constexpr bool has_simd = false;
    static constexpr ::crucible::algebra::lattices::SimdIsa value =
        ::crucible::algebra::lattices::SimdIsa::Scalar;
};
template <::crucible::algebra::lattices::SimdIsa W, typename U>
struct simd_isa_of<::crucible::safety::SimdWidthPinned<W, U>> {
    static constexpr bool has_simd = true;
    static constexpr ::crucible::algebra::lattices::SimdIsa value = W;
};
template <typename T> struct simd_isa_of<T&>       : simd_isa_of<T> {};
template <typename T> struct simd_isa_of<T const>  : simd_isa_of<T> {};
template <typename T> struct simd_isa_of<T const&> : simd_isa_of<T> {};

// hw_at_or_above_v<Floor, T>: wrapper present AND its tier is at-or-above
// Floor on the HwInstruction capability chain (higher ordinal = MORE
// hazardous, so "≥ Floor" reads as "at least this dangerous").
template <::crucible::algebra::lattices::HwInstruction Floor, typename T>
inline constexpr bool hw_at_or_above_v =
    hw_tier_of<T>::has_hw &&
    ::crucible::algebra::lattices::HwInstructionLattice::leq(
        Floor, hw_tier_of<T>::value);

template <::crucible::algebra::lattices::BarrierStrength Floor, typename T>
inline constexpr bool barrier_at_or_above_v =
    barrier_tier_of<T>::has_barrier &&
    ::crucible::algebra::lattices::BarrierStrengthLattice::leq(
        Floor, barrier_tier_of<T>::value);

// simd_isa_pins_specific_vector_v<T>: a SimdWidthPinned wrapper present
// whose tier is a SPECIFIC vector ISA — neither Scalar (no SIMD, runs
// everywhere identically) nor Portable (⊤, ISA-agnostic).  Both poles
// are replay-safe; any concrete trunk point pins a lane count that
// reorders FP reductions across ISAs.
template <typename T>
inline constexpr bool simd_isa_pins_specific_vector_v =
    simd_isa_of<T>::has_simd &&
    simd_isa_of<T>::value != ::crucible::algebra::lattices::SimdIsa::Scalar &&
    simd_isa_of<T>::value != ::crucible::algebra::lattices::SimdIsa::Portable;

// ── FIXY-V-268 memory-scope-axis detectors (ScopedFence tier extraction
//    off F::type_t + host-arch contradiction) ───────────────────────
//
// scope_tier_of mirrors barrier_tier_of: a sentinel on the primary
// template + CV / reference piercing + a partial spec reading the pinned
// MemoryScope from the V-267 ScopedFence carrier.  It ALSO pierces the
// sibling hardware-band wrappers (BarrierGuarded / Hw / SimdWidthPinned)
// so V401 finds the scope even when a barrier (or other hw-band wrapper)
// nests outside the ScopedFence — the dual of barrier_tier_of's
// ScopedFence-piercing above.  For a non-ScopedFence T, has_scope is false
// and scope_at_or_above_v short-circuits to false (rule trivially passes).
template <typename T> struct scope_tier_of {
    static constexpr bool has_scope = false;
    static constexpr ::crucible::algebra::lattices::MemoryScope value =
        ::crucible::algebra::lattices::MemoryScope::Thread;
};
template <::crucible::algebra::lattices::MemoryScope S, typename U>
struct scope_tier_of<::crucible::safety::ScopedFence<S, U>> {
    static constexpr bool has_scope = true;
    static constexpr ::crucible::algebra::lattices::MemoryScope value = S;
};
template <::crucible::algebra::lattices::BarrierStrength Tier, typename U>
struct scope_tier_of<::crucible::safety::BarrierGuarded<Tier, U>> : scope_tier_of<U> {};
template <::crucible::algebra::lattices::HwInstruction Tier, typename U>
struct scope_tier_of<::crucible::safety::Hw<Tier, U>> : scope_tier_of<U> {};
template <::crucible::algebra::lattices::SimdIsa W, typename U>
struct scope_tier_of<::crucible::safety::SimdWidthPinned<W, U>> : scope_tier_of<U> {};
template <typename T> struct scope_tier_of<T&>       : scope_tier_of<T> {};
template <typename T> struct scope_tier_of<T const>  : scope_tier_of<T> {};
template <typename T> struct scope_tier_of<T const&> : scope_tier_of<T> {};

// scope_at_or_above_v<Floor, T>: a ScopedFence is present AND its pinned
// scope SUBSUMES Floor on the MemoryScopeLattice partial order
// (leq(Floor, value) — "the publish scope is at-or-above the floor").  With
// Floor = Cluster (FIXY-FOUND-062 widening), {Cluster, Gpu, System} satisfy
// (cross-CTA-or-wider visibility — the silent-weak-memory-race set on
// Hopper thread-block-cluster and beyond); Cta / Warp and every ARM-trunk
// scope do NOT (cross-trunk leq is false), so the V401 rule fires ONLY for
// genuinely cross-CTA-or-wider accel-trunk scopes.
template <::crucible::algebra::lattices::MemoryScope Floor, typename T>
inline constexpr bool scope_at_or_above_v =
    scope_tier_of<T>::has_scope &&
    ::crucible::algebra::lattices::MemoryScopeLattice::leq(
        Floor, scope_tier_of<T>::value);

// scope_contradicts_host_arch(scope, arch): the V402 trunk-vs-host-arch
// incoherence predicate, reusing the V-265 trunk classifiers.  A Portable
// host pin (or a non-arch-pinned source, which arch_pin_v maps to Portable)
// never contradicts; the shared sentinels Thread (no fence) and System
// (full-system / `.sys` / DMB SY) are realizable on any host.  An ARM-
// shareability scope (Inner/Outer = DMB ISH/OSH) contradicts a non-ARM host
// (x86 has no ISH/OSH domain); an accel-trunk scope (Warp..Gpu = a PTX
// device scope) contradicts ANY concrete CPU-host pin (the host fence
// dialect — mfence / DMB — cannot realize a `.cta`/`.gpu` scope; ArchTag
// carries no GPU trunk, so a GPU-device scope is only coherent with a
// Portable host pin).
[[nodiscard]] constexpr bool scope_contradicts_host_arch(
        ::crucible::algebra::lattices::MemoryScope scope,
        ::crucible::safety::source::ArchTag arch) noexcept {
    using MS = ::crucible::algebra::lattices::MemoryScope;
    using AT = ::crucible::safety::source::ArchTag;
    if (arch == AT::Portable) return false;
    if (scope == MS::Thread || scope == MS::System) return false;
    if (::crucible::algebra::lattices::mem_scope_is_arm(scope)) {
        return arch != AT::Arm;
    }
    if (::crucible::algebra::lattices::mem_scope_is_accel(scope)) {
        return true;
    }
    return false;
}

// scope_arch_cross_trunk_v<F>: the type-readable V402 trigger — F::type_t
// pins a ScopedFence whose trunk contradicts arch_pin_v<F::source_t>.
template <typename F>
inline constexpr bool scope_arch_cross_trunk_v =
    scope_tier_of<typename F::type_t>::has_scope &&
    scope_contradicts_host_arch(
        scope_tier_of<typename F::type_t>::value,
        ::crucible::safety::arch_pin_v<typename F::source_t>);

// ── FIXY-FOUND-073: nested-cross-trunk ScopedFence detector ──────────
//
// inner_scope_tier_of<T>: peer to scope_tier_of, but extracts the FIRST
// nested ScopedFence WITHIN the outer ScopedFence's payload — i.e. the
// inner trunk of a `ScopedFence<S_outer, ... ScopedFence<S_inner, U> ...>`
// stack.  Pierces the same sibling wrappers (BarrierGuarded / Hw /
// SimdWidthPinned) AND CV / reference qualifiers as scope_tier_of.
//
// For NON-NESTED ScopedFence (outer scope wrapping a non-ScopedFence
// payload — the common case), has_inner_scope is false.  For genuinely
// nested ScopedFence stacks, the inner partial spec fires and exposes
// the SECOND (innermost-visible) trunk for cross-trunk comparison.
template <typename T> struct inner_scope_tier_of {
    static constexpr bool has_inner_scope = false;
    static constexpr ::crucible::algebra::lattices::MemoryScope value =
        ::crucible::algebra::lattices::MemoryScope::Thread;
};
// OUTER-level sibling piercing — when a BarrierGuarded / Hw /
// SimdWidthPinned wraps the entire nested-ScopedFence stack, recurse
// past it so the nested-ScopedFence detection still fires.  Dual of
// the scope_tier_of sibling piercing above; required because real
// V402-triggering bindings are typically `BarrierGuarded<Tier,
// ScopedFence<S_outer, ScopedFence<S_inner, U>>>`-shaped (the barrier
// pins the strength, the nested ScopedFences pin the scope trunks).
template <::crucible::algebra::lattices::BarrierStrength Tier, typename U>
struct inner_scope_tier_of<::crucible::safety::BarrierGuarded<Tier, U>>
    : inner_scope_tier_of<U> {};
template <::crucible::algebra::lattices::HwInstruction Tier, typename U>
struct inner_scope_tier_of<::crucible::safety::Hw<Tier, U>>
    : inner_scope_tier_of<U> {};
template <::crucible::algebra::lattices::SimdIsa W, typename U>
struct inner_scope_tier_of<::crucible::safety::SimdWidthPinned<W, U>>
    : inner_scope_tier_of<U> {};
// Outer ScopedFence wrapping an INNER ScopedFence (the nested case the
// shallow scope_tier_of missed).
template <::crucible::algebra::lattices::MemoryScope S_outer,
          ::crucible::algebra::lattices::MemoryScope S_inner,
          typename U>
struct inner_scope_tier_of<::crucible::safety::ScopedFence<S_outer,
                              ::crucible::safety::ScopedFence<S_inner, U>>> {
    static constexpr bool has_inner_scope = true;
    static constexpr ::crucible::algebra::lattices::MemoryScope value = S_inner;
};
// Sibling-wrapper piercing INSIDE the outer ScopedFence's payload so
// `ScopedFence<S_outer, BarrierGuarded<Tier, ScopedFence<S_inner, U>>>`
// (and the Hw / SimdWidthPinned analogues) still surface the inner
// trunk.  Without these, an interposed barrier/hw/width band silently
// hides the nested trunk.
template <::crucible::algebra::lattices::MemoryScope S_outer,
          ::crucible::algebra::lattices::BarrierStrength Tier,
          typename U>
struct inner_scope_tier_of<::crucible::safety::ScopedFence<S_outer,
                              ::crucible::safety::BarrierGuarded<Tier, U>>>
    : inner_scope_tier_of<::crucible::safety::ScopedFence<S_outer, U>> {};
template <::crucible::algebra::lattices::MemoryScope S_outer,
          ::crucible::algebra::lattices::HwInstruction Tier,
          typename U>
struct inner_scope_tier_of<::crucible::safety::ScopedFence<S_outer,
                              ::crucible::safety::Hw<Tier, U>>>
    : inner_scope_tier_of<::crucible::safety::ScopedFence<S_outer, U>> {};
template <::crucible::algebra::lattices::MemoryScope S_outer,
          ::crucible::algebra::lattices::SimdIsa W,
          typename U>
struct inner_scope_tier_of<::crucible::safety::ScopedFence<S_outer,
                              ::crucible::safety::SimdWidthPinned<W, U>>>
    : inner_scope_tier_of<::crucible::safety::ScopedFence<S_outer, U>> {};
template <typename T> struct inner_scope_tier_of<T&>       : inner_scope_tier_of<T> {};
template <typename T> struct inner_scope_tier_of<T const>  : inner_scope_tier_of<T> {};
template <typename T> struct inner_scope_tier_of<T const&> : inner_scope_tier_of<T> {};

// scopes_cross_trunk(a, b): two MemoryScope values land in CONTRADICTING
// trunks — one accel-trunk and one ARM-trunk, OR vice versa.  Shared
// sentinels Thread and System belong to neither trunk and never
// contradict.  Reuses the V-265 trunk classifiers; cross-trunk leq on
// MemoryScopeLattice is false in both directions, but this predicate
// answers the simpler "are the trunks DIFFERENT and both NON-SHARED"
// question (no host-arch in play — the contradiction is internal to
// the type stack).
[[nodiscard]] constexpr bool scopes_cross_trunk(
        ::crucible::algebra::lattices::MemoryScope a,
        ::crucible::algebra::lattices::MemoryScope b) noexcept {
    using MS = ::crucible::algebra::lattices::MemoryScope;
    if (a == MS::Thread || a == MS::System) return false;
    if (b == MS::Thread || b == MS::System) return false;
    const bool a_accel = ::crucible::algebra::lattices::mem_scope_is_accel(a);
    const bool b_accel = ::crucible::algebra::lattices::mem_scope_is_accel(b);
    const bool a_arm   = ::crucible::algebra::lattices::mem_scope_is_arm(a);
    const bool b_arm   = ::crucible::algebra::lattices::mem_scope_is_arm(b);
    return (a_accel && b_arm) || (a_arm && b_accel);
}

// nested_scope_cross_trunk_v<T>: a nested ScopedFence stack whose outer
// and inner trunks contradict.  This is the V402 detector the shallow
// scope_arch_cross_trunk_v missed — the contradiction is INTERNAL to
// the type and independent of host arch (no `.gpu` and `.ish` can
// coexist in one binding on any host).
template <typename T>
inline constexpr bool nested_scope_cross_trunk_v =
    scope_tier_of<T>::has_scope &&
    inner_scope_tier_of<T>::has_inner_scope &&
    scopes_cross_trunk(scope_tier_of<T>::value,
                       inner_scope_tier_of<T>::value);

// ── FIXY-V-260 hardware-axis rule concepts (8 of 8) ─────────────────

// V001 / V002 / V102: grant-pack rules — default-SAFE markers the
// V-258 / V-259 / V-261 grant-pack analysis specializes (cross-grant
// VALUE compatibility a single type_t wrapper read cannot express).
template <typename F>
concept V001_OK = !marks_vendor_isa_inconsistent<F>::value;
template <typename F>
concept V002_OK = !marks_vendor_cross_arch<F>::value;
template <typename F>
concept V102_OK = !marks_simd_width_exceeds_isa<F>::value;

// V101: replay-required × SimdWidthPinned pins a specific vector ISA.
template <typename F>
concept V101_OK = !(marks_replay_required<F>::value &&
                    simd_isa_pins_specific_vector_v<typename F::type_t>);

// V201: HotPath × Hw tier ≥ NonDeterministicTsc (rdtsc / privileged).
template <typename F>
concept V201_OK = !(marks_hot_path<F>::value &&
                    hw_at_or_above_v<
                        ::crucible::algebra::lattices::HwInstruction::NonDeterministicTsc,
                        typename F::type_t>);

// V202: Hw tier == PrivilegedMsr without an Init-context row.
template <typename F>
concept V202_OK = !(hw_tier_of<typename F::type_t>::has_hw &&
                    hw_tier_of<typename F::type_t>::value ==
                        ::crucible::algebra::lattices::HwInstruction::PrivilegedMsr &&
                    !row_has_effect_v<typename F::effect_row_t, effects::Effect::Init>);

// V203: replay-required × Hw tier ≥ NonDeterministicTsc (rdtsc nondeterminism).
template <typename F>
concept V203_OK = !(marks_replay_required<F>::value &&
                    hw_at_or_above_v<
                        ::crucible::algebra::lattices::HwInstruction::NonDeterministicTsc,
                        typename F::type_t>);

// V301: HotPath × BarrierStrength tier ≥ SeqCst (full fence on hot path).
template <typename F>
concept V301_OK = !(marks_hot_path<F>::value &&
                    barrier_at_or_above_v<
                        ::crucible::algebra::lattices::BarrierStrength::SeqCst,
                        typename F::type_t>);

// ── FIXY-V-268 memory-scope-axis rule concepts (2 of 2) ─────────────

// V401: scope ⊒ Cluster × BarrierStrength ⊏ AcqRel.  A cross-CTA-or-wider
// publish scope composed with a barrier weaker than acquire-release is an
// under-fenced publication (visibility widened, ordering never established).
// FIXY-FOUND-062 widened the threshold from Gpu to Cluster — Hopper
// thread-block-cluster publishes are cross-CTA and exhibit the same
// silent-weak-memory race as Gpu publishes when guarded by a sub-AcqRel
// barrier.  The widening catches {Cluster, Gpu, System} in the accel trunk
// (cross-trunk leq false leaves the ARM trunk untouched).
template <typename F>
concept V401_OK = !(scope_at_or_above_v<
                        ::crucible::algebra::lattices::MemoryScope::Cluster,
                        typename F::type_t> &&
                    !barrier_at_or_above_v<
                        ::crucible::algebra::lattices::BarrierStrength::AcqRel,
                        typename F::type_t>);

// V402: scope-trunk × host-arch cross-trunk.  Three trigger paths:
//   (1) Type-readable HOST-vs-OUTER: ScopedFence trunk on F::type_t
//       contradicts arch_pin_v<F::source_t>.
//   (2) Type-readable OUTER-vs-INNER: a nested ScopedFence stack whose
//       outer and inner trunks contradict (FIXY-FOUND-073 — the
//       internal-to-type contradiction the host-vs-outer detector
//       missed; e.g. `.gpu`-trunk wrapping an `ish`-trunk inner has no
//       coherent realization on ANY host).
//   (3) Grant-driven marker: marks_scope_arch_cross_trunk for the
//       cross-grant-value cases a single type read cannot express.
template <typename F>
concept V402_OK = !(marks_scope_arch_cross_trunk<F>::value ||
                    scope_arch_cross_trunk_v<F> ||
                    nested_scope_cross_trunk_v<typename F::type_t>);

template <typename F>
concept AllRulesOK =
    I002_OK<F> && L002_OK<F> && E044_OK<F> && I003_OK<F> &&
    M012_OK<F> && P002_OK<F> && I004_OK<F> && N002_OK<F> &&
    L003_OK<F> && M011_OK<F> && S010_OK<F> && S011_OK<F> &&
    L004_OK<F> && B001_OK<F> && H001_OK<F> && H002_OK<F> &&
    L005_OK<F> && F001_OK<F> && H003_OK<F> && F002_OK<F> &&
    W001_OK<F> && W002_OK<F> &&
    F101_OK<F> && F102_OK<F> && F103_OK<F> && F104_OK<F> && F105_OK<F> &&
    C001_OK<F> && D001_OK<F> && D002_OK<F> && G001_OK<F> &&
    L006_OK<F> && P003_OK<F> && S001_OK<F> && S004_OK<F> &&
    G002_OK<F> &&
    V001_OK<F> && V002_OK<F> && V101_OK<F> && V102_OK<F> &&
    V201_OK<F> && V202_OK<F> && V203_OK<F> && V301_OK<F> &&
    V401_OK<F> && V402_OK<F> &&
    H010_OK<F> && P010_OK<F> && L007_OK<F> && T001_OK<F>;

template <typename F>
[[nodiscard]] consteval RuleCode first_failure() noexcept {
    if constexpr (!I002_OK<F>) {
        return RuleCode::I002;
    } else if constexpr (!L002_OK<F>) {
        return RuleCode::L002;
    } else if constexpr (!E044_OK<F>) {
        return RuleCode::E044;
    } else if constexpr (!I003_OK<F>) {
        return RuleCode::I003;
    } else if constexpr (!M012_OK<F>) {
        return RuleCode::M012;
    } else if constexpr (!P002_OK<F>) {
        return RuleCode::P002;
    } else if constexpr (!I004_OK<F>) {
        return RuleCode::I004;
    } else if constexpr (!N002_OK<F>) {
        return RuleCode::N002;
    } else if constexpr (!L003_OK<F>) {
        return RuleCode::L003;
    } else if constexpr (!M011_OK<F>) {
        return RuleCode::M011;
    } else if constexpr (!S010_OK<F>) {
        return RuleCode::S010;
    } else if constexpr (!S011_OK<F>) {
        return RuleCode::S011;
    } else if constexpr (!L004_OK<F>) {
        return RuleCode::L004;
    } else if constexpr (!B001_OK<F>) {
        return RuleCode::B001;
    } else if constexpr (!H001_OK<F>) {
        return RuleCode::H001;
    } else if constexpr (!H002_OK<F>) {
        return RuleCode::H002;
    } else if constexpr (!L005_OK<F>) {
        return RuleCode::L005;
    } else if constexpr (!F001_OK<F>) {
        return RuleCode::F001;
    } else if constexpr (!H003_OK<F>) {
        return RuleCode::H003;
    } else if constexpr (!F002_OK<F>) {
        return RuleCode::F002;
    } else if constexpr (!W001_OK<F>) {
        return RuleCode::W001;
    } else if constexpr (!W002_OK<F>) {
        return RuleCode::W002;
    } else if constexpr (!F101_OK<F>) {
        return RuleCode::F101;
    } else if constexpr (!F102_OK<F>) {
        return RuleCode::F102;
    } else if constexpr (!F103_OK<F>) {
        return RuleCode::F103;
    } else if constexpr (!F104_OK<F>) {
        return RuleCode::F104;
    } else if constexpr (!F105_OK<F>) {
        return RuleCode::F105;
    } else if constexpr (!C001_OK<F>) {
        return RuleCode::C001;
    } else if constexpr (!D001_OK<F>) {
        return RuleCode::D001;
    } else if constexpr (!D002_OK<F>) {
        return RuleCode::D002;
    } else if constexpr (!G001_OK<F>) {
        return RuleCode::G001;
    } else if constexpr (!L006_OK<F>) {
        return RuleCode::L006;
    } else if constexpr (!P003_OK<F>) {
        return RuleCode::P003;
    } else if constexpr (!S001_OK<F>) {
        return RuleCode::S001;
    } else if constexpr (!S004_OK<F>) {
        return RuleCode::S004;
    } else if constexpr (!G002_OK<F>) {
        return RuleCode::G002;
    } else if constexpr (!V001_OK<F>) {
        return RuleCode::V001;
    } else if constexpr (!V002_OK<F>) {
        return RuleCode::V002;
    } else if constexpr (!V101_OK<F>) {
        return RuleCode::V101;
    } else if constexpr (!V102_OK<F>) {
        return RuleCode::V102;
    } else if constexpr (!V201_OK<F>) {
        return RuleCode::V201;
    } else if constexpr (!V202_OK<F>) {
        return RuleCode::V202;
    } else if constexpr (!V203_OK<F>) {
        return RuleCode::V203;
    } else if constexpr (!V301_OK<F>) {
        return RuleCode::V301;
    } else if constexpr (!V401_OK<F>) {
        return RuleCode::V401;
    } else if constexpr (!V402_OK<F>) {
        return RuleCode::V402;
    } else if constexpr (!H010_OK<F>) {
        return RuleCode::H010;
    } else if constexpr (!P010_OK<F>) {
        return RuleCode::P010;
    } else if constexpr (!L007_OK<F>) {
        return RuleCode::L007;
    } else if constexpr (!T001_OK<F>) {
        return RuleCode::T001;
    } else {
        return RuleCode::None;
    }
}

template <typename F>
inline constexpr RuleCode first_failure_v = first_failure<F>();

template <typename F>
struct CollisionDiagnostic
    : CollisionDiagnosticByRule<F, first_failure_v<F>> {};

// ═════════════════════════════════════════════════════════════════════
// ── Phase B pack-level rules (L005 + F001) ────────────────────────
// ═════════════════════════════════════════════════════════════════════
//
// Two of the eight Phase B rules read across MULTIPLE Fn instances —
// they cannot be enforced by a single-Fn concept gate.  Phase B ships
// them under `crucible::safety::fn::collision::pack` as compile-time
// boolean metafunctions over `Fn<...>...` packs.  fixy/Fn.h reads them
// after resolving its `Grants...` pack into a `safety::Fn<...>` pack.
//
//   L005_LinearAliasSameRegionTag —
//     no two Linear-usage Fns share the same lifetime::In<Tag>.
//     `lifetime::Static` is a global default and is never an alias;
//     only region-tagged lifetimes participate.
//
//   F001_FrameDeclaresAxisCollision —
//     in a frame manifesto (a curated pack), no axis disagrees across
//     the pack.  Phase B ships the security axis as the seed (every
//     binding in a frame must agree on classification level); future
//     axes are added by extending the conjunction below.

namespace pack {

// ── Lifetime tag extraction ─────────────────────────────────────────
//
// For non-region lifetimes (lifetime::Static or anything else that is
// not a region), the extraction returns `void` — a sentinel that the
// aliasing check ignores.  For lifetime::In<Tag>, the extraction
// returns the Tag value at type level via std::integral_constant.
template <typename L>
struct region_tag_of {
    using type = void;
};
template <auto RegionTag>
struct region_tag_of<lifetime::In<RegionTag>> {
    using type = std::integral_constant<decltype(RegionTag), RegionTag>;
};
template <typename L>
using region_tag_of_t = typename region_tag_of<L>::type;

// ── Is a given Fn a Linear in a region? ────────────────────────────
template <typename F>
inline constexpr bool is_linear_in_region_v =
    F::usage_v == UsageMode::Linear &&
    is_region_lifetime<typename F::lifetime_t>::value;

// ── Are two extracted region-tag carriers identical and non-void? ──
template <typename Tag1, typename Tag2>
inline constexpr bool same_region_tag_v =
    !std::is_void_v<Tag1> &&
    !std::is_void_v<Tag2> &&
    std::is_same_v<Tag1, Tag2>;

// ── Pairwise alias check across a Fn pack ──────────────────────────
//
// O(n^2) over a typically small pack (≤ 20 grants/fns in production).
// Uses a fold expression over two index packs.
template <typename... Fs>
[[nodiscard]] consteval bool no_linear_region_alias() noexcept {
    constexpr std::size_t N = sizeof...(Fs);
    if constexpr (N < 2) {
        return true;  // single binding cannot alias itself
    } else {
        bool ok = true;
        [&]<std::size_t... Is>(std::index_sequence<Is...>) {
            using FnTuple = std::tuple<Fs...>;
            ((void)([&]<std::size_t I>() {
                using Fi = std::tuple_element_t<I, FnTuple>;
                if constexpr (is_linear_in_region_v<Fi>) {
                    using TagI = region_tag_of_t<typename Fi::lifetime_t>;
                    [&]<std::size_t... Js>(std::index_sequence<Js...>) {
                        ((void)([&]<std::size_t J>() {
                            if constexpr (J > I) {
                                using Fj = std::tuple_element_t<J, FnTuple>;
                                if constexpr (is_linear_in_region_v<Fj>) {
                                    using TagJ = region_tag_of_t<typename Fj::lifetime_t>;
                                    if constexpr (same_region_tag_v<TagI, TagJ>) {
                                        ok = false;
                                    }
                                }
                            }
                        }.template operator()<Js>()), ...);
                    }(std::make_index_sequence<N>{});
                }
            }.template operator()<Is>()), ...);
        }(std::make_index_sequence<N>{});
        return ok;
    }
}

template <typename... Fs>
inline constexpr bool no_linear_region_alias_v = no_linear_region_alias<Fs...>();

// ── Frame manifesto axis consistency (security seed) ───────────────
//
// Every binding in a frame manifesto must agree on the security axis.
// More axes can be added to this conjunction as the frame discipline
// grows; Phase B ships the security seed.
template <typename... Fs>
[[nodiscard]] consteval bool frame_axis_consistent() noexcept {
    constexpr std::size_t N = sizeof...(Fs);
    if constexpr (N < 2) {
        return true;
    } else {
        using FnTuple = std::tuple<Fs...>;
        constexpr SecLevel first_security =
            std::tuple_element_t<0, FnTuple>::security_v;
        bool ok = true;
        [&]<std::size_t... Is>(std::index_sequence<Is...>) {
            ((void)([&]<std::size_t I>() {
                using Fi = std::tuple_element_t<I, FnTuple>;
                if constexpr (Fi::security_v != first_security) {
                    ok = false;
                }
            }.template operator()<Is>()), ...);
        }(std::make_index_sequence<N>{});
        return ok;
    }
}

template <typename... Fs>
inline constexpr bool frame_axis_consistent_v = frame_axis_consistent<Fs...>();

// ── S004 Meyers-singleton init-cycle detector ───────────────────────
//
// Reusable consteval cycle detector over a compile-time directed-edge
// list on `NodeCount` singleton tags.  An edge (from, to) reads "the
// lazy initializer of singleton `from` touches singleton `to`", so a
// cycle is the static-initialization-order fiasco in its subtlest
// (lazy-init) form: the first thread to touch either singleton triggers
// a re-entrant initialization that observes a half-constructed peer.
//
// V-248 (Scenario D) supplies the edges from registered
// grant::global::singleton<Tag> annotations and specializes
// marks_singleton_init_cycle<F> for any Fn whose tag participates in a
// detected cycle.  Kahn's algorithm: repeatedly retire a zero-in-degree
// node; if any node survives when none can be retired, the graph has a
// cycle.  Returns true iff ACYCLIC.
template <std::size_t NodeCount, std::size_t EdgeCount>
[[nodiscard]] consteval bool singleton_init_acyclic(
    const std::array<std::pair<std::size_t, std::size_t>, EdgeCount>& edges) noexcept {
    std::array<std::size_t, NodeCount> in_degree{};
    for (const auto& edge : edges) {
        if (edge.second < NodeCount) {
            ++in_degree[edge.second];
        }
    }
    std::array<bool, NodeCount> retired{};
    std::size_t retired_count = 0;
    bool made_progress = true;
    while (made_progress) {
        made_progress = false;
        for (std::size_t node = 0; node < NodeCount; ++node) {
            if (retired[node] || in_degree[node] != 0) {
                continue;
            }
            retired[node] = true;
            ++retired_count;
            made_progress = true;
            for (const auto& edge : edges) {
                if (edge.first == node && edge.second < NodeCount &&
                    !retired[edge.second]) {
                    --in_degree[edge.second];
                }
            }
        }
    }
    return retired_count == NodeCount;
}

template <std::size_t NodeCount, std::size_t EdgeCount>
inline constexpr bool singleton_init_has_cycle(
    const std::array<std::pair<std::size_t, std::size_t>, EdgeCount>& edges) noexcept {
    return !singleton_init_acyclic<NodeCount>(edges);
}

}  // namespace pack

}  // namespace crucible::safety::fn::collision

namespace crucible::safety::fn {

template <typename F>
struct CollisionRules {
    static constexpr bool valid = true;
};

template <
    typename       Type,
    typename       Refinement,
    UsageMode      Usage,
    typename       EffectRow,
    SecLevel       Security,
    typename       Protocol,
    typename       Lifetime,
    typename       Source,
    typename       Trust,
    ReprKind       Repr,
    typename       Cost,
    typename       Precision,
    typename       Space,
    OverflowMode   Overflow,
    MutationMode   Mutation,
    ReentrancyMode Reentrancy,
    typename       Size,
    std::uint32_t  Version,
    typename       Staleness
>
struct CollisionRules<Fn<Type, Refinement, Usage, EffectRow, Security,
                         Protocol, Lifetime, Source, Trust, Repr, Cost,
                         Precision, Space, Overflow, Mutation, Reentrancy,
                         Size, Version, Staleness>> {
    using F = Fn<Type, Refinement, Usage, EffectRow, Security,
                 Protocol, Lifetime, Source, Trust, Repr, Cost,
                 Precision, Space, Overflow, Mutation, Reentrancy,
                 Size, Version, Staleness>;

    static constexpr bool classified =
        Security == SecLevel::Classified || Security == SecLevel::Secret;
    static constexpr bool async = collision::marks_async<F>::value;
    static constexpr bool ct = collision::marks_ct<F>::value;
    static constexpr bool fail = collision::marks_fail<F>::value;
    static constexpr bool fail_secret = collision::marks_fail_error_secret<F>::value;
    static constexpr bool borrow_capture =
        Usage == UsageMode::Borrow ||
        collision::is_borrowed_carrier<std::remove_cvref_t<Type>>::value ||
        collision::marks_borrow_capture<F>::value;
    static constexpr bool concurrent =
        async ||
        collision::row_has_effect_v<EffectRow, effects::Effect::Bg> ||
        collision::marks_concurrent_context<F>::value;
    static constexpr bool session_protocol = !std::is_same_v<Protocol, proto::None>;
    static constexpr bool stale_nonfresh = !std::is_same_v<Staleness, stale::Fresh>;

    // ── Phase B per-F predicates (6 of 8 new rules) ──────────────────
    static constexpr bool region_lifetime =
        collision::is_region_lifetime<Lifetime>::value;
    static constexpr bool lifetime_unprotected =
        collision::marks_lifetime_region_unprotected<F>::value;
    static constexpr bool unbounded_cost =
        collision::is_unbounded_cost<Cost>::value;
    static constexpr bool hot_path = collision::marks_hot_path<F>::value;
    static constexpr bool externally_observable =
        collision::marks_externally_observable<F>::value;
    static constexpr bool federation_peer =
        collision::marks_federation_peer<F>::value;
    static constexpr bool trivial_refinement =
        collision::is_trivial_refinement<Refinement>::value;
    static constexpr bool row_has_bg =
        collision::row_has_effect_v<EffectRow, effects::Effect::Bg>;
    static constexpr bool row_has_alloc_or_io =
        collision::row_has_effect_v<EffectRow, effects::Effect::Alloc> ||
        collision::row_has_effect_v<EffectRow, effects::Effect::IO>;

    // ── W001: HotPath × Wait<{Block,Park,AcquireWait,UmwaitC01}> ─────
    // FIXY-FOUND-061: rejected set widened to all four syscall-tier
    // strategies (was {Block, Park} only) to match CLAUDE.md §IX.
    static constexpr bool type_has_kernel_wait =
        collision::wait_strategy_of<Type>::has_wait &&
        []() consteval {
            if constexpr (collision::wait_strategy_of<Type>::has_wait) {
                return collision::is_kernel_wait_v<
                    collision::wait_strategy_of<Type>::value>;
            } else {
                return false;
            }
        }();

    // ── W002: Bg-row × Wait<SpinPause> or Wait<BoundedSpin> ──────────
    static constexpr bool type_has_active_spin_wait =
        collision::wait_strategy_of<Type>::has_wait &&
        []() consteval {
            if constexpr (collision::wait_strategy_of<Type>::has_wait) {
                return collision::is_active_spin_v<
                    collision::wait_strategy_of<Type>::value>;
            } else {
                return false;
            }
        }();

    // ── FIXY-V-091 F-family predicates (FP-mode cross-axis) ──────────
    //
    // Each predicate checks whether Type is an FpModePinned wrapper for
    // the load-bearing toxic mode value on its sub-axis.  Per-axis
    // detector is `wraps_fp_axis_mode_v<AxisMode, T>` — one generic
    // detector covers all 11 sub-axes because the NTTP type-discriminates.
    static constexpr bool replay_required =
        collision::marks_replay_required<F>::value;
    static constexpr bool fp_reassoc_unrestricted =
        collision::wraps_fp_axis_mode_v<
            ::crucible::safety::FpReassociate::UnrestrictedRewrite, Type>;
    // FIXY-FOUND-074: any non-Forbidden FpReassociate setting under
    // replay defeats bit-exact cross-vendor replay (BoundedTreeDepth's
    // vendor-specific lane assignment within the log-N tree shape
    // diverges the same as UnrestrictedRewrite's free reordering).
    // F103 (CT-axis) still reads the narrower fp_reassoc_unrestricted
    // because BoundedTreeDepth has data-independent topology and is
    // CT-safe even though it is replay-unsafe.
    static constexpr bool fp_reassoc_bounded_tree =
        collision::wraps_fp_axis_mode_v<
            ::crucible::safety::FpReassociate::BoundedTreeDepth, Type>;
    static constexpr bool fp_reassoc_non_strict =
        fp_reassoc_unrestricted || fp_reassoc_bounded_tree;
    static constexpr bool fp_contract_fast =
        collision::wraps_fp_axis_mode_v<
            ::crucible::safety::FpContract::Fast, Type>;
    static constexpr bool fp_denormal_input_honored =
        collision::wraps_fp_axis_mode_v<
            ::crucible::safety::FpDenormalInput::HonorDenormals, Type>;
    static constexpr bool fp_ftz_preserved =
        collision::wraps_fp_axis_mode_v<
            ::crucible::safety::FpFtz::PreserveSubnormals, Type>;

    // ── FIXY-V-243 hazard-axis predicates (8 cross-axis rules) ───────
    static constexpr bool aborts = collision::marks_aborts<F>::value;
    static constexpr bool cf_witnesses_abort =
        collision::cf_at_or_above_v<
            ::crucible::algebra::lattices::ControlFlow::AbortOnly, Type>;
    static constexpr bool indirect_call_not_noexcept =
        collision::marks_indirect_call_not_noexcept<F>::value;
    static constexpr bool recurses_unbounded =
        collision::marks_recurses_unbounded<F>::value;
    static constexpr bool thread_local_untagged =
        collision::marks_thread_local_untagged<F>::value;
    static constexpr bool longjmp_unsafe =
        collision::marks_longjmp_unsafe<F>::value ||
        collision::cf_at_or_above_v<
            ::crucible::algebra::lattices::ControlFlow::MayLongjmp, Type>;
    static constexpr bool fork_worker = collision::marks_fork_worker<F>::value;
    static constexpr bool fork_body_throws =
        collision::marks_throws<F>::value ||
        collision::cf_at_or_above_v<
            ::crucible::algebra::lattices::ControlFlow::ThrowOnly, Type>;
    static constexpr bool hot_path_stdio =
        collision::stdio_at_or_above_v<
            ::crucible::algebra::lattices::Stdio::BufferedWrite, Type>;
    static constexpr bool singleton_init_cycle =
        collision::marks_singleton_init_cycle<F>::value;
    static constexpr bool thread_local_atomic =
        collision::marks_thread_local_atomic<F>::value;

    // ── FIXY-V-260 hardware-axis predicates (8 cross-axis rules) ─────
    static constexpr bool vendor_isa_inconsistent =
        collision::marks_vendor_isa_inconsistent<F>::value;
    static constexpr bool vendor_cross_arch =
        collision::marks_vendor_cross_arch<F>::value;
    static constexpr bool simd_width_exceeds_isa =
        collision::marks_simd_width_exceeds_isa<F>::value;
    static constexpr bool replay_simd_isa_pinned =
        collision::simd_isa_pins_specific_vector_v<Type>;
    static constexpr bool hot_path_nondet_tsc =
        collision::hw_at_or_above_v<
            ::crucible::algebra::lattices::HwInstruction::NonDeterministicTsc, Type>;
    static constexpr bool privileged_msr_no_init =
        collision::hw_tier_of<Type>::has_hw &&
        collision::hw_tier_of<Type>::value ==
            ::crucible::algebra::lattices::HwInstruction::PrivilegedMsr &&
        !collision::row_has_effect_v<EffectRow, effects::Effect::Init>;
    static constexpr bool hot_path_full_fence =
        collision::barrier_at_or_above_v<
            ::crucible::algebra::lattices::BarrierStrength::SeqCst, Type>;

    // ── FIXY-V-268 memory-scope-axis predicates (2 cross-axis rules) ─
    // FIXY-FOUND-062: V401 threshold widened Gpu → Cluster, catching the
    // Hopper thread-block-cluster cross-CTA publish case alongside
    // device-wide / system-wide publishes.  See V401_OK concept above.
    static constexpr bool scope_at_or_above_cluster =
        collision::scope_at_or_above_v<
            ::crucible::algebra::lattices::MemoryScope::Cluster, Type>;
    static constexpr bool barrier_at_least_acqrel =
        collision::barrier_at_or_above_v<
            ::crucible::algebra::lattices::BarrierStrength::AcqRel, Type>;
    static constexpr bool scope_strength_insufficient =
        scope_at_or_above_cluster && !barrier_at_least_acqrel;
    // Read the destructured Type / Source template params directly (NOT
    // scope_arch_cross_trunk_v<F>, which would touch F::type_t / F::source_t
    // and force Fn<> to complete — re-entering its own
    // static_assert(ValidComposition<Fn>) and recursing).  The V402_OK
    // concept reads F::type_t in the safe AllRulesOK / first_failure path.
    //
    // FIXY-FOUND-073: extend with the nested-cross-trunk leg.  A binding
    // whose Type pins two ScopedFence layers from contradicting trunks
    // (accel + ARM) has no coherent realization on any host; the
    // contradiction is internal to the type and independent of Source.
    static constexpr bool scope_arch_cross_trunk =
        collision::marks_scope_arch_cross_trunk<F>::value ||
        (collision::scope_tier_of<Type>::has_scope &&
         collision::scope_contradicts_host_arch(
             collision::scope_tier_of<Type>::value,
             ::crucible::safety::arch_pin_v<Source>)) ||
        collision::nested_scope_cross_trunk_v<Type>;

    [[nodiscard]] static consteval bool validate() noexcept {
        static_assert(!(classified && fail && !fail_secret),
            "I002: classified value cannot flow through Fail(E) with "
            "non-secret error payload. Declare Fail(secret E), declassify "
            "explicitly, or remove Fail from the classified region.");
        static_assert(!(borrow_capture && async),
            "L002: borrow x Async incompatible. Borrow lifetime cannot "
            "bridge await suspension; scope the borrow before await or "
            "capture by value.");
        static_assert(!(ct && async),
            "E044: CT x Async incompatible. Async scheduling defeats "
            "the constant-time guarantee; keep the CT core synchronous.");
        static_assert(!(ct && fail && collision::marks_fail_on_secret<F>::value),
            "I003: CT x Fail-on-secret incompatible. Do not expose a "
            "secret-dependent branch through failure; use ct_select first.");
        static_assert(!(Mutation == MutationMode::Monotonic &&
                        concurrent &&
                        Repr != ReprKind::Atomic),
            "M012: monotonic x concurrent without atomic representation. "
            "Use ReprKind::Atomic or safety::AtomicMonotonic<T, Cmp>.");
        static_assert(!(Usage == UsageMode::Ghost &&
                        collision::marks_runtime_ghost_use<F>::value),
            "P002: ghost x runtime incompatible. Ghost values are erased "
            "and cannot drive runtime branches, indexes, or returns.");
        static_assert(!(classified && async && session_protocol && !ct),
            "I004: classified x async x session without CT leaks timing. "
            "Use a synchronous CT send region or explicitly declassify.");
        static_assert(!(collision::is_exact_decimal<std::remove_cvref_t<Type>>::value &&
                        Overflow == OverflowMode::Wrap),
            "N002: decimal x overflow(wrap) is invalid. Exact decimal "
            "types must use trap, saturate, or widen semantics.");
        static_assert(!(borrow_capture && collision::marks_unscoped_spawn<F>::value),
            "L003: borrow x unscoped spawn incompatible. Use task_group, "
            "permission_fork, or move ownership into the spawned closure.");
        static_assert(!(Usage == UsageMode::Linear &&
                        fail &&
                        collision::marks_linear_uncleaned_fail<F>::value),
            "M011: linear x Fail without cleanup leaks a linear resource. "
            "Register defer/errdefer, use RAII cleanup, or fail before acquire.");
        static_assert(!(ct && stale_nonfresh),
            "S010: Staleness x CT incompatible. Runtime freshness checks "
            "defeat constant-time timing; require stale::Fresh or drop CT.");
        static_assert(!(Usage == UsageMode::Capability &&
                        collision::marks_replay_required<F>::value &&
                        !collision::marks_replay_stable<F>::value),
            "S011: Capability x Replay incompatible. Ephemeral capabilities "
            "must not enter replay-stable code without content-addressed handles.");
        // ── Phase B per-Fn collision asserts ─────────────────────────
        static_assert(!(Usage == UsageMode::Linear &&
                        region_lifetime && lifetime_unprotected),
            "L004: Linear x lifetime::In<Tag> without Permission proof. "
            "Thread a Permission<Tag> through the call or move Usage out "
            "of Linear.");
        static_assert(!(row_has_bg && externally_observable && unbounded_cost),
            "B001: Bg observable surface with unbounded resource. "
            "Declare space::Bounded<N> + cost::Linear<N> (or stricter); a "
            "Bg-observable surface that may run unbounded is a back-pressure trap.");
        static_assert(!(hot_path && unbounded_cost),
            "H001: HotPath x cost::Unstated/Unbounded. Declare "
            "cost::Constant or cost::Linear<N>; the hot path must justify "
            "its compute envelope.");
        static_assert(!(hot_path && trivial_refinement),
            "H002: HotPath x pred::True refinement (no witness floor). "
            "Attach a Refined<predicate, Type> input that proves an invariant "
            "the hot body assumes (aligned, in-range, non-zero); pred::True "
            "is review-rejected on hot paths.");
        static_assert(!(hot_path && row_has_alloc_or_io && unbounded_cost),
            "H003: HotPath x (Alloc or IO) x unbounded cost. Move Alloc/IO "
            "outside the hot path or attach an Init/Bg context that owns "
            "the unbounded surface.");
        static_assert(!(hot_path && row_has_bg),
            "H010: HotPath x Row<Bg>. A function cannot be BOTH on the hot "
            "path (<=40 ns intra-socket per CLAUDE.md §IX) AND in background "
            "context (Alloc/IO/Block/millisecond latency allowed). The two "
            "contexts are mutually exclusive. H001 catches HotPath x unbounded "
            "cost; H003 catches HotPath x Alloc/IO x unbounded cost; but a "
            "HotPath x Row<Bg> x cost::Constant x no-Alloc/IO/Block Fn slips "
            "both — yet is still a context contradiction. Drop the HotPath "
            "marker (genuinely Bg) OR drop the Bg atom (genuinely hot-path). "
            "FIXY-FOUND-063.");
        static_assert(!(Usage == UsageMode::Ghost &&
                        (row_has_alloc_or_io ||
                         collision::row_has_effect_v<EffectRow,
                                                     effects::Effect::Block>)),
            "P010: Ghost x Row<Alloc|IO|Block>. A Ghost-usage binding is "
            "erased at codegen (no emitted instructions, no register "
            "pressure, no stack footprint), but Alloc emits heap-touching "
            "code, IO emits syscalls / external observers, and Block "
            "emits blocking primitives. ALL THREE require emitted code; "
            "Ghost contractually forbids it. Structurally parallel to "
            "H010 (HotPath x Bg) on the Usage axis. P002 catches the "
            "grant-driven marks_runtime_ghost_use variant; P010 catches "
            "the structural effect-row read. Drop UsageMode::Ghost OR "
            "drop the observable effect atom (wrap the runtime "
            "implementation in a sibling non-Ghost binding if a downstream "
            "caller needs the Alloc/IO/Block surface). FIXY-FOUND-064.");
        static_assert(!(borrow_capture && row_has_bg),
            "L007: borrow_capture x Row<Bg>. A borrow-capture function takes "
            "a borrowed reference whose lifetime is tied to the caller's "
            "stack; a Bg-row function executes in background-thread context. "
            "When the background thread runs the body, the caller's stack "
            "may have unwound, leaving the borrow dangling. L002 catches "
            "borrow x marks_async (coroutine/await suspension); L003 catches "
            "borrow x marks_unscoped_spawn (spawn-marker driven). But a "
            "borrow_capture binding with Row<Bg> in its effect row and NO "
            "marker specialization slips both — yet has the same cross-thread "
            "lifetime hazard. Drop UsageMode::Borrow (move ownership into "
            "the Bg-context closure via Linear / Capability / Owned), OR drop "
            "the Bg atom from the effect row, OR use the explicit "
            "permission_fork API for lifetime-scoped fork-join borrow "
            "patterns. FIXY-FOUND-065.");
        static_assert(!(Usage == UsageMode::Capability &&
                        std::is_same_v<Trust,
                                       ::crucible::safety::trust::Unverified>),
            "T001: UsageMode::Capability x trust::Unverified. A Capability "
            "binding mints a non-revocable authorization token that "
            "downstream consumers treat as legitimate proof of authority; "
            "a binding whose provenance is Unverified (the FOUND-034 "
            "Biba-safe default) cannot establish that authority chain. "
            "Engage grant::trust_verified (cryptographically-verified or "
            "proof-witnessed mint sites) OR grant::trust_tested "
            "(capabilities exercised under test isolation with measured "
            "behavior). If the binding should not assert any authority, "
            "drop UsageMode::Capability and pick Linear / Borrow / Ghost. "
            "FIXY-FOUND-070.");
        static_assert(!(federation_peer && unbounded_cost),
            "F002: federation peer x unbounded cost. Attach a wall-clock "
            "budget (cost::Linear<N>) and a terminating bound; federation "
            "peers cannot run forever.");
        static_assert(!(hot_path && type_has_kernel_wait),
            "W001: HotPath x kernel-tier Wait. Hot-path functions MUST NOT "
            "wrap return/parameter type in a kernel-mediated Wait strategy "
            "(rejected set: Block, Park, AcquireWait, UmwaitC01). Block "
            "(poll/epoll_wait) and Park (condvar/futex) involve 1-5 us "
            "latency; AcquireWait (atomic::wait) is futex-backed at 1-5 us "
            "(BANNED on hot path per CLAUDE.md §IX); UmwaitC01 (WAITPKG) is "
            "100-500 ns plus wait time and is 'Not applicable on our hot "
            "path' per §IX. All four are incompatible with the hot-path "
            "budget (<= 40 ns intra-socket). Switch to Wait<SpinPause> "
            "(CLAUDE.md §IX default, 10-40 ns intra-socket via PAUSE + MESI) "
            "or Wait<BoundedSpin> for unknown-delay signals; or move the "
            "blocking call into an Init/Bg context. See safety/Wait.h "
            "is_hot_path_waiter_admissible for the strict gate. FIXY-FOUND-"
            "124 (Park/Block); FIXY-FOUND-061 (AcquireWait/UmwaitC01 widen).");
        static_assert(!(row_has_bg && type_has_active_spin_wait),
            "W002: Bg-row x Wait<SpinPause> or Wait<BoundedSpin>. Bg-context "
            "functions MUST NOT wrap return/parameter type in an active-spin "
            "Wait strategy. SpinPause and BoundedSpin occupy 100% of the "
            "hosting core. Bg threads are by contract permitted to block, so "
            "the kernel should be free to schedule another runnable thread "
            "onto the core. Switch to Wait<UmwaitC01> (power-aware), "
            "Wait<AcquireWait> (futex), Wait<Park> (condvar), or Wait<Block> "
            "(poll/epoll).");
        // ── FIXY-V-091 F-family static asserts ───────────────────────
        static_assert(!(replay_required && fp_reassoc_non_strict),
            "F101: Replay-required x non-strict FpReassociatePinned "
            "(UnrestrictedRewrite OR BoundedTreeDepth). UnrestrictedRewrite "
            "(-fassociative-math) reorders FP additions freely; "
            "BoundedTreeDepth pins a log-N tree DEPTH but lets vendors pick "
            "the per-level LANE ASSIGNMENT (NVIDIA warp-shuffle vs AMD "
            "wavefront vs Intel SVE differ on operand ordering within the "
            "same tree shape). Both paths diverge bit-equality across the "
            "cross-vendor numerics CI matrix. Switch to "
            "FpReassociatePinned<Forbidden> (IEEE 754 default, the ONLY "
            "setting compatible with bit-exact replay), or remove the "
            "marks_replay_required marker if the binding does not require "
            "bit-exact replay. FIXY-FOUND-074.");
        static_assert(!(replay_required && fp_contract_fast),
            "F102: Replay-required x FpContractPinned<Fast>. Cross-statement "
            "FMA folding picks different contraction boundaries per vendor "
            "(NVIDIA SASS vs AMD CDNA vs Intel SPR); same source produces "
            "different bit patterns. Switch to FpContractPinned<Off> or "
            "FpContractPinned<OnInExpr> (within-statement FMA, IEEE 754-2008 "
            "default).");
        static_assert(!(ct && fp_reassoc_unrestricted),
            "F103: CT x FpReassociatePinned<UnrestrictedRewrite>. "
            "Reassociation introduces data-dependent reduction-tree topology, "
            "violating the timing-independence guarantee. Switch to "
            "FpReassociatePinned<Forbidden> on the CT path.");
        static_assert(!(ct && fp_denormal_input_honored),
            "F104: CT x FpDenormalInputPinned<HonorDenormals>. DAZ=0 "
            "introduces a 30-100x cycle-count delta when the input IS "
            "denormal (textbook FP timing side-channel). Switch to "
            "FpDenormalInputPinned<DenormalsAreZero> (MXCSR.DAZ / FPCR.FZ).");
        static_assert(!(ct && fp_ftz_preserved),
            "F105: CT x FpFtzPinned<PreserveSubnormals>. FTZ=0 introduces a "
            "30-100x slowdown when the result IS denormal; result-magnitude "
            "leaks through cycle count. Switch to FpFtzPinned<FlushToZero> "
            "on the CT path.");
        // ── FIXY-V-243 hazard-axis static asserts ─────────────────────
        static_assert(!(aborts && !cf_witnesses_abort),
            "C001: abort-declaring function (grant::ctrl::abort<Rationale>) MUST "
            "carry a ControlFlow tier >= AbortOnly that witnesses the escape. "
            "Wrap the result in ControlFlowPinned<AbortOnly, U> (or higher), or "
            "remove the abort grant if the function always returns.");
        static_assert(!indirect_call_not_noexcept,
            "D001: indirect-call grant (grant::dispatch::indirect_call<Family>) "
            "carries a callable whose RunFn is NOT noexcept. A throwing indirect "
            "call across a -fno-exceptions boundary terminates. Add noexcept to "
            "the callable family or wrap it in a noexcept trampoline.");
        static_assert(!recurses_unbounded,
            "D002: recursion grant (grant::dispatch::recurses<>) declared without "
            "an NTTP MaxDepth. Declare grant::dispatch::recurses<MaxDepth> with "
            "the proven worst-case depth, or rewrite as a bounded iterative loop.");
        static_assert(!thread_local_untagged,
            "G001: thread_local grant (grant::global::thread_local_<>) declared "
            "without a TLSTag NTTP. Declare grant::global::thread_local_<TLSTag> "
            "with a unique phantom tag (mirrors safety::ThreadLocalRef<Tag, T>).");
        static_assert(!(Usage == UsageMode::Linear && longjmp_unsafe),
            "L006: Linear resource held across a ControlFlow tier >= MayLongjmp "
            "(or a marks_longjmp_unsafe grant). longjmp SKIPS destructors — the "
            "Linear would leak / dangle across the jump. Lower the ControlFlow "
            "tier, release the Linear before the jump, or move Usage out of Linear.");
        static_assert(!(fork_worker && fork_body_throws),
            "P003: permission_fork worker body carries a ControlFlow tier >= "
            "ThrowOnly (or a marks_throws grant). A throw inside a jthread fork "
            "body is std::terminate under -fno-exceptions. Lower the ControlFlow "
            "tier, or convert the failure into a std::expected return.");
        static_assert(!(hot_path && hot_path_stdio),
            "S001: hot-path function carries a Stdio tier >= BufferedWrite. "
            "TraceRing / Arena / KernelCache MUST NOT do stdio (CLAUDE.md §XII). "
            "Use StdioPinned<NoStdio>, push events to an SPSC ring for bg drain, "
            "or move the stdio into a Bg context.");
        static_assert(!singleton_init_cycle,
            "S004: Meyers-singleton init-dependency cycle detected by the V-248 "
            "tag-graph closure walk — the lazy-init form of the static-init-order "
            "fiasco. Break the cycle (inject the dependency or merge the "
            "singletons); see pack::singleton_init_acyclic for the detector.");
        static_assert(!thread_local_atomic,
            "G002: thread_local storage paired with an atomic memory-order "
            "wrapper. An atomic op on a per-thread object orders against no peer "
            "(one instance per thread) — either drop thread_local for a "
            "process-wide static std::atomic<T>, or drop the atomic for a plain "
            "thread_local (Scenario E: bench_smoke.cpp:78).");
        // ── FIXY-V-260 hardware-axis static asserts ───────────────────
        static_assert(!vendor_isa_inconsistent,
            "V001: vendor::intrinsic grants in the binding pack declare an "
            "inconsistent (vendor, ISA-family). The per-grant "
            "vendor_isa_consistent_v<V, I> gate (V-258) only checks a single "
            "intrinsic; pin one vendor for the binding or split the multi-vendor "
            "work into per-vendor Fn signatures (the Mimic per-vendor pattern).");
        static_assert(!vendor_cross_arch,
            "V002: a single binding composes intrinsics from incompatible "
            "architecture trunks (x86 + ARM). The emitted binary would #UD on "
            "whichever ISA it lands. Select the architecture at the dispatch "
            "boundary and keep each arch's intrinsics in its own single-target "
            "binary (CLAUDE.md §VIII); see the V-261 source::ArchPinned gate.");
        static_assert(!(replay_required && replay_simd_isa_pinned),
            "V101: replay-required function pins a specific vector ISA via "
            "SimdWidthPinned<W>. AVX-512 and NEON have different lane counts, so "
            "the same IR produces a different FP-reduction tree and the bit "
            "pattern diverges across the cross-vendor CI matrix. Pin "
            "SimdIsa::Scalar or SimdIsa::Portable, or drop replay eligibility.");
        static_assert(!simd_width_exceeds_isa,
            "V102: a simd::width<W> grant pins a register width wider than the "
            "declared vendor ISA family supports (the width<512>-on-AVX2 case: "
            "AVX2 tops out at 256-bit). Lower the pinned width to the ISA family's "
            "native maximum, or raise the ISA family so the width is representable.");
        static_assert(!(hot_path && hot_path_nondet_tsc),
            "V201: hot-path function carries a Hw tier >= NonDeterministicTsc. "
            "rdtsc/rdtscp are serializing (~20-40 cycles); rdmsr/wrmsr are ring-0 "
            "privileged traps — both blow the <= 40 ns intra-socket hot budget "
            "(CLAUDE.md §IX). Move the timestamp/MSR read into an Init or Bg "
            "context, or drop the Hw tier to Hw<Vectorizable>/<Scalar>.");
        static_assert(!privileged_msr_no_init,
            "V202: a Hw<PrivilegedMsr> tier is carried outside an Init-context "
            "row. rdmsr/wrmsr/IN/OUT require ring 0 and a Permission proof; the "
            "HwInstructionLattice doc pins them to one-shot privileged Init setup. "
            "Thread the access through an Init context (effects::Init), or drop "
            "the PrivilegedMsr tier.");
        static_assert(!(replay_required && hot_path_nondet_tsc),
            "V203: replay-required function reads a non-deterministic timestamp "
            "counter (Hw tier >= NonDeterministicTsc). rdtsc is hardware-dependent "
            "(different cycle base / invariant-TSC behavior across hosts), so a "
            "replay body diverges across reincarnation hardware. Use the "
            "deterministic Philox / logical-clock source, or drop replay eligibility.");
        static_assert(!(hot_path && hot_path_full_fence),
            "V301: hot-path function carries a BarrierStrength tier >= SeqCst. "
            "A full fence (mfence / lock-prefixed) drains the store buffer "
            "(~20-40+ cycles) — CLAUDE.md §IX mandates acquire/release only on the "
            "hot path (free on x86 TSO). Use BarrierGuarded<AcquireLoad>/"
            "<ReleaseStore>/<AcqRel>; reserve SeqCst/FullFence for Init or Bg.");
        // ── FIXY-V-268 memory-scope-axis static asserts ──────────────
        static_assert(!scope_strength_insufficient,
            "V401: a ScopedFence scope ⊒ Cluster (cross-CTA / device / system visibility) "
            "is composed with a BarrierStrength ⊏ AcqRel (None / CompilerBarrier / "
            "AcquireLoad / ReleaseStore). A `.cluster`/`.gpu`/`.sys`-scope publication "
            "widens visibility but a sub-AcqRel barrier never establishes the two-sided "
            "ordering cross-CTA / cross-cluster / cross-device readers require — a silent "
            "weak-memory race. Raise the barrier to BarrierGuarded<AcqRel> (or SeqCst / "
            "FullFence) on the cross-CTA-scope value, or narrow the ScopedFence scope to "
            "Cta / Warp where the weaker barrier suffices. FIXY-FOUND-062 widened the "
            "catch set from {Gpu, System} to {Cluster, Gpu, System} — Hopper thread-block "
            "clusters cross CTA boundaries via distributed shared memory.");
        static_assert(!scope_arch_cross_trunk,
            "V402: a ScopedFence scope trunk contradicts the binding's pinned host "
            "architecture (arch_pin_v<F::source_t>) — an accel (GPU device) scope on a "
            "CPU-host arch pin (ArchPinned<Arm>/<X86>, whose DMB/mfence fence dialect "
            "cannot realize a PTX `.cta`/`.gpu` scope), OR an ARM-shareability scope "
            "(Inner/Outer = DMB ISH/OSH) on a non-ARM host (x86 has no ISH/OSH domain). "
            "The MemoryScope-axis mirror of V002 cross-arch mixing. Pin a host arch whose "
            "trunk matches the scope (ArchPinned<Arm> for Inner/Outer; leave the pin "
            "Portable for a GPU-device scope), or drop the contradicting scope.");
        // L005 and F001 are pack-level rules (no single-Fn enforcement
        // shape); fixy/Fn.h checks them across the Grants pack via
        // pack::no_linear_region_alias_v and pack::frame_axis_consistent_v.
        return !(classified && fail && !fail_secret) &&
               !(borrow_capture && async) &&
               !(ct && async) &&
               !(ct && fail && collision::marks_fail_on_secret<F>::value) &&
               !(Mutation == MutationMode::Monotonic && concurrent &&
                 Repr != ReprKind::Atomic) &&
               !(Usage == UsageMode::Ghost &&
                 collision::marks_runtime_ghost_use<F>::value) &&
               !(classified && async && session_protocol && !ct) &&
               !(collision::is_exact_decimal<std::remove_cvref_t<Type>>::value &&
                 Overflow == OverflowMode::Wrap) &&
               !(borrow_capture && collision::marks_unscoped_spawn<F>::value) &&
               !(Usage == UsageMode::Linear && fail &&
                 collision::marks_linear_uncleaned_fail<F>::value) &&
               !(ct && stale_nonfresh) &&
               !(Usage == UsageMode::Capability &&
                 collision::marks_replay_required<F>::value &&
                 !collision::marks_replay_stable<F>::value) &&
               !(Usage == UsageMode::Linear && region_lifetime &&
                 lifetime_unprotected) &&
               !(row_has_bg && externally_observable && unbounded_cost) &&
               !(hot_path && unbounded_cost) &&
               !(hot_path && trivial_refinement) &&
               !(hot_path && row_has_alloc_or_io && unbounded_cost) &&
               !(federation_peer && unbounded_cost) &&
               !(hot_path && type_has_kernel_wait) &&
               !(row_has_bg && type_has_active_spin_wait) &&
               !(replay_required && fp_reassoc_non_strict) &&
               !(replay_required && fp_contract_fast) &&
               !(ct && fp_reassoc_unrestricted) &&
               !(ct && fp_denormal_input_honored) &&
               !(ct && fp_ftz_preserved) &&
               !(aborts && !cf_witnesses_abort) &&
               !indirect_call_not_noexcept &&
               !recurses_unbounded &&
               !thread_local_untagged &&
               !(Usage == UsageMode::Linear && longjmp_unsafe) &&
               !(fork_worker && fork_body_throws) &&
               !(hot_path && hot_path_stdio) &&
               !singleton_init_cycle &&
               !thread_local_atomic &&
               !vendor_isa_inconsistent &&
               !vendor_cross_arch &&
               !(replay_required && replay_simd_isa_pinned) &&
               !simd_width_exceeds_isa &&
               !(hot_path && hot_path_nondet_tsc) &&
               !privileged_msr_no_init &&
               !(replay_required && hot_path_nondet_tsc) &&
               !(hot_path && hot_path_full_fence) &&
               !scope_strength_insufficient &&
               !scope_arch_cross_trunk &&
               !(hot_path && row_has_bg) &&
               !(Usage == UsageMode::Ghost &&
                 (row_has_alloc_or_io ||
                  collision::row_has_effect_v<EffectRow,
                                              effects::Effect::Block>)) &&
               !(borrow_capture && row_has_bg) &&
               !(Usage == UsageMode::Capability &&
                 std::is_same_v<Trust,
                                ::crucible::safety::trust::Unverified>);
    }

    static constexpr bool valid = validate();
};

namespace detail::collision_catalog_self_test {

struct FreshCtMarker {};
using FreshCtCarrier = Fn<FreshCtMarker>;

}  // namespace detail::collision_catalog_self_test

namespace collision {

template <>
struct marks_ct<detail::collision_catalog_self_test::FreshCtCarrier> : std::true_type {};

}  // namespace collision

namespace detail::collision_catalog_self_test {

using DefaultFn = Fn<int>;
static_assert(ValidComposition<DefaultFn>);
// FIXY-V-234 bump: 27 → 28 with M001_DontNeedRequiresReleaseAware.
// Use `>=` floor pattern (per feedback_catalog_cardinality_test_drift) so
// future appends don't silently red this self-test; the per-rule
// rule_bijection assertions below pin each catalog entry individually.
static_assert(collision::catalog_size >= 36,
              "FIXY-V-091/V-234/V-243 floor: catalog must include F101..F105 + M001 "
              "+ C001/D001/D002/G001/L006/P003/S001/S004");
static_assert(std::is_same_v<
    collision::rule_tag_t<collision::RuleCode::I002>,
    collision::I002_ClassifiedFailPayload>);
static_assert(collision::rule_code_of_v<collision::I002_ClassifiedFailPayload>
              == collision::RuleCode::I002);
// FIXY-FOUND-134: replaces 38 hand-maintained per-rule static_asserts with
// a Pattern B reflection fold that iterates `enumerators_of(^^RuleCode)` and
// instantiates `rule_bijection_v<E>` for every non-sentinel enumerator.
// Auto-extends to new RuleCode atoms on append; closes FIXY-FOUND-075
// (the 9 codes G002 / V001 / V002 / V101 / V102 / V201 / V202 / V203 / V301
// shipped with `rule_tag` + `rule_code_of` specializations but no bijection
// assertion).  Mirrors the effects::is_observable Pattern B from FOUND-133.
consteval bool every_rule_code_has_bijection_() noexcept {
    static constexpr auto enumerators =
        std::define_static_array(std::meta::enumerators_of(^^collision::RuleCode));
    bool result = true;
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wshadow"
    template for (constexpr auto en : enumerators) {
        constexpr collision::RuleCode code = [:en:];
        // RuleCode::None is the "no violation" sentinel — no rule_tag
        // specialization exists for it (intentional; the inverse map
        // would have no payload).  Skip; every other code MUST bijct.
        if constexpr (code != collision::RuleCode::None) {
            result = result && collision::rule_bijection_v<code>;
        }
    }
#pragma GCC diagnostic pop
    return result;
}
static_assert(every_rule_code_has_bijection_(),
    "FIXY-FOUND-134: every RuleCode enumerator (excluding None) must satisfy "
    "rule_code_of_v<rule_tag_t<C>> == C.  Adding a new RuleCode requires a "
    "matching rule_tag + rule_code_of specialization; the reflection fold "
    "above instantiates rule_bijection_v<C> for every catalog atom.");
static_assert(collision::CollisionDiagnosticByRule<DefaultFn, collision::RuleCode::I002>::rule_code()
              == std::string_view{"I002"});
static_assert(collision::CollisionDiagnostic<DefaultFn>::category()
              == collision::RuleCode::None);
static_assert(collision::CollisionDiagnostic<DefaultFn>::rule_code()
              == std::string_view{"OK"});

using ConcurrentAtomic = Fn<int, pred::True, UsageMode::Linear,
                            effects::Row<effects::Effect::Bg>,
                            SecLevel::Public, proto::None, lifetime::Static,
                            source::FromInternal, trust::Verified,
                            ReprKind::Atomic, cost::Unstated,
                            precision::Exact, space::Zero,
                            OverflowMode::Trap, MutationMode::Monotonic>;
static_assert(ValidComposition<ConcurrentAtomic>);

static_assert(ValidComposition<FreshCtCarrier>);

}  // namespace detail::collision_catalog_self_test

}  // namespace crucible::safety::fn

#endif  // CRUCIBLE_SAFETY_COLLISION_CATALOG_BODY
#endif  // CRUCIBLE_SAFETY_FN_COLLISION_CATALOG_INTEGRATION
