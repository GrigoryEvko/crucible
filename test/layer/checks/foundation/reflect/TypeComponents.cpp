// The compile-time checks of foundation/reflect/TypeComponents.h.

#include <foundation/reflect/TypeComponents.h>

namespace foundation::reflect {

namespace detail::type_components_self_test {

struct Needle {};
struct HoldsNeedle {
    Needle held;
};
struct DerivesNeedle : Needle {};
struct PointsAtNeedle {
    Needle* held = nullptr;
};
struct Unrelated {
    int value = 0;
};
struct SelfReferential {
    SelfReferential* next = nullptr;
    int value = 0;
};
template <class T>
struct Wrap {};
template <int N, class T>
struct MixedWrap {};
template <auto V>
struct ValueWrap {};

struct NeedleValue {
    Needle held{};
};

// The walk stops the build if it instantiates this template where the
// template is not a root.
template <class T>
struct Detonates {
    static_assert(sizeof(T) == 0, "the walk instantiated a specialization that it reached indirectly");
};

inline constexpr auto is_needle = [](TypeNode node) consteval { return node.type == ^^Needle; };

static_assert(any_component_satisfies<is_needle>(^^Needle));
static_assert(any_component_satisfies<is_needle>(^^Needle const&));
static_assert(any_component_satisfies<is_needle>(^^HoldsNeedle), "a member is a component");
static_assert(any_component_satisfies<is_needle>(^^DerivesNeedle), "a base is a component");
static_assert(any_component_satisfies<is_needle>(^^PointsAtNeedle), "a pointee is a component");
static_assert(any_component_satisfies<is_needle>(^^Needle[4]));
static_assert(any_component_satisfies<is_needle>(^^Wrap<Needle>));
static_assert(any_component_satisfies<is_needle>(^^MixedWrap<3, Needle>),
              "a template with a value parameter is still read for its type arguments");
static_assert(any_component_satisfies<is_needle>(^^ValueWrap<NeedleValue{}>),
              "the type of a value argument is a component");
static_assert(any_component_satisfies<is_needle>(^^Wrap<HoldsNeedle>),
              "a class that is not a specialization is read for members wherever the walk reaches it");
static_assert(first_component_satisfying<is_needle>(^^HoldsNeedle).type == ^^Needle,
              "the first accepted node is the one the walk answers with");
static_assert(first_component_satisfying<is_needle>(^^Unrelated).type == std::meta::info{},
              "a walk that accepts no node answers with a null type");

// A function hands out its return type and takes its parameters.  A
// pointer to member is a function from its class to its member.
struct Holder {};
struct HoldsFactory {
    Needle (*make)() = nullptr;
};
struct HoldsFunctionReference {
    Needle (&make)();
};
static_assert(any_component_satisfies<is_needle>(^^Needle (*)()), "a return type is a component");
static_assert(any_component_satisfies<is_needle>(^^void (*)(int, Needle const&) noexcept),
              "a parameter type is a component");
static_assert(any_component_satisfies<is_needle>(^^HoldsFactory), "a function pointer member is read");
static_assert(any_component_satisfies<is_needle>(^^HoldsFunctionReference), "a function reference member is read");
static_assert(any_component_satisfies<is_needle>(^^Needle Holder::*),
              "the member type of a pointer to member is a component");
static_assert(any_component_satisfies<is_needle>(^^int Needle::*), "the class of a pointer to member is a component");
static_assert(any_component_satisfies<is_needle>(^^void (Holder::*)(Needle) const&),
              "a parameter of a member function is a component");
static_assert(!any_component_satisfies<is_needle>(^^int (*)(double, ...)));
static_assert(!any_component_satisfies<is_needle>(^^long (Holder::*)() volatile&&));
static_assert(member_pointer_class_of(^^Needle Holder::*) == ^^Holder);
static_assert(member_pointer_member_of(^^Needle Holder::* const) == ^^Needle);
static_assert(member_pointer_class_of(^^Needle*) == std::meta::info{});

// The capture gap, pinned.  The closure holds a Needle and the walk does
// not see it, because GCC 16 reflects no data member of a closure type.
inline constexpr auto captures_needle = [held = NeedleValue{}] { return sizeof(held); };
static_assert(!any_component_satisfies<is_needle>(^^decltype(captures_needle)),
              "the walk now sees a lambda capture.  The compiler reflects captures, which closes a gap: delete "
              "this cell and the capture paragraph at the head of this header.");

// A gate refuses the closure instead, because its state is unreadable.
inline constexpr auto captures_nothing = [] { return 7; };
struct HoldsOnlyAnUnnamedBitField {
    unsigned : 8;
};
static_assert(holds_unreadable_state(TypeNode{^^decltype(captures_needle), true}));
static_assert(holds_unreadable_state(TypeNode{^^HoldsOnlyAnUnnamedBitField, true}));
static_assert(!holds_unreadable_state(TypeNode{^^decltype(captures_nothing), true}), "an empty closure holds nothing");
static_assert(!holds_unreadable_state(TypeNode{^^HoldsNeedle, true}));
static_assert(!holds_unreadable_state(TypeNode{^^int, true}));
static_assert(!holds_unreadable_state(TypeNode{^^decltype(captures_needle), false}),
              "a node whose members the walk may not read is not read");

static_assert(!any_component_satisfies<is_needle>(^^int));
static_assert(!any_component_satisfies<is_needle>(^^Unrelated));
static_assert(!any_component_satisfies<is_needle>(^^SelfReferential), "a cycle through a pointer ends the walk");
static_assert(!any_component_satisfies<is_needle>(^^Wrap<Unrelated>));

// A specialization reached through a template argument is read for its
// arguments and not instantiated.
static_assert(!any_component_satisfies<is_needle>(^^Wrap<Detonates<int>>));
static_assert(any_component_satisfies<is_needle>(^^Wrap<Detonates<Needle>>));
static_assert(!any_component_satisfies<is_needle>(^^Detonates<int> (*)(Detonates<long>)),
              "a return and a parameter type are read for their arguments and not instantiated");
// GCC instantiates the class of a pointer to member when it forms the
// type, so the class here is a harmless wrapper of the detonator.
static_assert(!any_component_satisfies<is_needle>(^^Detonates<int> Wrap<Detonates<long>>::*),
              "the two parts of a pointer to member are read for their arguments and not instantiated");

// The instantiating read.  A specialization that holds a Needle in a
// member that no argument names is seen only when the walk reads it.
template <class T>
struct BoxesNeedle {
    T tag{};
    Needle held{};
};
template <class T>
struct OnlyDeclared;
inline constexpr auto is_unreadable_class = [](TypeNode node) consteval {
    return std::meta::is_class_type(node.type) && !node.may_read_members;
};
constexpr SpecializationRead kInstantiating = SpecializationRead::Instantiating;

static_assert(!any_component_satisfies<is_needle>(^^BoxesNeedle<int>*));
static_assert(any_component_satisfies<is_needle, kInstantiating>(^^BoxesNeedle<int>*),
              "a pointee specialization is read for its members");
static_assert(any_component_satisfies<is_needle, kInstantiating>(^^Wrap<BoxesNeedle<int>>),
              "a specialization named by an argument is read for its members");
static_assert(any_component_satisfies<is_needle, kInstantiating>(^^BoxesNeedle<int> (*)()),
              "a specialization named by a return type is read for its members");
static_assert(any_component_satisfies<is_needle, kInstantiating>(^^int BoxesNeedle<long>::*),
              "a specialization named by a pointer to member is read for its members");
static_assert(first_component_satisfying<is_unreadable_class, kInstantiating>(^^OnlyDeclared<int>*).type
                  == ^^OnlyDeclared<int>,
              "a template with no definition stays unreadable, so a gate can refuse it");

}  // namespace detail::type_components_self_test

}  // namespace foundation::reflect
