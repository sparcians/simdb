simdb_test(ArgosCollector main.cpp)

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
    COMMAND ${CMAKE_COMMAND} -E rm -f $<TARGET_FILE_DIR:ArgosCollector>/collection_replayers.py
    COMMAND ${CMAKE_COMMAND} -E create_symlink
            ${CMAKE_CURRENT_SOURCE_DIR}/collection_replayers.py
            $<TARGET_FILE_DIR:ArgosCollector>/collection_replayers.py)

add_custom_command(TARGET ArgosCollector POST_BUILD ${_ArgosCollector_post_build}
                   VERBATIM)

set_tests_properties(ArgosCollector_test PROPERTIES WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR})
