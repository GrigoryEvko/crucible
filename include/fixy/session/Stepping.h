#pragma once

// The Stepping modality's wrapper contract, and the abandonment-check
// policy that decides what a session handle costs at runtime.
//
// foundation/algebra/Modality.h reserves ModalityKind::Stepping for
// session handles and says why they do not instantiate Graded: the
// grade is a protocol remainder, every operation advances it, so a
// handle at one grade produces a handle at the NEXT grade rather than
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
//     design of Lagaillardie, Neykova and Yoshida (ECOOP 2022).
//   - check::Off does nothing.  It is an explicit hatch, and a handle
//     under it does not keep the guarantee above.
//
// The default is check::Enforced in every build mode, and NDEBUG does not
// change it.  check::Cancel is not the default, because it needs a
// Resource that can carry a cancellation, and most Resources cannot.
// check::Off adds no instruction to a step.  A checking policy adds the
// claim and release of a record in fixy/session/Watch.h at mint and at
// End, one atomic read-modify-write each.  At each move and each step,
// it compares the thread-local slot of the calling thread with the slot
// that the handle keeps.  It reads no shared state there.
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

#include <foundation/algebra/Modality.h>

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
    Abort,   // print the construction site, then std::abort
    Cancel,  // send a cancellation to the peer through the Resource
};

namespace check {

namespace detail {

// Records the handle's construction site and whether it was consumed.
// The destructor reads both, and so does the liveness check of every
// operation.  Enforced and Cancel differ only in the action.
template <AbandonAction Action>
class tracked_policy {
    static_assert(Action != AbandonAction::Ignore,
                  "fixy/session/Stepping.h: a tracked policy acts on a dropped protocol.  check::Off is the policy "
                  "that ignores one.");

    bool flag_ = false;
    // The thread that holds the handle, as the watch last saw it.  With
    // the record below it sits in the padding after the flag, so the
    // policy keeps its size.
    watch::thread_slot holder_ = watch::thread_slot::none;
    // The session's record in fixy/session/Watch.h.  A consumed policy
    // holds none: its record belongs to the next handle now.
    watch::endpoint_id endpoint_ = watch::endpoint_id::none;
    // The handle's construction site.  It is an unknown source for a
    // handle minted without an explicit location.
    std::source_location loc_{};

    // Tells the watch when the handle is on a thread other than the one
    // it last saw.  A handle that stays on its thread reads one
    // thread-local value and writes nothing.  A handle that no thread
    // holds yet, from a channel mint, names none, and so does a thread
    // that has not touched the watch: the first move or step on a thread
    // then makes that thread the holder.
    constexpr void track_holder_() noexcept {
        if !consteval {
            if (endpoint_ == watch::endpoint_id::none) return;
            const watch::thread_slot current = watch::current_thread_slot();
            if (current != holder_ || current == watch::thread_slot::none) [[unlikely]] {
                holder_ = watch::note_holder(endpoint_);
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
        return Action == AbandonAction::Cancel ? std::string_view{"Cancel"} : std::string_view{"Enforced"};
    }

    constexpr tracked_policy() noexcept = default;
    constexpr explicit tracked_policy(std::source_location loc) noexcept : loc_{loc} {}
    // A step builds the next handle on the thread that runs the step.
    // That thread is the holder from now on, although it can differ from
    // the holder that the reference names when it reached the handle
    // through a pointer.
    constexpr tracked_policy(std::source_location loc, watch::session_ref session) noexcept
        : holder_{session.holder}, endpoint_{session.endpoint}, loc_{loc} {
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
    // the live handle then reads as consumed.
    //
    // A move of a live handle tells the watch that the calling thread
    // holds it now, which is what the deadlock detector reads.
    constexpr void move_from(tracked_policy& other) noexcept {
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

}  // namespace detail

using Enforced = detail::tracked_policy<AbandonAction::Abort>;
using Cancel = detail::tracked_policy<AbandonAction::Cancel>;

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

static_assert(std::is_empty_v<Off>, "check::Off must be empty, or [[no_unique_address]] cannot collapse it and a "
                                    "handle under the unchecked policy stops costing exactly its Resource.");

static_assert(!std::is_empty_v<Enforced>, "check::Enforced must carry state.  An empty Enforced would be Off wearing "
                                          "the enforcing name: every handle would report itself consumed and the "
                                          "destructor check would pass for a leaked protocol.");

static_assert(!std::is_empty_v<Cancel>, "check::Cancel must carry state.  An empty Cancel cannot tell a live handle "
                                        "from a consumed one, so it cannot know when to send the cancellation.");

static_assert(!std::is_same_v<Enforced, Cancel>, "check::Enforced and check::Cancel must be different types, "
                                                 "because the policy is part of the handle type.");

static_assert(sizeof(Enforced) == 2 * sizeof(std::source_location),
              "the record index of fixy/session/Watch.h must sit in the padding after the flag, so that the watch "
              "costs a checked handle no byte.");

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

static_assert(AbandonmentPolicy<check::Enforced>);
static_assert(AbandonmentPolicy<check::Cancel>);
static_assert(AbandonmentPolicy<check::Off>);

// THE ONE SWITCH.  Every handle in the tree defaults its policy
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
// Hash.h uses for stable_name_of, and it returns a view into the
// program's constant data.
template <typename T>
inline constexpr std::string_view type_display_name_v = std::meta::display_string_of(^^T);

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
