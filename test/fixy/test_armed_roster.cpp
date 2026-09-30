// Every gate predicate in foundation and fixy has an armed cell, or a
// ledger entry that says it does not.
//
// A predicate that answers "no" for every type reads, at each call
// site, as a gate that admits.  So does one that answers "yes" for
// every type.  foundation/contracts/Armed.h gives the cell that pins
// both directions and the walk that finds every predicate.  The roster
// comes from reflection over the two namespaces, not from a list here,
// so a predicate added tomorrow is walked tomorrow.  The next unarmed
// predicate is then the one that fails this file, not the one nobody
// thought to list.
//
// The ledger at the foot names the predicates that are not armed.  It
// can only shrink.  An entry that becomes armed, or that names a
// predicate the walk no longer finds, fails the walk too.
//
// The build generates every_header.h from the headers under
// include/foundation and include/fixy, so the walk sees each header the
// two layers ship, and no list here has to name them.

#include "every_header.h"

#include <array>
#include <cstdio>
#include <memory>
#include <meta>
#include <optional>
#include <string>
#include <string_view>
#include <tuple>
#include <type_traits>
#include <vector>

namespace fc = ::foundation::contracts;
namespace fe = ::foundation::effects;
namespace fp = ::foundation::permissions;
namespace at = ::fixy::atom;
namespace sess = ::fixy::session;

using fc::armed_cell;
using fc::witnesses;
using fe::Effect;
using fe::Row;

namespace armed_roster_witness {

struct RegionTag {
    using permission_row = Row<>;
};
struct GlobalTag {};
struct Plain {
    int value = 0;
};
struct HoldsCapability {
    fe::Capability<Effect::IO, fe::Bg> held;
};
struct DeclaresAuthority {
    static constexpr bool conveys_authority = true;
};
struct Pusher {
    bool try_push(int) noexcept { return true; }
};
struct Popper {
    std::optional<int> try_pop() noexcept { return std::nullopt; }
};
struct Publisher {
    void publish(int) noexcept {}
};
struct Loader {
    int load() const noexcept { return 0; }
};
// Two dispatch families: one names a noexcept signature, one names data.
struct NamesNoexceptSignature {
    using signature = void(void*) noexcept;
};
struct NamesDataSignature {
    using signature = int;
};
// Two grades that name no wait strategy: an atom on the axis with no
// member, and a type whose member is not a WaitStrategy.
struct NamesNoStrategy final : at::atom_of<::fixy::Axis::Synchronization> {};
struct NamesIntStrategy {
    static constexpr int strategy = 0;
};

using BgCtx = fe::ExecCtx<fe::Bg, Row<Effect::Bg>>;
using BgIoCtx = fe::ExecCtx<fe::Bg, Row<Effect::Bg, Effect::IO>>;
using NvEnd = sess::VendorPinned<::foundation::algebra::lattices::VendorBackend::NV, sess::End>;

// A region split in two, and one nothrow body per half, for the spawn fit.
struct SpawnWhole {
    using permission_row = Row<>;
};
struct SpawnLeft {
    using permission_row = Row<>;
};
struct SpawnRight {
    using permission_row = Row<>;
};
struct SpawnBrand {};
struct LeftBody {
    void operator()(fp::WriteView<SpawnLeft, SpawnBrand> const&, BgCtx const&) const noexcept {}
};
struct RightBody {
    void operator()(fp::WriteView<SpawnRight, SpawnBrand> const&, BgCtx const&) const noexcept {}
};

// Roles, labels and global types for the global-type walks.
struct RoleP {};
struct RoleQ {};
struct RoleR {};
struct LabelA {};
struct LabelB {};
namespace gl = ::fixy::session::global;
using OneMsg = gl::Msg<RoleP, RoleQ, LabelA, int, gl::End>;
using LoopPQ = gl::Rec<gl::Msg<RoleP, RoleQ, LabelA, int, gl::Var>>;
using FreeVar = gl::Msg<RoleP, RoleQ, LabelA, int, gl::Var>;
using EnRoutePQ = gl::EnRoute<RoleP, RoleQ, LabelA, int, gl::End>;
using EnRouteAfterMsg = gl::Msg<RoleQ, RoleR, LabelB, int, EnRoutePQ>;
// R is in the loop body on one branch only, so the loop starves R.
using StarvesR = gl::Rec<gl::Comm<RoleP, RoleQ, gl::Branch<LabelA, int, gl::Var>,
                                  gl::Branch<LabelB, int, gl::Msg<RoleP, RoleR, LabelA, int, gl::End>>>>;

// A local protocol and a delegated endpoint for the session cells.
namespace sn = ::fixy::session;
using BareRecv = sn::Recv<int, sn::End>;
struct DelegatedWire {};
using DelegatedRecv = sn::DelegatedSession<BareRecv, DelegatedWire, sn::DefaultAbandonmentPolicy, fp::EmptyPermSet>;

// A stage type that claims to run inline, and one that makes no claim.
struct InlineClaimed {};
struct InlineUnclaimed {};

// A machine with one edge, Idle to Busy, for the transition predicate.
struct Idle {};
struct Busy {};
struct Done {};
namespace idle_edges {
inline constexpr ::foundation::fail_closed::edge<Idle, Busy> idle_to_busy{};
}  // namespace idle_edges
using IdleMachine = ::fixy::Machine<Idle, ^^idle_edges>;

// The channel-indexed classification of the corpus, named once.
using Channel = ::fixy::corpus::DischargeAxis;
template <Channel C, class Grade>
using classified_on = ::fixy::corpus::detail::is_classified_on_<C, Grade>;

}  // namespace armed_roster_witness

// The split that the spawn fit reads, declared beside its tags.
template <>
struct foundation::permissions::can_split_into_pack<armed_roster_witness::SpawnWhole, armed_roster_witness::SpawnLeft,
                                                    armed_roster_witness::SpawnRight> : std::true_type {};
template <>
struct foundation::permissions::has_split_pack_authoring_witness<
    armed_roster_witness::SpawnWhole, armed_roster_witness::SpawnLeft, armed_roster_witness::SpawnRight>
    : std::true_type {};
template <>
struct foundation::permissions::can_split_into<armed_roster_witness::SpawnWhole, armed_roster_witness::SpawnLeft,
                                               armed_roster_witness::SpawnRight> : std::true_type {};
template <>
struct foundation::permissions::has_split_authoring_witness<
    armed_roster_witness::SpawnWhole, armed_roster_witness::SpawnLeft, armed_roster_witness::SpawnRight>
    : std::true_type {};

// The inline claim is an opt-in that the stage's author writes.
template <>
struct fixy::concurrent::is_stage_inline_safe<armed_roster_witness::InlineClaimed> : std::true_type {};

namespace w = armed_roster_witness;

// ── foundation ──────────────────────────────────────────────────────

template <>
struct foundation::contracts::armed_cell<::foundation::algebra::is_graded_specialization> {
    using accepts = witnesses<fe::ComputationGraded<Row<>, int>, fe::ComputationGraded<Row<Effect::Bg>, double>>;
    using refuses = witnesses<int, fe::Computation<Row<>, int>>;
};

