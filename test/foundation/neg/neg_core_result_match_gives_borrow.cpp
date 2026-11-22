// An arm of the rvalue match takes its part by value, so the part ends when
// the arm returns.  A view of the part in the result dangles after the match,
// so the rvalue match refuses each result that holds a borrow, also a View of
// an object that outlives the match.  The rule view-escape refuses the return
// of the view of the part itself in the arm, so the arms here give the View of
// an inline variable, and the refusal is the refusal of the match alone.

#include <foundation/core/Array.h>
#include <foundation/core/Choice.h>

using Numbers = ::foundation::core::FixedArray<int, 4>;

enum class Code : unsigned char {
    closed
};

inline Numbers fallback_numbers{};

int main() {
    ::foundation::core::Result<Numbers, Code> result{Numbers{1, 2, 3, 4}};
    ::foundation::core::View<int, 4> const seen =
        static_cast<::foundation::core::Result<Numbers, Code>&&>(result).match(
            [](Numbers) noexcept { return fallback_numbers.view(); },
            [](Code) noexcept { return fallback_numbers.view(); });
    return seen.size() == 4 ? 0 : 1;
}
