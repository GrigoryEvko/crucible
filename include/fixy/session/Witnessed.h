#pragma once

// A borrow that names what has to be presented before it can be read.
//
// A View of the Region family (foundation/core/Region.h) borrows a run of
// elements, and it does not say WHEN the borrow may be read.  Its element
// accessors are callable from anywhere the value is in scope, and lexical
// scope is the only thing standing between a borrow and a read.  That is
// the guarantee Rust gives, and it is the whole of it.
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
// witnesses in the check file of this header pin the recogniser in both
// directions, so a kind cannot be added without being recognised and
// the recogniser cannot quietly grow to admit everything.
//
// Two kinds ship, and the two that do not are named rather than
// implied:
//
//   AtProtocol<Handle> — TEMPORAL.  A borrow minted at one session
//     handle is read only against that handle, still where it stood.
//     The witness names the full type of the handle: its protocol
//     position, Resource, loop context, policy and permission set.  So
//     a handle of another type does not compile at the read.  Two
//     handles of one type are two sessions of the same shape, or one
//     session at two times, after a loop iteration gave the handle its
//     type again.  The carrier keeps the record of the session in the
//     watch, the generation of the record and the count of steps, and
//     the read ends the process when the presented handle differs in
//     one of them.  Validity is one handle at one time, not lexical
//     scope.
//
//   UnderRow<Tag> — EFFECT.  The mint takes the permission tag of the
//     region that the borrow views, and permission_row<Tag> says what
//     touching that region costs: DiskSpilledRegionTag is Row<IO, Block>,
//     GpuMemoryTag is Row<Alloc>.  A plain borrow does not check that row.
//     A witnessed borrow is read only against a context whose own row
//     admits the tag's, which is the same Subrow test the permission pool
//     applies to a lend.
//
//   EPOCH is not built.  It needs an advance that mints a fresh epoch
//     brand and consumes the old one, and neither include/foundation
//     nor include/fixy has a replay engine to drive that advance.
//
//   RESIDENCY is not built.  It needs the execution context to carry a
//     node, a heat and a residency, and foundation::effects::ExecCtx
//     has two axes, a capability source and a row.  So a borrow here
//     has no node to inherit and no node to compare against.
//
// What a witnessed borrow does not do, stated rather than implied
// ---------------------------------------------------------------
//   - deref hands the borrow back by value, and the gate is at the
//     deref and nowhere after it.  A caller who stores the result and
//     reads it later is outside what this type can see.
//   - A session handle that has been consumed is still an lvalue of the
//     same type.  A move or a step lets go of the record, so the read
//     against a consumed handle ends the process.  That answer is at
//     run time, as the answer for two handles of one type is.
//   - The run-time check needs the record of a checking policy.  A
//     handle under check::Off keeps none, so mint_witnessed_at refuses
//     it.  A handle at End, and a handle of a constant evaluation, have
//     no record either, and a read against them ends the process.
//   - An UnderRow witness is a type and carries no value, so two regions under
//     one tag share one UnderRow witness.  A View names no region, so
//     nothing here separates those.
//   - The caller names the tag of an UnderRow witness, and nothing checks
//     that the View is of a region under that tag.

#include <fixy/session/Handle.h>
#include <fixy/session/Watch.h>
#include <foundation/Platform.h>
#include <foundation/core/Fatal.h>
#include <foundation/core/Region.h>
#include <foundation/effects/Ctx.h>
#include <foundation/permissions/Permission.h>
#include <foundation/reflect/Instance.h>

#include <cstdint>
#include <meta>
#include <type_traits>
#include <utility>

// The claims the carriers in this header make that no lattice grades.
// foundation/diag/RowHash.h folds each identity, so every carrier here
// takes a cache slot of its own rather than the zero a bare payload has.
namespace fixy::row_discipline {
template <typename Witness>
struct witnessed;
}  // namespace fixy::row_discipline

