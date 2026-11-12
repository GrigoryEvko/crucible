// I002: a declassification x a thrown failure whose error type is not
// Secret.
//
// The binding declassifies under AuditedLogging, whose mask licenses IO and
// nothing else.  No policy licenses the failure channel, so the error value
// leaves the binding while the binding is still classified on that channel.
// The pack names no IO row, so no corpus entry matches, and it trips I002
// alone.

#include <fixy/Fn.h>

struct fault final {};

int main() {
    [[maybe_unused]] ::fixy::fn<int, ::fixy::atom::declassify<::fixy::tags::secret_policy::AuditedLogging>,
                                ::fixy::atom::ctrl::throws<fault>> refused{};
    return 0;
}
