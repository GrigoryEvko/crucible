# The test of the pair rule in cmake/ContractSemantic.cmake.  ctest runs it as:
#   cmake -P cmake/ContractSemanticSelfTest.cmake
#
# The rule must accept a scope with the two items or with neither.  It must
# reject each scope that has one item without the other, in each form that a
# build can give the item.
cmake_minimum_required(VERSION 3.25)
include("${CMAKE_CURRENT_LIST_DIR}/ContractSemantic.cmake")

set(failures "")

# Makes the rule read ITEMS, and records a failure when its verdict is not
# the one that EXPECT_ERROR names.
function(expect name expect_error items)
  crucible_contract_ignore_pair_error(error "${items}")
  if(expect_error AND NOT error)
    set(failures ${failures} "${name}: the rule accepts a scope that it must reject" PARENT_SCOPE)
  elseif(NOT expect_error AND error)
    set(failures ${failures} "${name}: the rule rejects a scope that it must accept (${error})" PARENT_SCOPE)
  endif()
endfunction()

expect("the full list" FALSE "${CRUCIBLE_CONTRACT_IGNORE_OPTIONS}")
expect("the full list among other options" FALSE "-O3;${CRUCIBLE_CONTRACT_IGNORE_OPTIONS};-Wall")
expect("the flag as an option, the define as a definition" FALSE
  "-fcontract-evaluation-semantic=ignore;CRUCIBLE_CONTRACT_SEMANTIC_IGNORE=1")
expect("no item" FALSE "-O3;-fcontract-evaluation-semantic=observe;NDEBUG")
expect("an empty scope" FALSE "")
expect("the flag alone" TRUE "-O3;-fcontract-evaluation-semantic=ignore")
expect("the define alone as an option" TRUE "-DCRUCIBLE_CONTRACT_SEMANTIC_IGNORE=1")
expect("the define alone as a definition" TRUE "CRUCIBLE_CONTRACT_SEMANTIC_IGNORE=1")
expect("the define with the observe semantic" TRUE
  "-fcontract-evaluation-semantic=observe;-DCRUCIBLE_CONTRACT_SEMANTIC_IGNORE=1")
expect("the flag inside a generator expression" TRUE
  "$<$<CONFIG:Release>:-fcontract-evaluation-semantic=ignore>")

# The note adds exactly one line for each split scope and nothing for a
# scope that keeps the pair.
set(found "")
crucible_contract_ignore_note(found "the split scope" "-fcontract-evaluation-semantic=ignore")
crucible_contract_ignore_note(found "the whole scope" "${CRUCIBLE_CONTRACT_IGNORE_OPTIONS}")
list(LENGTH found found_count)
if(NOT found_count EQUAL 1 OR NOT found MATCHES "^the split scope has ")
  list(APPEND failures "the note gives [${found}] for one split scope and one whole scope")
endif()

if(failures)
  list(JOIN failures "\n  " text)
  message(FATAL_ERROR "ContractSemanticSelfTest:\n  ${text}")
endif()
message(STATUS "ContractSemanticSelfTest: the pair rule gives the correct verdict for each case")
