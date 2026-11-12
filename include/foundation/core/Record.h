#pragma once

// The Record family: a product of values, and a list of types.
//
// A product whose parts have meanings is a named struct.  Record<Ts...> is
// the product of generic code: the result of a split into two tokens, a
// value with the next step of a protocol, the pair of an index and an
// element.  Pack<Ts...> is a list of types with no value.
//
// A Record is an aggregate.  Record<A, B>{a, b} builds it, and a narrowing
// conversion does not compile.  The deduction guide gives Record{a, b} the
// part types of its arguments, and `return {std::move(value),
// std::move(next)};` builds the Record of a return.  A Record has no
// converting constructor, so a Record of other parts never becomes this one.
//
// A structured binding gives out the parts: `auto [first, second] =
// record;`, `auto& [first, second] = record;`, and `auto [first, second] =
// std::move(record);`, which moves each part one time.  A binding pack
// `auto [...parts] = record;` takes the place of std::apply.  GCC 16 permits
// a binding pack only in a template.  `template for (auto& part : record)`
// visits each part in order, but GCC 16 gives -Wshadow for each expansion of
// a destructurable expression, so -Werror refuses it.  The language finds
// the hook get<I> of a binding through argument-dependent lookup, so the
// hook is a hidden friend.
//
// A part is an object type that is not an array, not cv-qualified and not
// abstract, with a destructor that does not throw.  A part that moves, moves
// with no throw, so a move of a Record never stops after one part.  A
// reference part would hide a borrow, so a borrow is a Ref or a View.  A
// const part would block the move of a linear part.  A C array part is a
// FixedArray.  A part that cannot move, such as a Pinned part, is valid, and
// the Record then cannot move.  A move-only part makes the Record move-only,
// and a linear part makes the Record linear (foundation/Linear.h).
//
// The layout.  Record<Ts...> derives from one flat base, which derives from
// one cell for each part, so the instantiation depth is two for each count
// of parts.  A cell holds its part in one member.  An empty part is a
// [[no_unique_address]] member, so it takes no byte when the next part can
// share its address.  Each other part takes its full size, so no part
// shares a byte with a different part.  The parts are in declaration
// order.  A part with a default constructor gets its value initialization
// from the default member initializer of its cell, so `Record<int, int>
// pair;` holds two zeros.  A Record is trivially copyable, and a structural
// type, when each part is.  The offset of each part is the offset in a
// struct with the same members, where only an empty member has
// [[no_unique_address]].  The check file asserts this for twelve shapes.
//
// == and <=> are lexicographic, on two Records of one type only.  == exists
// when each part has == that gives bool, and no part is a floating value.
// <=> exists when each part has <=> that gives std::strong_ordering or
// std::partial_ordering.  Its category is strong when each part is strong,
// and partial in each other case.
//
// Pack<Ts...> is an empty class.  Its operations are variable templates and
// aliases over one class, so each has an instantiation depth of one:
//
//   pack_size_v<P>             the count of types
//   pack_at_t<I, P>            the type at index I, for I below the count
//   pack_contains_v<T, P>      true when T is one of the types
//   pack_index_of_v<T, P>      the index of T, when T is in P one time
//   pack_concat_t<Ps...>       one Pack of the types of each P, in order
//   pack_apply_t<Template, P>  Template<Ts...>
//   pack_unique_v<P>           true when no type is in P two times

#include <compare>
#include <concepts>
#include <cstddef>
#include <meta>
#include <type_traits>
#include <utility>