template <>
struct foundation::contracts::armed_cell<fe::detail::is_computation> {
    using accepts = witnesses<fe::Computation<Row<>, int>, fe::Computation<Row<Effect::IO>, w::Plain>>;
    using refuses = witnesses<int, fe::ComputationGraded<Row<>, int>>;
};

template <>
struct foundation::contracts::armed_cell<fe::detail::conveys_authority_listed> {
    using accepts = witnesses<fe::Capability<Effect::IO, fe::Bg>, fp::Permission<w::RegionTag>, w::BgCtx,
                              fe::Computation<Row<Effect::Bg>, int>>;
    using refuses = witnesses<int, fe::Computation<Row<>, int>, w::DeclaresAuthority>;
};

template <>
struct foundation::contracts::armed_cell<fe::detail::conveys_authority_directly> {
    using accepts = witnesses<fe::Capability<Effect::IO, fe::Bg>, w::DeclaresAuthority>;
    using refuses = witnesses<int, w::Plain, w::HoldsCapability>;
};

template <>
struct foundation::contracts::armed_cell<fe::detail::conveys_authority> {
    using accepts = witnesses<w::HoldsCapability, std::tuple<int, fe::Capability<Effect::IO, fe::Bg>>>;
    using refuses = witnesses<int, w::Plain, std::tuple<int, double>>;
};

template <>
struct foundation::contracts::armed_cell<fe::detail::extract_admits_payload> {
    using accepts = witnesses<int, w::Plain, fe::Computation<Row<>, int>>;
    using refuses = witnesses<w::HoldsCapability, fe::Computation<Row<Effect::Bg>, int>>;
};

template <>
struct foundation::contracts::armed_cell<fe::is_effect_row> {
    using accepts = witnesses<Row<>, Row<Effect::IO, Effect::Bg>>;
    using refuses = witnesses<int, at::with<Effect::IO>>;
};

template <>
struct foundation::contracts::armed_cell<fe::is_cap_type> {
    using accepts = witnesses<fe::Bg, fe::Init, fe::Test>;
    using refuses = witnesses<int, Row<>, w::BgCtx>;
};

template <>
struct foundation::contracts::armed_cell<fp::detail::is_permission_impl> {
    using accepts = witnesses<fp::Permission<w::RegionTag>>;
    using refuses = witnesses<int, fp::SharedPermission<w::RegionTag>>;
};

template <>
struct foundation::contracts::armed_cell<fp::detail::is_shared_permission_impl> {
    using accepts = witnesses<fp::SharedPermission<w::RegionTag>>;
    using refuses = witnesses<int, fp::Permission<w::RegionTag>>;
};

template <>
struct foundation::contracts::armed_cell<::foundation::diag::is_diagnostic> {
    using accepts = witnesses<::foundation::diag::Diagnostic<::foundation::diag::EffectRowMismatch, int>>;
    using refuses = witnesses<int, ::foundation::diag::EffectRowMismatch>;
};

// A row is a subrow when each of its effects is in the other row.  A
// shape that is not a row is in no subrow relation.
template <>
struct foundation::contracts::armed_instances<^^fe::is_subrow> {
    using accepts =
        witnesses<fe::is_subrow<Row<>, Row<Effect::IO>>, fe::is_subrow<Row<Effect::IO>, Row<Effect::Bg, Effect::IO>>>;
    using refuses = witnesses<fe::is_subrow<Row<Effect::IO>, Row<>>,
                              fe::is_subrow<Row<Effect::IO, Effect::Bg>, Row<Effect::IO>>, fe::is_subrow<int, Row<>>>;
};

// A context admits a tuple of permission tags when its row holds the row
// of each tag.  HugePageTag carries the IO row.
template <>
struct foundation::contracts::armed_instances<^^fp::detail::ctx_admits_tuple> {
    using accepts = witnesses<fp::detail::ctx_admits_tuple<w::BgCtx, std::tuple<w::RegionTag>>,
                              fp::detail::ctx_admits_tuple<w::BgIoCtx, std::tuple<w::RegionTag, fp::tag::HugePageTag>>>;
    using refuses = witnesses<fp::detail::ctx_admits_tuple<w::BgCtx, std::tuple<fp::tag::HugePageTag>>,
                              fp::detail::ctx_admits_tuple<w::BgCtx, std::tuple<w::RegionTag, fp::tag::HugePageTag>>>;
};

// A split manifest and its authoring witness hold only for the parent
// and the children in the order that the author declared.  A reversed
// or undeclared split holds neither.
template <>
struct foundation::contracts::armed_instances<^^fp::can_split_into> {
    using accepts = witnesses<fp::can_split_into<w::SpawnWhole, w::SpawnLeft, w::SpawnRight>>;
    using refuses = witnesses<fp::can_split_into<w::SpawnWhole, w::SpawnRight, w::SpawnLeft>,
                              fp::can_split_into<w::SpawnLeft, w::SpawnWhole, w::SpawnRight>>;
};

template <>
struct foundation::contracts::armed_instances<^^fp::has_split_authoring_witness> {
    using accepts = witnesses<fp::has_split_authoring_witness<w::SpawnWhole, w::SpawnLeft, w::SpawnRight>>;
    using refuses = witnesses<fp::has_split_authoring_witness<w::SpawnWhole, w::SpawnRight, w::SpawnLeft>,
                              fp::has_split_authoring_witness<w::SpawnLeft, w::SpawnWhole, w::SpawnRight>>;
};

template <>
struct foundation::contracts::armed_instances<^^fp::can_split_into_pack> {
    using accepts = witnesses<fp::can_split_into_pack<w::SpawnWhole, w::SpawnLeft, w::SpawnRight>>;
    using refuses = witnesses<fp::can_split_into_pack<w::SpawnWhole, w::SpawnRight, w::SpawnLeft>,
                              fp::can_split_into_pack<w::SpawnWhole, w::SpawnLeft>>;
};

template <>
struct foundation::contracts::armed_instances<^^fp::has_split_pack_authoring_witness> {
    using accepts = witnesses<fp::has_split_pack_authoring_witness<w::SpawnWhole, w::SpawnLeft, w::SpawnRight>>;
    using refuses = witnesses<fp::has_split_pack_authoring_witness<w::SpawnWhole, w::SpawnRight, w::SpawnLeft>,
                              fp::has_split_pack_authoring_witness<w::SpawnWhole, w::SpawnLeft>>;
};

// A tag pack is distinct when no tag occurs twice in it.
template <>
struct foundation::contracts::armed_instances<^^fp::detail::is_each_tag_distinct> {
    using accepts =
        witnesses<fp::detail::is_each_tag_distinct<>, fp::detail::is_each_tag_distinct<w::SpawnLeft, w::SpawnRight>>;
    using refuses = witnesses<fp::detail::is_each_tag_distinct<w::SpawnLeft, w::SpawnLeft>,
                              fp::detail::is_each_tag_distinct<w::SpawnLeft, w::SpawnRight, w::SpawnLeft>>;
};

