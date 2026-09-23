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
// crucible/sessions/SessionCheckpoint.h had CheckpointedSession<Base,
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

namespace fixy::session {

// ── The three primitives ────────────────────────────────────────────

// Take a checkpoint, then continue with K.
template <typename K>
struct Commit {
    using next = K;
};

// Return both parties to their checkpoints.
struct Roll {};

// Return both parties to the start of the session.
struct Abort {};

// A checkpoint protocol cannot run on one endpoint, because compliance
// is a property of the pair.  The plain handle factory therefore sees
// these three as ill-formed, and only mint_checkpoint_session admits
// them.
template <typename K, typename LoopCtx>
struct is_well_formed<Commit<K>, LoopCtx> : std::false_type {};
template <typename LoopCtx>
struct is_well_formed<Roll, LoopCtx> : std::false_type {};
template <typename LoopCtx>
struct is_well_formed<Abort, LoopCtx> : std::false_type {};

// The dual mirrors the label.  A Select that picks Commit faces an Offer
// that receives it, so the dual is well defined.  A dual pair is not
// compliant by that fact alone, because the imposed flag can still make
// a Roll fail.  The mint checks the pair.
template <typename K>
struct dual_of<Commit<K>> {
    using type = Commit<dual_of_t<K>>;
};
template <>
struct dual_of<Roll> {
    using type = Roll;
};
template <>
struct dual_of<Abort> {
    using type = Abort;
};

template <typename K>
struct is_dual_involutive<Commit<K>> : is_dual_involutive<K> {};
template <>
struct is_dual_involutive<Roll> : std::true_type {};
template <>
struct is_dual_involutive<Abort> : std::true_type {};

// A Roll or an Abort never falls through to what follows it, so
// composition keeps it, as it keeps a crashed endpoint.
template <typename K, typename Q>
struct compose<Commit<K>, Q> {
    using type = Commit<compose_t<K, Q>>;
};
template <typename Q>
struct compose<Roll, Q> {
    using type = Roll;
};
template <typename Q>
struct compose<Abort, Q> {
    using type = Abort;
};

template <typename K>
struct is_empty_choice<Commit<K>> : is_empty_choice<K> {};
template <>
struct is_empty_choice<Roll> : std::false_type {};
template <>
struct is_empty_choice<Abort> : std::false_type {};

template <typename K>
struct is_terminal_state<Commit<K>> : std::false_type {};
template <>
struct is_terminal_state<Roll> : std::false_type {};
template <>
struct is_terminal_state<Abort> : std::false_type {};

template <typename P>
struct is_checkpoint_primitive : std::false_type {};
template <typename K>
struct is_checkpoint_primitive<Commit<K>> : std::true_type {};
template <>
struct is_checkpoint_primitive<Roll> : std::true_type {};
template <>
struct is_checkpoint_primitive<Abort> : std::true_type {};

// ── Erasure to the plain protocol ───────────────────────────────────
//
// A checkpoint session runs over a plain handle.  The handle steps the
// erased protocol, in which Commit<K> is K and Roll and Abort are End.
// The branch count of every choice is unchanged, so a label index means
// the same branch in both protocols.  An unknown combinator has no
// erasure, so it stops the build.

namespace detail::checkpoint {

template <typename P>
struct erase;

template <typename P>
using erase_t = typename erase<P>::type;

template <>
struct erase<End> {
    using type = End;
};
template <>
struct erase<Continue> {
    using type = Continue;
};
template <typename T, typename K>
struct erase<Send<T, K>> {
    using type = Send<T, erase_t<K>>;
};
template <typename T, typename K>
struct erase<Recv<T, K>> {
    using type = Recv<T, erase_t<K>>;
};
template <typename... Bs>
struct erase<Select<Bs...>> {
    using type = Select<erase_t<Bs>...>;
};
template <typename... Bs>
struct erase<Offer<Bs...>> {
    using type = Offer<erase_t<Bs>...>;
};
template <typename Role, typename... Bs>
struct erase<Offer<Sender<Role>, Bs...>> {
    using type = Offer<Sender<Role>, erase_t<Bs>...>;
};
template <typename B>
struct erase<Loop<B>> {
    using type = Loop<erase_t<B>>;
};
template <typename K>
struct erase<Commit<K>> {
    using type = erase_t<K>;
};
template <>
struct erase<Roll> {
    using type = End;
};
template <>
struct erase<Abort> {
    using type = End;
};

template <typename LoopCtx>
struct erase_loop {
    using type = erase_t<LoopCtx>;
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

template <typename P>
using checkpoint_erase_t = detail::checkpoint::erase_t<P>;

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
// holds, so a checkpoint session carries no delegation either.
consteval bool payload_admitted(info payload) {
    return std::meta::extract<bool>(std::meta::substitute(^^is_plain_payload_v, {payload}))
        && !std::meta::extract<bool>(std::meta::substitute(^^is_crash_payload_v, {payload}))
        && !std::meta::extract<bool>(std::meta::substitute(^^::fixy::session::detail::crash::carries_delegation_v, {payload}));
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

}  // namespace detail::checkpoint

// The verdict for a pair of checkpoint protocols, one for each endpoint.
template <typename P1, typename P2>
inline constexpr CheckpointVerdict checkpoint_verdict_v = detail::checkpoint::verdict_of(^^P1, ^^P2);

// The question "are these two protocols compliant", as a one-argument
// trait that can hold an armed cell.
template <typename P1, typename P2>
struct CheckpointPair {};

template <typename Q>
struct is_checkpoint_compliant : std::false_type {};
template <typename P1, typename P2>
struct is_checkpoint_compliant<CheckpointPair<P1, P2>>
    : std::bool_constant<checkpoint_verdict_v<P1, P2> == CheckpointVerdict::Compliant> {};

template <typename P1, typename P2>
inline constexpr bool checkpoint_compliant_v = is_checkpoint_compliant<CheckpointPair<P1, P2>>::value;

// Whether Proto can run on one endpoint of a checkpoint session whose
// other endpoint runs PeerProto.  Each refusal is its own atomic
// constraint, so the diagnostic names the verdict.  The last verdict
// clause asks for Compliant, so a verdict added later and not listed
// here still refuses.
template <typename Proto, typename PeerProto>
concept CheckpointSessionAdmissible =
    checkpoint_verdict_v<Proto, PeerProto> != CheckpointVerdict::NotCheckpointShaped
    && checkpoint_verdict_v<Proto, PeerProto> != CheckpointVerdict::Stuck
    && checkpoint_verdict_v<Proto, PeerProto> != CheckpointVerdict::LabelOutOfRange
    && checkpoint_verdict_v<Proto, PeerProto> != CheckpointVerdict::LabelsDisagree
    && checkpoint_verdict_v<Proto, PeerProto> != CheckpointVerdict::RollToImposedCheckpoint
    && checkpoint_verdict_v<Proto, PeerProto> != CheckpointVerdict::LoopUnresolved
    && checkpoint_verdict_v<Proto, PeerProto> != CheckpointVerdict::TooManyConfigurations
    && checkpoint_compliant_v<Proto, PeerProto> && WellFormedRunnableProtocol<checkpoint_erase_t<Proto>>
    && WellFormedRunnableProtocol<checkpoint_erase_t<PeerProto>>;

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

namespace detail::checkpoint {

// Builds the plain handle at protocol R, with the loop context of the
// position.  The handle's builder takes the permission set as a fifth
// argument where the handle carries one, and a checkpoint session moves
// no permission, so the set is empty.
template <typename R, typename Resource, typename LoopCtx, typename Policy>
[[nodiscard]] constexpr auto step_plain(Resource resource) noexcept {
    using ::foundation::permissions::EmptyPermSet;
    if constexpr (requires { ::fixy::session::detail::step_to_next<R, Resource, LoopCtx, Policy, EmptyPermSet>; }) {
        return ::fixy::session::detail::step_to_next<R, Resource, LoopCtx, Policy, EmptyPermSet>(
            std::forward<Resource>(resource));
    } else {
        return ::fixy::session::detail::step_to_next<R, Resource, LoopCtx, Policy>(std::forward<Resource>(resource));
    }
}

template <typename Head, typename HeadLoop, typename Frame, typename Inner>
[[nodiscard]] constexpr auto make_checkpoint_handle(Inner inner) noexcept
    -> CheckpointHandle<Inner, Head, HeadLoop, Frame>;

}  // namespace detail::checkpoint

// A plain handle over the erased protocol, with the original protocol
// and the checkpoint state in the type.  Commit, Roll and Abort take
// effect in the same call that exchanges their label, so no handle is
// ever positioned at one of them.
template <typename Inner, typename Head, typename HeadLoop, typename Frame>
class [[nodiscard]] CheckpointHandle {
    Inner inner_;

