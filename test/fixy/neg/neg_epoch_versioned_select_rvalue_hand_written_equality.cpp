// The moving form of select_fresher has the same rule as the copying
// form.  A payload whose operator== is hand-written is refused, here where
// the operator== sits in a member of the payload.

#include <fixy/EpochVersioned.h>

#include <utility>

struct Celsius {
    int degrees = 0;
    [[nodiscard]] constexpr bool operator==(Celsius const&) const noexcept { return true; }
};

struct Reading {
    int sensor = 0;
    Celsius temperature{};
};

int main() {
    auto first = fixy::EpochVersioned<Reading>::at_genesis(Reading{1, Celsius{20}});
    auto second = fixy::EpochVersioned<Reading>::at_genesis(Reading{1, Celsius{30}});
    return fixy::select_fresher(std::move(first), std::move(second)).has_value() ? 1 : 0;
}