// A fork body takes the view of its own child, under the parent's brand,
// and the context without throwing.  Bodies in the wrong order, too few
// bodies, or a shape that is not two tuples take nothing.  A body of
// another brand and a body that asks for its token have fixtures of their
// own in test/foundation/neg.
template <>
struct foundation::contracts::armed_instances<^^fp::detail::can_each_body_take_its_child> {
    using accepts = witnesses<fp::detail::can_each_body_take_its_child<
        w::BgCtx, w::SpawnBrand, std::tuple<w::SpawnLeft, w::SpawnRight>, std::tuple<w::LeftBody, w::RightBody>>>;
    using refuses = witnesses<
        fp::detail::can_each_body_take_its_child<w::BgCtx, w::SpawnBrand, std::tuple<w::SpawnLeft, w::SpawnRight>,
                                                 std::tuple<w::RightBody, w::LeftBody>>,
        fp::detail::can_each_body_take_its_child<w::BgCtx, w::SpawnBrand, std::tuple<w::SpawnLeft, w::SpawnRight>,
                                                 std::tuple<w::LeftBody>>,
        fp::detail::can_each_body_take_its_child<w::BgCtx, w::SpawnBrand, int, int>>;
};

// ── fixy: the throws atom ───────────────────────────────────────────

template <>
struct foundation::contracts::armed_cell<::fixy::detail::is_throws_atom> {
    using accepts = witnesses<at::ctrl::throws<>, at::ctrl::throws<w::Plain> const&>;
    using refuses = witnesses<int, std::tuple<at::ctrl::throws<>>, at::ctrl::any_exception>;
};

// ── fixy: the grade readers of the collision rules ──────────────────
//
// The rules read the row of the binding, which is a canonical Row: the
// Effect grade joined with the lifts of the atoms.  The observable reader
// answers for a row, and it gives false for a stated grade, which is not
// the row a rule reads.

template <>
struct foundation::contracts::armed_cell<::fixy::collision::detail::row_admits_observable_> {
    using accepts = witnesses<Row<Effect::Alloc>, Row<Effect::IO>, Row<Effect::Block>>;
    using refuses = witnesses<Row<Effect::Bg>, Row<>, at::with<Effect::IO>>;
};

template <>
struct foundation::contracts::armed_cell<::fixy::collision::detail::repr_is_atomic_> {
    using accepts = witnesses<at::repr<::fixy::pole::ReprKind::Atomic>>;
    using refuses = witnesses<int, std::integral_constant<::fixy::pole::ReprKind, ::fixy::pole::ReprKind::Opaque>>;
};

template <>
struct foundation::contracts::armed_cell<::fixy::collision::detail::is_thread_local_> {
    using accepts = witnesses<at::global::thread_local_<w::GlobalTag>>;
    using refuses = witnesses<int, at::global::singleton<w::GlobalTag>>;
};

template <>
struct foundation::contracts::armed_cell<::fixy::collision::detail::is_longjmp_unsafe_> {
    using accepts = witnesses<at::ctrl::longjmp_unsafe<"setjmp island">>;
    using refuses = witnesses<int, at::ctrl::abort<"setjmp island">>;
};

template <>
struct foundation::contracts::armed_cell<::fixy::collision::detail::is_recursing_> {
    using accepts = witnesses<at::dispatch::recurses<8>>;
    using refuses = witnesses<int, void>;
};

// protocol<proto::None> writes the strict pole out, so it names no session.
template <>
struct foundation::contracts::armed_cell<::fixy::collision::detail::is_session_protocol_> {
    using accepts =
        witnesses<at::protocol<armed_roster_witness::Plain>, at::session::live_handle<armed_roster_witness::Plain>>;
    using refuses = witnesses<int, at::protocol<::fixy::pole::proto::None>, at::spawn::detach_with<"no join">>;
};

// A protocol grade says that a binding speaks a protocol, and it does not
// say that the frame holds a live handle.
template <>
struct foundation::contracts::armed_cell<::fixy::collision::detail::is_live_handle_grade_> {
    using accepts = witnesses<at::session::live_handle<armed_roster_witness::Plain>>;
    using refuses = witnesses<int, at::protocol<armed_roster_witness::Plain>, at::protocol<::fixy::pole::proto::None>>;
};

// A handle before End owes its protocol, a handle at End does not, and a
// value that is not a handle holds none.
template <>
struct foundation::contracts::armed_cell<::fixy::collision::detail::is_live_handle_payload_> {
    using accepts =
        witnesses<sess::SessionHandle<sess::Send<int, sess::End>, armed_roster_witness::Plain>,
                  ::fixy::DetSafe<::fixy::DetSafeTier_v::Pure,
                                  sess::SessionHandle<sess::Recv<int, sess::End>, armed_roster_witness::Plain>>>;
    using refuses = witnesses<int, void, sess::SessionHandle<sess::End, armed_roster_witness::Plain>>;
};

// A DetSafe band at PhiloxRng or above claims replay, also under a band
// of another lattice.  A band below it, and a payload with no DetSafe
// band, claim nothing.
template <>
struct foundation::contracts::armed_cell<::fixy::collision::detail::is_replay_deterministic_> {
    using accepts =
        witnesses<::fixy::DetSafe<::fixy::DetSafeTier_v::Pure, int>,
                  ::fixy::DetSafe<::fixy::DetSafeTier_v::PhiloxRng, int>,
                  ::fixy::HotPath<::fixy::HotPathTier_v::Hot, ::fixy::DetSafe<::fixy::DetSafeTier_v::Pure, int>>>;
    using refuses = witnesses<int, void, ::fixy::DetSafe<::fixy::DetSafeTier_v::MonotonicClockRead, int>,
                              ::fixy::HotPath<::fixy::HotPathTier_v::Hot, int>>;
};

// The shared bottom and the shared top of the scope lattice pin no trunk.
// An atom of a different axis names no scope.
template <>
struct foundation::contracts::armed_cell<::fixy::collision::detail::is_scope_trunk_pinned_> {
    using accepts = witnesses<at::scope::cta, at::scope::gpu, at::scope::inner>;
    using refuses = witnesses<at::scope::thread, at::scope::system, int, at::barrier::seq_cst>;
};

template <>
struct foundation::contracts::armed_cell<::fixy::collision::detail::is_scope_on_host_trunk_> {
    using accepts = witnesses<at::scope::inner, at::scope::outer>;
    using refuses = witnesses<at::scope::cta, at::scope::gpu, int>;
};

// A portable or scalar ISA pins no trunk.
template <>
struct foundation::contracts::armed_cell<::fixy::collision::detail::is_isa_trunk_pinned_> {
    using accepts = witnesses<at::simd::avx2, at::simd::neon, at::simd::sve2>;
    using refuses = witnesses<at::simd::scalar, at::simd::portable, int>;
};

template <>
struct foundation::contracts::armed_cell<::fixy::collision::detail::is_isa_on_arm_trunk_> {
    using accepts = witnesses<at::simd::neon, at::simd::sve2>;
    using refuses = witnesses<at::simd::avx2, at::simd::sse2, int>;
};

