#pragma once

// Checkpoint and rollback for binary sessions, with a coordinated choice.
//
// The authority is Mezzina, Tiezzi and Yoshida, "Checkpoint-based rollback
// recovery in session programming", LMCS 21(1), 2025.  Their calculus has
// three primitives: commit takes a checkpoint, roll returns both parties
// to their last checkpoints, and abort returns both parties to the start
// of the session.  A pair of session types is compliant (Def. 4.1, p. 12)
// when every configuration that the semantics of Fig. 11 (p. 13) reaches,
// and that cannot move, has both parties at end.  Compliance is decidable
// (Thm. 4.2).
//
// ── Why the choice is coordinated here ──────────────────────────────
//
// In the calculus, commit, roll and abort are actions of one party, and
// the runtime moves the other party.  A C++ handle cannot be moved from
// outside: the peer holds a handle at a type, and only its own next step
// can change that type.  So each primitive is a label that one party
// selects and the other party receives:
//
//   Select<..., Commit<K>, ...>  faces  Offer<..., Commit<K'>, ...>
//   Select<..., Roll, ...>       faces  Offer<..., Roll, ...>
//   Select<..., Abort, ...>      faces  Offer<..., Abort, ...>
//
// The party that selects is the active party of Fig. 11.  The party that
// receives is the passive party.  Commit, Roll and Abort are legal only
// as a branch of a Select or an Offer.  In any other position one party
// would decide alone.  The two endpoints could then take different
// branches, and nothing on the wire would tell either one what the other
// did.  The checkpoint mint refuses such a position.
//
// ── The semantics, rule by rule ─────────────────────────────────────
//
// Each party carries a checkpoint: a protocol position, the loop context
// at that position, and a flag that says the other party imposed it.
//
//   TS-Com, TS-Lab  A send meets a receive of the same payload type.  A
//                   Select meets an Offer, and label i of the Select
//                   meets branch i of the Offer.
//   TS-Cmt1         On Commit, the active party saves its continuation.
//                   The passive party saves its own continuation, and
//                   the save is imposed.
//   TS-Cmt2         If the passive party's checkpoint already is that
//                   continuation, it keeps its checkpoint and its flag.
//   TS-Rll1         On Roll, both parties return to their checkpoints.
//   TS-Rll2         A Roll by a party whose checkpoint is imposed is an
//                   error.  The rolling party must own its checkpoint.
//   TS-Abt1         On Abort, both parties return to their initial
//                   protocols and checkpoints.
//
// Because each commit happens at one label exchange, the two
// checkpoints always name a pair of positions that the session reached
// together.  A rollback therefore restores a state that existed.  The
// imposed flag keeps the rule of the paper that a rollback satisfies the
// party that requests it only when that party set the checkpoint (Sec.
// 2, Fig. 1(b)).
//
// The check uses the synchronous semantics of Fig. 11.  It explores
// every configuration it can reach and refuses the pair when one of
// them is stuck before both ends, when a label is Commit on one side
// and not on the other, when a Roll finds an imposed checkpoint, or
// when the model grows past its bound.
//
// ── What a rollback reverts ─────────────────────────────────────────
//
// The protocol position reverts, on both sides.  Program state does not:
// the calculus restores process state, and C++ code keeps its variables.
// So a payload that moves a permission cannot cross a checkpoint session.
// After a rollback the protocol would ask for the token again, and the
// receiver would still hold the first one.  The mint refuses every
// payload that is not plain in the sense of fixy/session/Flow.h.
// A crash branch cannot appear either, because no paper types the two
// together.  The mint refuses a Crash payload.

#include <fixy/session/Crash.h>
#include <fixy/session/Flow.h>
#include <fixy/session/Entry.h>
#include <fixy/session/Handle.h>
#include <fixy/session/Registry.h>

#include <foundation/NoObject.h>
#include <foundation/Platform.h>
#include <foundation/contracts/Armed.h>
#include <foundation/permissions/PermSet.h>

#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <functional>
#include <meta>
#include <optional>
#include <source_location>
#include <tuple>
#include <type_traits>
#include <utility>
#include <vector>

// The row-hash identity of a checkpoint handle, declared and never
// defined.  foundation/diag/RowHash.h folds it with the inner handle, so a
// checkpoint handle and the plain handle over the erased protocol take two
// cache slots.
namespace fixy::row_discipline {
template <typename Head, typename HeadLoop, typename Frame>
struct checkpoint_handle;
}  // namespace fixy::row_discipline

