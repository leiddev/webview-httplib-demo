// ---------------------------------------------------------------------------
//  main.cpp —— cpp-httplib + webview + cpp-embedlib 桌面应用示例
//
//  运行结构：
//
//     [ webview 原生窗口 ]                        [ 后台线程 ]
//       │  WebView2 载入 http://127.0.0.1:<port>     │
//       └──────────────► GET /            ────────►  cpp-embedlib 内嵌资源
//       │                GET /api/*       ────────►  cpp-httplib 处理
//       └── webview_bind ──────────────────────────► 直接调用 C++ 函数（不经 HTTP）
//
// ---------------------------------------------------------------------------
#include "WebAssets.h"  // cpp-embedlib 生成的：Web::FS

#include <cpp-embedlib-httplib.h>  // httplib::mount(svr, Web::FS)
#include <httplib.h>               // cpp-httplib

#include <webview/webview.h>  // webview 0.12 的 C API（header-only）

#include <cctype>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <ctime>
#include <string>
#include <string_view>
#include <thread>
#include <utility>

#ifdef _WIN32
#include <windows.h>
#endif

namespace {

// --------------------------------------------------------------- 全局状态 --
const auto g_started_at = std::chrono::steady_clock::now();
webview_t g_webview = nullptr;  // webview_bind 回调里要用它回传结果

// ---------------------------------------------------------------- 小工具 ----
void log_line(const std::string& msg) {
    const std::string line = msg + "\n";
    std::fputs(line.c_str(), stdout);
    std::fflush(stdout);
#ifdef _WIN32
    // GUI 子系统没有控制台，日志在 VS 输出窗口 / DebugView 里可见
    OutputDebugStringA(line.c_str());
#endif
}

[[noreturn]] void fatal(const std::string& msg) {
    log_line("[fatal] " + msg);
#ifdef _WIN32
    MessageBoxW(nullptr, std::wstring(msg.begin(), msg.end()).c_str(), L"webview-httplib-demo",
                MB_ICONERROR | MB_OK);
#endif
    std::exit(1);
}

std::string json_escape(std::string_view s) {
    std::string out;
    out.reserve(s.size() + 8);
    for (unsigned char c : s) {
        switch (c) {
        case '"': out += "\\\""; break;
        case '\\': out += "\\\\"; break;
        case '\n': out += "\\n"; break;
        case '\r': out += "\\r"; break;
        case '\t': out += "\\t"; break;
        default:
            if (c < 0x20) {
                char buf[8];
                std::snprintf(buf, sizeof(buf), "\\u%04x", c);
                out += buf;
            } else {
                out += static_cast<char>(c);
            }
        }
    }
    return out;
}

std::string local_time_string() {
    const std::time_t t = std::time(nullptr);
    std::tm tm{};
#ifdef _WIN32
    localtime_s(&tm, &t);
#else
    localtime_r(&t, &tm);
#endif
    char buf[32];
    std::strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M:%S", &tm);
    return buf;
}

long long uptime_ms() {
    return std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() -
                                                                 g_started_at)
        .count();
}

void reply_json(httplib::Response& res, std::string body, int status = 200) {
    res.status = status;
    res.set_content(std::move(body), "application/json; charset=utf-8");
}

// 取 webview_bind 收到的 JSON 数组参数里的第一个字符串
// （示例够用：req 形如 ["hello"]）
std::string first_json_string(std::string_view json) {
    const auto pos = json.find('"');
    if (pos == std::string_view::npos) { return {}; }
    std::string out;
    for (std::size_t i = pos + 1; i < json.size(); ++i) {
        const char c = json[i];
        if (c == '\\' && i + 1 < json.size()) {
            const char n = json[++i];
            switch (n) {
            case 'n': out += '\n'; break;
            case 't': out += '\t'; break;
            case 'r': out += '\r'; break;
            default: out += n; break;
            }
        } else if (c == '"') {
            break;
        } else {
            out += c;
        }
    }
    return out;
}

double form_number(const httplib::Request& req, const std::string& key, bool& ok) {
    const std::string value = req.get_param_value(key);
    if (value.empty()) {
        ok = false;
        return 0.0;
    }
    try {
        std::size_t used = 0;
        const double d = std::stod(value, &used);
        ok = (used == value.size());
        return d;
    } catch (...) {
        ok = false;
        return 0.0;
    }
}

