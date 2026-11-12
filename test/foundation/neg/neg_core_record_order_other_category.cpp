// <=> of a Record takes only parts whose <=> gives a strong or a partial
// order.  A part whose <=> gives a different type gives the Record no <=>.

#include <foundation/core/Record.h>

#include <compare>

struct SignedOrder {
    int value = 0;
    friend int operator<=>(SignedOrder const& left, SignedOrder const& right) noexcept {
        return left.value - right.value;
    }
};

int main() {
    ::foundation::core::Record<int, SignedOrder> const left{};
    ::foundation::core::Record<int, SignedOrder> const right{};
    return std::is_lt(left <=> right) ? 0 : 1;
}
