#pragma once

// The Stepping modality's wrapper contract, and the abandonment-check
// policy that decides what a session handle costs at runtime.
//
// foundation/algebra/Modality.h reserves ModalityKind::Stepping for
// session handles and says why they do not instantiate Graded: the
// grade is a protocol remainder, every operation advances it, so a
// handle at one grade produces a handle at the next grade rather than
// a handle at the same grade over a new value.  Graded's operations
// are the wrong shape for that.  The note ends by naming the contract
// such a handle satisfies instead, a SteppingGraded concept.  This
// header states that concept, so the claim "satisfies the Stepping
// contract" has a check.
//
// ── The abandonment policy ───────────────────────────────────────────
//
// If a program destroys a handle at a non-terminal protocol state before
// it consumes the handle, the protocol is dropped.  The peer then waits
// for a message that does not come.  LinearActris (Jacobs, Hinrichsen and
// Krebbers, POPL 2024) gets deadlock freedom from linear endpoints, so a
// silent drop removes the guarantee.
//
// The policy is a template parameter of the handle.  It tells the
// destructor what to do with a dropped protocol:
//
//   - check::Enforced prints the construction site and aborts.
//   - check::Cancel sends a cancellation to the peer through the
//     Resource, and then the program continues.  This is the affine
//     design of Lagaillardie, Neykova and Yoshida (ECOOP 2022).  A handle
//     under it is linear too (SessionHandleBase of fixy/session/Handle.h),
//     because a destructor that sends a message is a hidden effect.  So a
//     path that ends a session early calls cancel(), and the cancellation of
//     the destructor is the fallback of a path that no rule reads.
//   - check::Off does nothing.  It is an explicit hatch, and a handle
//     under it does not keep the guarantee above.
//   - check::Pinned and check::PinnedCancel act as Enforced and Cancel,
//     with no holder check at a step or a move.  A mint under them, a bind
//     and a pin of fixy/session/Handle.h build such a handle on the calling
//     thread, and the type keeps that handle on that thread.
//
// The default is check::Enforced in every build mode, and NDEBUG does not
// change it.  check::Cancel is not the default, because it needs a
// Resource that can carry a cancellation, and most Resources cannot.
// check::Off adds no instruction to a step.  A checking policy adds the
// claim and release of a record in fixy/session/Watch.h at mint and at
// End, one atomic read-modify-write each.  At each move and each step,
// it compares the thread-local slot of the calling thread with the slot
// that the handle keeps.  It reads no shared state there.  The policy
// keeps no count of the steps.  A handle in a loop keeps one beside its
// Resource (fixy/session/Handle.h), and each step adds one to it, so a
// witness of fixy/session/Witnessed.h can tell two handles of one type
// apart.  A handle outside each loop keeps no count, because its type
// tells its position.
//
// The policy is in the type.  A Debug object and a Release object that
// use different policies name different types, so a layout disagreement
// is a diagnostic at the boundary and not a silent link result.
//
// The same flag gives the liveness check.  A move marks the source
// consumed, and every operation on a consumed handle aborts under the
// two checking policies.  A moved-from handle therefore cannot advance
// a protocol a second time.

#include <fixy/session/Protocol.h>
#include <fixy/session/Watch.h>

#include <foundation/Transfer.h>
#include <foundation/algebra/Modality.h>
#include <foundation/core/Text.h>

#include <concepts>
#include <cstdint>
#include <meta>
#include <source_location>
#include <string_view>
#include <type_traits>

namespace fixy::session {

using ::foundation::algebra::ModalityKind;

// What a destructor does with a handle that still owes its protocol.
enum class AbandonAction : std::uint8_t {
    Ignore,  // nothing
    Abort,  // print the construction site, then std::abort
    Cancel,  // send a cancellation to the peer through the Resource
};

namespace check {

namespace detail {

// Where a checking policy compares the thread that holds the handle with the
// calling thread.
enum class HolderCheck : std::uint8_t {
    // At each step and each move: the tracked policy.
    EachStep,
    // Only at the pin that opens the handle: the pinned policy.  The type
    // keeps the handle on the thread of the pin.
    AtThePin,
};

// The record of a checking policy: the handle's construction site and
// whether it was consumed.  The destructor reads both, and so does the
// liveness check of every operation.  Enforced and Cancel differ only in the
// action, and the tracked and the pinned policies differ only in the holder
// check.
template <AbandonAction Action, HolderCheck Check>
class policy_record {
    static_assert(Action != AbandonAction::Ignore,
                  "fixy/session/Stepping.h: a checking policy acts on a dropped protocol.  check::Off is the policy "
                  "that ignores one.");

