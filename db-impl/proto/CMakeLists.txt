# Avoid using standard gRPC config files that cause conflicts
find_package(Protobuf REQUIRED)
find_package(PkgConfig REQUIRED)

# Find gRPC using pkg-config, only requiring the main package
pkg_check_modules(GRPC REQUIRED grpc++)

# Try to find reflection library, but don't require it
# (it might be included in the main package in newer versions)
pkg_check_modules(GRPCPP grpc++_reflection)

# Find necessary executables
find_program(PROTOBUF_PROTOC_EXECUTABLE protoc)
if(NOT PROTOBUF_PROTOC_EXECUTABLE)
  message(FATAL_ERROR "Could not find protoc, please install it")
endif()

find_program(GRPC_CPP_PLUGIN_EXECUTABLE grpc_cpp_plugin)
if(NOT GRPC_CPP_PLUGIN_EXECUTABLE)
  message(FATAL_ERROR "Could not find grpc_cpp_plugin, please install it")
endif()

# Generate the gRPC code
get_filename_component(chat_proto "${CMAKE_CURRENT_SOURCE_DIR}/chat.proto" ABSOLUTE)
get_filename_component(chat_proto_path "${chat_proto}" PATH)

# Generate protobuf and gRPC files
set(chat_proto_srcs "${CMAKE_CURRENT_BINARY_DIR}/chat.pb.cc")
set(chat_proto_hdrs "${CMAKE_CURRENT_BINARY_DIR}/chat.pb.h")
set(chat_grpc_srcs "${CMAKE_CURRENT_BINARY_DIR}/chat.grpc.pb.cc")
set(chat_grpc_hdrs "${CMAKE_CURRENT_BINARY_DIR}/chat.grpc.pb.h")

add_custom_command(
  OUTPUT "${chat_proto_srcs}" "${chat_proto_hdrs}" "${chat_grpc_srcs}" "${chat_grpc_hdrs}"
  COMMAND ${PROTOBUF_PROTOC_EXECUTABLE}
  ARGS --grpc_out="${CMAKE_CURRENT_BINARY_DIR}"
    --cpp_out="${CMAKE_CURRENT_BINARY_DIR}"
    -I "${chat_proto_path}"
    --plugin=protoc-gen-grpc="${GRPC_CPP_PLUGIN_EXECUTABLE}"
    "${chat_proto}"
  DEPENDS "${chat_proto}"
)

# Include generated *.pb.h files
include_directories("${CMAKE_CURRENT_BINARY_DIR}")

# Create proto library
add_library(chat_proto
  ${chat_proto_srcs}
  ${chat_proto_hdrs}
  ${chat_grpc_srcs}
  ${chat_grpc_hdrs}
)

# Link with directly discovered libraries instead of using targets
target_include_directories(chat_proto PUBLIC 
  ${PROTOBUF_INCLUDE_DIRS}
  ${GRPC_INCLUDE_DIRS}
)

# Link with reflection library only if found
if(GRPCPP_FOUND)
  target_include_directories(chat_proto PUBLIC ${GRPCPP_INCLUDE_DIRS})
  target_link_libraries(chat_proto
    ${PROTOBUF_LIBRARIES}
    ${GRPC_LIBRARIES}
    ${GRPCPP_LIBRARIES}
  )
else()
  # Otherwise just link with the main gRPC library
  target_link_libraries(chat_proto
    ${PROTOBUF_LIBRARIES}
    ${GRPC_LIBRARIES}
  )
endif()

# Make the library visible to other subprojects
target_include_directories(chat_proto PUBLIC ${CMAKE_CURRENT_BINARY_DIR})
