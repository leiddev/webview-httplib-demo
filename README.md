# webview-httplib-demo

用 **cpp-httplib + webview + cpp-embedlib** 搭的最小桌面应用示例（Windows / VS2022）。

跑起来之后是一个原生窗口，界面是一个本地 HTML 页面，但它不是从磁盘读的——
HTML / CSS / JS 全部被 `cpp-embedlib` 编译进了 exe，由后台的 `cpp-httplib` 服务器
从内存里发出来，窗口本身由 `webview` 创建（内核是 Edge WebView2）。

webview 用的是 **0.12.0 的 C++ API**（`webview::webview w(true, nullptr)` 加
`set_title` / `set_size` / `bind` / `navigate` / `run`，靠 RAII 析构销毁窗口）；
同一个头文件里 C API 依然存在，两者的对应关系见下面「C API ↔ C++ API 对照」。

```
┌──────────────────────────────────────────────┐
│  webview 窗口 (WebView2)                     │
│  ┌────────────────────────────────────────┐  │
│  │ index.html + app.js  ← 内存中的内嵌资源 │  │
│  │                                        │  │
│  │  fetch('/api/xxx')                     │  │
│  │        │                               │  │
│  │        ▼   HTTP (127.0.0.1:随机端口)    │  │
│  │  ┌──────────────────────────┐          │  │
│  │  │ cpp-httplib 服务器(线程)  │          │  │
│  │  └──────────────────────────┘          │  │
│  │                                        │  │
│  │  window.cppNativeXxx()                 │  │
│  │        │  webview::bind（不走 HTTP）    │  │
│  │        ▼                               │  │
│  │  C++ 函数（同一进程，主线程）            │  │
│  └────────────────────────────────────────┘  │
└──────────────────────────────────────────────┘
```

## 目录结构

```
webview-httplib-demo/
├─ CMakeLists.txt          构建脚本（三个依赖全部自动 FetchContent 下载）
├─ CMakePresets.json       VS2022 预设：x64 / x64-console，release / debug
├─ src/main.cpp            全部 C++ 代码：HTTP 服务 + 路由 + 原生绑定 + 窗口
├─ www/                    前端（会被编译进 exe）
│  ├─ index.html
│  ├─ style.css
│  └─ app.js
├─ .github/workflows/      CI：windows-latest 上配置 + 编译 + 上传产物
├─ .clang-format           C++ / JS 格式化规则（clang-format 19 实测 0 违规）
├─ .editorconfig           缩进、换行、编码统一
├─ .gitignore              build/、.vs/、MSVC 中间产物…
├─ .gitattributes          换行统一 LF、二进制标记、语言统计
├─ THIRD_PARTY_NOTICES.md  三方组件与许可（cpp-httplib/webview/cpp-embedlib/WebView2）
└─ LICENSE                 MIT
```

## 构建与运行

需要：VS2022（含“使用 C++ 的桌面开发”）、CMake ≥ 3.20、Git、能访问 github.com（首次配置还要访问 nuget.org）。
运行环境需要 **WebView2 运行时**（Win10/11 一般已随 Edge 预装）。

### 用预设（推荐）

```powershell
cd C:\Project\webview-httplib-demo

cmake --preset x64          # 配置（首次会 clone 三个库 + 下载 WebView2 SDK，需要几分钟）
cmake --build --preset release
.\build\Release\webview-demo.exe
```

可用预设：

| 预设 | 作用 |
|---|---|
| `x64` / `release`、`debug` | 默认 GUI 版，输出在 `build\Release` 或 `build\Debug` |
| `x64-console` / `console-release` | 控制台版（带日志窗口），输出在 `build-console\Release` |

### 不用预设

```powershell
cmake -S . -B build -G "Visual Studio 17 2022" -A x64
cmake --build build --config Release
.\build\Release\webview-demo.exe
```

可选参数：

| 参数 | 作用 |
|---|---|
| `--port 8080` | 用固定端口（默认自动挑一个空闲端口） |
| 构建时 `-DWEBVIEW_DEMO_CONSOLE=ON` | 编译成控制台程序，能直接看到日志（调试用） |

