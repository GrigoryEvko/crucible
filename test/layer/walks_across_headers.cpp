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
// namespace of foundation or fixy.  This unit includes it, and then each
// check file whose checks walk such a namespace.  The include of the header
// at the top of a check file then adds nothing, and its checks run with
// every member in view.  Each build compiles this unit, so a walk that
// refuses a member of another header stops the build.

#include "walk_headers.h"

#include "checks/foundation/effects/Capability.cpp"
#include "checks/foundation/effects/Resources.cpp"
#include "checks/foundation/permissions/Permission.cpp"

#include "checks/fixy/Atom.cpp"
#include "checks/fixy/Bands.cpp"
#include "checks/fixy/Refined.cpp"
#include "checks/fixy/Secret.cpp"
#include "checks/fixy/Tagged.cpp"
#include "checks/fixy/atoms/Barrier.cpp"
#include "checks/fixy/atoms/Ctrl.cpp"
#include "checks/fixy/atoms/Dispatch.cpp"
#include "checks/fixy/atoms/Global.cpp"
#include "checks/fixy/atoms/Hw.cpp"
#include "checks/fixy/atoms/Os.cpp"
#include "checks/fixy/atoms/Regime.cpp"
#include "checks/fixy/atoms/Scope.cpp"
#include "checks/fixy/atoms/Session.cpp"
#include "checks/fixy/atoms/Simd.cpp"
#include "checks/fixy/atoms/Stack.cpp"
#include "checks/fixy/atoms/Stdio.cpp"
#include "checks/fixy/atoms/Sync.cpp"
#include "checks/fixy/atoms/Syscall.cpp"
#include "checks/fixy/session/Protocol.cpp"

#include "checks/crucible/warden/Quarantine.cpp"
