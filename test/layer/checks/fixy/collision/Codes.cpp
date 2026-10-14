// The compile-time checks of fixy/collision/Codes.h.

#include <fixy/collision/Codes.h>

namespace fixy {

namespace collision {

// ---------------------------------------------------------------------
// The corpus pins.
//
// Each compares two things that were written separately.  None computes
// one side from the other.

// Fifty-four codes of the specification, plus three that this file adds.
//
// The count is stated against the specification, an external list, so a
// code dropped from this list stops being reported as absent — the
// failure this list exists to prevent.
//
// B002 is one addition, and it is an addition rather than a rereading of
// a code of the specification.  Axis::Observability carries two
// theorems: B001's back-pressure trap, and the containment of the
// observability row in the effect row.  Reusing B001 for the second
// would discard a theorem that has its own remedy, and the codes are
// stable API precisely so that cannot happen quietly.
//
// R004 and W003 are the other two.  R004 refuses a continuation that
// captures a live session handle in a frame that is not linear, and W003
// refuses a hot binding that holds one and states no wait.
static_assert(rule_corpus_size == 57,
              "fixy/collision/Codes.h: the rule corpus must account for the 54 codes of the specification, plus "
              "B002, R004 and W003, which this file adds.  A code dropped from this list stops being reported "
              "as absent.");

namespace detail {

[[nodiscard]] consteval std::string_view named_(std::string_view lead, std::string_view code) noexcept {
    return std::define_static_string(std::string{lead} + std::string{code});
}

[[nodiscard]] consteval std::string_view absent_pin_message_(std::string_view lead, const std::string& offenders) {
    std::string message;
    for (const char letter : lead)
        message += letter;
    message += offenders;
    return std::define_static_string(message);
}

}  // namespace detail

static_assert(detail::code_the_enum_disagrees_about_().empty(),
              detail::named_("fixy/collision/Codes.h: the rule corpus and the RuleCode enum disagree about rule ",
                             detail::code_the_enum_disagrees_about_()));

static_assert(detail::code_the_corpus_never_listed_().empty(),
              detail::named_("fixy/collision/Codes.h: this RuleCode enumerator is in no rule_corpus entry: ",
                             detail::code_the_corpus_never_listed_()));

static_assert(detail::absent_rows_not_listed_().empty(),
              detail::absent_pin_message_(
                  "fixy/collision/Codes.h: the corpus marks these rules Absent and absent_rule_codes does not name "
                  "them.  The Absent set only shrinks: a rule leaves it by being written or by a recorded reason, "
                  "and never joins it.  Write the rule, or keep the row it had: ",
                  detail::absent_rows_not_listed_()));

static_assert(detail::listed_codes_not_absent_().empty(),
              detail::absent_pin_message_(
                  "fixy/collision/Codes.h: absent_rule_codes names these codes and the corpus does not mark them "
                  "Absent.  A code whose row left the Absent set leaves this list in the same edit: ",
                  detail::listed_codes_not_absent_()));

static_assert(detail::absent_code_listed_twice_().empty(),
              detail::named_("fixy/collision/Codes.h: absent_rule_codes names this code twice: ",
                             detail::absent_code_listed_twice_()));

// pending_rules and the corpus are two hand-written lists of the same
// set.  Comparing them catches an edit to one and not the other.
static_assert(pending_rule_count == detail::corpus_count_(Disposition::Pending),
              "fixy/collision/Codes.h: pending_rules and the corpus disagree about how many rules are waiting on "
              "an atomless axis.");

// There is deliberately no pin on pending_axis_count here.  It would be
// derived from the array it counts, and the biconditional of
// every_pending_axis_is_still_empty() in fixy/Collision.h already
// fixes the SET by name against the atom catalog: an axis dropped from
// pending_axes while still atomless fails it, and an axis added while it
// has an atom fails it too.  The count could therefore never fail on its
// own, and a pin that cannot fail reads as a second check.

}  // namespace collision

}  // namespace fixy