namespace fixy::session {

// ── The witness kinds ────────────────────────────────────────────────
//
// Each kind is an empty type carrying only its parameter.  The kind
// holds no answer of its own: witness_discharges below reads the
// parameter of the kind, and a caller reads it only through Discharges.
namespace witness {

// TEMPORAL.  The obligation is one session handle at one time.  The type
// names the handle, and the carrier keeps the time.
template <typename Handle>
struct AtProtocol {
    using handle_type = Handle;
};

// EFFECT.  The obligation is the row the region's tag carries.
template <typename Tag>
struct UnderRow {
    using tag_type = Tag;
};

}  // namespace witness

// The closed family of kinds.  A kind that is not one of these is not a
// witness, so a caller cannot mint an obligation the gate cannot read.
// Adding a kind is one more reflection of a template here, and the
// witnesses in the check file of this header fail if this line and the
// kinds above stop agreeing.
template <typename W>
concept IsWitnessKind = ::foundation::reflect::IsInstanceOfAny<W, ^^witness::AtProtocol, ^^witness::UnderRow>;

// ── The one gate ─────────────────────────────────────────────────────
//
// True when the presented type discharges the witness.  An AtProtocol
// witness takes a handle of its type, and nothing else: the presented type
// is an instance of the one handle template, and it is the parameter of
// the witness, with each of its arguments.  The carrier then checks the
// time at the read (detail::require_same_time).  An UnderRow witness takes an execution
// context whose row admits the row of its tag, the same test a lend from
// the permission pool applies.  A tag with no row declared is refused,
// because has_permission_row answers false for it.
// The answer is a function at namespace scope that is not a template, so
// no translation unit can specialize it.
[[nodiscard]] consteval bool witness_discharges(std::meta::info witness, std::meta::info presented) {
    const std::meta::info kind = std::meta::dealias(std::meta::remove_cvref(witness));
    const std::meta::info value = std::meta::dealias(std::meta::remove_cvref(presented));
    if (!std::meta::has_template_arguments(kind)) return false;
    const std::meta::info obligation = std::meta::template_arguments_of(kind)[0];
    if (std::meta::template_of(kind) == ^^witness::AtProtocol) {
        return std::meta::has_template_arguments(value)
            && std::meta::template_of(value) == (^^::fixy::session::SessionHandle)
            && value == std::meta::dealias(obligation);
    }
    if (std::meta::template_of(kind) == ^^witness::UnderRow) {
        return std::meta::extract<bool>(
            std::meta::substitute(^^::foundation::permissions::CtxAdmitsPermission, {obligation, value}));
    }
    return false;
}

// Written once, for every kind.  A caller presents a value; the gate
// asks the witness whether that value discharges it.
template <typename Witness, typename Presented>
concept Discharges = IsWitnessKind<Witness> && witness_discharges(^^Witness, ^^Presented);

namespace detail {

// The door's key.  Only the mints below hold it, so a witnessed borrow
// cannot be assembled beside the mint that weighed the witness.
struct witnessed_mint_t {};

// The time of one handle: the record of its session in the watch, the
// generation of that record, and the count of steps that led to the
// handle.  A record that a later session claimed has a new generation,
// and a handle after a loop iteration has a higher count.
struct handle_stamp {
    watch::endpoint_id endpoint = watch::endpoint_id::none;
    std::uint32_t generation = 0;
    std::uint64_t steps = 0;
};

// What an UnderRow witness keeps: nothing, because its obligation is a
// type only.
struct no_stamp {};

template <typename Witness>
struct stamp_of {
    using type = no_stamp;
};
template <typename Handle>
struct stamp_of<witness::AtProtocol<Handle>> {
    using type = handle_stamp;
};

template <typename Witness>
inline constexpr bool is_protocol_witness_v = std::is_same_v<typename stamp_of<Witness>::type, handle_stamp>;

// The stamp of a handle now.  A constant evaluation has no watch, so the
// stamp then holds no record.
template <typename Handle>
[[nodiscard]] constexpr handle_stamp stamp_handle(Handle const& handle) noexcept {
    const watch::endpoint_id endpoint = handle.watch_endpoint();
    std::uint32_t generation = 0;
    if !consteval {
        generation = watch::record_generation(endpoint);
    }
    return handle_stamp{endpoint, generation, handle.step_count()};
}

[[noreturn, gnu::cold, gnu::noinline]] inline void report_stale_witness() noexcept {
    ::foundation::core::fatal(
        "fixy::session: diagnostic [Witness_Stale]: a borrow witnessed at a session handle was read against a "
        "handle of the same type that is not that handle at that time.  It is another session of the same shape, "
        "the same session after a step or a loop iteration, a handle that a move or a step consumed, or a handle "
        "with no record in the watch.  Read the borrow against the handle that it was minted at, before the next "
        "step of that handle.");
}

// Ends the process unless the presented handle stands where the handle of
// the mint stood: one record, one generation of it, and one count of
// steps.  A stamp with no record is refused, so a handle with no record
// never discharges a witness.
template <typename Handle>
constexpr void require_same_time(handle_stamp const& minted, Handle const& presented) noexcept {
    const handle_stamp now = stamp_handle(presented);
    const bool is_same_time = minted.endpoint != watch::endpoint_id::none && now.endpoint == minted.endpoint
                           && now.generation == minted.generation && now.steps == minted.steps;
    if (!is_same_time) [[unlikely]]
        report_stale_witness();
}

}  // namespace detail

// A borrow that a witness can guard: a View of the Region family.
template <typename UBorrow>
concept WitnessableBorrow = ::foundation::reflect::IsInstanceOf<UBorrow, ^^::foundation::core::View>;

// The gate of mint_witnessed_at: a borrow, and a handle under a policy
// that keeps the record of its session in the watch.  Under check::Off the
// handle keeps no record and no count of steps, so the read could not
// check the time.
template <typename UBorrow, typename Policy>
concept WitnessableAt = WitnessableBorrow<UBorrow> && AbandonmentPolicy<Policy> && bool{Policy::checks_abandonment};

// The gate of mint_witnessed_under: a borrow, and a tag that declares the
// row of its region.  A tag with no row is refused, because
// has_permission_row answers false for it.
template <typename UBorrow, typename Tag>
concept WitnessableUnder = WitnessableBorrow<UBorrow> && ::foundation::permissions::has_permission_row(^^Tag);

// ── The carrier ──────────────────────────────────────────────────────
//
// It publishes no accessor of the borrow it holds.  data(), size(),
// operator[] and the rest are the borrow's, and the borrow is reachable
// only through deref.  A carrier that forwarded even size() would be a
// second door with no witness on it.
template <typename Borrow, typename Witness>
    requires(WitnessableBorrow<Borrow> && IsWitnessKind<Witness>)
class [[nodiscard]] Witnessed {
public:
    using borrow_type = Borrow;
    using witness_type = Witness;
    using element_type = typename Borrow::element_type;
    using row_discipline = ::fixy::row_discipline::witnessed<Witness>;
    using row_payload = Borrow;

private:
    using stamp_type = typename detail::stamp_of<Witness>::type;

