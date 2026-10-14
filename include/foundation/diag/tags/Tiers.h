#pragma once

// The tags of the tier bands: the hot path, determinism, numerics, memory
// order, allocation, the vendor, the exits of a frame, consistency, lifetime,
// waits, progress, the Cipher tier, residency, the epoch, budgets, NUMA
// placement and the recipe.  The authoring rule of a tag is in
// foundation/diag/Tag.h.

#include <foundation/diag/Tag.h>

namespace foundation::diag {

struct HotPathViolation : tag_base {
    static constexpr tag_text_literal name = "HotPathViolation";
    static constexpr tag_text_literal description = "A function declared as HotPath<Hot, T> (the foreground recording "
                                                    "path: zero allocation, zero syscall, zero block) invoked a "
                                                    "callee whose HotPath grade is Warm (alloc OK, no syscall) or "
                                                    "Cold (block + IO OK).  The hot path's per-op latency budget is "
                                                    "shape-dependent; admitting Warm/Cold callees breaks the budget "
                                                    "structurally.  Crucible discipline per CLAUDE.md §IX.";
    static constexpr tag_text_literal remediation = "Either move the offending operation off the hot path (drain "
                                                    "to bg thread via SPSC ring; defer to a Warm-tier helper), OR "
                                                    "if the operation IS hot-path-safe, give its declaration a "
                                                    "HotPath<Hot, T> wrapper to admit it under the gate.  The "
                                                    "common refactor: a printf for debugging is Cold; replace with "
                                                    "atomic counter increment (Hot) and drain to a bg formatter.";

    static constexpr Severity severity = Severity::Error;
    static constexpr tag_text_literal why_this_matters = "The hot-path latency floor is the MESI cache-line transfer "
                                                         "cost — ~10-40 ns intra-socket, ~30-100 ns cross-socket "
                                                         "(CLAUDE.md §IX latency hierarchy).  Admitting a Warm callee "
                                                         "(allocation, ~50-200 ns) or Cold callee (block / IO, "
                                                         "~10-100 us) on a 5 ns recording path is a 10x to 10000x "
                                                         "slowdown.  HotPath<Hot, T> is the type system's promise that "
                                                         "the recorded code path stays within the latency floor; the "
                                                         "rejection here proves a regression would have shipped.";
    static constexpr tag_text_literal symptom_pattern = "Almost always surfaces after adding logging, asserting, or "
                                                        "instrumentation to a previously-clean hot-path TU.  The new "
                                                        "fprintf / std::cout / std::format call is Cold-tier; the "
                                                        "containing function's HotPath<Hot> declaration rejects the "
                                                        "transitive call at compile time, before the regression can "
                                                        "ship.  Less commonly: an inline-friendly helper that grew a "
                                                        "vector<T>::push_back over time.";
    static constexpr tag_text_literal correct_example = "static std::atomic<uint64_t> debug_counter; // Hot-path-safe";
    static constexpr tag_text_literal violating_example =
        "fprintf(stderr, \"debug %d\\n\", x); // Cold; rejected by Hot caller";
};

struct DetSafeLeak : tag_base {
    static constexpr tag_text_literal name = "DetSafeLeak";
    static constexpr tag_text_literal description = "The 8th axiom (DetSafe per CLAUDE.md §II.8) is violated: a "
                                                    "function declared as DetSafe<Pure, T> or DetSafe<PhiloxRng, "
                                                    "T> invoked a callee carrying MonotonicClockRead, "
                                                    "WallClockRead, EntropyRead, FilesystemMtime, or "
                                                    "NonDeterministicSyscall.  Same inputs → same outputs is "
                                                    "structurally broken; bit-exact replay (CI invariant) is "
                                                    "broken; cross-vendor numerics CI will reject downstream "
                                                    "outputs.  This is the load-bearing diagnostic that the "
                                                    "cache row fence enforces.";
    static constexpr tag_text_literal remediation = "Either eliminate the non-deterministic source (replace "
                                                    "wall-clock seed with Philox-derived seed; replace "
                                                    "/dev/urandom read with seeded Philox), OR if the operation "
                                                    "is genuinely impure (e.g., runtime metric collection), "
                                                    "lift the caller out of DetSafe<Pure> into "
                                                    "DetSafe<MonotonicClockRead> or higher tier.  Cipher::record_event "
                                                    "refuses any tier above PhiloxRng; the replay log cannot be "
                                                    "constructed from impure values.";

