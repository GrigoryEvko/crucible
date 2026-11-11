# The Ninja pools of the compiles and the links.
#
# The host gives the depths of the two pools at each configure.  `loop jobs`
# prints DEFAULT_JOBS of utils/assay/core/proc.py, the number of CPUs that the
# process can use.  The package uses the same count for its parallel work, so
# one holder gives the job count of the host.  This file gives Ninja two pools:
#
#   compile  each compile of each target, and each custom command that compiles
#            an object (JOB_POOL ${CRUCIBLE_COMPILE_POOL}, as in
#            cmake/CrucibleBpf.cmake).  The depth is DEFAULT_JOBS
#   link     each link and each archive of each target.  The depth is half of
#            DEFAULT_JOBS, and at least 1
#
# Each link runs mold with the threads of the rule build-graph
# (cmake/Linker.cmake).  On 2026-10-17, a relink of the tree ran 128 links at
# one time on a host of 224 CPUs, and it took 0.298 s with mold at 4 threads.
# Half the cores is the fraction nearest to that count.  On
# 2026-10-16, the sum of the 128 largest link peaks of the Debug build was
# 3.6 GB, so the memory does not limit the depth.  A heavy step of a gate
# decreases its jobs to the free memory when it runs (assay.loop.heavy).
#
# Ninja runs at most the depth of a pool of its edges at one time, also with a
# larger -j.  The rule build-graph
# (utils/assay/guard/engines/buildsys/build_graph.py) refuses a build.ninja
# with an edge that is not in its pool.
#
# A target reads CMAKE_JOB_POOL_COMPILE and CMAKE_JOB_POOL_LINK when it is
# created, so this file comes before the first target.  The file of the quarantine
# plugin includes it too, for the jobs of the plugin build, and a scratch project
# of a self-test can include that file.

include_guard(GLOBAL)

find_program(CRUCIBLE_PYTHON3 NAMES python3 DOC "The Python interpreter of the build launcher")
if(NOT CRUCIBLE_PYTHON3)
  message(FATAL_ERROR "Crucible pools: no python3 is in PATH, and the configure asks `loop jobs` for the job count of "
                      "the host.  Install Python 3.14, or give -DCRUCIBLE_PYTHON3=<path>.")
endif()
execute_process(
  COMMAND "${CRUCIBLE_PYTHON3}" -S "${CMAKE_CURRENT_LIST_DIR}/../utils/bin/loop" jobs
  RESULT_VARIABLE _crucible_jobs_status
  OUTPUT_VARIABLE CRUCIBLE_COMPILE_POOL_DEPTH
  ERROR_VARIABLE _crucible_jobs_error
  OUTPUT_STRIP_TRAILING_WHITESPACE
  ERROR_STRIP_TRAILING_WHITESPACE)
if(NOT _crucible_jobs_status EQUAL 0 OR NOT CRUCIBLE_COMPILE_POOL_DEPTH MATCHES "^[1-9][0-9]*$")
  message(FATAL_ERROR "Crucible pools: `loop jobs` exits ${_crucible_jobs_status} and gives "
                      "'${CRUCIBLE_COMPILE_POOL_DEPTH}', and the depth of a pool is an integer above 0:\n"
                      "  ${_crucible_jobs_error}\nRun the configure with Python 3.14.")
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
