# test/layer/walk-headers.cmake — the crucible headers that the walk units of test/layer/CMakeLists.txt include.
# `python3 utils/bin/loop derive header-lists --write` writes this file from the parse trees,
# and the test guard_header_lists fails when the file and the tree disagree.  Do not edit it by hand.
#
# A crucible header joins when it opens a namespace of foundation or fixy, or when it names a macro
# of foundation or fixy whose body opens a namespace.  Each foundation and fixy header joins too.
set(CRUCIBLE_WALK_HEADERS
    crucible/PermissionedMetaLog.h
)
