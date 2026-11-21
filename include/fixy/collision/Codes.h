#pragma once

// The codes of the collision rules, and the corpus that gives the
// disposition of each code.
//
// fixy/Collision.h holds the rules, and it includes this header.  Each
// rule has one code.  The negative fixtures read the codes, so a change
// does not give a code a different name or a second rule.  The corpus is
// the list of the specification, and each entry tells if its rule is
// live, pending, absent or retired.  fixy/collision/Report.h compares the
// corpus with the rules that the build compiles.

#include <fixy/Axis.h>
#include <foundation/reflect/ReflectionList.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <meta>
#include <string>
#include <string_view>

namespace fixy {

namespace collision {

// ---------------------------------------------------------------------
// The rule codes.  Letter codes are stable API: the negative corpus
// greps them, so a code is never renamed or reused.

// The order of the enumerators is the order of the rule table below, so a
// refusal names the codes that a pack trips in this order.
enum class RuleCode : std::uint8_t {
    L002,  // borrow x async
    M012,  // monotonic x concurrent without atomic representation
    P010,  // ghost x observable effect
    L007,  // borrow x Bg row
    T001,  // capability x unverified trust
    R002,  // coroutine x borrow
    R003,  // coroutine x Bg row
    L006,  // linear x longjmp
    G002,  // thread-local x atomic representation x no reader on another thread
    D002,  // unbounded recursion x an unbounded or unstated cost
    P002,  // ghost x an emitting surface other than the effect row
    // The codes from here to F102 read an axis family.  Each is a live rule,
    // so pending_rules below is empty.
    H001,
    H002,
    H003,
    H010,
    R001,
    S001,
    W001,
    W002,
    B001,
    // B002 is a code of its own, not a renaming of B001.  B001's theorem
    // is the back-pressure trap, with its own remedy.  The containment
    // invariant below is a different theorem about the same axis, and the
    // codes are stable API that is never reused.
    B002,
    V201,
    V202,
    V203,
    V301,
    V401,
    V101,
    V402,
    F101,
    F102,
    // An Absent or Retired code has no enumerator, and a pin in the check
    // file of this header holds that: this enum holds exactly the codes
    // that a rule of fixy/Collision.h can be named by, and any other code
    // is named only by the corpus, as a string.  Nothing is renamed and
    // nothing is reused: the corpus carries each string, with the reason
    // that it cannot fire or is retired.
    //
    // Each premise of these three is a grade that a binding states.
    S011,
    D001,
    L003,
    // The constant-time family and the failure family.  Each reads the
    // Security grade atom::constant_time, a failure path in the type, or
    // both.  foundation/effects/Effect.h says why neither is a row atom.
    F103,
    F104,
    F105,
    E044,
    S010,
    I004,
    I003,
    I002,
    // A code that this file adds.  It reads the session atom of
    // fixy/atoms/Session.h and the payload of a binding that is a
    // session handle.
    R004,
    // A code that this file adds.  It reads the same live handle as
    // R004, against the regime and a stated wait strategy.
    W003,
    // Two codes that hold a fine grade of the payload to the row: an
    // AllocClass band against Alloc, and a Wait band against Block.
    A001,
    W004,
};

// The count of the codes.  A pin in the check file of this header holds it
// to the count of the enumerators.
inline constexpr std::size_t rule_count = 45;

// ---------------------------------------------------------------------
// The premises of the rules.
//
// A premise is one fact about a binding that a rule reads.  fixy/Collision.h
// gives each premise in one pass over the pack and one pass over the
// payload, and each rule below is a predicate over this record.  A field
// is false when the binding does not state the fact.  The pass that sets a
// field tells how it reads the grade.
struct rule_premises {
    // The Usage grade.
    bool borrow{};
    bool ghost{};
    bool capability{};
    bool linear{};
    // A suspension, a hand-off to another thread, or either.
    bool coroutine{};
    bool suspends_in_control_flow{};
    bool suspends{};
    bool concurrent{};
    bool monotonic{};
    bool unverified{};
    // The cost and the space of the binding.
    bool cost_unstated{};
    bool unbounded_cost{};
    bool may_run_unbounded{};
    // The row of the binding: the Effect grade joined with the lifts.
    bool row_bg{};
    bool row_io{};
    bool row_observable{};
    bool row_alloc_or_io{};
    bool row_init{};
    bool blocks{};
    bool allocates{};
    bool observes_something{};
    bool observes_within_the_row{};
    // The representation, the global state, the exits and the calls.
    bool atomic_repr{};
    bool thread_local_state{};
    bool longjmp_unsafe{};
    bool recurses_unbounded{};
    bool publishes_past_the_thread{};
    bool indirect_call_may_throw{};
    bool spawn_outlives_the_frame{};
    bool emits_outside_the_row{};
    // The regime and the waits.
    bool hot{};
    bool no_witness_floor{};
    bool states_stdio{};
    bool core_burning_spin{};
    bool states_a_wait{};
    // The instruction tier, the fence, the scope and the ISA.
    bool hw_nondeterministic{};
    bool hw_privileged{};
    bool barrier_seq_cst_or_above{};
    bool barrier_acq_rel_or_above{};
    bool scope_cluster_or_above{};
    bool scope_pinned{};
    bool isa_pinned{};
    bool trunks_cohere{};
    // The FP mode.
    bool fp_reassociates{};
    bool fp_contracts_across_statements{};
    bool fp_mode_stated{};
    bool fp_flushes_denormal_inputs{};
    bool fp_flushes_subnormal_results{};
    bool fp_rewrites_without_bound{};
    // The Security grade, the staleness and the session.  The corpus of
    // fixy/Corpus.h reads the Internal tier, the classification on Bg and on
    // Staleness, and a stale_to grade.
    bool constant_time{};
    bool internal{};
    bool classified_on_send{};
    bool classified_on_bg{};
    bool classified_on_staleness{};
    bool classified_on_failure{};
    bool states_staleness{};
    bool stale{};
    bool session_protocol{};
    bool has_live_handle{};
    // The failure path, from the payload or from a throw.
    bool fails{};
    bool fails_with_plain_error{};
    // The claims of the bands of the payload.
    bool replay_deterministic{};
    bool claims_no_allocation{};
    bool claims_user_space_wait{};
};

// ---------------------------------------------------------------------
// The rule table.
//
// One row for each code: whether the rule refuses a binding with these
// premises, the code, and the theorem of the rule with its remedy.  The
// refusal of fixy::fn prints the theorem of each rule that the pack trips,
// so each theorem is written one time.  A rule is a theorem about two or
// more axes, and the reason that it refuses a pair is the point of it.
//
// The function is not a template, so no other file can give a rule another
// predicate.  The switch names each enumerator, and the pragma makes a
// missing enumerator an error of -Wswitch-enum.  Each case builds its row
// with a designated initializer, because a call with a constant argument
// in a consteval function that is not a template runs in each unit that
// includes this header.
//
// The code and the theorem are string literals, held by their first
// character, and a zero ends each one.  A TextView takes the length of the
// literal in its type, so each theorem of a distinct length would
// instantiate one more constructor in each unit that includes this header.
struct rule_row {
    bool trips = false;
    char const* code = "";
    char const* theorem = "";
};

#pragma GCC diagnostic push
#pragma GCC diagnostic error "-Wswitch-enum"
[[nodiscard]] consteval rule_row rule_row_of(RuleCode code, const rule_premises& premise) noexcept {
    switch (code) {
        case RuleCode::L002:
            return rule_row{.trips = premise.borrow && premise.concurrent,
                            .code = "L002",
                            .theorem = "L002: borrow x async. A borrowed reference's lifetime is tied to the caller's "
                                       "frame and cannot bridge a suspension or a hand-off to another thread. Scope "
                                       "the borrow before the await, or capture by value."};
        // The atomic representation is what rescues the counter, and that
        // is the whole content of the rule.
        case RuleCode::M012:
            return rule_row{.trips = premise.monotonic && premise.concurrent && !premise.atomic_repr,
                            .code = "M012",
                            .theorem = "M012: monotonic x concurrent without an atomic representation. Two threads "
                                       "stepping a monotonic counter through a non-atomic carrier lose updates. Use "
                                       "repr<ReprKind::Atomic>."};
        // The row holds the lifts of the atoms, so the rule refuses a ghost
        // stdio write, which lifts IO and Block.
        case RuleCode::P010:
            return rule_row{.trips = premise.ghost && premise.row_observable,
                            .code = "P010",
                            .theorem = "P010: ghost x Row<Alloc|IO|Block>. A ghost binding is erased at codegen and "
                                       "emits no instructions, but each of those three effects requires emitted "
                                       "code. The row holds the lifts of the atoms, so a kernel wait or a system "
                                       "call counts too. Drop the ghost grade, or drop the observable effect."};
        case RuleCode::L007:
            return rule_row{.trips = premise.borrow && premise.row_bg,
                            .code = "L007",
                            .theorem = "L007: borrow x Row<Bg>. When the background thread runs the body, the "
                                       "caller's frame may have unwound and the borrow dangles. Move ownership into "
                                       "the closure, or drop the Bg atom."};
        case RuleCode::T001:
            return rule_row{.trips = premise.capability && premise.unverified,
                            .code = "T001",
                            .theorem = "T001: capability x trust::unverified. A capability mints a non-revocable "
                                       "authorization token that consumers treat as proof of authority, which a "
                                       "binding of unverified provenance cannot establish."};
        case RuleCode::R002:
            return rule_row{.trips = premise.coroutine && premise.borrow,
                            .code = "R002",
                            .theorem = "R002: coroutine x borrow. A coroutine resumes after the caller's frame may "
                                       "have unwound, dangling the borrow."};
        case RuleCode::R003:
            return rule_row{.trips = premise.coroutine && premise.row_bg,
                            .code = "R003",
                            .theorem = "R003: coroutine x Row<Bg>. The suspension and the background hand-off are two "
                                       "independent reasons the body outlives its caller; naming both says neither "
                                       "owns the lifetime."};
        case RuleCode::L006:
            return rule_row{.trips = premise.linear && premise.longjmp_unsafe,
                            .code = "L006",
                            .theorem = "L006: linear x longjmp. A longjmp past a linear value's scope skips its "
                                       "consumption, so the resource leaks with no diagnostic."};
        case RuleCode::G002:
            return rule_row{.trips =
                                premise.thread_local_state && premise.atomic_repr && !premise.publishes_past_the_thread,
                            .code = "G002",
                            .theorem = "G002: thread-local x atomic representation x no reader on another thread. A "
                                       "thread-local slot whose stores reach no other thread is private to one "
                                       "thread, so an atomic carrier inside it pays for synchronization nobody can "
                                       "observe. Drop the atomic representation, or state the scope that the stores "
                                       "reach and the fence that publishes them, such as scope::system with "
                                       "barrier::acq_rel for a per-thread counter that a monitor thread reads."};
        case RuleCode::D002:
            return rule_row{.trips = premise.recurses_unbounded && (premise.unbounded_cost || premise.cost_unstated),
                            .code = "D002",
                            .theorem = "D002: unbounded recursion x an unbounded or unstated cost. A recursion with "
                                       "neither a depth bound nor a cost bound is a stack overflow the type system "
                                       "could have refused. State the proven depth with dispatch::recurses<N>, or "
                                       "bound the cost of the binding."};
        // A call through the vDSO lifts the empty row, and a deliberate leak
        // lifts the empty row too, but each atom still states an emitting
        // surface.  A ghost binding that engages either axis is the
        // contradiction of P010 through a different door.
        case RuleCode::P002:
            return rule_row{.trips = premise.ghost && premise.emits_outside_the_row,
                            .code = "P002",
                            .theorem = "P002: ghost x an emitting surface. A ghost binding is erased at codegen, and "
                                       "a stdio write or a syscall is emitted code by definition. P010 catches the "
                                       "atoms that lift an effect through the row of the binding. A call through the "
                                       "vDSO lifts the empty row, and these two axes are the door that catches it."};
        // The regime family.  Regime's discharge is Measurement: which tier
        // a body achieves is the answer of the bench.  These six rules are
        // the part that needs no bench, because each is a contradiction
        // between the declared tier and something else that the binding
        // declares.  H001 refuses an unstated cost and an explicit
        // unbounded one: the hot path has to say what its compute envelope
        // is.
        case RuleCode::H001:
            return rule_row{.trips = premise.hot && (premise.cost_unstated || premise.unbounded_cost),
                            .code = "H001",
                            .theorem = "H001: hot x an unstated or unbounded cost. The hot path must justify its "
                                       "compute envelope, so declare atom::cost_constant or atom::cost_linear<N>. An "
                                       "unstated cost is the Complexity strict pole, which states no cost, and a hot "
                                       "binding must state one."};
        // A hot body assumes an invariant that it does not check, and that
        // is what buys the nanoseconds.  So it must carry a witness that
        // something else proved it.
        case RuleCode::H002:
            return rule_row{.trips = premise.hot && premise.no_witness_floor,
                            .code = "H002",
                            .theorem = "H002: hot x no refinement witness. A hot body buys its nanoseconds by "
                                       "assuming an invariant instead of checking it, so something upstream must "
                                       "have proved it. Attach a Refined input that carries the proof."};
        // H003 reads Alloc and IO in the row of the binding, and not the
        // cost.  The allocator and the kernel bound neither in nanoseconds,
        // and CLAUDE.md bans an allocation and a system call on the hot
        // path, with a bounded cost or not.  A system call that lifts IO
        // trips the rule although it cannot block, and getpid is one.  A
        // read of the clock through the vDSO lifts the empty row, so the
        // rule admits it.  Block is the premise of W001.
        case RuleCode::H003:
            return rule_row{.trips = premise.hot && premise.row_alloc_or_io,
                            .code = "H003",
                            .theorem = "H003: hot x Alloc or IO in the row of the binding. The allocator and the "
                                       "kernel bound neither in nanoseconds, whatever cost the binding states, and a "
                                       "system call that lifts IO counts as a stated IO does. Move the allocation or "
                                       "the I/O off the hot path, into an Init or a Bg context."};
        // A hot Bg binding with cost::Constant and no Alloc or IO row passes
        // H001 and H003, and it is still a contradiction of contexts,
        // because the two name different threads.
        case RuleCode::H010:
            return rule_row{.trips = premise.hot && premise.row_bg,
                            .code = "H010",
                            .theorem = "H010: hot x Row<Bg>. A function cannot be both on the foreground hot path "
                                       "and in background context: the two name different threads. H001 and H003 "
                                       "both admit a hot Bg binding with a constant cost and no Alloc or IO, which is "
                                       "still this contradiction."};
        case RuleCode::R001:
            return rule_row{.trips = premise.coroutine && premise.hot,
                            .code = "R001",
                            .theorem = "R001: coroutine x hot. A coroutine frame costs an indirect call plus a state "
                                       "spill at every suspension point, and one resume breaches the budget."};
        // A stdio write lifts IO and Block, so H003 and W001 refuse the same
        // pack.  S001 keeps its own theorem and its own code: the write
        // takes the lock of the stream before it reaches the kernel.
        case RuleCode::S001:
            return rule_row{.trips = premise.states_stdio && premise.hot,
                            .code = "S001",
                            .theorem = "S001: stdio x hot. Buffered stdio takes a lock and may block. Neither belongs "
                                       "on a path budgeted in nanoseconds."};
        // The wait family.  A kernel wait is too slow for the hot path, and
        // a spin that burns the core is too expensive for a background one.
        // W001 reads the row of the binding: a kernel wait lifts to
        // Row<Block>, a system call that can park the caller lifts to a row
        // with Block, and with<Block> states Block on the Effect axis.  The
        // scheduler bounds each of them, and the three are one theorem.  A
        // new atom that can block puts Block in its lift, and W001 then
        // refuses it with no edit here.
        case RuleCode::W001:
            return rule_row{.trips = premise.hot && premise.blocks,
                            .code = "W001",
                            .theorem = "W001: hot x a kernel wait. Block is in the row that the pack lifts to, from a "
                                       "wait strategy that enters the kernel, a system call that can park the caller, "
                                       "or a stated Block. The scheduler bounds such a wait at 1-5 us, against a "
                                       "budget that the cache-coherence fabric bounds at 10-40 ns. Wait with a spin, "
                                       "move the call off the hot path, or leave the hot path."};
        // W002 reads burns_the_core of fixy/atoms/Sync.h, which is narrower
        // than "not a kernel wait" by one grade: UMWAIT halts the core in
        // C0.1 instead of spinning it, so a background body may use it.
        case RuleCode::W002:
            return rule_row{.trips = premise.row_bg && premise.core_burning_spin,
                            .code = "W002",
                            .theorem = "W002: Row<Bg> x a spin that burns the core. A background body that spins "
                                       "holds a core the scheduler could have given to foreground work. Park, or use "
                                       "UMWAIT, which halts the core instead of spinning it."};
        // "May run unbounded" reads four ways, and B001 refuses all four: an
        // unstated cost, an explicit unbounded cost, an unstated space, or
        // an explicit unbounded space.  The remedy that the theorem names is
        // the pair space_bounded plus cost_linear.
        case RuleCode::B001:
            return rule_row{.trips = premise.row_bg && premise.observes_something && premise.may_run_unbounded,
                            .code = "B001",
                            .theorem = "B001: a Bg observable surface that may run unbounded is a back-pressure trap. "
                                       "The producer cannot see the consumer fall behind, because the surface exists "
                                       "to report facts and not to apply back pressure. Declare "
                                       "atom::space_bounded<N> and atom::cost_linear<N>."};
        // B002 is the containment: the effects that a binding declares
        // observable must be effects that it declared at all.  The row of
        // the binding stays the one authority for what the binding may do,
        // and a passive surface that widens it is not passive.
        case RuleCode::B002:
            return rule_row{.trips = !premise.observes_within_the_row,
                            .code = "B002",
                            .theorem = "B002: an observability surface names an effect outside the binding's effect "
                                       "row. Observability names which PART of the declared row is observation; it "
                                       "is not a second row and cannot widen the first. Add the effect to the Effect "
                                       "grade if the operation really performs it, or drop it from the surface."};
        // The hardware-instruction family.  The ladder is a chain where each
        // tier admits each class below it, so "at or above
        // NonDeterministicTsc" covers PrivilegedMsr too.
        case RuleCode::V201:
            return rule_row{.trips = premise.hot && premise.hw_nondeterministic,
                            .code = "V201",
                            .theorem = "V201: hot x an instruction tier at or above NonDeterministicTsc. A serialising "
                                       "timestamp read drains the pipeline, 20-40 cycles against a budget of tens of "
                                       "nanoseconds, and PrivilegedMsr sits above it on the ladder. Read the counter "
                                       "off the hot path, or drop the tier."};
        case RuleCode::V202:
            return rule_row{.trips = premise.hw_privileged && !premise.row_init,
                            .code = "V202",
                            .theorem = "V202: the PrivilegedMsr tier x an effect row with no Init. rdmsr, wrmsr, IN "
                                       "and OUT belong to startup, where they can be ordered and audited and where the "
                                       "ring-0 capability is held. Add Effect::Init to the row, or drop the tier."};
        // The tier that is too slow for the hot path is also
        // non-deterministic by construction, and a payload that claims
        // replay-determinism cannot survive it.
        case RuleCode::V203:
            return rule_row{.trips = premise.replay_deterministic && premise.hw_nondeterministic,
                            .code = "V203",
                            .theorem = "V203: a replay-deterministic payload x an instruction tier at or above "
                                       "NonDeterministicTsc. The payload's DetSafe band claims the same bits on every "
                                       "replay; a timestamp counter differs per run by construction. Lower the band, "
                                       "or drop the tier."};
        // A seq_cst fence or the standalone full fence drains the store
        // buffer.  A release store and an acquire load are one MOV each on
        // x86 and are what the SPSC ring is made of, so the floor sits above
        // them and above acq_rel.
        case RuleCode::V301:
            return rule_row{.trips = premise.hot && premise.barrier_seq_cst_or_above,
                            .code = "V301",
                            .theorem = "V301: hot x a fence at or above SeqCst. A seq_cst fence drains the store "
                                       "buffer, about 30 ns on x86, against a budget bounded by the cache-coherence "
                                       "fabric at 10-40 ns. Publish with a release store and read with an acquire "
                                       "load, or leave the hot path."};
        // A scope is how far a publication reaches, and a strength is what
        // the fence orders.  A binding that names a wide scope and no
        // strength provides no fence at all, so the rule fires on it too.
        // The host trunk is incomparable with Cluster, and the rule stands
        // down for it.
        case RuleCode::V401:
            return rule_row{.trips = premise.scope_cluster_or_above && !premise.barrier_acq_rel_or_above,
                            .code = "V401",
                            .theorem = "V401: a scope at or above Cluster x a fence below AcqRel. The publication "
                                       "reaches further than the fence orders, so a reader on another block can "
                                       "observe the write before the data the writer published ahead of it. Fence "
                                       "with acq_rel or stronger, or narrow the scope."};
        // Scalar pins nothing, and Portable is one kernel for each set, so
        // both stand down.
        case RuleCode::V101:
            return rule_row{.trips = premise.replay_deterministic && premise.isa_pinned,
                            .code = "V101",
                            .theorem = "V101: a replay-deterministic payload x an ISA pinned to one trunk. A body "
                                       "emitted for one vendor's vector width reduces in that width's order, so its "
                                       "bits differ per host; the payload's DetSafe band claims the same bits on "
                                       "every replay. Emit Scalar or Portable, or lower the band."};
        // The host shareability trunk, Inner and Outer, is the trunk of
        // ARM, so it coheres with an ARM ISA and with nothing else.  The
        // accelerator trunk is GPU scope and coheres with no host ISA.  The
        // shared points on either axis cohere with each other point.
        case RuleCode::V402:
            return rule_row{.trips = premise.scope_pinned && premise.isa_pinned && !premise.trunks_cohere,
                            .code = "V402",
                            .theorem = "V402: a trunk-pinned scope x a trunk-pinned ISA that do not cohere. The host "
                                       "shareability scopes are the ARM DMB ISH/OSH family and the accelerator scopes "
                                       "are GPU scope; an x86 ISA has neither and an ARM ISA has only the first. Pin "
                                       "the scope and the ISA on one trunk, or leave one at its shared point."};
        // The floating-point-mode family.  A sum rewritten into a different
        // tree rounds differently.  Contraction within one expression is
        // not refused: -ffp-contract=on permits it, the fusion is visible in
        // the source expression, and so it is the same on each build.
        case RuleCode::F101:
            return rule_row{.trips = premise.replay_deterministic && premise.fp_reassociates,
                            .code = "F101",
                            .theorem = "F101: a replay-deterministic payload x FP reassociation permitted. "
                                       "Reassociation rewrites the sum into a different tree, and a different tree "
                                       "rounds differently, so the bits differ between builds; the bounded-depth "
                                       "rung is refused with the unrestricted one for the same reason. Forbid "
                                       "reassociation, or lower the band."};
        case RuleCode::F102:
            return rule_row{.trips = premise.replay_deterministic && premise.fp_contracts_across_statements,
                            .code = "F102",
                            .theorem = "F102: a replay-deterministic payload x FP contraction across statements. "
                                       "Whether a multiply and an add fuse into one FMA decides where the single "
                                       "rounding falls, and 'fast' leaves that to the build. Contract within an "
                                       "expression instead, which is visible in the source and stable, or lower the "
                                       "band."};
        // A capability is an authorization token minted for one run.  T001
        // also reads capability_usage, and a capability with no trust grade
        // is unverified, so a fixture that isolates S011 states a trust.
        case RuleCode::S011:
            return rule_row{.trips = premise.capability && premise.replay_deterministic,
                            .code = "S011",
                            .theorem = "S011: capability x a replay-deterministic payload. A capability is an "
                                       "authorization token minted for one run, and replay cannot mint the same token "
                                       "again, so the payload's claim of the same bits on every replay cannot hold. "
                                       "Carry a content-addressed handle instead of the capability, or lower the "
                                       "band."};
        // A family named by an opaque tag class names no signature, and an
        // unknown callee is potentially-throwing, so the rule refuses it.
        case RuleCode::D001:
            return rule_row{.trips = premise.indirect_call_may_throw,
                            .code = "D001",
                            .theorem = "D001: an indirect call whose family names no noexcept signature. A throw out "
                                       "of the callee crosses a boundary that promised none and ends the process, and "
                                       "a callee whose signature nobody names is potentially-throwing. Name the "
                                       "signature with noexcept, as a function pointer type or as a member type "
                                       "`signature` of the tag class, or put a noexcept trampoline in front of the "
                                       "call that turns the failure into a Result."};
        // L003 is the lifetime hazard of L002 through a different door.  A
        // detached or raw-cloned child is neither a suspension nor a Bg row,
        // and it can still run after the frame of the borrow has unwound.
        case RuleCode::L003:
            return rule_row{.trips = premise.borrow && premise.spawn_outlives_the_frame,
                            .code = "L003",
                            .theorem = "L003: borrow x a spawn no structured join ties to the frame. A detached or "
                                       "raw-cloned child shares the address space and can run after the caller's "
                                       "frame has unwound, so the borrow dangles. Spawn through mint_spawn, which "
                                       "joins, or move ownership into the child."};
        // The constant-time family reads the FP mode against the timing
        // claim.  F103 refuses only the unrestricted rewrite: a tree of
        // bounded depth has a topology that the data cannot choose.  F104
        // and F105 refuse a mode that does not name the flush, because the
        // strict value of each setting is the slow one.  A body that names
        // no FP mode claims nothing about floating point.
        case RuleCode::F103:
            return rule_row{.trips = premise.constant_time && premise.fp_rewrites_without_bound,
                            .code = "F103",
                            .theorem = "F103: constant time x FP reassociation by an unrestricted rewrite. The "
                                       "compiler can pick the tree of the sum from the magnitudes of the operands, so "
                                       "the count of operations follows the data. Forbid reassociation, or bound it "
                                       "to a tree of fixed depth."};
        case RuleCode::F104:
            return rule_row{.trips =
                                premise.constant_time && premise.fp_mode_stated && !premise.fp_flushes_denormal_inputs,
                            .code = "F104",
                            .theorem = "F104: constant time x an FP mode that honours denormal inputs. A denormal "
                                       "input costs 30 to 100 times the cycles of a normal one on x86 and ARM, so the "
                                       "time tells the magnitude of the input. Name "
                                       "FpDenormalInput::DenormalsAreZero in the mode (MXCSR.DAZ, FPCR.FZ)."};
        case RuleCode::F105:
            return rule_row{.trips = premise.constant_time && premise.fp_mode_stated
                                  && !premise.fp_flushes_subnormal_results,
                            .code = "F105",
                            .theorem = "F105: constant time x an FP mode that preserves subnormal results. A "
                                       "subnormal result costs 30 to 100 times the cycles of a normal one, so the "
                                       "time tells the magnitude of the result. Name FpFtz::FlushToZero in the mode."};
        // E044 and I004 read the one concurrency premise, so a classified
        // async session is refused both ways: E044 when it claims constant
        // time, I004 when it does not.
        case RuleCode::E044:
            return rule_row{.trips = premise.constant_time && premise.concurrent,
                            .code = "E044",
                            .theorem = "E044: constant time x a suspension or a Bg row. The scheduler then decides "
                                       "when the body runs, and it sees the classified data through the timing. Keep "
                                       "the constant-time core synchronous, and put only its boundary in async "
                                       "code."};
        // A constant-time binding is classified, so each trip of S010 is
        // also a trip of the corpus entry staleness_secret_without_declassify.
        // The rule is kept for its own theorem and its own remedy.
        case RuleCode::S010:
            return rule_row{.trips = premise.constant_time && premise.states_staleness,
                            .code = "S010",
                            .theorem = "S010: constant time x a staleness window. A stale value makes the body check "
                                       "freshness at run time, and that check is a branch whose time follows the "
                                       "data. Keep Staleness at Fresh, or drop constant_time."};
        case RuleCode::I004:
            return rule_row{.trips = premise.classified_on_send && premise.concurrent && premise.session_protocol
                                  && !premise.constant_time,
                            .code = "I004",
                            .theorem = "I004: classified x a suspension or a Bg row x a session protocol, without "
                                       "constant time. The peer sees each send, and the time of each send follows the "
                                       "classified data. Send from a synchronous binding that states "
                                       "atom::constant_time, or declassify before the send under a policy whose mask "
                                       "licenses IO: AuditedLogging, WireSerialize or UserDisplay."};
        // A constant-time body works on classified data by definition, and
        // the caller of a body that can fail branches on the discriminant.
        // fixy::ct::eq traps on a length mismatch for this reason.
        case RuleCode::I003:
            return rule_row{.trips = premise.constant_time && premise.fails,
                            .code = "I003",
                            .theorem = "I003: constant time x a failure path. The caller branches on success or "
                                       "failure, so the discriminant leaks the condition that chose it, and that "
                                       "condition reads classified data. Select the result with fixy::ct::select and "
                                       "fail after a declassification, or trap as fixy::ct::eq does."};
        case RuleCode::I002:
            return rule_row{.trips = premise.classified_on_failure && premise.fails_with_plain_error,
                            .code = "I002",
                            .theorem = "I002: classified x a failure whose error type is not fixy::Secret. The error "
                                       "value leaves the classified binding and carries what the body knew when it "
                                       "failed. No declassification policy licenses the failure channel. Wrap the "
                                       "error type in fixy::Secret, or state atom::as_public."};
        // The suspension turns the frame into a continuation, and the
        // handle is then part of the continuation (Tang, Hillerstrom,
        // Lindley and Morris, POPL 2024).  A linear frame is resumed exactly
        // one time, so the rule admits it.
        case RuleCode::R004:
            return rule_row{.trips = premise.has_live_handle && premise.suspends && !premise.linear,
                            .code = "R004",
                            .theorem = "R004: a suspension x a live session handle x a frame that is not linear. The "
                                       "suspension makes the frame a continuation that holds the handle. A "
                                       "continuation that is dropped drops the protocol, and one that is resumed "
                                       "twice does one step two times. Keep the Usage grade linear, or close the "
                                       "session before the suspension."};
        // W001 refuses a hot binding that states a kernel wait, and it has
        // nothing to read on a binding that states no wait at all.  W003
        // refuses that silence.  The rule reads a stated strategy, not the
        // axis, so a type on the axis that names no strategy does not
        // satisfy it.
        case RuleCode::W003:
            return rule_row{.trips = premise.hot && premise.has_live_handle && !premise.states_a_wait,
                            .code = "W003",
                            .theorem = "W003: hot x a live session handle x no stated wait. The next step of the "
                                       "handle goes through a transport, and a receive or a full send waits there. An "
                                       "unstated wait can be a futex at 1-5 us against a budget of 10-40 ns. State "
                                       "the wait strategy with a fixy::atom::sync atom, a spin such as "
                                       "sync::spin_pause, or close the session before the hot path."};
        // A001 and W004 hold the fine grades of the payload to the row.  The
        // row is the one source of the coarse fact, so a band that claims
        // more than the row is false.
        case RuleCode::A001:
            return rule_row{.trips = premise.claims_no_allocation && premise.allocates,
                            .code = "A001",
                            .theorem = "A001: a no-allocation grade x Alloc in the row. A Pool or Stack grade of "
                                       "AllocClass claims that no allocator ran, and the row of the binding "
                                       "allocates. The row is the one source of the fact. Give the band the grade of "
                                       "the allocator, or remove the allocation."};
        case RuleCode::W004:
            return rule_row{.trips = premise.claims_user_space_wait && premise.blocks,
                            .code = "W004",
                            .theorem = "W004: a user-space wait grade x Block in the row. A UmwaitC01, BoundedSpin or "
                                       "SpinPause grade of Wait claims that the wait stays out of the kernel, and the "
                                       "row of the binding blocks. The row is the one source of the fact. Give the "
                                       "band a kernel wait grade, or remove the kernel wait."};
        default:
            return rule_row{};
    }
}
#pragma GCC diagnostic pop

// ---------------------------------------------------------------------
// The pending roster.  Each entry keeps the theorem and the citation,
// and names the axis that would make it live.

struct pending_rule {
    RuleCode code{};
    Axis waits_on{};
    std::string_view theorem{};
};

// Empty, and empty for two different reasons.
//
// Every rule that reads an axis family is live, because every axis has
// its atoms: H001, H002, H003, H010, R001 and S001 read
// fixy/atoms/Regime.h, W002 reads Sync.h, W001 reads the row that the
// atoms of Sync.h, Syscall.h and Os.h lift to, B001 reads Observe.h,
// V201, V202 and V203 read Hw.h, V301 reads Barrier.h, V401 reads
// Scope.h, V101 and V402 read Simd.h, and F101 and F102 read Fp.h.  Each
// is a row of the rule table above and a Live row in rule_corpus.  Three of
// them read more than one family: W001 a tier AND a lifted row, V401 a
// scope AND a strength, V402 a scope AND a pinned ISA.
//
// Four sit in rule_corpus instead, because the FpMode or SimdIsa atom is
// not all they read.  F103, F104 and F105, which read an FP mode against
// a constant-time claim, read that claim from the Security grade
// atom::constant_time.  V102, which reads a SIMD width, is Retired,
// because the one ISA grade a binding names already fixes its width.
// That is the distinction this list exists to draw: pending means
// waiting on an atom, and absent means waiting on something this layer
// does not have.
inline constexpr std::array<pending_rule, 0> pending_rules{};

inline constexpr std::size_t pending_rule_count = pending_rules.size();

// ---------------------------------------------------------------------
// The corpus: every rule code, and the disposition of each one here.
//
// This list is the external specification, written out by name.  That is
// the whole point of it.  A rule that nobody writes is absent from
// RuleCode, so any quantity computed FROM RuleCode — including a count —
// agrees with itself for a roster of the wrong size and cannot see the
// absence.  A pin such as `live == roster - pending` is arithmetic rather
// than a measurement.
//
// The pins in the check file of this header compare this list against the
// RuleCode enum, and the enum against the rows of the rule table, so no
// one of the three can drift without one of the others reporting it.
//
// The specification names 54 codes, and this file adds five: B002, R004,
// W003, A001 and W004.

enum class Disposition : std::uint8_t {
    Live,  // a row of the rule table, and able to fire today
    Pending,  // kept as a theorem, waiting on an axis that has no atom
    Absent,  // not implemented; the note names what is missing
    Retired,  // needs no rule, and the note says why
};

struct corpus_entry {
    std::string_view code{};
    Disposition disposition{};
    std::string_view note{};
};

inline constexpr corpus_entry rule_corpus[] = {
    // The eleven that fire today.
    {"L002", Disposition::Live, "borrow x async"},
    {"M012", Disposition::Live, "monotonic x concurrent without an atomic representation"},
    {"P010", Disposition::Live, "ghost x an observable effect in the row of the binding"},
    {"P002", Disposition::Live, "ghost x stdio or a syscall surface"},
    {"L007", Disposition::Live, "borrow x Row<Bg>"},
    {"T001", Disposition::Live, "capability x unverified trust"},
    {"R002", Disposition::Live, "coroutine x borrow"},
    {"R003", Disposition::Live, "coroutine x Row<Bg>"},
    {"L006", Disposition::Live, "linear x longjmp"},
    {"G002", Disposition::Live, "thread-local x atomic representation x no reader on another thread"},
    {"D002", Disposition::Live, "unbounded recursion x an unbounded or unstated cost"},

    // The regime family, which reads the three HotPathTier atoms of
    // fixy/atoms/Regime.h.
    {"H001", Disposition::Live, "hot x unstated or unbounded cost"},
    {"H002", Disposition::Live, "hot x no refinement witness"},
    {"H003", Disposition::Live, "hot x Alloc or IO in the row of the binding"},
    {"H010", Disposition::Live, "hot x Row<Bg>"},
    {"R001", Disposition::Live, "coroutine x hot"},
    {"S001", Disposition::Live, "stdio x hot"},

    // The wait family, which reads the six WaitStrategy atoms of
    // fixy/atoms/Sync.h.  W001 reads a tier and the row that the pack
    // lifts to, so it reads the Regime family too, and every family whose
    // atoms lift to a row: a kernel wait, a system call that can park the
    // caller, and a stated Block each put Block in that row.
    {"W001", Disposition::Live, "hot x a kernel wait: Block in the row that the pack lifts to"},
    {"W002", Disposition::Live, "Row<Bg> x a spin that burns the core"},

    // The observability family, which reads the surface atom of
    // fixy/atoms/Observe.h.  Two theorems, not one: B002 is the
    // containment of the observability row in the effect row, and B001 is
    // the back-pressure trap.  B002 is a code of its own rather than a
    // rereading of B001, because the codes are stable API and the negative
    // corpus greps them.
    {"B001", Disposition::Live, "Row<Bg> x an observable surface x an unbounded resource"},
    {"B002", Disposition::Live, "an observability row outside the binding's effect row"},

    // The hardware-instruction family, which reads the five
    // HwInstruction atoms of fixy/atoms/Hw.h.  V203 reads the payload's
    // replay claim against them.
    {"V201", Disposition::Live, "hot x a tier at or above NonDeterministicTsc"},
    {"V202", Disposition::Live, "the PrivilegedMsr tier x an effect row with no Init"},
    {"V203", Disposition::Live, "a replay-deterministic payload x a tier at or above NonDeterministicTsc"},

    // The barrier-strength family, which reads the seven BarrierStrength
    // atoms of fixy/atoms/Barrier.h.
    {"V301", Disposition::Live, "hot x a fence at or above SeqCst"},

    // The memory-scope family, which reads the eight MemoryScope atoms of
    // fixy/atoms/Scope.h.  V401 reads a scope and a strength together.
    {"V401", Disposition::Live, "a scope at or above Cluster x a fence below AcqRel"},

    // The SIMD-ISA family, which reads the fifteen SimdIsa atoms of
    // fixy/atoms/Simd.h.  V101 reads the payload's replay claim against
    // them, and V402 reads a scope and an ISA together.
    {"V101", Disposition::Live, "a replay-deterministic payload x an ISA pinned to one trunk"},
    {"V402", Disposition::Live, "a trunk-pinned scope x a trunk-pinned ISA that do not cohere"},

    // The floating-point-mode family, which reads the product atom of
    // fixy/atoms/Fp.h.  Both read the payload's replay claim, as V203 and
    // V101 do.
    {"F101", Disposition::Live, "a replay-deterministic payload x FP reassociation permitted"},
    {"F102", Disposition::Live, "a replay-deterministic payload x FP contraction across statements"},

    // Three whose premises are grades that a binding states.  S011 reads
    // the payload's replay claim as the family above does.
    {"S011", Disposition::Live, "capability x a replay-deterministic payload"},
    {"D001", Disposition::Live, "an indirect call whose family names no noexcept signature"},
    {"L003", Disposition::Live, "borrow x a spawn that no structured join ties to the frame"},

    // The constant-time family and the failure family.  The specification
    // writes the constant-time claim and the failure path as effects in
    // the row, `with CT` and Fail(E), and neither is a row atom here:
    // foundation/effects/Effect.h says why at its top.  Constant time is
    // the Security grade atom::constant_time, and a failure is the payload
    // Result<T, E>, Option<T> or std::expected<T, E>, or the ControlFlow
    // grade ctrl::throws<E>.
    {"F103", Disposition::Live, "constant time x FP reassociation by an unrestricted rewrite"},
    {"F104", Disposition::Live, "constant time x an FP mode that honours denormal inputs"},
    {"F105", Disposition::Live, "constant time x an FP mode that preserves subnormal results"},
    {"E044", Disposition::Live, "constant time x a suspension or a Bg row"},
    {"S010", Disposition::Live, "constant time x a staleness window"},
    {"I004", Disposition::Live, "classified x a suspension or a Bg row x a session protocol, without constant time"},
    {"I003", Disposition::Live, "constant time x a failure path"},
    {"I002", Disposition::Live, "classified x a failure whose error type is not Secret"},

    // The session family, whose two codes this file adds.  R004 reads the
    // live-handle atom of fixy/atoms/Session.h, or a payload that is a
    // session handle, against a suspension.
    {"R004", Disposition::Live, "a suspension x a live session handle x a frame that is not linear"},
    {"W003", Disposition::Live, "hot x a live session handle x no stated wait"},
    {"A001", Disposition::Live, "a no-allocation grade of AllocClass x Alloc in the row"},
    {"W004", Disposition::Live, "a user-space wait grade of Wait x Block in the row"},

    // Nothing waits on an atom.  pending_rules above is empty and says why
    // in two parts, and the rest of this list is the second part.

    // The rules this layer cannot state yet.  Each note names the thing
    // that is missing, so the entry is a claim someone can check rather
    // than a gap someone has to notice.  absent_rule_codes below names the
    // same set, and the set only shrinks.
    //
    // Three read a premise that no binding in the tree can state.
    {"F002", Disposition::Absent,
     "reads a federation peer against cost_unbounded. No atom, band or role names a federation peer"},
    {"N002", Disposition::Absent,
     "reads an exact-decimal payload against overflow_wrap. The tree has no decimal type, so the premise has no "
     "type to read and no witness to arm"},
    {"L004", Disposition::Absent,
     "reads a linear in_region binding that holds no Permission for its region. The Permission is an argument "
     "at the call, and a binding sees no call. in_region names its region by a value and Permission by a type"},

    // Three hold across several bindings.  The verdict reads one binding,
    // so a relation over a set of bindings has no place here.
    {"F001", Disposition::Absent,
     "every binding in one frame names the same Security grade. A Stage names a function pointer and a "
     "context, and no gate in the tree takes a set of fixy::fn bindings"},
    {"L005", Disposition::Absent,
     "no two linear bindings in one frame name the same in_region tag. The rule needs the set of bindings "
     "that F001 needs, and no gate takes one"},
    {"S004", Disposition::Absent,
     "walks the init-dependency graph of every singleton for a cycle. global::singleton<Tag> names no edge. "
     "crucible/SingletonInitGraph.h states the edges and refuses a cycle at its own gate, and no gate takes the "
     "set of bindings that would read the graph"},

    // Retired: the code needs no rule, and the note says why.  Each of
    // these is discharged by the shape of the tree: the combination it
    // names cannot be written.  A retired rule is not a rule deferred, so
    // it is outside the Absent set rather than waiting in it.
    {"C001", Disposition::Retired,
     "discharged: ctrl::abort<Reason> IS the binding's ControlFlow grade, so the abort declaration and the "
     "ControlFlow tier are one claim and cannot disagree"},
    {"P003", Disposition::Retired,
     "discharged: a fork worker is a callable handed to mint_spawn, which refuses one whose type carries "
     "ctrl::throws (fixy/os/Spawn.h, design note 1), after foundation's fork gate has required it be noexcept"},
    {"V001", Disposition::Retired,
     "discharged: Axis::SimdIsa takes one grade per binding and tier 4 refuses a second, so a pack cannot name "
     "two vendors' instruction sets"},
    {"V002", Disposition::Retired,
     "discharged: Axis::SimdIsa takes one grade per binding and tier 4 refuses a second, so a pack cannot name "
     "an x86 set and an ARM set together"},
    {"V102", Disposition::Retired,
     "discharged: the width a body emits is the width of the one SimdIsa grade it names; no atom or band states "
     "a width apart from it, so a width wider than the set cannot be written"},
    {"G001", Disposition::Retired,
     "discharged: atom::global::thread_local_<StaticTag> requires the tag, so the untagged form this rule refused "
     "cannot be named"},
    {"M001", Disposition::Retired,
     "discharged: DontNeed discards pages, so fixy/os/Mmap.h refuses it at advise and admits it only at "
     "advise_release_aware, which takes the Permission of the mapping"},
    {"M011", Disposition::Retired,
     "discharged: a failure here is a Result return or a throw, and each runs every destructor in scope, so "
     "a linear value is released on the failure path; the one exit that skips destructors is longjmp, and L006 "
     "refuses it against a linear binding.  Which values are live at the failure point is a property of the body, "
     "and a binding carries no body"},

};

inline constexpr std::size_t rule_corpus_size = sizeof(rule_corpus) / sizeof(rule_corpus[0]);

namespace detail {

[[nodiscard]] consteval bool corpus_lists_(std::string_view code) noexcept {
    for (const corpus_entry& entry : rule_corpus) {
        if (entry.code == code) return true;
    }
    return false;
}

[[nodiscard]] consteval bool corpus_ships_(std::string_view code) noexcept {
    for (const corpus_entry& entry : rule_corpus) {
        if (entry.code == code)
            return entry.disposition == Disposition::Live || entry.disposition == Disposition::Pending;
    }
    return false;
}

[[nodiscard]] consteval std::size_t corpus_count_(Disposition wanted) noexcept {
    std::size_t found = 0;
    for (const corpus_entry& entry : rule_corpus) {
        if (entry.disposition == wanted) ++found;
    }
    return found;
}

// A template, so that only a translation unit that calls it expands the
// walk.  The same holds for each walk over an enum or a class below that
// takes the parameter Anchor.  Each list and each text of such a walk
// depends on Anchor (foundation/reflect/Anchor.h), and each call between
// the walks passes Anchor on.
template <class Anchor = void>
[[nodiscard]] consteval bool rule_code_names_(std::string_view code) noexcept {
    bool found = false;
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wshadow"
    template for (constexpr auto enumerator :
                  ::foundation::reflect::reflection_list<^^Anchor>(std::meta::enumerators_of(^^RuleCode))) {
        if (std::meta::identifier_of(enumerator) == code) found = true;
    }
#pragma GCC diagnostic pop
    return found;
}

}  // namespace detail

inline constexpr std::size_t live_rule_count = detail::corpus_count_(Disposition::Live);

// Every code the corpus says ships has an enumerator, and every code it
// says is absent or retired has none.  A rule written without a corpus
// entry, or recorded as absent after being written, reddens in the check
// file of this header.
//
// Both checks answer with the offending code rather than with a bool, so
// the diagnostic names the rule instead of asking the reader to diff two
// lists of fifty-seven.

namespace detail {

template <class Anchor = void>
[[nodiscard]] consteval std::string_view code_the_enum_disagrees_about_() noexcept {
    for (const corpus_entry& entry : rule_corpus) {
        const bool shipped = entry.disposition == Disposition::Live || entry.disposition == Disposition::Pending;
        if (rule_code_names_<Anchor>(entry.code) != shipped) return entry.code;
    }
    return {};
}

template <class Anchor = void>
[[nodiscard]] consteval std::string_view code_the_corpus_never_listed_() noexcept {
    std::string_view missing{};
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wshadow"
    template for (constexpr auto enumerator :
                  ::foundation::reflect::reflection_list<^^Anchor>(std::meta::enumerators_of(^^RuleCode))) {
        constexpr std::string_view name = std::meta::identifier_of(enumerator);
        if (missing.empty() && !corpus_lists_(name)) missing = name;
    }
#pragma GCC diagnostic pop
    return missing;
}

}  // namespace detail

// ---------------------------------------------------------------------
// The Absent set, by name.
//
// The pins of fixy/collision/Report.h bind the Live rows to the rules
// that run, and they say
// nothing about the Absent rows.  An Absent row asks for nothing, so a
// rule could be moved to Absent, or a new code added as Absent, and every
// pin would stay green.  A count would not close that: one name can leave
// the set while another joins it, and the count holds.
//
// So the set is written out a second time, here, and the two lists must
// name the same codes.  The list only shrinks.  A code leaves it when its
// rule is written and its row turns Live, or when its row turns Retired
// and records why the code needs no rule.  A code never joins it: adding a name here
// is the one edit that grows the set, and it is a visible line in a diff
// rather than a disposition changed in passing.
inline constexpr std::string_view absent_rule_codes[] = {
    "F002", "N002", "L004", "F001", "L005", "S004",
};

namespace detail {

[[nodiscard]] consteval bool absent_list_names_(std::string_view code) noexcept {
    for (const std::string_view listed : absent_rule_codes) {
        if (listed == code) return true;
    }
    return false;
}

[[nodiscard]] consteval bool corpus_marks_absent_(std::string_view code) noexcept {
    for (const corpus_entry& entry : rule_corpus) {
        if (entry.code == code) return entry.disposition == Disposition::Absent;
    }
    return false;
}

// Every offender, comma-separated, so one known offender cannot mask a
// second.  O(n^2) over the corpus, at compile time only.
//
// The two collectors return a std::string rather than a static view, and
// the diagnostic is assembled in one call.  Handing a static view built
// by define_static_string to std::string's constructor is not a constant
// expression under GCC 16: the constructor compares the pointer against
// null, and GCC refuses that comparison for such a pointer.
[[nodiscard]] consteval std::string absent_rows_not_listed_() {
    std::string offenders;
    for (const corpus_entry& entry : rule_corpus) {
        if (entry.disposition != Disposition::Absent || absent_list_names_(entry.code)) continue;
        if (!offenders.empty()) offenders += ", ";
        for (const char letter : entry.code)
            offenders += letter;
    }
    return offenders;
}

[[nodiscard]] consteval std::string listed_codes_not_absent_() {
    std::string offenders;
    for (const std::string_view listed : absent_rule_codes) {
        if (corpus_marks_absent_(listed)) continue;
        if (!offenders.empty()) offenders += ", ";
        for (const char letter : listed)
            offenders += letter;
    }
    return offenders;
}

[[nodiscard]] consteval std::string_view absent_code_listed_twice_() noexcept {
    const std::size_t listed = sizeof(absent_rule_codes) / sizeof(absent_rule_codes[0]);
    for (std::size_t first = 0; first < listed; ++first) {
        for (std::size_t second = first + 1; second < listed; ++second) {
            if (absent_rule_codes[first] == absent_rule_codes[second]) return absent_rule_codes[first];
        }
    }
    return {};
}

}  // namespace detail

}  // namespace collision

}  // namespace fixy