std::string format_number(double v) {
    char buf[64];
    if (v == static_cast<double>(static_cast<long long>(v))) {
        std::snprintf(buf, sizeof(buf), "%lld", static_cast<long long>(v));
    } else {
        std::snprintf(buf, sizeof(buf), "%.6g", v);
    }
    return buf;
}

// ------------------------------------------------- webview_bind 回调（C++）--
// 这些函数由 JavaScript 直接调用，完全不走 HTTP。

// JS: cppNativeEcho("...") —— 把文本交给 C++ 处理后再返回
void on_native_echo(const char* id, const char* req, void*) {
    const std::string text = first_json_string(req ? req : "");
    const std::string result = "{\"source\":\"webview_bind → C++\",\"echo\":\"" +
                               json_escape(text) + "\",\"chars\":" + std::to_string(text.size()) +
                               ",\"upper\":\"" + json_escape([&] {
        std::string u = text;
        for (char& c : u) {
            c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
        }
        return u;
    }()) + "\",\"time\":\"" + local_time_string() +
                               "\"}";
    webview_return(g_webview, id, 0, result.c_str());
}

// JS: cppNativeHandle() —— 拿到原生 HWND，演示 JS 与原生窗口互操作
void on_native_handle(const char* id, const char*, void*) {
    const auto hwnd = reinterpret_cast<std::uintptr_t>(webview_get_window(g_webview));
    char buf[96];
    std::snprintf(buf, sizeof(buf), "\"HWND = 0x%llX（由 webview_get_window 取得）\"",
                  static_cast<unsigned long long>(hwnd));
    webview_return(g_webview, id, 0, buf);
}

// JS: cppCloseWindow() —— 关闭窗口（结束 webview_run 的消息循环）
void on_close_window(const char* id, const char*, void*) {
    webview_return(g_webview, id, 0, "\"closing...\"");
    webview_terminate(g_webview);
}

// ------------------------------------------------------------------ 路由 ----
void register_api(httplib::Server& svr) {
    // ---- GET /api/hello ---------------------------------------------------
    svr.Get("/api/hello", [](const httplib::Request&, httplib::Response& res) {
        reply_json(res, "{\"message\":\"你好，这是 C++ 后端（cpp-httplib）的问候\","
                        "\"from\":\"GET /api/hello\","
                        "\"time\":\"" +
                            local_time_string() + "\"}");
    });

    // ---- GET /api/time ----------------------------------------------------
    svr.Get("/api/time", [](const httplib::Request&, httplib::Response& res) {
        reply_json(res, "{\"local\":\"" + local_time_string() +
                            "\",\"uptime_ms\":" + std::to_string(uptime_ms()) + "}");
    });

    // ---- GET /api/echo?q=... ---------------------------------------------
    svr.Get("/api/echo", [](const httplib::Request& req, httplib::Response& res) {
        const std::string q = req.get_param_value("q");
        reply_json(res, "{\"you_said\":\"" + json_escape(q) +
                            "\",\"length\":" + std::to_string(q.size()) + "}");
    });

    // ---- POST /api/add （application/x-www-form-urlencoded: a=..&b=..）----
    svr.Post("/api/add", [](const httplib::Request& req, httplib::Response& res) {
        bool ok_a = false, ok_b = false;
        const double a = form_number(req, "a", ok_a);
        const double b = form_number(req, "b", ok_b);
        if (!ok_a || !ok_b) {
            reply_json(res, "{\"error\":\"需要表单参数 a 与 b，例如 a=12&b=30\"}", 400);
            return;
        }
        reply_json(res, "{\"a\":" + format_number(a) + ",\"b\":" + format_number(b) +
                            ",\"sum\":" + format_number(a + b) + "}");
    });

    // ---- GET /api/assets —— 列出被 cpp-embedlib 内嵌的资源 ----------------
    svr.Get("/api/assets", [](const httplib::Request&, httplib::Response& res) {
        std::string items;
        int files = 0;
        for (auto it = Web::FS.begin(); it != Web::FS.end(); ++it) {
            const auto entry = *it;
            if (entry.is_dir()) { continue; }
            ++files;
            const auto bytes = entry.bytes();
            if (!items.empty()) { items += ','; }
            items += "{\"path\":\"" + json_escape(entry.path()) + "\",\"mime\":\"" +
                     json_escape(entry.mime_type()) +
                     "\",\"bytes\":" + std::to_string(bytes ? bytes->size() : 0) + "}";
        }
        reply_json(res, "{\"count\":" + std::to_string(files) + ",\"files\":[" + items + "]}");
    });

    // ---- GET /api/info —— 三个库的版本信息 -------------------------------
    svr.Get("/api/info", [](const httplib::Request&, httplib::Response& res) {
        const webview_version_info_t* wv = webview_version();
        reply_json(res, "{\"cpp-httplib\":\"" CPPHTTPLIB_VERSION "\",\"webview\":\"" +
                            std::string(wv ? wv->version_number : "?") +
                            "\",\"cpp-embedlib\":\"main\",\"pid\":" +
                            std::to_string(
#ifdef _WIN32
                                GetCurrentProcessId()
#else
                                0
#endif
                                    ) +
                            ",\"uptime_ms\":" + std::to_string(uptime_ms()) + "}");
    });
}