    static constexpr Severity severity = Severity::Fatal;
    static constexpr tag_text_literal why_this_matters = "DetSafe is the 8TH AXIOM (CLAUDE.md §II.8): same inputs → "
                                                         "same outputs, bit-identical under BITEXACT replay.  It is "
                                                         "the ONE axiom historically un-fenced — caught only by the "
                                                         "bit_exact_replay_invariant CI test ~12 hours after the "
                                                         "commit lands.  This rejection at compile time is what fences "
                                                         "the axiom.  A wall-clock read or /dev/urandom call recorded "
                                                         "into the replay log makes EVERY subsequent replay produce "
                                                         "different outputs; cross-vendor numerics CI then rejects "
                                                         "Mimic backends that 'fail' to match the corrupted oracle.  "
                                                         "Severity::Fatal because silent breakage cascades into hours "
                                                         "of debugging lost replay determinism.";
    static constexpr tag_text_literal symptom_pattern =
        "Surfaces when adding 'just one little timestamp' to a Cipher "
        "event-recording site for debugging.  Or when a refactor "
        "replaces seeded Philox with std::random_device for 'better "
        "randomness'.  Or when the runtime metric collector's wall-clock "
        "sample accidentally crosses into a record_event path.  All "
        "three are real production patterns the framework now catches.";
    static constexpr tag_text_literal correct_example =
        "DetSafe<Pure, uint64_t> seed = philox(counter, key); // deterministic";
    static constexpr tag_text_literal violating_example =
        "auto seed = std::chrono::steady_clock::now(); // wall clock; rejected";
};

struct NumericalTierMismatch : tag_base {
    static constexpr tag_text_literal name = "NumericalTierMismatch";
    static constexpr tag_text_literal description = "A function pinned at NumericalTier<BITEXACT_STRICT> or "
                                                    "NumericalTier<BITEXACT_TC> invoked a kernel whose recipe "
                                                    "tier is RELAXED, ULP_INT8, ULP_FP8, or ULP_FP16.  The "
                                                    "tier-pinned consumer requires bit-exact (or bounded-ULP) "
                                                    "outputs; the looser kernel cannot satisfy the contract.  "
                                                    "Recipe tier vocabulary per FORGE.md §20 / NumericalRecipe.h.";
    static constexpr tag_text_literal remediation = "Either select a recipe whose tier matches the caller's "
                                                    "requirement (use Forge Phase E.RecipeSelect's fleet "
                                                    "intersection picker constrained to the required tier), OR "
                                                    "loosen the caller's tier pin if bit-exact is not actually "
                                                    "required for this code path.  Cross-vendor numerics CI "
                                                    "pairwise-validates recipe outputs against the CPU scalar-FMA "
                                                    "oracle per tolerance — verify the looser tier still meets "
                                                    "the application's accuracy requirement.";

    static constexpr Severity severity = Severity::Error;
    static constexpr tag_text_literal why_this_matters =
        "Recipe tiers (FORGE.md §20) discipline the cross-vendor "
        "numerics CI: BITEXACT_STRICT recipes produce byte-identical "
        "output across all backends; BITEXACT_TC tolerate ≤1 ULP "
        "tensor-core deviation; ORDERED enforce a per-recipe tolerance; "
        "UNORDERED admit reduction-order changes.  A caller pinned at "
        "BITEXACT_STRICT calling an UNORDERED kernel silently propagates "
        "non-determinism through the model — checkpoint loads from a "
        "BITEXACT chain produce divergent outputs after the UNORDERED "
        "kernel runs.  The recipe-tier discipline is what makes "
        "federation work.";
    static constexpr tag_text_literal symptom_pattern = "Often surfaces after a Mimic backend ships a fast-path "
                                                        "ALLREDUCE (UNORDERED tier) and a downstream training step "
                                                        "expecting BITEXACT_TC tries to consume its output.  Or after "
                                                        "a Forge Phase E recipe-select picker switches to a tighter "
                                                        "BITEXACT_STRICT recipe and a previously-OK ORDERED kernel is "
                                                        "now rejected.";
    static constexpr tag_text_literal correct_example =
        "select_recipe<Tolerance::BITEXACT_TC>(KernelKind::GEMM_MM, fleet);";
    static constexpr tag_text_literal violating_example =
        "auto k = unordered_allreduce(x); // UNORDERED; rejected by BITEXACT";
};

struct MemOrderViolation : tag_base {
    static constexpr tag_text_literal name = "MemOrderViolation";
    static constexpr tag_text_literal description = "A hot binding names a fence at or above seq_cst.  The "
                                                    "BarrierStrength atoms of fixy/atoms/Barrier.h record the "
                                                    "fence strength that a binding provides, and collision rule "
                                                    "V301 refuses a hot binding that names seq_cst or full_fence.  "
                                                    "A binding that names no strength provides no fence, and V301 "
                                                    "does not fire on it.";
    static constexpr tag_text_literal remediation = "Name the weakest fence that the protocol needs: acq_rel for a "
                                                    "read-modify-write, release_store for a publication, and "
                                                    "acquire_load for a read of a publication.  If the code needs "
                                                    "one total order across more than one atomic, examine the "
                                                    "ownership boundaries again.  A total order is almost always a "
                                                    "sign that they are wrong.  If the total order is necessary, "
                                                    "move the fence to a binding that is not hot.";

