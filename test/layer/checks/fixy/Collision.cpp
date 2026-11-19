// The compile-time checks of fixy/Collision.h.

#include <fixy/Collision.h>
// The real session handles, for the cell of the payload that owes a protocol.
#include <fixy/session/Handle.h>

#include <cstddef>
#include <expected>
#include <meta>
#include <string>
#include <string_view>

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

// A pack names an axis when an atom of the pack states it, and the record
// counts those atoms.  The count gives the grade lookup, the rules and fn one
// answer, and it agrees with the question whether the grade is the strict
// pole only when no strict pole is an atom.  The first part of the pin covers
// each instantiation of each parametric atom, because each atom of the
// catalog is an atom and no pole is one.  The second part asks each atom of
// the roster about the pole of its own axis.
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wshadow"

// The first strict pole that is an atom, or the first atom of the roster
// that is the strict pole of its axis, or the null reflection.
[[nodiscard]] consteval std::meta::info first_pole_that_is_an_atom_() {
    namespace cd = ::fixy::collision::detail;
    template for (constexpr std::meta::info axis_member :
                  std::define_static_array(std::meta::enumerators_of(^^::fixy::Axis))) {
        const std::meta::info strict = axis_record([:axis_member:]).strict;
        if (strict != std::meta::info{} && cd::is_atom_(strict)) return strict;
    }
    template for (constexpr auto member : ::fixy::atom::detail::roster_members_v<::fixy::collision::all_atom_roster>) {
        using Atom = [:member:];
        const std::meta::info strict = axis_record(Atom::axis).strict;
        if (strict != std::meta::info{} && std::meta::dealias(member) == std::meta::dealias(strict)) return member;
    }
    return {};
}

#pragma GCC diagnostic pop

// A pack that names such an atom states its axis and leaves the grade at the
// pole, so the count of the atoms on an axis no longer says whether the grade
// is the pole.  The message names the pole or the atom.
static_assert(first_pole_that_is_an_atom_() == std::meta::info{},
              std::meta::display_string_of(first_pole_that_is_an_atom_() == std::meta::info{}
                                               ? ^^void
                                               : first_pole_that_is_an_atom_()));

// Each Row axis of the table has an atom over a row, so the walk of a pack
// can give the union of two atoms on it.  An axis of each other shape has
// no row in the record.
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wshadow"

[[nodiscard]] consteval bool every_row_axis_has_an_atom_over_its_row_() {
    namespace cd = ::fixy::collision::detail;
    std::size_t row_axes = 0;
    bool is_each_answered = true;
    template for (constexpr auto axis_member : std::define_static_array(std::meta::enumerators_of(^^::fixy::Axis))) {
        constexpr Axis axis = [:axis_member:];
        if constexpr (axis_record(axis).shape == Shape::Row) {
            ++row_axes;
            constexpr std::meta::info empty = cd::row_axis_atom_over_(axis, ^^::foundation::effects::Row<>);
            using Empty = [:empty:];
            is_each_answered = is_each_answered
                            && cd::record_of_key_(^^::fixy::collision::pack_key<Empty>).row[std::to_underlying(axis)]
                                   == std::meta::dealias(^^::foundation::effects::Row<>);
        } else {
            is_each_answered = is_each_answered
                            && cd::record_of_key_(^^::fixy::collision::pack_key<>).row[std::to_underlying(axis)]
                                   == std::meta::info{};
        }
    }
    return is_each_answered && row_axes == count_axes_of_shape(Shape::Row);
}

#pragma GCC diagnostic pop

static_assert(every_row_axis_has_an_atom_over_its_row_(),
              "fixy/Collision.h: a Row axis of fixy/Axis.h has no atom over its row in row_axis_atom_over_, or "
              "the record holds a row for an axis that is not a Row axis.");

static_assert(!pack_verdict_v<::fixy::atom::dispatch::recurses<4>>.premises.indirect_call_may_throw,
              "a recursion is no call through a pointer");
static_assert(
    !pack_verdict_v<::fixy::atom::dispatch::recurses_unbounded, ::fixy::atom::cost_linear<4>>.premises.indirect_call_may_throw,
    "a recursion with no depth bound is no call through a pointer either");

static_assert(!::fixy::atom::IsEffectGrade<int> && !::fixy::atom::IsEffectGrade<::fixy::atom::copy>,
              "a type that is not an Effect grade has no row to read");

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
// A Result returns its failure as the std spelling does, and the cells of
// the readers of fixy/Collision.h read the std spelling.
using PlainResult = ::foundation::core::Result<int, plain_error>;

static_assert(!binding_verdict_v<PlainResult>.admits(I002), "the strict Security pole is classified");
static_assert(!binding_verdict_v<int, at::ctrl::throws<>>.admits(I002),
              "an exception family the reader cannot see is plain");
static_assert(!binding_verdict_v<::fixy::DetSafe<::fixy::DetSafeTier_v::Pure, PlainResult>>.admits(I002),
              "a band does not hide the failure it grades");
