// Corpus entry classified_io_without_declassify, under a policy for a
// different channel.
//
// A declassification licenses exactly the channels its policy names.
// AuthorizedReplay licenses a bounded replay window, and says nothing
// about an export.  The binding stays classified on the IO channel, so an
// IO row under it is a classified value on an observable channel.  An
// export policy, such as WireSerialize, licenses the same row.
//
// No collision rule reads this pair, so the refusal is the corpus's
// alone and the message carries the entry and its citation.

#include <fixy/Fn.h>

int main() {
    [[maybe_unused]] ::fixy::fn<int, ::fixy::atom::with_io,
                                ::fixy::atom::declassify<::fixy::tags::secret_policy::AuthorizedReplay>> refused{};
    return 0;
}