    bool flag_ = false;
    // The thread that holds the handle, as the watch last saw it.  With
    // the record below it sits in the padding after the flag, so the
    // policy keeps its size.
    watch::thread_slot holder_ = watch::thread_slot::none;
    // The session's record in fixy/session/Watch.h.  A consumed policy
    // holds none: its record belongs to the next handle now.
    watch::endpoint_id endpoint_ = watch::endpoint_id::none;
    // The site of the mint, for the first handle of a session.  A handle
    // that a step built names no site, and the report of a dropped protocol
    // then reads the site of the mint from the record of the session
    // (fixy/session/Handle.h).  A move keeps the site.
    std::source_location loc_{};

    // Tells the watch when the handle is on a thread other than the one
    // it last saw.  A handle that stays on its thread reads one
    // thread-local value and writes nothing.  A handle that no thread
    // holds yet, from a channel mint, names none, and so does a thread
    // that has not touched the watch: the first move or step on a thread
    // then makes that thread the holder.  A pinned record has no check here.
    constexpr void track_holder_() noexcept {
        if constexpr (Check == HolderCheck::EachStep) {
            if !consteval {
                if (endpoint_ == watch::endpoint_id::none) return;
                const watch::thread_slot current = watch::current_thread_slot();
                if (current != holder_ || current == watch::thread_slot::none) [[unlikely]] {
                    holder_ = watch::note_holder(endpoint_);
                }
            }
        }
    }

public:
    static constexpr AbandonAction action = Action;

    // True when a handle under this policy carries the flag and its
    // destructor acts on it.  Read this and not NDEBUG: the question is
    // about the policy, not about the build mode.
    static constexpr bool checks_abandonment = true;

    static constexpr std::string_view name() noexcept {
        constexpr bool is_pinned = Check == HolderCheck::AtThePin;
        return std::string_view{Action == AbandonAction::Cancel ? (is_pinned ? "PinnedCancel" : "Cancel")
                                                                : (is_pinned ? "Pinned" : "Enforced")};
    }

    constexpr policy_record() noexcept = default;
    constexpr explicit policy_record(std::source_location loc) noexcept : loc_{loc} {}
    // A step builds the next handle on the thread that runs the step.
    // That thread is the holder from now on, although it can differ from
    // the holder that the reference names when it reached the handle
    // through a pointer.
    constexpr policy_record(std::source_location loc, watch::session_ref session) noexcept
        : holder_{session.holder}, endpoint_{session.endpoint}, loc_{loc} {
        track_holder_();
    }

    // The one holder check of a pin (HandleFactory::pin of
    // fixy/session/Handle.h): it tells the watch that the calling thread
    // holds the handle when the handle comes from another thread.
    constexpr void hold_on_this_thread() noexcept
        requires(Check == HolderCheck::EachStep)
    {
        track_holder_();
    }

    /// Marks the handle consumed.  The record goes with the protocol
    /// position, so this policy lets go of it.
    constexpr void mark() noexcept {
        flag_ = true;
        endpoint_ = watch::endpoint_id::none;
    }
    [[nodiscard]] constexpr bool was_marked() const noexcept { return flag_; }
    [[nodiscard]] constexpr std::source_location construction_loc() const noexcept { return loc_; }
    [[nodiscard]] constexpr watch::session_ref session() const noexcept { return {endpoint_, holder_}; }