static_assert(binding_verdict_v<std::expected<int, SecretError>>.is_valid());
static_assert(binding_verdict_v<int, at::ctrl::throws<SecretError>>.is_valid());
static_assert(binding_verdict_v<PlainResult, at::as_public>.is_valid());
static_assert(binding_verdict_v<PlainResult, at::as_internal>.is_valid());
static_assert(binding_verdict_v<::fixy::Secret<PlainResult>>.is_valid(),
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

// ---------------------------------------------------------------------
// The cells of the readers.
//
// Each reader of a grade, a payload, a record or a verdict in
// fixy/Collision.h is a concept or a function.  Neither holds an armed cell
// of foundation/contracts/Armed.h, and the walk of test_armed_roster.cpp
// finds class templates only.  So the cell of a reader here is the variable
// cell_NAME, where NAME is the name of the reader.  The cell holds the
// answers that the reader must give: each answer in accepts is true, each
// answer in refuses is false, and neither list is empty.  The key is the
// name, because two function templates as template arguments of a key do
// not compare as two different keys on this compiler.
//
// The walk at the foot lists each concept, function and function template of
// fixy::collision and fixy::collision::detail that fixy/Collision.h or a
// source file declares.  It refuses a reader with no cell, a cell that does
// not hold, a cell that names no walked reader, and a row of the helpers that
// names no walked entity or that names an entity with a cell.  So a new
// reader fails this file until it has a cell, or until a row of the helpers
// names it with its reason.
namespace reader_cells {
namespace at = ::fixy::atom;
namespace cd = ::fixy::collision::detail;
namespace col = ::fixy::collision;
namespace lat = ::foundation::algebra::lattices;
namespace sess = ::fixy::session;
using Effect = ::foundation::effects::Effect;
template <Effect... Es>
using Row = ::foundation::effects::Row<Es...>;
using Tier = at::hw::HwInstruction;
using Option = ::foundation::core::Option<int>;
template <class T>
using Pure = ::fixy::DetSafe<::fixy::DetSafeTier_v::Pure, T>;
template <class T>
using UnderVendor = ::fixy::Vendor<::fixy::VendorBackend_v::NV, T>;

// The answers of one list.  A list holds when it has at least one answer
// and each answer is the one that the list asks for.
struct answer_list {
    unsigned count = 0;
    bool holds = false;
};

template <bool... Answers>
inline constexpr answer_list each_true{sizeof...(Answers), sizeof...(Answers) > 0 && (Answers && ...)};

template <bool... Answers>
inline constexpr answer_list each_false{sizeof...(Answers), sizeof...(Answers) > 0 && (!Answers && ...)};

struct reader_cell {
    answer_list accepts{};
    answer_list refuses{};

    [[nodiscard]] consteval bool holds() const noexcept { return accepts.holds && refuses.holds; }
    [[nodiscard]] consteval unsigned answers() const noexcept { return accepts.count + refuses.count; }
};

struct global_tag final {};
struct plain {
    int value = 0;
};
struct plain_error final {};
using SecretError = ::fixy::Secret<plain_error>;
struct names_noexcept_signature {
    using signature = void(void*) noexcept;
};
struct names_data_signature {
    using signature = int;
};
struct names_class_signature {
    using signature = plain;
};
struct names_int_strategy {
    static constexpr int strategy = 0;
};
// An atom of the axis with no strategy member.  It is not an atom of the
// catalog, and a reader of a wait strategy still reads it as no wait.
struct names_no_strategy final : at::atom_of<Axis::Synchronization> {};
// A Stepping payload before its end owes its protocol, and one at its end
// owes nothing.  A Stepping payload that does not answer is read as live.
struct live_stepper {
    static constexpr ::foundation::algebra::ModalityKind modality = ::foundation::algebra::ModalityKind::Stepping;
    [[nodiscard]] static consteval bool is_terminal() noexcept { return false; }
};
struct ended_stepper {
    static constexpr ::foundation::algebra::ModalityKind modality = ::foundation::algebra::ModalityKind::Stepping;
    [[nodiscard]] static consteval bool is_terminal() noexcept { return true; }
};
struct silent_stepper {
    static constexpr ::foundation::algebra::ModalityKind modality = ::foundation::algebra::ModalityKind::Stepping;
};
struct graded_not_stepping {
    static constexpr ::foundation::algebra::ModalityKind modality = ::foundation::algebra::ModalityKind::Absolute;
    [[nodiscard]] static consteval bool is_terminal() noexcept { return false; }
};
// The real session handles: one before its end in each direction, and one
// at its end.
using HandleSend = sess::SessionHandle<sess::Send<int, sess::End>, plain>;
using HandleRecv = sess::SessionHandle<sess::Recv<int, sess::End>, plain>;
using HandleEnd = sess::SessionHandle<sess::End, plain>;

// ── The record and the row ─────────────────────────────────────────

inline constexpr reader_cell cell_row_axis_family_{
    .accepts = each_true<(cd::row_axis_family_(Axis::Effect) == ^^at::with),
                         (cd::row_axis_family_(Axis::Observability) == ^^at::observe::surface)>,
    .refuses = each_false<(cd::row_axis_family_(Axis::Effect) == ^^at::observe::surface)>};

inline constexpr reader_cell cell_row_of_row_atom_{
    .accepts = each_true<(cd::row_of_row_atom_(Axis::Effect, ^^at::with_io) == std::meta::dealias(^^Row<Effect::IO>)),
                         (cd::row_of_row_atom_(Axis::Observability, ^^at::observe::surface<Effect::Bg>)
                          == std::meta::dealias(^^Row<Effect::Bg>)),
                         (cd::row_of_row_atom_(Axis::Effect, ^^at::with<>) == std::meta::dealias(^^Row<>))>,
    .refuses = each_false<(cd::row_of_row_atom_(Axis::Effect, ^^at::with_io) == std::meta::dealias(^^Row<>))>};

inline constexpr reader_cell cell_row_axis_atom_over_{
    .accepts =
        each_true<(cd::row_axis_atom_over_(Axis::Effect, ^^Row<Effect::IO>) == std::meta::dealias(^^at::with_io)),
                  (cd::row_axis_atom_over_(Axis::Observability, ^^Row<Effect::Bg>)
                   == std::meta::dealias(^^at::observe::surface<Effect::Bg>))>,
    .refuses = each_false<(cd::row_axis_atom_over_(Axis::Observability, ^^Row<Effect::IO>)
                           == std::meta::dealias(^^at::with_io))>};

inline constexpr reader_cell cell_axis_has_an_atom_{
    .accepts = each_true<cd::axis_has_an_atom_<Axis::Usage>(), cd::axis_has_an_atom_<Axis::Observability>()>,
    .refuses = each_false<cd::axis_has_an_atom_<Axis::Type>()>};

inline constexpr reader_cell cell_names_axis_{
    .accepts = each_true<cd::names_axis_(cd::record_of_key_(^^col::pack_key<at::copy>), Axis::Usage),
                         cd::names_axis_(cd::record_of_key_(^^col::pack_key<at::with<>>), Axis::Effect)>,
    .refuses = each_false<cd::names_axis_(cd::record_of_key_(^^col::pack_key<>), Axis::Usage),
                          cd::names_axis_(cd::record_of_key_(^^col::pack_key<at::copy>), Axis::Effect)>};

// The record of a pack: the walk of the key, the sealed cell, and the reads
// that refuse a cell of another key.  Each atom states its axis, and a type
// that is not an atom states none.
inline constexpr reader_cell cell_atom_axis_of_{
    .accepts =
        each_true<(cd::atom_axis_of_(^^at::copy) == Axis::Usage), (cd::atom_axis_of_(^^at::with_io) == Axis::Effect)>,
    .refuses = each_false<(cd::atom_axis_of_(^^at::copy) == Axis::Effect)>};

inline constexpr reader_cell cell_is_atom_{
    .accepts = each_true<cd::is_atom_(^^at::copy), cd::is_atom_(^^at::observe::surface<Effect::IO>)>,
    .refuses = each_false<cd::is_atom_(^^int), cd::is_atom_(^^names_no_strategy)>};

inline constexpr reader_cell cell_copies_in_key_{
    .accepts = each_true<(cd::copies_in_key_(std::meta::dealias(^^col::pack_key<at::copy, at::copy>), ^^at::copy) == 2),
                         (cd::copies_in_key_(std::meta::dealias(^^col::pack_key<at::copy>), ^^at::copy) == 1)>,
    .refuses = each_false<(cd::copies_in_key_(std::meta::dealias(^^col::pack_key<at::copy>), ^^at::affine) != 0)>};

// The walk reads the arguments after the payload, and an alias of a key
// reads as the key that it names.
inline constexpr reader_cell cell_read_pack_of_key_{
    .accepts = each_true<
        cd::read_pack_of_key_(^^col::pack_key<at::copy>).atoms_on[std::to_underlying(Axis::Usage)] == 1,
        cd::read_pack_of_key_(^^col::pack_key<at::copy, at::affine>).is_graded_twice[std::to_underlying(Axis::Usage)],
        (cd::read_pack_of_key_(^^col::pack_key<int>).first_malformed == ^^int),
        (cd::read_pack_of_key_(^^col::verdict_key<int, at::copy>).atoms_on[std::to_underlying(Axis::Usage)] == 1)>,
    .refuses = each_false<cd::read_pack_of_key_(^^col::pack_key<>).atoms_on[std::to_underlying(Axis::Usage)] == 1,
                          cd::read_pack_of_key_(^^col::pack_key<at::with_io, at::with_bg>)
                              .is_graded_twice[std::to_underlying(Axis::Effect)],
                          (cd::read_pack_of_key_(^^col::pack_key<at::copy>).first_malformed != ^^void)>};

inline constexpr reader_cell cell_build_pack_record_cell{
    .accepts = each_true<
        (cd::build_pack_record_cell(^^col::pack_key<at::copy>).key() == std::meta::dealias(^^col::pack_key<at::copy>)),
        cd::build_pack_record_cell(^^col::pack_key<at::copy>).value().atoms_on[std::to_underlying(Axis::Usage)] == 1>,
    .refuses =
        each_false<cd::build_pack_record_cell(^^col::pack_key<>).value().atoms_on[std::to_underlying(Axis::Usage)]
                   == 1>};

inline constexpr reader_cell cell_record_of_{
    .accepts = each_true<cd::record_of_(cd::pack_record_cell_v<col::pack_key<at::copy>>, ^^col::pack_key<at::copy>)
                             .atoms_on[std::to_underlying(Axis::Usage)]
                         == 1>,
    .refuses = each_false<cd::record_of_(cd::pack_record_cell_v<col::pack_key<>>, ^^col::pack_key<>)
                              .atoms_on[std::to_underlying(Axis::Usage)]
                          == 1>};

inline constexpr reader_cell cell_record_of_key_{
    .accepts =
        each_true<cd::record_of_key_(^^col::pack_key<at::copy>).atoms_on[std::to_underlying(Axis::Usage)] == 1,
                  (cd::record_of_key_(^^col::pack_key<at::copy, at::affine>).atoms_on[std::to_underlying(Axis::Usage)]
                   == 2)>,
    .refuses = each_false<cd::record_of_key_(^^col::pack_key<>).atoms_on[std::to_underlying(Axis::Usage)] == 1>};

// The set of P010 is the observable effects of Effect.h without Bg.
inline constexpr reader_cell cell_IsObservableWithoutBg{
    .accepts = each_true<cd::IsObservableWithoutBg<Effect::Alloc>, cd::IsObservableWithoutBg<Effect::IO>,
                         cd::IsObservableWithoutBg<Effect::Block>, cd::IsObservableWithoutBg<Effect::Div>>,
    .refuses = each_false<cd::IsObservableWithoutBg<Effect::Bg>, cd::IsObservableWithoutBg<Effect::Init>,
                          cd::IsObservableWithoutBg<Effect::Test>, cd::IsObservableWithoutBg<Effect::Nondet>>};

// The observable reader of P010 reads a canonical Row, and it leaves Bg out
// of its set.  A stated with<IO> answers as the row Row<IO> does.
inline constexpr reader_cell cell_names_observable_effect_without_bg_{
    .accepts =
        each_true<cd::names_observable_effect_without_bg_(std::meta::dealias(^^Row<Effect::Alloc>)),
                  cd::names_observable_effect_without_bg_(std::meta::dealias(^^Row<Effect::IO>)),
                  cd::names_observable_effect_without_bg_(std::meta::dealias(^^Row<Effect::Block>)),
                  cd::names_observable_effect_without_bg_(std::meta::dealias(^^at::effect_row_of_t<at::with_io>))>,
    .refuses =
        each_false<cd::names_observable_effect_without_bg_(std::meta::dealias(^^Row<Effect::Bg>)),
                   cd::names_observable_effect_without_bg_(std::meta::dealias(^^Row<>)),
                   cd::names_observable_effect_without_bg_(std::meta::dealias(^^at::effect_row_of_t<at::with_bg>)),
                   cd::names_observable_effect_without_bg_(std::meta::dealias(^^at::effect_row_of_t<at::with<>>))>};

// ── The bands of the payload ───────────────────────────────────────
//
// A band claims for its lattice, through the bands of the other lattices.
// The outermost band of the lattice decides.

inline constexpr reader_cell cell_IsDetSafeBand{
    .accepts = each_true<cd::IsDetSafeBand<Pure<int>>,
                         cd::IsDetSafeBand<::fixy::DetSafe<::fixy::DetSafeTier_v::MonotonicClockRead, int>>>,
    .refuses = each_false<cd::IsDetSafeBand<int>, cd::IsDetSafeBand<UnderVendor<int>>,
                          cd::IsDetSafeBand<UnderVendor<Pure<int>>>>};

inline constexpr reader_cell cell_ClaimsReplay{
    .accepts = each_true<cd::ClaimsReplay<Pure<int>>,
                         cd::ClaimsReplay<::fixy::DetSafe<::fixy::DetSafeTier_v::PhiloxRng, int>>>,
    .refuses = each_false<cd::ClaimsReplay<::fixy::DetSafe<::fixy::DetSafeTier_v::MonotonicClockRead, int>>,
                          cd::ClaimsReplay<int>>};

inline constexpr reader_cell cell_IsAllocClassBand{
    .accepts = each_true<cd::IsAllocClassBand<::fixy::alloc_class::Arena<int>>,
                         cd::IsAllocClassBand<::fixy::alloc_class::Pool<int>>>,
    .refuses = each_false<cd::IsAllocClassBand<int>, cd::IsAllocClassBand<Pure<int>>>};

inline constexpr reader_cell cell_ClaimsNoAllocation{
    .accepts = each_true<cd::ClaimsNoAllocation<::fixy::alloc_class::Pool<int>>,
                         cd::ClaimsNoAllocation<::fixy::alloc_class::Stack<int>>>,
    .refuses = each_false<cd::ClaimsNoAllocation<::fixy::alloc_class::Arena<int>>, cd::ClaimsNoAllocation<int>>};

inline constexpr reader_cell cell_IsWaitBand{
    .accepts = each_true<cd::IsWaitBand<::fixy::wait::AcquireWait<int>>, cd::IsWaitBand<::fixy::wait::SpinPause<int>>>,
    .refuses = each_false<cd::IsWaitBand<int>, cd::IsWaitBand<::fixy::alloc_class::Pool<int>>>};

inline constexpr reader_cell cell_ClaimsUserSpaceWait{
    .accepts = each_true<cd::ClaimsUserSpaceWait<::fixy::wait::SpinPause<int>>,
                         cd::ClaimsUserSpaceWait<::fixy::wait::UmwaitC01<int>>>,
    .refuses = each_false<cd::ClaimsUserSpaceWait<::fixy::wait::AcquireWait<int>>, cd::ClaimsUserSpaceWait<int>>};

[[nodiscard]] consteval bool claims_replay(std::meta::info payload) {
    return cd::outermost_band_claims_(payload, ^^cd::IsDetSafeBand, ^^cd::ClaimsReplay);
}

[[nodiscard]] consteval bool claims_no_allocation(std::meta::info payload) {
    return cd::outermost_band_claims_(payload, ^^cd::IsAllocClassBand, ^^cd::ClaimsNoAllocation);
}

inline constexpr reader_cell cell_outermost_band_claims_{
    .accepts =
        each_true<claims_replay(^^Pure<int>), claims_replay(^^::fixy::DetSafe<::fixy::DetSafeTier_v::PhiloxRng, int>),
                  claims_replay(^^UnderVendor<Pure<int>>),
                  claims_no_allocation(^^UnderVendor<::fixy::alloc_class::Stack<int>>)>,
    .refuses = each_false<claims_replay(^^int), claims_replay(^^void),
                          claims_replay(^^::fixy::DetSafe<::fixy::DetSafeTier_v::MonotonicClockRead, int>),
                          claims_replay(^^UnderVendor<int>), claims_no_allocation(^^::fixy::alloc_class::Arena<int>)>};

// ── The floors and the trunks ──────────────────────────────────────
//
// An atom of a different axis carries a member of the same name, and the
// type check of each reader refuses it.

inline constexpr reader_cell cell_HasHwTierAtOrAbove{
    .accepts = each_true<cd::HasHwTierAtOrAbove<at::hw::privileged_msr, Tier::NonDeterministicTsc>,
                         cd::HasHwTierAtOrAbove<at::hw::scalar, Tier::Scalar>>,
    .refuses = each_false<cd::HasHwTierAtOrAbove<at::hw::vectorizable, Tier::PrivilegedMsr>,
                          cd::HasHwTierAtOrAbove<int, Tier::Scalar>,
                          cd::HasHwTierAtOrAbove<at::barrier::full_fence, Tier::Scalar>>};

// The order of the fences is not a chain: an acquire load and a release
// store are not comparable, so a floor on one tag refuses the other.
inline constexpr reader_cell cell_HasBarrierAtOrAbove{
    .accepts = each_true<cd::HasBarrierAtOrAbove<at::barrier::full_fence, lat::BarrierStrength::SeqCst>,
                         cd::HasBarrierAtOrAbove<at::barrier::seq_cst, lat::BarrierStrength::AcqRel>,
                         cd::HasBarrierAtOrAbove<at::barrier::acq_rel, lat::BarrierStrength::AcquireLoad>>,
    .refuses = each_false<cd::HasBarrierAtOrAbove<at::barrier::acq_rel, lat::BarrierStrength::SeqCst>,
                          cd::HasBarrierAtOrAbove<int, lat::BarrierStrength::AcqRel>,
                          cd::HasBarrierAtOrAbove<at::hw::privileged_msr, lat::BarrierStrength::None>,
                          cd::HasBarrierAtOrAbove<at::barrier::release_store, lat::BarrierStrength::AcquireLoad>,
                          cd::HasBarrierAtOrAbove<at::barrier::acquire_load, lat::BarrierStrength::ReleaseStore>>};

inline constexpr reader_cell cell_HasMemoryScope{
    .accepts = each_true<cd::HasMemoryScope<at::scope::cta>, cd::HasMemoryScope<at::scope::system>>,
    .refuses = each_false<cd::HasMemoryScope<int>, cd::HasMemoryScope<at::barrier::seq_cst>>};

// The scope lattice has two trunks, so a host scope is not at or above an
// accelerator floor.
inline constexpr reader_cell cell_HasScopeAtOrAbove{
    .accepts = each_true<cd::HasScopeAtOrAbove<at::scope::gpu, lat::MemoryScope::Cluster>,
                         cd::HasScopeAtOrAbove<at::scope::system, lat::MemoryScope::Cta>>,
    .refuses = each_false<cd::HasScopeAtOrAbove<at::scope::inner, lat::MemoryScope::Cluster>,
                          cd::HasScopeAtOrAbove<at::scope::cta, lat::MemoryScope::Gpu>,
                          cd::HasScopeAtOrAbove<int, lat::MemoryScope::Cluster>>};

// The shared bottom and the shared top of the scope lattice pin no trunk.
inline constexpr reader_cell cell_HasPinnedScopeTrunk{
    .accepts = each_true<cd::HasPinnedScopeTrunk<at::scope::cta>, cd::HasPinnedScopeTrunk<at::scope::gpu>,
                         cd::HasPinnedScopeTrunk<at::scope::inner>>,
    .refuses = each_false<cd::HasPinnedScopeTrunk<at::scope::thread>, cd::HasPinnedScopeTrunk<at::scope::system>,
                          cd::HasPinnedScopeTrunk<int>, cd::HasPinnedScopeTrunk<at::barrier::seq_cst>>};

inline constexpr reader_cell cell_HasScopeOnHostTrunk{
    .accepts = each_true<cd::HasScopeOnHostTrunk<at::scope::inner>, cd::HasScopeOnHostTrunk<at::scope::outer>>,
    .refuses = each_false<cd::HasScopeOnHostTrunk<at::scope::cta>, cd::HasScopeOnHostTrunk<at::scope::gpu>,
                          cd::HasScopeOnHostTrunk<int>>};

inline constexpr reader_cell cell_HasSimdIsa{
    .accepts = each_true<cd::HasSimdIsa<at::simd::avx2>, cd::HasSimdIsa<at::simd::scalar>>,
    .refuses = each_false<cd::HasSimdIsa<int>, cd::HasSimdIsa<at::scope::cta>>};

// A portable or scalar ISA pins no trunk.
inline constexpr reader_cell cell_HasPinnedIsaTrunk{
    .accepts = each_true<cd::HasPinnedIsaTrunk<at::simd::avx2>, cd::HasPinnedIsaTrunk<at::simd::neon>,
                         cd::HasPinnedIsaTrunk<at::simd::sve2>>,
    .refuses = each_false<cd::HasPinnedIsaTrunk<at::simd::scalar>, cd::HasPinnedIsaTrunk<at::simd::portable>,
                          cd::HasPinnedIsaTrunk<int>>};

inline constexpr reader_cell cell_HasIsaOnArmTrunk{
    .accepts = each_true<cd::HasIsaOnArmTrunk<at::simd::neon>, cd::HasIsaOnArmTrunk<at::simd::sve2>>,
    .refuses = each_false<cd::HasIsaOnArmTrunk<at::simd::avx2>, cd::HasIsaOnArmTrunk<at::simd::sse2>,
                          cd::HasIsaOnArmTrunk<int>>};

// A mode names only the settings it was written with, and a mode of two
// settings names each of them.
using Fast = at::fp::mode<at::fp::FpContract::Fast>;
using FastRewrite = at::fp::mode<at::fp::FpContract::Fast, at::fp::FpReassociate::UnrestrictedRewrite>;
inline constexpr reader_cell cell_NamesFpSetting{
    .accepts = each_true<cd::NamesFpSetting<Fast, at::fp::FpContract::Fast>,
                         cd::NamesFpSetting<FastRewrite, at::fp::FpReassociate::UnrestrictedRewrite>,
                         cd::NamesFpSetting<FastRewrite, at::fp::FpContract::Fast>>,
    .refuses = each_false<cd::NamesFpSetting<Fast, at::fp::FpContract::Off>,
                          cd::NamesFpSetting<at::fp::mode<>, at::fp::FpContract::Fast>,
                          cd::NamesFpSetting<int, at::fp::FpContract::Fast>>};

// ── The waits ──────────────────────────────────────────────────────
//
// UMWAIT halts the core and does not spin it.  A type with no strategy, or
// with a strategy that is no WaitStrategy, names no wait.

inline constexpr reader_cell cell_BurnsTheCore{
    .accepts = each_true<cd::BurnsTheCore<at::sync::bounded_spin>, cd::BurnsTheCore<at::sync::spin_pause>>,
    .refuses =
        each_false<cd::BurnsTheCore<at::sync::umwait_c01>, cd::BurnsTheCore<at::sync::block>, cd::BurnsTheCore<int>>};

inline constexpr reader_cell cell_StatesWaitStrategy{
    .accepts = each_true<cd::StatesWaitStrategy<at::sync::block>, cd::StatesWaitStrategy<at::sync::umwait_c01>,
                         cd::StatesWaitStrategy<at::sync::spin_pause>>,
    .refuses = each_false<cd::StatesWaitStrategy<int>, cd::StatesWaitStrategy<plain>,
                          cd::StatesWaitStrategy<names_no_strategy>, cd::StatesWaitStrategy<names_int_strategy>>};

// ── The families of a grade ────────────────────────────────────────
//
// The reads of GlobalState, ControlFlow and Protocol by family.  A protocol
// grade says that a binding speaks a protocol, and it does not say that the
// frame holds a live handle.

inline constexpr reader_cell cell_is_grade_of_family_{
    .accepts =
        each_true<cd::is_grade_of_family_(^^at::global::thread_local_<global_tag>, ^^at::global::thread_local_),
                  cd::is_grade_of_family_(^^at::ctrl::longjmp_unsafe<"setjmp island">, ^^at::ctrl::longjmp_unsafe),
                  cd::is_grade_of_family_(^^at::ctrl::coroutine<at::ctrl::async_task>, ^^at::ctrl::coroutine),
                  cd::is_grade_of_family_(^^at::session::live_handle<plain>, ^^at::session::live_handle)>,
    .refuses =
        each_false<cd::is_grade_of_family_(^^int, ^^at::global::thread_local_),
                   cd::is_grade_of_family_(^^at::global::singleton<global_tag>, ^^at::global::thread_local_),
                   cd::is_grade_of_family_(^^int, ^^at::ctrl::longjmp_unsafe),
                   cd::is_grade_of_family_(^^at::ctrl::abort<"setjmp island">, ^^at::ctrl::longjmp_unsafe),
                   cd::is_grade_of_family_(^^int, ^^at::ctrl::coroutine),
                   cd::is_grade_of_family_(^^at::ctrl::longjmp_unsafe<"setjmp island">, ^^at::ctrl::coroutine),
                   cd::is_grade_of_family_(^^at::ctrl::throws<>, ^^at::ctrl::coroutine),
                   cd::is_grade_of_family_(^^int, ^^at::session::live_handle),
                   cd::is_grade_of_family_(^^at::protocol<plain>, ^^at::session::live_handle),
                   cd::is_grade_of_family_(^^at::protocol<::fixy::pole::proto::None>, ^^at::session::live_handle)>};

inline constexpr reader_cell cell_first_template_argument_{
    .accepts = each_true<(cd::first_template_argument_(^^at::dispatch::indirect_call<int(char)>) == ^^int(char)),
                         (cd::first_template_argument_(^^at::global::thread_local_<global_tag>) == ^^global_tag)>,
    .refuses = each_false<(cd::first_template_argument_(^^at::dispatch::indirect_call<int(char)>) == ^^int)>};

// protocol<proto::None> writes the strict pole out, so it names no session.
inline constexpr reader_cell cell_is_session_protocol_{
    .accepts = each_true<cd::is_session_protocol_(^^at::protocol<plain>),
                         cd::is_session_protocol_(^^at::session::live_handle<plain>)>,
    .refuses =
        each_false<cd::is_session_protocol_(^^int), cd::is_session_protocol_(^^at::protocol<::fixy::pole::proto::None>),
                   cd::is_session_protocol_(^^at::spawn::detach_with<"no join">)>};

// ── The calls and the spawns ───────────────────────────────────────
//
// A family names a noexcept signature through a function type, a pointer or
// member pointer to one, or a signature member that is not a class.  A
// family that names no function answers false.

inline constexpr reader_cell cell_NamesSignatureMember{
    .accepts =
        each_true<cd::NamesSignatureMember<names_noexcept_signature>, cd::NamesSignatureMember<names_data_signature>>,
    .refuses = each_false<cd::NamesSignatureMember<plain>, cd::NamesSignatureMember<int>,
                          cd::NamesSignatureMember<names_class_signature>>};

inline constexpr reader_cell cell_named_signature_of_{
    .accepts = each_true<(cd::named_signature_of_(^^void (*)() noexcept) == ^^void() noexcept),
                         (cd::named_signature_of_(^^names_noexcept_signature) == ^^void(void*) noexcept),
                         (cd::named_signature_of_(^^int (&)(char)) == ^^int(char))>,
    .refuses = each_false<(cd::named_signature_of_(^^plain) != ^^void),
                          (cd::named_signature_of_(^^names_data_signature) != ^^void)>};

inline constexpr reader_cell cell_names_noexcept_signature_{
    .accepts = each_true<cd::names_noexcept_signature_(^^void() noexcept),
                         cd::names_noexcept_signature_(^^int (*)(char, ...) noexcept),
                         cd::names_noexcept_signature_(^^void (plain::*)() & noexcept),
                         cd::names_noexcept_signature_(^^names_noexcept_signature)>,
    .refuses = each_false<cd::names_noexcept_signature_(^^void()), cd::names_noexcept_signature_(^^int (&)(char)),
                          cd::names_noexcept_signature_(^^void (plain::*)() &), cd::names_noexcept_signature_(^^plain),
                          cd::names_noexcept_signature_(^^names_data_signature)>};

inline constexpr reader_cell cell_call_shape_reading_{
    .accepts = each_true<
        (cd::call_shape_reading_(axis_record(Axis::CallShape).strict) == cd::CallShapeReading::NoIndirectCall),
        (cd::call_shape_reading_(^^at::dispatch::recurses<4>) == cd::CallShapeReading::NoIndirectCall),
        (cd::call_shape_reading_(^^at::dispatch::recurses_unbounded) == cd::CallShapeReading::NoIndirectCall),
        (cd::call_shape_reading_(^^at::dispatch::indirect_call<int(char)>) == cd::CallShapeReading::IndirectCall)>,
    .refuses = each_false<(cd::call_shape_reading_(^^int) != cd::CallShapeReading::Unread),
                          (cd::call_shape_reading_(^^at::copy) != cd::CallShapeReading::Unread)>};

// An indirect call can throw unless its family names a noexcept signature.
// An opaque tag family names no signature, so the call can throw.  A
// recursion and the strict pole call through no pointer.
inline constexpr reader_cell cell_indirect_call_throws_{
    .accepts = each_true<cd::indirect_call_throws_(^^at::dispatch::indirect_call<int(char)>),
                         cd::indirect_call_throws_(^^at::dispatch::indirect_call<plain>),
                         cd::indirect_call_throws_(^^at::dispatch::indirect_call<void (plain::*)() &>),
                         cd::indirect_call_throws_(^^at::dispatch::indirect_call<int (*)(char, ...)>)>,
    .refuses = each_false<cd::indirect_call_throws_(^^at::dispatch::indirect_call<int(char) noexcept>),
                          cd::indirect_call_throws_(^^at::dispatch::indirect_call<void (*)() noexcept>),
                          cd::indirect_call_throws_(^^at::dispatch::indirect_call<names_noexcept_signature>),
                          cd::indirect_call_throws_(^^at::dispatch::recurses<8>),
                          cd::indirect_call_throws_(^^::fixy::pole::Unconstrained<Axis::CallShape>)>};

inline constexpr reader_cell cell_call_shape_reading_or_refuse_{
    .accepts = each_true<(cd::call_shape_reading_or_refuse_<at::dispatch::indirect_call<int(char)>>()
                          == cd::CallShapeReading::IndirectCall),
                         (cd::call_shape_reading_or_refuse_<::fixy::pole::Unconstrained<Axis::CallShape>>()
                          == cd::CallShapeReading::NoIndirectCall)>,
    .refuses = each_false<(cd::call_shape_reading_or_refuse_<at::dispatch::recurses<8>>()
                           == cd::CallShapeReading::IndirectCall)>};

inline constexpr reader_cell cell_protocol_reading_{
    .accepts = each_true<
        (cd::protocol_reading_(axis_record(Axis::Protocol).strict) == cd::ProtocolReading::NoSpawn),
        (cd::protocol_reading_(^^at::protocol<plain>) == cd::ProtocolReading::NoSpawn),
        (cd::protocol_reading_(^^at::session::live_handle<plain>) == cd::ProtocolReading::NoSpawn),
        (cd::protocol_reading_(^^at::spawn::detach_with<"drain">) == cd::ProtocolReading::SpawnOutlivesTheFrame),
        (cd::protocol_reading_(^^at::spawn::subprocess<"helper">) == cd::ProtocolReading::SpawnInItsOwnSpace)>,
    .refuses = each_false<(cd::protocol_reading_(^^int) != cd::ProtocolReading::Unread),
                          (cd::protocol_reading_(^^at::copy) != cd::ProtocolReading::Unread)>};

// A subprocess has its own copy of the address space, so it cannot outlive
// the frame that it borrows from.  A protocol, a live handle and the strict
// pole state no spawn.
inline constexpr reader_cell cell_spawn_outlives_frame_{
    .accepts = each_true<cd::spawn_outlives_frame_(^^at::spawn::detach_with<"drain">),
                         cd::spawn_outlives_frame_(^^at::spawn::syscall_only<"loader">)>,
    .refuses = each_false<cd::spawn_outlives_frame_(^^at::spawn::subprocess<"helper">),
                          cd::spawn_outlives_frame_(^^at::protocol<plain>),
                          cd::spawn_outlives_frame_(^^at::session::live_handle<plain>),
                          cd::spawn_outlives_frame_(^^::fixy::pole::proto::None)>};

inline constexpr reader_cell cell_protocol_reading_or_refuse_{
    .accepts =
        each_true<(cd::protocol_reading_or_refuse_<at::spawn::detach_with<"drain">>()
                   == cd::ProtocolReading::SpawnOutlivesTheFrame),
                  (cd::protocol_reading_or_refuse_<::fixy::pole::proto::None>() == cd::ProtocolReading::NoSpawn)>,
    .refuses = each_false<(cd::protocol_reading_or_refuse_<at::protocol<plain>>()
                           == cd::ProtocolReading::SpawnOutlivesTheFrame)>};

// ── The failure path ───────────────────────────────────────────────

// The read goes through each band, and it stops at the first type that is
// not a band.  A wrapper that is not a band stays in place.
inline constexpr reader_cell cell_unbanded_of{
    .accepts = each_true<(cd::unbanded_of(^^Pure<Option>) == ^^::foundation::core::Option<int>),
                         (cd::unbanded_of(^^UnderVendor<Pure<int>>) == ^^int)>,
    .refuses = each_false<(cd::unbanded_of(^^int) != ^^int),
                          (cd::unbanded_of(^^::fixy::Secret<int>) != ^^::fixy::Secret<int>)>};

inline constexpr reader_cell cell_ReturnsExpectedFailure{
    .accepts = each_true<cd::ReturnsExpectedFailure<std::expected<int, plain_error>>>,
    .refuses = each_false<cd::ReturnsExpectedFailure<int>, cd::ReturnsExpectedFailure<Option>>};

inline constexpr reader_cell cell_is_secret_type_{
    .accepts = each_true<cd::is_secret_type_(^^SecretError)>,
    .refuses = each_false<cd::is_secret_type_(^^plain_error), cd::is_secret_type_(^^int)>};

// A Result and a std::expected carry an error value, and an Option carries
// none.  A Secret holds the discriminant and the error inside it.
inline constexpr reader_cell cell_returned_failure_of{
    .accepts = each_true<cd::returned_failure_of(^^::foundation::core::Result<int, plain_error>).carries_error,
                         cd::returned_failure_of(^^Option).fails,
                         cd::returned_failure_of(^^std::expected<int, plain_error>).fails,
                         cd::returned_failure_of(^^std::expected<int, SecretError>).error_is_secret>,
    .refuses = each_false<cd::returned_failure_of(^^int).fails,
                          cd::returned_failure_of(^^::fixy::Secret<::foundation::core::Result<int, plain_error>>).fails,
                          cd::returned_failure_of(^^Option).carries_error,
                          cd::returned_failure_of(^^::foundation::core::Result<int, plain_error>).error_is_secret>};

// Only ctrl::throws throws.  Its error is the exception family, and a Secret
// family keeps the error classified.
inline constexpr reader_cell cell_thrown_failure_of{
    .accepts = each_true<cd::thrown_failure_of(^^at::ctrl::throws<>).fails,
                         cd::thrown_failure_of(^^at::ctrl::throws<plain_error>).carries_error,
                         cd::thrown_failure_of(^^at::ctrl::throws<SecretError>).error_is_secret>,
    .refuses = each_false<cd::thrown_failure_of(^^at::ctrl::throws<plain_error>).error_is_secret,
                          cd::thrown_failure_of(^^at::ctrl::coroutine<at::ctrl::async_task>).fails,
                          cd::thrown_failure_of(axis_record(Axis::ControlFlow).strict).fails>};

// ── The Security grade, the session and the Trust grade ───────────

inline constexpr reader_cell cell_security_class_or_refuse_{
    .accepts =
        each_true<(cd::security_class_or_refuse_<at::as_secret>() == at::SecurityClass::Classified),
                  (cd::security_class_or_refuse_<strict_pole_t<Axis::Security>>() == at::SecurityClass::Classified),
                  (cd::security_class_or_refuse_<at::as_internal>() == at::SecurityClass::Internal),
                  (cd::security_class_or_refuse_<at::constant_time>() == at::SecurityClass::ConstantTime)>,
    .refuses = each_false<(cd::security_class_or_refuse_<at::as_public>() == at::SecurityClass::Classified)>};

inline constexpr reader_cell cell_IsSteppingPayload{
    .accepts = each_true<cd::IsSteppingPayload<live_stepper>, cd::IsSteppingPayload<ended_stepper>,
                         cd::IsSteppingPayload<HandleEnd>>,
    .refuses = each_false<cd::IsSteppingPayload<int>, cd::IsSteppingPayload<graded_not_stepping>>};

inline constexpr reader_cell cell_AnswersIsTerminal{
    .accepts = each_true<cd::AnswersIsTerminal<live_stepper>, cd::AnswersIsTerminal<ended_stepper>>,
    .refuses = each_false<cd::AnswersIsTerminal<silent_stepper>, cd::AnswersIsTerminal<int>>};

// A handle before End owes its protocol, a handle at End does not, and a
// value that is not a handle holds none.  A Stepping payload that does not
// answer is read as live.
inline constexpr reader_cell cell_OwesAProtocol{
    .accepts = each_true<cd::OwesAProtocol<HandleSend>, cd::OwesAProtocol<HandleRecv>, cd::OwesAProtocol<live_stepper>,
                         cd::OwesAProtocol<silent_stepper>>,
    .refuses = each_false<cd::OwesAProtocol<int>, cd::OwesAProtocol<void>, cd::OwesAProtocol<HandleEnd>,
                          cd::OwesAProtocol<ended_stepper>, cd::OwesAProtocol<plain>>};

// The pass of the premises reads the payload through its bands, and the
// Protocol grade by its family.  The Representation grade is read by
// identity.
static_assert(col::binding_verdict_v<Pure<HandleRecv>>.premises.has_live_handle
                  && col::binding_verdict_v<Pure<live_stepper>>.premises.has_live_handle,
              "the read of the payload goes through its bands");
static_assert(!col::binding_verdict_v<HandleEnd>.premises.has_live_handle
              && !col::binding_verdict_v<int>.premises.has_live_handle);
static_assert(pack_verdict_v<at::session::live_handle<plain>>.premises.has_live_handle
              && !pack_verdict_v<at::protocol<plain>>.premises.has_live_handle
              && !pack_verdict_v<at::protocol<::fixy::pole::proto::None>>.premises.has_live_handle);
static_assert(pack_verdict_v<at::repr<::fixy::pole::ReprKind::Atomic>>.premises.atomic_repr
              && !pack_verdict_v<at::repr<::fixy::pole::ReprKind::C>>.premises.atomic_repr
              && !pack_verdict_v<>.premises.atomic_repr);

// The level of a Trust grade is a tag of fixy/Tags.h: the level that a trust
// atom names, Assumed for trust_assumed, and Unverified for the strict pole.
inline constexpr reader_cell cell_trust_level_of_{
    .accepts =
        each_true<(cd::trust_level_of_(^^at::trust<::fixy::tags::trust::Tested>) == ^^::fixy::tags::trust::Tested),
                  (cd::trust_level_of_(^^at::trust_assumed<0>) == ^^::fixy::tags::trust::Assumed),
                  (cd::trust_level_of_(axis_record(Axis::Trust).strict) == ^^::fixy::tags::trust::Unverified)>,
    .refuses = each_false<(cd::trust_level_of_(^^int) != std::meta::info{}),
                          (cd::trust_level_of_(^^at::copy) != std::meta::info{})>};

inline constexpr reader_cell cell_trust_level_or_refuse_{
    .accepts =
        each_true<(cd::trust_level_or_refuse_<at::trust<::fixy::tags::trust::Verified>>()
                   == ^^::fixy::tags::trust::Verified),
                  (cd::trust_level_or_refuse_<strict_pole_t<Axis::Trust>>() == ^^::fixy::tags::trust::Unverified)>,
    .refuses = each_false<(cd::trust_level_or_refuse_<at::trust<::fixy::tags::trust::Verified>>()
                           == ^^::fixy::tags::trust::Unverified)>};

inline constexpr reader_cell cell_IsTrustGrade{
    .accepts =
        each_true<col::IsTrustGrade<strict_pole_t<Axis::Trust>>,
                  col::IsTrustGrade<at::trust<::fixy::tags::trust::Verified>>, col::IsTrustGrade<at::trust_assumed<0>>>,
    .refuses = each_false<col::IsTrustGrade<int>, col::IsTrustGrade<at::copy>>};

inline constexpr reader_cell cell_HasTrustLevel{
    .accepts = each_true<col::HasTrustLevel<at::trust<::fixy::tags::trust::Tested>, ::fixy::tags::trust::Tested>,
                         col::HasTrustLevel<at::trust_assumed<0>, ::fixy::tags::trust::Assumed>,
                         col::HasTrustLevel<strict_pole_t<Axis::Trust>, ::fixy::tags::trust::Unverified>>,
    .refuses = each_false<col::HasTrustLevel<at::trust<::fixy::tags::trust::External>, ::fixy::tags::trust::Tested>,
                          col::HasTrustLevel<at::copy, ::fixy::tags::trust::Unverified>>};

// Two surfaces over one set of effects take one spelling.
inline constexpr reader_cell cell_canonical_surface_of{
    .accepts = each_true<(col::canonical_surface_of(^^at::observe::surface<Effect::IO, Effect::Bg>)
                          == col::canonical_surface_of(^^at::observe::surface<Effect::Bg, Effect::IO>)),
                         (col::canonical_surface_of(^^at::observe::surface<Effect::IO>)
                          == std::meta::dealias(^^at::observe::surface<Effect::IO>))>,
    .refuses = each_false<(col::canonical_surface_of(^^at::observe::surface<Effect::IO>)
                           == col::canonical_surface_of(^^at::observe::surface<Effect::Bg>))>};

// ── The premises and the verdict ───────────────────────────────────

inline constexpr reader_cell cell_join_premises_{
    .accepts = each_true<
        cd::join_premises_(col::rule_premises{}, cd::payload_premises{.owes_a_protocol = true}).has_live_handle,
        cd::join_premises_(col::rule_premises{},
                           cd::payload_premises{.returned_failure = cd::ReturnedFailure{.fails = true}})
            .fails,
        cd::join_premises_(col::rule_premises{.has_live_handle = true}, cd::payload_premises{}).has_live_handle>,
    .refuses = each_false<cd::join_premises_(col::rule_premises{}, cd::payload_premises{}).has_live_handle,
                          cd::join_premises_(col::rule_premises{}, cd::payload_premises{}).fails>};

// A floor reader over a grade, one overload for each type of floor.
inline constexpr reader_cell cell_satisfies_at_{
    .accepts =
        each_true<cd::satisfies_at_(^^cd::HasHwTierAtOrAbove, ^^at::hw::privileged_msr, Tier::NonDeterministicTsc),
                  cd::satisfies_at_(^^cd::HasBarrierAtOrAbove, ^^at::barrier::seq_cst, lat::BarrierStrength::AcqRel),
                  cd::satisfies_at_(^^cd::NamesFpSetting, ^^Fast, at::fp::FpContract::Fast),
                  cd::satisfies_at_(^^at::IsClassifiedOn, ^^at::as_secret, at::DischargeAxis::IO)>,
    .refuses =
        each_false<cd::satisfies_at_(^^cd::HasBarrierAtOrAbove, ^^at::barrier::acq_rel, lat::BarrierStrength::SeqCst),
                   cd::satisfies_at_(^^cd::HasScopeAtOrAbove, ^^at::scope::inner, lat::MemoryScope::Cluster),
                   cd::satisfies_at_(^^at::IsClassifiedOn, ^^at::as_public, at::DischargeAxis::IO)>};

// The sealed caches of the premises: the pass of a pack, the pass of a
// payload, their cells, and the reads that refuse a cell of another key.
inline constexpr reader_cell cell_premises_of_pack_{
    .accepts = each_true<cd::premises_of_pack_(^^col::pack_key<at::borrow>).borrow,
                         cd::premises_of_pack_(^^col::pack_key<>).linear,
                         cd::premises_of_pack_(^^col::pack_key<at::coroutine>).concurrent>,
    .refuses = each_false<cd::premises_of_pack_(^^col::pack_key<>).borrow,
                          cd::premises_of_pack_(^^col::pack_key<at::copy>).linear>};

inline constexpr reader_cell cell_build_pack_premises_cell{
    .accepts = each_true<(cd::build_pack_premises_cell(^^col::pack_key<at::borrow>).key()
                          == std::meta::dealias(^^col::pack_key<at::borrow>)),
                         cd::build_pack_premises_cell(^^col::pack_key<at::borrow>).value().borrow>,
    .refuses = each_false<cd::build_pack_premises_cell(^^col::pack_key<>).value().borrow>};

inline constexpr reader_cell cell_pack_premises_in_{
    .accepts = each_true<
        cd::pack_premises_in_(cd::pack_premises_cell_v<col::pack_key<at::borrow>>, ^^col::pack_key<at::borrow>).borrow>,
    .refuses = each_false<cd::pack_premises_in_(cd::pack_premises_cell_v<col::pack_key<>>, ^^col::pack_key<>).borrow>};

inline constexpr reader_cell cell_pack_premises_of_key_{
    .accepts = each_true<cd::pack_premises_of_key_(^^col::pack_key<at::borrow>).borrow,
                         cd::pack_premises_of_key_(^^col::pack_key<at::ghost, at::with_io>).row_observable>,
    .refuses = each_false<cd::pack_premises_of_key_(^^col::pack_key<at::copy>).borrow,
                          cd::pack_premises_of_key_(^^col::pack_key<at::with_bg>).row_observable>};

inline constexpr reader_cell cell_premises_of_payload_{
    .accepts = each_true<cd::premises_of_payload_(^^Pure<int>).replay_deterministic,
                         cd::premises_of_payload_(^^Pure<HandleSend>).owes_a_protocol,
                         cd::premises_of_payload_(^^Option).returned_failure.fails>,
    .refuses = each_false<cd::premises_of_payload_(^^void).replay_deterministic,
                          cd::premises_of_payload_(^^HandleEnd).owes_a_protocol,
                          cd::premises_of_payload_(^^int).returned_failure.fails>};

inline constexpr reader_cell cell_build_payload_premises_cell{
    .accepts = each_true<(cd::build_payload_premises_cell(^^col::verdict_key<Pure<int>>).key()
                          == std::meta::dealias(^^col::verdict_key<Pure<int>>)),
                         cd::build_payload_premises_cell(^^col::verdict_key<Pure<int>>).value().replay_deterministic>,
    .refuses = each_false<cd::build_payload_premises_cell(^^col::verdict_key<int>).value().replay_deterministic>};

inline constexpr reader_cell cell_payload_premises_in_{
    .accepts = each_true<cd::payload_premises_in_(cd::payload_premises_cell_v<col::verdict_key<Pure<int>>>,
                                                  ^^col::verdict_key<Pure<int>>)
                             .replay_deterministic>,
    .refuses =
        each_false<cd::payload_premises_in_(cd::payload_premises_cell_v<col::verdict_key<int>>, ^^col::verdict_key<int>)
                       .replay_deterministic>};

inline constexpr reader_cell cell_payload_premises_of_key_{
    .accepts = each_true<cd::payload_premises_of_key_(^^col::verdict_key<Pure<int>>).replay_deterministic,
                         cd::payload_premises_of_key_(^^col::verdict_key<HandleSend>).owes_a_protocol>,
    .refuses = each_false<cd::payload_premises_of_key_(^^col::verdict_key<void>).replay_deterministic,
                          cd::payload_premises_of_key_(^^col::verdict_key<HandleEnd>).owes_a_protocol>};

// The premises of a binding join the premises of its pack and of its payload.
inline constexpr reader_cell cell_premises_of_binding_{
    .accepts = each_true<cd::premises_of_binding_(^^col::verdict_key<HandleSend, at::borrow>).has_live_handle,
                         cd::premises_of_binding_(^^col::verdict_key<int, at::borrow>).borrow,
                         cd::premises_of_binding_(^^col::verdict_key<Option>).fails>,
    .refuses = each_false<cd::premises_of_binding_(^^col::verdict_key<int, at::borrow>).has_live_handle,
                          cd::premises_of_binding_(^^col::pack_key<at::borrow>).fails>};

// The premises of the empty pack with a borrow, and with a borrow across a
// suspension, so that the table judges a record that a pack can give.
[[nodiscard]] consteval col::rule_premises borrow_premises_() {
    col::rule_premises premises = cd::pack_premises_of_key_(^^col::pack_key<>);
    premises.borrow = true;
    return premises;
}

[[nodiscard]] consteval col::rule_premises borrow_across_a_suspension_premises_() {
    col::rule_premises premises = borrow_premises_();
    premises.concurrent = true;
    return premises;
}

inline constexpr reader_cell cell_judge_{
    .accepts = each_true<cd::judge_(cd::pack_premises_of_key_(^^col::pack_key<>)).is_valid(),
                         cd::judge_(borrow_premises_()).is_valid(),
                         cd::judge_(borrow_across_a_suspension_premises_()).admits(col::RuleCode::M012)>,
    .refuses = each_false<cd::judge_(borrow_across_a_suspension_premises_()).admits(col::RuleCode::L002),
                          cd::judge_(borrow_across_a_suspension_premises_()).is_valid()>};

inline constexpr reader_cell cell_build_verdict_cell{
    .accepts = each_true<(col::build_verdict_cell(^^col::pack_key<at::borrow>).key()
                          == std::meta::dealias(^^col::pack_key<at::borrow>)),
                         col::build_verdict_cell(^^col::pack_key<at::borrow>).verdict().is_valid()>,
    .refuses = each_false<col::build_verdict_cell(^^col::pack_key<at::borrow, at::coroutine>).verdict().is_valid()>};

// A cell of another binding has a fixture of its own:
// neg_collision_verdict_cell_of_another_binding.
inline constexpr reader_cell cell_read_verdict{
    .accepts = each_true<
        col::read_verdict(col::verdict_cell_v<void, at::borrow>, ^^col::pack_key<at::borrow>).is_valid(),
        col::read_verdict(col::verdict_cell_v<int, at::borrow>, ^^col::verdict_key<int, at::borrow>).is_valid()>,
    .refuses = each_false<col::read_verdict(col::verdict_cell_v<void, at::borrow, at::coroutine>,
                                            ^^col::pack_key<at::borrow, at::coroutine>)
                              .is_valid()>};

// verdict_of reads the cell of the binding that its key names, and the key
// pack_key<Atoms...> names the pack-only view.
inline constexpr reader_cell cell_verdict_of{
    .accepts = each_true<col::verdict_of(^^col::pack_key<at::borrow>).is_valid(),
                         col::verdict_of(^^col::verdict_key<int, at::borrow>).is_valid(),
                         (col::verdict_of(^^col::pack_key<at::borrow, at::coroutine>).tripped
                          == pack_verdict_v<at::borrow, at::coroutine>.tripped)>,
    .refuses =
        each_false<col::verdict_of(^^col::pack_key<at::borrow, at::coroutine>).is_valid(),
                   col::verdict_of(^^col::verdict_key<::foundation::core::Result<int, plain_error>>).is_valid()>};

static_assert(std::is_same_v<col::pack_key<at::borrow>, col::verdict_key<void, at::borrow>>,
              "the key of the pack-only view is the key of a binding with the payload void");

// ── The helpers ────────────────────────────────────────────────────
//
// The walked entities that read no grade of their own.  Each row says why.
// A row names its entity by a reflection, so a row of a name that does not
// exist does not compile.
[[nodiscard]] consteval bool is_helper_(std::meta::info member) {
    return
        // The plumbing of the pass of the premises: a grade of the record,
        // and a concept over a grade.  Each premise cell of this file reads
        // them.
        member == ^^cd::grade_of_ || member == ^^cd::grade_is_ || member == ^^cd::satisfies_
        || member == ^^cd::satisfies_floor_
        // The cell of the premises after the message of each grade with no
        // reading.  A fixture holds each message.
        || member == ^^cd::checked_pack_premises_cell_
        // The slot of an axis, and the axes of a roster.  The cells of grades
        // and of axis_has_an_atom_ read them.
        || member == ^^cd::axis_slot_
        || member == ^^cd::roster_axes_
        // The pending gate.  pending_axes is empty, and this file asserts the
        // gate.
        || member == ^^col::axis_is_pending
        || member == ^^col::every_pending_axis_is_still_empty
        // Functions with no constant definition.  A call stops the build, and
        // a fixture holds each stop that a reader can reach.
        || member == ^^cd::a_row_axis_has_no_family_of_atoms
        || member == ^^cd::a_call_shape_grade_has_no_reading_in_call_shape_reading_
        || member == ^^cd::a_protocol_grade_has_no_reading_in_protocol_reading_
        || member == ^^cd::a_trust_grade_has_no_level_in_trust_level_of_
        || member == ^^cd::a_security_grade_has_no_class_in_security_class_of_
        || member == ^^cd::a_cache_cell_stands_for_another_key
        || member == ^^col::a_verdict_cell_stands_for_another_binding;
}

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wshadow"

// A walked entity is a concept, a function or a function template of the two
// namespaces that fixy/collision/Codes.h and fixy/collision/Report.h do not
// declare: an entity of fixy/Collision.h, or of a source file, such as a
// fixture that plants one.  Each of the two other headers holds its own
// checks.
[[nodiscard]] consteval bool is_walked_(std::meta::info member) {
    namespace ad = ::fixy::atom::detail;
    if (!std::meta::is_concept(member) && !std::meta::is_function(member) && !std::meta::is_function_template(member)) {
        return false;
    }
    return !ad::same_file_(member, ^^cd::corpus_lists_)
        && !ad::is_header_beside_(ad::declaration_file_(member), ad::declaration_file_(^^cd::corpus_lists_),
                                  "Report.h");
}

// True when the cell is the cell of the entity: its name is cell_ and the
// name of the entity.  The name is the key, so the overloads of one name
// share one cell.
[[nodiscard]] consteval bool is_cell_of_(std::meta::info cell, std::meta::info entity) {
    return std::meta::identifier_of(cell).substr(5) == std::meta::identifier_of(entity);
}

// The cell of an entity, or the null reflection: a variable of type
// reader_cell of this namespace, with the name cell_NAME.  Complexity: the
// members of this namespace.
[[nodiscard]] consteval std::meta::info cell_of_(std::meta::info entity) {
    // A member of a namespace has no access, so the context of this function
    // sees each one.
    template for (constexpr std::meta::info member : std::define_static_array(std::meta::members_of(
                      ^^::fixy::detail::collision_self_test::reader_cells, std::meta::access_context::current()))) {
        if constexpr (std::meta::is_variable(member)
                      && std::meta::remove_cvref(std::meta::type_of(member)) == ^^reader_cell) {
            if (is_cell_of_(member, entity)) return member;
        }
    }
    return {};
}

// The first walked entity of a namespace with no cell that holds, and that
// no helper row names, or the null reflection.  Complexity: the members of
// the namespace times the cells.
template <std::meta::info Scope>
[[nodiscard]] consteval std::meta::info first_reader_without_a_cell_in_() {
    template for (constexpr std::meta::info member :
                  std::define_static_array(std::meta::members_of(Scope, std::meta::access_context::current()))) {
        if (is_walked_(member) && !is_helper_(member)) {
            const std::meta::info cell = cell_of_(member);
            if (cell == std::meta::info{} || !std::meta::extract<reader_cell>(cell).holds()) return member;
        }
    }
    return {};
}

// True when a walked entity of the namespace that no helper row names has the
// cell.
template <std::meta::info Scope>
[[nodiscard]] consteval bool names_a_reader_in_(std::meta::info cell) {
    template for (constexpr std::meta::info member :
                  std::define_static_array(std::meta::members_of(Scope, std::meta::access_context::current()))) {
        if (is_walked_(member) && !is_helper_(member) && is_cell_of_(cell, member)) return true;
    }
    return false;
}

// The first cell that names no walked reader, or the null reflection.
[[nodiscard]] consteval std::meta::info first_stale_cell_() {
    template for (constexpr std::meta::info member : std::define_static_array(std::meta::members_of(
                      ^^::fixy::detail::collision_self_test::reader_cells, std::meta::access_context::current()))) {
        if constexpr (std::meta::is_variable(member)
                      && std::meta::remove_cvref(std::meta::type_of(member)) == ^^reader_cell) {
            if (!names_a_reader_in_<^^::fixy::collision>(member)
                && !names_a_reader_in_<^^::fixy::collision::detail>(member))
                return member;
        }
    }
    return {};
}

#pragma GCC diagnostic pop

// A reader of fixy/Collision.h with no cell that holds fails here, and the
// message names it.  Give the reader a variable cell_NAME of type reader_cell
// above, with answers that it accepts and answers that it refuses, or give it
// a row of is_helper_ with the reason.  A walk that sees no reader proves
// nothing, so the first assertion asks for the cell of a known reader.
static_assert(is_walked_(^^cd::judge_) && cell_of_(^^cd::judge_) != std::meta::info{},
              "the walk of the readers of fixy/Collision.h sees no reader");
static_assert(first_reader_without_a_cell_in_<^^::fixy::collision>() == std::meta::info{},
              std::meta::display_string_of(first_reader_without_a_cell_in_<^^::fixy::collision>() == std::meta::info{}
                                               ? ^^void
                                               : first_reader_without_a_cell_in_<^^::fixy::collision>()));
static_assert(first_reader_without_a_cell_in_<^^::fixy::collision::detail>() == std::meta::info{},
              std::meta::display_string_of(first_reader_without_a_cell_in_<^^::fixy::collision::detail>()
                                                   == std::meta::info{}
                                               ? ^^void
                                               : first_reader_without_a_cell_in_<^^::fixy::collision::detail>()));
// A cell that names no walked reader is stale: its reader went, or a helper
// row names it.
static_assert(first_stale_cell_() == std::meta::info{},
              std::meta::display_string_of(first_stale_cell_() == std::meta::info{} ? ^^void : first_stale_cell_()));
}  // namespace reader_cells

}  // namespace detail::collision_self_test

}  // namespace fixy
