// The escape from a Secret that does not go through declassify.
//
// Secret has no public reader: transform maps the payload and rewraps
// the result, size forwards a size, zeroize overwrites, and declassify
// is the one door that hands a value out — and it takes a policy from
// the closed set, which is what makes the audit trail greppable.
//
// transform is where that discipline could be walked around, because a
// mapping function that returns a REFERENCE hands out an alias to the
// classified storage with no policy named.  The reference would also
// dangle: transform is rvalue-qualified and consumes the payload, so the
// storage it aliases is moved-from by the time the caller reads it.  The
// static_assert inside transform refuses it and names both halves.
//
// The sibling fixtures cover the other two routes: neg_secret_copy
// duplicates the wrapper, and neg_secret_declassify_unlisted_policy
// names a policy with no edge in the admitted relation.

#include <fixy/Secret.h>

namespace {

int stored = 0;

}  // namespace

int main() {
    auto key = ::fixy::mint_secret<int>(1);
    [[maybe_unused]] auto aliased = std::move(key).transform([](int&&) -> int& { return stored; });
    return 0;
}
