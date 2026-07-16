if (NOT DEFINED TEST_EXECUTABLE OR NOT DEFINED TEST_WORKING_DIRECTORY)
    message(FATAL_ERROR "TEST_EXECUTABLE and TEST_WORKING_DIRECTORY are required")
endif ()

execute_process(
        COMMAND "${CMAKE_COMMAND}" -E env ${TEST_ENVIRONMENT} "${TEST_EXECUTABLE}"
        WORKING_DIRECTORY "${TEST_WORKING_DIRECTORY}"
        RESULT_VARIABLE result
        OUTPUT_VARIABLE stdout
        ERROR_VARIABLE stderr
        TIMEOUT 25)
set(output "${stdout}\n${stderr}")

if (NOT result EQUAL 0)
    message(FATAL_ERROR "FrameLift launch failed (${result}):\n${output}")
endif ()

foreach (pattern IN LISTS REQUIRED_REGEXES)
    if (NOT output MATCHES "${pattern}")
        message(FATAL_ERROR "Required launch output not found: ${pattern}\n${output}")
    endif ()
endforeach ()

foreach (pattern IN LISTS FORBIDDEN_REGEXES)
    if (output MATCHES "${pattern}")
        message(FATAL_ERROR "Forbidden launch output found: ${pattern}\n${output}")
    endif ()
endforeach ()

if (DEFINED COUNT_REGEX AND DEFINED EXPECTED_COUNT)
    string(REGEX MATCHALL "${COUNT_REGEX}" matches "${output}")
    list(LENGTH matches actual_count)
    if (NOT actual_count EQUAL EXPECTED_COUNT)
        message(FATAL_ERROR
                "Expected ${EXPECTED_COUNT} matches for ${COUNT_REGEX}, got ${actual_count}\n${output}")
    endif ()
endif ()

message(STATUS "FrameLift launch checks passed")
