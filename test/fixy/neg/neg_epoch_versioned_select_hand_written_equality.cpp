// Two values at one version are one event only when their payloads are
// the same, and the equality derived from the members decides that.  A
// payload whose operator== is hand-written is refused: one that holds for
// values a reader tells apart would let a conflict at one version pass as
// one event.

#include <fixy/EpochVersioned.h>

struct Reading {
    int celsius = 0;
    [[nodiscard]] constexpr bool operator==(Reading const&) const noexcept { return true; }
};

int main() {
    auto const first = fixy::EpochVersioned<Reading>::at_genesis(Reading{1});
    auto const second = fixy::EpochVersioned<Reading>::at_genesis(Reading{2});
    return fixy::select_fresher(first, second).has_value() ? 1 : 0;
}
