#include <crucible/cntp/IncastControl.h>

namespace crucible::cntp {

::fixy::TextView incast_error_name(IncastError error) noexcept { return ::foundation::core::enum_name_text(error); }

}  // namespace crucible::cntp
