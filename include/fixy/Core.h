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

#include <foundation/core/Atomic.h>
#include <foundation/core/Choice.h>
#include <foundation/core/Format.h>
#include <foundation/core/Ref.h>
#include <foundation/core/Region.h>
#include <foundation/core/Report.h>
#include <foundation/core/Text.h>

namespace fixy {

using ::foundation::core::fatal;
using ::foundation::core::Fmt;
using ::foundation::core::format;
using ::foundation::core::Formattable;
using ::foundation::core::report;
using ::foundation::core::Sink;
using ::foundation::core::Site;
using ::foundation::core::Truncated;
using ::foundation::core::unreachable;

using ::foundation::core::FixedText;
using ::foundation::core::TextView;

using ::foundation::core::none;
using ::foundation::core::NoValue;
using ::foundation::core::Option;

using ::foundation::core::Err;
using ::foundation::core::err;
using ::foundation::core::Result;
using ::foundation::core::Unit;

using ::foundation::core::Box;
using ::foundation::core::mint_box;

using ::foundation::core::Atomic;
using ::foundation::core::CacheLine;
using ::foundation::core::CasRefusal;
using ::foundation::core::Tally;

using ::foundation::core::copy;
using ::foundation::core::dynamic_extent;
using ::foundation::core::fill;
using ::foundation::core::LengthMismatch;
using ::foundation::core::View;

}  // namespace fixy