    // A self-move must not change the tracker.  Without the guard,
    // `h = std::move(h)` copies flag_ onto itself and then sets it, and
    // the live handle then reads as consumed.  The kind self-move of the rule
    // must-consume refuses each move assignment of a handle from itself that
    // one function names.  The guard is the run-time backstop of an alias
    // through two names, such as two reference parameters, which no rule that
    // reads one function at a time can see.
    //
    // A move of a live handle under the tracked policy tells the watch that
    // the calling thread holds it now, which is what the deadlock detector
    // reads.  A move under the pinned policy stays on the thread of the pin.
    constexpr void move_from(policy_record& other) noexcept {
        if (this == &other) [[unlikely]]
            return;
        flag_ = other.flag_;
        holder_ = other.holder_;
        endpoint_ = other.endpoint_;
        loc_ = other.loc_;
        other.flag_ = true;
        other.endpoint_ = watch::endpoint_id::none;
        track_holder_();
    }
};

// The policy of a handle that can go to another thread: Enforced and Cancel.
// Each step and each move compares the holder with the calling thread.
//
// A source_location names strings of static storage that no thread writes,
// and each other member of the record is a value.  So the policy moves to
// another thread with its handle (foundation/Transfer.h), as a delegated
// endpoint does, and as the site of a channel mint of fixy/session/Handle.h
// does.
template <AbandonAction Action>
class[[= ::foundation::transfer::sendable{}]] tracked_policy : public policy_record<Action, HolderCheck::EachStep> {
public:
    using policy_record<Action, HolderCheck::EachStep>::policy_record;
};

// The policy of a handle that a pin holds on its thread (HandleFactory::pin
// of fixy/session/Handle.h): Pinned and PinnedCancel.  It keeps each check of
// the tracked policy of the same action: the flag, the record, the site and
// the report of a dropped protocol.  It has no holder check.  The pin made
// the one holder check, and the one note when the handle
// came from another thread, when it opened, and the type keeps the handle on
// that thread.  The policy is thread_bound and not Shareable
// (foundation/Transfer.h), so a handle that holds it is neither: no door
// moves it to another thread, and no other thread reads it.  So a step and a
// move under it read no slot of the thread and call no note.  The holder of
// the record is the thread of the pin, and the turn back gives it to the
// tracked handle.
template <AbandonAction Action>
class[[= ::foundation::transfer::thread_bound{}]][[= ::foundation::transfer::not_shareable{}]] pinned_policy
    : public policy_record<Action, HolderCheck::AtThePin> {
public:
    using policy_record<Action, HolderCheck::AtThePin>::policy_record;
};

// The pinned policy of a tracked policy, and the tracked policy of a pinned
// policy, for the two turns of a pin.
template <typename Policy>
struct policy_turn;

template <AbandonAction Action>
struct policy_turn<tracked_policy<Action>> {
    using type = pinned_policy<Action>;
};

template <AbandonAction Action>
struct policy_turn<pinned_policy<Action>> {
    using type = tracked_policy<Action>;
};

// The policy of the handles of a session that an owning entry point opens
// (with_session, and each side of mint_forked_channel).  Such an entry point
// opens the session on the thread that runs the body, and it closes the End
// handle that the body returns on that thread.  So each handle of the session
// lives on one thread by construction, and the entry point opens the session
// under the pinned policy of the same action.  Each other policy stays.
template <typename Policy>
struct owned_policy {
    using type = Policy;
};

template <AbandonAction Action>
struct owned_policy<tracked_policy<Action>> {
    using type = pinned_policy<Action>;
};

template <typename Policy>
using owned_policy_t = typename owned_policy<Policy>::type;

}  // namespace detail

using Enforced = detail::tracked_policy<AbandonAction::Abort>;
using Cancel = detail::tracked_policy<AbandonAction::Cancel>;

// The policies of a handle that stays on one thread (detail::pinned_policy).
// A mint under them, HandleFactory::bind and HandleFactory::pin build a handle
// under them on the calling thread.
using Pinned = detail::pinned_policy<AbandonAction::Abort>;
using PinnedCancel = detail::pinned_policy<AbandonAction::Cancel>;

// Carries nothing and answers "consumed" to every question, so the
// destructor's check folds away and [[no_unique_address]] leaves the
// handle the size of its Resource.  The liveness check folds away too.
class Off {
public:
    static constexpr AbandonAction action = AbandonAction::Ignore;
    static constexpr bool checks_abandonment = false;

    static constexpr std::string_view name() noexcept { return "Off"; }

    constexpr Off() noexcept = default;
    // The signatures match Enforced, so a handle constructor forwards a
    // location and a record with no branch on the policy.  Off keeps
    // neither, so no mint gives it a record.
    constexpr explicit Off(std::source_location) noexcept {}
    constexpr Off(std::source_location, watch::session_ref) noexcept {}

