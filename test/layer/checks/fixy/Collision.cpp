// The compile-time checks of fixy/Collision.h.

#include <fixy/Collision.h>

namespace fixy {

namespace collision {

// grades resolves an unmentioned axis to axis_traits<A>::strict, so the
// resolver depends on the refusal of an unclassified axis in
// fixy/Axis.h.  The message names the two ways to classify an axis.
static_assert(::fixy::every_axis_has_traits(),
              "fixy::collision::grades resolves an unmentioned axis to axis_traits<A>::strict, so every axis "
              "must be classified: it carries a hand-written specialization, or it is named on "
              "Axis.h's defaulted_axes roster as a Fact axis whose pole claims nothing.");

// The vantage point of fixy/Collision.h.  It covers the families that
// the header includes and no others; utils/scripts/check-atom-roster-joined.sh
// instantiates the same two templates from a sentinel that includes every
// header under fixy/ and so covers the rest.  The tag is never defined —
// it is an identity for the instantiation point, not a type anyone uses.
struct collision_header_site;

static_assert(roster_join_diagnostic<collision_header_site>().empty(), roster_join_diagnostic<collision_header_site>());
static_assert(sample_set_diagnostic<collision_header_site>().empty(), sample_set_diagnostic<collision_header_site>());

static_assert(every_pending_axis_is_still_empty(),
              "fixy/Collision.h: the pending-rule roster is out of date.  Either an axis listed in "
              "collision::pending_axes has gained its first atom — in which case the rules registered against it in "
              "collision::pending_rules can now fire and must be written as live rules, and the axis removed from "
              "pending_axes — or, since both lists are empty, a family roster under fixy/atoms/ is no longer "
              "joined into collision::all_atom_roster, which makes its axes look empty.");

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
              "fixy/Collision.h: the rule corpus must account for the 54 codes of the specification, plus "
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
              detail::named_("fixy/Collision.h: the rule corpus and the RuleCode enum disagree about rule ",
                             detail::code_the_enum_disagrees_about_()));

static_assert(detail::code_the_corpus_never_listed_().empty(),
              detail::named_("fixy/Collision.h: this RuleCode enumerator is in no rule_corpus entry: ",
                             detail::code_the_corpus_never_listed_()));

static_assert(detail::absent_rows_not_listed_().empty(),
              detail::absent_pin_message_(
                  "fixy/Collision.h: the corpus marks these rules Absent and absent_rule_codes does not name them.  "
                  "The Absent set only shrinks: a rule leaves it by being written or by a recorded reason, and "
                  "never joins it.  Write the rule, or keep the row it had: ",
                  detail::absent_rows_not_listed_()));

static_assert(detail::listed_codes_not_absent_().empty(),
              detail::absent_pin_message_(
                  "fixy/Collision.h: absent_rule_codes names these codes and the corpus does not mark them "
                  "Absent.  A code whose row left the Absent set leaves this list in the same edit: ",
                  detail::listed_codes_not_absent_()));

static_assert(detail::absent_code_listed_twice_().empty(),
              detail::named_("fixy/Collision.h: absent_rule_codes names this code twice: ",
                             detail::absent_code_listed_twice_()));

// pending_rules and the corpus are two hand-written lists of the same
// set.  Comparing them catches an edit to one and not the other.
static_assert(pending_rule_count == detail::corpus_count_(Disposition::Pending),
              "fixy/Collision.h: pending_rules and the corpus disagree about how many rules are waiting on an "
              "atomless axis.");

// There is deliberately no pin on pending_axis_count here.  It would be
// derived from the array it counts, and the biconditional above already
// fixes the SET by name against the atom catalog: an axis dropped from
// pending_axes while still atomless fails it, and an axis added while it
// has an atom fails it too.  The count could therefore never fail on its
// own, and a pin that cannot fail reads as a second check.

static_assert(detail::every_live_entry_is_implemented_(),
              "fixy/Collision.h: the corpus records a rule as Live and live_rules defines no <code>_ok member "
              "for it.  A rule is Live when it is written, not when it is listed.");

static_assert(detail::implemented_rule_count_() == live_rule_count,
              "fixy/Collision.h: live_rules defines a different number of <code>_ok members than the corpus "
              "records as Live.  Either a rule was written without a corpus entry, or one entry names a rule "
              "the implementation spells differently.");

static_assert(detail::every_ok_member_has_a_verdict_row_(),
              "fixy/Collision.h: live_rules defines a <code>_ok member with no row in verdicts().  fn's "
              "tier-5 message reads those rows, so the rule would refuse a binding without naming itself, "
              "and its negative fixture would have nothing to floor on.");

static_assert(live_rules<>::verdicts().size() == detail::implemented_rule_count_(),
              "fixy/Collision.h: verdicts() holds a different number of rows than live_rules has <code>_ok "
              "members.  A row without a member, or a member without a row.");

static_assert(live_rules<>::failing_codes().empty(),
              "fixy/Collision.h: the empty pack sits at every strict pole and must trip no rule, so the "
              "code list it produces is empty.  A non-empty answer here means a rule fires on a binding "
              "that claims nothing.");

}  // namespace collision

