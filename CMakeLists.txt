cmake_minimum_required(VERSION 3.20)

project(DynamicIsland VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# ImGui paths
set(IMGUI_DIR ${CMAKE_SOURCE_DIR}/include/imgui)

set(IMGUI_SOURCES
    ${IMGUI_DIR}/imgui.cpp
    ${IMGUI_DIR}/imgui_draw.cpp
    ${IMGUI_DIR}/imgui_tables.cpp
    ${IMGUI_DIR}/imgui_widgets.cpp
    ${IMGUI_DIR}/imgui_demo.cpp
)

set(PROJECT_SOURCES
    src/main.cpp
    src/Logger.cpp
    src/window.cpp
    src/ui.cpp
    src/sysinfo.cpp
    src/scheduler.cpp
    src/trayicon.cpp
    src/config.cpp
    src/transferstation.cpp
)

add_executable(${PROJECT_NAME} ${PROJECT_SOURCES} ${IMGUI_SOURCES})

target_include_directories(${PROJECT_NAME} PRIVATE
    ${IMGUI_DIR}
    ${IMGUI_DIR}/backends
    src
)

# File transfer station — enabled
target_compile_definitions(${PROJECT_NAME} PRIVATE
    USE_FILE_TRANSFER=1
)

# === Platform-specific configuration ===
if(WIN32)
    # Windows: MinGW + Direct3D 11
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -municode")
    set(CMAKE_EXE_LINKER_FLAGS "-static-libgcc -static-libstdc++ -static")
    set(CMAKE_WIN32_EXECUTABLE TRUE)

    target_sources(${PROJECT_NAME} PRIVATE
        ${IMGUI_DIR}/backends/imgui_impl_win32.cpp
        ${IMGUI_DIR}/backends/imgui_impl_dx11.cpp
    )

    target_link_libraries(${PROJECT_NAME}
        d3d11 dxgi d3dcompiler dwmapi pdh wbemuuid taskschd
        shell32 ole32 oleaut32 uuid winmm gdi32 user32
        advapi32 setupapi psapi iphlpapi ws2_32 gdiplus shlwapi
    )

    add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_directory
        ${CMAKE_SOURCE_DIR}/assets $<TARGET_FILE_DIR:${PROJECT_NAME}>/
    )
else()
    # Linux: GLFW + OpenGL 3
    find_package(OpenGL REQUIRED)
    find_package(glfw3 REQUIRED)

    target_sources(${PROJECT_NAME} PRIVATE
        ${IMGUI_DIR}/backends/imgui_impl_glfw.cpp
        ${IMGUI_DIR}/backends/imgui_impl_opengl3.cpp
    )

    target_link_libraries(${PROJECT_NAME}
        OpenGL::GL glfw dl pthread
    )

    add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_directory
        ${CMAKE_SOURCE_DIR}/assets $<TARGET_FILE_DIR:${PROJECT_NAME}>/
    )
endif()
