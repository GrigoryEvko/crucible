// The compile-time checks of foundation/core/TypeParts.h.

#include <foundation/core/TypeParts.h>

#include <foundation/Brand.h>
#include <foundation/core/Array.h>
#include <foundation/core/Choice.h>
#include <foundation/core/Frame.h>
#include <foundation/core/List.h>
#include <foundation/core/Ref.h>
#include <foundation/core/Region.h>

#include <cstddef>
#include <meta>
#include <utility>

namespace foundation::core::detail::type_parts_checks {

// A brand of one instance that names no scope.
struct CheckBrand {};

// True when the walks of the frame doors find nothing in an object of the
// class.
template <class Class>
[[nodiscard]] consteval bool is_plain() {
    return type_parts_v<Class>.is_plain && is_plain_class_(^^Class);
}

// A class of scalars, a wrapper of a plain class, a mutable scalar, a run of
// plain elements and a base with none of the parts below are plain, also at a
// depth.  A type argument that no raw storage holds changes nothing, also when
// it is an incomplete class.
struct Scalars {
    int count = 0;
    mutable long last = 0;
};
struct PlainBase {
    FixedArray<Scalars, 2> scalars{};
};
struct PlainWrapper : PlainBase {
    Scalars inner{};
    FixedArray<unsigned char, 7> padding{};
    long scalar = 0;
};
template <class Tag>
struct Counter {
    int value = 0;
};
struct UndeclaredTag;
static_assert(is_plain<Scalars>() && is_plain<PlainBase>() && is_plain<PlainWrapper>());
static_assert(is_plain<Counter<int>>() && is_plain<Counter<UndeclaredTag>>() && is_plain<FixedList<int, 3>>());
static_assert(is_plain<FixedArray<unsigned char, 7>>() && is_plain<FixedArray<Scalars, 3>>());

// A reference to a class or to a scalar, a borrow, an owner, raw storage that a
// type argument can fill, and a class that holds one of them by value are not
// plain.  A reference to a scalar is a part.
struct WithReference {
    Scalars& target;
};
struct WithScalarReference {
    int count = 0;
    int const& scalar;
};
struct WithByteReference {
    unsigned char& byte;
};
struct DerivedOfScalarReference : WithScalarReference {};
static_assert(!is_plain<WithScalarReference>() && !is_plain<WithByteReference>()
              && !is_plain<DerivedOfScalarReference>());
static_assert(type_parts_v<WithScalarReference>.size == 1);
static_assert(type_parts_v<WithScalarReference>.run()[0].type == ^^int);
static_assert(type_parts_v<WithScalarReference>.run()[0].has(PartFact::was_reference));
static_assert(type_parts_v<WithScalarReference>.run()[0].has(PartFact::was_const));
struct WithView {
    View<int const> view{};
};
struct WithViews {
    FixedArray<View<int const>, 2> views{};
};
struct DerivedOfView : WithView {};
struct WithMutableView {
    mutable View<int const> view{};
};
static_assert(!is_plain<WithReference>() && !is_plain<WithView>() && !is_plain<WithViews>());
static_assert(!is_plain<DerivedOfView>() && !is_plain<WithMutableView>() && !is_plain<HeapArray<int>>());
static_assert(!is_plain<Option<View<int>>>() && !is_plain<FixedList<Scalars, 3>>() && !is_plain<Box<int>>());

// A part by value that is no class: a pointer to a function and a pointer to
// member are plain, and a pointer to an object is not.  An array answers for
// its element.
static_assert(is_plain_held_(^^void (*)(int)) && is_plain_held_(^^int Scalars::*) && is_plain_held_(^^Scalars[2]));
static_assert(!is_plain_held_(^^int*) && !is_plain_held_(^^WithView[2]) && !is_plain_held_(^^int* [2]));

// The facts of the marks: a View and a Ref refer, a Box and a HeapArray own,
// and a class of the base with an owner mark owns.
static_assert(type_parts_v<View<int>>.is_borrow && !type_parts_v<View<int>>.is_storage_owner);
static_assert(type_parts_v<Ref<int, CheckBrand>>.is_borrow && !type_parts_v<Ref<int, CheckBrand>>.gives_const_access);
static_assert(type_parts_v<Box<int>>.is_storage_owner && !type_parts_v<Box<int>>.is_borrow);
static_assert(type_parts_v<HeapArray<int>>.is_storage_owner && !type_parts_v<Scalars>.is_storage_owner);
struct[[= owns_its_storage{}]] MarkedOfTheBase {
    int count = 0;
};
static_assert(type_parts_v<MarkedOfTheBase>.is_storage_owner && !is_plain<MarkedOfTheBase>());

// The parts stand in the order of the type arguments, the bases and the data
// members, with the facts of each one.  A leaf is no part.
template <class First, class Second>
struct Mixed : WithView, PlainBase {
    int count = 0;
    Scalars const& reference;
    mutable View<int> written{};
    Scalars const fixed{};
    Second last{};
};
[[nodiscard]] consteval bool has_the_parts_of_mixed() {
    TypeParts const& mixed = type_parts_v<Mixed<View<int> const, long>>;
    PartRun const run = mixed.run();
    if (mixed.size != 6 || mixed.argument_count != 1) return false;
    std::meta::info const view = std::meta::dealias(^^View<int>);
    std::meta::info const base = ^^WithView;
    std::meta::info const plain_base = ^^PlainBase;
    std::meta::info const scalars = ^^Scalars;
    bool const has_first = mixed.first_argument == view && mixed.is_first_argument_const;
    bool const has_argument = run[0].type == view && run[0].has(PartFact::was_const);
    bool const has_bases = run[1].type == base && run[1].kept != std::meta::info{} && run[2].type == plain_base
                        && run[2].has(PartFact::is_plain);
    bool const has_reference =
        run[3].type == scalars && run[3].has(PartFact::was_reference) && run[3].has(PartFact::was_const);
    bool const has_written = run[4].has(PartFact::is_mutable) && !run[4].has(PartFact::is_plain);
    bool const has_fixed = run[5].type == scalars && run[5].has(PartFact::is_plain) && run[5].has(PartFact::was_const);
    return has_first && has_argument && has_bases && has_reference && has_written && has_fixed;
}
static_assert(has_the_parts_of_mixed());

// The first type argument stands in the facts also when it is no part, and a
// borrow and an owner give their type arguments and no member.
static_assert(type_parts_v<Counter<int>>.size == 0);
static_assert(type_parts_v<Counter<int>>.first_argument == ^^int);
static_assert(type_parts_v<View<Scalars>>.size == type_parts_v<View<Scalars>>.argument_count);
static_assert(type_parts_v<HeapArray<Scalars>>.size == type_parts_v<HeapArray<Scalars>>.argument_count);

// The kept parts of a part are the parts of its class.
static_assert(std::meta::extract<TypeParts const&>(type_parts_v<DerivedOfView>.run()[0].kept).size == 1);
static_assert(kept_parts_of(^^Scalars) == ^^type_parts_v<Scalars>);

// A class with more parts than the near rows gives each one.
struct Many {
    View<int> a0{};
    View<int> a1{};
    View<int> a2{};
    View<int> a3{};
    View<int> a4{};
    View<int> a5{};
    View<int> a6{};
    View<int> a7{};
    View<int> a8{};
    View<int> a9{};
    View<int> a10{};
    View<int> a11{};
    View<long> a12{};
    View<char> a13{};
};
static_assert(type_parts_v<Many>.size == 14 && type_parts_v<Many>.wide != std::meta::info{});
static_assert(type_parts_v<Many>.run()[11].type == std::meta::dealias(^^View<int>));
static_assert(type_parts_v<Many>.run()[12].type == std::meta::dealias(^^View<long>));
static_assert(type_parts_v<Many>.run()[13].type == std::meta::dealias(^^View<char>));

// An integer sequence is a leaf, and the class of a run of an array names one.
static_assert(is_leaf_(^^std::integer_sequence<std::size_t, 0, 1, 2>) && is_leaf_(^^int) && is_leaf_(^^int[4]));
static_assert(!is_leaf_(^^Scalars) && !is_leaf_(^^int*) && !is_leaf_(^^Scalars[4]) && !is_leaf_(^^void()));

// A class that reaches itself through a borrow or an argument ends its read,
// because the read follows only what the class holds by value.
struct ReachesItself {
    View<ReachesItself> next{};
    Option<Ref<ReachesItself, CheckBrand>> other{none};
};
static_assert(type_parts_v<ReachesItself>.size == 2 && !is_plain<ReachesItself>());

// A scope brand is the closure type of CRUCIBLE_SCOPE_BRAND or a
// specialization with such an argument at each depth.  A fresh brand, a named
// class, a closure with a generic call operator and a specialization whose own
// call operator holds the mark are none.
template <class Brand>
struct RowBrand {};
template <class Brand>
struct MarkedCall {
    [[= ::foundation::brand::scope_brand{}]] void operator()() const noexcept {}
};
template <class Brand>
[[nodiscard]] consteval bool is_brand() {
    return is_scope_brand_(^^Brand);
}
[[nodiscard]] consteval bool reads_scope_brands() {
    using Scope = CRUCIBLE_SCOPE_BRAND;
    using Fresh = CRUCIBLE_FRESH_BRAND;
    auto generic = [](auto value) noexcept { return value; };
    return is_brand<Scope>() && is_brand<RowBrand<Scope>>() && is_brand<RowBrand<RowBrand<Scope>>>()
        && !is_brand<Fresh>() && !is_brand<RowBrand<Fresh>>() && !is_brand<decltype(generic)>() && !is_brand<Scalars>()
        && !is_brand<int>() && !is_brand<MarkedCall<int>>() && type_parts_v<ViewMut<int, Scope>>.is_frame_borrow
        && !type_parts_v<ViewMut<int, Fresh>>.is_frame_borrow;
}
static_assert(reads_scope_brands());

}  // namespace foundation::core::detail::type_parts_checks

// A class outside the base that states an owner mark or refers_deep{} gives no
// fact of an owner and of const access.
namespace type_parts_outside_checks {

namespace core = ::foundation::core;
namespace transfer = ::foundation::transfer;

struct[[= core::owns_its_storage{}]] OwnerOutside {
    int count = 0;
};
template <class Target>
struct[[= transfer::refers_deep{}]] DeepOutside {
    core::View<Target> targets{};
};
static_assert(!core::detail::type_parts_v<OwnerOutside>.is_storage_owner);
static_assert(core::detail::type_parts_v<DeepOutside<int>>.is_borrow);
static_assert(!core::detail::type_parts_v<DeepOutside<int>>.gives_const_access);

}  // namespace type_parts_outside_checks