namespace detail::collision_self_test {

using ::fixy::collision::grades;
using ::fixy::collision::live_rules;

// The empty pack trips nothing: every axis sits at its strict pole.
static_assert(live_rules<>::valid);
static_assert(live_rules<>::validate());

// grades answers the same question fn::grade_on does, without fn.
static_assert(std::is_same_v<grades<::fixy::atom::copy>::on<Axis::Usage>, ::fixy::atom::copy>);
static_assert(std::is_same_v<grades<>::on<Axis::Usage>, typename axis_traits<Axis::Usage>::strict>);
static_assert(grades<::fixy::atom::copy>::mentions<Axis::Usage>);
static_assert(!grades<>::mentions<Axis::Usage>);

// Each live rule refuses its pair and admits the halves.  Going through
// grades rather than fn is deliberate: fn would refuse to compile, and
// a rule that is only observable as a build failure cannot be tested.
static_assert(!live_rules<::fixy::atom::borrow, ::fixy::atom::coroutine>::L002_ok);
static_assert(live_rules<::fixy::atom::borrow>::L002_ok);
static_assert(live_rules<::fixy::atom::coroutine>::L002_ok);

static_assert(!live_rules<::fixy::atom::ghost, ::fixy::atom::with<::foundation::effects::Effect::Alloc>>::P010_ok);
static_assert(live_rules<::fixy::atom::ghost>::P010_ok);

// A binding that says nothing about Trust sits at the strict pole, which is
// unverified, so T001 refuses a capability alone, as it refuses the pack
// with trust_unverified.  A trust grade that is not Unverified admits it.
static_assert(!live_rules<::fixy::atom::capability_usage, ::fixy::atom::trust_unverified>::T001_ok);
static_assert(!live_rules<::fixy::atom::capability_usage>::T001_ok);
static_assert(live_rules<::fixy::atom::capability_usage, ::fixy::atom::trust_verified>::T001_ok);
static_assert(live_rules<::fixy::atom::trust_unverified>::T001_ok);
static_assert(live_rules<>::T001_ok);

// Every Trust atom of the roster has a class.  The walk names each Trust
// atom that the closed relation does not answer for.  A walk that sees no
// Trust atom proves nothing, so the walk refuses that roster too.
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wshadow"

[[nodiscard]] consteval std::string trust_atoms_without_a_class_() {
    std::size_t seen = 0;
    std::string offenders;
    template for (constexpr auto member : ::fixy::atom::detail::roster_members_v<::fixy::collision::all_atom_roster>) {
        using Atom = [:member:];
        if constexpr (Atom::axis == Axis::Trust) {
            ++seen;
            if (!::fixy::collision::detail::trust_class_of_(member)) {
                if (!offenders.empty()) offenders += ", ";
                offenders += std::meta::display_string_of(member);
            }
        }
    }
    return seen == 0 ? std::string{"(the roster has no Trust atom)"} : offenders;
}

#pragma GCC diagnostic pop

static_assert(trust_atoms_without_a_class_().empty(),
              std::string_view{std::define_static_string(
                  "fixy/Collision.h: these Trust atoms have no class in fixy::collision::TrustClass: "
                  + trust_atoms_without_a_class_()
                  + ".  Name each one in the closed relation trust_class_of_, and decide if T001 reads it as "
                    "unverified.")});
static_assert(::fixy::collision::IsTrustGrade<typename axis_traits<Axis::Trust>::strict>,
              "the strict Trust pole must have a class, because a binding that says nothing about Trust reads it");
static_assert(!::fixy::collision::IsTrustGrade<int>, "a type that is not a Trust grade has no class");
static_assert(!::fixy::collision::IsTrustGrade<::fixy::atom::copy>, "an atom on another axis has no Trust class");

static_assert(!live_rules<::fixy::atom::coroutine, ::fixy::atom::borrow>::R002_ok);
static_assert(!live_rules<::fixy::atom::borrow, ::fixy::atom::with<::foundation::effects::Effect::Bg>>::L007_ok);

// M012 needs all three premises, so the atomic representation is what
// rescues it.  That is the rule's whole content.
static_assert(!live_rules<::fixy::atom::mut_monotonic, ::fixy::atom::coroutine>::M012_ok);
static_assert(live_rules<::fixy::atom::mut_monotonic, ::fixy::atom::coroutine,
                         ::fixy::atom::repr<::fixy::pole::ReprKind::Atomic>>::M012_ok);

// P002 reaches the two emitting axes, and a call through the vDSO lifts
// the empty row, so the pair it refuses is one P010 admits.  Both halves
// alone are fine.  A stdio write lifts IO and Block, so P010 refuses a
// ghost write as well.
namespace p002_cells {
using VdsoRead = ::fixy::atom::syscall::per<::fixy::atom::syscall::SyscallId::clock_gettime>;
using StdoutWrite = ::fixy::atom::stdio::write<::fixy::atom::stdio::streams::Stdout>;
static_assert(!live_rules<::fixy::atom::ghost, VdsoRead>::P002_ok);
static_assert(live_rules<::fixy::atom::ghost, VdsoRead>::P010_ok,
              "P002 must be the rule that catches this pair; if P010 already did, P002 would be redundant");
static_assert(live_rules<VdsoRead>::P002_ok);
static_assert(live_rules<::fixy::atom::ghost>::P002_ok);
static_assert(!live_rules<::fixy::atom::ghost, StdoutWrite>::P002_ok
              && !live_rules<::fixy::atom::ghost, StdoutWrite>::P010_ok);
}  // namespace p002_cells

// The constant-time family.  constant_time alone trips nothing, and each
// rule needs its second premise.
namespace ct_cells {
namespace at = ::fixy::atom;
namespace fp = ::fixy::atom::fp;
using CT = at::constant_time;
using FlushBoth = fp::mode<fp::FpDenormalInput::DenormalsAreZero, fp::FpFtz::FlushToZero>;

static_assert(live_rules<CT>::valid);
static_assert(live_rules<CT, FlushBoth>::valid, "a constant-time mode that names both flushes is legal");

static_assert(!live_rules<CT, fp::mode<fp::FpReassociate::UnrestrictedRewrite, fp::FpDenormalInput::DenormalsAreZero,
                                       fp::FpFtz::FlushToZero>>::F103_ok);
static_assert(live_rules<CT, fp::mode<fp::FpReassociate::BoundedTreeDepth, fp::FpDenormalInput::DenormalsAreZero,
                                      fp::FpFtz::FlushToZero>>::valid,
              "a tree of bounded depth keeps its topology apart from the data, so F103 admits it");
static_assert(live_rules<fp::mode<fp::FpReassociate::UnrestrictedRewrite>>::F103_ok, "no timing claim, no F103");

static_assert(!live_rules<CT, fp::mode<fp::FpFtz::FlushToZero>>::F104_ok, "an unnamed setting honours denormals");
static_assert(!live_rules<CT, fp::mode<fp::FpDenormalInput::HonorDenormals, fp::FpFtz::FlushToZero>>::F104_ok);
static_assert(live_rules<CT, fp::mode<fp::FpDenormalInput::DenormalsAreZero, fp::FpFtz::FlushToZero>>::F104_ok);
static_assert(!live_rules<CT, fp::mode<fp::FpDenormalInput::DenormalsAreZero>>::F105_ok);
static_assert(!live_rules<CT, fp::mode<fp::FpFtz::PreserveSubnormals, fp::FpDenormalInput::DenormalsAreZero>>::F105_ok);
static_assert(live_rules<CT>::F104_ok && live_rules<CT>::F105_ok, "no FP mode stated, no FP claim to refuse");
static_assert(live_rules<fp::mode<>>::F104_ok && live_rules<fp::mode<>>::F105_ok, "no timing claim");

static_assert(!live_rules<CT, at::coroutine>::E044_ok);
static_assert(!live_rules<CT, at::ctrl::coroutine<at::ctrl::async_task>>::E044_ok);
static_assert(!live_rules<CT, at::with<::foundation::effects::Effect::Bg>>::E044_ok);
static_assert(live_rules<CT, at::with<>>::E044_ok);

static_assert(!live_rules<CT, at::stale_to<1>>::S010_ok);
static_assert(live_rules<at::stale_to<1>>::S010_ok);

struct session_witness final {};
using Session = at::protocol<session_witness>;
static_assert(!live_rules<at::coroutine, Session>::I004_ok, "the strict Security pole is classified");
static_assert(!live_rules<at::as_secret, at::ctrl::coroutine<at::ctrl::async_task>, Session>::I004_ok);
static_assert(live_rules<CT, at::coroutine, Session>::I004_ok, "I004 stands down for constant time; E044 fires");
static_assert(!live_rules<CT, at::coroutine, Session>::E044_ok);
static_assert(live_rules<at::as_public, at::coroutine, Session>::valid);
static_assert(live_rules<at::as_internal, at::coroutine, Session>::valid, "internal is below the carrier");
static_assert(live_rules<at::declassify<::fixy::tags::secret_policy::WireSerialize>, at::coroutine, Session>::valid);
static_assert(live_rules<at::coroutine, at::protocol<::fixy::pole::proto::None>>::valid, "no session");
static_assert(live_rules<Session>::valid, "a synchronous session sends at no time the scheduler chooses");

// The failure family.  The payload half stands down under void, so these
// cells name the payload.
struct plain_error final {};
using SecretError = ::fixy::Secret<plain_error>;
using ::fixy::collision::rules_of;

static_assert(!rules_of<std::expected<int, plain_error>>::I002_ok, "the strict Security pole is classified");
static_assert(!rules_of<int, at::ctrl::throws<>>::I002_ok, "an exception family the reader cannot see is plain");
static_assert(!rules_of<::fixy::DetSafe<::fixy::DetSafeTier_v::Pure, std::expected<int, plain_error>>>::I002_ok,
              "a band does not hide the failure it grades");
static_assert(rules_of<std::expected<int, SecretError>>::valid);
static_assert(rules_of<int, at::ctrl::throws<SecretError>>::valid);
static_assert(rules_of<std::expected<int, plain_error>, at::as_public>::valid);
static_assert(rules_of<std::expected<int, plain_error>, at::as_internal>::valid);
static_assert(rules_of<::fixy::Secret<std::expected<int, plain_error>>>::valid,
              "the error inside a Secret is secret, and the discriminant with it");
static_assert(live_rules<at::ctrl::throws<SecretError>>::valid);

static_assert(!rules_of<std::expected<int, SecretError>, CT>::I003_ok);
static_assert(rules_of<std::expected<int, SecretError>, CT>::I002_ok, "a Secret error isolates I003");
static_assert(!rules_of<int, CT, at::ctrl::throws<SecretError>>::I003_ok);
static_assert(rules_of<int, CT>::valid);
}  // namespace ct_cells

// The session family.  R004 needs three premises: a live handle, a
// suspension on either axis, and a usage weaker than linear.  Each cell
// removes one premise and the rule stands down.
namespace session_cells {
namespace at = ::fixy::atom;
using ::fixy::collision::rules_of;

struct handshake final {};
using Live = at::session::live_handle<handshake>;
using Suspends = at::ctrl::coroutine<at::ctrl::async_task>;

static_assert(!live_rules<Live, Suspends, at::affine>::R004_ok);
static_assert(!live_rules<Live, at::coroutine, at::copy>::R004_ok, "a suspension on Reentrancy counts too");
static_assert(live_rules<Live, Suspends>::R004_ok, "a linear frame is resumed exactly once");
static_assert(live_rules<Live, at::affine>::R004_ok, "no suspension, so no continuation");
static_assert(live_rules<Suspends, at::affine>::R004_ok, "no handle in the frame");
static_assert(live_rules<at::protocol<handshake>, Suspends, at::affine>::R004_ok,
              "a protocol grade says the binding speaks a protocol, not that its frame holds a handle");

// A live handle is a session, so I004 reads it as one.
static_assert(!live_rules<Live, Suspends>::I004_ok, "the strict Security pole is classified");
static_assert(live_rules<Live, Suspends, at::as_public>::valid);

// The payload half.  These stand-ins carry the Stepping contract that a
// session handle carries: the modality and the answer to is_terminal().
struct owes_a_step final {
    static constexpr ::foundation::algebra::ModalityKind modality = ::foundation::algebra::ModalityKind::Stepping;
    [[nodiscard]] static constexpr bool is_terminal() noexcept { return false; }
};
struct at_the_end final {
    static constexpr ::foundation::algebra::ModalityKind modality = ::foundation::algebra::ModalityKind::Stepping;
    [[nodiscard]] static constexpr bool is_terminal() noexcept { return true; }
};
struct stepping_with_no_answer final {
    static constexpr ::foundation::algebra::ModalityKind modality = ::foundation::algebra::ModalityKind::Stepping;
};
struct graded_not_stepping final {
    static constexpr ::foundation::algebra::ModalityKind modality = ::foundation::algebra::ModalityKind::Absolute;
    [[nodiscard]] static constexpr bool is_terminal() noexcept { return false; }
};

static_assert(!rules_of<owes_a_step, Suspends, at::affine>::R004_ok);
static_assert(rules_of<owes_a_step, Suspends>::R004_ok, "a linear frame is resumed exactly once");
static_assert(rules_of<at_the_end, Suspends, at::affine>::R004_ok, "a handle at End owes nothing");
static_assert(!rules_of<stepping_with_no_answer, Suspends, at::affine>::R004_ok,
              "a Stepping type that does not answer is read as live");
static_assert(rules_of<graded_not_stepping, Suspends, at::affine>::R004_ok, "another modality is not a session");
static_assert(!rules_of<::fixy::DetSafe<::fixy::DetSafeTier_v::Pure, owes_a_step>, Suspends, at::affine>::R004_ok,
              "a band does not hide the handle it grades");
static_assert(live_rules<Suspends, at::affine>::R004_ok, "the payload half stands down under the pack-only view");

// W003 needs three premises: the hot regime, a live handle, and no stated
// wait.  A stated wait moves the judgement to W001, which refuses a
// kernel wait and admits a spin.
struct hot_invariant final {};
template <class... Extra>
using hot_rules =
    live_rules<at::regime::hot, at::cost_constant, at::refined_with<hot_invariant>, at::as_public, Extra...>;
static_assert(!hot_rules<Live>::W003_ok, "a hot handle with no stated wait");
static_assert(hot_rules<Live, at::sync::spin_pause>::valid, "a stated spin is the hot-path wait");
static_assert(hot_rules<Live, at::sync::bounded_spin>::valid);
static_assert(hot_rules<Live, at::sync::umwait_c01>::valid, "UMWAIT stays in user space, as W001 reads it");
static_assert(hot_rules<Live, at::sync::park>::W003_ok && !hot_rules<Live, at::sync::park>::W001_ok,
              "a stated kernel wait is W001's refusal, not W003's");
static_assert(live_rules<Live>::W003_ok, "not hot, so the wait is the regime's business");
static_assert(hot_rules<>::W003_ok, "no handle, so no transport wait");
// A type on the axis that names no strategy is not an atom of the
// catalog, so no rule reads it and the grade lookup refuses it.
struct names_no_strategy final : at::atom_of<Axis::Synchronization> {};
static_assert(!::fixy::atom::IsAtom<names_no_strategy>);
template <class G>
concept grade_lookup_forms = requires { typename grades<G>; };
static_assert(grade_lookup_forms<at::sync::park>);
static_assert(!grade_lookup_forms<names_no_strategy>, "a grade lookup over a type that is not an atom must not form");
static_assert(
    !rules_of<owes_a_step, at::regime::hot, at::cost_constant, at::refined_with<hot_invariant>, at::as_public>::W003_ok,
    "a payload that is a live handle waits as the atom does");
static_assert(
    rules_of<at_the_end, at::regime::hot, at::cost_constant, at::refined_with<hot_invariant>, at::as_public>::W003_ok,
    "a handle at End takes no step");
}  // namespace session_cells

}  // namespace detail::collision_self_test

}  // namespace fixy