// ------------------------------------------------------------------ 参数 ----
int parse_port_argument(int argc, char** argv) {
    for (int i = 1; i + 1 < argc; ++i) {
        if (std::string_view(argv[i]) == "--port") { return std::atoi(argv[i + 1]); }
    }
    return 0;  // 0 = 自动挑选空闲端口
}

}  // namespace

// ---------------------------------------------------------------------------
int main(int argc, char** argv) {
    // ---- 1. HTTP 服务器：cpp-httplib ------------------------------------
    httplib::Server svr;
    svr.set_read_timeout(5, 0);
    svr.set_payload_max_length(1024 * 1024);

    // 访问日志：控制台构建直接可见；GUI 构建可在 VS 输出窗口 / DebugView 中看到
    svr.set_logger([](const httplib::Request& req, const httplib::Response& res) {
        log_line("[http] " + req.method + " " + req.path + " -> " + std::to_string(res.status));
    });

    register_api(svr);

    // ---- 2. 前端资源：cpp-embedlib（挂到 "/"）----------------------------
    // 注意：API 路由要先注册，mount 作为兜底放在后面。
    httplib::mount(svr, Web::FS);

    const int requested_port = parse_port_argument(argc, argv);
    int port = 0;
    if (requested_port > 0) {
        if (!svr.bind_to_port("127.0.0.1", requested_port)) {
            fatal("端口 " + std::to_string(requested_port) + " 已被占用");
        }
        port = requested_port;
    } else {
        port = svr.bind_to_any_port("127.0.0.1");
    }
    if (port <= 0) { fatal("无法绑定本地端口"); }

    std::thread server_thread([&svr] { svr.listen_after_bind(); });

    const std::string url = "http://127.0.0.1:" + std::to_string(port) + "/";
    log_line("HTTP 服务已启动: " + url);

    // ---- 3. 原生窗口：webview（C API）-----------------------------------
    g_webview = webview_create(1 /* debug=1: 开启右键 DevTools */, nullptr);
    if (!g_webview) { fatal("webview_create 失败（请确认已安装 WebView2 运行时）"); }

    webview_set_title(g_webview, "cpp-httplib + webview + cpp-embedlib Demo");
    webview_set_size(g_webview, 1080, 780, WEBVIEW_HINT_NONE);

    // JS → C++ 的三个绑定
    webview_bind(g_webview, "cppNativeEcho", &on_native_echo, nullptr);
    webview_bind(g_webview, "cppNativeHandle", &on_native_handle, nullptr);
    webview_bind(g_webview, "cppCloseWindow", &on_close_window, nullptr);

    // 让窗口载入本机 HTTP 服务（同源，前端可直接 fetch("/api/...")）
    webview_navigate(g_webview, url.c_str());

    webview_run(g_webview);  // 阻塞：直到窗口关闭

    // ---- 4. 收尾 ---------------------------------------------------------
    webview_destroy(g_webview);
    g_webview = nullptr;

    svr.stop();
    if (server_thread.joinable()) { server_thread.join(); }

    log_line("已退出。");
    return 0;
}
