// A crash session whose payload names a specialization of a template that
// is only declared here.  The payload walk reads a class that a template
// argument names, and it cannot read this one.  A unit that defines the
// template would read it, so the walk stops the build instead of giving a
// value.  The walk answers the permission question and the delegation
// question at one time, so the gate of the crash mint stops the build at
// the payload readable gate.  The fixture asks the gate with no mint call,
// so the refusal is the one error.

#include <fixy/session/CrashTransport.h>

namespace s = fixy::session;

namespace {
struct Alice {};
struct Bob {};

template <typename T>
struct OpaqueChannel;

template <typename T>
struct Names {};

using Proto = s::Offer<s::Recv<Names<OpaqueChannel<int>>, s::End>, s::Recv<s::Crash<Bob>, s::End>>;

constexpr bool is_admitted = s::CrashSessionAdmissible<Proto, Alice, Bob, s::ReliableSet<>>;
}  // namespace

int main() { return is_admitted ? 1 : 0; }
