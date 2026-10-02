// The walks of the check files, run again with each header in view that can
// add to the namespaces they walk.
//
// A walk over a namespace or a relation reads the members that its
// translation unit declares before the walk runs.  A check file includes
// its own header and the includes of that header, so a walk in it sees no
// member that another header declares.  When the walk stood in its header,
// a translation unit that included that other header first walked the new
// member too.  A header outside the include closure of a check file can add
// a member that the walk refuses: a policy tag with no edge in
// fixy::tags::secret_policy, a class with no row edge in
// foundation::permissions::tag, an atom that no family roster names, or a
// class in a tag namespace of the OS atoms.
//
// walk_headers.h, which test/layer/CMakeLists.txt writes, includes each
// foundation and fixy header, and each crucible header that opens a
// namespace of foundation or fixy.  This unit includes it, and then the
// check files of walk_checks.h.  test/layer/CMakeLists.txt divides the check
// files of test/layer/walk-checks.txt into several units, and
// utils/scripts/check-walk-units.py derives that list from the parse tree:
// each check file whose checks can call members_of on the reflection of a
// namespace that a header opens.  The include of the header at the top of a
// check file then adds nothing, and its checks run with every member in
// view.  Each build compiles the units, so a walk that refuses a member of
// another header stops the build.

#include "walk_headers.h"

#include "walk_checks.h"
