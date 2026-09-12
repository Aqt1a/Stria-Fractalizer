cmake_minimum_required(VERSION 3.20)
cmake_policy(VERSION 3.20)
project(Stria-Fractalizer C CXX)

set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}")
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}")
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}")

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

include(FetchContent)

# Set policy minimum to allow older CMake projects
set(CMAKE_POLICY_VERSION_MINIMUM 3.5)

# Use local Raylib installation
set(RAYLIB_PATH "../Raylib/raylib")
add_subdirectory(${RAYLIB_PATH} raylib_build)

# Setup project directories
#file(MAKE_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/external")
#file(DOWNLOAD "https://raw.githubusercontent.com/raysan5/raygui/4.0/src/raygui.h" "${CMAKE_CURRENT_SOURCE_DIR}/external/raygui.h")

add_executable(${PROJECT_NAME} 
    src/main.cpp
    src/tinyfiledialogs_windows.cpp
)

target_link_libraries(${PROJECT_NAME} PRIVATE raylib comdlg32)
target_include_directories(${PROJECT_NAME} PRIVATE 
    include 
    external
    "${RAYLIB_PATH}/src"
)

# Copy shaders to build folder on every build
add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
    "${CMAKE_CURRENT_SOURCE_DIR}/resources"
    "$<TARGET_FILE_DIR:${PROJECT_NAME}>/resources"
)
