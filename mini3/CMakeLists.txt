cmake_minimum_required(VERSION 3.10)

project(mini3)

# Set C++ standard
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Set architecture
if(APPLE)
    set(CMAKE_OSX_ARCHITECTURES "arm64" CACHE STRING "Build architectures for OSX" FORCE)
    set(CMAKE_SYSTEM_PROCESSOR "arm64")
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -arch arm64")
    set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -arch arm64")
endif()

# Find required packages
find_package(Threads REQUIRED)

# Set Homebrew paths
set(HOMEBREW_PREFIX "/opt/homebrew")
set(HOMEBREW_INCLUDE_DIR "${HOMEBREW_PREFIX}/include")
set(HOMEBREW_LIB_DIR "${HOMEBREW_PREFIX}/lib")

# Add include directories
include_directories(
    ${HOMEBREW_INCLUDE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/cpp/include
    ${CMAKE_CURRENT_SOURCE_DIR}/proto
)

# Find gRPC executables
find_program(GRPC_CPP_PLUGIN_EXECUTABLE grpc_cpp_plugin)

# Proto files
set(PROTO_FILES
    ${CMAKE_CURRENT_SOURCE_DIR}/proto/mini2.proto
)

# Generated files paths
set(PROTO_GEN_DIR ${CMAKE_CURRENT_SOURCE_DIR}/proto)
set(PROTO_GEN_SRCS ${PROTO_GEN_DIR}/mini2.pb.cc)
set(PROTO_GEN_HDRS ${PROTO_GEN_DIR}/mini2.pb.h)
set(PROTO_GRPC_GEN_SRCS ${PROTO_GEN_DIR}/mini2.grpc.pb.cc)
set(PROTO_GRPC_GEN_HDRS ${PROTO_GEN_DIR}/mini2.grpc.pb.h)

# Create proto directory if it doesn't exist
file(MAKE_DIRECTORY ${PROTO_GEN_DIR})

# Generate protobuf files
add_custom_command(
    OUTPUT ${PROTO_GEN_SRCS} ${PROTO_GEN_HDRS} ${PROTO_GRPC_GEN_SRCS} ${PROTO_GRPC_GEN_HDRS}
    COMMAND protoc
    ARGS --grpc_out=${PROTO_GEN_DIR}
         --cpp_out=${PROTO_GEN_DIR}
         --plugin=protoc-gen-grpc=${GRPC_CPP_PLUGIN_EXECUTABLE}
         -I ${CMAKE_CURRENT_SOURCE_DIR}/proto
         ${PROTO_FILES}
    DEPENDS ${PROTO_FILES}
)

# Create library for generated files
add_library(proto STATIC
    ${PROTO_GEN_SRCS}
    ${PROTO_GRPC_GEN_SRCS}
)

# Set include directories for proto library
target_include_directories(proto PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/proto
)

# Server executable
add_executable(server
    cpp/src/server.cpp
    cpp/src/ReplicationManager.cpp
    cpp/src/SpatialAnalysis.cpp
    cpp/src/ReplicationAlgorithm.cpp
    cpp/src/MetricsCollector.cpp
)

# Client executable
add_executable(client
    cpp/src/client.cpp
)

# Link libraries with full paths
target_link_libraries(server
    proto
    ${HOMEBREW_LIB_DIR}/libgrpc++.1.71.0.dylib
    ${HOMEBREW_LIB_DIR}/libgrpc.46.0.0.dylib
    ${HOMEBREW_LIB_DIR}/libgrpc_unsecure.46.0.0.dylib
    ${HOMEBREW_LIB_DIR}/libgrpc_plugin_support.1.71.0.dylib
    ${HOMEBREW_LIB_DIR}/libgrpcpp_channelz.1.71.0.dylib
    ${HOMEBREW_LIB_DIR}/libgrpc++_reflection.1.71.0.dylib
    ${HOMEBREW_LIB_DIR}/libgpr.46.0.0.dylib
    ${HOMEBREW_LIB_DIR}/libprotobuf.dylib
    ${HOMEBREW_LIB_DIR}/libabsl_base.2407.0.0.dylib
    ${HOMEBREW_LIB_DIR}/libabsl_throw_delegate.2407.0.0.dylib
    ${HOMEBREW_LIB_DIR}/libabsl_raw_logging_internal.2407.0.0.dylib
    ${HOMEBREW_LIB_DIR}/libabsl_spinlock_wait.2407.0.0.dylib
    ${HOMEBREW_LIB_DIR}/libabsl_synchronization.2407.0.0.dylib
    ${HOMEBREW_LIB_DIR}/libabsl_time.2407.0.0.dylib
    ${HOMEBREW_LIB_DIR}/libabsl_strings.2407.0.0.dylib
    ${HOMEBREW_LIB_DIR}/libabsl_strings_internal.2407.0.0.dylib
    ${HOMEBREW_LIB_DIR}/libabsl_log_severity.2407.0.0.dylib
    ${HOMEBREW_LIB_DIR}/libabsl_cord.2407.0.0.dylib
    ${HOMEBREW_LIB_DIR}/libabsl_cord_internal.2407.0.0.dylib
    ${HOMEBREW_LIB_DIR}/libabsl_cordz_functions.2407.0.0.dylib
    ${HOMEBREW_LIB_DIR}/libabsl_cordz_info.2407.0.0.dylib
    ${HOMEBREW_LIB_DIR}/libabsl_cordz_handle.2407.0.0.dylib
    ${HOMEBREW_LIB_DIR}/libabsl_cordz_sample_token.2407.0.0.dylib
    ${HOMEBREW_LIB_DIR}/libabsl_status.2407.0.0.dylib
    ${HOMEBREW_LIB_DIR}/libabsl_statusor.2407.0.0.dylib
    ${HOMEBREW_LIB_DIR}/libabsl_str_format_internal.2407.0.0.dylib
    ${HOMEBREW_LIB_DIR}/libabsl_strerror.2407.0.0.dylib
    ${HOMEBREW_LIB_DIR}/libabsl_string_view.2407.0.0.dylib
    ${HOMEBREW_LIB_DIR}/libabsl_symbolize.2407.0.0.dylib
    ${HOMEBREW_LIB_DIR}/libabsl_malloc_internal.2407.0.0.dylib
    ${HOMEBREW_LIB_DIR}/libabsl_stacktrace.2407.0.0.dylib
    ${HOMEBREW_LIB_DIR}/libabsl_debugging_internal.2407.0.0.dylib
    ${HOMEBREW_LIB_DIR}/libabsl_demangle_internal.2407.0.0.dylib
    ${HOMEBREW_LIB_DIR}/libabsl_graphcycles_internal.2407.0.0.dylib
    ${HOMEBREW_LIB_DIR}/libabsl_kernel_timeout_internal.2407.0.0.dylib
    ${HOMEBREW_LIB_DIR}/libabsl_time_zone.2407.0.0.dylib
    ${HOMEBREW_LIB_DIR}/libabsl_civil_time.2407.0.0.dylib
    ${HOMEBREW_LIB_DIR}/libabsl_log_internal_check_op.2407.0.0.dylib
    ${HOMEBREW_LIB_DIR}/libabsl_log_internal_message.2407.0.0.dylib
    ${HOMEBREW_LIB_DIR}/libabsl_log_internal_nullguard.2407.0.0.dylib
    ${HOMEBREW_LIB_DIR}/libabsl_log_internal_format.2407.0.0.dylib
    ${HOMEBREW_LIB_DIR}/libabsl_log_internal_globals.2407.0.0.dylib
    ${HOMEBREW_LIB_DIR}/libabsl_log_internal_proto.2407.0.0.dylib
    ${HOMEBREW_LIB_DIR}/libabsl_log_internal_conditions.2407.0.0.dylib
    ${HOMEBREW_LIB_DIR}/libabsl_log_internal_log_sink_set.2407.0.0.dylib
    ${HOMEBREW_LIB_DIR}/libabsl_log_sink.2407.0.0.dylib
    ${HOMEBREW_LIB_DIR}/libabsl_log_entry.2407.0.0.dylib
    ${HOMEBREW_LIB_DIR}/libabsl_log_initialize.2407.0.0.dylib
    ${HOMEBREW_LIB_DIR}/libabsl_log_globals.2407.0.0.dylib
    ${HOMEBREW_LIB_DIR}/libabsl_log_flags.2407.0.0.dylib
    ${HOMEBREW_LIB_DIR}/libabsl_hash.2407.0.0.dylib
    ${HOMEBREW_LIB_DIR}/libabsl_city.2407.0.0.dylib
    ${HOMEBREW_LIB_DIR}/libabsl_low_level_hash.2407.0.0.dylib
    ${HOMEBREW_LIB_DIR}/libabsl_raw_hash_set.2407.0.0.dylib
    Threads::Threads
)

target_link_libraries(client
    proto
    ${HOMEBREW_LIB_DIR}/libgrpc++.1.71.0.dylib
    ${HOMEBREW_LIB_DIR}/libgrpc.46.0.0.dylib
    ${HOMEBREW_LIB_DIR}/libgrpc_unsecure.46.0.0.dylib
    ${HOMEBREW_LIB_DIR}/libgrpc_plugin_support.1.71.0.dylib
    ${HOMEBREW_LIB_DIR}/libgrpcpp_channelz.1.71.0.dylib
    ${HOMEBREW_LIB_DIR}/libgrpc++_reflection.1.71.0.dylib
    ${HOMEBREW_LIB_DIR}/libgpr.46.0.0.dylib
    ${HOMEBREW_LIB_DIR}/libprotobuf.dylib
    ${HOMEBREW_LIB_DIR}/libabsl_base.2407.0.0.dylib
    ${HOMEBREW_LIB_DIR}/libabsl_throw_delegate.2407.0.0.dylib
    ${HOMEBREW_LIB_DIR}/libabsl_raw_logging_internal.2407.0.0.dylib
    ${HOMEBREW_LIB_DIR}/libabsl_spinlock_wait.2407.0.0.dylib
    ${HOMEBREW_LIB_DIR}/libabsl_synchronization.2407.0.0.dylib
    ${HOMEBREW_LIB_DIR}/libabsl_time.2407.0.0.dylib
    ${HOMEBREW_LIB_DIR}/libabsl_strings.2407.0.0.dylib
    ${HOMEBREW_LIB_DIR}/libabsl_strings_internal.2407.0.0.dylib
    ${HOMEBREW_LIB_DIR}/libabsl_log_severity.2407.0.0.dylib
    ${HOMEBREW_LIB_DIR}/libabsl_cord.2407.0.0.dylib
    ${HOMEBREW_LIB_DIR}/libabsl_cord_internal.2407.0.0.dylib
    ${HOMEBREW_LIB_DIR}/libabsl_cordz_functions.2407.0.0.dylib
    ${HOMEBREW_LIB_DIR}/libabsl_cordz_info.2407.0.0.dylib
    ${HOMEBREW_LIB_DIR}/libabsl_cordz_handle.2407.0.0.dylib
    ${HOMEBREW_LIB_DIR}/libabsl_cordz_sample_token.2407.0.0.dylib
    ${HOMEBREW_LIB_DIR}/libabsl_status.2407.0.0.dylib
    ${HOMEBREW_LIB_DIR}/libabsl_statusor.2407.0.0.dylib
    ${HOMEBREW_LIB_DIR}/libabsl_str_format_internal.2407.0.0.dylib
    ${HOMEBREW_LIB_DIR}/libabsl_strerror.2407.0.0.dylib
    ${HOMEBREW_LIB_DIR}/libabsl_string_view.2407.0.0.dylib
    ${HOMEBREW_LIB_DIR}/libabsl_symbolize.2407.0.0.dylib
    ${HOMEBREW_LIB_DIR}/libabsl_malloc_internal.2407.0.0.dylib
    ${HOMEBREW_LIB_DIR}/libabsl_stacktrace.2407.0.0.dylib
    ${HOMEBREW_LIB_DIR}/libabsl_debugging_internal.2407.0.0.dylib
    ${HOMEBREW_LIB_DIR}/libabsl_demangle_internal.2407.0.0.dylib
    ${HOMEBREW_LIB_DIR}/libabsl_graphcycles_internal.2407.0.0.dylib
    ${HOMEBREW_LIB_DIR}/libabsl_kernel_timeout_internal.2407.0.0.dylib
    ${HOMEBREW_LIB_DIR}/libabsl_time_zone.2407.0.0.dylib
    ${HOMEBREW_LIB_DIR}/libabsl_civil_time.2407.0.0.dylib
    ${HOMEBREW_LIB_DIR}/libabsl_log_internal_check_op.2407.0.0.dylib
    ${HOMEBREW_LIB_DIR}/libabsl_log_internal_message.2407.0.0.dylib
    ${HOMEBREW_LIB_DIR}/libabsl_log_internal_nullguard.2407.0.0.dylib
    ${HOMEBREW_LIB_DIR}/libabsl_log_internal_format.2407.0.0.dylib
    ${HOMEBREW_LIB_DIR}/libabsl_log_internal_globals.2407.0.0.dylib
    ${HOMEBREW_LIB_DIR}/libabsl_log_internal_proto.2407.0.0.dylib
    ${HOMEBREW_LIB_DIR}/libabsl_log_internal_conditions.2407.0.0.dylib
    ${HOMEBREW_LIB_DIR}/libabsl_log_internal_log_sink_set.2407.0.0.dylib
    ${HOMEBREW_LIB_DIR}/libabsl_log_sink.2407.0.0.dylib
    ${HOMEBREW_LIB_DIR}/libabsl_log_entry.2407.0.0.dylib
    ${HOMEBREW_LIB_DIR}/libabsl_log_initialize.2407.0.0.dylib
    ${HOMEBREW_LIB_DIR}/libabsl_log_globals.2407.0.0.dylib
    ${HOMEBREW_LIB_DIR}/libabsl_log_flags.2407.0.0.dylib
    ${HOMEBREW_LIB_DIR}/libabsl_hash.2407.0.0.dylib
    ${HOMEBREW_LIB_DIR}/libabsl_city.2407.0.0.dylib
    ${HOMEBREW_LIB_DIR}/libabsl_low_level_hash.2407.0.0.dylib
    ${HOMEBREW_LIB_DIR}/libabsl_raw_hash_set.2407.0.0.dylib
    Threads::Threads
)

# Set include directories for targets
target_include_directories(server PRIVATE
    ${PROTO_GEN_DIR}
    ${HOMEBREW_INCLUDE_DIR}
)

target_include_directories(client PRIVATE
    ${PROTO_GEN_DIR}
    ${HOMEBREW_INCLUDE_DIR}
)

target_include_directories(proto PRIVATE
    ${PROTO_GEN_DIR}
    ${HOMEBREW_INCLUDE_DIR}
)
