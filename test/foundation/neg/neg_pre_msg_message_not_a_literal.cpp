// The message of CRUCIBLE_PRE_MSG is a string literal.  The form puts ""
// before the message, and a string literal next to "" is one literal.  A
// message of a different kind, such as a named constant, next to "" is no
// expression, so the build stops at the call.

#include <foundation/contracts/Pre.h>

namespace {

constexpr int reason_code = 7;

int check_value(int value) noexcept {
    CRUCIBLE_PRE_MSG(value > 0, reason_code);
    return value;
}

}  // namespace

int main() { return check_value(1); }
