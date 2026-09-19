// The OS tag check walks the nine namespaces the tags live in, rather
// than the hand-written roster of thirty-five types that stood there.
// The point of walking is to see a member nobody listed, so the witness
// that it works is a class the check was never told about.
//
// The class is planted before the header, not after.  The walk is a
// template instantiated once, and its member list is fixed at that
// instantiation, so a class declared after the header's own self-test
// has already run is not visible to it.  Declaring the class first is
// what puts it in front of the walk, and it is also the real shape of
// the drift: a tag arrives in the namespace, and the check either
// notices or it does not.

namespace fixy::io::engine {

// Neither empty nor final: not the shape of a tag.
struct DriftedIntoTheNamespace {
    int state = 0;
};

}  // namespace fixy::io::engine

#include <fixy/atoms/Os.h>

int main() { return 0; }
