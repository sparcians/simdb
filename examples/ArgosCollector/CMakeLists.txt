simdb_test(ArgosCollector main.cpp)

set(ARGOS_UI_SMOKE_DEFAULT 0)
find_package(Python3 COMPONENTS Interpreter QUIET)
if(Python3_Interpreter_FOUND)
    execute_process(
        COMMAND ${Python3_EXECUTABLE} -c "import wx"
        RESULT_VARIABLE _argos_wx_import_rc
        OUTPUT_QUIET
        ERROR_QUIET
    )
    if(_argos_wx_import_rc EQUAL 0)
        set(ARGOS_UI_SMOKE_DEFAULT 1)
    endif()
endif()

target_compile_definitions(ArgosCollector PRIVATE ARGOS_UI_SMOKE_DEFAULT=${ARGOS_UI_SMOKE_DEFAULT})

set(_ArgosCollector_post_build
    COMMAND ${CMAKE_COMMAND} -E rm -f $<TARGET_FILE_DIR:ArgosCollector>/dump.py
    COMMAND ${CMAKE_COMMAND} -E create_symlink
            ${CMAKE_CURRENT_SOURCE_DIR}/dump.py
            $<TARGET_FILE_DIR:ArgosCollector>/dump.py
    COMMAND ${CMAKE_COMMAND} -E rm -f $<TARGET_FILE_DIR:ArgosCollector>/trace_compare.py
    COMMAND ${CMAKE_COMMAND} -E create_symlink
            ${CMAKE_CURRENT_SOURCE_DIR}/trace_compare.py
            $<TARGET_FILE_DIR:ArgosCollector>/trace_compare.py
    COMMAND ${CMAKE_COMMAND} -E rm -f $<TARGET_FILE_DIR:ArgosCollector>/value_compare.py
    COMMAND ${CMAKE_COMMAND} -E create_symlink
            ${CMAKE_CURRENT_SOURCE_DIR}/value_compare.py
            $<TARGET_FILE_DIR:ArgosCollector>/value_compare.py
    COMMAND ${CMAKE_COMMAND} -E rm -f $<TARGET_FILE_DIR:ArgosCollector>/trace_format.py
    COMMAND ${CMAKE_COMMAND} -E create_symlink
            ${CMAKE_CURRENT_SOURCE_DIR}/trace_format.py
            $<TARGET_FILE_DIR:ArgosCollector>/trace_format.py
    COMMAND ${CMAKE_COMMAND} -E rm -f $<TARGET_FILE_DIR:ArgosCollector>/trace_db_common.py
    COMMAND ${CMAKE_COMMAND} -E create_symlink
            ${CMAKE_CURRENT_SOURCE_DIR}/trace_db_common.py
            $<TARGET_FILE_DIR:ArgosCollector>/trace_db_common.py
    COMMAND ${CMAKE_COMMAND} -E rm -f $<TARGET_FILE_DIR:ArgosCollector>/collection_trace_compare.py
    COMMAND ${CMAKE_COMMAND} -E create_symlink
            ${CMAKE_CURRENT_SOURCE_DIR}/collection_trace_compare.py
            $<TARGET_FILE_DIR:ArgosCollector>/collection_trace_compare.py
    COMMAND ${CMAKE_COMMAND} -E rm -f $<TARGET_FILE_DIR:ArgosCollector>/collection_replayers.py
    COMMAND ${CMAKE_COMMAND} -E create_symlink
            ${CMAKE_CURRENT_SOURCE_DIR}/collection_replayers.py
            $<TARGET_FILE_DIR:ArgosCollector>/collection_replayers.py
    COMMAND ${CMAKE_COMMAND} -E rm -f $<TARGET_FILE_DIR:ArgosCollector>/ui_smoke.py
    COMMAND ${CMAKE_COMMAND} -E create_symlink
            ${CMAKE_CURRENT_SOURCE_DIR}/ui_smoke.py
            $<TARGET_FILE_DIR:ArgosCollector>/ui_smoke.py)

add_custom_command(TARGET ArgosCollector POST_BUILD ${_ArgosCollector_post_build}
                   VERBATIM)

set_tests_properties(ArgosCollector_test PROPERTIES WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR})
