#pragma once

// The keys that a root door mints for its body (the borrow of a root door of
// foundation/core/Frame.h).
//
//   KeyBrands<B...>     the brands of the keys of one body, in the order of
//                       its parameters
//   root_key_brands_t<Body, Leading>
//                       the brands of the trailing parameters LoanKey<B>& of
//                       the call operator of Body, or of a function pointer,
//                       after Leading other parameters.  A body whose call
//                       operator is a template, such as a generic lambda,
//                       names no key: KeyBrands<>
//   NamesRootKeys<Body, Leading>
//                       Body names one key or more after Leading parameters
//   detail::run_root_body_<Leading>(body, leading...)
//                       the one entry of the root doors of the base: it checks
//                       the body for each brand, takes the body by value, and
//                       calls it one time through the once door call_once of
//                       foundation/Linear.h with the leading arguments and one
//                       fresh key of each brand.  The keys are lvalues of one
//                       local cell, which lives for the call
//   detail::run_dispatch_body_<Leading>(body, leading...)
//                       the entry of the dispatch door: the same call, for
//                       keys of dispatch brands only, with no walk
//
// Checks
//   The brands are distinct key brands (IsKeyBrand), so no body gets two keys
//   of one brand, and no body gets a key of Unkeyed.  run_root_body_ mints no
//   key of a dispatch brand, and run_dispatch_body_ mints only such keys (the
//   dispatch brand of foundation/core/Frame.h).  For each brand, the body,
//   each leading argument and the result of the body hold BorrowsNoKeyOwner of
//   foundation/core/Frame.h.  The checks are in the entry that mints, so no
//   door can mint with no check.  A door that starts more than one thread for
//   one body object, such as a parallel loop, does not call the entry: its body
//   gets the arguments of the door only, so a body that names a key does not
//   compile there.

#include <foundation/NoObject.h>
#include <foundation/Platform.h>
#include <foundation/core/Frame.h>

#include <cstddef>
#include <meta>
#include <type_traits>
#include <utility>

