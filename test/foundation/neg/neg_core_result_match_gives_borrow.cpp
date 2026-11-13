// An arm of the rvalue match takes its part by value, so the part ends when
// the arm returns.  A view of the part in the result dangles after the match,
// so the rvalue match refuses a result that holds a borrow.

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
            [](Numbers numbers) noexcept { return numbers.view(); },
            [](Code) noexcept { return fallback_numbers.view(); });
    return seen.size() == 4 ? 0 : 1;
}
