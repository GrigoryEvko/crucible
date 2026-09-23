#pragma once

// What a message does to the permission sets of its two endpoints.
//
// A session endpoint holds a set of exclusive permissions, a PermSet.  A
// payload can carry tokens, so each send and each receive changes the
// set of the endpoint that does it.  This header gives that change for
// one payload.  The handle applies it at each Send and each Recv.
//
// The markers:
//
//   Transferable<T, Tag>     The sender gives the token to the recipient.
//   Returned<T, Tag>         The same move.  The type records that it
//                            closes an exclusive loan.
//   Borrowed<T, Tag>         A read loan.  The sender keeps the token but
//                            cannot use it until the loan ends.  The
//                            recipient gets a read proof.
//   Released<T, Tag>         The end of a read loan.  The lender gets the
//                            use of its token back.
//   DelegatedSession<P, PS>  An endpoint of another protocol.  The tokens
//                            in PS move with it.
//   SharedReader<Tag>        A read share of a pool.  No set changes,
//                            because the pool counts its shares.
//
// A bare Permission in a payload moves as a Transferable does.
//
// ── A read loan is two states, not a free copy ──────────────────────
//
// A read proof that travels to a different thread lets the recipient
// read the region while the sender continues.  So the sender must not
// write the region, or give it away, until the proof comes back.  The
// set records this.  A send of Borrowed<T, Tag> takes Tag out of the
// sender's set and puts LentOut<Tag> in its place, and the recipient's
// set takes BorrowedIn<Tag>.  A send of Released<T, Tag> reverses both.
// While LentOut<Tag> is in the set, no send can move Tag or lend it
// again, because Tag is not in the set.  That is the rule of Saffrich,
// Spaderna, Thiemann and Vasconcelos, "Borrowing from Session Types",
// OOPSLA 2025, errata: a borrowed prefix on a different thread stops the
// suffix until something consumes the prefix.
//
// A protocol that ends with LentOut or BorrowedIn in a set leaves a loan
// open.  The handle refuses to close in that state.
//
// ── How the header finds the tokens ─────────────────────────────────
//
// The old header read the marker only at the top of the payload.  So
// std::pair<Transferable<int, X>, int> travelled with an empty set, and
// the sender kept X in its set while the recipient held the token.  Here
// the walk reads every component of the payload, as
// foundation/reflect/TypeComponents.h defines a component, and it knows
// how each component was reached:
//
//   owned      the root, a base, a by-value member of an owned class
//   aliased    through a pointer, a reference or a reference member
//   in union   a member of a union, which covers std::optional and
//              std::variant, whose storage is a union
//   in array   an element of an array
//
// A token counts only when the walk reaches it owned.  A token reached
// aliased is a second name for a token that stays with the sender.  A
// token in a union can be absent at run time.  A token in an array is
// several tokens of one tag, which a set cannot hold.  The walk refuses
// each of these, and it refuses these shapes too:
//
//   * a read proof, a share or a pool outside its marker;
//   * a type-erasure family: std::function, std::move_only_function,
//     std::copyable_function, std::function_ref and std::any, because
//     the static type does not name what they hold;
//   * a class whose state the walk cannot read, which is the shape of a
//     lambda with captures;
//   * a class that is only declared;
//   * one tag twice in one payload.
//
// A refusal is a compile error.  Its text names the refused type.
//
// What the walk cannot see.  A specialization reached through a pointer
// is read for its template arguments and not for its members, so a
// member that the arguments do not name is not seen.
// TypeComponents.h states the same limit.
//
// Old spelling: include/crucible/sessions/SessionPermPayloads.h,
// namespace crucible::safety::proto.

#include <foundation/Brand.h>
#include <foundation/permissions/PermSet.h>
#include <foundation/permissions/Permission.h>
#include <foundation/permissions/ReadView.h>
#include <foundation/reflect/TypeComponents.h>

#include <any>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <meta>
#include <span>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <vector>