```powershell
# 控制台版（能看到 [http] 访问日志），等价于上面的 x64-console 预设
cmake --preset x64-console
cmake --build --preset console-release
.\build-console\Release\webview-demo.exe --port 8080
```

## 界面里能验证什么

| 区域 | 演示内容 | 涉及技术 |
|---|---|---|
| ① HTTP API | `fetch` 调用 `/api/hello`、`/api/time`、`/api/info`、`/api/assets`、`/api/echo`、`POST /api/add` | cpp-httplib |
| ② 原生调用 | `cppNativeEcho()`、`cppNativeHandle()`、`cppCloseWindow()`，**不经过 HTTP** | webview `webview::bind` |
| ③ 通信日志 | 每次请求/响应的原始 JSON | 前端 JS |

`GET /api/assets` 会列出被内嵌进 exe 的文件（路径 / MIME / 字节数），可以直观看到
"前端资源在 exe 里"这件事；把 www 目录删掉，程序照样能跑。

## 开发辅助

```powershell
# C++ / JS 格式化（用 VS 自带的 clang-format 19，任意 19.x 均可）
$cf = "C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Tools\Llvm\x64\bin\clang-format.exe"
& $cf -i src\main.cpp www\app.js
& $cf --dry-run --Werror src\main.cpp www\app.js   # 0 违规，可用作格式门禁

# 前端语法检查
node --check www\app.js
```

| 文件 | 作用 |
|---|---|
| `.clang-format` | C++20 与 JS 的格式化规则（当前代码实测 0 违规；`SortIncludes: false` 以保护 `WebAssets.h` 的包含顺序） |
| `.editorconfig` | UTF-8 + LF；C++ 4 空格、前端 2 空格、Markdown 保留行尾空格；IDE 自动生效 |
| `.gitattributes` | 仓库内统一 LF 存储，Windows 脚本保留 CRLF，二进制文件标记 |
| `.gitignore` | `build*/`、`.vs/`、MSVC 中间产物、WebView2 运行时数据目录 |
| `CMakePresets.json` | 上表的四个预设；用 VS「打开文件夹」也能在配置下拉里直接选 |
| `.github/workflows/windows-build.yml` | push / PR 时在 `windows-latest` 上配置 + 编译 + 上传 exe |
| `THIRD_PARTY_NOTICES.md` | 三方组件与许可声明（再分发前请留意） |

## 各库在代码里的落点

```cmake
cpp_embedlib_add(WebAssets FOLDER ${CMAKE_CURRENT_SOURCE_DIR}/www NAMESPACE Web)  # 生成 Web::FS
target_link_libraries(webview-demo PRIVATE
    WebAssets             # 内嵌资源（同时把 C++20 要求传播过来）
    cpp-embedlib-httplib  # 提供 httplib::mount(svr, Web::FS)
    httplib::httplib      # HTTP 服务器
    webview::core)        # webview（header-only；C API 与 C++ API 同一个头文件）
```

```cpp
#include "WebAssets.h"             // 由 cpp-embedlib 生成
#include <cpp-embedlib-httplib.h>  // httplib::mount
#include <httplib.h>
#include <webview/webview.h>       // webview 0.12 的 C++ API（header-only）

httplib::mount(svr, Web::FS);                        // 内嵌资源挂到 "/"
int port = svr.bind_to_any_port("127.0.0.1");        // 随机空闲端口
std::thread t([&] { svr.listen_after_bind(); });     // 后台线程跑服务

webview::webview w(true /* debug */, nullptr);       // 必须主线程；析构即销毁窗口
w.set_title("...");
w.set_size(1080, 780, WEBVIEW_HINT_NONE);
w.bind("cppNativeEcho", &on_native_echo, nullptr);   // JS → C++（不走 HTTP）
w.navigate(url);                                     // 打开本地地址
w.run();                                             // 阻塞，直到窗口关闭
svr.stop(); t.join();
```

### C API ↔ C++ API 对照

两套接口在同一个头文件里，本项目用的是右边那列：

