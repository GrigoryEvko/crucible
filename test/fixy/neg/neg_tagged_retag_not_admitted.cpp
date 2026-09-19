// The trust catalog is a one-way ratchet.  Verified -> Unverified has
// no edge in fixy::tags::admitted_retags, so the requires-clause of
// retag<To>() refuses the call and names RetagAllowed.

#include <fixy/Tagged.h>

#include <utility>

namespace tags = ::fixy::tags;

int main() {
    fixy::Tagged<int, tags::trust::Verified> proved = fixy::mint_tagged<tags::trust::Verified>(42);
    auto erased = std::move(proved).retag<tags::trust::Unverified>();
    return erased.value();
}
