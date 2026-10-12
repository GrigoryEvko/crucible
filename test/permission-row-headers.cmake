# test/permission-row-headers.cmake — the headers that the roster test of the permission tags includes.
# `python3 utils/bin/loop derive header-lists --write` writes this file from the parse trees,
# and the test guard_header_lists fails when the file and the tree disagree.  Do not edit it by hand.
#
# A header joins when it declares an alias permission_row or parent_type, or when it opens the
# namespace permission_rows.
set(CRUCIBLE_PERMISSION_ROW_HEADERS
    crucible/BackgroundThread.h
    crucible/KernelCache.h
    crucible/PermissionedMetaLog.h
    crucible/cntp/BackpressureRuntime.h
    crucible/cntp/ConnectionPoolRuntime.h
    crucible/cntp/PathSwap.h
    crucible/ledger/ProbeSupport.h
    crucible/observe/Metrics.h
    crucible/perf/detail/BpfHub.h
    crucible/warden/Quarantine.h
    fixy/Federation.h
    fixy/OwnedRegion.h
    fixy/concurrent/PermissionedMpscChannel.h
    fixy/concurrent/PermissionedSpscChannel.h
    fixy/concurrent/SwmrSession.h
    fixy/os/OwnedMmap.h
    foundation/permissions/Permission.h
    foundation/permissions/ReadView.h
)