namespace fixy::session {

// ── The three primitives ────────────────────────────────────────────
//
// Commit, Roll and Abort and their registrations stand here, in the
// family `checkpoint` of the registry (fixy/session/Registry.h).  A
// checkpoint protocol cannot run on one endpoint, because compliance is a
// property of the pair, so each primitive is registered as not plain: the
// plain handle factory refuses a protocol that holds one, and only
// mint_checkpoint_session admits it.  Commit is a marker whose dual
// commits at the same label.  Roll and Abort are terminals that keep
// their place under composition, as a crashed endpoint does.  A dual pair
// is not compliant by that fact alone, because the imposed flag can still
// make a Roll fail, so the mint checks the pair.
//
// Commit<K> takes a checkpoint and continues with K, Roll returns both
// parties to their checkpoints, and Abort returns both parties to the
// start.  Each is a label of a choice that the two parties exchange.

template <typename K>
struct Commit {
    using next = K;
};

struct Roll {};

struct Abort {};

namespace combinators::checkpoint {

// No handle stands at one of the three: the checkpoint handle does the
// primitive in the step that exchanges its label.
inline constexpr ::fixy::session::transition::combinator commit{
    .shape = ^^Commit, .kind = ::fixy::session::transition::shape_kind::marker, .dual = ^^Commit, .is_plain = false};

inline constexpr ::fixy::session::transition::combinator roll{.shape = ^^Roll,
                                                              .kind = ::fixy::session::transition::shape_kind::terminal,
                                                              .dual = ^^Roll,
                                                              .absorbs_suffix = true,
                                                              .is_plain = false};

inline constexpr ::fixy::session::transition::combinator restart{.shape = ^^Abort,
                                                                 .kind =
                                                                     ::fixy::session::transition::shape_kind::terminal,
                                                                 .dual = ^^Abort,
                                                                 .absorbs_suffix = true,
                                                                 .is_plain = false};

inline constexpr ::fixy::session::transition::seal combinator_seal{.kind = ^^::fixy::session::transition::combinator,
                                                                   .count = 3};

}  // namespace combinators::checkpoint

// Each trait of this header is an alias and each _v form is a concept, so
// no program can specialize one to change its answer.

namespace detail::checkpoint {

// A primitive at the head of P, with no wrapper around it.
[[nodiscard]] consteval bool is_primitive_type(std::meta::info type) {
    const std::meta::info shape = ::fixy::session::transition::shape_of(type);
    return shape == ^^Commit || shape == ^^Roll || shape == ^^Abort;
}

}  // namespace detail::checkpoint

template <typename P>
using is_checkpoint_primitive = std::bool_constant<detail::checkpoint::is_primitive_type(^^P)>;

// ── Erasure to the plain protocol ───────────────────────────────────
//
// A checkpoint session runs over a plain handle.  The handle steps the
// erased protocol, in which Commit<K> is K and Roll and Abort are End: the
// checkpoint erasure of the one erase walk of fixy/session/Crash.h.  The
// branch count of every choice is unchanged, so a label index means the
// same branch in both protocols.  An unknown combinator has no erasure,
// so it stops the build.

namespace detail::checkpoint {

// The checkpoint erasure of the erase walk, with Roll and Abort as the two
// terminals that it reads as End.  Complexity: linear in the size of the
// protocol.
[[nodiscard]] consteval std::meta::info erased_of(std::meta::info protocol) {
    return ::fixy::session::transition::fold(protocol_registry, protocol,
                                             erase_algebra{erasure::checkpoints, ^^Roll, ^^Abort}, 0);
}

}  // namespace detail::checkpoint

template <typename P>
using checkpoint_erase_t = typename[:detail::checkpoint::erased_of(^^P):];

namespace detail::checkpoint {

template <typename LoopCtx>
struct erase_loop {
    using type = checkpoint_erase_t<LoopCtx>;
};
template <>
struct erase_loop<void> {
    using type = void;
};

template <typename LoopCtx>
using erase_loop_t = typename erase_loop<LoopCtx>::type;

// Continue, Loop, a vendor pin and a named state resolved, as the handle
// resolves them: the head of R and the loop context at that head.  The check
// of the pair reads each position under its pins and its names, so no head
// of a checkpoint handle holds a pin or a name.
template <typename R, typename LoopCtx>
struct resolve {
    using head = R;
    using loop = LoopCtx;
};
template <typename LoopCtx>
struct resolve<Continue, LoopCtx> : resolve<::fixy::session::detail::loop_body_t<LoopCtx>, LoopCtx> {};
template <typename B, typename LoopCtx>
struct resolve<Loop<B>, LoopCtx> : resolve<B, Loop<B>> {};
template <VendorBackend V, typename P, typename LoopCtx>
struct resolve<VendorPinned<V, P>, LoopCtx> : resolve<P, LoopCtx> {};
template <typename Name, typename LoopCtx>
struct resolve<Named<Name>, LoopCtx> : resolve<named_state_protocol_t<Named<Name>>, LoopCtx> {};

}  // namespace detail::checkpoint

// ── The compliance check (Def. 4.1 over Fig. 11) ────────────────────

enum class CheckpointVerdict : std::uint8_t {
    Compliant,
    // A primitive outside a choice branch, a payload that moves a
    // permission or carries a crash, or a combinator the model does not
    // know.
    NotCheckpointShaped,
    // A reachable configuration cannot move and is not End against End.
    Stuck,
    // The Select has more labels than the facing Offer has branches.
    LabelOutOfRange,
    // Label i is a primitive on one side and something else on the
    // other, or a different primitive.
    LabelsDisagree,
    // TS-Rll2: the rolling party does not own its checkpoint.
    RollToImposedCheckpoint,
    // A Continue with no loop, or a loop whose body never reaches a head.
    LoopUnresolved,
    // The walk of the model needs more work than its bound admits.
    TooManyConfigurations,
};

// The bound of the work of the compliance check of one pair, when the caller
// gives none.  The check counts admit_work units for each node of the shape
// that it visits and for each edge of the model that it admits, and one
// unit for each kept answer and each configuration that it compares.  A
// check past its bound gives TooManyConfigurations, and the gate refuses
// the pair with the diagnostic Checkpoint_Work_Bound.  A caller with a
// larger pair gives the gate and the mint a larger bound, the template
// argument WorkBound.
//
// The default is below the operation limit of a constant evaluation
// (-fconstexpr-ops-limit=33554432 in this project) for the shapes that the
// tests try: a chain, a row of wide choices and a row of shared arms.  A
// count of configurations cannot do that: a chain of eight branches meets
// the limit near 800 configurations, and a row of choices with 150 labels
// near 120.  The work of each of these at the limit is more than 360000
// units, and the pair of 321 configurations of the tests needs 116132.  A
// bound past the operation limit gives the error of the operation limit,
// which names the flag that raises it.
inline constexpr std::size_t checkpoint_work_bound = 196608;

namespace detail::checkpoint {

namespace tr = ::fixy::session::transition;
using info = std::meta::info;

enum class Kind : std::uint8_t {
    End,
    Continue,
    Send,
    Recv,
    Select,
    Offer,
    Loop,
    Commit,
    Roll,
    Abort,
    Unknown
};

// The work of one walk.  The walk admits each edge of the model, and the
// admit compares the next configuration with each configuration that the
// walk saw.  One compare is one unit of work, and one admit is admit_work
// units more, for its unfold, its reads of the protocol and its label
// exchange.  So the work grows with the edges times the configurations, and
// a wide choice costs as much as a long chain.  A walk past its bound
// (checkpoint_work_bound above) ends with TooManyConfigurations.  The check
// of the shape before the walk counts its work in the same bound.
inline constexpr std::size_t admit_work = 64;
inline constexpr int unfold_bound = 64;
inline constexpr int depth_bound = 256;

// The type under each VendorPinned and each named state at the head.
consteval info strip(info type) {
    type = std::meta::dealias(type);
    for (;;) {
        if (!std::meta::has_template_arguments(type)) return type;
        const info family = std::meta::template_of(type);
        if (family == ^^VendorPinned) {
            type = std::meta::dealias(std::meta::template_arguments_of(type)[1]);
        } else if (family == ^^Named) {
            const info body = tr::protocol_under_name(protocol_registry, type);
            if (body == info{}) return type;
            type = body;
        } else {
            return type;
        }
    }
}

// The type under each vendor pin and each named state of P.  The door and
// the recorder read a branch through it, so they see the primitive that the
// check saw.
template <typename P>
using bare_t = typename[:strip(^^P):];

consteval Kind kind_of(info type) {
    type = strip(type);
    if (type == ^^End) return Kind::End;
    if (type == ^^Continue) return Kind::Continue;
    if (type == ^^Roll) return Kind::Roll;
    if (type == ^^Abort) return Kind::Abort;
    if (!std::meta::has_template_arguments(type)) return Kind::Unknown;
    const info family = std::meta::template_of(type);
    if (family == ^^Send) return Kind::Send;
    if (family == ^^Recv) return Kind::Recv;
    if (family == ^^Select) return Kind::Select;
    if (family == ^^Offer) return Kind::Offer;
    if (family == ^^Loop) return Kind::Loop;
    if (family == ^^Commit) return Kind::Commit;
    return Kind::Unknown;
}

consteval info argument(info type, std::size_t index) {
    return strip(std::meta::template_arguments_of(strip(type))[index]);
}

// The template arguments of a Select or an Offer.  The real branches start
// at `first_`, because a Sender tag is not a branch.  A reader reads the
// branches through the pointer of data().  No walk changes the vector.
class BranchList {
public:
    consteval explicit BranchList(info choice) : arguments_{std::meta::template_arguments_of(strip(choice))} {
        if (!arguments_.empty()) {
            const info head = std::meta::dealias(arguments_.data()[0]);
            if (std::meta::has_template_arguments(head) && std::meta::template_of(head) == ^^Sender) {
                first_ = 1;
            }
        }
    }

