// The delegation query instantiates each specialization that a payload
// reaches, at the point of the query.  An explicit specialization of the
// same arguments after that point is ill-formed, and GCC reports it.

#include <fixy/session/Payload.h>

namespace s = fixy::session;

namespace {
template <typename T>
struct Frame {
    T value{};
};

static_assert(!s::payload_conveys_delegation_v<Frame<int>*>);

template <>
struct Frame<int> {
    long value = 0;
};
}  // namespace

int main() { return 0; }
