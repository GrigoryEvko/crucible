#pragma once

// The keys that a root door mints for its body (THE BORROW OF A ROOT DOOR of
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
//                       the body for each brand, and it calls the body with the
//                       leading arguments and one fresh key of each brand, as
//                       lvalues that live for the call
//
// THE CHECKS
//   The brands are distinct key brands (IsKeyBrand), so no body gets two keys
//   of one brand, and no body gets a key of Unkeyed.  For each brand, the body,
//   each leading argument and the result of the body hold BorrowsNoKeyOwner of
//   foundation/core/Frame.h.  The checks are in the entry that mints, so no
//   door can mint with no check.  A door that starts more than one thread for
//   one body object, such as a parallel loop, does not call the entry: its body
//   gets the arguments of the door only, so a body that names a key does not
//   compile there.

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

// True when the brands are distinct key brands (THE CHECKS).  Complexity:
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

// True when each part holds BorrowsNoKeyOwner for the brand, and for each
// brand (THE CHECKS).
template <class Brand, class... Parts>
consteval bool parts_borrow_no_key_owner_() {
    return (BorrowsNoKeyOwner<Parts, Brand> && ...);
}
template <class... Parts, class... Brands>
consteval bool borrow_no_key_owner_(KeyBrands<Brands...> /*brands*/) {
    return (parts_borrow_no_key_owner_<Brands, Parts...>() && ...);
}

// Calls call with one fresh key of each brand, in the order of the brands.
// Each key is a local of one level of the recursion, so it lives for the call.
template <class Call, class... Keys>
constexpr decltype(auto) call_with_root_keys_(KeyBrands<> /*brands*/, Call&& call, Keys&... keys) noexcept {
    return static_cast<Call&&>(call)(keys...);
}
template <class First, class... Rest, class Call, class... Keys>
constexpr decltype(auto) call_with_root_keys_(KeyBrands<First, Rest...> /*brands*/, Call&& call,
                                              Keys&... keys) noexcept {
    LoanKey<First> key{FrameDoor::key()};
    return call_with_root_keys_(KeyBrands<Rest...>{}, static_cast<Call&&>(call), keys..., key);
}

template <class Body, class Brands, class... Args>
struct root_body_result_;
template <class Body, class... Brands, class... Args>
struct root_body_result_<Body, KeyBrands<Brands...>, Args...> {
    using type = decltype(std::declval<Body&>()(std::declval<Args>()..., std::declval<LoanKey<Brands>&>()...));
};

// The one entry of the root doors (THE CHECKS): checks the body, and calls it
// with the leading arguments and one fresh key of each brand that it names.
template <std::size_t Leading, class Body, class... Args>
    requires(sizeof...(Args) == Leading)
constexpr decltype(auto) run_root_body_(Body& body, Args&&... leading) noexcept {
    using Brands = root_key_brands_t<Body, Leading>;
    using Result = typename root_body_result_<Body, Brands, Args&&...>::type;
    static_assert(are_distinct_key_brands_of_(Brands{}),
                  "a body of a root door names two keys of one brand, or a key of a brand that is no key brand: name "
                  "each key brand one time");
    static_assert(borrow_no_key_owner_<Body, std::remove_cvref_t<Args>...>(Brands{}),
                  "a body of a root door reaches a key of a brand of its keys, or an owner of such a brand through a "
                  "borrow: move each owner into the body by value, and keep each key with its holder");
    if constexpr (!std::is_void_v<Result>) {
        static_assert(borrow_no_key_owner_<Result>(Brands{}),
                      "the result of a body of a root door reaches a key of a brand of its keys, or an owner of such a "
                      "brand through a borrow");
    }
    return call_with_root_keys_(Brands{}, [&body, &leading...](auto&... keys) noexcept -> decltype(auto) {
        return body(static_cast<Args&&>(leading)..., keys...);
    });
}

}  // namespace detail

}  // namespace foundation::core
