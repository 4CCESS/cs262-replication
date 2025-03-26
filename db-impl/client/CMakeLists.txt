# Client executable
add_executable(chat_client
  client.cc
  client.h
  client_main.cc
)

# Find gRPC via pkg-config
find_package(PkgConfig REQUIRED)
pkg_check_modules(GRPC REQUIRED grpc++)
pkg_check_modules(GRPCPP grpc++_reflection)

# Include directories
target_include_directories(chat_client PRIVATE 
  ${CMAKE_CURRENT_BINARY_DIR}
  ${GRPC_INCLUDE_DIRS}
)

# Add reflection includes only if found
if(GRPCPP_FOUND)
  target_include_directories(chat_client PRIVATE ${GRPCPP_INCLUDE_DIRS})
endif()

# Link libraries
target_link_libraries(chat_client
  chat_proto
  ${CMAKE_THREAD_LIBS_INIT}
)