| 功能 | C API | C++ API |
|---|---|---|
| 创建 / 销毁 | `webview_create(1, nullptr)` / `webview_destroy(w)` | `webview::webview w(true, nullptr);`（RAII，析构自动销毁） |
| 标题 / 尺寸 | `webview_set_title` / `webview_set_size` | `w.set_title(...)` / `w.set_size(1080, 780, WEBVIEW_HINT_NONE)` |
| 打开页面 | `webview_navigate` | `w.navigate(url)` |
| 直接塞 HTML | `webview_set_html` | `w.set_html(html)` |
| JS → C++ | `webview_bind(w, "name", fn, arg)` | `w.bind("name", fn, nullptr)` |
| 回传结果 | `webview_return(w, id, 0, json)` | `w.resolve(id, 0, json)` |
| 注入脚本 | `webview_init(w, js)` | `w.init(js)` |
| 执行脚本 | `webview_eval(w, js)` | `w.eval(js)` |
| 投递到 UI 线程 | `webview_dispatch(w, fn, arg)` | `w.dispatch(fn)` |
| 取原生句柄 | `webview_get_window(w)` | `w.window()`（返回 `result<void*>`，要判 `.ok()`） |
| 主循环 / 结束 | `webview_run(w)` / `webview_terminate(w)` | `w.run()` / `w.terminate()` |
| 错误处理 | 返回 `webview_error_t`，`webview_create` 失败返回 `nullptr` | 抛 `webview::exception`；`noresult` / `result<T>` 可用 `.ok()` / `.ensure_ok()` |

C++ 侧的三个额外注意点（都在 `src/main.cpp` 里体现了）：

- **构造函数没有默认参数**：必须写全 `webview::webview w(true, nullptr)`，不能只写 `webview::webview w;`。
  构造时若 WebView2 不可用会抛 `webview::exception`，所以整段要包 `try / catch`。
- **回调签名是 `binding_t`**：`std::function<void(std::string id, std::string args, void* arg)>`，
  比 C 版的 `const char*` 更省事；`args` 是 JSON 数组字符串，用 `resolve()` 回传（结果必须是合法 JSON）。
- **没有公开的版本查询函数**：C 的 `webview_version()` 其实就是返回 `webview::detail::library_version_info`，
  C++ 这边没有等价公开接口，所以 `/api/info` 直接用头文件里的公开宏 `WEBVIEW_VERSION_NUMBER`。

## 需要注意的几个坑（都实测踩过）

1. **webview 0.12 里 C API 和 C++ API 是并存的，别信“C++ API 已被移除”的说法。**
   这条我最初搞错了，纠正如下：`core/include/webview/webview.h` 里**搜不到 `class webview`**，
   但那不代表没有 C++ API——公开类型 `webview::webview` 是个 **type alias**：

   ```cpp
   // webview.h:4367 / 4374
   using browser_engine = detail::win32_edge_engine;   // 各平台各挑一个（GTK / Cocoa / Edge）
   namespace webview { using webview = browser_engine; }
   ```

   真正的实现在 `webview::detail::engine_base`（webview.h:1221，暴露
   `set_title` / `set_size` / `navigate` / `set_html` / `init` / `eval` / `bind` / `run` …）
   和三个平台子类 `gtk_webkit_engine` / `cocoa_wkwebview_engine` / `win32_edge_engine`。
   官方 README 的第一个示例就是 **C++ Example**（`webview::webview w(false, nullptr);`），
   `examples/basic.cc` 同理，实测能编过。

   更关键的是**依赖方向**：C API 是包在这套 C++ 类外面的薄壳——
   `webview_create()` 就是 `new webview::webview{...}`，其余 C 函数清一色转发到成员函数（webview.h:4389 起）。

   两种写法都能用、都编得过；本项目原来用 C API，现已按对照表整体换成 C++ API。
   顺带一个教训：**判断某个 API 是否还在，不能只搜 `class X`**——别名（`using`）和宏同样可能是入口。

