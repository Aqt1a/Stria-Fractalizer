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

# 1. Fetch raylib and download it directly into the file system (external/raylib)
set(BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(BUILD_GAMES OFF CACHE BOOL "" FORCE)

FetchContent_Declare(
    raylib
    GIT_REPOSITORY https://github.com/raysan5/raylib.git
    GIT_TAG        master
    SOURCE_DIR     ${CMAKE_CURRENT_SOURCE_DIR}/external/raylib
)
FetchContent_MakeAvailable(raylib)

# 2. Automatically download raygui.h into the external folder if it's missing and patch it
if(NOT EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/external/raygui.h")
    file(MAKE_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/external")
    file(DOWNLOAD 
        "https://raw.githubusercontent.com/raysan5/raygui/master/src/raygui.h" 
        "${CMAKE_CURRENT_SOURCE_DIR}/external/raygui.h"
        TIMEOUT 15
        STATUS DOWNLOAD_STATUS
    )
    list(GET DOWNLOAD_STATUS 0 STATUS_CODE)
    if(NOT STATUS_CODE EQUAL 0)
        message(FATAL_ERROR "Failed to download raygui.h. Check your network connection.")
    else()
        message(STATUS "Successfully downloaded raygui.h")
    endif()

    # --- AUTOMATED ROUNDED FUNCTION PATCH (EXACT MATCH) ---
    message(STATUS "Patching raygui.h with custom rounded GuiDrawRectangle...")
    file(READ "${CMAKE_CURRENT_SOURCE_DIR}/external/raygui.h" RAYGUI_CONTENT)

    # 1. Normalize line endings to avoid \r\n vs \n matching failures
    string(REPLACE "\r" "" RAYGUI_CONTENT "${RAYGUI_CONTENT}")

    # 2. Define the exact original string from raygui to replace
    string(CONCAT ORIGINAL_FUNCTION
        "// Gui draw rectangle using default raygui plain style with borders\n"
        "static void GuiDrawRectangle(Rectangle rec, int borderWidth, Color borderColor, Color color)\n"
        "{\n"
        "    if (color.a > 0)\n"
        "    {\n"
        "        // Draw rectangle filled with color\n"
        "        DrawRectangle((int)rec.x, (int)rec.y, (int)rec.width, (int)rec.height, GuiFade(color, guiAlpha));\n"
        "    }\n"
        "\n"
        "    if (borderWidth > 0)\n"
        "    {\n"
        "        // Draw rectangle border lines with color\n"
        "        DrawRectangle((int)rec.x, (int)rec.y, (int)rec.width, borderWidth, GuiFade(borderColor, guiAlpha));\n"
        "        DrawRectangle((int)rec.x, (int)rec.y + borderWidth, borderWidth, (int)rec.height - 2*borderWidth, GuiFade(borderColor, guiAlpha));\n"
        "        DrawRectangle((int)rec.x + (int)rec.width - borderWidth, (int)rec.y + borderWidth, borderWidth, (int)rec.height - 2*borderWidth, GuiFade(borderColor, guiAlpha));\n"
        "        DrawRectangle((int)rec.x, (int)rec.y + (int)rec.height - borderWidth, (int)rec.width, borderWidth, GuiFade(borderColor, guiAlpha));\n"
        "    }\n"
        "\n"
        "#if defined(RAYGUI_DEBUG_RECS_BOUNDS)\n"
        "    DrawRectangle((int)rec.x, (int)rec.y, (int)rec.width, (int)rec.height, Fade(RED, 0.4f));\n"
        "#endif\n"
        "}"
    )

    # 3. Define your custom rounded replacement
    string(CONCAT CUSTOM_GUI_DRAW_RECTANGLE
        "static void GuiDrawRectangle(Rectangle rec, int borderWidth, Color borderColor, Color color)\n"
        "{\n"
        "    float targetRadius = 12.0f;\n"
        "    float smallerDimension = (rec.width < rec.height) ? rec.width : rec.height;\n"
        "    float roundness = 0.0f;\n"
        "    if (smallerDimension > 0.0f)\n"
        "    {\n"
        "        roundness = (targetRadius * 2.0f) / smallerDimension;\n"
        "        if (roundness > 1.0f) roundness = 1.0f;\n"
        "    }\n"
        "    int segments = 16;\n"
        "\n"
        "    if (color.a > 0)\n"
        "    {\n"
        "        DrawRectangleRounded(rec, roundness, segments, GuiFade(color, guiAlpha));\n"
        "    }\n"
        "\n"
        "    if (borderWidth > 0)\n"
        "    {\n"
        "        DrawRectangleRoundedLines(rec, roundness, segments, GuiFade(borderColor, guiAlpha));\n"
        "    }\n"
        "\n"
        "#if defined(RAYGUI_DEBUG_RECS_BOUNDS)\n"
        "    DrawRectangle((int)rec.x, (int)rec.y, (int)rec.width, (int)rec.height, Fade(RED, 0.4f));\n"
        "#endif\n"
        "}"
    )

    # 4. Perform an EXACT string replacement
    string(REPLACE "${ORIGINAL_FUNCTION}" "${CUSTOM_GUI_DRAW_RECTANGLE}" RAYGUI_CONTENT "${RAYGUI_CONTENT}")

    # Write the patched content back
    file(WRITE "${CMAKE_CURRENT_SOURCE_DIR}/external/raygui.h" "${RAYGUI_CONTENT}")
    message(STATUS "raygui.h successfully patched with rounded rects!")
endif()

add_executable(${PROJECT_NAME} 
    src/main.cpp
    src/tinyfiledialogs_windows.cpp
)

target_link_libraries(${PROJECT_NAME} PRIVATE raylib comdlg32)
target_include_directories(${PROJECT_NAME} PRIVATE 
    include 
    external
    "${CMAKE_CURRENT_SOURCE_DIR}/external/raylib/src"
)

# Copy shaders to build folder on every build
add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
    "${CMAKE_CURRENT_SOURCE_DIR}/resources"
    "$<TARGET_FILE_DIR:${PROJECT_NAME}>/resources"
)

# Custom target to purge fetched dependencies and clean build cache
add_custom_target(purge
    COMMAND ${CMAKE_COMMAND} -E remove_directory "${CMAKE_CURRENT_SOURCE_DIR}/external"
    COMMAND ${CMAKE_COMMAND} -E remove -f "${CMAKE_CURRENT_BINARY_DIR}/CMakeCache.txt"
    COMMAND ${CMAKE_COMMAND} -E remove_directory "${CMAKE_CURRENT_BINARY_DIR}/CMakeFiles"
    COMMENT "Purging external dependencies and resetting CMake cache..."
)