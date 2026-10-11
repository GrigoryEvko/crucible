# The Ninja pools of the compiles and the links.
#
# The error threshold of the rule compile-jobs of utils/registry.txt is the
# number of compile jobs at one time, and the error threshold of the rule
# link-jobs is the number of link jobs at one time.  The registry is the one
# home of the two counts.  This file gives Ninja two pools with these depths:
#
#   compile  each compile of each target, and each custom command that compiles
#            an object (JOB_POOL ${CRUCIBLE_COMPILE_POOL}, as in
#            cmake/CrucibleBpf.cmake)
#   link     each link and each archive of each target
#
# Ninja runs at most the depth of a pool of its edges at one time, also with a
# larger -j.  `cmake --build build` with no -j then runs no more compiles and
# links than the registry gives.  The rules compile-jobs and link-jobs
# (utils/assay/guard/engines/buildsys/job_pools.py) refuse a build.ninja with
# an edge that is not in its pool, or with a depth that is not the registry
# count.
#
# The cache variables CRUCIBLE_COMPILE_JOBS and CRUCIBLE_LINK_JOBS are empty by
# default, and an empty variable takes the count of the registry at each
# configure.  An integer gives the pool that depth, and the two rules then
# refuse the build.  Raise a count in the registry, with a measurement of the
# memory of the build in the commit.
#
# A target reads CMAKE_JOB_POOL_COMPILE and CMAKE_JOB_POOL_LINK when it is
# created, so this file comes before the first target.  The file of the quarantine
# plugin includes it too, for the jobs of the plugin build and of the plugin
# witness, and a scratch project of a self-test can include that file.

include_guard(GLOBAL)
include("${CMAKE_CURRENT_LIST_DIR}/Guards.cmake")

set(CRUCIBLE_COMPILE_JOBS "" CACHE STRING
  "The compile jobs at one time, the depth of the Ninja pool compile.  Empty takes the rule compile-jobs of utils/registry.txt")
set(CRUCIBLE_LINK_JOBS "" CACHE STRING
  "The link jobs at one time, the depth of the Ninja pool link.  Empty takes the rule link-jobs of utils/registry.txt")

# crucible_job_pool_depth(<rule> <cache-variable> <out-var>)
#
# Set <out-var> to the value of <cache-variable>, or to the error threshold of <rule> when the variable is empty.  A
# value that is not an integer above 0 stops the configure.
function(crucible_job_pool_depth rule variable out_var)
  set(depth "${${variable}}")
  if(depth STREQUAL "")
    crucible_rule_limit(${rule} depth)
  elseif(NOT depth MATCHES "^[1-9][0-9]*$")
    message(FATAL_ERROR "${variable} is '${depth}'.  Give an integer above 0, or leave it empty for the count of "
                        "the rule ${rule} of utils/registry.txt.")
  endif()
  set(${out_var} "${depth}" PARENT_SCOPE)
endfunction()

crucible_job_pool_depth(compile-jobs CRUCIBLE_COMPILE_JOBS CRUCIBLE_COMPILE_POOL_DEPTH)
crucible_job_pool_depth(link-jobs CRUCIBLE_LINK_JOBS CRUCIBLE_LINK_POOL_DEPTH)
set(CRUCIBLE_COMPILE_POOL compile)
set(CRUCIBLE_LINK_POOL link)
set_property(GLOBAL PROPERTY JOB_POOLS
  "${CRUCIBLE_COMPILE_POOL}=${CRUCIBLE_COMPILE_POOL_DEPTH}" "${CRUCIBLE_LINK_POOL}=${CRUCIBLE_LINK_POOL_DEPTH}")
set(CMAKE_JOB_POOL_COMPILE "${CRUCIBLE_COMPILE_POOL}")
set(CMAKE_JOB_POOL_LINK "${CRUCIBLE_LINK_POOL}")