    static constexpr Severity severity = Severity::Error;
    static constexpr tag_text_literal why_this_matters = "A standalone seq_cst fence emits MFENCE on x86 and DMB ISH "
                                                         "on ARM, and each one waits for the store buffer to drain.  "
                                                         "Acquire and release ordering is sufficient for every ring, "
                                                         "every snapshot and every lock-free pattern that Crucible "
                                                         "uses (CLAUDE.md section IX).  A seq_cst fence on the hot "
                                                         "path costs one or two orders of magnitude of latency, and it "
                                                         "usually shows that nobody examined the ordering that the "
                                                         "protocol needs.";
    static constexpr tag_text_literal symptom_pattern = "Surfaces in code that came from outside the project, for "
                                                        "example a lock-free fragment copied from a reference.  Or in "
                                                        "code refactored from a region under a mutex without an "
                                                        "examination of the ordering that it needs.  The diagnostic "
                                                        "names the hot binding and the strength that it provides.";
    static constexpr tag_text_literal correct_example =
        "x.store(v, std::memory_order_release);  // release_store: a publication needs no more";
    static constexpr tag_text_literal violating_example =
        "std::atomic_thread_fence(std::memory_order_seq_cst);  // seq_cst on a hot binding: V301";
};

struct AllocClassViolation : tag_base {
    static constexpr tag_text_literal name = "AllocClassViolation";
    static constexpr tag_text_literal description = "A function pinned at AllocClass<Stack>, AllocClass<Pool>, or "
                                                    "AllocClass<Arena> attempted to allocate from Heap or Mmap "
                                                    "(or invoked a callee that does).  The hot path forbids heap "
                                                    "allocation: malloc round-trip is ~50-200ns and unpredictable; "
                                                    "Arena bump is ~2ns and lock-free.  Crucible discipline per "
                                                    "CLAUDE.md §XVIII.";
    static constexpr tag_text_literal remediation = "Replace heap allocation with arena allocation: use "
                                                    "Arena::alloc_obj<T>() / Arena::alloc_array<T>(n) for DAG-"
                                                    "lifetime objects; PoolAllocator for object-pool patterns; "
                                                    "static buffers for genuinely-fixed-size data.  If the "
                                                    "allocation is required and cannot be moved off the hot path, "
                                                    "lift the caller's AllocClass to Heap explicitly — but "
                                                    "document why the hot-path discipline is being relaxed.";

    static constexpr Severity severity = Severity::Error;
    static constexpr tag_text_literal why_this_matters =
        "Hot-path code MUST NOT allocate from the heap (CLAUDE.md §XVIII): "
        "malloc round-trip is ~50-200 ns, unpredictable under "
        "contention, and breaks the per-iteration latency budget.  "
        "Arena bump allocation is ~2 ns and lock-free; PoolAllocator "
        "is ~5 ns and bounded.  The AllocClass wrapper makes the "
        "discipline visible in function signatures so a refactor that "
        "introduces std::vector::push_back into the hot path is "
        "rejected at the call site before it ships.";
    static constexpr tag_text_literal symptom_pattern = "Almost always surfaces after a refactor that 'just adds a "
                                                        "vector<T>' to collect intermediate results, OR after using "
                                                        "std::string concatenation in a logging path, OR after "
                                                        "refactoring an arena-backed buffer to std::array<T, N> via a "
                                                        "helper that returns std::vector instead.";
    static constexpr tag_text_literal correct_example = "auto* buf = arena.alloc_array<float>(n); // Arena, ~2 ns";
    static constexpr tag_text_literal violating_example =
        "auto buf = std::vector<float>(n); // Heap; rejected on hot path";
};

struct VendorBackendMismatch : tag_base {
    static constexpr tag_text_literal name = "VendorBackendMismatch";
    static constexpr tag_text_literal description = "A kernel pinned at Vendor<NV>, Vendor<AMD>, Vendor<TPU>, "
                                                    "Vendor<TRN>, or Vendor<CER> was emitted by, or routed to, "
                                                    "the wrong vendor's Mimic backend.  Each vendor's backend "
                                                    "owns its IR003* lowering and native ISA emission; cross-"
                                                    "vendor mismatches indicate a routing bug in Forge Phase H "
                                                    "(MIMIC.md §22) or a recipe-registry drift between "
                                                    "advertised native_on bitmaps and actual emit support.";
    static constexpr tag_text_literal remediation = "Verify the kernel's Vendor pin matches the target backend at "
                                                    "the dispatcher level.  Cross-vendor portability is an explicit "
                                                    "design choice (Vendor<Portable> admits any backend); if the "
                                                    "kernel is Portable, the routing layer should pick a backend "
                                                    "based on the active TargetCaps — not propagate the Portable "
                                                    "tag downstream.";

    static constexpr Severity severity = Severity::Error;
    static constexpr tag_text_literal why_this_matters = "Mimic per-vendor backends (mimic::nv / am / tpu / trn / cpu) "
                                                         "are NOT interchangeable — each emits an instruction stream "
                                                         "valid only for its target ISA.  A Vendor-tagged kernel "
                                                         "(MIMIC.md §22) must be routed to the matching backend; "
                                                         "feeding an `Vendor::AMD`-rowed KernelNode<...> to "
                                                         "mimic::nv::compile_kernel produces SASS that doesn't match "
                                                         "the AMD GPU's instruction set, then surfaces as a runtime "
                                                         "kernel-launch failure or silent miscompile.  The compile-"
                                                         "time fence catches this BEFORE the silicon does.";
    static constexpr tag_text_literal symptom_pattern = "Surfaces when a kernel originally compiled for one vendor "
                                                        "is reused on another via a copy-paste of dispatch wiring "
                                                        "without updating the Vendor tag.  Less commonly: a fleet "
                                                        "join brings in peers of a different vendor and reshard "
                                                        "neglects to re-emit per-vendor.  The diagnostic fires at "
                                                        "the first mimic::*::compile_kernel call site whose row "
                                                        "doesn't include the backend's vendor tag.";
    static constexpr tag_text_literal correct_example = "mimic::nv::compile_kernel(node_with_Vendor_NV_row, ...);";
    static constexpr tag_text_literal violating_example =
        "mimic::nv::compile_kernel(node_with_Vendor_AMD_row, ...);  // WRONG";
};

struct CrashClassMismatch : tag_base {
    static constexpr tag_text_literal name = "CrashClassMismatch";
    static constexpr tag_text_literal description = "A binding that must leave its frame only by a return called "
                                                    "a callee that can leave it another way.  The ctrl atoms of "
                                                    "fixy/atoms/Ctrl.h record each other exit: throws, abort, "
                                                    "longjmp_unsafe and exit.  A binding with no ctrl atom leaves "
                                                    "only by a return, and that is the strict default.  Nothing in "
                                                    "this tree throws, and the rule artifact-symbols fails the "
                                                    "build when __cxa_throw reaches an artifact.";
    static constexpr tag_text_literal remediation = "Two routes.  (a) If the callee can fail, return "
                                                    "std::expected<T, E> from it.  The caller then handles the "
                                                    "failure explicitly.  (b) If the failure cannot occur at this "
                                                    "call site, call the callee through an adapter that calls a "
                                                    "cold [[noreturn]] helper, which calls std::abort(), on the "
                                                    "impossible case.  The adapter carries a "
                                                    "ctrl::abort atom that states the reason, and the caller admits "
                                                    "that exit by name.";

