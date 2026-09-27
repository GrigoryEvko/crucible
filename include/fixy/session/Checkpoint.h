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
// would decide alone, which is the defect of the ported source below, so
// the checkpoint mint refuses it.
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
// payload that is not plain in the sense of fixy/session/Payload.h.
// A crash branch cannot appear either, because no paper types the two
// together.  The mint refuses a Crash payload.
//
// ── What the ported source did wrong ────────────────────────────────
//
// crucible/sessions/_SessionCheckpoint.h had CheckpointedSession<Base,
// Rollback>, a choice that each endpoint made alone, and a dual that
// mirrored the choice onto the peer as a second local choice.  The two
// endpoints could take different branches, and nothing on the wire told
// either one what the other did.

#include <fixy/session/Crash.h>
#include <fixy/session/Handle.h>
#include <fixy/session/Payload.h>

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
// Commit, Roll and Abort and their registrations stand in
// fixy/session/Protocol.h, under the seal of the registry.  A checkpoint
// protocol cannot run on one endpoint, because compliance is a property
// of the pair, so each primitive is registered as not plain: the plain
// handle factory refuses a protocol that holds one, and only
// mint_checkpoint_session admits it.  Commit is a marker whose dual
// commits at the same label.  Roll and Abort are terminals that keep
// their place under composition, as a crashed endpoint does.  A dual pair
// is not compliant by that fact alone, because the imposed flag can still
// make a Roll fail, so the mint checks the pair.

// Each trait of this header is an alias and each _v form is a concept, so
// no program can specialize one to change its answer.