    template <typename, typename, typename, typename>
    friend class CheckpointHandle;

    template <typename FHead, typename FLoop, typename FFrame, typename FInner>
    friend constexpr auto detail::checkpoint::make_checkpoint_handle(FInner) noexcept
        -> CheckpointHandle<FInner, FHead, FLoop, FFrame>;

    constexpr explicit CheckpointHandle(Inner inner) noexcept : inner_{std::move(inner)} {}

    using resource_t = typename Inner::resource_type;
    using policy_t = typename Inner::abandonment_policy;

    template <typename R, typename Loop, typename NewFrame, typename Next>
    [[nodiscard]] static constexpr auto wrap_(Next next) noexcept {
        using Resolved = detail::checkpoint::resolve<R, Loop>;
        return CheckpointHandle<Next, typename Resolved::head, typename Resolved::loop, NewFrame>{std::move(next)};
    }

    // The plain handle that a branch reaches: Commit<K> reaches K, and
    // Roll and Abort reach End.
    template <typename Branch>
    using inner_branch_t = decltype(detail::checkpoint::step_plain<checkpoint_erase_t<Branch>, resource_t,
                                                                   detail::checkpoint::erase_loop_t<HeadLoop>, policy_t>(
        std::declval<resource_t>()));

    // Rebuilds the plain handle at the checkpoint, or at the start.
    template <typename Saved, typename SavedLoop>
    [[nodiscard]] static constexpr auto rebuild_(resource_t resource) noexcept {
        return detail::checkpoint::step_plain<checkpoint_erase_t<Saved>, resource_t,
                                              detail::checkpoint::erase_loop_t<SavedLoop>, policy_t>(
            std::forward<resource_t>(resource));
    }

