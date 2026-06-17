simdb_test(ArgosSmoke main.cpp)

add_custom_target(ArgosSmoke_10k
    COMMAND $<TARGET_FILE:ArgosSmoke> 10000
    WORKING_DIRECTORY $<TARGET_FILE_DIR:ArgosSmoke>
    DEPENDS ArgosSmoke
    VERBATIM)
