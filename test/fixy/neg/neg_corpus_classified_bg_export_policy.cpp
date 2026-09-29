// Corpus entry classified_bg_without_declassify, under an export policy.
//
// AuditedLogging licenses the log line it writes, which leaves through
// IO.  A crossing into a background context is a different channel: the
// scheduling then depends on the value, and no policy licenses that.  The
// binding stays classified on the Bg channel, so the crossing is refused.
//
// No collision rule reads this pair, so the refusal is the corpus's
// alone and the message carries the entry and its citation.

#include <fixy/Fn.h>

int main() {
    [[maybe_unused]] ::fixy::fn<int, ::fixy::atom::with_bg,
                                ::fixy::atom::declassify<::fixy::tags::secret_policy::AuditedLogging>> refused{};
    return 0;
}