// UMWAIT halts the core and does not spin it.  The refusing list holds
// it beside a kernel wait.
template <>
struct foundation::contracts::armed_cell<::fixy::collision::detail::is_busy_wait_> {
    using accepts = witnesses<at::sync::bounded_spin, at::sync::spin_pause>;
    using refuses = witnesses<at::sync::umwait_c01, at::sync::block, int>;
};

// A grade names a wait strategy when its `strategy` member is a
// WaitStrategy.  A type on the axis with no strategy names none.
template <>
struct foundation::contracts::armed_cell<::fixy::collision::detail::has_wait_strategy_> {
    using accepts = witnesses<at::sync::block, at::sync::umwait_c01, at::sync::spin_pause>;
    using refuses = witnesses<int, w::Plain, w::NamesNoStrategy, w::NamesIntStrategy>;
};

// A family names a noexcept signature through a function type, a pointer
// or member pointer to one, or a `signature` member that is not a class.
// A family that names no function answers false.
template <>
struct foundation::contracts::armed_cell<::fixy::collision::detail::is_signature_noexcept_> {
    using accepts = witnesses<void() noexcept, int (*)(char, ...) noexcept, void (w::Plain::*)() & noexcept,
                              w::NamesNoexceptSignature>;
    using refuses = witnesses<void(), int (&)(char), void (w::Plain::*)() &, w::Plain, w::NamesDataSignature>;
};

// An indirect call can throw unless its family names a noexcept signature.
// An opaque tag family names no signature, so the call can throw.
template <>
struct foundation::contracts::armed_cell<::fixy::collision::detail::can_indirect_call_throw_> {
    using accepts =
        witnesses<at::dispatch::indirect_call<int(char)>, at::dispatch::indirect_call<w::Plain>,
                  at::dispatch::indirect_call<void (w::Plain::*)() &>, at::dispatch::indirect_call<int (*)(char, ...)>>;
    using refuses =
        witnesses<at::dispatch::indirect_call<int(char) noexcept>, at::dispatch::indirect_call<void (*)() noexcept>,
                  at::dispatch::indirect_call<w::NamesNoexceptSignature>, int>;
};

// A subprocess has its own copy of the address space, so it cannot
// outlive the frame that it borrows from.
template <>
struct foundation::contracts::armed_cell<::fixy::collision::detail::can_spawn_outlive_the_frame_> {
    using accepts = witnesses<at::spawn::detach_with<"drain">, at::spawn::syscall_only<"loader">>;
    using refuses = witnesses<at::spawn::subprocess<"helper">, int>;
};

template <>
struct foundation::contracts::armed_cell<::fixy::collision::detail::control_flow_has_suspension_> {
    using accepts = witnesses<at::ctrl::coroutine<at::ctrl::async_task>>;
    using refuses = witnesses<int, at::ctrl::longjmp_unsafe<"setjmp island">, at::ctrl::throws<>>;
};

// The floor readers take a value and a grade, so each has an instance
// cell.  An atom of a different axis carries a member of the same name,
// and the type check of each reader refuses it.
template <>
struct foundation::contracts::armed_instances<^^::fixy::collision::detail::is_hw_at_or_above_> {
    using Tier = ::fixy::atom::hw::HwInstruction;
    using accepts =
        witnesses<::fixy::collision::detail::is_hw_at_or_above_<Tier::NonDeterministicTsc, at::hw::privileged_msr>,
                  ::fixy::collision::detail::is_hw_at_or_above_<Tier::Scalar, at::hw::scalar>>;
    using refuses = witnesses<::fixy::collision::detail::is_hw_at_or_above_<Tier::PrivilegedMsr, at::hw::vectorizable>,
                              ::fixy::collision::detail::is_hw_at_or_above_<Tier::Scalar, int>,
                              ::fixy::collision::detail::is_hw_at_or_above_<Tier::Scalar, at::barrier::full_fence>>;
};

template <>
struct foundation::contracts::armed_instances<^^::fixy::collision::detail::is_barrier_at_or_above_> {
    using Strength = ::foundation::algebra::lattices::BarrierStrength;
    using accepts =
        witnesses<::fixy::collision::detail::is_barrier_at_or_above_<Strength::SeqCst, at::barrier::full_fence>,
                  ::fixy::collision::detail::is_barrier_at_or_above_<Strength::AcqRel, at::barrier::seq_cst>,
                  ::fixy::collision::detail::is_barrier_at_or_above_<Strength::AcquireLoad, at::barrier::acq_rel>>;
    using refuses = witnesses<
        ::fixy::collision::detail::is_barrier_at_or_above_<Strength::SeqCst, at::barrier::acq_rel>,
        ::fixy::collision::detail::is_barrier_at_or_above_<Strength::AcqRel, int>,
        ::fixy::collision::detail::is_barrier_at_or_above_<Strength::None, at::hw::privileged_msr>,
        ::fixy::collision::detail::is_barrier_at_or_above_<Strength::AcquireLoad, at::barrier::release_store>,
        ::fixy::collision::detail::is_barrier_at_or_above_<Strength::ReleaseStore, at::barrier::acquire_load>>;
};

// The scope lattice has two trunks, so a host scope is not at or above an
// accelerator floor.
template <>
struct foundation::contracts::armed_instances<^^::fixy::collision::detail::is_scope_at_or_above_> {
    using Scope = ::foundation::algebra::lattices::MemoryScope;
    using accepts = witnesses<::fixy::collision::detail::is_scope_at_or_above_<Scope::Cluster, at::scope::gpu>,
                              ::fixy::collision::detail::is_scope_at_or_above_<Scope::Cta, at::scope::system>>;
    using refuses = witnesses<::fixy::collision::detail::is_scope_at_or_above_<Scope::Cluster, at::scope::inner>,
                              ::fixy::collision::detail::is_scope_at_or_above_<Scope::Gpu, at::scope::cta>,
                              ::fixy::collision::detail::is_scope_at_or_above_<Scope::Cluster, int>>;
};

// A mode names only the settings it was written with.
template <>
struct foundation::contracts::armed_instances<^^::fixy::collision::detail::fp_mode_has_setting_> {
    using accepts = witnesses<::fixy::collision::detail::fp_mode_has_setting_<at::fp::FpContract::Fast,
                                                                              at::fp::mode<at::fp::FpContract::Fast>>,
                              ::fixy::collision::detail::fp_mode_has_setting_<
                                  at::fp::FpReassociate::UnrestrictedRewrite,
                                  at::fp::mode<at::fp::FpContract::Fast, at::fp::FpReassociate::UnrestrictedRewrite>>>;
    using refuses = witnesses<::fixy::collision::detail::fp_mode_has_setting_<at::fp::FpContract::Off,
                                                                              at::fp::mode<at::fp::FpContract::Fast>>,
                              ::fixy::collision::detail::fp_mode_has_setting_<at::fp::FpContract::Fast, at::fp::mode<>>,
                              ::fixy::collision::detail::fp_mode_has_setting_<at::fp::FpContract::Fast, int>>;
};

// ── fixy: concurrency handles and pipelines ─────────────────────────

template <>
struct foundation::contracts::armed_cell<::fixy::concurrent::detail::has_try_push> {
    using accepts = witnesses<w::Pusher>;
    using refuses = witnesses<int, w::Popper, w::Plain>;
};

