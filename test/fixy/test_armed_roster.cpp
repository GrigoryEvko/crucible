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

#include <cstdio>
#include <memory>
#include <optional>
#include <tuple>
#include <type_traits>

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

using BgCtx = fe::ExecCtx<fe::Bg, Row<Effect::Bg>>;
using NvEnd = sess::VendorPinned<::foundation::algebra::lattices::VendorBackend::NV, sess::End>;

}  // namespace armed_roster_witness

namespace w = armed_roster_witness;

// ── foundation ──────────────────────────────────────────────────────

template <>
struct foundation::contracts::armed_cell<::foundation::reflect::detail::is_noexcept_function> {
    using accepts = witnesses<void() noexcept, int(char) noexcept>;
    using refuses = witnesses<void(), int, void (*)() noexcept>;
};

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
struct foundation::contracts::armed_cell<fe::is_exec_ctx> {
    using accepts = witnesses<w::BgCtx, fe::ExecCtx<>>;
    using refuses = witnesses<int, fe::Bg>;
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

// ── fixy: the throws atom, spawn and io ─────────────────────────────

template <>
struct foundation::contracts::armed_cell<::fixy::detail::is_throws_atom> {
    using accepts = witnesses<at::ctrl::throws<>, at::ctrl::throws<w::Plain> const&>;
    using refuses = witnesses<int, std::tuple<at::ctrl::throws<>>, at::ctrl::any_exception>;
};

template <>
struct foundation::contracts::armed_cell<at::spawn::detail::is_detach_with> {
    using accepts = witnesses<at::spawn::detach_with<"drain outlives the owner">>;
    using refuses = witnesses<int, at::spawn::syscall_only<"loader">, at::spawn::subprocess<"helper">>;
};

template <>
struct foundation::contracts::armed_cell<at::spawn::detail::is_syscall_only> {
    using accepts = witnesses<at::spawn::syscall_only<"loader">>;
    using refuses = witnesses<int, at::spawn::detach_with<"drain">, at::spawn::subprocess<"helper">>;
};

template <>
struct foundation::contracts::armed_cell<at::spawn::detail::is_subprocess> {
    using accepts = witnesses<at::spawn::subprocess<"helper">>;
    using refuses = witnesses<int, at::spawn::detach_with<"drain">, at::spawn::syscall_only<"loader">>;
};

template <>
struct foundation::contracts::armed_cell<::fixy::io::engine_is_io_uring> {
    using accepts = witnesses<::fixy::io::engine::IoUring>;
    using refuses = witnesses<void, int, ::fixy::io::zerocopy::Sendfile>;
};

template <>
struct foundation::contracts::armed_cell<::fixy::io::zerocopy_is_simple_transfer> {
    using accepts = witnesses<::fixy::io::zerocopy::Sendfile, ::fixy::io::zerocopy::CopyFileRange>;
    using refuses = witnesses<void, int, ::fixy::io::engine::IoUring>;
};

// ── fixy: the grade readers of the collision rules ──────────────────
//
// The Effect grade has two shapes, a stated with<Es...> and the strict
// pole Row<>.  Each reader answers for the stated shape and stands down
// for the pole, which names no effect.

template <>
struct foundation::contracts::armed_cell<::fixy::collision::detail::row_admits_bg_> {
    using accepts = witnesses<at::with<Effect::Bg>, at::with<Effect::IO, Effect::Bg>>;
    using refuses = witnesses<at::with<Effect::IO>, at::with<>, Row<>>;
};

template <>
struct foundation::contracts::armed_cell<::fixy::collision::detail::row_admits_observable_> {
    using accepts = witnesses<at::with<Effect::Alloc>, at::with<Effect::IO>, at::with<Effect::Block>>;
    using refuses = witnesses<at::with<Effect::Bg>, at::with<>, Row<>>;
};

template <>
struct foundation::contracts::armed_cell<::fixy::collision::detail::row_admits_init_> {
    using accepts = witnesses<at::with<Effect::Init>, at::with<Effect::IO, Effect::Init>>;
    using refuses = witnesses<at::with<Effect::Bg>, at::with<>, Row<>>;
};

template <>
struct foundation::contracts::armed_cell<::fixy::collision::detail::row_admits_alloc_or_io_> {
    using accepts = witnesses<at::with<Effect::Alloc>, at::with<Effect::IO>>;
    using refuses = witnesses<at::with<Effect::Block>, at::with<>, Row<>>;
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

template <>
struct foundation::contracts::armed_cell<::fixy::corpus::detail::is_secret_carrier_> {
    using accepts = witnesses<at::as_secret, at::as_classified>;
    using refuses = witnesses<int, at::as_internal, at::as_public>;
};

template <>
struct foundation::contracts::armed_cell<::fixy::corpus::detail::is_secret_grant_> {
    using accepts =
        witnesses<at::as_secret, at::declassify<::fixy::tags::secret_policy::WireSerialize>>;
    using refuses = witnesses<int, at::as_internal, at::as_public>;
};

template <>
struct foundation::contracts::armed_cell<::fixy::corpus::detail::is_internal_> {
    using accepts = witnesses<at::as_internal>;
    using refuses = witnesses<int, at::as_secret, at::as_public>;
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

template <>
struct foundation::contracts::armed_cell<::fixy::is_fn> {
    using accepts = witnesses<::fixy::fn<int>>;
    using refuses = witnesses<int, w::Plain>;
};

template <>
struct foundation::contracts::armed_cell<::fixy::is_scoped_view> {
    using accepts = witnesses<::fixy::ScopedView<w::Plain, w::GlobalTag>>;
    using refuses = witnesses<int, w::Plain>;
};

// ── fixy: session protocol shapes ───────────────────────────────────

template <>
struct foundation::contracts::armed_cell<sess::is_send> {
    using accepts = witnesses<sess::Send<int, sess::End>, sess::VendorPinned<::foundation::algebra::lattices::VendorBackend::NV,
                                                                             sess::Send<int, sess::End>>>;
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
    using refuses = witnesses<sess::Continue, sess::Send<int, sess::Continue>>;
};

namespace {

// ── the ledger ──────────────────────────────────────────────────────
//
// The predicates the walk finds and that have no cell.  Each entry says
// why.  The ledger can only shrink: an entry that gains a cell, or that
// names a predicate the walk stops finding, fails the walk.
inline constexpr std::meta::info unarmed_ledger[] = {
    // Two type arguments.  A cell takes a predicate over one, so the
    // walk counts these unproven.
    ^^::foundation::effects::is_subrow,
    ^^::foundation::permissions::detail::ctx_admits_tuple,
    // Three arguments, one of them a value.
    ^^::fixy::mach::detail::can_transition_impl,
    // A stage names a function pointer, and no witness here can name a
    // stage without the stage machinery the pipeline tests build.
    ^^::fixy::concurrent::detail::is_stage,
    // The primary answers true, and every specialization recurses to a
    // true leaf, so no protocol is refused.  A predicate that accepts
    // everything is the unarmed shape this file exists to find.
    ^^::fixy::session::is_dual_involutive,
};

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

}  // namespace

int main() {
    std::printf("test_armed_roster: %zu predicates walked, %zu armed, %zu on the unarmed ledger\n", verdict.walked,
                verdict.armed, verdict.ledgered);
    return 0;
}