namespace foundation::core {

// A part of a Record.  An abstract class can be no member.
template <class T>
concept RecordElement = std::is_object_v<T> && !std::is_array_v<T> && !std::is_const_v<T> && !std::is_volatile_v<T>
                     && !std::is_abstract_v<T> && std::is_nothrow_destructible_v<T>
                     && (!std::is_move_constructible_v<T> || std::is_nothrow_move_constructible_v<T>);

// A list of types, for deduction only.  It has no member.
template <class... Ts>
struct Pack final {};

// A Pack<Ts...> for some Ts, with no cv-qualifier.  The gate reads the
// template of its argument by reflection, so no member and no trait of a
// unit can change its answer.
template <class P>
concept PackType = !std::is_const_v<P> && !std::is_volatile_v<P> && std::meta::has_template_arguments(^^P)
                && std::meta::template_of(^^P) == ^^Pack;

template <RecordElement... Ts>
struct Record;

namespace detail {

// A part whose == gives bool.  The == of a floating value is not an
// equality: a NaN differs from itself, and the tree refuses it with
// -Werror=float-equal.  So a floating part gives no ==, and a Record with
// one compares through <=>.
template <class T>
concept RecordEqualPart = !std::is_floating_point_v<T> && requires(T const& left, T const& right) {
    { left == right } -> std::same_as<bool>;
};

// A part whose <=> gives a strong order.
template <class T>
concept RecordStrongPart = requires(T const& left, T const& right) {
    { left <=> right } -> std::same_as<std::strong_ordering>;
};

// A part whose <=> gives a strong or a partial order.
template <class T>
concept RecordOrderedPart = RecordStrongPart<T> || requires(T const& left, T const& right) {
    { left <=> right } -> std::same_as<std::partial_ordering>;
};

// The category of the <=> of a Record of the parts Ts.  The declaration of
// each <=> names it also for a part with no <=>, so it never fails.
template <class... Ts>
using record_order_t = std::conditional_t<(RecordStrongPart<Ts> && ...), std::strong_ordering, std::partial_ordering>;

// The cell of the part at index I.  The index keeps two parts of one type
// in two distinct bases.  A part with a default constructor gets its value
// initialization from the default member initializer.  A part with no
// default constructor, such as a token, has no initializer, so the
// aggregate initialization of the Record must give it a value.
//
// Only an empty part is a [[no_unique_address]] member, so it takes no byte.
// A part with bytes is a plain member, which takes its full size.  A
// [[no_unique_address]] member with tail padding would let the ABI put the
// next part in that padding, and a copy of the bytes of the first part
// would then write the next part.
//
// The member part is public, because the aggregate initialization of the
// Record gives it its value, and a structural type has only public members.
// The family list names it.  A binding is the access to a part.
//
// A cell and the flat base have no operator.  A Record converts to each of
// its bases, so an operator of a base would compare one part of two Records.
template <std::size_t I, class T, bool = std::is_empty_v<T>, bool = std::is_default_constructible_v<T>>
struct RecordCell;

template <std::size_t I, class T>
struct RecordCell<I, T, true, true> {
    [[no_unique_address]] T part{};
};

template <std::size_t I, class T>
struct RecordCell<I, T, true, false> {
    [[no_unique_address]] T part;
};

template <std::size_t I, class T>
struct RecordCell<I, T, false, true> {
    T part{};
};

template <std::size_t I, class T>
struct RecordCell<I, T, false, false> {
    T part;
};

// The flat base of a Record: one cell for each part, in order.
template <class Indices, class... Ts>
struct RecordCells;

template <std::size_t... Is, class... Ts>
struct RecordCells<std::index_sequence<Is...>, Ts...> : RecordCell<Is, Ts>... {};

// The type at index I of Ts.  A class defers the index, because a Pack of
// no type cannot name a type of its pack in a declaration.
template <std::size_t I, class... Ts>
struct PackAt {
    using type = Ts...[I];
};

// The operations of one Pack.  The primary has no definition, so a type
// that is no Pack has no operation.  Each constraint of an operation is a
// constraint of a member, so a gate reads only its own instance.
template <class P>
struct PackParts;

template <class... Ts>
struct PackParts<Pack<Ts...>> {
    static constexpr std::size_t size = sizeof...(Ts);

    template <std::size_t I>
        requires(I < sizeof...(Ts))
    using at = typename PackAt<I, Ts...>::type;

    template <class T>
    static constexpr std::size_t count = (std::size_t{0} + ... + std::size_t{std::is_same_v<T, Ts>});

    // The index of T, when T is in the Pack one time.  Linear in the count
    // of types, at compile time only.
    template <class T>
        requires(count<T> == 1)
    [[nodiscard]] static consteval std::size_t index_of() noexcept {
        constexpr bool matches[] = {std::is_same_v<T, Ts>..., false};
        std::size_t index = 0;
        while (!matches[index]) {
            ++index;
        }
        return index;
    }

    static constexpr bool unique = ((count<Ts> == 1) && ...);

    template <template <class...> class Template>
    using apply = Template<Ts...>;
};

// The step of the fold of pack_concat_t.  Only this class has the operator,
// so no Pack gets an operator.
template <class P>
struct PackJoin;

template <class... Ts>
struct PackJoin<Pack<Ts...>> {
    using type = Pack<Ts...>;

    template <class... Us>
    friend consteval PackJoin<Pack<Ts..., Us...>> operator|(PackJoin, PackJoin<Pack<Us...>>) noexcept {
        return {};
    }
};

}  // namespace detail

template <RecordElement... Ts>
struct Record : detail::RecordCells<std::index_sequence_for<Ts...>, Ts...> {
    using types = Pack<Ts...>;

