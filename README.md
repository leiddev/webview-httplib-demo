# webview-httplib-demo

[![build](https://github.com/leiddev/webview-httplib-demo/actions/workflows/build.yml/badge.svg)](https://github.com/leiddev/webview-httplib-demo/actions/workflows/build.yml)

用 **cpp-httplib + webview + cpp-embedlib** 搭的最小桌面应用示例（**Windows / Linux** 均已实测，macOS 理论可行但未验证）。

跑起来之后是一个原生窗口，界面是一个本地 HTML 页面，但它不是从磁盘读的——
HTML / CSS / JS 全部被 `cpp-embedlib` 编译进了可执行文件，由后台的 `cpp-httplib` 服务器
从内存里发出来，窗口本身由 `webview` 创建（Windows 上内核是 Edge WebView2，Linux 上是 GTK + WebKitGTK）。

webview 用的是 **0.12.0 的 C++ API**（`webview::webview w(true, nullptr)` 加
`set_title` / `set_size` / `bind` / `navigate` / `run`，靠 RAII 析构销毁窗口）；
同一个头文件里 C API 依然存在，两者的对应关系见下面「C API ↔ C++ API 对照」。

```
┌──────────────────────────────────────────────┐
│  webview 窗口 (WebView2 / WebKitGTK)          │
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
├─ CMakePresets.json       Windows: x64 / x64-console；Linux: linux / linux-debug
├─ src/main.cpp            全部 C++ 代码：HTTP 服务 + 路由 + 原生绑定 + 窗口
├─ www/                    前端（会被编译进可执行文件）
│  ├─ index.html
│  ├─ style.css
│  └─ app.js
├─ .github/workflows/      CI：windows-latest + ubuntu-22.04 配置 + 编译（+ Linux 冒烟测试）
├─ .clang-format           C++ / JS 格式化规则（clang-format 19 实测 0 违规）
├─ .editorconfig           缩进、换行、编码统一
├─ .gitignore              build/、.vs/、MSVC 中间产物…
├─ .gitattributes          换行统一 LF、二进制标记、语言统计
├─ THIRD_PARTY_NOTICES.md  三方组件与许可（cpp-httplib/webview/cpp-embedlib/WebView2）
└─ LICENSE                 MIT
```

## 构建与运行

### Windows（Visual Studio 2022 或更新）

需要：Visual Studio 2022 或更新（含"使用 C++ 的桌面开发"工作负载）、CMake ≥ 3.20、Git、
能访问 github.com（首次配置还要访问 nuget.org）。
预设**不指定 Visual Studio 版本**，会跟随本机默认（最新）的 VS，所以 VS2022 / VS2026 都能直接用（见坑 10）。
运行环境需要 **WebView2 运行时**（Win10/11 一般已随 Edge 预装）。

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

预设带 `condition`，在 Linux 上 `cmake --list-presets` 只会列出 `linux*`，反之亦然——不会选错。

### Linux

需要：**CMake ≥ 3.20**、**GCC ≥ 10**（或等价的 clang）、`pkg-config`、
GTK3 + WebKitGTK 的开发包、Git、能访问 github.com。
无显示器的机器（容器 / CI / 服务器）想真的把窗口跑起来，还要 `xvfb`。

```bash
# Ubuntu 22.04 / 24.04
sudo apt install -y build-essential cmake ninja-build pkg-config \
                    libgtk-3-dev libwebkit2gtk-4.1-dev

# Ubuntu 20.04 只有 WebKitGTK 4.0，而且自带 GCC 9 编不了 cpp-embedlib（缺 <span>），
# 所以要额外装 g++-10 并指定 WEBVIEW_WEBKITGTK_API：
#   sudo apt install -y g++-10 libwebkit2gtk-4.0-dev
#   然后配置时加 -DCMAKE_CXX_COMPILER=g++-10 -DWEBVIEW_WEBKITGTK_API=4.0

cmake -S . -B build-linux -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build-linux
./build-linux/webview-demo              # 或者用预设：cmake --preset linux && cmake --build --preset linux-release
```

无显示器时（`DISPLAY` 为空）：

```bash
export WEBKIT_DISABLE_DMABUF_RENDERER=1   # 虚拟机/无 GPU 时 DMA-BUF 会失败
export WEBKIT_DISABLE_COMPOSITING_MODE=1
export LIBGL_ALWAYS_SOFTWARE=1
xvfb-run -a --server-args="-screen 0 1280x1024x24" ./build-linux/webview-demo --port 8080
```

> Linux 上默认构建本来就是带 stdout 的普通程序，日志直接打在终端里，
> **不需要** `WEBVIEW_DEMO_CONSOLE`（那个开关只影响 Windows 的 GUI 子系统）。

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
| 构建时 `-DWEBVIEW_DEMO_CONSOLE=ON` | 仅 Windows：编译成控制台程序，能直接看到日志（调试用） |

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

`GET /api/assets` 会列出被内嵌进可执行文件的资源（路径 / MIME / 字节数），可以直观看到
"前端资源就在二进制里"这件事；把 www 目录删掉，程序照样能跑。

## 开发辅助

```bash
# 任意 clang-format 19.x 均可
#   Windows: "C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Tools\Llvm\x64\bin\clang-format.exe"
#   Linux:   sudo apt install clang-format   (或复用 VS 之外任意 19.x)
clang-format -i src/main.cpp www/app.js
clang-format --dry-run --Werror src/main.cpp www/app.js   # 0 违规，可用作格式门禁

node --check www/app.js                                   # 前端语法检查
```

> `.clang-format` 里的 CSS 段对 `www/style.css` **不生效**——clang-format 不支持 CSS，
> 拿它去校验 style.css 会按 C++ 解析、报一堆假违规。

| 文件 | 作用 |
|---|---|
| `.clang-format` | C++20 与 JS 的格式化规则（当前代码实测 0 违规；`SortIncludes: false` 以保护 `WebAssets.h` 的包含顺序） |
| `.editorconfig` | UTF-8 + LF；C++ 4 空格、前端 2 空格、Markdown 保留行尾空格；IDE 自动生效 |
| `.gitattributes` | 仓库内统一 LF 存储，Windows 脚本保留 CRLF，二进制文件标记 |
| `.gitignore` | `build*/`、`.vs/`、MSVC 中间产物、WebView2 运行时数据目录 |
| `CMakePresets.json` | 四个 Windows 预设 + `linux` / `linux-debug`；用 `condition` 按平台过滤，`cmake --list-presets` 不会列出不适用的 |
| `.github/workflows/build.yml` | push / PR 时 `windows-latest`（当前镜像只有 VS2026）配置 + 编译 + 上传 exe；`ubuntu-22.04` 配置 + 编译 + **Xvfb 无头冒烟测试** + 上传 ELF；actions 用 v5 |
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

6. **（仅 Windows）无控制台窗口的 GUI 程序看不到 `printf`。**
   项目默认 `WIN32_EXECUTABLE`（不弹黑框），日志走 `OutputDebugStringA`（VS 输出窗口 / DebugView 可见）。
   调试阶段用 `-DWEBVIEW_DEMO_CONSOLE=ON` 更方便。
   注意 GUI 子系统下 MSVC 默认找 `WinMain`，本项目用 `target_link_options(... "/ENTRY:mainCRTStartup")`
   保留标准 `main()` 入口。Linux 上这个开关没有意义——默认构建就能在终端里看到日志。

7. **端口冲突**：默认用 `bind_to_any_port` 自动挑空闲端口，窗口打开的就是该端口，不需要硬编码。

8. **Linux 上的三个硬门槛**（都是实测踩出来的，缺一个都编不过）：
   - **CMake ≥ 3.20**：Ubuntu 20.04 自带的 3.16 会直接拒绝配置（22.04 的 3.22 正好够）；
   - **GCC ≥ 10**（或等价的 clang）：GCC 9 的 libstdc++ 没有 `<span>`，cpp-embedlib 会报
     `fatal error: span: No such file or directory`；
   - **`pkg-config` + GTK3/WebKitGTK 开发包**：webview 在 **configure 阶段**就
     `find_package(PkgConfig REQUIRED)`，缺了会立刻 FATAL_ERROR（不会拖到编译才报错）。
     无头机器想真的跑起来还需要 `xvfb`。

9. **原生句柄不是同一个东西。** `w.window()` 在 Windows 上返回 `HWND`，在 Linux 上返回
   `GtkWidget *`——本项目把它当不透明指针打印，名字由 `k_native_handle_name` 按平台切换。
   另外 `title` / `set_size` 的单位、窗口管理器行为在各平台也不完全一致。

10. **别在预设里写死 Visual Studio 版本**（这条是 CI 跑红了才发现的）。
    `x64` 预设原来写的 `"generator": "Visual Studio 17 2022"`，而 GitHub 的
    `windows-latest`（现在是 Windows Server 2025）**已经只剩 VS2026**，
    配置直接报 `Generator Visual Studio 17 2022 could not find any instance of Visual Studio.`
    ——只装了 VS2026 的用户会撞到一模一样的错。现在改成**不指定 generator**，
    跟随本机默认（最新）的 Visual Studio：本机只有 VS2022 时仍然落到 `Visual Studio 17 2022`，
    行为不变；装了更新的 VS 也不会再报错。代价是一台机器上有多个 VS 时用的是最新的那个，
    要指定就手写 `cmake -S . -B build -G "Visual Studio 17 2022" -A x64`。

## 接口一览

| 方法 | 路径 | 说明 |
|---|---|---|
| GET | `/`、`/style.css`、`/app.js` | 内嵌前端资源（html/css/js 的 MIME 自动识别） |
| GET | `/api/hello` | 返回问候语 + 服务器时间 |
| GET | `/api/time` | 本地时间 + 进程运行毫秒数 |
| GET | `/api/info` | cpp-httplib / webview / cpp-embedlib 版本、操作系统、PID、运行时长 |
| GET | `/api/assets` | 列出被 cpp-embedlib 内嵌的文件 |
| GET | `/api/echo?q=...` | 回显参数 |
| POST | `/api/add` | 表单 `a=..&b=..`，返回和 |

JS → C++ 绑定：`cppNativeEcho(text)`、`cppNativeHandle()`、`cppCloseWindow()`。

## 已验证的环境

**Windows**：Windows 10 22H2 (19045) x64 · VS2022 Community 17.14 · MSVC 14.44 · CMake 4.3.0 / VS 自带 3.31.6 ·
WebView2 Runtime 153.0.4234.48

**Linux**：Ubuntu 22.04.1 LTS（kernel 6.8，无显示器）· GCC 11.4.0 · CMake 3.22.1 · Ninja ·
WebKitGTK 2.50.4（`webkit2gtk-4.1`）+ GTK 3.24.33 + libsoup3 · Xvfb

依赖版本：cpp-httplib v0.38.0 · webview 0.12.0 · cpp-embedlib main

### Windows 实测记录

`build\Release\webview-demo.exe`，编译 0 警告，约 470 KB：

- 窗口类名 `webview`、客户区 1080×780、标题与 `set_title()` 一致 → 构造 / `set_size` / `set_title` 生效；
- 服务端日志里先出现 `GET /`、`GET /style.css`、`GET /app.js`（是**窗口自己**来拉的）→ `navigate` 生效、页面渲染成功；
- 全部 7 个接口 200，`POST /api/add` 返回 `{"a":3,"b":4,"sum":7}`；
- 注入脚本串起三个绑定：`cppNativeEcho` 返回 `{"source":"webview::bind → C++",…}`、
  `cppNativeHandle` 返回 `HWND = 0x…`、`cppCloseWindow` 之后进程自行退出（ExitCode=0）→
  `bind` / `resolve` / `window` / `terminate` 与 RAII 析构都正常。

### Linux 实测记录

在 Ubuntu 22.04（无显示器）上把 `git archive HEAD` 的干净快照跑了一遍，
**不改一行代码**就能配置 + 编译 + 运行：

- **configure 9 秒**，webview 自动选中 `webkit2gtk-4.1` 2.50.4 + `gtk+-3.0` 3.24.33 + libsoup3；
- **build 17 秒、0 警告**，产出 786,792 字节 ELF，链接 `libwebkit2gtk-4.1` / `libjavascriptcoregtk-4.1`；
  cpp-embedlib 在 Linux 上正常生成 `_data_index_html` / `_data_app_js` / `_data_style_css` → `libWebAssets.a`；
- 用 Xvfb 无头运行：7 个接口全 200，`POST /api/add` 返回 `{"a":12,"b":30,"sum":42}`；
  日志最前面是**窗口自己**发起的 `/`、`/style.css`、`/app.js`、`/api/info` → GTK + WebKitGTK 渲染链路通；
- JS→C++ 桥在 GTK 后端同样正常：`cppNativeEcho`（中文正常）、
  `cppNativeHandle` 返回 `GtkWidget * = 0x60C7D2C8C290`、`cppCloseWindow` 之后进程自行退出（exit code 0）；
- `localtime_r` 分支正确，返回真实本地时间。

> 同一份代码在 Windows 和 Linux 上的 `/api/info` 分别返回 `"os":"windows"` / `"os":"linux"`
> 和各自真实的 PID —— 这两处正是这次移植时修掉的平台相关 bug。

### GitHub Actions 实测记录

第一次把代码推上去时，Linux job 一次通过、Windows job 挂在 configure。
摸到的 runner 实情（`windows-latest` 现在长这样了）：

| 镜像 | 系统 | CMake | Visual Studio |
|---|---|---|---|
| `windows-latest` | Windows Server 2025 (26100) | 4.4.3 | **Enterprise 2026**（没有 VS2022） |
| `windows-2022` | Windows Server 2022 (20348) | 3.31.6 | Enterprise 2022 |
| `ubuntu-22.04` | Ubuntu 22.04 | — | — |

- **`ubuntu-22.04` job 全绿**：配置 + 编译 + `ldd` 校验 WebKitGTK + Xvfb 无头冒烟
  （7 个接口、内嵌资源、日志里确认是窗口自己拉的页面）；
- **Windows job 的报错**是 `Generator Visual Studio 17 2022 could not find any instance of Visual Studio.`
  → 见坑 10，预设里不再写死 VS 版本后即通过。