namespace foundation::core {

template <class... Brands>
struct KeyBrands {
    static constexpr std::size_t count = sizeof...(Brands);
};

namespace detail {

template <class... Parameters>
struct ParameterList {};

template <class Parameter>
struct key_brand_of_ {
    static constexpr bool is_key = false;
    using brand = void;
};
template <class Brand>
struct key_brand_of_<LoanKey<Brand>&> {
    static constexpr bool is_key = true;
    using brand = Brand;
};

// The parameters of a call operator that is no template, or of a function
// pointer.  Each body of a root door is noexcept.
template <class Signature>
struct call_parameters_ {
    static constexpr bool is_known = false;
    using list = ParameterList<>;
};
template <class R, class C, class... Ps>
struct call_parameters_<R (C::*)(Ps...) noexcept> {
    static constexpr bool is_known = true;
    using list = ParameterList<Ps...>;
};
template <class R, class C, class... Ps>
struct call_parameters_<R (C::*)(Ps...) const noexcept> : call_parameters_<R (C::*)(Ps...) noexcept> {};
template <class R, class... Ps>
struct call_parameters_<R (*)(Ps...) noexcept> {
    static constexpr bool is_known = true;
    using list = ParameterList<Ps...>;
};
template <class R, class... Ps>
struct call_parameters_<R (*)(Ps...)> : call_parameters_<R (*)(Ps...) noexcept> {};

template <class Body>
struct body_signature_ {
    using type = void;
};
template <class Body>
    requires std::is_pointer_v<Body> && std::is_function_v<std::remove_pointer_t<Body>>
struct body_signature_<Body> {
    using type = Body;
};
template <class Body>
    requires requires { &Body::operator(); }
struct body_signature_<Body> {
    using type = decltype(&Body::operator());
};

// The brands of the trailing parameters after Leading parameters, and whether
// each trailing parameter is a key.
template <std::size_t Leading, class List>
struct trailing_key_brands_ {
    static constexpr bool is_all_keys = false;
    using type = KeyBrands<>;
};
template <class... Ps>
struct trailing_key_brands_<0, ParameterList<Ps...>> {
    static constexpr bool is_all_keys = (key_brand_of_<Ps>::is_key && ...);
    using type = std::conditional_t<is_all_keys, KeyBrands<typename key_brand_of_<Ps>::brand...>, KeyBrands<>>;
};
template <std::size_t Leading, class First, class... Ps>
    requires(Leading > 0)
struct trailing_key_brands_<Leading, ParameterList<First, Ps...>>
    : trailing_key_brands_<Leading - 1, ParameterList<Ps...>> {};

template <class Body, std::size_t Leading>
using body_trailing_ =
    trailing_key_brands_<Leading, typename call_parameters_<typename body_signature_<Body>::type>::list>;

}  // namespace detail

template <class Body, std::size_t Leading>
using root_key_brands_t = typename detail::body_trailing_<std::remove_cvref_t<Body>, Leading>::type;

template <class Body, std::size_t Leading>
concept NamesRootKeys = root_key_brands_t<Body, Leading>::count > 0;

namespace detail {

// True when the brands are distinct key brands (see the checks).  Complexity:
// quadratic in the count of the brands, at compile time only.
template <class... Brands>
consteval bool are_distinct_key_brands_() {
    if constexpr (sizeof...(Brands) == 0) {
        return true;
    } else {
        std::meta::info const listed[]{std::meta::dealias(^^Brands)...};
        for (std::size_t first = 0; first < sizeof...(Brands); ++first) {
            for (std::size_t second = first + 1; second < sizeof...(Brands); ++second) {
                if (listed[first] == listed[second]) {
                    return false;
                }
            }
        }
        return (IsKeyBrand<Brands> && ...);
    }
}

template <class... Brands>
consteval bool are_distinct_key_brands_of_(KeyBrands<Brands...> /*brands*/) {
    return are_distinct_key_brands_<Brands...>();
}

// True when each brand is a dispatch brand, and true when none is (the
// dispatch brand of foundation/core/Frame.h).
template <class... Brands>
consteval bool are_dispatch_brands_(KeyBrands<Brands...> /*brands*/) {
    return (is_dispatch_brand_(^^Brands) && ...);
}
template <class... Brands>
consteval bool names_no_dispatch_brand_(KeyBrands<Brands...> /*brands*/) {
    return (!is_dispatch_brand_(^^Brands) && ...);
}

// True when each part holds BorrowsNoKeyOwner for the brand, and for each
// brand (see the checks).
template <class Brand, class... Parts>
consteval bool parts_borrow_no_key_owner_() {
    return (BorrowsNoKeyOwner<Parts, Brand> && ...);
}
template <class... Parts, class... Brands>
consteval bool borrow_no_key_owner_(KeyBrands<Brands...> /*brands*/) {
    return (parts_borrow_no_key_owner_<Brands, Parts...>() && ...);
}

// One key of the key cell of a root door.
template <class Brand>
struct root_key_slot_ {
    LoanKey<Brand> key;
};

// The keys of one call of a root door: one key of each brand that the body
// names, each in a base of its own.  Two keys of one brand would be two bases
// of one type, so the cell takes only distinct key brands.
template <class... Brands>
    requires(are_distinct_key_brands_<Brands...>())
struct root_key_cell_ final : root_key_slot_<Brands>... {};

// The two entries of the root doors (the definitions below).
// RootKeysFrameKeyDoor names them as friends, so these declarations stand
// before the door.
template <std::size_t Leading, class Body, class... Args>
    requires(sizeof...(Args) == Leading)
constexpr decltype(auto) run_root_body_(Body body, Args&&... leading) noexcept;
template <std::size_t Leading, class Body, class... Args>
    requires(sizeof...(Args) == Leading)
constexpr decltype(auto) run_dispatch_body_(Body body, Args&&... leading) noexcept;

}  // namespace detail

// The door of the entries of the root doors to the key of the frame doors
// (the doors of the owners above foundation/core/Frame.h).  Its one member is
// private, and run_root_body_ and run_dispatch_body_ are its friends.  The
// invariant: an entry checks the brands that the body names and the walk of
// each brand first, and then mints one key of each brand for one call of the
// body.  Each entry passes the key to call_with_root_keys_, so no code mints a
// key with no check.  A home owner row of utils/rule-table.txt keeps this
// definition in this header.
class RootKeysFrameKeyDoor final : ::foundation::NoObject<RootKeysFrameKeyDoor> {
    template <std::size_t Leading, class Body, class... Args>
        requires(sizeof...(Args) == Leading)
    friend constexpr auto ::foundation::core::detail::run_root_body_(Body body, Args&&... leading) noexcept
        -> decltype(auto);
    template <std::size_t Leading, class Body, class... Args>
        requires(sizeof...(Args) == Leading)
    friend constexpr auto ::foundation::core::detail::run_dispatch_body_(Body body, Args&&... leading) noexcept
        -> decltype(auto);

