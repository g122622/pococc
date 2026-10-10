cmake_minimum_required(VERSION 3.20)
project(pococc VERSION 0.1.0 LANGUAGES CXX)
if(NOT CMAKE_SYSTEM_NAME STREQUAL "Linux")
    message(FATAL_ERROR "The prototype supports Linux. Use WSL on Windows.")
endif()
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
include(CTest)
add_library(pococc_core
    src/pococc/system/ProcessRunner.cpp
    src/pococc/compiler/arguments/ResponseFile.cpp
    src/pococc/compiler/arguments/InvocationAnalyzer.cpp
    src/pococc/cluster/HostList.cpp
    src/pococc/telemetry/BuildJournal.cpp
    src/pococc/compiler/CompilationManager.cpp
    src/pococc/application/Application.cpp
)
target_include_directories(pococc_core PUBLIC src)
target_compile_options(pococc_core PRIVATE -Wall -Wextra -Wpedantic -Werror)
foreach(target IN ITEMS pococc pococc-cc)
    add_executable(${target} src/main.cpp)
    target_link_libraries(${target} PRIVATE pococc_core)
    target_compile_options(${target} PRIVATE -Wall -Wextra -Wpedantic -Werror)
endforeach()
if(BUILD_TESTING)
    add_executable(pococc_tests tests/unit/test_invocation.cpp)
    target_link_libraries(pococc_tests PRIVATE pococc_core)
    target_compile_options(pococc_tests PRIVATE -Wall -Wextra -Wpedantic -Werror)
    add_test(NAME invocation COMMAND pococc_tests)
    find_package(Python3 COMPONENTS Interpreter REQUIRED)
    add_test(NAME integration
        COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tests/integration/test_distributed.py
            --pococc $<TARGET_FILE:pococc> --launcher $<TARGET_FILE:pococc-cc>)
    set_tests_properties(integration PROPERTIES TIMEOUT 120)
    add_test(NAME bootstrap_evidence
        COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tests/ci/test_bootstrap.py)
endif()
install(TARGETS pococc pococc-cc RUNTIME DESTINATION bin)