    constexpr void mark() noexcept {}
    [[nodiscard]] constexpr bool was_marked() const noexcept { return true; }
    constexpr void move_from(Off&) noexcept {}
    [[nodiscard]] constexpr std::source_location construction_loc() const noexcept { return std::source_location{}; }
    [[nodiscard]] constexpr watch::session_ref session() const noexcept { return {}; }
};

}  // namespace check

// A policy records a construction site, is marked, answers whether it
// was marked, moves, and names the action its destructor takes.  The
// concept constrains the policy parameter of a handle, and it gives a
// fourth policy a contract to meet.
//
// The last clause binds the two constants.  A policy that claims to
// check and then ignores a dropped protocol, or the reverse, is refused.
template <typename P>
concept AbandonmentPolicy = requires(P policy, P other, std::source_location loc, watch::session_ref session) {
    { P::checks_abandonment } -> std::convertible_to<bool>;
    { P::action } -> std::convertible_to<AbandonAction>;
    { P::name() } noexcept -> std::same_as<std::string_view>;
    P{};
    P{loc};
    P{loc, session};
    { policy.mark() } noexcept;
    { policy.was_marked() } noexcept -> std::same_as<bool>;
    { policy.move_from(other) } noexcept;
    { policy.construction_loc() } noexcept -> std::same_as<std::source_location>;
    { policy.session() } noexcept -> std::same_as<watch::session_ref>;
    requires(P::checks_abandonment == (P::action != AbandonAction::Ignore));
};

// The one switch.  Every handle in the tree defaults its policy
// parameter to this alias, and nothing in fixy/session reads NDEBUG.  A
// Release binary and a Debug binary therefore catch a dropped protocol
// in the same way.
using DefaultAbandonmentPolicy = check::Enforced;

// True when a handle under the default policy catches a dropped
// protocol.  A test asserts on this and not on NDEBUG.
inline constexpr bool default_policy_checks_abandonment = DefaultAbandonmentPolicy::checks_abandonment;

// The tree's one spelling of a type's name.  Every diagnostic that
// prints a protocol, and the concept's own name check below, read this,
// so a handle cannot report one spelling while a diagnostic prints
// another.  display_string_of is the same primitive foundation/reflect/
// Hash.h uses for stable_name, and it returns a view into the program's
// constant data.
template <typename T>
inline constexpr std::string_view type_display_name_v = std::meta::display_string_of(^^T);

// The same name as a text of the base, for a report through fatal.  The
// name lives in the constant data of the program, as the view above.
template <typename T>
inline constexpr ::foundation::core::TextView type_display_text_v =
    ::foundation::core::mint_static_text(type_display_name_v<T>.data(), type_display_name_v<T>.size());

// ── The Stepping wrapper contract ────────────────────────────────────
//
// leq on this modality is protocol subtyping, so the grade of a handle
// after an operation is the `next` of the grade before it.  The
// concept cannot check the whole step relation — that would require
// naming every operation — but it does check the three things a
// stepping wrapper cannot be correct without: it reports the Stepping
// modality, it names a protocol and a resource, and it is linear.
//
// The name forwarder repeats GradedWrapper's cheat closure, and for the
// same reason.  A wrapper can define protocol_name() with a body that
// returns something else, and then every diagnostic in the tree reports
// a protocol the handle does not have.  Comparing the reported name
// against the name of the type it claims closes that: the only way to
// satisfy the comparison is to actually forward.

template <typename H>
concept SteppingGraded = requires {
    typename H::protocol_type;
    typename H::resource_type;

    requires(H::modality == ModalityKind::Stepping);

    { H::protocol_name() } noexcept -> std::same_as<std::string_view>;
    { H::is_terminal() } noexcept -> std::same_as<bool>;

    // The cheat closure.  A handle that renders its protocol any other
    // way fails here rather than shipping a diagnostic that names the
    // wrong protocol.
    requires(H::protocol_name() == type_display_name_v<typename H::protocol_type>);

    // The handle's own answer must agree with the protocol trait.  A
    // handle that reported itself terminal while sitting at a Send
    // would be destroyed without complaint at a position where the
    // protocol still owes a message.
    requires(H::is_terminal() == is_terminal_state_v<typename H::protocol_type>);

    // Linearity.  Protocol progress is consumed, not duplicated: two
    // handles at one protocol position would each believe they owe the
    // next step, and the peer would see it once or twice depending on
    // which ran.
    requires(!std::is_copy_constructible_v<H>);
    requires(!std::is_copy_assignable_v<H>);
    requires(std::is_move_constructible_v<H>);
};

}  // namespace fixy::session