    // The key of the frame doors, for the keys of one call of a body.
    [[nodiscard]] CRUCIBLE_INLINE static constexpr detail::FrameKey key_() noexcept { return detail::FrameDoor::key(); }
};

namespace detail {

// Calls the body with the leading arguments and one fresh key of each brand,
// in the order of the brands.  The keys are a local of this frame, so they live
// for the call.  A closure goes through the once door call_once of
// foundation/Linear.h, which owns it, calls it one time and ends it.  A
// function pointer holds no capture, so the entry calls it with no door.  The
// entry gives the key of the frame doors, so only an entry mints the keys.
// Each key initializes the one member of its slot through brace elision, so
// each key starts in place: no key moves, and a key of a dispatch brand has
// no move.
template <class... Brands, class Body, class... Args>
constexpr decltype(auto) call_with_root_keys_(FrameKey const& key, KeyBrands<Brands...> /*brands*/, Body&& body,
                                              Args&&... leading) noexcept {
    root_key_cell_<Brands...> cell{LoanKey<Brands>{key}...};
    if constexpr (std::is_class_v<std::remove_cvref_t<Body>>) {
        return ::foundation::linear::call_once(static_cast<Body&&>(body), static_cast<Args&&>(leading)...,
                                               static_cast<root_key_slot_<Brands>&>(cell).key...);
    } else {
        return body(static_cast<Args&&>(leading)..., static_cast<root_key_slot_<Brands>&>(cell).key...);
    }
}

template <class Body, class Brands, class... Args>
struct root_body_result_;
template <class Body, class... Brands, class... Args>
struct root_body_result_<Body, KeyBrands<Brands...>, Args...> {
    using type = decltype(std::declval<Body>()(std::declval<Args>()..., std::declval<LoanKey<Brands>&>()...));
};

// The one entry of the root doors (see the checks): checks the body, and calls it
// with the leading arguments and one fresh key of each brand that it names.
// The body moves into the frame of the entry, and it runs one time.
template <std::size_t Leading, class Body, class... Args>
    requires(sizeof...(Args) == Leading)
constexpr decltype(auto) run_root_body_(Body body, Args&&... leading) noexcept {
    using Brands = root_key_brands_t<Body, Leading>;
    using Result = typename root_body_result_<Body, Brands, Args&&...>::type;
    // A function pointer holds no state, and its type names its own key
    // parameters, so the walk reads only its arguments.
    using State = std::conditional_t<std::is_pointer_v<Body>, KeyBrands<>, Body>;
    static_assert(are_distinct_key_brands_of_(Brands{}),
                  "a body of a root door names two keys of one brand, or a key of a brand that is no key brand: name "
                  "each key brand one time");
    static_assert(names_no_dispatch_brand_(Brands{}),
                  "a body of a door that starts a thread names a key of a dispatch brand: only the dispatch door of "
                  "fixy/concurrent/Dispatch.h mints such a key, one window at a time on each thread");
    static_assert(borrow_no_key_owner_<State, std::remove_cvref_t<Args>...>(Brands{}),
                  "a body of a root door reaches a key of a brand of its keys, or an owner of such a brand through a "
                  "borrow: move each owner into the body by value, and keep each key with its holder");
    if constexpr (!std::is_void_v<Result>) {
        static_assert(borrow_no_key_owner_<Result>(Brands{}),
                      "the result of a body of a root door reaches a key of a brand of its keys, or an owner of such a "
                      "brand through a borrow");
    }
    return call_with_root_keys_(RootKeysFrameKeyDoor::key_(), Brands{}, static_cast<Body&&>(body),
                                static_cast<Args&&>(leading)...);
}

// The entry of the dispatch door of fixy/concurrent/Dispatch.h (the dispatch
// brand of foundation/core/Frame.h): it checks that the brands are distinct
// dispatch brands and that the body gives no reference, and it calls the body
// with the leading arguments and one fresh key of each brand.  It has no borrow
// walk and no export walk.  The body reaches its owners through outside data,
// and the window of the door holds one key of a brand on a thread.  A key of a
// dispatch brand does not move, and no Ref, Box or View holds one, so the body
// keeps no key past its call.  The key cell builds each key in place, so such
// a key needs no move there either.
template <std::size_t Leading, class Body, class... Args>
    requires(sizeof...(Args) == Leading)
constexpr decltype(auto) run_dispatch_body_(Body body, Args&&... leading) noexcept {
    using Brands = root_key_brands_t<Body, Leading>;
    using Result = typename root_body_result_<Body, Brands, Args&&...>::type;
    static_assert(are_distinct_key_brands_of_(Brands{}),
                  "a body of the dispatch door names two keys of one brand, or a key of a brand that is no key brand: "
                  "name each key brand one time");
    static_assert(are_dispatch_brands_(Brands{}),
                  "a body of the dispatch door names a key of a brand that states no dispatch_brand{}: a door that "
                  "starts a thread can mint such a key, so the window of the dispatch would not hold it alone");
    static_assert(!std::is_reference_v<Result>,
                  "a body of the dispatch door gives a reference, which outlives its keys");
    return call_with_root_keys_(RootKeysFrameKeyDoor::key_(), Brands{}, static_cast<Body&&>(body),
                                static_cast<Args&&>(leading)...);
}

}  // namespace detail

}  // namespace foundation::core