2. **webview 必须在 UI 线程调用（C API / C++ API 都一样）。**
   `eval` / `terminate` 这类调用不会自动切线程——在子线程里调用会“返回成功但毫无效果”
   （底层 `ICoreWebView2::ExecuteScript` 只能在 UI 线程跑）。
   子线程要操作窗口，用 `w.dispatch(fn)`（C 版是 `webview_dispatch`）投递到主线程；
   `w.bind()` 注册的回调本身就是在主线程执行的，可以直接调用这些成员函数。

3. **`w.init(js)` 注入的脚本只对“之后创建”的文档生效。**
   想让它在首页就生效，必须在 `w.navigate()` **之前**调用，否则第一次加载的页面不会执行它。

4. **首次配置要联网。** 三个库走 `FetchContent`；Windows 上 webview 还会自动从 nuget.org
   拉 `Microsoft.Web.WebView2` SDK（默认 1.0.1150.38）。如果 nuget 不可达：
   手动下载 `Microsoft.Web.WebView2` 的 nupkg 并解压，然后配置时加
   `-DMSWebView2_ROOT=<解压目录>`（目录里要有 `build/native/include/WebView2.h`）。
   默认启用 webview 内置的 WebView2Loader 实现，所以**不需要**往输出目录拷 `WebView2Loader.dll`。

5. **`WebAssets.h` 是构建时生成的**，第一次编译前 IDE 会在 `#include "WebAssets.h"` 上标红，
   `cmake --build` 一次之后就正常了。

6. **无控制台窗口的 GUI 程序看不到 `printf`。**
   项目默认 `WIN32_EXECUTABLE`（不弹黑框），日志走 `OutputDebugStringA`（VS 输出窗口 / DebugView 可见）。
   调试阶段用 `-DWEBVIEW_DEMO_CONSOLE=ON` 更方便。
   注意 GUI 子系统下 MSVC 默认找 `WinMain`，本项目用 `target_link_options(... "/ENTRY:mainCRTStartup")`
   保留标准 `main()` 入口。

7. **端口冲突**：默认用 `bind_to_any_port` 自动挑空闲端口，窗口打开的就是该端口，不需要硬编码。

## 接口一览

| 方法 | 路径 | 说明 |
|---|---|---|
| GET | `/`、`/style.css`、`/app.js` | 内嵌前端资源（html/css/js 的 MIME 自动识别） |
| GET | `/api/hello` | 返回问候语 + 服务器时间 |
| GET | `/api/time` | 本地时间 + 进程运行毫秒数 |
| GET | `/api/info` | cpp-httplib / webview / cpp-embedlib 版本、PID、运行时长 |
| GET | `/api/assets` | 列出被 cpp-embedlib 内嵌的文件 |
| GET | `/api/echo?q=...` | 回显参数 |
| POST | `/api/add` | 表单 `a=..&b=..`，返回和 |

JS → C++ 绑定：`cppNativeEcho(text)`、`cppNativeHandle()`、`cppCloseWindow()`。

## 已验证的环境

Windows 10 22H2 (19045) x64 · VS2022 Community 17.14 · MSVC 14.44 · CMake 4.3.0 / VS 自带 3.31.6 ·
WebView2 Runtime 153.0.4234.48 · cpp-httplib v0.38.0 · webview 0.12.0 · cpp-embedlib main

换成 C++ API 后的实测记录（`build\Release\webview-demo.exe`，编译 0 警告，约 470 KB）：

- 窗口类名 `webview`、客户区 1080×780、标题与 `set_title()` 一致 → 构造 / `set_size` / `set_title` 生效；
- 服务端日志里先出现 `GET /`、`GET /style.css`、`GET /app.js`（是**窗口自己**来拉的）→ `navigate` 生效、页面渲染成功；
- 全部 7 个接口 200，`POST /api/add` 返回 `{"a":3,"b":4,"sum":7}`；
- 注入脚本串起三个绑定：`cppNativeEcho` 返回 `{"source":"webview::bind → C++",…}`、
  `cppNativeHandle` 返回 `HWND = 0x…`、`cppCloseWindow` 之后进程自行退出（ExitCode=0）→
  `bind` / `resolve` / `window` / `terminate` 与 RAII 析构都正常。