    // The effect of branch B, given the plain handle it reached.
    template <typename Branch, typename Next>
    [[nodiscard]] static constexpr auto take_branch_(Next next) noexcept {
        if constexpr (std::is_same_v<Branch, Roll>) {
            using Saved = typename Frame::saved;
            using SavedLoop = typename Frame::saved_loop;
            return wrap_<Saved, SavedLoop, Frame>(rebuild_<Saved, SavedLoop>(std::move(next).close()));
        } else if constexpr (std::is_same_v<Branch, Abort>) {
            using Initial = typename Frame::initial;
            using Restart = CheckpointFrame<Initial, Initial, void>;
            return wrap_<Initial, void, Restart>(rebuild_<Initial, void>(std::move(next).close()));
        } else if constexpr (is_checkpoint_primitive<Branch>::value) {
            using K = typename Branch::next;
            using Saved = CheckpointFrame<typename Frame::initial, K, HeadLoop>;
            return wrap_<K, HeadLoop, Saved>(std::move(next));
        } else {
            return wrap_<Branch, HeadLoop, Frame>(std::move(next));
        }
    }

public:
    using protocol = Head;
    using inner_type = Inner;
    using resource_type = resource_t;
    using abandonment_policy = policy_t;
    using frame = Frame;

    constexpr CheckpointHandle(CheckpointHandle&&) noexcept = default;
    constexpr CheckpointHandle& operator=(CheckpointHandle&&) noexcept = default;
    CheckpointHandle(const CheckpointHandle&) = delete("a checkpoint handle is linear, like the handle it wraps");
    CheckpointHandle& operator=(const CheckpointHandle&) =
        delete("a checkpoint handle is linear, like the handle it wraps");
    ~CheckpointHandle() = default;

    template <typename Transport, typename P = Head>
        requires is_send_v<P> && std::is_invocable_v<Transport, resource_t&, typename P::message_type&&>
    [[nodiscard]] constexpr auto send(typename P::message_type value, Transport transport) && {
        return wrap_<typename P::next, HeadLoop, Frame>(std::move(inner_).send(std::move(value), std::move(transport)));
    }

