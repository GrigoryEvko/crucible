// The compile-time checks of fixy/Witnessed.h.

#include <fixy/Witnessed.h>

namespace fixy {

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

// The borrows are spelled at a named brand rather than at the erased
// one.  A borrow spelled with no brand argument is DefaultBrand, which
// utils/scripts/check-brand-drain.py counts and does not let a new file add,
// and every claim below reads the same at either brand.
struct probe_brand {};

using PureBorrow = Borrowed<int, pure_tag, probe_brand>;
using SpilledBorrow = Borrowed<int, spilled_tag, probe_brand>;
using UnrowedBorrow = Borrowed<int, unrowed_tag, probe_brand>;

// The recogniser answers for both kinds and for nothing else.  An empty
// recogniser fails the first line, a universal one the second.
static_assert(IsWitnessKind<witness::AtProtocol<::fixy::session::End>>);
static_assert(IsWitnessKind<witness::UnderRow<pure_tag>>);
static_assert(!IsWitnessKind<int>);
static_assert(!IsWitnessKind<pure_tag>);
static_assert(!IsWitnessKind<PureBorrow>);

// EFFECT, as a type question.  A pure tag is admitted by a foreground
// context and a spilled one is not, and the context that carries IO
// admits both.
static_assert(Discharges<witness::UnderRow<pure_tag>, FgCtx>);
static_assert(!Discharges<witness::UnderRow<spilled_tag>, FgCtx>, "a foreground context does not admit an IO region");
static_assert(Discharges<witness::UnderRow<spilled_tag>, IoCtx>, "and a context that carries IO does");
static_assert(!Discharges<witness::UnderRow<pure_tag>, int>, "a context is a context, not any type");

// TEMPORAL, as a type question.  A handle still at the position the
// borrow was minted at discharges the witness, and the same handle
// spelling at any other position does not.  Naming the two handle types
// is the whole test: the protocol is in the type, so the compiler can
// answer without a handle ever being built.
struct probe_payload {
    int value = 0;
};
struct probe_wire {
    int last_sent = 0;
};

using AtSend = ::fixy::session::SessionHandle<::fixy::session::Send<probe_payload, ::fixy::session::End>, probe_wire>;
using AtEnd = ::fixy::session::SessionHandle<::fixy::session::End, probe_wire>;
using SendWitness = witness::AtProtocol<::fixy::session::Send<probe_payload, ::fixy::session::End>>;

static_assert(Discharges<SendWitness, AtSend>, "a handle still at the position the borrow was minted at reads it");
static_assert(!Discharges<SendWitness, AtEnd>, "and the same session one step on does not");
static_assert(!Discharges<witness::AtProtocol<::fixy::session::End>, AtSend>, "nor one step short");

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
concept HasBareAccessor = requires(W const& w) { w.size(); } || requires(W const& w) { w.data(); }
                       || requires(W const& w) { w[std::size_t{0}]; };
static_assert(!HasBareAccessor<Witnessed<PureBorrow, witness::UnderRow<pure_tag>>>,
              "a witnessed borrow that forwarded an accessor would be a second door with no witness on it");

// And deref is refused for a context that does not admit the row, at
// the same carrier that admits the one that does.
template <typename Wit, typename Ctx>
concept CanDeref = requires(Wit const& w, Ctx const& ctx) { deref(w, ctx); };
static_assert(CanDeref<Witnessed<PureBorrow, witness::UnderRow<pure_tag>>, FgCtx>);
static_assert(!CanDeref<Witnessed<SpilledBorrow, witness::UnderRow<spilled_tag>>, FgCtx>);
static_assert(CanDeref<Witnessed<SpilledBorrow, witness::UnderRow<spilled_tag>>, IoCtx>);

// A tag that declares no row cannot be witnessed under one, because
// there is no row to weigh a context against.
template <typename B>
concept CanMintUnder = requires(B b) { mint_witnessed_under(b); };
static_assert(CanMintUnder<PureBorrow>);
static_assert(!CanMintUnder<UnrowedBorrow>, "a tag that declares no row states no obligation");

// The carrier is the borrow and the obligation is a type, so it costs
// what the borrow costs.
static_assert(sizeof(Witnessed<PureBorrow, witness::UnderRow<pure_tag>>) == sizeof(PureBorrow));

}  // namespace detail::witnessed_self_test

}  // namespace fixy