    [[nodiscard]] static consteval std::size_t size() noexcept { return sizeof...(Ts); }

    // The two compares fold over the parts in order.  The lexicographic
    // compare stops at the first part that is not equivalent.  The cells
    // have no operator, because a Record converts to each of its bases.
    // A Record of no part reads no parameter.
    [[nodiscard]] friend constexpr bool operator==([[maybe_unused]] Record const& left,
                                                   [[maybe_unused]] Record const& right) noexcept
        requires(detail::RecordEqualPart<Ts> && ...)
    {
        return [&]<std::size_t... Is>(std::index_sequence<Is...>) noexcept {
            return ((static_cast<detail::RecordCell<Is, Ts> const&>(left).part
                     == static_cast<detail::RecordCell<Is, Ts> const&>(right).part)
                    && ...);
        }(std::index_sequence_for<Ts...>{});
    }
    [[nodiscard]] friend constexpr detail::record_order_t<Ts...>
    operator<=>([[maybe_unused]] Record const& left, [[maybe_unused]] Record const& right) noexcept
        requires(detail::RecordOrderedPart<Ts> && ...)
    {
        detail::record_order_t<Ts...> result = std::strong_ordering::equal;
        [&]<std::size_t... Is>(std::index_sequence<Is...>) noexcept {
            static_cast<void>(((result = static_cast<detail::RecordCell<Is, Ts> const&>(left).part
                                     <=> static_cast<detail::RecordCell<Is, Ts> const&>(right).part,
                                std::is_eq(result))
                               && ...));
        }(std::index_sequence_for<Ts...>{});
        return result;
    }

    // The hook of a structured binding.  The language fixes the name get and
    // finds it through argument-dependent lookup.  The rvalue form gives an
    // rvalue reference to the part, so a binding of an rvalue Record moves
    // each part out one time.
    // The result types are deduced, because a Record of no part cannot name
    // a part type in a declaration.
    template <std::size_t I>
        requires(I < sizeof...(Ts))
    [[nodiscard]] friend constexpr auto& get(Record& record) noexcept {
        return static_cast<detail::RecordCell<I, Ts...[I]>&>(record).part;
    }
    template <std::size_t I>
        requires(I < sizeof...(Ts))
    [[nodiscard]] friend constexpr auto const& get(Record const& record) noexcept {
        return static_cast<detail::RecordCell<I, Ts...[I]> const&>(record).part;
    }
    template <std::size_t I>
        requires(I < sizeof...(Ts))
    [[nodiscard]] friend constexpr auto&& get(Record&& record) noexcept {
        return std::move(record).template take_part_<I>();
    }

private:
    // The part at index I of a Record that a binding gives up.  The hidden
    // variable of the binding keeps each part until a name of the binding
    // moves it out, so this function moves nothing.  The rule must-consume
    // reads the call of this rvalue member as the end of the parameter of
    // the rvalue hook: the binding gives up the Record.
    template <std::size_t I>
    [[nodiscard]] constexpr auto&& take_part_() && noexcept {
        return static_cast<Ts...[I]&&>(static_cast<detail::RecordCell<I, Ts...[I]>&>(*this).part);
    }
};

template <class... Ts>
Record(Ts...) -> Record<Ts...>;

template <PackType P>
inline constexpr std::size_t pack_size_v = detail::PackParts<P>::size;

template <std::size_t I, PackType P>
using pack_at_t = typename detail::PackParts<P>::template at<I>;

template <class T, PackType P>
inline constexpr bool pack_contains_v = detail::PackParts<P>::template count<T> != 0;

template <class T, PackType P>
inline constexpr std::size_t pack_index_of_v = detail::PackParts<P>::template index_of<T>();

template <PackType... Ps>
using pack_concat_t = typename decltype((detail::PackJoin<Pack<>>{} | ... | detail::PackJoin<Ps>{}))::type;

template <template <class...> class Template, PackType P>
using pack_apply_t = typename detail::PackParts<P>::template apply<Template>;

template <PackType P>
inline constexpr bool pack_unique_v = detail::PackParts<P>::unique;

}  // namespace foundation::core

// The tuple protocol of a structured binding.  The class forms only: the
// rule table admits std::tuple_size and std::tuple_element in this
// directory, and no other name of the protocol.
template <class... Ts>
struct std::tuple_size<::foundation::core::Record<Ts...>> {
    static constexpr std::size_t value = sizeof...(Ts);
};

template <std::size_t I, class... Ts>
    requires(I < sizeof...(Ts))
struct std::tuple_element<I, ::foundation::core::Record<Ts...>> {
    using type = Ts...[I];
};
