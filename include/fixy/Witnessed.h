#pragma once

// A borrow that names what has to be presented before it can be read.
//
// fixy/Borrowed.h gives a borrow an owner tag and a brand, so a borrow
// of one region cannot stand in for a borrow of another.  Neither says
// WHEN the borrow may be read.  Its element accessors are callable from
// anywhere the value is in scope, and lexical scope is the only thing
// standing between a borrow and a read.  That is the guarantee Rust
// gives, and it is the whole of it.
//
// A witnessed borrow says more.  It is minted against a witness, it
// exposes no accessor of its own, and the one way to the borrow inside
// is deref(witnessed, presented), which the compiler admits only when
// what the caller presents discharges the obligation the borrow was
// minted under.  Validity stops being a region of source text and
// becomes a fact the caller has to still hold.
//
// One gate, instantiated per kind
// -------------------------------
// The gate is the `Discharges` concept below, and it is written once.
// A witness kind is a type that answers one question — does this
// presented value discharge me — and each kind answers it its own way.
// Adding a kind is a type and one line in the recogniser, and the
// witnesses at the foot of this header pin the recogniser in both
// directions, so a kind cannot be added without being recognised and
// the recogniser cannot quietly grow to admit everything.
//
// Two kinds ship, and the two that do not are named rather than
// implied:
//
//   AtProtocol<Proto> — TEMPORAL.  A borrow minted while a session
//     handle sits at protocol position Proto is read only against a
//     handle still at Proto.  The handle is linear: its consumer
//     methods are rvalue-qualified and its copy is deleted, so
//     advancing the session leaves no handle at Proto to present.
//     Validity is protocol position, not lexical scope.
//
//   UnderRow<Tag> — EFFECT.  A borrow of a region carries the region's
//     owner tag, and permission_row<Tag> says what touching that region
//     costs: DiskSpilledRegionTag is Row<IO, Block>, GpuMemoryTag is
//     Row<Alloc>.  The row was always on the region and never reached
//     the borrow.  Here the borrow is read only against a context whose
//     own row admits the tag's, which is the same Subrow test the
//     permission pool already applies to a lend.
//
//   EPOCH is not built.  It needs an advance that mints a fresh epoch
//     brand and consumes the old one, and neither include/foundation
//     nor include/fixy has a replay engine or an epoch type.  Measured
//     2026-09-20: the word appears twice in the two trees, once in an
//     arena comment and once in a sentence in fixy/session/Handle.h
//     naming gates that live in the frozen tree.
//
//   RESIDENCY is not built.  It needs the execution context to carry a
//     node, a heat and a residency, and foundation::effects::ExecCtx
//     has two axes, a capability source and a row.  The five residency
//     axes are declared only in the frozen include/crucible/effects,
//     so a borrow here has no node to inherit and no node to compare
//     against.
//
// What a witnessed borrow does not do, stated rather than implied
// ---------------------------------------------------------------
//   - deref hands the borrow back by value, and the gate is at the
//     deref and nowhere after it.  A caller who stores the result and
//     reads it later is outside what this type can see.
//   - A session handle that has been consumed is still an lvalue whose
//     protocol_type is unchanged, so presenting a moved-from handle
//     discharges AtProtocol.  The handle already carries a consumed
//     tracker in SessionHandleBase; routing deref through it would
//     close this at run time, and it is a run-time answer rather than
//     the compile-time one the rest of this header gives.
//   - A witness is a type and carries no value, so two regions under
//     one tag share one UnderRow witness.  The brand on the borrow is
//     what separates those, and it is a separate axis.

#include <fixy/Borrowed.h>
#include <fixy/session/Handle.h>
#include <foundation/Platform.h>
#include <foundation/effects/Ctx.h>
#include <foundation/permissions/Permission.h>
#include <foundation/reflect/Instance.h>

#include <meta>
#include <type_traits>
#include <utility>

