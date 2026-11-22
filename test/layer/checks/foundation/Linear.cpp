// The compile-time checks of foundation/Linear.h.

#include <foundation/Linear.h>

#include <foundation/ByteSeal.h>
#include <foundation/Pinned.h>

#include <type_traits>

namespace foundation::linear {

namespace linear_checks {

// A linear class of the shape that the trait asks for: a seal member, a
// trivial move constructor, a move assignment that is not trivial, and a
// trivial destructor.
struct[[= linear{}]] Token {
    [[no_unique_address]] ::foundation::lifetime::byte_seal seal_{};
    constexpr Token() noexcept = default;
    Token(const Token&) = delete("a linear token has one owner");
    Token& operator=(const Token&) = delete("a linear token has one owner");
    constexpr Token(Token&&) noexcept = default;
    constexpr Token& operator=(Token&&) noexcept { return *this; }
    ~Token() = default;
};

struct HoldsToken {
    Token token;
    int count = 0;
};

struct DerivedFromToken : Token {};

struct PinnedHolder : ::foundation::Pinned<PinnedHolder> {
    Token token;
};

struct HoldsReference {
    Token& token;
};

template <bool IsLinear>
struct[[= linear_when<IsLinear>]] Conditional {
    [[no_unique_address]] ::foundation::lifetime::byte_seal seal_{};
    constexpr Conditional() noexcept = default;
    Conditional(const Conditional&) = delete("a linear token has one owner");
    constexpr Conditional(Conditional&&) noexcept = default;
    constexpr Conditional& operator=(Conditional&&) noexcept { return *this; }
    ~Conditional() = default;
};

[[= drop_door{}]] constexpr void end_token(Token&& token) noexcept { static_cast<void>(token); }

inline constexpr auto captures_token = [token = Token{}] { static_cast<void>(token); };

// A context and a key that a body of the once door takes by reference.
struct OnceContext {
    int base = 0;
};
struct OnceKey {
    int uses = 0;
};

}  // namespace linear_checks

namespace lc = linear_checks;

// The shape of a linear class, which keeps the call ABI of the class.
static_assert(is_linear_v<lc::Token>);
static_assert(is_marked_linear(^^lc::Token));
static_assert(has_linear_shape(^^lc::Token));
static_assert(!std::is_trivially_copyable_v<lc::Token>);
static_assert(std::is_trivially_move_constructible_v<lc::Token>);
static_assert(!std::is_trivially_move_assignable_v<lc::Token>);
static_assert(std::is_trivially_destructible_v<lc::Token>);
static_assert(std::is_empty_v<lc::Token> && sizeof(lc::Token) == 1);

// The trait reads the type with its cv-qualifiers and its reference removed.
static_assert(is_linear_v<lc::Token const&>);
static_assert(is_linear_v<lc::Token&&>);
static_assert(!is_linear_v<int>);
static_assert(!is_linear_v<int*>);

// A class that can move is linear by a base or a member that it holds by
// value, and an array is linear by its element.  The mark stays on the
// class that carries it.  The rule proof-routes refuses a union that holds
// a sealed class, so this file declares no such union.
static_assert(is_linear_v<lc::HoldsToken>);
static_assert(!is_marked_linear(^^lc::HoldsToken));
static_assert(is_linear_v<lc::DerivedFromToken>);
static_assert(is_linear_v<lc::Token[3]>);

// A class that cannot move is not linear by its members.
static_assert(!std::is_move_constructible_v<lc::PinnedHolder>);
static_assert(!is_linear_v<lc::PinnedHolder>);

// A reference holds no value.  The checks of foundation/reflect/TypeComponents.h
// hold the pointer, which the walk reads as a part that is not held by value.
static_assert(!is_linear_v<lc::HoldsReference>);

// A class template marks only the instances whose condition is true.
static_assert(is_linear_v<lc::Conditional<true>>);
static_assert(!is_linear_v<lc::Conditional<false>>);
static_assert(carries_annotation(^^lc::Conditional<false>, ^^not_linear));
static_assert(!carries_annotation(^^lc::Conditional<false>, ^^linear));

// A function carries the annotation of its door.
static_assert(carries_annotation(^^lc::end_token, ^^drop_door));
static_assert(!carries_annotation(^^lc::end_token, ^^detach_door));

// A closure that holds a linear value by copy is linear: the sixth GCC
// patch shows the field of the capture, and the trait reads it as a member.
static_assert(is_linear_v<decltype(lc::captures_token)>);

// The drop door takes a linear value as an rvalue only.
static_assert(LinearValue<lc::Token>);
static_assert(!LinearValue<lc::Token&>);
static_assert(!LinearValue<lc::Token const>);
static_assert(!LinearValue<int>);
static_assert([] {
    lc::Token token;
    drop(static_cast<lc::Token&&>(token));
    return true;
}());
// The probe of one call of the drop door.  Its trailing return type makes a
// call that the door refuses a substitution failure.
inline constexpr auto drop_probe = [](auto&& value) -> decltype(drop(static_cast<decltype(value)&&>(value))) {};
static_assert(std::is_invocable_v<decltype(drop_probe), lc::Token&&>);
static_assert(!std::is_invocable_v<decltype(drop_probe), lc::Token&>);
static_assert(!std::is_invocable_v<decltype(drop_probe), int&&>);

// The once door owns the closure and calls it one time, and its result is the
// result of that call.  A closure that moves its linear capture out goes
// through it.
static_assert(call_once([value = 7]() noexcept { return value + 1; }) == 8);
static_assert([] {
    lc::Token token;
    call_once([held = static_cast<lc::Token&&>(token)]() mutable noexcept { drop(static_cast<lc::Token&&>(held)); });
    return true;
}());
// The door forwards its arguments to the one call: a context by a reference
// to const and a key by a reference that the body writes.
static_assert([] {
    lc::OnceContext const context{40};
    lc::OnceKey key{};
    int const result = call_once(
        [extra = 2](lc::OnceContext const& held_context, lc::OnceKey& held_key) noexcept {
            ++held_key.uses;
            return held_context.base + extra;
        },
        context, key);
    return result == 42 && key.uses == 1;
}());
// The probe of one call of the once door.  It takes a closure as an rvalue
// only, so a named closure stays with no second name that can call it again.
inline constexpr auto once_probe =
    [](auto&& closure) -> decltype(call_once(static_cast<decltype(closure)&&>(closure))) {};
using GiveOne = decltype([]() noexcept { return 1; });
static_assert(std::is_invocable_v<decltype(once_probe), GiveOne&&>);
static_assert(!std::is_invocable_v<decltype(once_probe), GiveOne&>);
static_assert(!std::is_invocable_v<decltype(once_probe), GiveOne const&&>);
static_assert(!std::is_invocable_v<decltype(once_probe), int&&>);
// The door refuses arguments that the closure does not take.
inline constexpr auto once_with_probe = [](auto&& closure, auto&& argument)
    -> decltype(call_once(static_cast<decltype(closure)&&>(closure), static_cast<decltype(argument)&&>(argument))) {};
using TakesKey = decltype([](lc::OnceKey& key) noexcept { return key.uses; });
static_assert(std::is_invocable_v<decltype(once_with_probe), TakesKey&&, lc::OnceKey&>);
static_assert(!std::is_invocable_v<decltype(once_with_probe), TakesKey&&, lc::OnceContext&>);
static_assert(!std::is_invocable_v<decltype(once_with_probe), TakesKey&, lc::OnceKey&>);

}  // namespace foundation::linear