namespace fixy::session {

// ── Loan states in a permission set ─────────────────────────────────
//
// Each is an empty tag, so a PermSet holds it as it holds any other tag.
// The region of LentOut<Tag> and of BorrowedIn<Tag> is Tag.

template <class Tag>
struct LentOut {};

template <class Tag>
struct BorrowedIn {};

// ── The markers ─────────────────────────────────────────────────────
//
// Every marker is move-only.  A copy of a token is a second owner, and
// a copy of a read proof is a second loan that no set records.

template <class T, class Tag>
struct [[nodiscard]] Transferable {
    using payload_type = T;
    using transferred_perm = Tag;

    T value;
    [[no_unique_address]] ::foundation::permissions::Permission<Tag> perm;

    template <class Brand>
    constexpr Transferable(T v, ::foundation::permissions::Permission<Tag, Brand>&& p) noexcept(
        std::is_nothrow_move_constructible_v<T>)
        : value{std::move(v)}, perm{std::move(p)} {}

    Transferable(const Transferable&) = delete("Transferable carries a linear token. A copy is a second owner");
    Transferable& operator=(const Transferable&) =
        delete("Transferable carries a linear token. A copy is a second owner");
    constexpr Transferable(Transferable&&) noexcept = default;
    constexpr Transferable& operator=(Transferable&&) noexcept = default;
    ~Transferable() = default;
};

template <class T, class Tag>
struct [[nodiscard]] Returned {
    using payload_type = T;
    using returned_perm = Tag;

    T value;
    [[no_unique_address]] ::foundation::permissions::Permission<Tag> perm;

    template <class Brand>
    constexpr Returned(T v, ::foundation::permissions::Permission<Tag, Brand>&& p) noexcept(
        std::is_nothrow_move_constructible_v<T>)
        : value{std::move(v)}, perm{std::move(p)} {}

    Returned(const Returned&) = delete("Returned carries a linear token. A copy is a second owner");
    Returned& operator=(const Returned&) = delete("Returned carries a linear token. A copy is a second owner");
    constexpr Returned(Returned&&) noexcept = default;
    constexpr Returned& operator=(Returned&&) noexcept = default;
    ~Returned() = default;
};

// The read proof comes from mint_read_view, which takes a live
// Permission or SharedPermission of the tag.  There is no constructor
// that takes no proof, so a Borrowed cannot exist without its tag.
template <class T, class Tag>
struct [[nodiscard]] Borrowed {
    using payload_type = T;
    using borrowed_perm = Tag;

    T value;
    [[no_unique_address]] ::foundation::permissions::ReadView<Tag> view;

    template <class Brand>
    constexpr Borrowed(T v, ::foundation::permissions::ReadView<Tag, Brand> proof) noexcept(
        std::is_nothrow_move_constructible_v<T>)
        : value{std::move(v)}, view{proof} {}

    Borrowed(const Borrowed&) = delete("Borrowed is one read loan. A copy is a second loan that no set records");
    Borrowed& operator=(const Borrowed&) =
        delete("Borrowed is one read loan. A copy is a second loan that no set records");
    constexpr Borrowed(Borrowed&&) noexcept = default;
    Borrowed& operator=(Borrowed&&) = delete("Borrowed holds a single-binding read proof");
    ~Borrowed() = default;
};

// The end of a read loan carries no token, because the borrower never
// held one.  The type-level check is what makes a send of it sound: the
// sender must hold BorrowedIn<Tag>.
template <class T, class Tag>
struct [[nodiscard]] Released {
    using payload_type = T;
    using released_perm = Tag;

    T value;

    constexpr explicit Released(T v) noexcept(std::is_nothrow_move_constructible_v<T>) : value{std::move(v)} {}

