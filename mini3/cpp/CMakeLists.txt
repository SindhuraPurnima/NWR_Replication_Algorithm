cmake_minimum_required(VERSION 3.10)
project(mini3)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(Protobuf REQUIRED)
find_package(gRPC REQUIRED)
find_package(Threads REQUIRED)

# Add proto subdirectory
add_subdirectory(proto)

# Include directories
include_directories(
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${CMAKE_CURRENT_BINARY_DIR}/proto
)

# Source files
set(SOURCES
    src/server.cpp
    src/client.cpp
    src/ReplicationManager.cpp
    src/MetricsCollector.cpp
)

# Add executable
add_executable(server src/server.cpp)
add_executable(client src/client.cpp)

# Link libraries
target_link_libraries(server
    mini2_proto
    protobuf::libprotobuf
    gRPC::grpc++
    gRPC::grpc
    Threads::Threads
)

target_link_libraries(client
    mini2_proto
    protobuf::libprotobuf
    gRPC::grpc++
    gRPC::grpc
    Threads::Threads
) 