# Relatively quick test for simdb_regress (30 seconds)
simdb_test(ArgosSmoke main.cpp)

# Exhaustive test for github checks (4 minutes)
add_custom_target(ArgosSmoke_10k
    COMMAND $<TARGET_FILE:ArgosSmoke> 10000
    WORKING_DIRECTORY $<TARGET_FILE_DIR:ArgosSmoke>
    DEPENDS ArgosSmoke
    VERBATIM)