template <>
struct foundation::contracts::armed_cell<::fixy::concurrent::detail::has_try_pop> {
    using accepts = witnesses<w::Popper>;
    using refuses = witnesses<int, w::Pusher, w::Plain>;
};

template <>
struct foundation::contracts::armed_cell<::fixy::concurrent::detail::has_publish> {
    using accepts = witnesses<w::Publisher>;
    using refuses = witnesses<int, w::Loader, w::Plain>;
};

template <>
struct foundation::contracts::armed_cell<::fixy::concurrent::detail::has_load> {
    using accepts = witnesses<w::Loader>;
    using refuses = witnesses<int, w::Publisher, w::Plain>;
};

template <>
struct foundation::contracts::armed_cell<::fixy::concurrent::detail::is_stage_edge> {
    using accepts = witnesses<::fixy::concurrent::StageEdge<0, 1, 0, 0>>;
    using refuses = witnesses<int, ::fixy::concurrent::StagePack<>>;
};

// A stage is named by the self-test stage of fixy/concurrent/Stage.h,
// which fits the foreground context.  Naming the type does not build a
// stage, so no handle and no channel is made here.
template <>
struct foundation::contracts::armed_cell<::fixy::concurrent::detail::is_stage> {
    using accepts = witnesses<
        ::fixy::concurrent::Stage<&::fixy::concurrent::detail::stage_self_test::stage_pass_through, ::fixy::HotFgCtx>>;
    using refuses = witnesses<int, ::fixy::concurrent::StagePack<>, ::fixy::concurrent::StageEdge<0, 1, 0, 0>>;
};

template <>
struct foundation::contracts::armed_cell<::fixy::concurrent::detail::is_stage_graph> {
    using accepts =
        witnesses<::fixy::concurrent::StageGraph<::fixy::concurrent::StagePack<>, ::fixy::concurrent::EdgePack<>>>;
    using refuses = witnesses<int, ::fixy::concurrent::StagePack<>, ::fixy::concurrent::EdgePack<>>;
};

// ── fixy: mutation, usage, security and view recognisers ────────────

template <>
struct foundation::contracts::armed_cell<::fixy::is_writeonce> {
    using accepts = witnesses<::fixy::WriteOnce<int>, ::fixy::WriteOnce<int> const&>;
    using refuses = witnesses<int, ::fixy::WriteOnceNonNull<int*>>;
};

template <>
struct foundation::contracts::armed_cell<::fixy::is_writeoncenonnull> {
    using accepts = witnesses<::fixy::WriteOnceNonNull<int*>>;
    using refuses = witnesses<int*, ::fixy::WriteOnce<int>>;
};

// The two Qtt recognisers carry the witnesses of their own assertions
// in fixy/Qtt.h.
template <>
struct foundation::contracts::armed_cell<::fixy::is_already_linear> {
    using accepts = witnesses<::fixy::Linear<int>, ::fixy::Linear<int> const&, ::fixy::Linear<void*>>;
    using refuses = witnesses<int, void*, ::fixy::Affine<int>, std::unique_ptr<int>>;
};

template <>
struct foundation::contracts::armed_cell<::fixy::is_already_consume_disciplined> {
    using accepts = witnesses<::fixy::Linear<int>, ::fixy::Affine<int>, ::fixy::Affine<void*>&&>;
    using refuses = witnesses<int, void*, std::unique_ptr<int>>;
};

// A carrier is classified on every channel.  A declassification is
// classified on each channel its policy does not name.
template <>
struct foundation::contracts::armed_instances<^^::fixy::corpus::detail::is_classified_on_> {
    using accepts =
        witnesses<w::classified_on<w::Channel::IO, at::as_secret>, w::classified_on<w::Channel::Bg, at::constant_time>,
                  w::classified_on<w::Channel::Bg, at::declassify<::fixy::tags::secret_policy::WireSerialize>>,
                  w::classified_on<w::Channel::IO, at::declassify<::fixy::tags::secret_policy::AuthorizedReplay>>>;
    using refuses = witnesses<
        w::classified_on<w::Channel::IO, at::declassify<::fixy::tags::secret_policy::WireSerialize>>,
        w::classified_on<w::Channel::Staleness, at::declassify<::fixy::tags::secret_policy::AuthorizedReplay>>,
        w::classified_on<w::Channel::IO, at::as_public>, w::classified_on<w::Channel::Bg, at::as_internal>,
        w::classified_on<w::Channel::IO, int>>;
};

template <>
struct foundation::contracts::armed_cell<::fixy::corpus::detail::is_internal_> {
    using accepts = witnesses<at::as_internal>;
    using refuses = witnesses<int, at::as_secret, at::as_public, at::constant_time>;
};

template <>
struct foundation::contracts::armed_cell<::fixy::corpus::detail::is_stale_> {
    using accepts = witnesses<at::stale_to<5>>;
    using refuses = witnesses<int, at::ghost>;
};

template <>
struct foundation::contracts::armed_cell<::fixy::corpus::detail::is_ghost_> {
    using accepts = witnesses<at::ghost>;
    using refuses = witnesses<int, at::copy>;
};

// Init and Test stay outside the observable set.  The entry reads the row
// of the binding, so a stated grade that is not a row names nothing here.
template <>
struct foundation::contracts::armed_cell<::fixy::corpus::detail::is_row_observable_> {
    using accepts = witnesses<Row<Effect::IO>, Row<Effect::Alloc>, Row<Effect::Init, Effect::Block>>;
    using refuses = witnesses<Row<Effect::Init>, Row<Effect::Test>, Row<>, at::with_io, int>;
};

// Only AuthorizedReplay discharges an axis, and it discharges staleness
// alone.
template <>
struct foundation::contracts::armed_instances<^^::fixy::corpus::detail::can_discharge_> {
    using accepts = witnesses<::fixy::corpus::detail::can_discharge_<
        ::fixy::corpus::DischargeAxis::Staleness, at::declassify<::fixy::tags::secret_policy::AuthorizedReplay>>>;
    using refuses =
        witnesses<::fixy::corpus::detail::can_discharge_<::fixy::corpus::DischargeAxis::IO,
                                                         at::declassify<::fixy::tags::secret_policy::AuthorizedReplay>>,
                  ::fixy::corpus::detail::can_discharge_<::fixy::corpus::DischargeAxis::Staleness,
                                                         at::declassify<::fixy::tags::secret_policy::WireSerialize>>,
                  ::fixy::corpus::detail::can_discharge_<::fixy::corpus::DischargeAxis::Staleness, int>>;
};

template <>
struct foundation::contracts::armed_cell<::fixy::is_fn> {
    using accepts = witnesses<::fixy::fn<int>>;
    using refuses = witnesses<int, w::Plain>;
};

// A pack with two atoms on one axis is no accepted binding, and a type
// that is not an fn is none.
template <>
struct foundation::contracts::armed_cell<::fixy::detail::role::is_accepted_fn> {
    using accepts = witnesses<::fixy::fn<int>, ::fixy::fn<int, at::copy>>;
    using refuses = witnesses<int, ::fixy::fn<int, at::copy, at::affine>, ::fixy::fn<void, at::copy>>;
};

