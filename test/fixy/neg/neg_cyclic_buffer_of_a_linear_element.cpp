// A CyclicBuffer of a linear element.  A push into a full ring would end the
// oldest element with no consume, and no path of a caller would show it.  A
// ring holds only an element that a holder can drop.

#include <fixy/CyclicBuffer.h>
#include <foundation/Linear.h>

namespace test_cyclic_buffer_linear_element {
class [[nodiscard]][[= ::foundation::linear::linear{}]] Ticket final {
public:
    Ticket() noexcept = default;
    Ticket(Ticket&&) noexcept = default;
    Ticket& operator=(Ticket&&) noexcept = default;
    Ticket(Ticket const&) = delete("a ticket is one use");
    Ticket& operator=(Ticket const&) = delete("a ticket is one use");
    ~Ticket() = default;
};
}  // namespace test_cyclic_buffer_linear_element

int main() {
    using test_cyclic_buffer_linear_element::Ticket;
    return static_cast<int>(sizeof(::fixy::CyclicBuffer<Ticket, 2>));
}