    static constexpr Severity severity = Severity::Error;
    static constexpr tag_text_literal why_this_matters = "The ctrl atoms classify the ways a binding leaves its frame "
                                                         "other than by a return.  A binding with no ctrl atom leaves "
                                                         "only by a return, and an error comes back as a "
                                                         "std::expected value.  When such a binding calls a callee "
                                                         "that can abort, it loses that guarantee in the middle of a "
                                                         "protocol.  The rollback, recovery and replay reasoning that "
                                                         "depends on the guarantee then fails without a diagnostic.";
    static constexpr tag_text_literal symptom_pattern = "Surfaces after a refactor that replaces a helper that returns "
                                                        "std::expected with one that aborts on failure, for example a "
                                                        "change to contract_assert.  The new helper carries a "
                                                        "ctrl::abort atom, and its caller admits no ctrl atom.  The "
                                                        "protocol step that a caller could recover from is now a "
                                                        "process kill.  The caller must go back to an error return, or "
                                                        "admit the abort by name and update the rollback logic.";
    static constexpr tag_text_literal correct_example = "std::expected<T, E> safe_op() noexcept;  // recoverable";
    static constexpr tag_text_literal violating_example =
        "T dangerous_op() noexcept;  // calls std::abort(), so it carries ctrl::abort";
};

struct ConsistencyMismatch : tag_base {
    static constexpr tag_text_literal name = "ConsistencyMismatch";
    static constexpr tag_text_literal description = "Reserved for a replicated value that reaches a consumer at a "
                                                    "weaker consistency level than the consumer needs.  No binding "
                                                    "or wrapper in this tree carries a consistency level, so nothing "
                                                    "emits this category.";
    static constexpr tag_text_literal remediation = "No action is necessary, because nothing emits this category.  "
                                                    "A consistency level needs a written meaning and a partial order "
                                                    "before a wrapper can carry it.";

    static constexpr Severity severity = Severity::Error;
    static constexpr tag_text_literal why_this_matters = "The levels do not form a chain.  A chain puts "
                                                         "BOUNDED_STALENESS above READ_YOUR_WRITES.  But a read "
                                                         "that is K steps stale can miss a write that the reader "
                                                         "made.  So bounded staleness does not imply read-your-writes, "
                                                         "and the levels need a partial order.  "
                                                         "Bounded staleness also has a carrier already: the staleness "
                                                         "semiring of fixy/Stale.h.  The category keeps its index, "
                                                         "because each index is an ordinal pin of the federation "
                                                         "cache keys.";
    static constexpr tag_text_literal symptom_pattern = "Does not surface.  Nothing in this tree emits the category.";
    static constexpr tag_text_literal correct_example =
        "// No example: nothing in this tree carries a consistency level.";
    static constexpr tag_text_literal violating_example =
        "// No example: nothing in this tree emits ConsistencyMismatch.";
};

struct LifetimeViolation : tag_base {
    static constexpr tag_text_literal name = "LifetimeViolation";
    static constexpr tag_text_literal description = "An OpaqueLifetime<PER_REQUEST, T> value crossed a boundary "
                                                    "into a PER_PROGRAM or PER_FLEET scope.  The lifetime "
                                                    "lattice's chain order is "
                                                    "PER_REQUEST ⊑ PER_PROGRAM ⊑ PER_FLEET; promoting a "
                                                    "PER_REQUEST value to longer lifetime leaks per-request data "
                                                    "across requests (security / isolation violation).  Pattern "
                                                    "from Pie SOSP 2025 inferlets.";
    static constexpr tag_text_literal remediation = "Either rebuild the value at the longer-lifetime boundary "
                                                    "(so the longer-lifetime cell holds a fresh value, not a "
                                                    "borrowed PER_REQUEST one), OR if the value is genuinely "
                                                    "PER_FLEET-correct, lift its construction site to declare "
                                                    "OpaqueLifetime<PER_FLEET, T>.  Cipher tier promotion: "
                                                    "PER_REQUEST goes to hot tier only; PER_FLEET writes go to "
                                                    "cold tier (S3); never promote across tiers via aliasing.";

