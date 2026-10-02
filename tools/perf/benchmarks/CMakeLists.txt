add_executable(axyne_perf_benchmark
    axyne_perf_benchmark.c
)

target_include_directories(axyne_perf_benchmark PRIVATE
    ${PROJECT_SOURCE_DIR}/include
)
target_link_libraries(axyne_perf_benchmark PRIVATE axyne_core)

if(WIN32)
    target_link_libraries(axyne_perf_benchmark PRIVATE psapi)
elseif(APPLE)
    target_link_libraries(axyne_perf_benchmark PRIVATE proc)
endif()

if(MSVC)
    target_compile_options(axyne_perf_benchmark PRIVATE /W4 /permissive-)
else()
    target_compile_options(axyne_perf_benchmark PRIVATE -Wall -Wextra -Wpedantic)
endif()

if(BUILD_TESTING)
    add_test(
        NAME axyne-performance-budget
        COMMAND $<TARGET_FILE:axyne_perf_benchmark>
                --headless
                --iterations 3
                --warmup 1
                --startup-budget-ms 1000
                --memory-budget-mb 100
    )
    set_tests_properties(axyne-performance-budget PROPERTIES TIMEOUT 15)
endif()