    [[nodiscard]] consteval const info* data() const { return arguments_.data() + first_; }
    [[nodiscard]] consteval std::size_t size() const { return arguments_.size() - first_; }

private:
    std::vector<info> arguments_;
    std::size_t first_ = 0;
};

consteval BranchList branches_of(info type) { return BranchList{type}; }

// A rollback cannot recall a delegated endpoint that the peer already
// holds, so a checkpoint session carries no delegation either.  The
// question is the one a crash session asks, through the same walk of
// fixy/session/Crash.h.  The walk takes a reflection, so it reaches the
// plain-payload concept through a substitution, which also keeps the
// named diagnostic of a payload that it cannot read.
consteval bool payload_admitted(info payload) {
    return std::meta::extract<bool>(std::meta::substitute(^^is_plain_payload_v, {payload}))
        && !crash::is_crash_payload_type(payload) && !crash::conveys_delegation(payload);
}

consteval bool is_primitive(Kind kind) { return kind == Kind::Commit || kind == Kind::Roll || kind == Kind::Abort; }

// ── The check of the shape ──────────────────────────────────────────
//
// The check visits the protocol from its start.  A choice whose branches
// share one arm, and a row of such choices, makes a protocol tree that is
// exponential in the length of the row.  So the check keeps the answer of
// each choice and of each branch of a choice that it visited, and a second
// visit reads the kept answer.  A send, a receive and a loop have their
// answers from their children, so a chain of steps between two choices keeps
// no answer.  A branch that is the branch before it has the answer of that
// branch.
//
// Each visit of a node is admit_work units of the work of the bound, and
// each compare with a kept answer is one unit.  A check past its bound, or
// past depth_bound at the first visit of a node, answers false, and a false
// answer of a node is the false answer of the protocol.  Complexity: each
// choice and each branch of a choice is visited one time, and each chain of
// steps one time for each choice or branch above it.  A search of the kept
// answers is linear in them.

// The kept answer of a node at one place: in a branch of a choice or not.
// A primitive is legal only in a branch, so the place is part of the
// question.
struct ShapeAnswer {
    info node = ^^void;
    bool is_at_branch = false;
    bool is_shaped = false;
};

// The kept answers of the check of one protocol, its work and its bound.
struct ShapeCheck {
    tr::stack<ShapeAnswer> answered;
    std::size_t work = 0;
    std::size_t bound = 0;
};

// The check keeps the answer of a choice and of a branch of a choice.  The
// search reads the newest answer first, because the arm that a choice shares
// with an earlier choice is the last node that the check answered.  The
// check of a node and of its children is one function, so each level of the
// protocol is one call deep, as the payload walk of each step needs.
consteval bool shaped(info type, bool at_branch, int depth, ShapeCheck& check) {
    const info node = strip(type);
    const Kind kind = kind_of(node);
    const bool is_kept = at_branch || kind == Kind::Select || kind == Kind::Offer;
    if (is_kept) {
        const ShapeAnswer* const known = check.answered.items;
        for (std::size_t place = check.answered.top; place != 0; --place) {
            ++check.work;
            const ShapeAnswer& answer = known[place - 1];
            if (answer.node == node && answer.is_at_branch == at_branch) return answer.is_shaped;
        }
    }
    check.work += admit_work;
    if (depth > depth_bound || check.work > check.bound) return false;
    bool is_shaped = false;
    switch (kind) {
        case Kind::End:
        case Kind::Continue:
            is_shaped = true;
            break;
        case Kind::Send:
        case Kind::Recv:
            is_shaped = payload_admitted(argument(node, 0)) && shaped(argument(node, 1), false, depth + 1, check);
            break;
        case Kind::Select:
        case Kind::Offer: {
            const BranchList branches = branches_of(node);
            const info* const branch = branches.data();
            const std::size_t branch_count = branches.size();
            is_shaped = branch_count != 0;
            info previous = ^^void;
            for (std::size_t place = 0; is_shaped && place < branch_count; ++place) {
                const info arm = strip(branch[place]);
                if (place != 0 && arm == previous) continue;
                previous = arm;
                is_shaped = shaped(arm, true, depth + 1, check);
            }
            break;
        }
        case Kind::Loop:
            is_shaped = shaped(argument(node, 0), false, depth + 1, check);
            break;
        case Kind::Commit:
            is_shaped = at_branch && shaped(argument(node, 0), false, depth + 1, check);
            break;
        case Kind::Roll:
        case Kind::Abort:
            is_shaped = at_branch;
            break;
        case Kind::Unknown:
        default:
            is_shaped = false;
            break;
    }
    if (is_kept) tr::push(check.answered, ShapeAnswer{node, at_branch, is_shaped});
    return is_shaped;
}

// The check of the shape of one protocol, with answers of its own.  The two
// protocols of a pair share the work of the bound.
consteval bool is_checkpoint_shaped(info protocol, std::size_t& work, std::size_t bound) {
    ShapeCheck check{};
    check.work = work;
    check.bound = bound;
    const bool is_shaped = shaped(protocol, false, 0, check);
    work = check.work;
    return is_shaped;
}

struct Party {
    info position = ^^void;
    info loop = ^^void;
    info checkpoint = ^^void;
    info checkpoint_loop = ^^void;
    bool is_imposed = false;
};

struct Configuration {
    Party left;
    Party right;
};

consteval bool same_party(const Party& lhs, const Party& rhs) {
    return lhs.position == rhs.position && lhs.loop == rhs.loop && lhs.checkpoint == rhs.checkpoint
        && lhs.checkpoint_loop == rhs.checkpoint_loop && lhs.is_imposed == rhs.is_imposed;
}

consteval bool same_configuration(const Configuration& lhs, const Configuration& rhs) {
    return same_party(lhs.left, rhs.left) && same_party(lhs.right, rhs.right);
}

// Resolves Loop and Continue at the head, as the handle does.
consteval bool unfold(Party& party) {
    for (int step = 0; step < unfold_bound; ++step) {
        const Kind kind = kind_of(party.position);
        if (kind == Kind::Loop) {
            party.loop = strip(party.position);
            party.position = argument(party.position, 0);
        } else if (kind == Kind::Continue) {
            if (party.loop == ^^void) return false;
            party.position = argument(party.loop, 0);
        } else {
            party.position = strip(party.position);
            return true;
        }
    }
    return false;
}

consteval Party initial_party(info protocol) {
    const info start = strip(protocol);
    return Party{start, ^^void, start, ^^void, false};
}

// `seen` holds each configuration that the walk admitted, and `pending`
// each admitted configuration that the walk has not stepped yet.  Each is
// a stack of fixy/session/Registry.h, read through a pointer, so
// the walk changes no vector.  `work` counts the units of `bound`.
struct Exploration {
    tr::stack<Configuration> seen;
    tr::stack<Configuration> pending;
    CheckpointVerdict verdict = CheckpointVerdict::Compliant;
    std::size_t work = 0;
    std::size_t bound = 0;
};

// Admits one edge into `next`.  The work of the admit is counted before the
// scan of `seen`, so a walk past its bound ends before it does the work.
consteval void admit(Exploration& run, Configuration next) {
    run.work += admit_work + run.seen.top;
    if (run.work > run.bound) {
        run.verdict = CheckpointVerdict::TooManyConfigurations;
        return;
    }
    if (!unfold(next.left) || !unfold(next.right)) {
        run.verdict = CheckpointVerdict::LoopUnresolved;
        return;
    }
    const Configuration* const known = run.seen.items;
    for (std::size_t place = 0; place < run.seen.top; ++place) {
        if (same_configuration(known[place], next)) return;
    }
    tr::push(run.seen, next);
    tr::push(run.pending, next);
}

// One label exchange.  `active` selected label `index`, and `passive`
// received it.  Each branch is read under its wrappers and named states one
// time, so each later read of the position resolves no name.
consteval CheckpointVerdict exchange(Exploration& run, Party active, Party passive, info active_branch,
                                     info passive_branch, const Party& active_start, const Party& passive_start,
                                     bool active_is_left) {
    active_branch = strip(active_branch);
    passive_branch = strip(passive_branch);
    const Kind active_kind = kind_of(active_branch);
    const Kind passive_kind = kind_of(passive_branch);
    if ((is_primitive(active_kind) || is_primitive(passive_kind)) && active_kind != passive_kind) {
        return CheckpointVerdict::LabelsDisagree;
    }
    switch (active_kind) {
        case Kind::Commit: {
            const info active_next = argument(active_branch, 0);
            const info passive_next = argument(passive_branch, 0);
            active.checkpoint = active_next;
            active.checkpoint_loop = active.loop;
            active.is_imposed = false;
            active.position = active_next;
            const bool keeps = passive.checkpoint == passive_next && passive.checkpoint_loop == passive.loop;
            if (!keeps) {
                passive.checkpoint = passive_next;
                passive.checkpoint_loop = passive.loop;
                passive.is_imposed = true;
            }
            passive.position = passive_next;
            break;
        }
        case Kind::Roll:
            if (active.is_imposed) return CheckpointVerdict::RollToImposedCheckpoint;
            active.position = active.checkpoint;
            active.loop = active.checkpoint_loop;
            passive.position = passive.checkpoint;
            passive.loop = passive.checkpoint_loop;
            break;
        case Kind::Abort:
            active = active_start;
            passive = passive_start;
            break;
        case Kind::End:
        case Kind::Continue:
        case Kind::Send:
        case Kind::Recv:
        case Kind::Select:
        case Kind::Offer:
        case Kind::Loop:
        case Kind::Unknown:
        default:
            active.position = active_branch;
            passive.position = passive_branch;
            break;
    }
    admit(run, active_is_left ? Configuration{active, passive} : Configuration{passive, active});
    return run.verdict;
}

consteval CheckpointVerdict choose(Exploration& run, const Party& active, const Party& passive,
                                   const Party& active_start, const Party& passive_start, bool active_is_left) {
    const BranchList labels = branches_of(active.position);
    const BranchList accepted = branches_of(passive.position);
    const std::size_t label_count = labels.size();
    if (label_count > accepted.size()) return CheckpointVerdict::LabelOutOfRange;
    const info* const label = labels.data();
    const info* const acceptance = accepted.data();
    for (std::size_t index = 0; index < label_count; ++index) {
        const CheckpointVerdict step = exchange(run, active, passive, label[index], acceptance[index], active_start,
                                                passive_start, active_is_left);
        if (step != CheckpointVerdict::Compliant) return step;
    }
    return CheckpointVerdict::Compliant;
}

// The check of the shape and the walk share one bound of work.
// Complexity: the number of edges times the cost of the visited-set
// lookup, which is linear in the configurations.  `bound` caps it.
// The walk follows the protocol of each name.  On a cycle of names with a
// step between two names, the check of the shape stops at depth_bound.  A
// name whose protocol is a name of the cycle has no head under it, and the
// walk stops at the loop limit of a constant evaluation.  The gate refuses
// each cycle of names first, with Protocol_Named_State_Cycle.
consteval CheckpointVerdict verdict_of(info left_protocol, info right_protocol, std::size_t bound) {
    std::size_t shape_work = 0;
    if (!is_checkpoint_shaped(left_protocol, shape_work, bound)
        || !is_checkpoint_shaped(right_protocol, shape_work, bound)) {
        return shape_work > bound ? CheckpointVerdict::TooManyConfigurations : CheckpointVerdict::NotCheckpointShaped;
    }
    const Party left_start = initial_party(left_protocol);
    const Party right_start = initial_party(right_protocol);
    Exploration run{};
    run.work = shape_work;
    run.bound = bound;
    admit(run, Configuration{left_start, right_start});
    while (run.verdict == CheckpointVerdict::Compliant && run.pending.top != 0) {
        const Configuration here = run.pending.items[run.pending.top - 1];
        --run.pending.top;
        const Kind left = kind_of(here.left.position);
        const Kind right = kind_of(here.right.position);
        if (left == Kind::End && right == Kind::End) continue;
        if (left == Kind::Send && right == Kind::Recv) {
            if (argument(here.left.position, 0) != argument(here.right.position, 0)) return CheckpointVerdict::Stuck;
            Configuration next = here;
            next.left.position = argument(here.left.position, 1);
            next.right.position = argument(here.right.position, 1);
            admit(run, next);
        } else if (left == Kind::Recv && right == Kind::Send) {
            if (argument(here.left.position, 0) != argument(here.right.position, 0)) return CheckpointVerdict::Stuck;
            Configuration next = here;
            next.left.position = argument(here.left.position, 1);
            next.right.position = argument(here.right.position, 1);
            admit(run, next);
        } else if (left == Kind::Select && right == Kind::Offer) {
            const CheckpointVerdict step = choose(run, here.left, here.right, left_start, right_start, true);
            if (step != CheckpointVerdict::Compliant) return step;
        } else if (left == Kind::Offer && right == Kind::Select) {
            const CheckpointVerdict step = choose(run, here.right, here.left, right_start, left_start, false);
            if (step != CheckpointVerdict::Compliant) return step;
        } else {
            return CheckpointVerdict::Stuck;
        }
    }
    return run.verdict;
}

// ── The sealed cache of the verdict ─────────────────────────────────
//
// The gate asks the verdict of one pair in each of its clauses, and one
// walk of a large pair costs a large part of the operation limit of a
// constant evaluation.  The cache is an alias template, which cannot be
// specialized, and the reader takes the verdict from the value argument of
// the cell that the alias names.  A specialization of the cell class
// changes its body, which no reader reads.  GCC keeps one specialization
// of an alias template for each argument list, so a translation unit walks
// each pair one time for each bound.

// The cell of the verdict of one pair.  It holds nothing.
template <CheckpointVerdict Verdict>
struct verdict_cell {};

template <class Left, class Right, std::size_t Bound>
using verdict_cached = verdict_cell<verdict_of(^^Left, ^^Right, Bound)>;

// The verdict of a pair under a bound of work.  A reflection that is not a
// type has no cache cell, so its verdict comes from the walk.
//
// A walk that stops the constant evaluation of the cell, at the operation
// limit or with an error, makes the substitution fail, and the failure of a
// substitution names no cause.  The walk then runs again here, in the
// evaluation that asked, so the error of the walk stops that evaluation with
// its own text: the operation limit and the flag that raises it, or the text
// of the error.  Complexity: one walk for each pair of types and bound in a
// translation unit, and a lookup after that.  A walk that fails runs two
// times.
consteval CheckpointVerdict cached_verdict_of(info left_protocol, info right_protocol, std::size_t bound) {
    if (!std::meta::is_type(left_protocol) || !std::meta::is_type(right_protocol)) {
        return verdict_of(left_protocol, right_protocol, bound);
    }
    const info bound_value = std::meta::reflect_constant(bound);
    if (!std::meta::can_substitute(^^verdict_cached, {left_protocol, right_protocol, bound_value})) {
        return verdict_of(left_protocol, right_protocol, bound);
    }
    const info cell = std::meta::substitute(^^verdict_cached, {left_protocol, right_protocol, bound_value});
    return std::meta::extract<CheckpointVerdict>(argument(cell, 0));
}

// The refusal of a pair whose check needs more work than its bound.  The
// gate names the bound WorkBound in the context of the refusal.
consteval bool walk_fits_bound(info left_protocol, info right_protocol, std::size_t bound) {
    if (cached_verdict_of(left_protocol, right_protocol, bound) == CheckpointVerdict::TooManyConfigurations) {
        throw std::meta::exception(
            u8"fixy::session::diagnostic [Checkpoint_Work_Bound]: the compliance check of the checkpoint pair needs "
            u8"more work than its bound, the template argument WorkBound.  The check counts admit_work units for each "
            u8"node of the shape that it visits and for each edge of the model that it admits, and one unit for each "
            u8"kept answer and each configuration that it compares.  The default bound is "
            u8"fixy::session::checkpoint_work_bound.  Give a larger bound for this pair: "
            u8"mint_checkpoint_session<Proto, PeerProto, Policy, WorkBound>.",
            left_protocol);
    }
    return true;
}

// True when the question is a CheckpointPair whose verdict is Compliant.
// Any other type answers false.
[[nodiscard]] consteval bool pair_is_compliant(info question);

}  // namespace detail::checkpoint

// The verdict for a pair of checkpoint protocols, one for each endpoint,
// under a bound of work.  A function at namespace scope that is not a
// template, so no translation unit can specialize the verdict.  It reads the
// sealed cache, so a translation unit walks each pair one time for each
// bound.
[[nodiscard]] consteval CheckpointVerdict checkpoint_verdict_of(std::meta::info protocol, std::meta::info peer_protocol,
                                                                std::size_t work_bound = checkpoint_work_bound) {
    return detail::checkpoint::cached_verdict_of(protocol, peer_protocol, work_bound);
}

// The question "are these two protocols compliant", as a one-argument
// trait that can hold an armed cell.
template <typename P1, typename P2>
struct CheckpointPair {};

consteval bool detail::checkpoint::pair_is_compliant(info question) {
    const info asked = std::meta::dealias(question);
    if (!std::meta::is_type(asked) || !std::meta::has_template_arguments(asked)
        || std::meta::template_of(asked) != ^^CheckpointPair) {
        return false;
    }
    const std::vector<info> sides = std::meta::template_arguments_of(asked);
    return cached_verdict_of(sides[0], sides[1], checkpoint_work_bound) == CheckpointVerdict::Compliant;
}

template <typename Q>
using is_checkpoint_compliant = std::bool_constant<detail::checkpoint::pair_is_compliant(^^Q)>;

template <typename P1, typename P2>
concept checkpoint_compliant_v =
    detail::checkpoint::cached_verdict_of(^^P1, ^^P2, checkpoint_work_bound) == CheckpointVerdict::Compliant;

// Whether Proto can run on one endpoint of a checkpoint session whose
// other endpoint runs PeerProto, with the bound WorkBound on the work of
// the check.  Each refusal is its own atomic constraint, so the diagnostic
// names the verdict.  A pair whose check needs more work than its bound is
// refused with the diagnostic Checkpoint_Work_Bound, which names the bound
// and the way to raise it.  The last verdict clause asks for Compliant, so a
// verdict added later and not listed here still refuses.  The runtime reads
// nested members of each protocol, so the first two clauses refuse a node
// whose members disagree with its arguments (fixy/session/Protocol.h).  Each
// clause calls a function at namespace scope that is not a template, so no
// translation unit can specialize a verdict.  The clauses read the sealed
// cache, so they share one walk of the pair.
template <typename Proto, typename PeerProto, std::size_t WorkBound = checkpoint_work_bound>
concept CheckpointSessionAdmissible =
    ::fixy::session::detail::require_agreeing_members(^^Proto)
    && ::fixy::session::detail::require_agreeing_members(^^PeerProto)
    && detail::checkpoint::cached_verdict_of(^^Proto, ^^PeerProto, WorkBound) != CheckpointVerdict::NotCheckpointShaped
    && detail::checkpoint::cached_verdict_of(^^Proto, ^^PeerProto, WorkBound) != CheckpointVerdict::Stuck
    && detail::checkpoint::cached_verdict_of(^^Proto, ^^PeerProto, WorkBound) != CheckpointVerdict::LabelOutOfRange
    && detail::checkpoint::cached_verdict_of(^^Proto, ^^PeerProto, WorkBound) != CheckpointVerdict::LabelsDisagree
    && detail::checkpoint::cached_verdict_of(^^Proto, ^^PeerProto, WorkBound)
           != CheckpointVerdict::RollToImposedCheckpoint
    && detail::checkpoint::cached_verdict_of(^^Proto, ^^PeerProto, WorkBound) != CheckpointVerdict::LoopUnresolved
    && detail::checkpoint::walk_fits_bound(^^Proto, ^^PeerProto, WorkBound)
    && detail::checkpoint::cached_verdict_of(^^Proto, ^^PeerProto, WorkBound) == CheckpointVerdict::Compliant
    && WellFormedRunnableProtocol<checkpoint_erase_t<Proto>>
    && WellFormedRunnableProtocol<checkpoint_erase_t<PeerProto>>;

// The whole gate of mint_checkpoint_session: a compliant pair under the
// bound WorkBound, a Resource that a mint admits, and a context that admits
// the effect row of the protocol that the plain handle runs
// (CtxAdmitsProtocolRow of fixy/session/Admission.h).  The erasure keeps
// each payload, so that row is the row of Proto.
template <typename Ctx, typename Proto, typename PeerProto, typename Resource,
          std::size_t WorkBound = checkpoint_work_bound>
concept CtxFitsCheckpointSession =
    CheckpointSessionAdmissible<Proto, PeerProto, WorkBound> && PlainSessionResource<Resource>
    && CtxAdmitsProtocolRow<Ctx, checkpoint_erase_t<Proto>>;

// ── The runtime ─────────────────────────────────────────────────────

// The checkpoint state of one endpoint: its initial protocol, and its
// checkpoint with the loop context at it.  The imposed flag is not here,
// because the compliance check proved that every Roll this endpoint can
// select finds a checkpoint it owns.
template <typename Initial, typename Saved, typename SavedLoop>
struct CheckpointFrame {
    using initial = Initial;
    using saved = Saved;
    using saved_loop = SavedLoop;
};

template <typename Inner, typename Head, typename HeadLoop, typename Frame>
class CheckpointHandle;

// ── The door of the checkpoint session ──────────────────────────────
//
// The one class that builds a checkpoint handle and steps it.  The
// handle befriends this class and no other, and no class template, so no
// specialization of the handle reaches the constructor or the inner
// handle of another.  The class is final, and it is defined in this
// header, so a second definition in a translation unit that includes the
// header is a redefinition error.
//
// Each public member either states the whole gate of
// mint_checkpoint_session (open), or takes a live checkpoint handle and
// does one complete step on it.  A Commit, a Roll or an Abort takes effect
// in the step that exchanges its label.  A rollback rebuilds the plain
// handle through the rewind of fixy/session/Handle.h.  The handle forwards
// each step here, so a direct call is the same operation as the method.
// No object of the class exists.
class CheckpointDoor final : ::foundation::NoObject<CheckpointDoor> {
    template <typename R, typename Loop, typename NewFrame, typename Next>
    [[nodiscard]] static constexpr auto wrap_(Next next) noexcept {
        using Resolved = detail::checkpoint::resolve<R, Loop>;
        return CheckpointHandle<Next, typename Resolved::head, typename Resolved::loop, NewFrame>{std::move(next)};
    }

