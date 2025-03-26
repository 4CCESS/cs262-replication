# ---------------------------------------------------------------
# CMake configuration file for building the chat server executable.
#
# CMake is a cross-platform tool used to control the build process.
# It generates build files for your chosen environment (e.g., Makefiles).
#
# This file specifies the source files, finds required libraries,
# sets up include directories, and links libraries together.
# ---------------------------------------------------------------

# Define the executable target named "chat_server" using the listed source files.
add_executable(chat_server
  server.cc         # Implementation of the server’s gRPC service logic.
  server.h          # Header file declaring the server service class.
  db_manager.cc     # Implements database operations (using SQLite).
  db_manager.h      # Header file declaring the DBManager class.
  server_main.cc    # Contains the main() function to start the server.
)

# Find the SQLite3 package. This is used to interact with an SQLite database.
find_package(SQLite3 REQUIRED)

# PkgConfig is used to locate packages, such as gRPC, on your system.
find_package(PkgConfig REQUIRED)

# Find the gRPC C++ package using pkg-config.
pkg_check_modules(GRPC REQUIRED grpc++)

# Optionally find gRPC reflection (if available).
pkg_check_modules(GRPCPP grpc++_reflection)

# Set include directories for the target.
# - ${CMAKE_CURRENT_BINARY_DIR} allows including generated files.
# - ${SQLite3_INCLUDE_DIRS} gives the location of SQLite header files.
# - ${GRPC_INCLUDE_DIRS} gives the location of gRPC header files.
target_include_directories(chat_server PRIVATE 
  ${CMAKE_CURRENT_BINARY_DIR}
  ${SQLite3_INCLUDE_DIRS}
  ${GRPC_INCLUDE_DIRS}
)

# If gRPC reflection is available, add its include directories.
if(GRPCPP_FOUND)
  target_include_directories(chat_server PRIVATE ${GRPCPP_INCLUDE_DIRS})
endif()

# Link the libraries to the target:
# - chat_proto: The gRPC-generated code from your chat.proto file.
# - ${SQLite3_LIBRARIES}: The SQLite library.
# - ${CMAKE_THREAD_LIBS_INIT}: Ensures multi-threading support.
target_link_libraries(chat_server
  chat_proto
  ${SQLite3_LIBRARIES}
  ${CMAKE_THREAD_LIBS_INIT}
)
