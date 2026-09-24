// A version source is not started over bytes.  No constructor of it is
// trivial, so it is not an implicit-lifetime type, and the checked lifetime
// start refuses it.

#include <fixy/EpochVersioned.h>
#include <foundation/Lifetime.h>

int main() {
    alignas(8) unsigned char bytes[sizeof(fixy::VersionSource)]{};
    auto const sources = ::foundation::lifetime::start_as_array<fixy::VersionSource>(bytes, 1);
    return static_cast<int>(sources[0].stamp().epoch().raw());
}
