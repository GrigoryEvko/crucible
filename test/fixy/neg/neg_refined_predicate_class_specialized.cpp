// The mint reads the name of a predicate, and the implication relation
// reads it too.  This file tries to forge a refinement over in_range<1,
// 4096> that holds zero: it writes an explicit specialization of the
// class template InRange whose call operator admits each value.  The seal
// of the mint finds the class outside fixy/Predicates.h and stops the
// build.

#include <fixy/Refined.h>

template <>
struct fixy::InRange<1, 4096> {
    template <class V>
    constexpr bool operator()(V) const noexcept {
        return true;
    }
};

constexpr auto forged = fixy::mint_refined<fixy::in_range<1, 4096>>(0);

int main() { return forged.value(); }
