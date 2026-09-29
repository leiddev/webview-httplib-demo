# ---------------------------------------------------------------------------
#  webview-httplib-demo
#
#  一个最小的 Windows 桌面应用示例，演示三个库如何配合：
#    1. cpp-httplib   https://github.com/yhirose/cpp-httplib
#                     本地 HTTP 服务器（提供 /api/* 接口）
#    2. webview       https://github.com/webview/webview
#                     原生窗口 + WebView2 内核（把前端显示出来）
#    3. cpp-embedlib  https://github.com/yhirose/cpp-embedlib
#                     把 www/ 目录里的前端资源编译进 exe（单文件分发）
# ---------------------------------------------------------------------------
cmake_minimum_required(VERSION 3.20)

project(webview-httplib-demo
    VERSION     1.0.0
    DESCRIPTION "cpp-httplib + webview + cpp-embedlib desktop demo"
    LANGUAGES   CXX)

# --- 编译标准 / 编译选项 ----------------------------------------------------
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(MSVC)
    # /utf-8 : 源码与执行字符集都用 UTF-8（保证中文字符串字面量正确）
    # /MP    : 多进程编译
    add_compile_options(/utf-8 /MP /W3)
    add_compile_definitions(NOMINMAX _CRT_SECURE_NO_WARNINGS)
endif()

# --- 选项 -------------------------------------------------------------------
option(WEBVIEW_DEMO_CONSOLE "以控制台程序方式构建（可看到日志输出，便于调试）" OFF)

# ---------------------------------------------------------------------------
#  第三方依赖：全部在 configure 阶段自动下载
# ---------------------------------------------------------------------------
include(FetchContent)

# cpp-httplib —— 单头文件 HTTP 服务器/客户端
FetchContent_Declare(cpp-httplib
    GIT_REPOSITORY https://github.com/yhirose/cpp-httplib.git
    GIT_TAG        v0.38.0
    GIT_SHALLOW    TRUE)

# webview —— 极简跨平台 WebView 封装（0.12 起只提供 C API）
FetchContent_Declare(webview
    GIT_REPOSITORY https://github.com/webview/webview.git
    GIT_TAG        0.12.0
    GIT_SHALLOW    TRUE)

# cpp-embedlib —— 把前端资源嵌入可执行文件
FetchContent_Declare(cpp-embedlib
    GIT_REPOSITORY https://github.com/yhirose/cpp-embedlib.git
    GIT_TAG        main
    GIT_SHALLOW    TRUE)

# 让 httplib 保持零外部依赖（不找 OpenSSL / zlib / brotli）
set(HTTPLIB_REQUIRE_OPENSSL          OFF CACHE BOOL "" FORCE)
set(HTTPLIB_USE_OPENSSL_IF_AVAILABLE OFF CACHE BOOL "" FORCE)
set(HTTPLIB_USE_ZLIB_IF_AVAILABLE    OFF CACHE BOOL "" FORCE)
set(HTTPLIB_USE_BROTLI_IF_AVAILABLE  OFF CACHE BOOL "" FORCE)
# webview 只构建核心库，不构建它的示例/测试/文档
set(WEBVIEW_BUILD_TESTS    OFF CACHE BOOL "" FORCE)
set(WEBVIEW_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(WEBVIEW_BUILD_DOCS     OFF CACHE BOOL "" FORCE)
# 我们只用 header-only 的 webview::core，不需要编译好的 dll/lib
set(WEBVIEW_BUILD_SHARED_LIBRARY OFF CACHE BOOL "" FORCE)
set(WEBVIEW_BUILD_STATIC_LIBRARY OFF CACHE BOOL "" FORCE)

FetchContent_MakeAvailable(cpp-httplib webview cpp-embedlib)

# ---------------------------------------------------------------------------
#  把 www/ 里的前端资源嵌入成静态库，暴露 Web::FS（生成头文件 WebAssets.h）
# ---------------------------------------------------------------------------
cpp_embedlib_add(WebAssets
    FOLDER    ${CMAKE_CURRENT_SOURCE_DIR}/www
    NAMESPACE Web)

# ---------------------------------------------------------------------------
#  示例程序
# ---------------------------------------------------------------------------
add_executable(webview-demo src/main.cpp)

target_link_libraries(webview-demo PRIVATE
    WebAssets             # Web::FS —— 内嵌的前端资源
    cpp-embedlib-httplib  # httplib::mount(svr, Web::FS)
    httplib::httplib      # HTTP 服务器
    webview::core)        # webview C API（header-only 目标）

if(NOT WEBVIEW_DEMO_CONSOLE)
    # 默认构建成 GUI 程序：运行时不弹控制台黑框。
    # MSVC 的 GUI 子系统默认找 WinMain，这里让 CRT 仍然调用标准的 main()。
    set_target_properties(webview-demo PROPERTIES WIN32_EXECUTABLE TRUE)
    if(MSVC)
        target_link_options(webview-demo PRIVATE "/ENTRY:mainCRTStartup")
    endif()
endif()

message(STATUS "----------------------------------------------------------")
message(STATUS " webview-httplib-demo 配置完成")
message(STATUS "   生成器 : ${CMAKE_GENERATOR}")
message(STATUS "   构建   : cmake --build build --config Release")
message(STATUS "   运行   : build\\Release\\webview-demo.exe")
message(STATUS "----------------------------------------------------------")
