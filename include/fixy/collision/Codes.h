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
#include <foundation/reflect/Anchor.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <meta>
#include <string>
#include <string_view>
#include <vector>

namespace fixy {

namespace collision {

// ---------------------------------------------------------------------
// The rule codes.  Letter codes are stable API: the negative corpus
// greps them, so a code is never renamed or reused.

enum class RuleCode : std::uint8_t {
    L002,  // borrow x async
    M012,  // monotonic x concurrent without atomic representation
    P010,  // ghost x observable effect
    L007,  // borrow x Bg row
    T001,  // capability x unverified trust
    R002,  // coroutine x borrow
    R003,  // coroutine x Bg row
    L006,  // linear x longjmp
    G002,  // thread-local x atomic representation
    D002,  // unbounded recursion x unbounded cost
    P002,  // ghost x an emitting surface other than the effect row
    // Pending: the axis each waits on is named in pending_rules below.
    B001,
    // B002 is a code of its own, not a renaming of B001.  B001's theorem
    // is the back-pressure trap, with its own remedy.  The containment
    // invariant below is a different theorem about the same axis, and the
    // codes are stable API that is never reused.
    B002,
    H001,
    H002,
    H003,
    H010,
    R001,
    S001,
    W001,
    W002,
    F101,
    F102,
    // An Absent or Retired code has no enumerator, and a pin in the check
    // file of this header holds that: this enum holds exactly the codes
    // that a rule of fixy/Collision.h can be named by, and any other code
    // is named only by the corpus, as a string.  Nothing is renamed and
    // nothing is reused: the corpus
    // carries each string, with the reason that it cannot fire or is
    // retired.
    V101,
    V201,
    V202,
    V203,
    V301,
    V401,
    V402,
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
};

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
// is a live_rules member of fixy/Collision.h and a Live row in
// rule_corpus.  Three of
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
// The pins in the check files of this header and of
// fixy/collision/Report.h compare this list against the RuleCode enum and
// against the members live_rules actually defines, so no one of the three
// can drift without one of the others reporting it.
//
// The specification names 54 codes, and this file adds three.

enum class Disposition : std::uint8_t {
    Live,  // implemented in live_rules of fixy/Collision.h, and able to fire today
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
    {"G002", Disposition::Live, "thread-local x atomic representation"},
    {"D002", Disposition::Live, "unbounded recursion x unbounded cost"},

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
    // std::expected<T, E> or the ControlFlow grade ctrl::throws<E>.
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

    // Three hold across several bindings.  rules_of is handed one binding,
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
     "discharged: a failure here is a std::expected return or a throw, and each runs every destructor in scope, so "
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
    template for (constexpr auto enumerator : std::define_static_array(
                      static_cast<::foundation::reflect::anchored_t<^^Anchor, std::vector<std::meta::info>>>(
                          std::meta::enumerators_of(^^RuleCode)))) {
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
    template for (constexpr auto enumerator : std::define_static_array(
                      static_cast<::foundation::reflect::anchored_t<^^Anchor, std::vector<std::meta::info>>>(
                          std::meta::enumerators_of(^^RuleCode)))) {
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
