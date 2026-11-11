# The Ninja pools of the compiles and the links.
#
# The host gives the depths of the two pools at each configure.  This file
# gives Ninja two pools:
#
#   compile  each compile of each target, and each custom command that compiles
#            an object (JOB_POOL ${CRUCIBLE_COMPILE_POOL}, as in
#            cmake/CrucibleBpf.cmake).  The depth is the number of logical
#            cores of the host
#   link     each link and each archive of each target.  The depth is half the
#            number of logical cores of the host, and at least 1
#
# Each link runs mold with the threads of the rule link-threads
# (cmake/Linker.cmake).  On 2026-10-17, a relink of the tree ran 128 links at
# one time on a host of 224 logical cores, and it took 0.298 s with mold at 4
# threads.  Half the cores is the fraction nearest to that count.  On
# 2026-10-16, the sum of the 128 largest link peaks of the Debug build was
# 3.6 GB, so the memory does not limit the depth.
#
# Ninja runs at most the depth of a pool of its edges at one time, also with a
# larger -j.  The rules compile-jobs and link-jobs
# (utils/assay/guard/engines/buildsys/job_pools.py) refuse a build.ninja with
# an edge that is not in its pool.
#
# A target reads CMAKE_JOB_POOL_COMPILE and CMAKE_JOB_POOL_LINK when it is
# created, so this file comes before the first target.  The file of the quarantine
# plugin includes it too, for the jobs of the plugin build, and a scratch project
# of a self-test can include that file.

include_guard(GLOBAL)

cmake_host_system_information(RESULT CRUCIBLE_COMPILE_POOL_DEPTH QUERY NUMBER_OF_LOGICAL_CORES)
if(NOT CRUCIBLE_COMPILE_POOL_DEPTH MATCHES "^[1-9][0-9]*$")
  set(CRUCIBLE_COMPILE_POOL_DEPTH 1)
endif()
math(EXPR CRUCIBLE_LINK_POOL_DEPTH "${CRUCIBLE_COMPILE_POOL_DEPTH} / 2")
if(CRUCIBLE_LINK_POOL_DEPTH LESS 1)
  set(CRUCIBLE_LINK_POOL_DEPTH 1)
endif()
set(CRUCIBLE_COMPILE_POOL compile)
set(CRUCIBLE_LINK_POOL link)
set_property(GLOBAL PROPERTY JOB_POOLS
  "${CRUCIBLE_COMPILE_POOL}=${CRUCIBLE_COMPILE_POOL_DEPTH}" "${CRUCIBLE_LINK_POOL}=${CRUCIBLE_LINK_POOL_DEPTH}")
set(CMAKE_JOB_POOL_COMPILE "${CRUCIBLE_COMPILE_POOL}")
set(CMAKE_JOB_POOL_LINK "${CRUCIBLE_LINK_POOL}")
