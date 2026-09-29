# ---------------------------------------------------------------------------
#  webview-httplib-demo
#
#  一个最小的桌面应用示例（Windows / Linux；macOS 理论可行但未验证），
#  演示三个库如何配合：
#    1. cpp-httplib   https://github.com/yhirose/cpp-httplib
#                     本地 HTTP 服务器（提供 /api/* 接口）
#    2. webview       https://github.com/webview/webview
#                     原生窗口 + 系统 WebView 内核（Win: WebView2 / Linux: WebKitGTK）
#    3. cpp-embedlib  https://github.com/yhirose/cpp-embedlib
#                     把 www/ 目录里的前端资源编译进可执行文件（单文件分发）
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
else()
    # GCC / Clang 默认就是 UTF-8 源码字符集，中文注释与字面量无需额外开关
    add_compile_options(-Wall -Wextra)
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

# webview —— 极简跨平台 WebView 封装
#   注意：0.11+ 的 C API 与 C++ API 是并存的（webview::webview 只是平台引擎的 type alias），
#   本项目用的是 C++ API，见 src/main.cpp。
#   Linux 上它会要求系统有 GTK + WebKitGTK 开发包（configure 阶段就会检查）。
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
    webview::core)        # webview（header-only 目标，C/C++ 两套 API 同一个头文件）

# main.cpp 用 std::thread 跑 httplib 的监听线程。Windows 上不需要额外链接，
# 但 Linux 上要 -pthread（依赖里可能已经带上，这里显式声明更稳妥）。
find_package(Threads REQUIRED)
target_link_libraries(webview-demo PRIVATE Threads::Threads)

if(NOT WEBVIEW_DEMO_CONSOLE)
    # 默认构建成 GUI 程序。
    # 注意 WIN32_EXECUTABLE 只对 Windows 目标有意义，其它平台会被忽略 ——
    # 所以 Linux/macOS 构建本来就是个"控制台程序"，日志可以直接在终端看到。
    if(WIN32)
        # Windows 上运行时不弹控制台黑框。
        # MSVC 的 GUI 子系统默认找 WinMain，这里让 CRT 仍然调用标准的 main()。
        set_target_properties(webview-demo PROPERTIES WIN32_EXECUTABLE TRUE)
        if(MSVC)
            target_link_options(webview-demo PRIVATE "/ENTRY:mainCRTStartup")
        endif()
    endif()
endif()

if(WIN32)
    set(_run_hint "build\\Release\\webview-demo.exe")
else()
    set(_run_hint "build/webview-demo")
endif()
message(STATUS "----------------------------------------------------------")
message(STATUS " webview-httplib-demo 配置完成")
message(STATUS "   平台   : ${CMAKE_SYSTEM_NAME}")
message(STATUS "   生成器 : ${CMAKE_GENERATOR}")
message(STATUS "   构建   : cmake --build build --config Release")
message(STATUS "   运行   : ${_run_hint}")
message(STATUS "----------------------------------------------------------")
