// The reason of expect() ends with a zero character, so the report finds
// its end.  A constant array with no terminating zero stops the build at
// the call.

#include <foundation/core/Choice.h>

constexpr char unterminated_reason[3] = {'a', 'b', 'c'};

int main() { return ::foundation::core::Option<int>::some(1).expect(unterminated_reason); }
