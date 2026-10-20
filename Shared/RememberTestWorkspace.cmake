# Build directories can be arbitrarily deep. Recovery fixtures deliberately
# nest pending entries, revision UUIDs and atomic temporary filenames. Use a
# separate short working directory without changing those storage scenarios.
function(remember_use_test_workspace)
    set(REMEMBER_TEST_WORKING_DIRECTORY "" CACHE PATH "Optional short directory for isolated test fixtures")
    if(REMEMBER_TEST_WORKING_DIRECTORY)
        get_filename_component(workspace "${REMEMBER_TEST_WORKING_DIRECTORY}" ABSOLUTE)
    else()
        string(MD5 workspace_key "${CMAKE_CURRENT_BINARY_DIR}")
        string(SUBSTRING "${workspace_key}" 0 12 workspace_key)
        set(workspace "${REMEMBER_SOURCE_ROOT}/out/t/${workspace_key}")
    endif()
    file(MAKE_DIRECTORY "${workspace}")
    get_property(tests DIRECTORY PROPERTY TESTS)
    set_tests_properties(${tests} PROPERTIES WORKING_DIRECTORY "${workspace}")
    file(WRITE "${CMAKE_CURRENT_BINARY_DIR}/generated/test-workspace.txt" "${workspace}")
endfunction()