    // The plain handle that a branch reaches: Commit<K> reaches K, and
    // Roll and Abort reach End.  It is the handle at the erased protocol
    // of the branch, with the erased loop context of the position.
    template <typename Branch, typename Inner, typename HeadLoop>
    using inner_branch_t =
        HandleFactory::handle_at<checkpoint_erase_t<Branch>, typename Inner::resource_type,
                                 detail::checkpoint::erase_loop_t<HeadLoop>, typename Inner::abandonment_policy>;

    // Rebuilds the plain handle at the checkpoint, or at the start, from
    // the handle at End that the Roll or the Abort reached.  The rewind of
    // fixy/session/Handle.h opens a new session at that position.
    template <typename Saved, typename SavedLoop, typename AtEnd>
    [[nodiscard]] static constexpr auto rebuild_(AtEnd at_end) noexcept {
        return HandleFactory::rewind<checkpoint_erase_t<Saved>, detail::checkpoint::erase_loop_t<SavedLoop>>(
            std::move(at_end));
    }

    // The effect of branch B, given the plain handle it reached.  The check
    // reads the branch under its vendor pins and its names, so a pinned Roll
    // and a Roll behind a name roll back.
    template <typename Branch, typename HeadLoop, typename Frame, typename Next>
    [[nodiscard]] static constexpr auto take_branch_(Next next) noexcept {
        using Bare = detail::checkpoint::bare_t<Branch>;
        if constexpr (std::is_same_v<Bare, Roll>) {
            using Saved = typename Frame::saved;
            using SavedLoop = typename Frame::saved_loop;
            return wrap_<Saved, SavedLoop, Frame>(rebuild_<Saved, SavedLoop>(std::move(next)));
        } else if constexpr (std::is_same_v<Bare, Abort>) {
            using Initial = typename Frame::initial;
            using Restart = CheckpointFrame<Initial, Initial, void>;
            return wrap_<Initial, void, Restart>(rebuild_<Initial, void>(std::move(next)));
        } else if constexpr (is_checkpoint_primitive<Bare>::value) {
            using K = typename Bare::next;
            using Saved = CheckpointFrame<typename Frame::initial, K, HeadLoop>;
            return wrap_<K, HeadLoop, Saved>(std::move(next));
        } else {
            return wrap_<Branch, HeadLoop, Frame>(std::move(next));
        }
    }

