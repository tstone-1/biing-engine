# A signal/crash is not a passing negative test: require the normal failure code and the precise diagnostic.
foreach(pair IN ITEMS "kind|field kind" "width|field width" "bounds|field bounds" "offset|field bounds"
    "length|record length" "null|record length" "staff-negative|staff index" "staff-past|staff index"
    "slot-negative|slot index" "slot-past|slot index" "hospital-negative|slot index" "hospital-past|slot index"
    "staff-missing|staff storage" "slot-missing|slot storage" "staff-short|staff storage" "staff-storage-past|staff storage"
    "slot-short|slot storage" "slot-storage-past|slot storage" "link|staff-slot mismatch")
  string(REPLACE "|" ";" parts "${pair}")
  list(GET parts 0 case)
  list(GET parts 1 reason)
  execute_process(COMMAND "${DRIVER}" "${case}" RESULT_VARIABLE result OUTPUT_VARIABLE output ERROR_VARIABLE error TIMEOUT 10)
  string(FIND "${error}" "[ERROR] record check: ${reason} (" found)
  if(NOT "${result}" STREQUAL "1" OR found LESS 0)
    message(FATAL_ERROR "${case}: expected exit 1 and ${reason}; got ${result}\n${output}\n${error}")
  endif()
  message(STATUS "[PASS] ${case}: ${reason}")
endforeach()
