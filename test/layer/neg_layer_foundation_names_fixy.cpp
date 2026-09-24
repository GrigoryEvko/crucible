// A foundation header that names a fixy type and expects its includer to
// declare it.  The root of the foundation layer reaches no fixy header, so
// nothing declares the name, whatever the includer does elsewhere.

#include <foundation/Platform.h>

namespace foundation::layer_probe {
using Classified = ::fixy::Secret<int>;
}  // namespace foundation::layer_probe