// ── fixy: the global-type walks ─────────────────────────────────────

namespace gd = ::fixy::session::global::detail;

template <>
struct foundation::contracts::armed_instances<^^gd::is_role_in> {
    using accepts = witnesses<gd::is_role_in<w::RoleP, w::gl::Roles<w::RoleP, w::RoleQ>>,
                              gd::is_role_in<w::RoleQ, w::gl::Roles<w::RoleP, w::RoleQ>>>;
    using refuses =
        witnesses<gd::is_role_in<w::RoleR, w::gl::Roles<w::RoleP, w::RoleQ>>, gd::is_role_in<w::RoleP, w::gl::Roles<>>>;
};

// A projection domain covers a role list when each role of the list is in
// the domain.  A list that is not a Roles list is covered by nothing.
template <>
struct foundation::contracts::armed_instances<^^::fixy::session::detail::proj::is_each_role_in_domain> {
    using accepts = witnesses<
        ::fixy::session::detail::proj::is_each_role_in_domain<w::gl::Roles<w::RoleP, w::RoleQ>, w::gl::Roles<w::RoleP>>,
        ::fixy::session::detail::proj::is_each_role_in_domain<w::gl::Roles<w::RoleP>, w::gl::Roles<>>>;
    using refuses = witnesses<
        ::fixy::session::detail::proj::is_each_role_in_domain<w::gl::Roles<w::RoleP>, w::gl::Roles<w::RoleP, w::RoleQ>>,
        ::fixy::session::detail::proj::is_each_role_in_domain<w::gl::Roles<w::RoleP>, int>>;
};

// A Var is free unless a Rec above it binds it.
template <>
struct foundation::contracts::armed_cell<gd::has_free_var> {
    using accepts = witnesses<w::gl::Var, w::FreeVar>;
    using refuses = witnesses<w::gl::End, w::OneMsg, w::LoopPQ>;
};

template <>
struct foundation::contracts::armed_cell<gd::has_en_route> {
    using accepts = witnesses<w::EnRoutePQ, w::EnRouteAfterMsg>;
    using refuses = witnesses<w::gl::End, w::OneMsg, w::LoopPQ>;
};

// The crash pseudo-message of a crashed sender is no live en-route node.
template <>
struct foundation::contracts::armed_cell<gd::has_live_en_route> {
    using accepts = witnesses<w::EnRoutePQ, w::EnRouteAfterMsg>;
    using refuses = witnesses<w::gl::End, w::OneMsg, w::LoopPQ,
                              w::gl::EnRouteChoice<w::gl::Crashed<w::RoleP>, w::RoleQ, w::gl::CrashLabel,
                                                   w::gl::Branch<w::LabelA, int, w::gl::End>,
                                                   w::gl::Branch<w::gl::CrashLabel, void, w::gl::End>>>;
};

// A Rec body is guarded when its first node is a communication.
template <>
struct foundation::contracts::armed_cell<gd::is_rec_guarded> {
    using accepts = witnesses<w::OneMsg, w::FreeVar, w::gl::Rec<w::OneMsg>>;
    using refuses = witnesses<w::gl::Var, w::gl::Rec<w::gl::Var>>;
};

// Every path from the body back to its Rec meets the role.
template <>
struct foundation::contracts::armed_instances<^^gd::is_met_on_every_path> {
    using accepts =
        witnesses<gd::is_met_on_every_path<w::RoleP, w::FreeVar>, gd::is_met_on_every_path<w::RoleR, w::gl::End>>;
    using refuses =
        witnesses<gd::is_met_on_every_path<w::RoleR, w::FreeVar>, gd::is_met_on_every_path<w::RoleP, w::gl::Var>>;
};

template <>
struct foundation::contracts::armed_instances<^^gd::is_loop_met_by_each> {
    using accepts = witnesses<gd::is_loop_met_by_each<w::FreeVar, w::gl::Roles<w::RoleP, w::RoleQ>>>;
    using refuses = witnesses<gd::is_loop_met_by_each<w::FreeVar, w::gl::Roles<w::RoleP, w::RoleR>>>;
};

template <>
struct foundation::contracts::armed_cell<gd::is_each_loop_balanced> {
    using accepts = witnesses<w::gl::End, w::OneMsg, w::LoopPQ>;
    using refuses = witnesses<w::StarvesR, w::gl::Msg<w::RoleQ, w::RoleR, w::LabelB, int, w::StarvesR>>;
};

// The en-route pair is the sender and the receiver of an en-route
// message, in that order.
template <>
struct foundation::contracts::armed_instances<^^gd::has_en_route_pair> {
    using accepts = witnesses<gd::has_en_route_pair<w::RoleP, w::RoleQ, w::EnRoutePQ>,
                              gd::has_en_route_pair<w::RoleP, w::RoleQ, w::EnRouteAfterMsg>>;
    using refuses = witnesses<gd::has_en_route_pair<w::RoleQ, w::RoleP, w::EnRoutePQ>,
                              gd::has_en_route_pair<w::RoleP, w::RoleQ, w::OneMsg>>;
};

// ── fixy: the shapes the recorder reads ─────────────────────────────

namespace rec = ::fixy::session::detail::recording;

template <>
struct foundation::contracts::armed_cell<rec::is_crash_watched_shape> {
    using accepts = witnesses<w::sn::CrashWatched<int, w::RoleP, w::RoleQ, w::sn::ReliableSet<>, w::BgCtx>>;
    using refuses = witnesses<int, w::sn::CrashSend<int, int>>;
};

template <>
struct foundation::contracts::armed_cell<rec::is_checkpoint_shape> {
    using accepts = witnesses<w::sn::CheckpointHandle<int, int, int, int>>;
    using refuses = witnesses<int, w::sn::CrashWatched<int, w::RoleP, w::RoleQ, w::sn::ReliableSet<>, w::BgCtx>>;
};

template <>
struct foundation::contracts::armed_cell<rec::is_crash_send_shape> {
    using accepts = witnesses<w::sn::CrashSend<int, int>>;
    using refuses = witnesses<int, w::sn::CheckpointHandle<int, int, int, int>>;
};

// A delegated session is a hand-off, and every other payload is a message.
template <>
struct foundation::contracts::armed_cell<rec::is_delegation_shape> {
    using accepts = witnesses<w::DelegatedRecv>;
    using refuses = witnesses<int, w::BareRecv>;
};

// A stage runs inline only when its author claims it.
template <>
struct foundation::contracts::armed_cell<::fixy::concurrent::is_stage_inline_safe> {
    using accepts = witnesses<w::InlineClaimed>;
    using refuses = witnesses<w::InlineUnclaimed, int>;
};