namespace fixy {

// ── The witness kinds ────────────────────────────────────────────────
//
// Each kind is an empty type carrying only its parameter, and each
// answers `admits` for itself.  The answer is a variable template
// rather than a member concept because a concept cannot be a class
// member, and a caller reads it only through Discharges below.
namespace witness {

namespace detail {

// The second question is asked only of a type that answered the first.
// Written as one expression the two would both be instantiated, and
// reading protocol_type off a context that has none is an error rather
// than an answer.  `if constexpr` is what makes the order real.
template <typename Presented, typename Proto>
[[nodiscard]] consteval bool is_handle_at() noexcept {
    if constexpr (::foundation::reflect::IsInstanceOf<Presented, ^^::fixy::session::SessionHandle>) {
        return std::is_same_v<typename Presented::protocol_type, Proto>;
    } else {
        return false;
    }
}

}  // namespace detail

// TEMPORAL.  The obligation is a protocol position.
template <typename Proto>
struct AtProtocol {
    using protocol_type = Proto;

    // A handle at this position, and nothing else.  The recognition is
    // structural rather than a list of handle spellings: it asks
    // whether the presented type is an instance of the one handle
    // template, and then whether that instance sits at Proto.
    template <typename Presented>
    static constexpr bool admits = detail::is_handle_at<std::remove_cvref_t<Presented>, Proto>();
};

// EFFECT.  The obligation is the row the region's tag carries.
template <typename Tag>
struct UnderRow {
    using tag_type = Tag;

    // The same test a lend from the permission pool applies, reused
    // rather than restated: the presented type is an execution context
    // and its row admits the tag's row.  A tag with no row declared is
    // refused here, because permission_row_lookup answers only for a
    // tag that declares one.
    template <typename Presented>
    static constexpr bool admits =
        ::foundation::permissions::CtxAdmitsPermission<Tag, std::remove_cvref_t<Presented>>;
};

}  // namespace witness

// The closed family of kinds.  A kind that is not one of these is not a
// witness, so a caller cannot mint an obligation the gate cannot read.
// Adding a kind is one more reflection of a template here, and the
// witnesses at the foot of this header fail if this line and the kinds
// above stop agreeing.
template <typename W>
concept IsWitnessKind =
    ::foundation::reflect::IsInstanceOfAny<W, ^^witness::AtProtocol, ^^witness::UnderRow>;

// ── The one gate ─────────────────────────────────────────────────────
//
// Written once, for every kind.  A caller presents a value; the gate
// asks the witness whether that value discharges it.  Nothing here
// knows what a protocol or a row is.
template <typename Witness, typename Presented>
concept Discharges = IsWitnessKind<Witness> && Witness::template admits<std::remove_cvref_t<Presented>>;

namespace detail {

// The door's key.  Only the mints below hold it, so a witnessed borrow
// cannot be assembled beside the mint that weighed the witness.
struct witnessed_mint_t {};

}  // namespace detail

// ── The carrier ──────────────────────────────────────────────────────
//
// It publishes no accessor of the borrow it holds.  data(), size(),
// operator[] and the rest are the borrow's, and the borrow is reachable
// only through deref.  A carrier that forwarded even size() would be a
// second door with no witness on it.
template <typename Borrow, typename Witness>
    requires(IsBorrowed<Borrow> && IsWitnessKind<Witness>)
class [[nodiscard]] Witnessed {
public:
    using borrow_type = Borrow;
    using witness_type = Witness;
    using element_type = typename Borrow::element_type;
    using source_type = typename Borrow::source_type;
    using brand_type = typename Borrow::brand_type;

private:
    Borrow borrow_{};

    constexpr Witnessed(detail::witnessed_mint_t, Borrow borrow) noexcept : borrow_{borrow} {}

    template <typename UBorrow, typename Proto, typename Resource, typename LoopCtx,
              ::fixy::session::AbandonmentPolicy Policy>
        requires IsBorrowed<UBorrow>
    friend constexpr Witnessed<UBorrow, witness::AtProtocol<Proto>> mint_witnessed_at(
        ::fixy::session::SessionHandle<Proto, Resource, LoopCtx, Policy> const& handle, UBorrow borrow) noexcept;

