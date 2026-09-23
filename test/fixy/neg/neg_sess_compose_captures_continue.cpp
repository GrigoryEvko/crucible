// Composition with a bare Continue puts the Continue where the End of the
// loop stood.  The Continue binds that Loop, so the exit of the loop
// becomes a loop-back and the protocol can never end.  Composition
// refuses the capture.

#include <fixy/session/Protocol.h>

namespace {

namespace s = ::fixy::session;

struct Job {};
using Producer = s::Loop<s::Select<s::Send<Job, s::Continue>, s::End>>;

}  // namespace

int main() { return sizeof(s::compose_t<Producer, s::Continue>) == 0 ? 1 : 0; }
