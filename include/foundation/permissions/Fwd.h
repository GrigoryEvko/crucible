#pragma once

// The forward declarations of the two permission tokens, with their
// defaulted brand.  A default template argument may be written on one
// declaration only, so a header that names the tokens without wanting
// Permission.h as a prerequisite includes this one rather than
// declaring them itself: fixy/Qtt.h names both in a concept body and
// instantiates neither.

#include <foundation/Brand.h>

namespace foundation::permissions {

template <typename Tag, typename Brand = ::foundation::brand::DefaultBrand>
class Permission;

template <typename Tag, typename Brand = ::foundation::brand::DefaultBrand>
class SharedPermission;

}  // namespace foundation::permissions
