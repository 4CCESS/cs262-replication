cmake_minimum_required(VERSION 3.10)
project(SimpleChatApp)

# Use C++17 for better compatibility
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Find dependencies
find_package(Threads REQUIRED)
find_package(SQLite3 REQUIRED)

# On macOS, help find Homebrew packages
if(APPLE)
  if(EXISTS "/opt/homebrew")
    include_directories("/opt/homebrew/include")
    link_directories("/opt/homebrew/lib")
  endif()
endif()

# Set project structure
set(PROTO_DIR "${CMAKE_SOURCE_DIR}/proto")
set(SERVER_DIR "${CMAKE_SOURCE_DIR}/server")
set(CLIENT_DIR "${CMAKE_SOURCE_DIR}/client")
set(BUILD_DIR "${CMAKE_BINARY_DIR}")

# Create directory for generated files
set(GENERATED_DIR "${BUILD_DIR}/generated")
file(MAKE_DIRECTORY ${GENERATED_DIR})

# Find protoc and gRPC plugin
find_program(PROTOC protoc REQUIRED)
find_program(GRPC_CPP_PLUGIN grpc_cpp_plugin REQUIRED)

# Add custom command to generate protocol buffer code
add_custom_command(
  OUTPUT 
    "${GENERATED_DIR}/chat.pb.cc"
    "${GENERATED_DIR}/chat.pb.h"
    "${GENERATED_DIR}/chat.grpc.pb.cc"
    "${GENERATED_DIR}/chat.grpc.pb.h"
  COMMAND ${PROTOC}
    --proto_path=${PROTO_DIR}
    --cpp_out=${GENERATED_DIR}
    --grpc_out=${GENERATED_DIR}
    --plugin=protoc-gen-grpc=${GRPC_CPP_PLUGIN}
    "${PROTO_DIR}/chat.proto"
  DEPENDS "${PROTO_DIR}/chat.proto"
  COMMENT "Generating protocol buffer and gRPC code"
  VERBATIM
)

# Create a custom target for generating protocol buffer code
add_custom_target(
  generate_proto ALL
  DEPENDS 
    "${GENERATED_DIR}/chat.pb.cc"
    "${GENERATED_DIR}/chat.pb.h"
    "${GENERATED_DIR}/chat.grpc.pb.cc"
    "${GENERATED_DIR}/chat.grpc.pb.h"
)

# Create proto library
add_library(chat_proto
  "${GENERATED_DIR}/chat.pb.cc"
  "${GENERATED_DIR}/chat.grpc.pb.cc"
)
add_dependencies(chat_proto generate_proto)

target_include_directories(chat_proto PUBLIC ${GENERATED_DIR})
target_link_libraries(chat_proto protobuf grpc++)

# Server
add_executable(chat_server
  "${SERVER_DIR}/server.cc"
  "${SERVER_DIR}/server.h"
  "${SERVER_DIR}/db_manager.cc"
  "${SERVER_DIR}/db_manager.h"
  "${SERVER_DIR}/server_main.cc"
)

add_dependencies(chat_server chat_proto)

target_include_directories(chat_server PRIVATE 
  ${GENERATED_DIR}
  ${SQLite3_INCLUDE_DIRS}
)

target_link_libraries(chat_server
  chat_proto
  ${SQLite3_LIBRARIES}
  ${CMAKE_THREAD_LIBS_INIT}
)

# Client
add_executable(chat_client
  "${CLIENT_DIR}/client.cc"
  "${CLIENT_DIR}/client.h"
  "${CLIENT_DIR}/client_main.cc"
)

add_dependencies(chat_client chat_proto)

target_include_directories(chat_client PRIVATE ${GENERATED_DIR})

target_link_libraries(chat_client
  chat_proto
  ${CMAKE_THREAD_LIBS_INIT}
)
