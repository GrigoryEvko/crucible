// A move assignment of a Result whose value writes its own move.  That move could read
// the Result between the old part and the new one, so the Result has no assignment.

#include <foundation/core/Choice.h>

#include <cstdint>

enum class Refusal : std::uint8_t {
    full
};

struct Moved {
    int value = 0;
    explicit Moved(int from) noexcept : value{from} {}
    Moved(Moved&& other) noexcept : value{other.value} { other.value = 0; }
};

int main() {
    ::foundation::core::Result<Moved, Refusal> target{Moved{1}};
    target = ::foundation::core::Result<Moved, Refusal>{::foundation::core::err(Refusal::full)};
    return target.is_err() ? 0 : 1;
}