    Released(const Released&) = delete("Released ends one read loan. A copy ends it a second time");
    Released& operator=(const Released&) = delete("Released ends one read loan. A copy ends it a second time");
    constexpr Released(Released&&) noexcept = default;
    constexpr Released& operator=(Released&&) noexcept = default;
    ~Released() = default;
};

// The endpoint itself moves through the transport that does the
// handoff.  This type tells the two sets how authority moves with it.
template <class InnerProto, class InnerPS>
struct [[nodiscard]] DelegatedSession {
    using inner_proto = InnerProto;
    using inner_perm_set = InnerPS;
};

// Defined in fixy/session/Shared.h.  The walk knows the family by name.
template <class Tag, class Brand = ::foundation::brand::DefaultBrand>
class SharedReader;

namespace detail {

// ── Why a payload is refused ────────────────────────────────────────

enum class PayloadRefusal : std::uint8_t {
    None,
    TokenBehindPointer,
    TokenInUnion,
    TokenInArray,
    DuplicateTag,
    BareBorrowOrShare,
    TypeErasure,
    UnreadableState,
    IncompleteType,
};

enum class PayloadReach : std::uint8_t {
    Owned,
    Aliased,
    InUnion,
    InArray,
};

// The families the walk classifies by name.  Each entry reflects a class
// template, so one entry covers each specialization of it.

// A read proof, a share or a pool.  Each travels only inside a marker.
inline constexpr std::meta::info payload_proof_families[] = {
    ^^::foundation::permissions::ReadView,
    ^^::foundation::permissions::SharedPermission,
    ^^::foundation::permissions::SharedPermissionGuard,
    ^^::foundation::permissions::SharedPermissionPool,
};

// A type whose static type does not name what it holds.
inline constexpr std::meta::info payload_type_erasure_families[] = {
    ^^std::function,
    ^^std::move_only_function,
    ^^std::copyable_function,
    ^^std::function_ref,
};

[[nodiscard]] consteval bool payload_is_specialization(std::meta::info type) noexcept {
    return std::meta::has_template_arguments(std::meta::dealias(type));
}

[[nodiscard]] consteval bool payload_family_is(std::meta::info type, std::meta::info family) noexcept {
    return payload_is_specialization(type) && std::meta::template_of(std::meta::dealias(type)) == family;
}

[[nodiscard]] consteval bool payload_family_is_on(std::meta::info type,
                                                  std::span<const std::meta::info> roster) noexcept {
    for (const std::meta::info family : roster) {
        if (payload_family_is(type, family)) return true;
    }
    return false;
}

// A class that is complete and not empty, and that reflects no base and
// no data member.  GCC 16 reflects no capture, so this is the shape of a
// lambda with captures.
[[nodiscard]] consteval bool payload_holds_unreadable_state(std::meta::info type) {
    if (!std::meta::is_class_type(type) || payload_is_specialization(type)) return false;
    if (!std::meta::is_complete_type(type) || std::meta::is_empty_type(type)) return false;
    const auto unchecked = std::meta::access_context::unchecked();
    return std::meta::bases_of(type, unchecked).empty() && std::meta::nonstatic_data_members_of(type, unchecked).empty();
}

// The account of one payload: the tags it moves, lends and releases.
// This form holds vectors, so it lives only inside a constant
// evaluation.  The results cross into types through the reflections of
// PermSet specializations below.
struct PayloadAccount {
    std::vector<std::meta::info> moved;
    std::vector<std::meta::info> lent;
    std::vector<std::meta::info> released;
    bool carries_share = false;
    PayloadRefusal refusal = PayloadRefusal::None;
    std::meta::info refused_type{};
};

[[nodiscard]] consteval PayloadRefusal payload_refusal_for_reach(PayloadReach reach) noexcept {
    switch (reach) {
        case PayloadReach::Aliased:
            return PayloadRefusal::TokenBehindPointer;
        case PayloadReach::InUnion:
            return PayloadRefusal::TokenInUnion;
        case PayloadReach::InArray:
            return PayloadRefusal::TokenInArray;
        case PayloadReach::Owned:
            return PayloadRefusal::None;
        default:
            break;
    }
    return PayloadRefusal::None;
}

// The walk.  Each node is a type, a flag that says if the walk can read
// its members, and how the walk reached it.  Complexity: linear in the number of distinct
// nodes, times the cost of the visited-list scan.
[[nodiscard]] consteval PayloadAccount account_payload(std::meta::info root) {
    namespace refl = ::foundation::reflect;
    namespace fp = ::foundation::permissions;

    struct Node {
        std::meta::info type{};
        bool readable = false;
        PayloadReach reach = PayloadReach::Owned;
    };

    PayloadAccount account;
    std::vector<Node> pending{Node{refl::bare_type(root), true, PayloadReach::Owned}};
    std::vector<Node> visited;

    auto refuse = [&account](PayloadRefusal why, std::meta::info type) consteval {
        if (account.refusal != PayloadRefusal::None) return;
        account.refusal = why;
        account.refused_type = type;
    };
    auto names_tag = [&account](std::meta::info tag) consteval {
        for (const std::meta::info held : account.moved) {
            if (held == tag) return true;
        }
        for (const std::meta::info held : account.lent) {
            if (held == tag) return true;
        }
        for (const std::meta::info held : account.released) {
            if (held == tag) return true;
        }
        return false;
    };
    auto record = [&](std::vector<std::meta::info>& into, std::meta::info tag, std::meta::info at) consteval {
        const std::meta::info bare_tag = std::meta::dealias(tag);
        if (names_tag(bare_tag)) {
            refuse(PayloadRefusal::DuplicateTag, at);
            return;
        }
        into.push_back(bare_tag);
    };
    // A marker or a token counts only when the walk reaches it owned.
    auto owned_or_refuse = [&refuse](const Node& node) consteval {
        if (node.reach == PayloadReach::Owned) return true;
        refuse(payload_refusal_for_reach(node.reach), node.type);
        return false;
    };
    // The value a marker carries is a by-value member of the marker.
    auto push_carried_value = [&pending](std::meta::info value_type) consteval {
        pending.push_back(Node{refl::bare_type(value_type), true, PayloadReach::Owned});
    };

    while (!pending.empty() && account.refusal == PayloadRefusal::None) {
        const Node node = pending.back();
        pending.pop_back();

        // An owned node is not deduplicated.  A class can hold one type
        // twice by value, and each copy holds its own tokens, so each copy
        // counts.  A type cannot hold itself by value, so an owned path
        // has no cycle.  A path through a pointer can, and there the walk
        // visits each node once.
        if (node.reach != PayloadReach::Owned) {
            bool was_visited = false;
            for (const Node& seen : visited) {
                if (seen.type == node.type && seen.reach == node.reach && seen.readable == node.readable) {
                    was_visited = true;
                    break;
                }
            }
            if (was_visited) continue;
            visited.push_back(node);
        }

        const std::meta::info type = node.type;

        if (payload_is_specialization(type)) {
            const std::meta::info family = std::meta::template_of(type);
            const auto arguments = std::meta::template_arguments_of(type);
            if (family == ^^fp::Permission) {
                if (owned_or_refuse(node)) record(account.moved, arguments[0], type);
                continue;
            }
            if (family == ^^Transferable || family == ^^Returned) {
                if (owned_or_refuse(node)) {
                    record(account.moved, arguments[1], type);
                    push_carried_value(arguments[0]);
                }
                continue;
            }
            if (family == ^^Borrowed) {
                if (owned_or_refuse(node)) {
                    record(account.lent, arguments[1], type);
                    push_carried_value(arguments[0]);
                }
                continue;
            }
            if (family == ^^Released) {
                if (owned_or_refuse(node)) {
                    record(account.released, arguments[1], type);
                    push_carried_value(arguments[0]);
                }
                continue;
            }
            if (family == ^^DelegatedSession) {
                if (owned_or_refuse(node)) {
                    for (const std::meta::info tag : std::meta::template_arguments_of(arguments[1])) {
                        record(account.moved, tag, type);
                    }
                }
                continue;
            }
            if (family == ^^SharedReader) {
                if (owned_or_refuse(node)) account.carries_share = true;
                continue;
            }
            if (payload_family_is_on(type, payload_proof_families)) {
                refuse(PayloadRefusal::BareBorrowOrShare, type);
                continue;
            }
            if (payload_family_is_on(type, payload_type_erasure_families)) {
                refuse(PayloadRefusal::TypeErasure, type);
                continue;
            }
        }
        if (type == ^^std::any) {
            refuse(PayloadRefusal::TypeErasure, type);
            continue;
        }

        if (std::meta::is_pointer_type(type) || std::meta::is_reference_type(type)) {
            const std::meta::info element =
                std::meta::is_pointer_type(type) ? std::meta::remove_pointer(type) : std::meta::remove_reference(type);
            const refl::TypeNode reached = refl::node_reached_indirectly(element);
            pending.push_back(Node{reached.type, reached.may_read_members, PayloadReach::Aliased});
            continue;
        }
        if (std::meta::is_array_type(type)) {
            const PayloadReach reach = node.reach == PayloadReach::Owned ? PayloadReach::InArray : node.reach;
            pending.push_back(Node{refl::bare_type(std::meta::remove_all_extents(type)), node.readable, reach});
            continue;
        }
        const bool is_union = std::meta::is_union_type(type);
        if (!std::meta::is_class_type(type) && !is_union) continue;

        if (!node.readable) {
            // Reached through a pointer.  A specialization is read for its
            // template arguments, and each keeps the reach of the node.
            if (payload_is_specialization(type)) {
                for (const std::meta::info argument : std::meta::template_arguments_of(type)) {
                    if (!std::meta::is_type(argument)) continue;
                    const refl::TypeNode reached = refl::node_reached_indirectly(argument);
                    pending.push_back(Node{reached.type, reached.may_read_members, node.reach});
                }
                continue;
            }
            refuse(PayloadRefusal::IncompleteType, type);
            continue;
        }
        if (payload_holds_unreadable_state(type)) {
            refuse(PayloadRefusal::UnreadableState, type);
            continue;
        }

        const PayloadReach member_reach =
            is_union && node.reach == PayloadReach::Owned ? PayloadReach::InUnion : node.reach;
        const auto unchecked = std::meta::access_context::unchecked();
        for (const std::meta::info base : std::meta::bases_of(type, unchecked)) {
            pending.push_back(Node{refl::bare_type(std::meta::type_of(base)), true, member_reach});
        }
        for (const std::meta::info member : std::meta::nonstatic_data_members_of(type, unchecked)) {
            const std::meta::info member_type = std::meta::type_of(member);
            if (std::meta::is_reference_type(member_type)) {
                const refl::TypeNode reached = refl::node_reached_indirectly(member_type);
                pending.push_back(Node{reached.type, reached.may_read_members, PayloadReach::Aliased});
            } else {
                pending.push_back(Node{refl::bare_type(member_type), true, member_reach});
            }
        }
    }
    return account;
}

// The refusal and its type, without the vectors, so it can sit in a
// static data member.
struct PayloadVerdict {
    PayloadRefusal refusal = PayloadRefusal::None;
    std::meta::info refused_type{};
};

[[nodiscard]] consteval PayloadVerdict payload_verdict(std::meta::info payload) {
    const PayloadAccount account = account_payload(payload);
    return PayloadVerdict{account.refusal, account.refused_type};
}

[[nodiscard]] consteval std::string_view payload_refusal_reason(PayloadRefusal why) noexcept {
    switch (why) {
        case PayloadRefusal::None:
            return "";
        case PayloadRefusal::TokenBehindPointer:
            return "it reaches a permission token through a pointer or a reference.  The token stays with the sender, "
                   "so the recipient would hold a second name for it.  Move the token by value";
        case PayloadRefusal::TokenInUnion:
            return "it holds a permission token in a union, a std::optional or a std::variant.  The token can be "
                   "absent at run time, and the set change cannot depend on a value";
        case PayloadRefusal::TokenInArray:
            return "it holds a permission token in an array.  That is several tokens of one tag, and a permission "
                   "set holds each tag once";
        case PayloadRefusal::DuplicateTag:
            return "it names one permission tag twice.  A permission set holds each tag once";
        case PayloadRefusal::BareBorrowOrShare:
            return "it holds a read proof, a share or a pool outside its marker.  A read proof travels only as "
                   "Borrowed<T, Tag>, and a share only as SharedReader<Tag>, so that a set records the loan";
        case PayloadRefusal::TypeErasure:
            return "it holds a type-erasure family.  The static type does not name what it holds, so a token "
                   "inside it would move with no set change";
        case PayloadRefusal::UnreadableState:
            return "it holds a class whose state the walk cannot read, such as a lambda with captures";
        case PayloadRefusal::IncompleteType:
            return "it holds a class that is only declared, so nothing says what it holds";
        default:
            break;
    }
    return "";
}

[[nodiscard]] consteval std::string_view payload_refusal_text(PayloadVerdict verdict) {
    if (verdict.refusal == PayloadRefusal::None) return {};
    std::string text{"fixy::session::diagnostic [Payload_Refused]: the payload cannot travel on a session channel, "
                     "because "};
    text += payload_refusal_reason(verdict.refusal);
    text += ".  The refused type: ";
    text += std::meta::display_string_of(verdict.refused_type);
    return std::define_static_string(text);
}

// ── From the account to permission sets ─────────────────────────────

enum class PayloadSet : std::uint8_t {
    SenderRequires,
    SenderGains,
    ReceiverRequires,
    ReceiverGains,
};

[[nodiscard]] consteval std::meta::info payload_wrap_each(std::meta::info wrapper,
                                                          const std::vector<std::meta::info>& tags) {
    std::vector<std::meta::info> wrapped;
    for (const std::meta::info tag : tags) wrapped.push_back(std::meta::substitute(wrapper, {tag}));
    return std::meta::substitute(^^::foundation::permissions::PermSet, wrapped);
}

// The four sets.  The sender loses what it must hold, and the recipient
// loses what it must hold, so two sets name each side's loss too.
//
//   sender requires   moved, lent, and BorrowedIn<t> for each released t
//   sender gains      LentOut<t> for each lent t
//   receiver requires LentOut<t> for each released t
//   receiver gains    moved, BorrowedIn<t> for each lent t, and released
[[nodiscard]] consteval std::meta::info payload_set(std::meta::info payload, PayloadSet which) {
    const PayloadAccount account = account_payload(payload);
    std::vector<std::meta::info> tags;
    switch (which) {
        case PayloadSet::SenderRequires:
            for (const std::meta::info tag : account.moved) tags.push_back(tag);
            for (const std::meta::info tag : account.lent) tags.push_back(tag);
            for (const std::meta::info tag : account.released) tags.push_back(std::meta::substitute(^^BorrowedIn, {tag}));
            break;
        case PayloadSet::SenderGains:
            return payload_wrap_each(^^LentOut, account.lent);
        case PayloadSet::ReceiverRequires:
            return payload_wrap_each(^^LentOut, account.released);
        case PayloadSet::ReceiverGains:
            for (const std::meta::info tag : account.moved) tags.push_back(tag);
            for (const std::meta::info tag : account.lent) tags.push_back(std::meta::substitute(^^BorrowedIn, {tag}));
            for (const std::meta::info tag : account.released) tags.push_back(tag);
            break;
        default:
            break;
    }
    return std::meta::substitute(^^::foundation::permissions::PermSet, tags);
}

[[nodiscard]] consteval bool payload_carries_share(std::meta::info payload) {
    return account_payload(payload).carries_share;
}

// The region a set element names: Tag for LentOut<Tag> and BorrowedIn<Tag>,
// and the element itself for any other tag.
[[nodiscard]] consteval std::meta::info region_of(std::meta::info element) {
    const std::meta::info bare = std::meta::dealias(element);
    if (payload_family_is(bare, ^^LentOut) || payload_family_is(bare, ^^BorrowedIn)) {
        return std::meta::dealias(std::meta::template_arguments_of(bare)[0]);
    }
    return bare;
}

// True when no element of `gains` names a region that an element of
// `kept` already names.  One region in two states at once is two claims
// on it, which a set must not hold.
[[nodiscard]] consteval bool regions_disjoint(std::meta::info kept, std::meta::info gains) {
    for (const std::meta::info gained : std::meta::template_arguments_of(std::meta::dealias(gains))) {
        for (const std::meta::info held : std::meta::template_arguments_of(std::meta::dealias(kept))) {
            if (region_of(gained) == region_of(held)) return false;
        }
    }
    return true;
}

}  // namespace detail

// ── The classification predicates ───────────────────────────────────

// True when the walk accepts the payload.  A refused payload answers
// false here, and the refusal text comes from payload_perm_delta.
template <class P>
struct is_permission_classified : std::bool_constant<detail::payload_verdict(^^P).refusal ==
                                                     detail::PayloadRefusal::None> {};

template <class P>
inline constexpr bool is_permission_classified_v = is_permission_classified<P>::value;

// ── The delta ───────────────────────────────────────────────────────
//
// The permission accounting of one payload.  The handle applies it:
// Send<P, K> takes sender_requires from the set, and leaves
//   (set minus sender_requires) plus sender_gains.
// Recv<P, K> takes receiver_requires from the set, and leaves
//   (set minus receiver_requires) plus receiver_gains.
template <class P>
struct payload_perm_delta {
    static constexpr detail::PayloadVerdict verdict = detail::payload_verdict(^^P);
    static_assert(verdict.refusal == detail::PayloadRefusal::None, detail::payload_refusal_text(verdict));

