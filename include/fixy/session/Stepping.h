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
// such a handle satisfies instead — a SteppingGraded concept — which
// did not exist until this header.  Writing it is the point: without
// it "satisfies the Stepping contract" was a sentence in a comment
// with nothing to check it.
//
// ── The abandonment-check policy ─────────────────────────────────────
//
// A handle destroyed at a non-terminal protocol state, without having
// been consumed, is a dropped protocol.  The ported source detected
// that with a flag and a source_location per handle, and aborted from
// the destructor.  It switched the whole mechanism off with NDEBUG, in
// two independent places: the tracker's was_marked() returned a
// hardcoded true, AND the destructor body sat inside `#ifndef NDEBUG`.
//
// That is the arrangement this header replaces, for three reasons.
//
// 1.  The policy was implicit.  Nothing named it, so nothing could ask
//     for it.  A test could not assert which policy was in force, only
//     infer it from NDEBUG and hope the inference matched the two
//     `#ifdef`s.  It is now a type with a queryable constant, so a
//     test asserts the policy directly.
//
// 2.  Two kill switches for one decision is one too many.  They agreed
//     here, but nothing made them agree; a later edit to either alone
//     would have produced a tracker that records and a destructor that
//     never reads, or the reverse, and neither shows up as a failure.
//     There is now one switch.
//
// 3.  The policy changed sizeof(handle) WITHOUT changing its type.
//     The ported header says so itself — "the two shapes give
//     SessionHandle a different sizeof per build mode ... linking
//     debug and release objects together produces silent layout
//     mismatches" — and then leaves that as a warning in prose.  It is
//     a real arrangement in this repository: test/CMakeLists.txt puts
//     `-UNDEBUG` on every test target while the release preset
//     compiles the library with `-DNDEBUG`, so the two modes meet in
//     one binary by construction.  It is not a live defect today: no
//     library translation unit instantiates a handle (src/cntp/
//     PathSwap.cpp includes the session header but only through
//     function templates it never calls), so there is no second
//     definition to disagree with.  It is one `#include` and one call
//     away from being live, and it would be silent when it arrived.
//
//     Carrying the policy IN THE TYPE closes that permanently.  A
//     Debug translation unit and a Release one now name different
//     types, so a mismatch is a diagnostic at the boundary instead of
//     a layout disagreement the linker resolves by picking one.
//
// What is deliberately NOT changed: the default still follows NDEBUG,
// so a Release binary pays nothing and a Debug or test binary keeps
// the check exactly as before.  This is a port, and silently arming a
// per-handle branch and 24 bytes of source_location in Release would
// be a redesign smuggled in as one.  Choosing the other default is a
// one-line change here, which is the point of naming it.

#include <fixy/session/Protocol.h>

#include <foundation/algebra/Modality.h>

#include <concepts>
#include <meta>
#include <source_location>
#include <string_view>
#include <type_traits>

namespace fixy::session {

using ::foundation::algebra::ModalityKind;

namespace check {

// Records the handle's construction site and whether it was consumed.
// The destructor reads both.
class Enforced {
    bool flag_ = false;
    // The handle's construction site.  Default-constructs to an
    // unknown source for a handle minted without an explicit location.
    std::source_location loc_{};

public:
    // True when a handle under this policy carries the flag and its
    // destructor acts on it.  Read this rather than NDEBUG: a test
    // that wants to know whether abandonment is caught is asking about
    // the policy, not about the build mode that selected it.
    static constexpr bool checks_abandonment = true;

    static constexpr std::string_view name() noexcept { return "Enforced"; }

    constexpr Enforced() noexcept = default;
    constexpr explicit Enforced(std::source_location loc) noexcept : loc_{loc} {}

    constexpr void mark() noexcept { flag_ = true; }
    [[nodiscard]] constexpr bool was_marked() const noexcept { return flag_; }
    [[nodiscard]] constexpr std::source_location construction_loc() const noexcept { return loc_; }

    // Self-move must leave the tracker untouched.  Without the guard,
    // `h = std::move(h)` would copy flag_ onto itself and then set it
    // true, marking a live handle consumed and disabling the
    // destructor's abandonment check for it.
    constexpr void move_from(Enforced& other) noexcept {
        if (this == &other) [[unlikely]]
            return;
        flag_ = other.flag_;
        loc_ = other.loc_;
        other.flag_ = true;
    }
};

// Carries nothing and answers "consumed" to every question, so the
// destructor's check folds away and [[no_unique_address]] leaves the
// handle the size of its Resource.
class Off {
public:
    static constexpr bool checks_abandonment = false;

    static constexpr std::string_view name() noexcept { return "Off"; }

    constexpr Off() noexcept = default;
    // The signature matches Enforced so a handle constructor forwards
    // a location unconditionally rather than branching on the policy.
    constexpr explicit Off(std::source_location) noexcept {}

    constexpr void mark() noexcept {}
    [[nodiscard]] constexpr bool was_marked() const noexcept { return true; }
    constexpr void move_from(Off&) noexcept {}
    [[nodiscard]] constexpr std::source_location construction_loc() const noexcept { return std::source_location{}; }
};

static_assert(std::is_empty_v<Off>, "check::Off must be empty, or [[no_unique_address]] cannot collapse it and a "
                                    "handle under the unchecked policy stops costing exactly its Resource.");

static_assert(!std::is_empty_v<Enforced>, "check::Enforced must carry state.  An empty Enforced would be Off wearing "
                                          "the enforcing name: every handle would report itself consumed and the "
                                          "destructor check would pass for a leaked protocol.");

}  // namespace check

// A policy is anything that can record a construction site, be marked,
// be asked whether it was marked, and move.  Both shapes above satisfy
// it; the concept exists so a handle's policy parameter is constrained
// rather than duck-typed, and so a third policy — one that logs
// instead of aborting, say — has a contract to meet.
template <typename P>
concept AbandonmentPolicy = requires(P policy, P other, std::source_location loc) {
    { P::checks_abandonment } -> std::convertible_to<bool>;
    { P::name() } noexcept -> std::same_as<std::string_view>;
    P{};
    P{loc};
    { policy.mark() } noexcept;
    { policy.was_marked() } noexcept -> std::same_as<bool>;
    { policy.move_from(other) } noexcept;
    { policy.construction_loc() } noexcept -> std::same_as<std::source_location>;
};

static_assert(AbandonmentPolicy<check::Enforced>);
static_assert(AbandonmentPolicy<check::Off>);

// THE ONE SWITCH.  Every handle in the tree defaults its policy
// parameter to this alias, and nothing else in fixy/session reads
// NDEBUG.  Changing the tree's policy is changing this line.
using DefaultAbandonmentPolicy =
#ifdef NDEBUG
    check::Off;
#else
    check::Enforced;
#endif

// True when the translation unit compiles handles that catch
// abandonment.  A test asserts on this rather than on NDEBUG, so the
// assertion keeps meaning if the default above is ever re-decided.
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
