cmake_minimum_required(VERSION 3.16)
project(ClientServerChatApp CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Set the prefix path to your installed directory.
set(CMAKE_PREFIX_PATH "/home/jesse/.local" CACHE STRING "Prefix path for gRPC and Protobuf" FORCE)
set(gRPC_DIR "/home/jesse/.local/lib/cmake/grpc" CACHE STRING "Path to gRPC config" FORCE)
set(Protobuf_DIR "/home/jesse/.local/lib/cmake/protobuf" CACHE STRING "Path to Protobuf config" FORCE)

find_package(gRPC REQUIRED)
find_package(Protobuf REQUIRED)

message(STATUS "Using gRPC ${gRPC_VERSION}")
message(STATUS "Using Protobuf ${Protobuf_VERSION}")

set(PROTO_FILE "${CMAKE_CURRENT_SOURCE_DIR}/chat.proto")

# Locate protoc and the grpc_cpp_plugin.
find_program(PROTOC_EXECUTABLE protoc REQUIRED)
find_program(GRPC_CPP_PLUGIN_EXECUTABLE grpc_cpp_plugin REQUIRED)

# Set output paths for generated files.
set(PROTO_SRCS "${CMAKE_CURRENT_BINARY_DIR}/chat.pb.cc")
set(PROTO_HDRS "${CMAKE_CURRENT_BINARY_DIR}/chat.pb.h")
set(GRPC_SRCS "${CMAKE_CURRENT_BINARY_DIR}/chat.grpc.pb.cc")
set(GRPC_HDRS "${CMAKE_CURRENT_BINARY_DIR}/chat.grpc.pb.h")

# Custom command to generate gRPC and Protobuf files.
add_custom_command(
  OUTPUT ${PROTO_SRCS} ${PROTO_HDRS} ${GRPC_SRCS} ${GRPC_HDRS}
  COMMAND ${PROTOC_EXECUTABLE}
          --grpc_out "${CMAKE_CURRENT_BINARY_DIR}"
          --cpp_out "${CMAKE_CURRENT_BINARY_DIR}"
          --plugin=protoc-gen-grpc=${GRPC_CPP_PLUGIN_EXECUTABLE}
          -I "${CMAKE_CURRENT_SOURCE_DIR}"
          "${CMAKE_CURRENT_SOURCE_DIR}/chat.proto"
  DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/chat.proto"
  COMMENT "Generating gRPC and protobuf files from chat.proto"
)

add_custom_target(proto_files ALL DEPENDS ${PROTO_SRCS} ${PROTO_HDRS} ${GRPC_SRCS} ${GRPC_HDRS})

include_directories(${CMAKE_CURRENT_BINARY_DIR})
include_directories("/home/jesse/.local/include")

# Server executable.
add_executable(server
    server-grpc.cpp
    user_auth/user_auth.cpp
    ${PROTO_SRCS}
    ${GRPC_SRCS}
)

target_link_libraries(server
    gRPC::grpc++
    protobuf::libprotobuf
    ssl
    crypto
    pthread
    absl::base
    absl::strings
    absl::str_format_internal
    argon2  # Link against Argon2 for argon2_hash, argon2_verify, etc.
)

# Client executable.
add_executable(client
    client-grpc.cpp
    ${PROTO_SRCS}
    ${GRPC_SRCS}
)

target_link_libraries(client
    gRPC::grpc++
    protobuf::libprotobuf
    ssl
    crypto
    pthread
    absl::base
    absl::strings
    absl::str_format_internal
)

enable_testing()
add_executable(test-grpc test-grpc.cpp user_auth/user_auth.cpp)
target_link_libraries(test-grpc
    gRPC::grpc++
    protobuf::libprotobuf
    ssl
    crypto
    pthread
    gtest
    gtest_main
    absl::base
    absl::strings
)
add_test(NAME ChatServiceTests COMMAND test-grpc)