namespace detail::checkpoint {

// A primitive at the head of P, with no wrapper around it.
[[nodiscard]] consteval bool is_primitive_type(std::meta::info type) {
    const std::meta::info shape = ::foundation::algebra::transition::shape_of(type);
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

template <typename P>
using checkpoint_erase_t = typename[:detail::erased(^^P, detail::erasure::checkpoints):];

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

// Continue and Loop resolved, as the handle resolves them: the head of
// R and the loop context at that head.
template <typename R, typename LoopCtx>
struct resolve {
    using head = R;
    using loop = LoopCtx;
};
template <typename LoopCtx>
struct resolve<Continue, LoopCtx> : resolve<typename LoopCtx::body, LoopCtx> {};
template <typename B, typename LoopCtx>
struct resolve<Loop<B>, LoopCtx> : resolve<B, Loop<B>> {};

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
    // The model has more configurations than the bound admits.
    TooManyConfigurations,
};

namespace detail::checkpoint {

using info = std::meta::info;

enum class Kind : std::uint8_t { End, Continue, Send, Recv, Select, Offer, Loop, Commit, Roll, Abort, Unknown };

inline constexpr std::size_t configuration_bound = 4096;
inline constexpr int unfold_bound = 64;
inline constexpr int depth_bound = 256;

consteval info strip(info type) {
    type = std::meta::dealias(type);
    while (std::meta::has_template_arguments(type) && std::meta::template_of(type) == ^^VendorPinned) {
        type = std::meta::dealias(std::meta::template_arguments_of(type)[1]);
    }
    return type;
}

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

// The real branches of a Select or an Offer.  A Sender tag is not a
// branch.
consteval std::vector<info> branches_of(info type) {
    std::vector<info> branches = std::meta::template_arguments_of(strip(type));
    if (!branches.empty()) {
        const info first = std::meta::dealias(branches.front());
        if (std::meta::has_template_arguments(first) && std::meta::template_of(first) == ^^Sender) {
            branches.erase(branches.begin());
        }
    }
    return branches;
}

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

// Complexity: linear in the size of the protocol tree.
consteval bool shaped(info type, bool at_branch, int depth) {
    if (depth > depth_bound) return false;
    switch (kind_of(type)) {
        case Kind::End:
        case Kind::Continue:
            return true;
        case Kind::Send:
        case Kind::Recv:
            return payload_admitted(argument(type, 0)) && shaped(argument(type, 1), false, depth + 1);
        case Kind::Select:
        case Kind::Offer: {
            const std::vector<info> branches = branches_of(type);
            if (branches.empty()) return false;
            for (const info branch : branches) {
                if (!shaped(branch, true, depth + 1)) return false;
            }
            return true;
        }
        case Kind::Loop:
            return shaped(argument(type, 0), false, depth + 1);
        case Kind::Commit:
            return at_branch && shaped(argument(type, 0), false, depth + 1);
        case Kind::Roll:
        case Kind::Abort:
            return at_branch;
        case Kind::Unknown:
            return false;
        default:
            return false;
    }
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

struct Exploration {
    std::vector<Configuration> seen;
    std::vector<Configuration> pending;
    CheckpointVerdict verdict = CheckpointVerdict::Compliant;
};

consteval void admit(Exploration& run, Configuration next) {
    if (!unfold(next.left) || !unfold(next.right)) {
        run.verdict = CheckpointVerdict::LoopUnresolved;
        return;
    }
    for (const Configuration& known : run.seen) {
        if (same_configuration(known, next)) return;
    }
    if (run.seen.size() >= configuration_bound) {
        run.verdict = CheckpointVerdict::TooManyConfigurations;
        return;
    }
    run.seen.push_back(next);
    run.pending.push_back(next);
}

// One label exchange.  `active` selected label `index`, and `passive`
// received it.
consteval CheckpointVerdict exchange(Exploration& run, Party active, Party passive, info active_branch,
                                     info passive_branch, const Party& active_start, const Party& passive_start,
                                     bool active_is_left) {
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
    const std::vector<info> labels = branches_of(active.position);
    const std::vector<info> accepted = branches_of(passive.position);
    if (labels.size() > accepted.size()) return CheckpointVerdict::LabelOutOfRange;
    for (std::size_t index = 0; index < labels.size(); ++index) {
        const CheckpointVerdict step = exchange(run, active, passive, labels[index], accepted[index], active_start,
                                                passive_start, active_is_left);
        if (step != CheckpointVerdict::Compliant) return step;
    }
    return CheckpointVerdict::Compliant;
}

// Complexity: the number of reachable configurations times the cost of
// the visited-set lookup, which is linear, so quadratic in the number of
// configurations.  The bound caps it.
consteval CheckpointVerdict verdict_of(info left_protocol, info right_protocol) {
    if (!shaped(left_protocol, false, 0) || !shaped(right_protocol, false, 0)) {
        return CheckpointVerdict::NotCheckpointShaped;
    }
    const Party left_start = initial_party(left_protocol);
    const Party right_start = initial_party(right_protocol);
    Exploration run;
    admit(run, Configuration{left_start, right_start});
    while (run.verdict == CheckpointVerdict::Compliant && !run.pending.empty()) {
        const Configuration here = run.pending.back();
        run.pending.pop_back();
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

// The verdict that the gate reads.  It is computed once for each pair.
template <typename P1, typename P2>
inline constexpr CheckpointVerdict verdict_v = verdict_of(^^P1, ^^P2);

// True when the question is a CheckpointPair whose verdict is Compliant.
// Any other type answers false.
[[nodiscard]] consteval bool pair_is_compliant(info question);

}  // namespace detail::checkpoint

// The verdict for a pair of checkpoint protocols, one for each endpoint.
// The gate reads the walk and not this spelling, so a specialization of it
// changes only what its author reads.
template <typename P1, typename P2>
inline constexpr CheckpointVerdict checkpoint_verdict_v = detail::checkpoint::verdict_of(^^P1, ^^P2);

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
    return verdict_of(sides[0], sides[1]) == CheckpointVerdict::Compliant;
}

template <typename Q>
using is_checkpoint_compliant = std::bool_constant<detail::checkpoint::pair_is_compliant(^^Q)>;

template <typename P1, typename P2>
concept checkpoint_compliant_v = detail::checkpoint::verdict_v<P1, P2> == CheckpointVerdict::Compliant;

// Whether Proto can run on one endpoint of a checkpoint session whose
// other endpoint runs PeerProto.  Each refusal is its own atomic
// constraint, so the diagnostic names the verdict.  The last verdict
// clause asks for Compliant, so a verdict added later and not listed
// here still refuses.
template <typename Proto, typename PeerProto>
concept CheckpointSessionAdmissible =
    detail::checkpoint::verdict_v<Proto, PeerProto> != CheckpointVerdict::NotCheckpointShaped
    && detail::checkpoint::verdict_v<Proto, PeerProto> != CheckpointVerdict::Stuck
    && detail::checkpoint::verdict_v<Proto, PeerProto> != CheckpointVerdict::LabelOutOfRange
    && detail::checkpoint::verdict_v<Proto, PeerProto> != CheckpointVerdict::LabelsDisagree
    && detail::checkpoint::verdict_v<Proto, PeerProto> != CheckpointVerdict::RollToImposedCheckpoint
    && detail::checkpoint::verdict_v<Proto, PeerProto> != CheckpointVerdict::LoopUnresolved
    && detail::checkpoint::verdict_v<Proto, PeerProto> != CheckpointVerdict::TooManyConfigurations
    && checkpoint_compliant_v<Proto, PeerProto> && WellFormedRunnableProtocol<checkpoint_erase_t<Proto>>
    && WellFormedRunnableProtocol<checkpoint_erase_t<PeerProto>>;

// The whole gate of mint_checkpoint_session: a compliant pair, a Resource
// that a mint admits, and a context that admits the effect row of the
// protocol that the plain handle runs (CtxAdmitsProtocolRow of
// fixy/session/Handle.h).  The erasure keeps each payload, so that row is
// the row of Proto.
template <typename Ctx, typename Proto, typename PeerProto, typename Resource>
concept CtxFitsCheckpointSession = CheckpointSessionAdmissible<Proto, PeerProto> && SessionResource<Resource>
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
class CheckpointDoor final {
    CheckpointDoor() = delete("the checkpoint door holds static members only, and no object of it exists");
    CheckpointDoor(const CheckpointDoor&) = delete("the checkpoint door holds static members only");
    CheckpointDoor& operator=(const CheckpointDoor&) = delete("the checkpoint door holds static members only");
    CheckpointDoor(CheckpointDoor&&) = delete("the checkpoint door holds static members only");
    CheckpointDoor& operator=(CheckpointDoor&&) = delete("the checkpoint door holds static members only");
    constexpr ~CheckpointDoor() noexcept {}

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

    // The effect of branch B, given the plain handle it reached.
    template <typename Branch, typename HeadLoop, typename Frame, typename Next>
    [[nodiscard]] static constexpr auto take_branch_(Next next) noexcept {
        if constexpr (std::is_same_v<Branch, Roll>) {
            using Saved = typename Frame::saved;
            using SavedLoop = typename Frame::saved_loop;
            return wrap_<Saved, SavedLoop, Frame>(rebuild_<Saved, SavedLoop>(std::move(next)));
        } else if constexpr (std::is_same_v<Branch, Abort>) {
            using Initial = typename Frame::initial;
            using Restart = CheckpointFrame<Initial, Initial, void>;
            return wrap_<Initial, void, Restart>(rebuild_<Initial, void>(std::move(next)));
        } else if constexpr (is_checkpoint_primitive<Branch>::value) {
            using K = typename Branch::next;
            using Saved = CheckpointFrame<typename Frame::initial, K, HeadLoop>;
            return wrap_<K, HeadLoop, Saved>(std::move(next));
        } else {
            return wrap_<Branch, HeadLoop, Frame>(std::move(next));
        }
    }

    // The plain handle cannot say which label it took when two branches
    // erase to one type, so the branch that the wire word names decides.
    template <typename Branches, typename Inner, typename HeadLoop, typename Frame, typename Next, typename Handler,
              std::size_t... Is>
    static constexpr auto dispatch_(std::size_t label, Next next, Handler& handler, std::index_sequence<Is...>) {
        using First = std::tuple_element_t<0, Branches>;
        using Result = std::invoke_result_t<
            Handler&, decltype(take_branch_<First, HeadLoop, Frame>(
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
            if (!is_dispatched) [[unlikely]]
                std::abort();
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
            if (!is_dispatched) [[unlikely]]
                std::abort();
            return std::move(*result);
        }
    }

public:
    // mint_checkpoint_session: opens the plain handle of the erased
    // protocol, and puts around it the checkpoint handle at the start of
    // Proto.  The first checkpoint of the session is the start.
    template <typename Proto, typename PeerProto, AbandonmentPolicy Policy, typename Ctx, typename Resource>
        requires CtxFitsCheckpointSession<Ctx, Proto, PeerProto, Resource>
    [[nodiscard]] static constexpr auto open(Ctx const&, Resource resource, std::source_location loc) noexcept {
        using Start = detail::checkpoint::resolve<Proto, void>;
        auto inner =
            mint_session_handle<checkpoint_erase_t<Proto>, Resource, Policy>(std::forward<Resource>(resource), loc);
        return CheckpointHandle<decltype(inner), typename Start::head, typename Start::loop,
                                CheckpointFrame<Proto, Proto, void>>{std::move(inner)};
    }

    template <typename Inner, typename Head, typename HeadLoop, typename Frame, typename Transport>
        requires is_send_v<Head> && (!is_keyed_step_v<Head>)
              && WriteTransport<Transport, typename Inner::resource_type, typename Head::message_type>
    [[nodiscard]] static constexpr auto send(CheckpointHandle<Inner, Head, HeadLoop, Frame>&& handle,
                                             typename Head::message_type value, Transport transport) {
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
    // (fixy/session/Handle.h).  The step moves the word, and the handle
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
    // (branch_landing_t in fixy/session/Handle.h).
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
    // (branch_of_wire_word in fixy/session/Handle.h).  The erasure keeps
    // the count and the order of the branches.
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
    CheckpointHandle& operator=(const CheckpointHandle&) =
        delete("a checkpoint handle is linear, like the handle it wraps");
    ~CheckpointHandle() = default;

    template <typename Transport, typename P = Head>
        requires is_send_v<P> && (!is_keyed_step_v<P>)
              && WriteTransport<Transport, resource_t, typename P::message_type>
    [[nodiscard]] constexpr auto send(typename P::message_type value, Transport transport) && {
        return CheckpointDoor::send(std::move(*this), std::move(value), std::move(transport));
    }

    template <typename Transport, typename P = Head>
        requires is_recv_v<P> && (!is_keyed_step_v<P>)
              && ReadTransport<Transport, resource_t, typename P::message_type>
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

template <typename Proto, typename PeerProto, AbandonmentPolicy Policy = DefaultAbandonmentPolicy, typename Ctx,
          typename Resource>
    requires CtxFitsCheckpointSession<Ctx, Proto, PeerProto, Resource>
[[nodiscard]] constexpr auto mint_checkpoint_session(Ctx const& ctx, Resource resource,
                                                     std::source_location loc = std::source_location::current()) noexcept {
    return CheckpointDoor::open<Proto, PeerProto, Policy>(ctx, std::forward<Resource>(resource), loc);
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
    using accepts = witnesses<::fixy::session::Commit<::fixy::session::End>, ::fixy::session::Roll,
                              ::fixy::session::Abort>;
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
// foundation/contracts/Armed.h finds class templates only.  These two
// assertions read the two cells.
static_assert(::foundation::contracts::armed_cell_holds_v<::fixy::session::is_checkpoint_primitive>);
static_assert(::foundation::contracts::armed_cell_holds_v<::fixy::session::is_checkpoint_compliant>);