    // The plain handle cannot say which label it took when two branches
    // erase to one type, so the branch that the wire word names decides.
    // The plain handle refuses a word that names no branch first
    // (Word_Names_No_Branch), and the erasure keeps the count and the order
    // of the branches.  So the label names a branch whose plain handle is
    // Next, and a dispatch that runs no branch is a broken invariant.
    template <typename Branches, typename Inner, typename HeadLoop, typename Frame, typename Next, typename Handler,
              std::size_t... Is>
    static constexpr auto dispatch_(std::size_t label, Next next, Handler& handler, std::index_sequence<Is...>) {
        using First = std::tuple_element_t<0, Branches>;
        using Result = std::invoke_result_t<Handler&, decltype(take_branch_<First, HeadLoop, Frame>(
                                                          std::declval<inner_branch_t<First, Inner, HeadLoop>>()))>;
        bool is_dispatched = false;
        if constexpr (std::is_void_v<Result>) {
            (
                [&] {
                    using Branch = std::tuple_element_t<Is, Branches>;
                    if constexpr (std::is_same_v<inner_branch_t<Branch, Inner, HeadLoop>, Next>) {
                        if (!is_dispatched && label == Is) {
                            is_dispatched = true;
                            std::invoke(handler, take_branch_<Branch, HeadLoop, Frame>(std::move(next)));
                        }
                    }
                }(),
                ...);
            CRUCIBLE_FATAL_INVARIANT(is_dispatched);
        } else {
            std::optional<Result> result;
            (
                [&] {
                    using Branch = std::tuple_element_t<Is, Branches>;
                    if constexpr (std::is_same_v<inner_branch_t<Branch, Inner, HeadLoop>, Next>) {
                        if (!is_dispatched && label == Is) {
                            is_dispatched = true;
                            result.emplace(
                                std::invoke(handler, take_branch_<Branch, HeadLoop, Frame>(std::move(next))));
                        }
                    }
                }(),
                ...);
            CRUCIBLE_FATAL_INVARIANT(is_dispatched);
            return std::move(*result);
        }
    }

public:
    // mint_checkpoint_session: opens the plain handle of the erased
    // protocol, and puts around it the checkpoint handle at the start of
    // Proto.  The first checkpoint of the session is the start.
    template <typename Proto, typename PeerProto, AbandonmentPolicy Policy,
              std::size_t WorkBound = checkpoint_work_bound, typename Ctx, typename Resource>
        requires CtxFitsCheckpointSession<Ctx, Proto, PeerProto, Resource, WorkBound>
    [[nodiscard]] static constexpr auto open(Ctx const& ctx, Resource resource, std::source_location loc) noexcept {
        using Start = detail::checkpoint::resolve<Proto, void>;
        auto inner =
            mint_session<checkpoint_erase_t<Proto>, Policy, Ctx, Resource>(ctx, std::forward<Resource>(resource), loc);
        return CheckpointHandle<decltype(inner), typename Start::head, typename Start::loop,
                                CheckpointFrame<Proto, Proto, void>>{std::move(inner)};
    }