    template <typename UBorrow>
        requires(IsBorrowed<UBorrow>
                 && ::foundation::permissions::has_permission_row_v<typename UBorrow::source_type>)
    friend constexpr Witnessed<UBorrow, witness::UnderRow<typename UBorrow::source_type>> mint_witnessed_under(
        UBorrow borrow) noexcept;

public:
    // There is no default: a witnessed borrow nobody minted carries an
    // obligation nobody weighed.
    Witnessed() = delete("a witnessed borrow is minted against a witness, never default-constructed");

    // Reading is not consuming, so the carrier is an ordinary value.
    // What it guards is the read, and every read goes through deref.
    constexpr Witnessed(Witnessed const&) noexcept = default;
    constexpr Witnessed& operator=(Witnessed const&) noexcept = default;

    // Names the obligation in a diagnostic.  It is consteval because
    // the string it returns lives in the program's constant data only
    // when it is produced there.
    [[nodiscard]] static consteval std::string_view witness_name() noexcept {
        return std::meta::display_string_of(^^Witness);
    }

    // The one way in.  It is a friend rather than a member so that the
    // call reads as one act with two arguments, which is what it is,
    // and so that the witness sits beside the borrow at the call site.
    template <typename Presented>
        requires Discharges<Witness, Presented>
    [[nodiscard]] friend constexpr Borrow deref(Witnessed const& witnessed, Presented const&) noexcept {
        return witnessed.borrow_;
    }
};

// ── The mints ────────────────────────────────────────────────────────

// TEMPORAL.  The handle is taken by const reference: reading its
// position is not advancing it, and a mint that consumed the handle
// would leave nothing to present.
template <typename UBorrow, typename Proto, typename Resource, typename LoopCtx,
          ::fixy::session::AbandonmentPolicy Policy>
    requires IsBorrowed<UBorrow>
[[nodiscard]] constexpr Witnessed<UBorrow, witness::AtProtocol<Proto>>
mint_witnessed_at(::fixy::session::SessionHandle<Proto, Resource, LoopCtx, Policy> const& handle CRUCIBLE_LIFETIMEBOUND,
                  UBorrow borrow) noexcept {  // MINT-PATTERN-OK: the witness is the handle's position, not a context
    (void)handle;
    return Witnessed<UBorrow, witness::AtProtocol<Proto>>{detail::witnessed_mint_t{}, borrow};
}

// The handle names a position that outlives nothing: a temporary handle
// is consumed at the end of the full expression, so a borrow witnessed
// against one is witnessed against a position already gone.
template <typename UBorrow, typename Proto, typename Resource, typename LoopCtx,
          ::fixy::session::AbandonmentPolicy Policy>
    requires IsBorrowed<UBorrow>
constexpr auto mint_witnessed_at(::fixy::session::SessionHandle<Proto, Resource, LoopCtx, Policy> const&&,
                                 UBorrow) = delete("the handle is released at the end of the full expression, so the "
                                                   "protocol position the borrow would name is already gone; give the "
                                                   "handle a name that outlives the borrow");

// EFFECT.  The tag is the borrow's own source, so there is nothing for
// the caller to spell and nothing to spell wrongly.
template <typename UBorrow>
    requires(IsBorrowed<UBorrow> && ::foundation::permissions::has_permission_row_v<typename UBorrow::source_type>)
[[nodiscard]] constexpr Witnessed<UBorrow, witness::UnderRow<typename UBorrow::source_type>>
mint_witnessed_under(UBorrow borrow) noexcept {  // MINT-PATTERN-OK: the witness is read off the borrow's own tag
    return Witnessed<UBorrow, witness::UnderRow<typename UBorrow::source_type>>{detail::witnessed_mint_t{}, borrow};
}

// ── Self-test ────────────────────────────────────────────────────────
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
// one.  A borrow left at the old arity is DefaultBrand, which
// scripts/check-brand-drain.sh counts and does not let a new file add,
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
