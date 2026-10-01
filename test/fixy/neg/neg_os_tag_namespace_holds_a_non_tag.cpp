// The OS tag check walks the ten namespaces the tags live in, rather
// than a hand-written roster of types.  The point of walking is to see a
// member nobody listed, so the witness that it works is a class the
// check was never told about.
//
// The class is planted before the header, not after.  The walk is a
// template instantiated once, and its member list is fixed at that
// instantiation, so a class declared after the walk has run is not
// visible to it.  Declaring the class first is what puts it in front of
// the walk, and it is also the real shape of the drift: a tag arrives in
// the namespace, and the check either notices or it does not.  The
// header holds no checks, so the fixture includes the check file of the
// header, which includes the header and runs its checks.

namespace fixy::io::engine {

// Neither empty nor final: not the shape of a tag.
struct DriftedIntoTheNamespace {
    int state = 0;
};

}  // namespace fixy::io::engine

#include "../../layer/checks/fixy/atoms/Os.cpp"

int main() { return 0; }
