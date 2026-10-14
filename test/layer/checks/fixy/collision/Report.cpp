// The compile-time checks of fixy/collision/Report.h.

#include <fixy/collision/Report.h>

namespace fixy {

namespace collision {

// The vantage point of fixy/collision/Report.h.  Alone, this file covers
// the families that the header includes.  The walk units compile it again
// after each header that can add to fixy::atom::detail
// (test/layer/walk-checks.txt), and there the two checks cover the rest.
// The tag is never defined — it is an identity for the instantiation
// point, not a type anyone uses.
struct collision_header_site;

static_assert(roster_join_diagnostic<collision_header_site>().empty(), roster_join_diagnostic<collision_header_site>());
static_assert(sample_set_diagnostic<collision_header_site>().empty(), sample_set_diagnostic<collision_header_site>());

// The pins that bind the corpus of fixy/collision/Codes.h to the rules of
// fixy/Collision.h.

static_assert(detail::every_live_entry_is_implemented_(),
              "fixy/collision/Report.h: the corpus records a rule as Live and live_rules defines no <code>_ok "
              "member for it.  A rule is Live when it is written, not when it is listed.");

static_assert(detail::implemented_rule_count_() == live_rule_count,
              "fixy/collision/Report.h: live_rules defines a different number of <code>_ok members than the "
              "corpus records as Live.  Either a rule was written without a corpus entry, or one entry names a "
              "rule the implementation spells differently.");

static_assert(detail::every_ok_member_has_a_verdict_row_(),
              "fixy/collision/Report.h: live_rules defines a <code>_ok member with no row in verdicts().  fn's "
              "tier-5 message reads those rows, so the rule would refuse a binding without naming itself, "
              "and its negative fixture would have nothing to floor on.");

static_assert(live_rules<>::verdicts().size() == detail::implemented_rule_count_(),
              "fixy/collision/Report.h: verdicts() holds a different number of rows than live_rules has "
              "<code>_ok members.  A row without a member, or a member without a row.");

}  // namespace collision

}  // namespace fixy