    using sender_requires = [:detail::payload_set(^^P, detail::PayloadSet::SenderRequires):];
    using sender_loses = sender_requires;
    using sender_gains = [:detail::payload_set(^^P, detail::PayloadSet::SenderGains):];
    using receiver_requires = [:detail::payload_set(^^P, detail::PayloadSet::ReceiverRequires):];
    using receiver_loses = receiver_requires;
    using receiver_gains = [:detail::payload_set(^^P, detail::PayloadSet::ReceiverGains):];

    // A share moves no tag, so it changes no set.  The flag is here for
    // a handle that wants to know a share went by.
    static constexpr bool carries_share = detail::payload_carries_share(^^P);
};

// True when the payload moves, lends or releases nothing and carries no
// share.  Such a payload leaves both sets as they were.
template <class P>
struct is_plain_payload
    : std::bool_constant<[] consteval {
          if (detail::payload_verdict(^^P).refusal != detail::PayloadRefusal::None) return false;
          return std::meta::template_arguments_of(detail::payload_set(^^P, detail::PayloadSet::SenderRequires)).empty()
              && std::meta::template_arguments_of(detail::payload_set(^^P, detail::PayloadSet::ReceiverGains)).empty()
              && !detail::payload_carries_share(^^P);
      }()> {};

template <class P>
inline constexpr bool is_plain_payload_v = is_plain_payload<P>::value;

// ── The set after one step ──────────────────────────────────────────

template <class PS, class P>
using perm_set_after_send_t = ::foundation::permissions::perm_set_union_t<
    ::foundation::permissions::perm_set_difference_t<PS, typename payload_perm_delta<P>::sender_loses>,
    typename payload_perm_delta<P>::sender_gains>;

template <class PS, class P>
using perm_set_after_recv_t = ::foundation::permissions::perm_set_union_t<
    ::foundation::permissions::perm_set_difference_t<PS, typename payload_perm_delta<P>::receiver_loses>,
    typename payload_perm_delta<P>::receiver_gains>;

// A sender can send a payload when the walk accepts it, the sender holds
// what the payload takes, and no gained loan state names a region the sender
// still holds in another state.
template <class P, class PS>
concept SendablePayload =
    is_permission_classified_v<P>
    && ::foundation::permissions::perm_set_subset_v<typename payload_perm_delta<P>::sender_requires, PS>
    && detail::regions_disjoint(
        ^^::foundation::permissions::perm_set_difference_t<PS, typename payload_perm_delta<P>::sender_loses>,
        ^^typename payload_perm_delta<P>::sender_gains);

// A recipient can receive a payload when the walk accepts it, the
// recipient holds what the payload closes, and nothing it gains names a region
// the recipient already holds in any state.  Two tokens of one region
// in one set are two owners of it.
template <class P, class PS>
concept ReceivablePayload =
    is_permission_classified_v<P>
    && ::foundation::permissions::perm_set_subset_v<typename payload_perm_delta<P>::receiver_requires, PS>
    && detail::regions_disjoint(
        ^^::foundation::permissions::perm_set_difference_t<PS, typename payload_perm_delta<P>::receiver_loses>,
        ^^typename payload_perm_delta<P>::receiver_gains);

// True when the set holds an open loan: a LentOut or a BorrowedIn.  A
// handle must not close while one is open.
template <class PS>
inline constexpr bool perm_set_has_open_loan_v = [] consteval {
    for (const std::meta::info element : std::meta::template_arguments_of(std::meta::dealias(^^PS))) {
        const std::meta::info bare = std::meta::dealias(element);
        if (detail::payload_family_is(bare, ^^LentOut) || detail::payload_family_is(bare, ^^BorrowedIn)) return true;
    }
    return false;
}();

}  // namespace fixy::session
