# 设置最低的 CMake 版本
cmake_minimum_required(VERSION 3.10)

# 项目名称和版本
project(HPNetworkSystem_Example VERSION 1.0)

# 指定 C++ 标准
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED True)

# 包含 FetchContent 模块
include(FetchContent)

# 下载并构建外部库
FetchContent_Declare(
    SDL2
    GIT_REPOSITORY https://github.com/libsdl-org/SDL.git
    GIT_TAG        SDL2  # 或者指定某个标签或提交哈希
    BINARY_DIR ${CMAKE_CURRENT_BINARY_DIR}/ThirdParties/sdl  # 指定下载路径
)

FetchContent_Declare(
    ImGui
    GIT_REPOSITORY https://github.com/ocornut/imgui.git
    GIT_TAG        docking  # 或者指定某个标签或提交哈希
    SOURCE_DIR ${CMAKE_CURRENT_BINARY_DIR}/ThirdParties/imgui  # 指定下载路径
)

FetchContent_Declare(
    nlohmann_json
    GIT_REPOSITORY https://github.com/nlohmann/json.git
    GIT_TAG        develop  # 或者指定某个标签或提交哈希
    SOURCE_DIR ${CMAKE_CURRENT_BINARY_DIR}/ThirdParties/json  # 指定下载路径
)

find_package(OpenGL REQUIRED)

# 下载 FetchContent 声明的内容
FetchContent_MakeAvailable(SDL2 ImGui nlohmann_json)

# 创建可执行文件
add_executable(HPNetworkSystem_Example
    ${CMAKE_CURRENT_SOURCE_DIR}/HPNS/src/main_client.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/HPNS/src/client_gui.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/HPNS/src/gui.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../HPNS/HPNS.cpp
    ${CMAKE_CURRENT_BINARY_DIR}/ThirdParties/imgui/imgui.cpp
    ${CMAKE_CURRENT_BINARY_DIR}/ThirdParties/imgui/imgui_demo.cpp
    ${CMAKE_CURRENT_BINARY_DIR}/ThirdParties/imgui/imgui_draw.cpp
    ${CMAKE_CURRENT_BINARY_DIR}/ThirdParties/imgui/imgui_tables.cpp
    ${CMAKE_CURRENT_BINARY_DIR}/ThirdParties/imgui/imgui_widgets.cpp
    ${CMAKE_CURRENT_BINARY_DIR}/ThirdParties/imgui/backends/imgui_impl_opengl3.cpp
    ${CMAKE_CURRENT_BINARY_DIR}/ThirdParties/imgui/backends/imgui_impl_sdl2.cpp
)

# 添加包含目录
target_include_directories(HPNetworkSystem_Example PRIVATE
        ${OPENGL_INCLUDE_DIRS}
    ${CMAKE_CURRENT_BINARY_DIR}/ThirdParties/imgui/
    ${CMAKE_CURRENT_BINARY_DIR}/ThirdParties/imgui/backends/
    ${CMAKE_CURRENT_BINARY_DIR}/ThirdParties/json/include/
    ${CMAKE_CURRENT_BINARY_DIR}/ThirdParties/sdl/include/
    ${CMAKE_CURRENT_BINARY_DIR}/ThirdParties/sdl/include-config-release/
    ${CMAKE_CURRENT_SOURCE_DIR}/../HPNS/
    ${CMAKE_CURRENT_SOURCE_DIR}/HPNS/include/
)

# 链接 SDL2 库
target_link_libraries(HPNetworkSystem_Example PRIVATE ${OPENGL_LIBRARIES} SDL2::SDL2 SDL2::SDL2main)
# 复制 配置文件 到输出目录

add_custom_command(TARGET HPNetworkSystem_Example POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
    "${CMAKE_CURRENT_SOURCE_DIR}/HPNS/imgui.ini"
    "$<TARGET_FILE_DIR:HPNetworkSystem_Example>/imgui.ini"
)

# 复制 SDL2 的动态链接库到输出目录
if(WIN32)
    # Windows 下处理 DLL

        add_custom_command(TARGET HPNetworkSystem_Example POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "$<TARGET_FILE_DIR:SDL2::SDL2>/sdl2d.dll"  # 调试模式下的 DLL
            "$<TARGET_FILE_DIR:HPNetworkSystem_Example>/sdl2d.dll"  # 目标输出目录
        )

        add_custom_command(TARGET HPNetworkSystem_Example POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "$<TARGET_FILE_DIR:SDL2::SDL2>/sdl2.dll"  # 发布模式下的 DLL
            "$<TARGET_FILE_DIR:HPNetworkSystem_Example>/sdl2.dll"  # 目标输出目录
        )

elseif(UNIX)
    # Unix 下处理共享库

    add_custom_command(TARGET HPNetworkSystem_Example POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "$<TARGET_FILE_DIR:SDL2::SDL2>/libSDL2d.so"  # Linux 下的共享库
        "$<TARGET_FILE_DIR:HPNetworkSystem_Example>/libSDL2d.so"  # 目标输出目录
    )

    add_custom_command(TARGET HPNetworkSystem_Example POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
    "$<TARGET_FILE_DIR:SDL2::SDL2>/libSDL2.so"  # Linux 下的共享库
    "$<TARGET_FILE_DIR:HPNetworkSystem_Example>/libSDL2.so"  # 目标输出目录
)

endif()

install(TARGETS SDL2 EXPORT SDL2Targets
        RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
        LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
        ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR})
