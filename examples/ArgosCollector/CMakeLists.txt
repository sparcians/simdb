simdb_test(ArgosCollector main.cpp)

file(GLOB ArgosCollector_GOLDEN_OUTS CONFIGURE_DEPENDS
     "${CMAKE_CURRENT_SOURCE_DIR}/*.golden.out")

set(_ArgosCollector_post_build
    COMMAND ${CMAKE_COMMAND} -E rm -f $<TARGET_FILE_DIR:ArgosCollector>/dump.py
    COMMAND ${CMAKE_COMMAND} -E create_symlink
            ${CMAKE_CURRENT_SOURCE_DIR}/dump.py
            $<TARGET_FILE_DIR:ArgosCollector>/dump.py
    COMMAND ${CMAKE_COMMAND} -E rm -f $<TARGET_FILE_DIR:ArgosCollector>/collection_replayers.py
    COMMAND ${CMAKE_COMMAND} -E create_symlink
            ${CMAKE_CURRENT_SOURCE_DIR}/collection_replayers.py
            $<TARGET_FILE_DIR:ArgosCollector>/collection_replayers.py)
foreach(_golden IN LISTS ArgosCollector_GOLDEN_OUTS)
    get_filename_component(_golden_name "${_golden}" NAME)
    list(APPEND _ArgosCollector_post_build
         COMMAND ${CMAKE_COMMAND} -E rm -f
                 $<TARGET_FILE_DIR:ArgosCollector>/${_golden_name}
         COMMAND ${CMAKE_COMMAND} -E create_symlink "${_golden}"
                 $<TARGET_FILE_DIR:ArgosCollector>/${_golden_name})
endforeach()

add_custom_command(TARGET ArgosCollector POST_BUILD ${_ArgosCollector_post_build}
                   VERBATIM)

set_tests_properties(ArgosCollector_test PROPERTIES WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR})