    static constexpr Severity severity = Severity::Fatal;
    static constexpr tag_text_literal why_this_matters =
        "OpaqueLifetime<L, T> (algebra/lattices/LifetimeLattice.h) "
        "tags a value's persistence scope: PER_REQUEST ⊏ "
        "PER_PROGRAM ⊏ PER_FLEET.  A PER_REQUEST value contains "
        "request-local data (PII, session IDs, transient credentials); "
        "promoting it to PER_FLEET via Cipher cold tier OR Canopy "
        "Raft replication LEAKS the data across requests / tenants / "
        "machines.  In multi-tenant deployments this is a security "
        "incident requiring customer notification and (depending on "
        "jurisdiction) regulatory disclosure.  Severity::Fatal "
        "because the consequence class is data exfiltration, not "
        "performance.";
    static constexpr tag_text_literal symptom_pattern = "Surfaces when a refactor consolidates a per-request handler "
                                                        "with a per-fleet broadcast path and forgets to scrub the "
                                                        "request-local fields before publication.  Or when adding "
                                                        "telemetry to an inferlet (per-request user state) without "
                                                        "marking the metric as PER_FLEET-aggregated.  The diagnostic "
                                                        "names the source value's lifetime AND the destination's "
                                                        "required lifetime; remediation is almost always 'redact "
                                                        "first, then promote'.";
    static constexpr tag_text_literal correct_example =
        "publish_to_fleet(OpaqueLifetime<PER_FLEET, Aggregate>{value});";
    static constexpr tag_text_literal violating_example =
        "publish_to_fleet(OpaqueLifetime<PER_REQUEST, RawTrace>{value});";
};

struct WaitStrategyViolation : tag_base {
    static constexpr tag_text_literal name = "WaitStrategyViolation";
    static constexpr tag_text_literal description = "A function pinned at Wait<SpinPause> (intra-core wait, "
                                                    "10-40ns latency) invoked a callee whose wait strategy is "
                                                    "Park (futex / mutex, 1-5μs), Block (kernel scheduler, "
                                                    "10-100μs), or worse.  The hot path's wait latency budget is "
                                                    "the MESI cache-line transfer cost; admitting park/block "
                                                    "callees blows the budget by 2-4 orders of magnitude.  "
                                                    "Wait-strategy hierarchy per CLAUDE.md §IX.";
    static constexpr tag_text_literal remediation = "Either replace the slow wait with a SpinPause loop "
                                                    "(_mm_pause on x86, yield on ARM) — appropriate when the "
                                                    "expected wait is sub-μs, OR move the wait off the hot path "
                                                    "to a bg-thread helper that can afford Park/Block.  If the "
                                                    "wait is genuinely necessary on the hot path (rare), document "
                                                    "the lift from SpinPause to Park with a justification "
                                                    "comment.";

    static constexpr Severity severity = Severity::Error;
    static constexpr tag_text_literal why_this_matters = "Wait<Strategy, T> (28_04 §4.3.3) classifies wait costs: "
                                                         "SpinPause (≤40 ns intra-socket via MESI) ⊏ BoundedSpin "
                                                         "(N spins then back off) ⊏ UmwaitC01 (C0.1/C0.2 sleep) ⊏ "
                                                         "AcquireWait (atomic::wait/notify, futex-backed, ~1-5 us) "
                                                         "⊏ Park (jthread parking) ⊏ Block (mutex/condvar, ms).  "
                                                         "Hot-path callers admit only SpinPause; mixing in a Park "
                                                         "or Block via a transitively-called helper turns a 5 ns "
                                                         "recording into a 1-5 us syscall — a 1000× slowdown.  The "
                                                         "fence catches the regression at the call site, before "
                                                         "performance triage hours later.";
    static constexpr tag_text_literal symptom_pattern = "Surfaces after adding logging or metrics to a hot-path TU "
                                                        "where the new logger uses std::cout (line-buffered → "
                                                        "fwrite_lock → futex → Block).  Or replacing a custom "
                                                        "spin-wait with std::condition_variable for 'simplicity'.  "
                                                        "The diagnostic names the offending callee's Wait grade "
                                                        "(usually Park or Block) and the caller's required grade "
                                                        "(usually SpinPause).";
    static constexpr tag_text_literal correct_example = "while (!flag.load(acquire)) Wait<SpinPause>{}.do_pause();";
    static constexpr tag_text_literal violating_example =
        "flag.wait(false);  // Wait<AcquireWait> in a SpinPause caller";
};

struct ProgressClassViolation : tag_base {
    static constexpr tag_text_literal name = "ProgressClassViolation";
    static constexpr tag_text_literal description = "A hot binding has Block in its row, so a call into it can wait "
                                                    "with no bound on when the wait ends.  This tree has no "
                                                    "termination class.  fixy/Aliases.h spells divergence as Block, "
                                                    "the effect atom for a wait with no guaranteed bound.  Collision "
                                                    "rule W001 refuses a hot binding whose row contains Block: a "
                                                    "kernel wait, a system call that can park the caller, or a "
                                                    "stated Block.";
    static constexpr tag_text_literal remediation = "Two routes.  (a) Replace the kernel wait with a wait that "
                                                    "stays in user space, which fixy/atoms/Sync.h records as "
                                                    "spin_pause or bounded_spin.  (b) If the binding must wait in "
                                                    "the kernel, it is not hot.  Move the wait to a background "
                                                    "binding, or remove the hot tier and keep Block in the row.";

