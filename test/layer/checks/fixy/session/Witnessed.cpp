// The compile-time checks of fixy/session/Witnessed.h.

#include <fixy/session/Witnessed.h>

namespace fixy::session {

namespace detail::witnessed_self_test {

namespace eff = ::foundation::effects;
namespace perm = ::foundation::permissions;

struct pure_tag {
    using permission_row = eff::Row<>;
};
struct spilled_tag {
    using permission_row = eff::Row<eff::Effect::IO>;
};
struct unrowed_tag {};

using FgCtx = eff::ExecCtx<eff::ctx_cap::Fg, eff::Row<>>;
using IoCtx = eff::ExecCtx<eff::Bg, eff::Row<eff::Effect::Bg, eff::Effect::IO>>;

// The permission of a handle below is spelled at a named brand rather
// than at the erased one.  A permission spelled with no brand argument is
// DefaultBrand, which the rule erased-brand counts and does not let a new
// file add, and every claim below reads the same at either brand.
struct probe_brand {};

// The borrow of each cell.  A View names no region, so the tag of a cell
// is the argument of its witness.
using ProbeView = ::foundation::core::View<int>;

// The recogniser answers for both kinds and for nothing else.  An empty
// recogniser fails the first line, a universal one the second.
static_assert(IsWitnessKind<witness::AtProtocol<::fixy::session::End>>);
static_assert(IsWitnessKind<witness::UnderRow<pure_tag>>);
static_assert(!IsWitnessKind<int>);
static_assert(!IsWitnessKind<pure_tag>);
static_assert(!IsWitnessKind<ProbeView>);

// EFFECT, as a type question.  A pure tag is admitted by a foreground
// context and a spilled one is not, and the context that carries IO
// admits both.
static_assert(Discharges<witness::UnderRow<pure_tag>, FgCtx>);
static_assert(!Discharges<witness::UnderRow<spilled_tag>, FgCtx>, "a foreground context does not admit an IO region");
static_assert(Discharges<witness::UnderRow<spilled_tag>, IoCtx>, "and a context that carries IO does");
static_assert(!Discharges<witness::UnderRow<pure_tag>, int>, "a context is a context, not any type");

// TEMPORAL, as a type question.  The witness names the full type of the
// handle that it was minted at.  A handle of that type passes the gate,
// and the carrier then checks the time at the read.  A handle at another
// position, over another Resource, under another policy or with another
// permission set does not pass the gate.  Naming the handle types is the
// whole test, so the compiler answers without a handle ever being built.
struct probe_payload {
    int value = 0;
};
struct probe_wire {
    int last_sent = 0;
};
struct other_wire {
    int last_sent = 0;
};
struct probe_region {
    using permission_row = ::foundation::effects::Row<>;
};

using Sending = ::fixy::session::Send<probe_payload, ::fixy::session::End>;
using AtSend = ::fixy::session::SessionHandle<Sending, probe_wire>;
using AtEnd = ::fixy::session::SessionHandle<::fixy::session::End, probe_wire>;
using AtSendOtherWire = ::fixy::session::SessionHandle<Sending, other_wire>;
using AtSendUnchecked = ::fixy::session::SessionHandle<Sending, probe_wire, void, ::fixy::session::check::Off>;
using AtSendHolding = ::fixy::session::SessionHandle<
    Sending, probe_wire, void, ::fixy::session::check::Enforced,
    ::foundation::permissions::PermSet<::foundation::permissions::Permission<probe_region, probe_brand>>>;
using SendWitness = witness::AtProtocol<AtSend>;

static_assert(Discharges<SendWitness, AtSend>, "a handle of the type of the mint passes the gate");
static_assert(!Discharges<SendWitness, AtEnd>, "and the same session one step on does not");
static_assert(!Discharges<witness::AtProtocol<AtEnd>, AtSend>, "nor one step short");
static_assert(!Discharges<SendWitness, AtSendOtherWire>, "a session over another Resource is another session");
static_assert(!Discharges<SendWitness, AtSendUnchecked>, "a session under another policy is another session");
static_assert(!Discharges<SendWitness, AtSendHolding>, "a session with another permission set is another session");
static_assert(!Discharges<witness::AtProtocol<Sending>, AtSend>, "a protocol alone names no handle");

// The handle is linear, which is what makes the refusal mean something:
// advancing the session consumes the handle at the old position, so
// there is no second handle left at it to present.
static_assert(!std::is_copy_constructible_v<AtSend>, "a copyable handle would leave a witness behind at every step");

// A witness of one kind is not discharged by what discharges the other.
static_assert(!Discharges<SendWitness, FgCtx>, "a context does not stand in for a protocol position");
static_assert(!Discharges<witness::UnderRow<pure_tag>, AtSend>, "and a handle does not stand in for a row");

// The carrier publishes no accessor, so a read that skipped the gate
// has nothing to call.
template <typename W>
concept HasBareAccessor = requires(W const& w) { w.size(); } || requires(W const& w) { w.get(std::size_t{0}); }
                       || requires(W const& w) { w.begin(); };
static_assert(!HasBareAccessor<Witnessed<ProbeView, witness::UnderRow<pure_tag>>>,
              "a witnessed borrow that forwarded an accessor would be a second door with no witness on it");

// And deref is refused for a context that does not admit the row, at
// the same carrier that admits the one that does.
template <typename Wit, typename Ctx>
concept CanDeref = requires(Wit const& w, Ctx const& ctx) { deref(w, ctx); };
static_assert(CanDeref<Witnessed<ProbeView, witness::UnderRow<pure_tag>>, FgCtx>);
static_assert(!CanDeref<Witnessed<ProbeView, witness::UnderRow<spilled_tag>>, FgCtx>);
static_assert(CanDeref<Witnessed<ProbeView, witness::UnderRow<spilled_tag>>, IoCtx>);

// A tag that declares no row cannot be witnessed under one, because
// there is no row to weigh a context against.  The witnessed value is a
// View.
template <typename B, typename Tag>
concept CanMintUnder = requires(B b) { mint_witnessed_under<Tag>(b); };
static_assert(CanMintUnder<ProbeView, pure_tag>);
static_assert(!CanMintUnder<ProbeView, unrowed_tag>, "a tag that declares no row states no obligation");
static_assert(!CanMintUnder<int, pure_tag>, "the witnessed value is a borrow");

// A protocol witness checks the time of the handle at the read, from the
// record of the session.  A handle under check::Off keeps no record, so
// the gate of the mint refuses its policy.
static_assert(WitnessableAt<ProbeView, check::Enforced>);
static_assert(WitnessableAt<ProbeView, check::Cancel>);
static_assert(!WitnessableAt<ProbeView, check::Off>, "a handle under check::Off keeps no record to check");
static_assert(!WitnessableAt<int, check::Enforced>, "the witnessed value is a borrow");

// The carrier is the borrow and the obligation is a type, so it costs
// what the borrow costs.
static_assert(sizeof(Witnessed<ProbeView, witness::UnderRow<pure_tag>>) == sizeof(ProbeView));

}  // namespace detail::witnessed_self_test

}  // namespace fixy::session
