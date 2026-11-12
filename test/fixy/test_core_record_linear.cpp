// Tests of a Record of linear parts: permission tokens and fixy::Linear
// values.
//
// A Record that holds a linear part is linear itself, because it can move
// and holds a linear base (foundation/Linear.h).  It does not copy, it does
// not start a lifetime over bytes, and a binding of the moved Record gives
// out each part one time.

#include <fixy/Core.h>
#include <fixy/Qtt.h>
#include <foundation/Lifetime.h>
#include <foundation/Linear.h>
#include <foundation/Platform.h>
#include <foundation/permissions/Permission.h>

#include <type_traits>
#include <utility>

namespace {

struct LeftRegion final {
    using permission_row = ::foundation::effects::Row<>;
};
struct RightRegion final {
    using permission_row = ::foundation::effects::Row<>;
};

// The traits of a Record of two tokens: linear, move-only, with no default
// value and no lifetime start over bytes.
template <class Tokens>
consteval bool holds_the_token_traits() {
    return ::foundation::linear::is_linear_v<Tokens> && !std::is_copy_constructible_v<Tokens>
        && std::is_nothrow_move_constructible_v<Tokens> && !std::is_default_constructible_v<Tokens>
        && !std::is_trivially_copyable_v<Tokens> && !::foundation::lifetime::ImplicitLifetimeThroughout<Tokens>;
}

void test_tokens_move_out_through_a_binding() {
    fixy::Record tokens{::foundation::permissions::mint_permission_root<LeftRegion>(),
                        ::foundation::permissions::mint_permission_root<RightRegion>()};
    static_assert(holds_the_token_traits<decltype(tokens)>());
    static_assert(sizeof(tokens) == 1);

    auto [left, right] = std::move(tokens);
    ::foundation::linear::drop(std::move(left));
    ::foundation::linear::drop(std::move(right));
}

void test_linear_values_consume_through_a_binding() {
    using Values = fixy::Record<fixy::Linear<int>, fixy::Linear<int>>;
    static_assert(::foundation::linear::is_linear_v<Values>);
    static_assert(!std::is_copy_constructible_v<Values>);
    static_assert(sizeof(Values) == 2 * sizeof(int));

    Values values{fixy::mint_linear<int>(3), fixy::mint_linear<int>(4)};
    auto [first, second] = std::move(values);
    int const sum = std::move(first).consume() + std::move(second).consume();
    CRUCIBLE_FATAL_INVARIANT(sum == 7);
}

// A Record with a linear part and a plain part is linear too.
void test_mixed_parts() {
    fixy::Record<fixy::Linear<int>, int> mixed{fixy::mint_linear<int>(5), 6};
    static_assert(::foundation::linear::is_linear_v<decltype(mixed)>);
    auto [owned, plain] = std::move(mixed);
    CRUCIBLE_FATAL_INVARIANT(std::move(owned).consume() + plain == 11);
}

}  // namespace

int main() {
    test_tokens_move_out_through_a_binding();
    test_linear_values_consume_through_a_binding();
    test_mixed_parts();
    return 0;
}