    static constexpr Severity severity = Severity::Error;
    static constexpr tag_text_literal why_this_matters = "The scheduler bounds a kernel wait, and that bound is "
                                                         "microseconds or more.  The hot path has a budget of "
                                                         "nanoseconds.  A hot binding that reaches such a wait "
                                                         "through a callee misses its budget, and nothing at the call "
                                                         "site shows the cause.  The row carries the wait up to the "
                                                         "binding, and W001 reads the row.  A new atom that can block "
                                                         "puts Block in its lift, and W001 then refuses it with no "
                                                         "edit to the rule.";
    static constexpr tag_text_literal symptom_pattern = "Surfaces after a refactor adds a lock, a condition wait or a "
                                                        "system call that can park the caller to a helper that a hot "
                                                        "binding calls.  The atoms of the helper put Block in the row, "
                                                        "and the diagnostic names the hot binding.";
    static constexpr tag_text_literal correct_example =
        "while (!flag.load(std::memory_order_acquire)) CRUCIBLE_SPIN_PAUSE;  // user space, no Block";
    static constexpr tag_text_literal violating_example =
        "flag.wait(false);  // a kernel wait puts Block in the row of a hot binding: W001";
};

struct CipherTierViolation : tag_base {
    static constexpr tag_text_literal name = "CipherTierViolation";
    static constexpr tag_text_literal description = "A Cipher operation pinned at CipherTier<Hot> (other Relays' "
                                                    "RAM via RAID) was invoked at a context expecting Warm "
                                                    "(local NVMe) or Cold (S3 / GCS), or vice versa.  Tier "
                                                    "discipline per CRUCIBLE.md §L14: Hot writes are cheap and "
                                                    "ephemeral; Warm writes survive reboot; Cold writes survive "
                                                    "total cluster failure.  Mixing tiers loses the invariant the "
                                                    "tier was chosen for.";
    static constexpr tag_text_literal remediation = "Use the explicit per-tier API: Cipher::publish_hot for "
                                                    "RAID-replicated ephemeral state, publish_warm for NVMe "
                                                    "writes that survive reboot, publish_cold for S3 writes that "
                                                    "survive cluster failure.  Tier promotion is explicit: a "
                                                    "Hot value migrates to Warm via promote_to_warm() (cost: "
                                                    "fsync); to Cold via promote_to_cold() (cost: network).";

    static constexpr Severity severity = Severity::Error;
    static constexpr tag_text_literal why_this_matters =
        "CipherTier<Tier, T> (CRUCIBLE.md §L14: Hot ⊏ Warm ⊏ Cold) "
        "carries a value's persistence tier.  Hot tier is other-Relay "
        "RAM (RAID-style replication; ns-scale recovery on single-node "
        "failure), Warm is local NVMe (1/N FSDP shard; second-scale "
        "recovery from reboot), Cold is durable storage S3/GCS "
        "(minute-scale recovery from total cluster failure).  "
        "Mis-tiered persistence has hard consequences: a Hot value "
        "demoted to Cold loses ns-recovery; a Cold value promoted "
        "to Hot occupies precious other-Relay RAM that should hold "
        "active state.  The fence preserves the recovery-time SLA.";
    static constexpr tag_text_literal symptom_pattern = "Surfaces when Cipher::publish_warm is called on a value "
                                                        "whose CipherTier grade is Cold (e.g., a long-archive log "
                                                        "entry mistakenly routed through the warm tier).  Or when a "
                                                        "tier-promotion path (Hot → Warm at FSDP shard boundary) "
                                                        "loses the explicit tier marker via type erasure.  Diagnostic "
                                                        "names the source tier AND the destination's required tier.";
    static constexpr tag_text_literal correct_example =
        "Cipher::publish_warm(CipherTier<CipherTier::Warm, Shard>{value});";
    static constexpr tag_text_literal violating_example =
        "Cipher::publish_warm(CipherTier<CipherTier::Cold, Archive>{value});";
};

struct ResidencyHeatViolation : tag_base {
    static constexpr tag_text_literal name = "ResidencyHeatViolation";
    static constexpr tag_text_literal description =
        "Reserved for a value that gets to a consumer that accepts only a hotter residency.  fixy::ResidencyHeat in "
        "fixy/Bands.h pins the cache level that holds the working set of a value.  Hot is L1, Warm is L2, and Cold is "
        "L3 or DRAM.  These are the levels of the CPU cache hierarchy.  The L1, L2 and L3 tiers of the KernelCache "
        "grade portability, which is a different axis.  Nothing in this tree emits this category.";
    static constexpr tag_text_literal remediation =
        "No action is necessary for the category, because nothing emits it.  When the compiler rejects a residency, "
        "lower the residency that the consumer accepts, or make the value where its working set stays at that "
        "level.  relax moves only toward Cold, because a move up would claim a residency that nothing measured.";

