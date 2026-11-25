// A Handle of the kind File takes only an opened number whose file is a
// regular file (the kinds of fixy/os/Handle.h).  The number of a call that
// proves its kind by the call alone, such as a socket, states of_call, so no
// constructor of a File takes it.  The refusal is the type of the number, and
// it comes before the access of the constructor.

#include <fixy/os/Handle.h>

namespace fs = fixy::fs;
namespace os = fixy::os;

using File = os::Handle<os::kind::File, os::RightSet::read>;
using CalledNumber = fs::OpenedDescriptor<fs::OpenedType::of_call>;

namespace forge {
// A stand-in for the number that a door gives.  The compiler refuses the
// construction, so no fixture links, and the stand-in has no body.
[[nodiscard]] CalledNumber&& called_number() noexcept;
}  // namespace forge

int main() {
    [[maybe_unused]] File const file{forge::called_number()};
    return 0;
}