// The handles of a variadic stage match its parameters in count, order
// and type.
namespace stage_fakes = ::fixy::concurrent::detail::stage_self_test;
template <>
struct foundation::contracts::armed_instances<^^::fixy::concurrent::detail::can_variadic_stage_take_handles> {
    using accepts = witnesses<::fixy::concurrent::detail::can_variadic_stage_take_handles<
        &stage_fakes::stage_fan_in_two, std::tuple<stage_fakes::FakeConsumer<int>, stage_fakes::FakeConsumer<int>>,
        std::tuple<stage_fakes::FakeProducer<int>>>>;
    using refuses = witnesses<
        ::fixy::concurrent::detail::can_variadic_stage_take_handles<&stage_fakes::stage_fan_in_two,
                                                                    std::tuple<stage_fakes::FakeConsumer<int>>,
                                                                    std::tuple<stage_fakes::FakeProducer<int>>>,
        ::fixy::concurrent::detail::can_variadic_stage_take_handles<
            &stage_fakes::stage_fan_in_two,
            std::tuple<stage_fakes::FakeConsumer<int>, stage_fakes::FakeConsumer<float>>,
            std::tuple<stage_fakes::FakeProducer<int>>>,
        ::fixy::concurrent::detail::can_variadic_stage_take_handles<&stage_fakes::stage_fan_in_two, int, int>>;
};

// The spawn fit needs a context that owns Bg, a declared split, and one
// nothrow body per child.
template <>
struct foundation::contracts::armed_instances<^^::fixy::spawn::detail::can_ctx_fit_spawn> {
    using accepts = witnesses<::fixy::spawn::detail::can_ctx_fit_spawn<w::BgCtx, w::SpawnWhole, w::SpawnBrand,
                                                                       std::tuple<w::SpawnLeft, w::SpawnRight>,
                                                                       std::tuple<w::LeftBody, w::RightBody>>>;
    using refuses =
        witnesses<::fixy::spawn::detail::can_ctx_fit_spawn<fe::ExecCtx<>, w::SpawnWhole, w::SpawnBrand,
                                                           std::tuple<w::SpawnLeft, w::SpawnRight>,
                                                           std::tuple<w::LeftBody, w::RightBody>>,
                  ::fixy::spawn::detail::can_ctx_fit_spawn<w::BgCtx, w::SpawnWhole, w::SpawnBrand,
                                                           std::tuple<w::SpawnLeft, w::SpawnRight>,
                                                           std::tuple<w::LeftBody, w::LeftBody>>,
                  ::fixy::spawn::detail::can_ctx_fit_spawn<w::BgCtx, w::SpawnWhole, w::SpawnBrand,
                                                           std::tuple<w::SpawnRight, w::SpawnLeft>,
                                                           std::tuple<w::RightBody, w::LeftBody>>,
                  ::fixy::spawn::detail::can_ctx_fit_spawn<w::BgCtx, w::SpawnWhole, w::SpawnBrand, int, int>>;
};

// The third argument says whether the first is a machine.  A machine
// admits its declared edge and the diagonal, and no other move.
template <>
struct foundation::contracts::armed_instances<^^::fixy::mach::detail::can_transition_impl> {
    using accepts = witnesses<::fixy::mach::detail::can_transition_impl<w::IdleMachine, w::Busy, true>,
                              ::fixy::mach::detail::can_transition_impl<w::IdleMachine, w::Idle, true>>;
    using refuses =
        witnesses<::fixy::mach::detail::can_transition_impl<w::IdleMachine, w::Done, true>,
                  ::fixy::mach::detail::can_transition_impl<::fixy::Machine<w::Busy, ^^w::idle_edges>, w::Idle, true>,
                  ::fixy::mach::detail::can_transition_impl<int, double, false>>;
};

// ── fixy: session protocol shapes ───────────────────────────────────

template <>
struct foundation::contracts::armed_cell<sess::is_send> {
    using accepts =
        witnesses<sess::Send<int, sess::End>,
                  sess::VendorPinned<::foundation::algebra::lattices::VendorBackend::NV, sess::Send<int, sess::End>>>;
    using refuses = witnesses<sess::Recv<int, sess::End>, sess::End>;
};

template <>
struct foundation::contracts::armed_cell<sess::is_recv> {
    using accepts = witnesses<sess::Recv<int, sess::End>>;
    using refuses = witnesses<sess::Send<int, sess::End>, sess::End>;
};

template <>
struct foundation::contracts::armed_cell<sess::is_select> {
    using accepts = witnesses<sess::Select<sess::End>, sess::Select<>>;
    using refuses = witnesses<sess::Offer<sess::End>, sess::End>;
};

template <>
struct foundation::contracts::armed_cell<sess::is_offer> {
    using accepts = witnesses<sess::Offer<sess::End>, sess::Offer<>>;
    using refuses = witnesses<sess::Select<sess::End>, sess::End>;
};

template <>
struct foundation::contracts::armed_cell<sess::is_loop> {
    using accepts = witnesses<sess::Loop<sess::Send<int, sess::Continue>>>;
    using refuses = witnesses<sess::Send<int, sess::End>, sess::End>;
};

template <>
struct foundation::contracts::armed_cell<sess::is_end> {
    using accepts = witnesses<sess::End, w::NvEnd>;
    using refuses = witnesses<sess::Continue, sess::Send<int, sess::End>>;
};

template <>
struct foundation::contracts::armed_cell<sess::is_continue> {
    using accepts = witnesses<sess::Continue>;
    using refuses = witnesses<sess::End, sess::Send<int, sess::Continue>>;
};

template <>
struct foundation::contracts::armed_cell<sess::is_vendor_pinned> {
    using accepts = witnesses<w::NvEnd>;
    using refuses = witnesses<sess::End, sess::Send<int, sess::End>>;
};

template <>
struct foundation::contracts::armed_cell<sess::is_empty_choice> {
    using accepts = witnesses<sess::Select<>, sess::Offer<>, sess::Send<int, sess::Select<>>>;
    using refuses = witnesses<sess::End, sess::Select<sess::End>>;
};

template <>
struct foundation::contracts::armed_cell<sess::is_terminal_state> {
    using accepts = witnesses<sess::End, w::NvEnd>;
    using refuses = witnesses<sess::Continue, sess::Send<int, sess::End>>;
};

template <>
struct foundation::contracts::armed_cell<sess::is_well_formed> {
    using accepts = witnesses<sess::End, sess::Send<int, sess::End>, sess::Loop<sess::Send<int, sess::Continue>>>;
    using refuses = witnesses<sess::Continue, sess::Send<int, sess::Continue>, sess::Loop<sess::Continue>>;
};

// Each protocol trait above is an alias, so no program can specialize it.
// The walk finds class templates only, so these assertions read the cells.
static_assert(fc::armed_cell_holds_v<sess::is_send>);
static_assert(fc::armed_cell_holds_v<sess::is_recv>);
static_assert(fc::armed_cell_holds_v<sess::is_select>);
static_assert(fc::armed_cell_holds_v<sess::is_offer>);
static_assert(fc::armed_cell_holds_v<sess::is_loop>);
static_assert(fc::armed_cell_holds_v<sess::is_end>);
static_assert(fc::armed_cell_holds_v<sess::is_continue>);
static_assert(fc::armed_cell_holds_v<sess::is_vendor_pinned>);
static_assert(fc::armed_cell_holds_v<sess::is_empty_choice>);
static_assert(fc::armed_cell_holds_v<sess::is_terminal_state>);
static_assert(fc::armed_cell_holds_v<sess::is_well_formed>);