    static constexpr Severity severity = Severity::Warning;
    static constexpr tag_text_literal why_this_matters =
        "The residency tells a consumer what one access costs.  An L1 hit costs a few cycles, and an L3 or DRAM "
        "access costs tens to hundreds of cycles.  A consumer that accepts Warm also accepts a Hot value, and a "
        "consumer that accepts only Hot rejects a Cold value.  A mismatch is a compile error: satisfies_v is false, "
        "relax rejects a move up, and a Hot parameter does not take a Cold value.  Severity::Warning rather than "
        "Error, because a residency error decreases performance but does not corrupt state.  The category keeps its "
        "index, because each index is an ordinal pin of the federation cache keys.";
    static constexpr tag_text_literal symptom_pattern =
        "Does not surface.  Nothing in this tree emits the category.  A residency mismatch shows as a compile error "
        "at the call that gives a Warm or Cold value to a consumer that accepts only residency_heat::Hot.";
    static constexpr tag_text_literal correct_example =
        "static_assert(fixy::satisfies_v<fixy::residency_heat::Hot<Key>, fixy::ResidencyHeatTag_v::Warm>);";
    static constexpr tag_text_literal violating_example =
        "static_assert(fixy::satisfies_v<fixy::residency_heat::Cold<Key>, fixy::ResidencyHeatTag_v::Hot>);  // false";
};

struct EpochMismatch : tag_base {
    static constexpr tag_text_literal name = "EpochMismatch";
    static constexpr tag_text_literal description =
        "Reserved for a fixy::EpochVersioned value whose version is too old for its gate.  fixy/EpochVersioned.h "
        "spells EpochVersioned<T>: a payload with the fleet epoch and the per-node generation.  Only a VersionSource, "
        "minted with an Init context, gives the VersionStamp that sets a version.  is_at_least returns false when one "
        "counter is below the requirement.  select_fresher returns VersionConflict::Incomparable or "
        "VersionConflict::Divergent.  Nothing in this tree emits this category.";
    static constexpr tag_text_literal remediation =
        "No action is necessary for the category, because nothing emits it.  When is_at_least returns false, make "
        "the value again with a stamp from VersionSource::stamp().  For a value from a peer, adopt the newer version "
        "at the source, and then stamp the value with stamp_received().  An Incomparable or Divergent pair has no "
        "fresher operand, and the caller decides which value holds.";

    static constexpr Severity severity = Severity::Error;
    static constexpr tag_text_literal why_this_matters =
        "The version tells a reader which cluster state made the payload.  A gate accepts a value at or above a "
        "version, and a false high version makes a stale payload read as fresh.  A producer cannot state a version, "
        "and each stamp names a version that its source reached.  A value from an older epoch was calculated against "
        "a membership that no longer holds.  A collective that uses it calculates against the wrong partition.  The "
        "category keeps its index, because each index is an ordinal pin of the federation cache keys.";
    static constexpr tag_text_literal symptom_pattern =
        "Does not surface.  Nothing in this tree emits the category.  A caller sees the version check as the false "
        "return of is_at_least, or as the error of select_fresher.";
    static constexpr tag_text_literal correct_example =
        "fixy::EpochVersioned<Shard> const fresh{rebuild(shard), source.stamp()};";
    static constexpr tag_text_literal violating_example =
        "do_collective(stale);  // stamped at epoch N-1: is_at_least(EpochBound{N}, GenerationBound{0}) is false";
};

struct BudgetExceeded : tag_base {
    static constexpr tag_text_literal name = "BudgetExceeded";
    static constexpr tag_text_literal description =
        "Reserved for a fixy::Budgeted value with a grade above the bound of its gate.  fixy/Budgeted.h spells "
        "Budgeted<T> over BudgetLattice: a payload with the bits it transferred and the peak bytes it held.  Only a "
        "BudgetStamp from a BudgetAuthority, minted with an Init context, sets a finite grade.  satisfies returns "
        "false when one grade is above its bound, and a value that nothing measured is unbounded.  Nothing in this "
        "tree emits this category.";
    static constexpr tag_text_literal remediation =
        "No action is necessary for the category, because nothing emits it.  When satisfies returns false, decrease "
        "the use: smaller intermediate types, a smaller working set, or buffers that an arena uses again.  If the "
        "larger use is correct, get a larger grant from the BudgetAuthority.  A producer cannot state its own grade, "
        "and one stamp grades one value.";

    static constexpr Severity severity = Severity::Error;
    static constexpr tag_text_literal why_this_matters =
        "A grade is a claim about what the payload used, and a smaller grade is a stronger claim.  A producer that "
        "states its own grade can claim zero and pass every gate.  Only a BudgetAuthority grants an allowance, and "
        "one stamp grades one value, because a stamp does not copy.  accumulate adds the grades of a chain of stages "
        "with a saturating sum, and combine_max takes the larger grades of two parallel paths.  A grant bounds work "
        "only when the work spends through resources that draw on the grant.  The category keeps its index, because "
        "each index is an ordinal pin of the federation cache keys.";
    static constexpr tag_text_literal symptom_pattern =
        "Does not surface.  Nothing in this tree emits the category.  A caller sees the budget check as the false "
        "return of satisfies, for example after accumulate sums a chain of stages above the bound, or for an "
        "unbounded value.";
    static constexpr tag_text_literal correct_example =
        "fixy::Budgeted<Shard> const v{shard, authority.grant(BitsBudgetBound{1024}, PeakBytesBound{1 << 20})};";
    static constexpr tag_text_literal violating_example =
        "a.accumulate(b).satisfies(BitsBudgetBound{1024}, PeakBytesBound{1 << 20});  // false: 800 + 500 bits";
};

struct NumaPlacementMismatch : tag_base {
    static constexpr tag_text_literal name = "NumaPlacementMismatch";
    static constexpr tag_text_literal description = "A NumaPlacement<Node, Affinity, T> value was consumed at a "
                                                    "thread whose CPU affinity is not on the value's declared NUMA "
                                                    "node.  Per CLAUDE.md §VIII OS-tuning: cross-node memory "
                                                    "access is 2-4× slower than NUMA-local; the AdaptiveScheduler "
                                                    "(THREADING.md §5.4) routes work to NUMA-local cores when "
                                                    "the working set is L3-resident or DRAM-bound.";
    static constexpr tag_text_literal remediation = "Either pin the consuming thread to the value's NUMA node "
                                                    "(pthread_setaffinity_np / sched_setaffinity), OR migrate the "
                                                    "value to the consuming thread's NUMA node before consumption "
                                                    "(numa_move_pages).  AdaptiveScheduler does this automatically "
                                                    "for parallel_for_views / parallel_apply_pair when the cost "
                                                    "model recommends NumaLocal placement.";