    template <typename Inner, typename Head, typename HeadLoop, typename Frame, typename Transport>
        requires is_send_v<Head> && (!is_keyed_step_v<Head>)
              && WriteTransport<Transport, typename Inner::resource_type, typename Head::message_type>
    [[nodiscard]] static constexpr auto send(CheckpointHandle<Inner, Head, HeadLoop, Frame>&& handle,
                                             detail::sent_value_t<typename Head::message_type> value,
                                             Transport transport) {
        return wrap_<typename Head::next, HeadLoop, Frame>(
            std::move(handle.inner_).send(std::move(value), std::move(transport)));
    }

    template <typename Inner, typename Head, typename HeadLoop, typename Frame, typename Transport>
        requires is_recv_v<Head> && (!is_keyed_step_v<Head>)
              && ReadTransport<Transport, typename Inner::resource_type, typename Head::message_type>
    [[nodiscard]] static constexpr auto recv(CheckpointHandle<Inner, Head, HeadLoop, Frame>&& handle,
                                             Transport transport) {
        auto [value, next] = std::move(handle.inner_).recv(std::move(transport));
        return std::pair{std::move(value), wrap_<typename Head::next, HeadLoop, Frame>(std::move(next))};
    }

    // A keyed message is its label word and then the value of its payload
    // (fixy/session/Transport.h).  The step moves the word, and the handle
    // then stands at the value step, or past the message when the payload
    // is void (keyed_landing_t).
    template <typename Inner, typename Head, typename HeadLoop, typename Frame, typename Transport>
        requires is_send_v<Head> && is_keyed_step_v<Head>
              && WriteTransport<Transport, typename Inner::resource_type, std::size_t>
    [[nodiscard]] static constexpr auto send(CheckpointHandle<Inner, Head, HeadLoop, Frame>&& handle,
                                             Transport transport) {
        return wrap_<keyed_landing_t<Head>, HeadLoop, Frame>(std::move(handle.inner_).send(std::move(transport)));
    }

