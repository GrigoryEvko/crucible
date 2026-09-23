// The moving form of select_fresher has the same rule as the copying
// form: a payload with no equality cannot show that two values at one
// version are one event.

#include <fixy/EpochVersioned.h>

#include <utility>

struct Reading {
    int celsius = 0;
};

int main() {
    fixy::EpochVersioned<Reading> first{Reading{1}, fixy::Epoch{4}, fixy::Generation{4}};
    fixy::EpochVersioned<Reading> second{Reading{2}, fixy::Epoch{4}, fixy::Generation{4}};
    return fixy::select_fresher(std::move(first), std::move(second)).has_value() ? 1 : 0;
}