    static constexpr Severity severity = Severity::Warning;
    static constexpr tag_text_literal why_this_matters = "NumaPlacement<Node, Affinity, T> (28_04 §4.4.3) carries "
                                                         "a value's NUMA locality: which node owns its memory + the "
                                                         "thread affinity it expects.  A Node-3-tagged region "
                                                         "operated on by a Node-0-affined thread incurs cross-socket "
                                                         "memory latency on every access (~3-4× slower than local).  "
                                                         "Severity::Warning rather than Error because correctness is "
                                                         "preserved — only performance degrades — but on production "
                                                         "fleets this is a measurable revenue-cost regression.  The "
                                                         "fence catches the mis-placement BEFORE bench triage.";
    static constexpr tag_text_literal symptom_pattern = "Surfaces when AdaptiveScheduler dispatches a NumaLocal-"
                                                        "tagged work item to a NumaSpread thread pool (or vice "
                                                        "versa).  Or when a NumaPlacement<Node-N> region is fed to "
                                                        "a body whose own NUMA preference targets a different node.  "
                                                        "Diagnostic names the value's node + affinity AND the "
                                                        "consuming operation's expected combination.";
    static constexpr tag_text_literal correct_example =
        "scheduler.dispatch_local(NumaPlacement<Node{0}, Local>{region});";
    static constexpr tag_text_literal violating_example =
        "scheduler.dispatch_local(NumaPlacement<Node{3}, Spread>{region});";
};

struct RecipeSpecMismatch : tag_base {
    static constexpr tag_text_literal name = "RecipeSpecMismatch";
    static constexpr tag_text_literal description = "A RecipeSpec<Tier, Family, T> value was consumed at a Forge "
                                                    "Phase E.RecipeSelect picker that requires a different "
                                                    "(tier, family) combination, or was offered to a Mimic backend "
                                                    "whose native_on bitmap doesn't include the recipe's family "
                                                    "(PAIRWISE / LINEAR / KAHAN / BLOCK_STABLE).  Recipe registry "
                                                    "drift between recipes.json declarations and actual backend "
                                                    "support manifests as this diagnostic.";
    static constexpr tag_text_literal remediation = "Verify the recipe's declared (tier, family) matches the call "
                                                    "site's pin.  If the family is unsupported on the target "
                                                    "backend, the recipe registry's native_on bitmap should not "
                                                    "have advertised support — file a registry update.  If the "
                                                    "tier is wrong, see NumericalTierMismatch for tier-pinning "
                                                    "remediations.";

    static constexpr Severity severity = Severity::Error;
    static constexpr tag_text_literal why_this_matters = "RecipeSpec<Tier, Family, T> (28_04 §4.4.4) carries a "
                                                         "kernel's numerical recipe along TWO axes: ToleranceLattice "
                                                         "tier (BITEXACT_STRICT ⊐ BITEXACT_TC ⊐ ULP_FP64 ⊐ ... ⊐ "
                                                         "RELAXED) AND RecipeFamily (PAIRWISE / LINEAR / KAHAN / "
                                                         "BLOCK_STABLE).  Mismatched tiers produce nonequivalent "
                                                         "outputs (wrong by ULP); mismatched families break "
                                                         "associativity assumptions (a PAIRWISE-callable site fed "
                                                         "a LINEAR result has different reduction order, breaking "
                                                         "BITEXACT replay).  The fence ENFORCES the cross-vendor "
                                                         "numerics CI's (MIMIC.md §41) per-recipe equivalence "
                                                         "contract at the call site rather than in CI 12 hours later.";
    static constexpr tag_text_literal symptom_pattern = "Surfaces when Forge Phase E.RecipeSelect picks a tier "
                                                        "different from the caller's declared bound, OR when a "
                                                        "fleet-intersection narrows the available recipes such "
                                                        "that no candidate satisfies the row's RecipeSpec "
                                                        "constraint.  Diagnostic names BOTH axes mismatched; "
                                                        "common remediation is a recipe-family change OR a "
                                                        "tier relaxation (with documented impact on bit-exactness).";
    static constexpr tag_text_literal correct_example = "compile(RecipeSpec<BITEXACT_TC, PAIRWISE, Kernel>{node});";
    static constexpr tag_text_literal violating_example =
        "compile(RecipeSpec<RELAXED, KAHAN, Kernel>{node});  // wrong axes";
};

}  // namespace foundation::diag