    template <typename Inner, typename Head, typename HeadLoop, typename Frame, typename Transport>
        requires is_recv_v<Head> && is_keyed_step_v<Head>
              && ReadTransport<Transport, typename Inner::resource_type, std::size_t>
    [[nodiscard]] static constexpr auto recv(CheckpointHandle<Inner, Head, HeadLoop, Frame>&& handle,
                                             Transport transport) {
        return wrap_<keyed_landing_t<Head>, HeadLoop, Frame>(std::move(handle.inner_).recv(std::move(transport)));
    }

    // Selects label I and tells the peer.  A Commit, Roll or Abort takes
    // effect here, on this side, in the same call.  A keyed choice enters
    // its branch past the label word, and a checkpoint primitive is never a
    // keyed branch, so the landing is the branch that take_branch_ reads
    // (branch_landing_t in fixy/session/Transport.h).
    template <std::size_t I, typename Inner, typename Head, typename HeadLoop, typename Frame, typename Transport>
        requires is_select_v<Head> && WriteTransport<Transport, typename Inner::resource_type, std::size_t>
    [[nodiscard]] static constexpr auto select(CheckpointHandle<Inner, Head, HeadLoop, Frame>&& handle,
                                               Transport transport) {
        return take_branch_<branch_landing_t<Head, I>, HeadLoop, Frame>(
            std::move(handle.inner_).template select<I>(std::move(transport)));
    }

    // Receives the peer's wire word and calls the handler with the handle
    // for the branch that the word names.  A Commit, Roll or Abort takes
    // effect before the call.  Every branch must give the handler the
    // same return type.  The word is the word of the erased protocol that
    // the plain handle runs, so the branch comes from that protocol
    // (branch_of_wire_word in fixy/session/Transport.h).  The erasure
    // keeps the count and the order of the branches.
    template <typename Inner, typename Head, typename HeadLoop, typename Frame, typename Transport, typename Handler>
        requires is_offer_v<Head> && ReadTransport<Transport, typename Inner::resource_type, std::size_t>
    static constexpr auto branch(CheckpointHandle<Inner, Head, HeadLoop, Frame>&& handle, Transport transport,
                                 Handler handler) {
        constexpr std::size_t count = std::tuple_size_v<typename Head::branches_tuple>;
        using Branches = typename decltype([]<std::size_t... Is>(std::index_sequence<Is...>) {
            return std::type_identity<std::tuple<branch_landing_t<Head, Is>...>>{};
        }(std::make_index_sequence<count>{}))::type;
        std::size_t label = count;
        auto read_label = ::fixy::session::detail::observed_read<std::size_t, typename Inner::resource_type>(
            transport,
            [&label](std::size_t word) noexcept { label = branch_of_wire_word<typename Inner::protocol>(word); });
        return std::move(handle.inner_).branch(read_label, [&handler, &label](auto next) {
            return dispatch_<Branches, Inner, HeadLoop, Frame>(label, std::move(next), handler,
                                                               std::make_index_sequence<count>{});
        });
    }
};

// A plain handle over the erased protocol, with the original protocol
// and the checkpoint state in the type.  Commit, Roll and Abort take
// effect in the same call that exchanges their label, so no handle is
// ever positioned at one of them.
template <typename Inner, typename Head, typename HeadLoop, typename Frame>
class [[nodiscard]] CheckpointHandle {
    Inner inner_;

