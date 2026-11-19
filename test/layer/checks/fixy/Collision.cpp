// The compile-time checks of fixy/Collision.h.

#include <fixy/Collision.h>

namespace fixy {

namespace collision {

// grades resolves an unmentioned axis to the strict pole of its row in
// the axis table, so the resolver depends on a row for each axis in
// fixy/Axis.h.  The message names the two ways to write a row.
static_assert(::fixy::every_axis_has_traits(),
              "fixy::collision::grades resolves an unmentioned axis to strict_pole_t<A>, so every axis must "
              "have a row in axis_record of Axis.h: a row of its own, or the default row of a Fact axis whose "
              "pole claims nothing.");

static_assert(every_pending_axis_is_still_empty(),
              "fixy/Collision.h: the pending-rule roster is out of date.  Either an axis listed in "
              "collision::pending_axes has gained its first atom — in which case the rules registered against it in "
              "collision::pending_rules can now fire and must be written as live rules, and the axis removed from "
              "pending_axes — or, since both lists are empty, a family roster under fixy/atoms/ is no longer "
              "joined into collision::all_atom_roster, which makes its axes look empty.");

static_assert(pack_verdict_v<>.failing_codes().empty(),
              "fixy/Collision.h: the empty pack sits at every strict pole and must trip no rule, so the "
              "code list it produces is empty.  A non-empty answer here means a rule fires on a binding "
              "that claims nothing.");

}  // namespace collision

namespace detail::collision_self_test {

using ::fixy::collision::grades;
using ::fixy::collision::pack_verdict_v;
using enum ::fixy::collision::RuleCode;

// The empty pack trips nothing: every axis sits at its strict pole.
static_assert(pack_verdict_v<>.is_valid());
static_assert(pack_verdict_v<>.tripped == 0, "no bit of the verdict of the empty pack is set");

// grades answers the same question fn::grade_on does, without fn.
static_assert(std::is_same_v<grades<::fixy::atom::copy>::on<Axis::Usage>, ::fixy::atom::copy>);
static_assert(std::is_same_v<grades<>::on<Axis::Usage>, strict_pole_t<Axis::Usage>>);
static_assert(grades<::fixy::atom::copy>::mentions<Axis::Usage>);
static_assert(!grades<>::mentions<Axis::Usage>);

// Each live rule refuses its pair and admits the halves.  Going through
// grades rather than fn is deliberate: fn would refuse to compile, and
// a rule that is only observable as a build failure cannot be tested.
static_assert(!pack_verdict_v<::fixy::atom::borrow, ::fixy::atom::coroutine>.admits(L002));
static_assert(pack_verdict_v<::fixy::atom::borrow>.admits(L002));
static_assert(pack_verdict_v<::fixy::atom::coroutine>.admits(L002));

static_assert(
    !pack_verdict_v<::fixy::atom::ghost, ::fixy::atom::with<::foundation::effects::Effect::Alloc>>.admits(P010));
static_assert(pack_verdict_v<::fixy::atom::ghost>.admits(P010));

// A binding that says nothing about Trust sits at the strict pole, which is
// unverified, so T001 refuses a capability alone, as it refuses the pack
// with trust<tags::trust::Unverified>.  A trust grade that is not Unverified
// admits it.
namespace trust_level = ::fixy::tags::trust;
static_assert(
    !pack_verdict_v<::fixy::atom::capability_usage, ::fixy::atom::trust<trust_level::Unverified>>.admits(T001));
static_assert(!pack_verdict_v<::fixy::atom::capability_usage>.admits(T001));
static_assert(pack_verdict_v<::fixy::atom::capability_usage, ::fixy::atom::trust<trust_level::Verified>>.admits(T001));
static_assert(pack_verdict_v<::fixy::atom::trust<trust_level::Unverified>>.admits(T001));
static_assert(pack_verdict_v<>.admits(T001));

// The level of a Trust grade is a tag of fixy/Tags.h: the level that a trust
// atom names, Assumed for trust_assumed, and Unverified for the strict pole.
using ::fixy::collision::HasTrustLevel;
static_assert(HasTrustLevel<::fixy::atom::trust<trust_level::Tested>, trust_level::Tested>);
static_assert(HasTrustLevel<::fixy::atom::trust<trust_level::External>, trust_level::External>);
static_assert(!HasTrustLevel<::fixy::atom::trust<trust_level::External>, trust_level::Tested>);
static_assert(HasTrustLevel<::fixy::atom::trust_assumed<0>, trust_level::Assumed>);
static_assert(HasTrustLevel<strict_pole_t<Axis::Trust>, trust_level::Unverified>);
static_assert(!HasTrustLevel<::fixy::atom::copy, trust_level::Unverified>);

// The atom trust takes a level of the tags, and not Assumed, because
// trust_assumed carries the rationale of an assumption.
template <class Level>
concept NamesTrustAtom = requires { typename ::fixy::atom::trust<Level>; };
static_assert(NamesTrustAtom<trust_level::Verified> && !NamesTrustAtom<trust_level::Assumed>);
static_assert(!NamesTrustAtom<int> && !NamesTrustAtom<::fixy::tags::access::RO>);

// Every Trust, CallShape and Protocol atom of the roster has an answer in
// the closed relation of its axis.  The walk names each atom that its
// relation does not answer for.  A walk that sees no atom of one of the
// three axes proves nothing for that axis, so the walk refuses that roster
// too.
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wshadow"

[[nodiscard]] consteval std::string atoms_without_an_answer_() {
    namespace cd = ::fixy::collision::detail;
    std::size_t trust_seen = 0;
    std::size_t call_shape_seen = 0;
    std::size_t protocol_seen = 0;
    std::string offenders;
    template for (constexpr auto member : ::fixy::atom::detail::roster_members_v<::fixy::collision::all_atom_roster>) {
        using Atom = [:member:];
        bool is_answered = true;
        if constexpr (Atom::axis == Axis::Trust) {
            ++trust_seen;
            is_answered = ::fixy::collision::IsTrustGrade<Atom>;
        } else if constexpr (Atom::axis == Axis::CallShape) {
            ++call_shape_seen;
            is_answered = cd::call_shape_reading_(member) != cd::CallShapeReading::Unread;
        } else if constexpr (Atom::axis == Axis::Protocol) {
            ++protocol_seen;
            is_answered = cd::protocol_reading_(member) != cd::ProtocolReading::Unread;
        }
        if (!is_answered) {
            if (!offenders.empty()) offenders += ", ";
            offenders += std::meta::display_string_of(member);
        }
    }
    const bool sees_each_axis = trust_seen != 0 && call_shape_seen != 0 && protocol_seen != 0;
    return sees_each_axis ? offenders : std::string{"(the roster has no atom on one of the three axes)"};
}

#pragma GCC diagnostic pop

static_assert(atoms_without_an_answer_().empty(),
              std::string_view{std::define_static_string(
                  "fixy/Collision.h: these atoms have no answer in the closed relation of their axis: "
                  + atoms_without_an_answer_()
                  + ".  Name each one in trust_level_of_, call_shape_reading_ or protocol_reading_, and decide "
                    "how T001, D001 or L003 reads it.")});
static_assert(::fixy::collision::IsTrustGrade<strict_pole_t<Axis::Trust>>,
              "the strict Trust pole must have a level, because a binding that says nothing about Trust reads it");
static_assert(!::fixy::collision::IsTrustGrade<int>, "a type that is not a Trust grade has no level");
static_assert(!::fixy::collision::IsTrustGrade<::fixy::atom::copy>, "an atom on another axis has no Trust level");

// The CallShape relation and the Protocol relation name the strict pole of
// fixy/Axis.h by its type, and these pins hold the two to that pole.  A
// type of another axis gets no reading.
static_assert(::fixy::collision::detail::call_shape_reading_(axis_record(Axis::CallShape).strict)
                  == ::fixy::collision::detail::CallShapeReading::NoIndirectCall,
              "the strict CallShape pole calls through no pointer, and a binding that says nothing reads it");
static_assert(::fixy::collision::detail::protocol_reading_(axis_record(Axis::Protocol).strict)
                  == ::fixy::collision::detail::ProtocolReading::NoSpawn,
              "the strict Protocol pole states no spawn, and a binding that says nothing reads it");
static_assert(::fixy::collision::detail::call_shape_reading_(^^int)
                  == ::fixy::collision::detail::CallShapeReading::Unread,
              "int is no CallShape grade");
static_assert(::fixy::collision::detail::protocol_reading_(^^::fixy::atom::copy)
                  == ::fixy::collision::detail::ProtocolReading::Unread,
              "an atom on another axis has no Protocol reading");
static_assert(!pack_verdict_v<::fixy::atom::dispatch::recurses<4>>.premises.indirect_call_may_throw,
              "a recursion is no call through a pointer");
static_assert(
    !pack_verdict_v<::fixy::atom::dispatch::recurses_unbounded, ::fixy::atom::cost_linear<4>>.premises.indirect_call_may_throw,
    "a recursion with no depth bound is no call through a pointer either");

// The observable reader of P010 reads the row of an Effect grade through
// the closed relation of fixy/Atom.h.  A stated with<IO> answers as the row
// Row<IO> does, and a type that is not an Effect grade has no answer.
static_assert(::fixy::collision::detail::row_admits_observable_<::fixy::atom::with_io>::value,
              "a stated with<IO> names the observable effect IO, as the row Row<IO> does");
static_assert(!::fixy::collision::detail::row_admits_observable_<::fixy::atom::with_bg>::value,
              "Bg is left out of the set of P010");
static_assert(!std::meta::can_substitute(^^::fixy::collision::detail::row_admits_observable_, {^^int}),
              "a type that is not an Effect grade has no row to read");
static_assert(!std::meta::can_substitute(^^::fixy::collision::detail::row_admits_observable_, {^^::fixy::atom::copy}),
              "an atom of another axis has no row to read");

static_assert(!pack_verdict_v<::fixy::atom::coroutine, ::fixy::atom::borrow>.admits(R002));
static_assert(
    !pack_verdict_v<::fixy::atom::borrow, ::fixy::atom::with<::foundation::effects::Effect::Bg>>.admits(L007));

// M012 needs all three premises, so the atomic representation is what
// rescues it.  That is the rule's whole content.
static_assert(!pack_verdict_v<::fixy::atom::mut_monotonic, ::fixy::atom::coroutine>.admits(M012));
static_assert(pack_verdict_v<::fixy::atom::mut_monotonic, ::fixy::atom::coroutine,
                         ::fixy::atom::repr<::fixy::pole::ReprKind::Atomic>>.admits(M012));

// P002 reaches the two emitting axes, and a call through the vDSO lifts
// the empty row, so the pair it refuses is one P010 admits.  Both halves
// alone are fine.  A stdio write lifts IO and Block, so P010 refuses a
// ghost write as well.
namespace p002_cells {
using VdsoRead = ::fixy::atom::syscall::per<::fixy::atom::syscall::SyscallId::clock_gettime>;
using StdoutWrite = ::fixy::atom::stdio::write<::fixy::atom::stdio::streams::Stdout>;
static_assert(!pack_verdict_v<::fixy::atom::ghost, VdsoRead>.admits(P002));
static_assert(pack_verdict_v<::fixy::atom::ghost, VdsoRead>.admits(P010),
              "P002 must be the rule that catches this pair; if P010 already did, P002 would be redundant");
static_assert(pack_verdict_v<VdsoRead>.admits(P002));
static_assert(pack_verdict_v<::fixy::atom::ghost>.admits(P002));
static_assert(!pack_verdict_v<::fixy::atom::ghost, StdoutWrite>.admits(P002)
              && !pack_verdict_v<::fixy::atom::ghost, StdoutWrite>.admits(P010));
}  // namespace p002_cells

// The constant-time family.  constant_time alone trips nothing, and each
// rule needs its second premise.
namespace ct_cells {
namespace at = ::fixy::atom;
namespace fp = ::fixy::atom::fp;
using CT = at::constant_time;
using FlushBoth =
    fp::mode<::foundation::core::FpDenormalInput::DenormalsAreZero, ::foundation::core::FpFtz::FlushToZero>;

static_assert(pack_verdict_v<CT>.is_valid());
static_assert(pack_verdict_v<CT, FlushBoth>.is_valid(), "a constant-time mode that names both flushes is legal");

static_assert(
    !pack_verdict_v<CT,
                fp::mode<fp::FpReassociate::UnrestrictedRewrite, ::foundation::core::FpDenormalInput::DenormalsAreZero,
                         ::foundation::core::FpFtz::FlushToZero>>.admits(F103));
static_assert(
    pack_verdict_v<CT, fp::mode<fp::FpReassociate::BoundedTreeDepth, ::foundation::core::FpDenormalInput::DenormalsAreZero,
                            ::foundation::core::FpFtz::FlushToZero>>.is_valid(),
    "a tree of bounded depth keeps its topology apart from the data, so F103 admits it");
static_assert(pack_verdict_v<fp::mode<fp::FpReassociate::UnrestrictedRewrite>>.admits(F103),
              "no timing claim, no F103");

static_assert(!pack_verdict_v<CT, fp::mode<::foundation::core::FpFtz::FlushToZero>>.admits(F104),
              "an unnamed setting honours denormals");
static_assert(!pack_verdict_v<CT, fp::mode<::foundation::core::FpDenormalInput::HonorDenormals,
                                       ::foundation::core::FpFtz::FlushToZero>>.admits(F104));
static_assert(pack_verdict_v<CT, fp::mode<::foundation::core::FpDenormalInput::DenormalsAreZero,
                                      ::foundation::core::FpFtz::FlushToZero>>.admits(F104));
static_assert(!pack_verdict_v<CT, fp::mode<::foundation::core::FpDenormalInput::DenormalsAreZero>>.admits(F105));
static_assert(!pack_verdict_v<CT, fp::mode<::foundation::core::FpFtz::PreserveSubnormals,
                                       ::foundation::core::FpDenormalInput::DenormalsAreZero>>.admits(F105));
static_assert(pack_verdict_v<CT>.admits(F104) && pack_verdict_v<CT>.admits(F105),
              "no FP mode stated, no FP claim to refuse");
static_assert(pack_verdict_v<fp::mode<>>.admits(F104) && pack_verdict_v<fp::mode<>>.admits(F105), "no timing claim");

static_assert(!pack_verdict_v<CT, at::coroutine>.admits(E044));
static_assert(!pack_verdict_v<CT, at::ctrl::coroutine<at::ctrl::async_task>>.admits(E044));
static_assert(!pack_verdict_v<CT, at::with<::foundation::effects::Effect::Bg>>.admits(E044));
static_assert(pack_verdict_v<CT, at::with<>>.admits(E044));

static_assert(!pack_verdict_v<CT, at::stale_to<1>>.admits(S010));
static_assert(pack_verdict_v<at::stale_to<1>>.admits(S010));

struct session_witness final {};
using Session = at::protocol<session_witness>;
static_assert(!pack_verdict_v<at::coroutine, Session>.admits(I004), "the strict Security pole is classified");
static_assert(!pack_verdict_v<at::as_secret, at::ctrl::coroutine<at::ctrl::async_task>, Session>.admits(I004));
static_assert(pack_verdict_v<CT, at::coroutine, Session>.admits(I004),
              "I004 stands down for constant time; E044 fires");
static_assert(!pack_verdict_v<CT, at::coroutine, Session>.admits(E044));
static_assert(pack_verdict_v<at::as_public, at::coroutine, Session>.is_valid());
static_assert(pack_verdict_v<at::as_internal, at::coroutine, Session>.is_valid(), "internal is below the carrier");
static_assert(
    pack_verdict_v<at::declassify<::fixy::tags::secret_policy::WireSerialize>, at::coroutine, Session>.is_valid());
static_assert(
    pack_verdict_v<at::declassify<::fixy::tags::secret_policy::AuditedLogging>, at::coroutine, Session>.is_valid(),
    "each policy whose mask names IO licenses the send");
static_assert(
    !pack_verdict_v<at::declassify<::fixy::tags::secret_policy::HashForCompare>, at::coroutine, Session>.admits(I004),
    "a policy that names no channel leaves the send classified");
static_assert(
    !pack_verdict_v<at::declassify<::fixy::tags::secret_policy::AuthorizedReplay>, at::coroutine, Session>.admits(I004),
    "the replay policy licenses no export");
static_assert(pack_verdict_v<at::coroutine, at::protocol<::fixy::pole::proto::None>>.is_valid(), "no session");
static_assert(pack_verdict_v<Session>.is_valid(), "a synchronous session sends at no time the scheduler chooses");

// The failure family.  The payload half stands down under void, so these
// cells name the payload.
struct plain_error final {};
using SecretError = ::fixy::Secret<plain_error>;
using ::fixy::collision::binding_verdict_v;

static_assert(!binding_verdict_v<std::expected<int, plain_error>>.admits(I002),
              "the strict Security pole is classified");
static_assert(!binding_verdict_v<int, at::ctrl::throws<>>.admits(I002),
              "an exception family the reader cannot see is plain");
static_assert(
    !binding_verdict_v<::fixy::DetSafe<::fixy::DetSafeTier_v::Pure, std::expected<int, plain_error>>>.admits(I002),
    "a band does not hide the failure it grades");
static_assert(binding_verdict_v<std::expected<int, SecretError>>.is_valid());
static_assert(binding_verdict_v<int, at::ctrl::throws<SecretError>>.is_valid());
static_assert(binding_verdict_v<std::expected<int, plain_error>, at::as_public>.is_valid());
static_assert(binding_verdict_v<std::expected<int, plain_error>, at::as_internal>.is_valid());
static_assert(binding_verdict_v<::fixy::Secret<std::expected<int, plain_error>>>.is_valid(),
              "the error inside a Secret is secret, and the discriminant with it");
static_assert(pack_verdict_v<at::ctrl::throws<SecretError>>.is_valid());
static_assert(
    !binding_verdict_v<int, at::declassify<::fixy::tags::secret_policy::WireSerialize>, at::ctrl::throws<plain_error>>.admits(
        I002),
    "no policy licenses the failure channel");
static_assert(
    !binding_verdict_v<int, at::declassify<::fixy::tags::secret_policy::AuditedLogging>, at::ctrl::throws<>>.admits(
        I002),
    "no policy licenses a thrown failure");
static_assert(
    binding_verdict_v<int, at::declassify<::fixy::tags::secret_policy::AuditedLogging>, at::ctrl::throws<SecretError>>.is_valid(),
    "a Secret error stays classified on the failure channel");

static_assert(!binding_verdict_v<std::expected<int, SecretError>, CT>.admits(I003));
static_assert(binding_verdict_v<std::expected<int, SecretError>, CT>.admits(I002), "a Secret error isolates I003");
static_assert(!binding_verdict_v<int, CT, at::ctrl::throws<SecretError>>.admits(I003));
static_assert(binding_verdict_v<int, CT>.is_valid());
}  // namespace ct_cells

// The session family.  R004 needs three premises: a live handle, a
// suspension on either axis, and a usage weaker than linear.  Each cell
// removes one premise and the rule stands down.
namespace session_cells {
namespace at = ::fixy::atom;
using ::fixy::collision::binding_verdict_v;

struct handshake final {};
using Live = at::session::live_handle<handshake>;
using Suspends = at::ctrl::coroutine<at::ctrl::async_task>;

static_assert(!pack_verdict_v<Live, Suspends, at::affine>.admits(R004));
static_assert(!pack_verdict_v<Live, at::coroutine, at::copy>.admits(R004), "a suspension on Reentrancy counts too");
static_assert(pack_verdict_v<Live, Suspends>.admits(R004), "a linear frame is resumed exactly once");
static_assert(pack_verdict_v<Live, at::affine>.admits(R004), "no suspension, so no continuation");
static_assert(pack_verdict_v<Suspends, at::affine>.admits(R004), "no handle in the frame");
static_assert(pack_verdict_v<at::protocol<handshake>, Suspends, at::affine>.admits(R004),
              "a protocol grade says the binding speaks a protocol, not that its frame holds a handle");

// A live handle is a session, so I004 reads it as one.
static_assert(!pack_verdict_v<Live, Suspends>.admits(I004), "the strict Security pole is classified");
static_assert(pack_verdict_v<Live, Suspends, at::as_public>.is_valid());

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

static_assert(!binding_verdict_v<owes_a_step, Suspends, at::affine>.admits(R004));
static_assert(binding_verdict_v<owes_a_step, Suspends>.admits(R004), "a linear frame is resumed exactly once");
static_assert(binding_verdict_v<at_the_end, Suspends, at::affine>.admits(R004), "a handle at End owes nothing");
static_assert(!binding_verdict_v<stepping_with_no_answer, Suspends, at::affine>.admits(R004),
              "a Stepping type that does not answer is read as live");
static_assert(binding_verdict_v<graded_not_stepping, Suspends, at::affine>.admits(R004),
              "another modality is not a session");
static_assert(
    !binding_verdict_v<::fixy::DetSafe<::fixy::DetSafeTier_v::Pure, owes_a_step>, Suspends, at::affine>.admits(R004),
    "a band does not hide the handle it grades");
static_assert(pack_verdict_v<Suspends, at::affine>.admits(R004),
              "the payload half stands down under the pack-only view");

// W003 needs three premises: the hot regime, a live handle, and no stated
// wait.  A stated wait moves the judgement to W001, which refuses a
// kernel wait and admits a spin.
struct hot_invariant final {};
template <class... Extra>
constexpr const ::fixy::collision::binding_verdict& hot_rules =
    pack_verdict_v<at::regime::hot, at::cost_constant, at::refined_with<hot_invariant>, at::as_public, Extra...>;
static_assert(!hot_rules<Live>.admits(W003), "a hot handle with no stated wait");
static_assert(hot_rules<Live, at::sync::spin_pause>.is_valid(), "a stated spin is the hot-path wait");
static_assert(hot_rules<Live, at::sync::bounded_spin>.is_valid());
static_assert(hot_rules<Live, at::sync::umwait_c01>.is_valid(), "UMWAIT stays in user space, as W001 reads it");
static_assert(hot_rules<Live, at::sync::park>.admits(W003) && !hot_rules<Live, at::sync::park>.admits(W001),
              "a stated kernel wait is W001's refusal, not W003's");
static_assert(pack_verdict_v<Live>.admits(W003), "not hot, so the wait is the regime's business");
static_assert(hot_rules<>.admits(W003), "no handle, so no transport wait");
// A type on the axis that names no strategy is not an atom of the
// catalog, so no rule reads it and the grade lookup refuses it.
struct names_no_strategy final : at::atom_of<Axis::Synchronization> {};
static_assert(!::fixy::atom::IsAtom<names_no_strategy>);
template <class G>
concept grade_lookup_forms = requires { typename grades<G>; };
static_assert(grade_lookup_forms<at::sync::park>);
static_assert(!grade_lookup_forms<names_no_strategy>, "a grade lookup over a type that is not an atom must not form");
static_assert(
    !binding_verdict_v<owes_a_step, at::regime::hot, at::cost_constant, at::refined_with<hot_invariant>, at::as_public>.admits(
        W003),
    "a payload that is a live handle waits as the atom does");
static_assert(
    binding_verdict_v<at_the_end, at::regime::hot, at::cost_constant, at::refined_with<hot_invariant>, at::as_public>.admits(
        W003),
    "a handle at End takes no step");
}  // namespace session_cells

// The three closed reads of the failure rules and of the instruction tier.
// Each is a concept or a function that is not a template, so no unit can
// change an answer.  The cells pin both directions of each read.
namespace closed_read_cells {
namespace at = ::fixy::atom;
namespace cd = ::fixy::collision::detail;
using Tier = at::hw::HwInstruction;

// A tier at or above its floor answers true.  An atom of another axis that
// carries a member named tier, and a type with no tier, answer false.
static_assert(cd::HasHwTierAtOrAbove<at::hw::privileged_msr, Tier::NonDeterministicTsc>);
static_assert(cd::HasHwTierAtOrAbove<at::hw::scalar, Tier::Scalar>);
static_assert(!cd::HasHwTierAtOrAbove<at::hw::vectorizable, Tier::PrivilegedMsr>);
static_assert(!cd::HasHwTierAtOrAbove<int, Tier::Scalar>);
static_assert(!cd::HasHwTierAtOrAbove<at::barrier::full_fence, Tier::Scalar>);

// Only ctrl::throws throws.  Its error is the exception family, and a Secret
// family keeps the error classified.
struct plain_error final {};
static_assert(cd::thrown_failure_of(^^at::ctrl::throws<>).fails);
static_assert(cd::thrown_failure_of(^^at::ctrl::throws<plain_error>).carries_error);
static_assert(!cd::thrown_failure_of(^^at::ctrl::throws<plain_error>).error_is_secret);
static_assert(cd::thrown_failure_of(^^at::ctrl::throws<::fixy::Secret<plain_error>>).error_is_secret);
static_assert(!cd::thrown_failure_of(^^at::ctrl::coroutine<at::ctrl::async_task>).fails);
static_assert(!cd::thrown_failure_of(axis_record(Axis::ControlFlow).strict).fails,
              "the strict ControlFlow pole throws nothing");

// The read goes through each band, and it stops at the first type that is
// not a band.  A wrapper that is not a band stays in place.
static_assert(cd::unbanded_of(^^::fixy::DetSafe<::fixy::DetSafeTier_v::Pure, ::foundation::core::Option<int>>)
              == ^^::foundation::core::Option<int>);
static_assert(
    cd::unbanded_of(^^::fixy::Vendor<::fixy::VendorBackend_v::NV, ::fixy::DetSafe<::fixy::DetSafeTier_v::Pure, int>>)
    == ^^int);
static_assert(cd::unbanded_of(^^int) == ^^int);
static_assert(cd::unbanded_of(^^::fixy::Secret<int>) == ^^::fixy::Secret<int>,
              "a Secret is no band, so its value stays inside it");
}  // namespace closed_read_cells

}  // namespace detail::collision_self_test

}  // namespace fixy
