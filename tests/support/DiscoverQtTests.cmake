if (NOT DEFINED TEST_TARGET OR NOT DEFINED TEST_WORKING_DIR OR NOT DEFINED TEST_INCLUDE_FILE)
    message(FATAL_ERROR "DiscoverQtTests.cmake requires TEST_TARGET, TEST_WORKING_DIR, and TEST_INCLUDE_FILE")
endif ()

execute_process(
        COMMAND "${TEST_TARGET}" --framelift-list-tests
        WORKING_DIRECTORY "${TEST_WORKING_DIR}"
        RESULT_VARIABLE _result
        OUTPUT_VARIABLE _tests
        ERROR_VARIABLE _error
)
if (NOT _result EQUAL 0)
    message(FATAL_ERROR "Failed to discover QtTest cases for ${TEST_TARGET}:\n${_error}")
endif ()

file(WRITE "${TEST_INCLUDE_FILE}" "")
string(REPLACE "\n" ";" _tests "${_tests}")
foreach (_test IN LISTS _tests)
    if (_test STREQUAL "")
        continue()
    endif ()
    file(APPEND "${TEST_INCLUDE_FILE}"
            "add_test([=[${_test}]=] [=[${TEST_TARGET}]=] --framelift-test [=[${_test}]=])\n"
            "set_tests_properties([=[${_test}]=] PROPERTIES WORKING_DIRECTORY [=[${TEST_WORKING_DIR}]=])\n")
endforeach ()