    // The one friend.  The door builds each checkpoint handle and does each
    // step, so no specialization of this template reaches another.
    friend class CheckpointDoor;

    constexpr explicit CheckpointHandle(Inner inner) noexcept : inner_{std::move(inner)} {}

    using resource_t = typename Inner::resource_type;
    using policy_t = typename Inner::abandonment_policy;

public:
    using protocol = Head;
    using inner_type = Inner;
    using resource_type = resource_t;
    using abandonment_policy = policy_t;
    using frame = Frame;

    // The row-hash shape of a discipline carrier.  The identity names the
    // original protocol at the head, its loop context and the checkpoint
    // state, and the inner handle over the erased protocol folds its own
    // claim.
    using row_discipline = ::fixy::row_discipline::checkpoint_handle<Head, HeadLoop, Frame>;
    using row_payload = Inner;

    constexpr CheckpointHandle(CheckpointHandle&&) noexcept = default;
    constexpr CheckpointHandle& operator=(CheckpointHandle&&) noexcept = default;
    CheckpointHandle(const CheckpointHandle&) = delete("a checkpoint handle is linear, like the handle it wraps");
    CheckpointHandle&
    operator=(const CheckpointHandle&) = delete("a checkpoint handle is linear, like the handle it wraps");
    ~CheckpointHandle() = default;

    template <typename Transport, typename P = Head>
        requires is_send_v<P> && (!is_keyed_step_v<P>)
              && WriteTransport<Transport, resource_t, typename P::message_type>
    [[nodiscard]] constexpr auto send(detail::sent_value_t<typename P::message_type> value, Transport transport) && {
        return CheckpointDoor::send(std::move(*this), std::move(value), std::move(transport));
    }

    template <typename Transport, typename P = Head>
        requires is_recv_v<P> && (!is_keyed_step_v<P>) && ReadTransport<Transport, resource_t, typename P::message_type>
    [[nodiscard]] constexpr auto recv(Transport transport) && {
        return CheckpointDoor::recv(std::move(*this), std::move(transport));
    }

    template <typename Transport, typename P = Head>
        requires is_send_v<P> && is_keyed_step_v<P> && WriteTransport<Transport, resource_t, std::size_t>
    [[nodiscard]] constexpr auto send(Transport transport) && {
        return CheckpointDoor::send(std::move(*this), std::move(transport));
    }

    template <typename Transport, typename P = Head>
        requires is_recv_v<P> && is_keyed_step_v<P> && ReadTransport<Transport, resource_t, std::size_t>
    [[nodiscard]] constexpr auto recv(Transport transport) && {
        return CheckpointDoor::recv(std::move(*this), std::move(transport));
    }

    template <std::size_t I, typename Transport, typename P = Head>
        requires is_select_v<P> && WriteTransport<Transport, resource_t, std::size_t>
    [[nodiscard]] constexpr auto select(Transport transport) && {
        return CheckpointDoor::template select<I>(std::move(*this), std::move(transport));
    }

    template <typename Transport, typename Handler, typename P = Head>
        requires is_offer_v<P> && ReadTransport<Transport, resource_t, std::size_t>
    constexpr auto branch(Transport transport, Handler handler) && {
        return CheckpointDoor::branch(std::move(*this), std::move(transport), std::move(handler));
    }

    template <typename P = Head>
        requires std::is_same_v<P, End>
    [[nodiscard]] constexpr resource_t close() && {
        return std::move(inner_).close();
    }

    template <typename Reason>
        requires DetachReason<Reason>
    constexpr void detach(Reason reason) && noexcept {
        std::move(inner_).detach(reason);
    }

    [[nodiscard]] constexpr resource_t& resource() & noexcept { return inner_.resource(); }
    [[nodiscard]] constexpr const resource_t& resource() const& noexcept { return inner_.resource(); }
};

// ── The mint ─────────────────────────────────────────────────────────
//
// Mints one endpoint.  The other endpoint's protocol is a template
// argument, so the pair is checked without a mint that holds both ends.
// WorkBound is the bound of the work of the check of the pair.  A pair
// whose check needs more work names a larger bound here.

template <typename Proto, typename PeerProto, AbandonmentPolicy Policy = DefaultAbandonmentPolicy,
          std::size_t WorkBound = checkpoint_work_bound, typename Ctx, typename Resource>
    requires CtxFitsCheckpointSession<Ctx, Proto, PeerProto, Resource, WorkBound>
[[nodiscard]] constexpr auto
mint_checkpoint_session(Ctx const& ctx, Resource resource,
                        std::source_location loc = std::source_location::current()) noexcept {
    return CheckpointDoor::open<Proto, PeerProto, Policy, WorkBound>(ctx, std::forward<Resource>(resource), loc);
}

}  // namespace fixy::session

// ── Armed cells ──────────────────────────────────────────────────────

namespace fixy::session::detail::checkpoint::armed_witness {
using Decider = Select<Commit<Send<int, End>>, Roll>;
using Follower = Offer<Commit<Recv<int, End>>, Roll>;
using Swapped = Offer<Roll, Commit<Recv<int, End>>>;
}  // namespace fixy::session::detail::checkpoint::armed_witness

template <>
struct foundation::contracts::armed_cell<::fixy::session::is_checkpoint_primitive> {
    using accepts =
        witnesses<::fixy::session::Commit<::fixy::session::End>, ::fixy::session::Roll, ::fixy::session::Abort>;
    using refuses = witnesses<::fixy::session::End, ::fixy::session::Select<::fixy::session::Roll>>;
};

template <>
struct foundation::contracts::armed_cell<::fixy::session::is_checkpoint_compliant> {
    using accepts =
        witnesses<::fixy::session::CheckpointPair<::fixy::session::detail::checkpoint::armed_witness::Decider,
                                                  ::fixy::session::detail::checkpoint::armed_witness::Follower>,
                  ::fixy::session::CheckpointPair<::fixy::session::End, ::fixy::session::End>>;
    using refuses =
        witnesses<int,
                  ::fixy::session::CheckpointPair<::fixy::session::detail::checkpoint::armed_witness::Decider,
                                                  ::fixy::session::detail::checkpoint::armed_witness::Swapped>,
                  ::fixy::session::CheckpointPair<::fixy::session::Send<int, ::fixy::session::Roll>,
                                                  ::fixy::session::Recv<int, ::fixy::session::Roll>>>;
};

// The two traits are aliases, and the roster walk of
// foundation/contracts/ArmedRoster.h finds class templates only.  Two
// assertions in the check file of this header read the two cells.
