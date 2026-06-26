project(simdb_examples)

# See examples/README.md for details about what SimDB pipeline feature(s)
# are demonstrated in each of these examples.

add_subdirectory(SimplePipeline)
add_subdirectory(ConcurrentApps)
add_subdirectory(DatabaseWatchdog)
add_subdirectory(PipelineSnoopers)
add_subdirectory(AppFactory)

# TODO cnyce: fix sporadic failures
#add_subdirectory(MultiPortStages)
