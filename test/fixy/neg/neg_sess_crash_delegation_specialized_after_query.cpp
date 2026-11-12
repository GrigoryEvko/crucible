// The delegation query instantiates each specialization that a payload
// reaches, at the point of the query.  An explicit specialization of the
// same arguments after that point is ill-formed, and GCC reports it.  The
// specialization below is a declaration, so the report is the one error
// of the late specialization, with no second error for a redefinition.

#include <fixy/session/Flow.h>

namespace s = fixy::session;

namespace {
template <typename T>
struct Frame {
    T value{};
};

static_assert(!s::payload_conveys_delegation_v<Frame<int>*>);

template <>
struct Frame<int>;
}  // namespace

int main() { return 0; }
