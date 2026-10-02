// The compile-time checks of foundation/reflect/Hash.h.

#include <foundation/reflect/Hash.h>

namespace foundation::reflect {

namespace detail::stable_name_self_test {

static_assert(!stable_name_of<int>.empty());
static_assert(!stable_name_of<float>.empty());
static_assert(!stable_name_of<void>.empty());
static_assert(stable_name_of<int>.ends_with("int"));
static_assert(stable_name_of<float>.ends_with("float"));

static_assert(stable_type_id<int> != stable_type_id<float>);
static_assert(stable_type_id<int> != stable_type_id<double>);
static_assert(stable_type_id<float> != stable_type_id<double>);
static_assert(stable_type_id<int> != stable_type_id<unsigned int>);
static_assert(stable_type_id<int> != stable_type_id<long>);
static_assert(stable_type_id<void> != stable_type_id<int>);
static_assert(stable_type_id<char> != stable_type_id<unsigned char>);
static_assert(stable_type_id<short> != stable_type_id<int>);

static_assert(stable_type_id<int> != 0);
static_assert(stable_type_id<float> != 0);
static_assert(stable_type_id<void> != 0);

static_assert(stable_type_id<int> == stable_type_id<int>);

// These literals pin the ids the current toolchain produces. A shift in
// how a name is printed moves every downstream hash silently, so one of
// these fails first and names the type that moved. Treat a failure as a
// decision, not a bug: confirm the shift breaks nobody who reads a shared
// cache, then refresh the pins deliberately. A compiler major-version
// roll is expected to fail them.

static_assert(stable_type_id<int> == 0x038bf5d93760ba14ULL);
static_assert(stable_type_id<unsigned int> == 0x3e40352bf14d5e8cULL);
static_assert(stable_type_id<float> == 0xaac94173610ce8ebULL);
static_assert(stable_type_id<double> == 0x5a427827acb3b7f4ULL);
static_assert(stable_type_id<void> == 0x7095b61429cf52a0ULL);
static_assert(stable_type_id<char> == 0x24810aa534fd4e53ULL);
static_assert(stable_type_id<unsigned char> == 0xeb532a1cd85a3221ULL);
static_assert(stable_type_id<signed char> == 0xe668b88a72723d2eULL);
static_assert(stable_type_id<short> == 0x76a26fe7af41346dULL);
static_assert(stable_type_id<long> == 0xb398537731c4a05dULL);
static_assert(stable_type_id<long long> == 0x8e73a318de406be0ULL);
static_assert(stable_type_id<unsigned long long> == 0xcb9dc82adf69491aULL);
static_assert(stable_type_id<bool> == 0xc7dfd75159543180ULL);

static_assert(std::is_same_v<canonicalize_pack_t<>, std::tuple<>>);

static_assert(std::is_same_v<canonicalize_pack_t<int>, std::tuple<int>>);

// Which name sorts first depends on how reflection prints it. The
// canonical order is the same for every permutation of one pack, which
// is the only property callers rely on.
static_assert(std::is_same_v<canonicalize_pack_t<int, float>, canonicalize_pack_t<float, int>>);

static_assert(std::is_same_v<canonicalize_pack_t<int, float, double>, canonicalize_pack_t<float, double, int>>);

static_assert(std::is_same_v<canonicalize_pack_t<int, float, double>, canonicalize_pack_t<double, int, float>>);

static_assert(std::is_same_v<canonicalize_pack_t<char, short, int, long>, canonicalize_pack_t<long, int, short, char>>);

static_assert(std::is_same_v<canonicalize_pack_t<int, int>, std::tuple<int, int>>);

namespace fn_test {
inline void f0() noexcept {}
inline void f1(int) noexcept {}
inline void f2(float) noexcept {}
inline int f3(int) noexcept { return 0; }
inline void f4(int, int) noexcept {}
inline void f5(int, float) noexcept {}
inline void f1_twin(int) noexcept {}
}  // namespace fn_test

// Two functions of one signature share the type id and differ by name.
static_assert(stable_function_id<&fn_test::f1> == stable_function_id<&fn_test::f1_twin>);
static_assert(stable_function_name_id<&fn_test::f1> != stable_function_name_id<&fn_test::f1_twin>);
static_assert(stable_function_name_id<&fn_test::f1> == stable_function_name_id<&fn_test::f1>);
static_assert(stable_function_name_id<&fn_test::f0> != 0);

static_assert(stable_function_id<&fn_test::f0> != stable_function_id<&fn_test::f1>);
static_assert(stable_function_id<&fn_test::f1> != stable_function_id<&fn_test::f2>);
static_assert(stable_function_id<&fn_test::f1> != stable_function_id<&fn_test::f3>);
static_assert(stable_function_id<&fn_test::f1> != stable_function_id<&fn_test::f4>);
static_assert(stable_function_id<&fn_test::f4> != stable_function_id<&fn_test::f5>);

static_assert(stable_function_id<&fn_test::f0> != 0);

// The empty string consumes no bytes, so the digest is the offset basis.
static_assert(detail::fnv1a_64("") == detail::FNV1A_OFFSET_BASIS);

constexpr std::uint64_t expected_fnv_a = (detail::FNV1A_OFFSET_BASIS ^ 0x61ULL) * detail::FNV1A_PRIME;
static_assert(detail::fnv1a_64("a") == expected_fnv_a);

constexpr std::uint64_t expected_fnv_ab = []() consteval {
    std::uint64_t h = detail::FNV1A_OFFSET_BASIS;
    h = (h ^ 0x61ULL) * detail::FNV1A_PRIME;
    h = (h ^ 0x62ULL) * detail::FNV1A_PRIME;
    return h;
}();
static_assert(detail::fnv1a_64("ab") == expected_fnv_ab);

// Swapping the two stages produces different digests, so the order of
// composition is pinned here.
static_assert(detail::hash_name("test") == fmix64(detail::fnv1a_64("test")));

static_assert(combine_ids(1, 2) != combine_ids(2, 1));

// Each shape the identity walk opens, one positive and one negative.
namespace identity_test {
struct Named {};
template <typename T>
struct Box {};
template <auto V>
struct Holds {};
template <template <typename> typename Tmpl>
struct HoldsTemplate {};
enum class Colour : std::uint8_t {
    red
};
inline constexpr auto closure = [](auto value) { return value; };
inline constexpr auto plain_closure = [](int value) { return value; };
inline constexpr int (*closure_invoker)(int) = +[](int value) { return value; };
inline int named_function(int value) { return value; }
inline auto local_of_named() {
    struct Local {};
    return Local{};
}
// Two classes of one name in two blocks of one function print one name.
inline auto shadowed_local() {
    {
        struct Local {};
    }
    struct Local {};
    return Local{};
}
struct NestsInLocal {
    template <class T>
    static auto nested() {
        struct Local {
            struct Inner {};
        };
        return typename Local::Inner{};
    }
};
template <typename T>
inline int templated_function(int value) {
    return value;
}
static int internal_variable = 0;
inline int external_variable = 0;
template <int& Reference>
struct HoldsReference {};
struct Address {
    int const* pointer;
};
struct Bound {
    int limit;
};
union Either {
    int first;
    int second;
};
struct HoldsEither {
    Either either;
};
inline constexpr Bound named_bound{4};
template <Bound const& Reference>
struct HoldsBoundReference {};
inline constexpr auto local_of_closure = [] {
    struct Local {};
    return Local{};
};
inline auto unnamed_class_of_named() {
    struct {
        int field;
    } value{};
    return value;
}
enum {
    unnamed_enumerator
};
namespace {
struct Internal {};
template <typename T>
struct InternalBox {};
inline int internal_function(int value) { return value; }
}  // namespace
}  // namespace identity_test

static_assert(HasStableIdentity<int>);
static_assert(HasStableIdentity<identity_test::Named>);
static_assert(HasStableIdentity<identity_test::Named const volatile* const&>);
static_assert(HasStableIdentity<identity_test::Named[3][4]>);
static_assert(HasStableIdentity<identity_test::Box<identity_test::Box<int>>>);
static_assert(HasStableIdentity<identity_test::Colour>);
static_assert(HasStableIdentity<identity_test::Named (*)(identity_test::Colour, int&)>);
static_assert(HasStableIdentity<int identity_test::Named::*>);
static_assert(HasStableIdentity<identity_test::Holds<&identity_test::named_function>>);
static_assert(HasStableIdentity<identity_test::Holds<identity_test::Colour::red>>);
static_assert(HasStableIdentity<identity_test::HoldsTemplate<identity_test::Box>>);
static_assert(HasStableIdentity<decltype(^^int)>);

static_assert(!HasStableIdentity<decltype(identity_test::closure)>);
static_assert(!HasStableIdentity<decltype(identity_test::plain_closure)>);
static_assert(!HasStableIdentity<identity_test::Box<decltype(identity_test::closure)>>);
static_assert(!HasStableIdentity<identity_test::Holds<identity_test::closure>>);
static_assert(!HasStableIdentity<identity_test::Holds<identity_test::closure_invoker>>);
static_assert(!HasStableIdentity<decltype(identity_test::unnamed_class_of_named())>);
static_assert(!HasStableIdentity<decltype(identity_test::unnamed_enumerator)>);
static_assert(!HasStableIdentity<decltype(identity_test::local_of_closure())>);
static_assert(!HasStableIdentity<identity_test::Internal>);
static_assert(!HasStableIdentity<identity_test::Box<identity_test::Internal>>);
static_assert(!HasStableIdentity<identity_test::HoldsTemplate<identity_test::InternalBox>>);
static_assert(!HasStableIdentity<identity_test::Holds<&identity_test::internal_function>>);
static_assert(!HasStableIdentity<int (*)(decltype(identity_test::plain_closure))>);
static_assert(!HasStableIdentity<int decltype(identity_test::plain_closure)::*>);

// An object named by address or by reference, and a class value that
// holds an address, cannot be read back to a variable, whatever its
// linkage.  A reflection is read to the entity it names.
static_assert(!HasStableIdentity<identity_test::Holds<&identity_test::internal_variable>>);
static_assert(!HasStableIdentity<identity_test::Holds<&identity_test::external_variable>>);
static_assert(!HasStableIdentity<identity_test::HoldsReference<identity_test::internal_variable>>);
static_assert(!HasStableIdentity<identity_test::Holds<identity_test::Address{&identity_test::external_variable}>>);
static_assert(!HasStableIdentity<identity_test::Holds<^^identity_test::internal_variable>>);
static_assert(!HasStableIdentity<identity_test::Holds<^^identity_test::internal_function>>);
static_assert(!HasStableIdentity<identity_test::Holds<^^identity_test::Internal>>);
static_assert(HasStableIdentity<identity_test::Holds<^^identity_test::external_variable>>);
static_assert(HasStableIdentity<identity_test::Holds<^^identity_test::named_function>>);
static_assert(HasStableIdentity<identity_test::Holds<^^identity_test::Named>>);
static_assert(HasStableIdentity<identity_test::Holds<^^identity_test>>);

// A class value arrives as a template parameter object and is read as a
// value.  A reference to a constexpr variable of the same type prints the
// variable, so it is refused like every other reference.
static_assert(HasStableIdentity<identity_test::Holds<identity_test::Bound{3}>>);
static_assert(HasStableIdentity<identity_test::Holds<identity_test::Named{}>>);
static_assert(HasStableIdentity<identity_test::Holds<identity_test::named_bound>>);
static_assert(!HasStableIdentity<identity_test::Holds<identity_test::Internal{}>>);
static_assert(!HasStableIdentity<identity_test::HoldsBoundReference<identity_test::named_bound>>);

// A number prints without its type, so the stable name appends it.  A
// NaN and a union value print a text that a different value also prints.
static_assert(stable_type_id<identity_test::Holds<1>> != stable_type_id<identity_test::Holds<1L>>);
static_assert(stable_type_id<identity_test::Holds<65>> != stable_type_id<identity_test::Holds<u8'A'>>);
static_assert(stable_type_id<identity_test::Holds<1.5>> != stable_type_id<identity_test::Holds<1.5L>>);
static_assert(stable_type_id<identity_test::Holds<-1>>
              != stable_type_id<identity_test::Holds<static_cast<int identity_test::Bound::*>(nullptr)>>);
static_assert(!HasStableIdentity<identity_test::Holds<__builtin_nan("")>>);
static_assert(!HasStableIdentity<identity_test::Holds<identity_test::Either{.first = 1}>>);
static_assert(!HasStableIdentity<identity_test::Holds<identity_test::HoldsEither{identity_test::Either{.second = 1}}>>);

// An entity that a function body declares has no identity: a local
// class, a class nested in one, and a reflection of a local variable.  A
// function template specialization is read with its arguments.
consteval std::meta::info reflect_a_local_variable() {
    int local = 0;
    (void)local;
    return ^^local;
}
static_assert(!HasStableIdentity<decltype(identity_test::local_of_named())>);
static_assert(!HasStableIdentity<decltype(identity_test::shadowed_local())>);
static_assert(!HasStableIdentity<decltype(identity_test::NestsInLocal::nested<int>())>);
static_assert(!HasStableIdentity<identity_test::Holds<reflect_a_local_variable()>>);
static_assert(!HasStableIdentity<identity_test::Holds<^^identity_test::templated_function<identity_test::Internal>>>);
static_assert(identity_of_type(^^decltype(identity_test::local_of_named())).fault == identity_fault::function_local);
static_assert(identity_of_type(^^decltype(identity_test::local_of_named())).culprit
              == std::meta::dealias(^^decltype(identity_test::local_of_named())));

static_assert(HasStableFunctionIdentity<&identity_test::named_function>);
static_assert(!HasStableFunctionIdentity<identity_test::closure_invoker>);
static_assert(!HasStableFunctionIdentity<&identity_test::internal_function>);
static_assert(!HasStableFunctionIdentity<0>, "a value that is not a pointer to a function names no function");
static_assert(has_stable_function_identity(^^identity_test::named_function));
static_assert(!has_stable_function_identity(^^identity_test::internal_function));
static_assert(has_stable_identity(^^identity_test::Named) && !has_stable_identity(^^identity_test::Internal));

// The verdict names the part that has no identity, not the whole type,
// and the kind of fault that part has.
static_assert(identity_of_type(^^identity_test::Box<identity_test::Box<identity_test::Internal>>).culprit
              == ^^identity_test::Internal);
static_assert(identity_of_type(^^identity_test::Internal).fault == identity_fault::internal_linkage);
static_assert(
    identity_of_type(^^identity_test::Box<identity_test::Box<decltype(identity_test::plain_closure)>>).culprit
    == std::meta::dealias(std::meta::remove_cv(^^decltype(identity_test::plain_closure))));
static_assert(identity_of_type(^^decltype(identity_test::plain_closure)).fault == identity_fault::no_declared_name);

}  // namespace detail::stable_name_self_test

}  // namespace foundation::reflect
