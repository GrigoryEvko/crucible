// relax on a const lvalue copies the payload, so the overload asks for
// a copy-constructible T and a move-only payload leaves no viable
// overload at the call site.  The rvalue overload moves and is not
// viable for an lvalue, so the rejection names the copy_constructible
// clause rather than a deleted copy deep inside the substrate.

#include <fixy/Bands.h>

struct MoveOnlyValue {
    int v{0};
    constexpr explicit MoveOnlyValue(int x) noexcept : v{x} {}
    MoveOnlyValue(MoveOnlyValue const&) = delete;
    MoveOnlyValue& operator=(MoveOnlyValue const&) = delete;
    constexpr MoveOnlyValue(MoveOnlyValue&&) noexcept = default;
    constexpr MoveOnlyValue& operator=(MoveOnlyValue&&) noexcept = default;
};

int main() {
    fixy::det_safe::Pure<MoveOnlyValue> pure{MoveOnlyValue{42}, {}};
    auto philox = fixy::relax<fixy::DetSafeTier_v::PhiloxRng>(pure);
    return philox.peek().v;
}