    Borrow borrow_{};
    // The time of the handle of the mint, for an AtProtocol witness.  An
    // UnderRow witness keeps an empty stamp, which takes no storage.
    [[no_unique_address]] stamp_type stamp_{};

    constexpr Witnessed(detail::witnessed_mint_t, Borrow borrow, stamp_type stamp = {}) noexcept
        : borrow_{borrow}, stamp_{stamp} {}

    template <typename UBorrow, typename Proto, typename Resource, typename LoopCtx,
              ::fixy::session::AbandonmentPolicy Policy, typename PS>
        requires WitnessableAt<UBorrow, Policy>
    friend constexpr Witnessed<
        UBorrow, witness::AtProtocol<::fixy::session::SessionHandle<Proto, Resource, LoopCtx, Policy, PS>>>
    mint_witnessed_at(::fixy::session::SessionHandle<Proto, Resource, LoopCtx, Policy, PS> const& handle,
                      UBorrow borrow) noexcept;

    template <typename Tag, typename UBorrow>
        requires WitnessableUnder<UBorrow, Tag>
    friend constexpr Witnessed<UBorrow, witness::UnderRow<Tag>> mint_witnessed_under(UBorrow borrow) noexcept;

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
    // An AtProtocol witness also checks the time of the presented handle.
    template <typename Presented>
        requires Discharges<Witness, Presented>
    [[nodiscard]] friend constexpr Borrow deref(Witnessed const& witnessed, Presented const& presented) noexcept {
        if constexpr (detail::is_protocol_witness_v<Witness>) {
            detail::require_same_time(witnessed.stamp_, presented);
        }
        return witnessed.borrow_;
    }
};

// ── The mints ────────────────────────────────────────────────────────

// TEMPORAL.  The handle is taken by const reference: reading its
// position is not advancing it, and a mint that consumed the handle
// would leave nothing to present.  The witness names the full type of the
// handle, and the carrier keeps its time.
template <typename UBorrow, typename Proto, typename Resource, typename LoopCtx,
          ::fixy::session::AbandonmentPolicy Policy, typename PS>
    requires WitnessableAt<UBorrow, Policy>
[[nodiscard]] constexpr Witnessed<
    UBorrow, witness::AtProtocol<::fixy::session::SessionHandle<Proto, Resource, LoopCtx, Policy, PS>>>
mint_witnessed_at(::fixy::session::SessionHandle<Proto, Resource, LoopCtx, Policy, PS> const& handle
                  CRUCIBLE_LIFETIMEBOUND,
                  UBorrow borrow) noexcept {
    using Handle = ::fixy::session::SessionHandle<Proto, Resource, LoopCtx, Policy, PS>;
    return Witnessed<UBorrow, witness::AtProtocol<Handle>>{detail::witnessed_mint_t{}, borrow,
                                                           detail::stamp_handle(handle)};
}

// The handle names a position that outlives nothing: a temporary handle
// is consumed at the end of the full expression, so a borrow witnessed
// against one is witnessed against a position already gone.
template <typename UBorrow, typename Proto, typename Resource, typename LoopCtx,
          ::fixy::session::AbandonmentPolicy Policy, typename PS>
    requires WitnessableBorrow<UBorrow>
constexpr auto mint_witnessed_at(::fixy::session::SessionHandle<Proto, Resource, LoopCtx, Policy, PS> const&&,
                                 UBorrow) = delete("the handle is released at the end of the full expression, so the "
                                                   "protocol position the borrow would name is already gone; give the "
                                                   "handle a name that outlives the borrow");

// EFFECT.  The caller names the permission tag of the region that the
// borrow views: mint_witnessed_under<Tag>(view).
template <typename Tag, typename UBorrow>
    requires WitnessableUnder<UBorrow, Tag>
[[nodiscard]] constexpr Witnessed<UBorrow, witness::UnderRow<Tag>> mint_witnessed_under(UBorrow borrow) noexcept {
    return Witnessed<UBorrow, witness::UnderRow<Tag>>{detail::witnessed_mint_t{}, borrow};
}

}  // namespace fixy::session