// The payload walk of fixy/session/Payload.h.  A token reached owned is
// classified, and a token behind a pointer, in an optional, or a read
// proof outside its marker is refused.
template <>
struct foundation::contracts::armed_cell<sess::is_permission_classified> {
    using accepts = witnesses<int, sess::Transferable<int, fp::tag::HugePageTag>,
                              std::pair<sess::Transferable<int, fp::tag::HugePageTag>, int>>;
    using refuses =
        witnesses<sess::Transferable<int, fp::tag::HugePageTag>*,
                  std::optional<sess::Transferable<int, fp::tag::HugePageTag>>, fp::ReadView<fp::tag::HugePageTag>>;
};

template <>
struct foundation::contracts::armed_cell<sess::is_plain_payload> {
    using accepts = witnesses<int, std::pair<int, double>>;
    using refuses = witnesses<sess::Transferable<int, fp::tag::HugePageTag>,
                              std::pair<sess::Transferable<int, fp::tag::HugePageTag>, int>,
                              sess::Transferable<int, fp::tag::HugePageTag>*>;
};

namespace {

// ── the ledger ──────────────────────────────────────────────────────
//
// The predicates the walk finds and that have no cell.  Each entry says
// why.  The ledger can only shrink: an entry that gains a cell, or that
// names a predicate the walk stops finding, fails the walk.  It is empty,
// so every predicate the walk finds has a cell, and a new predicate with
// no cell fails the walk.
inline constexpr std::array<std::meta::info, 0> unarmed_ledger{};

inline constexpr std::meta::info walked_scopes[] = {^^::foundation, ^^::fixy};

constexpr fc::ArmedRosterVerdict verdict = fc::armed_roster_verdict(walked_scopes, unarmed_ledger);

[[nodiscard]] consteval std::string_view unarmed_message() {
    const std::meta::info first = fc::first_unarmed_outside_ledger(walked_scopes, unarmed_ledger);
    if (first == std::meta::info{}) return {};
    std::string text{"a gate predicate has no armed cell and no ledger entry.  Write a cell for it with "
                     "witnesses it accepts and witnesses it refuses, beside the predicate or in this file.  "
                     "The first one: "};
    text += std::meta::display_string_of(first);
    return std::define_static_string(text);
}

static_assert(verdict.walked > 0, "the walk over foundation and fixy found no predicate, so it proves nothing.  "
                                  "Either every header left every_header.h, or the name rule stopped matching.");
static_assert(verdict.unarmed_outside_ledger == 0, unarmed_message());
static_assert(verdict.stale_ledger_entries == 0,
              "a ledger entry names a predicate that is armed, or that the walk no longer finds.  Delete the "
              "entry: the ledger only shrinks.");
static_assert(verdict.armed + verdict.ledgered == verdict.walked);

// ── the relation census ─────────────────────────────────────────────
//
// Every namespace under foundation and fixy that declares a
// fail_closed edge is a relation that some check admits pairs from.
// A namespace can be opened again from any file, so each such relation
// carries a seal whose count every read checks, or it is named below as
// open with the reason its openness grants nothing.  The walk finds the
// relations by reflection, so a new relation is sealed or named the day
// it is declared.
struct OpenRelation {
    std::meta::info ns;
    std::string_view reason;
};

inline constexpr OpenRelation open_relations[] = {
    {^^::foundation::permissions::permission_rows,
     "the effect row of each permission tag, which a tag registers beside its own declaration.  A tag has one "
     "row: unique_target refuses a second edge from one tag, an edge beside a permission_row member is refused, "
     "and a derived tag, which has its parent's row, may declare neither"},
};

consteval void collect_relations(std::meta::info ns, std::vector<std::meta::info>& found) {
    bool holds_an_edge = false;
    for (const std::meta::info member : std::meta::members_of(ns, std::meta::access_context::unchecked())) {
        if (std::meta::is_namespace(member) && !std::meta::is_namespace_alias(member)) {
            collect_relations(member, found);
        } else if (::foundation::fail_closed::is_edge(member)) {
            holds_an_edge = true;
        }
    }
    if (holds_an_edge) found.push_back(ns);
}

struct RelationVerdict {
    std::size_t relations = 0;
    std::size_t sealed = 0;
    std::size_t named_open = 0;
    std::size_t neither = 0;
    std::size_t stale_open_entries = 0;
};

[[nodiscard]] consteval RelationVerdict relation_verdict() {
    std::vector<std::meta::info> relations;
    collect_relations(^^::foundation, relations);
    collect_relations(^^::fixy, relations);
    RelationVerdict result{};
    result.relations = relations.size();
    for (const std::meta::info ns : relations) {
        const auto reading = ::foundation::fail_closed::read_seal(ns);
        bool is_named_open = false;
        for (const OpenRelation& open : open_relations) {
            if (open.ns == ns && !open.reason.empty()) is_named_open = true;
        }
        if (reading.is_sealed && reading.fault == ::foundation::fail_closed::seal_fault::none && !is_named_open) {
            ++result.sealed;
        } else if (!reading.is_sealed && is_named_open) {
            ++result.named_open;
        } else {
            ++result.neither;
        }
    }
    for (const OpenRelation& open : open_relations) {
        bool is_a_relation = false;
        for (const std::meta::info ns : relations) {
            if (ns == open.ns) is_a_relation = true;
        }
        if (!is_a_relation) ++result.stale_open_entries;
    }
    return result;
}

[[nodiscard]] consteval std::string_view unsealed_message() {
    std::vector<std::meta::info> relations;
    collect_relations(^^::foundation, relations);
    collect_relations(^^::fixy, relations);
    std::string text{"a fail-closed relation under foundation or fixy is neither sealed nor named open, or is "
                     "both.  Add `inline constexpr foundation::fail_closed::seal sealed{.members = N};` to its "
                     "namespace.  The relations at fault: "};
    for (const std::meta::info ns : relations) {
        const auto reading = ::foundation::fail_closed::read_seal(ns);
        bool is_named_open = false;
        for (const OpenRelation& open : open_relations) {
            if (open.ns == ns) is_named_open = true;
        }
        const bool is_sound_seal = reading.is_sealed && reading.fault == ::foundation::fail_closed::seal_fault::none;
        if (is_sound_seal == is_named_open) {
            text += std::meta::display_string_of(ns);
            text += "; ";
        }
    }
    return std::define_static_string(text);
}

constexpr RelationVerdict relations = relation_verdict();

static_assert(relations.relations > 1, "the relation census found no relation, so it proves nothing");
static_assert(relations.neither == 0, unsealed_message());
static_assert(relations.stale_open_entries == 0,
              "an entry of open_relations names a namespace that holds no edge.  Delete the entry.");
static_assert(relations.sealed + relations.named_open == relations.relations);

}  // namespace

int main() {
    std::printf("test_armed_roster: %zu predicates walked, %zu armed, %zu on the unarmed ledger\n", verdict.walked,
                verdict.armed, verdict.ledgered);
    std::printf("test_armed_roster: %zu fail-closed relations, %zu sealed, %zu named open\n", relations.relations,
                relations.sealed, relations.named_open);
    return 0;
}
