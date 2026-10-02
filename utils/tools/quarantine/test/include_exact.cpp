// The unit enters gapsother/fixy/gaps/Same.h first, and then the base header
// fixy/GapsUser.h, which includes fixy/gaps/Same.h two times.  The second
// directive enters no file.  The plugin must find include/fixy/gaps/Same.h for
// it, as libcpp does, and not the first entered file whose path ends with the
// name.  So the unit has no upward_include.

#include "gapsother/fixy/gaps/Same.h"

#include <fixy/GapsUser.h>
