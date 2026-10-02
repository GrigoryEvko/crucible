#pragma once

// The primitive families in namespace fixy.
//
// The families live in include/foundation/core/, in namespace
// foundation::core, because the foundation layers use them too.  This
// header puts each public name of the families into namespace fixy, so
// code outside the base spells fixy::Option and fixy::none.  Each line
// is a using-declaration of the one entity, so the fixy name and the
// foundation name are the same entity and not a second type.
//
// A specialization of a family template, for example of
// foundation::core::niche, names the template in namespace
// foundation::core.  A specialization through a using-declaration is not
// valid C++.

#include <foundation/core/Choice.h>

namespace fixy {

using ::foundation::core::none;
using ::foundation::core::NoValue;
using ::foundation::core::Option;

}  // namespace fixy
