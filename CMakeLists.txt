cmake_minimum_required(VERSION 3.10)
project(RaftingTrip VERSION 1.0)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(CMAKE_CXX_FLAGS_DEBUG "${CMAKE_CXX_FLAGS_DEBUG} -g -O0 -rdynamic")
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wall -Wextra")
#set(CMAKE_CXX_FLAGS_DEBUG "${CMAKE_CXX_FLAGS_DEBUG} -fsanitize=address -fno-omit-frame-pointer")
#set(CMAKE_EXE_LINKER_FLAGS_DEBUG "${CMAKE_EXE_LINKER_FLAGS_DEBUG} -fsanitize=address")

include(FetchContent)

FetchContent_Declare(
        googletest
        URL https://github.com/google/googletest/archive/03597a01ee50ed33e9dfd640b249b4be3799d395.zip
)

set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(googletest)

file(
        DOWNLOAD
        https://github.com/cpm-cmake/CPM.cmake/releases/download/v0.38.3/CPM.cmake
        ${CMAKE_CURRENT_BINARY_DIR}/cmake/CPM.cmake
        EXPECTED_HASH SHA256=cc155ce02e7945e7b8967ddfaff0b050e958a723ef7aad3766d368940cb15494
)
include(${CMAKE_CURRENT_BINARY_DIR}/cmake/CPM.cmake)

find_package(Threads REQUIRED)

CPMAddPackage("gh:gabime/spdlog#v1.15.2")
CPMAddPackage(
        NAME gflags
        GITHUB_REPOSITORY gflags/gflags
        VERSION 2.3.0
        OPTIONS "REGISTER_INSTALL_PREFIX OFF" "BUILD_SHARED_LIBS OFF"
)
CPMAddPackage(
        NAME flatbuffers
        GITHUB_REPOSITORY google/flatbuffers
        GIT_TAG v25.2.10 # Adjust version as needed
        OPTIONS
        "FLATBUFFERS_BUILD_TESTS OFF"
        "FLATBUFFERS_BUILD_FLATHASH OFF"
        "FLATBUFFERS_BUILD_FLATLIB ON"
        "FLATBUFFERS_BUILD_FLATC ON"  # This is important! Make sure it's ON
        "FLATBUFFERS_INSTALL OFF"
)

set(CMAKE_C_COMPILER clang)
set(CMAKE_CXX_COMPILER clang++)

######## Flatbuffers compilation #################
function(compile_flatbuffers_schema)
    set(options)
    set(oneValueArgs TARGET OUTPUT_DIR)
    set(multiValueArgs SCHEMAS INCLUDES DEPENDENCIES)
    cmake_parse_arguments(COMPILE_FLATBUFFERS "${options}" "${oneValueArgs}" "${multiValueArgs}" ${ARGN})

    # Create output directory
    file(MAKE_DIRECTORY ${COMPILE_FLATBUFFERS_OUTPUT_DIR})

    # Get path to flatc executable
    set(FLATC_EXECUTABLE $<TARGET_FILE:flatc>)

    # Compile schemas
    foreach(schema ${COMPILE_FLATBUFFERS_SCHEMAS})
        get_filename_component(filename ${schema} NAME_WE)
        set(output_file "${COMPILE_FLATBUFFERS_OUTPUT_DIR}/${filename}_generated.h")

        add_custom_command(
                OUTPUT ${output_file}
                COMMAND ${FLATC_EXECUTABLE}
                ARGS --cpp -o ${COMPILE_FLATBUFFERS_OUTPUT_DIR} ${COMPILE_FLATBUFFERS_INCLUDES} ${schema}
                DEPENDS ${schema} flatc ${COMPILE_FLATBUFFERS_DEPENDENCIES}
                COMMENT "Compiling FlatBuffers schema ${schema}"
                WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
        )
        list(APPEND output_files ${output_file})
    endforeach()

    # Create a custom target for the generated files
    add_custom_target(${COMPILE_FLATBUFFERS_TARGET} DEPENDS ${output_files})
endfunction()

# Make sure the path to your schema files is correct
set(SCHEMA_DIR "${CMAKE_CURRENT_SOURCE_DIR}/schema") # Adjust to match your project structure

# Create directory for generated headers
set(GENERATED_FLATBUFFERS_DIR "${CMAKE_CURRENT_BINARY_DIR}/generated")

# Compile your schema files
compile_flatbuffers_schema(
        TARGET generate_flatbuffers_console
        OUTPUT_DIR ${GENERATED_FLATBUFFERS_DIR}
        SCHEMAS "${SCHEMA_DIR}/console.fbs"
        DEPENDENCIES ""
)

compile_flatbuffers_schema(
        TARGET generate_flatbuffers_raft
        OUTPUT_DIR ${GENERATED_FLATBUFFERS_DIR}
        SCHEMAS "${SCHEMA_DIR}/raft.fbs"
        DEPENDENCIES ""
)

#########################################################################################
# Warmup executables
add_executable(socket_server src/warmup/socket_server.cpp)
add_executable(socket_client src/warmup/socket_client.cpp)
add_executable(echo_server src/warmup/echo_server.cpp)
add_executable(echo_client src/warmup/echo_client.cpp)
add_executable(counting_threading src/warmup/counting_threading.cpp)
add_executable(threaded_queue src/warmup/threaded_queue.cpp)
add_executable(mutable_data src/warmup/mutable_data.cpp)
add_executable(future src/warmup/future.cpp)
target_include_directories(echo_server PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_include_directories(echo_client PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(echo_client gflags)
##########################################################################################

add_executable(server src/key_value/server.cpp
        src/key_value/server.h)
target_include_directories(server PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(server spdlog::spdlog gflags)

enable_testing()

add_executable(keyvalue_test src/tests/keyvalue_test.cpp)
target_link_libraries(keyvalue_test GTest::gtest_main)
add_executable(traffic src/traffic/traffic_light.cpp)
target_include_directories(traffic PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(traffic GTest::gtest_main)

add_library(raft src/raft/raft_server.cpp src/raft/raft_server.h
        src/raft/raft_node.cpp
        src/raft/raft_node.h
        src/raft/raft_log.cpp
        src/raft/raft_log.h
        src/raft/raft_config.cpp
        src/raft/raft_config.h
)
add_dependencies(raft generate_flatbuffers_raft generate_flatbuffers_console)

target_include_directories(raft PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/include
        ${GENERATED_FLATBUFFERS_DIR}
        ${flatbuffers_SOURCE_DIR}/include
)
target_link_libraries(raft spdlog::spdlog GTest::gtest_main flatbuffers)

add_executable(raft_main
        src/raft/main.cpp
)
target_link_libraries(raft_main raft gflags::gflags)

add_executable(raft_console src/raft/raft_console.cpp)
target_link_libraries(raft_console gflags::gflags raft)
target_include_directories(raft_console PRIVATE
        ${GENERATED_FLATBUFFERS_DIR}
        ${flatbuffers_SOURCE_DIR}/include
)

add_executable(raft_log_test src/tests/raft_log_test.cpp)
target_link_libraries(raft_log_test raft)

add_executable(rpc_test src/tests/raft_test.cpp)
target_link_libraries(rpc_test raft)

include(GoogleTest)
gtest_discover_tests(keyvalue_test traffic raft_log_test rpc_test)
