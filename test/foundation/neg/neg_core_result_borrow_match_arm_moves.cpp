// An arm of the borrow match of a Result that moves the Result ends the value
// that the reference of the arm names.  The rule borrow-match of the
// quarantine plugin refuses the move.

#include <foundation/core/Choice.h>

#include <utility>

enum class Code : unsigned char {
    full
};

int main() {
    ::foundation::core::Result<int, Code> result{2};
    return result.match([&](int const& value) { return std::move(result).value_or(0) + value; },
                        [](Code const&) { return 0; });
}
