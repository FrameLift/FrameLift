if (NOT DEFINED CASE OR NOT DEFINED FRAMELIFT_ROOT OR NOT DEFINED TEST_ROOT)
    message(FATAL_ERROR "CASE, FRAMELIFT_ROOT, and TEST_ROOT are required")
endif ()

file(REMOVE_RECURSE "${TEST_ROOT}")
file(MAKE_DIRECTORY "${TEST_ROOT}")

function(write_module file_name id enabled provides requires_modules requires_features optional_features)
    string(REPLACE ";" "\", \"" provides_json "${provides}")
    string(REPLACE ";" "\", \"" requires_modules_json "${requires_modules}")
    string(REPLACE ";" "\", \"" requires_features_json "${requires_features}")
    string(REPLACE ";" "\", \"" optional_features_json "${optional_features}")
    foreach (name IN ITEMS provides_json requires_modules_json requires_features_json optional_features_json)
        if (${name})
            set(${name} "\"${${name}}\"")
        endif ()
    endforeach ()
    file(WRITE "${TEST_ROOT}/${file_name}.Module.json" "{
  \"fileVersion\": 1,
  \"id\": \"${id}\",
  \"name\": \"${id}\",
  \"enabled\": ${enabled},
  \"provides\": { \"features\": [${provides_json}] },
  \"requires\": { \"modules\": [${requires_modules_json}], \"features\": [${requires_features_json}] },
  \"optional\": { \"modules\": [], \"features\": [${optional_features_json}] },
  \"platforms\": []
}
")
endfunction()

if (CASE STREQUAL "valid")
    write_module(provider framelift.provider true "host.feature" "" "" "")
    write_module(consumer framelift.consumer true "consumer.feature" "framelift.provider" "host.feature" "")
elseif (CASE STREQUAL "missing_required_feature")
    write_module(consumer framelift.consumer true "consumer.feature" "" "missing.feature" "")
elseif (CASE STREQUAL "duplicate_provider")
    write_module(first framelift.first true "shared.feature" "" "" "")
    write_module(second framelift.second true "shared.feature" "" "" "")
elseif (CASE STREQUAL "disabled_required_provider")
    write_module(provider framelift.provider false "host.feature" "" "" "")
    write_module(consumer framelift.consumer true "consumer.feature" "" "host.feature" "")
elseif (CASE STREQUAL "required_cycle")
    write_module(first framelift.first true "first.feature" "framelift.second" "" "")
    write_module(second framelift.second true "second.feature" "framelift.first" "" "")
elseif (CASE STREQUAL "optional_disabled_provider")
    write_module(provider framelift.provider false "optional.feature" "" "" "")
    write_module(consumer framelift.consumer true "consumer.feature" "" "" "optional.feature")
else ()
    message(FATAL_ERROR "Unknown CASE '${CASE}'")
endif ()

include("${FRAMELIFT_ROOT}/cmake/FrameLiftBuiltinModules.cmake")
framelift_load_builtin_modules("${TEST_ROOT}")