    template <typename Transport, typename P = Head>
        requires is_recv_v<P> && std::is_invocable_r_v<typename P::message_type, Transport, resource_t&>
    [[nodiscard]] constexpr auto recv(Transport transport) && {
        auto [value, next] = std::move(inner_).recv(std::move(transport));
        return std::pair{std::move(value), wrap_<typename P::next, HeadLoop, Frame>(std::move(next))};
    }

    // Selects label I and tells the peer.  A Commit, Roll or Abort takes
    // effect here, on this side, in the same call.
    template <std::size_t I, typename Transport, typename P = Head>
        requires is_select_v<P> && std::is_invocable_v<Transport, resource_t&, std::size_t>
    [[nodiscard]] constexpr auto select(Transport transport) && {
        using Branch = std::tuple_element_t<I, typename P::branches_tuple>;
        return take_branch_<Branch>(std::move(inner_).template select<I>(std::move(transport)));
    }

    // Receives the peer's label and calls the handler with the handle for
    // that branch.  A Commit, Roll or Abort takes effect before the call.
    // Every branch must give the handler the same return type.
    template <typename Transport, typename Handler, typename P = Head>
        requires is_offer_v<P> && std::is_invocable_r_v<std::size_t, Transport, resource_t&>
    constexpr auto branch(Transport transport, Handler handler) && {
        using Branches = typename P::branches_tuple;
        constexpr std::size_t count = std::tuple_size_v<Branches>;
        std::size_t label = count;
        auto read_label = [&transport, &label](resource_t& resource) -> std::size_t {
            label = std::invoke(transport, resource);
            return label;
        };
        return std::move(inner_).branch(read_label, [&handler, &label](auto next) {
            return dispatch_<Branches>(label, std::move(next), handler, std::make_index_sequence<count>{});
        });
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

private:
    // The plain handle cannot say which label it took when two branches
    // erase to one type, so the label read on the wire decides.
    template <typename Branches, typename Next, typename Handler, std::size_t... Is>
    static constexpr auto dispatch_(std::size_t label, Next next, Handler& handler, std::index_sequence<Is...>) {
        using First = std::tuple_element_t<0, Branches>;
        using Result = std::invoke_result_t<Handler&, decltype(take_branch_<First>(
                                                          std::declval<inner_branch_t<First>>()))>;
        bool is_dispatched = false;
        if constexpr (std::is_void_v<Result>) {
            (
                [&] {
                    using Branch = std::tuple_element_t<Is, Branches>;
                    if constexpr (std::is_same_v<inner_branch_t<Branch>, Next>) {
                        if (!is_dispatched && label == Is) {
                            is_dispatched = true;
                            std::invoke(handler, take_branch_<Branch>(std::move(next)));
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
                    if constexpr (std::is_same_v<inner_branch_t<Branch>, Next>) {
                        if (!is_dispatched && label == Is) {
                            is_dispatched = true;
                            result.emplace(std::invoke(handler, take_branch_<Branch>(std::move(next))));
                        }
                    }
                }(),
                ...);
            if (!is_dispatched) [[unlikely]]
                std::abort();
            return std::move(*result);
        }
    }
};

namespace detail::checkpoint {

template <typename Head, typename HeadLoop, typename Frame, typename Inner>
[[nodiscard]] constexpr auto make_checkpoint_handle(Inner inner) noexcept
    -> CheckpointHandle<Inner, Head, HeadLoop, Frame> {
    return CheckpointHandle<Inner, Head, HeadLoop, Frame>{std::move(inner)};
}

}  // namespace detail::checkpoint

// ── The mint ─────────────────────────────────────────────────────────
//
// Mints one endpoint.  The other endpoint's protocol is a template
// argument, so the pair is checked without a mint that holds both ends.

template <typename Proto, typename PeerProto, AbandonmentPolicy Policy = DefaultAbandonmentPolicy, typename Resource>
    requires CheckpointSessionAdmissible<Proto, PeerProto> && SessionResource<Resource>
[[nodiscard]] constexpr auto mint_checkpoint_session(Resource resource,
                                                     std::source_location loc = std::source_location::current()) noexcept {
    using Start = detail::checkpoint::resolve<Proto, void>;
    return detail::checkpoint::make_checkpoint_handle<typename Start::head, typename Start::loop,
                                                      CheckpointFrame<Proto, Proto, void>>(
        mint_session_handle<checkpoint_erase_t<Proto>, Resource, Policy>(std::forward<Resource>(resource), loc));
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
