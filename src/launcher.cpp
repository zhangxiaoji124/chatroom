#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <winsock2.h>
#include <ws2tcpip.h>
#include <shellapi.h>

#include <chrono>
#include <filesystem>
#include <string>
#include <thread>
#include <vector>

#pragma comment(lib, "ws2_32.lib")
#pragma comment(lib, "shell32.lib")

namespace {

bool is_server_online(int port = 8080) {
    WSADATA wsa_data;
    if (WSAStartup(MAKEWORD(2, 2), &wsa_data) != 0) {
        return false;
    }

    SOCKET sock = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (sock == INVALID_SOCKET) {
        WSACleanup();
        return false;
    }

    u_long mode = 1; // Non-blocking
    ioctlsocket(sock, FIONBIO, &mode);

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(static_cast<u_short>(port));
    inet_pton(AF_INET, "127.0.0.1", &addr.sin_addr);

    connect(sock, reinterpret_cast<sockaddr*>(&addr), sizeof(addr));

    fd_set write_set;
    FD_ZERO(&write_set);
    FD_SET(sock, &write_set);

    timeval tv{};
    tv.tv_sec = 0;
    tv.tv_usec = 200000; // 200ms

    int select_res = select(0, nullptr, &write_set, nullptr, &tv);
    bool online = (select_res > 0);

    closesocket(sock);
    WSACleanup();
    return online;
}

std::wstring get_module_directory() {
    wchar_t buffer[MAX_PATH];
    GetModuleFileNameW(nullptr, buffer, MAX_PATH);
    std::filesystem::path p(buffer);
    return p.parent_path().wstring();
}

bool start_server_process(const std::wstring& root_dir) {
    std::vector<std::wstring> candidates = {
        root_dir + L"\\chatroom_server_v2.exe",
        root_dir + L"\\build\\chatroom_server_v2.exe",
        root_dir + L"\\chatroom_server.exe",
        root_dir + L"\\build\\chatroom_server.exe",
        root_dir + L"\\..\\build\\chatroom_server_v2.exe",
        root_dir + L"\\..\\build\\chatroom_server.exe",
        root_dir + L"\\..\\chatroom_server.exe"
    };

    std::wstring server_exe;
    for (const auto& candidate : candidates) {
        if (std::filesystem::exists(candidate)) {
            server_exe = candidate;
            break;
        }
    }

    if (server_exe.empty()) {
        return false;
    }

    STARTUPINFOW si{};
    si.cb = sizeof(si);
    si.dwFlags = STARTF_USESHOWWINDOW;
    si.wShowWindow = SW_HIDE;

    PROCESS_INFORMATION pi{};

    std::filesystem::path working_dir = std::filesystem::path(server_exe).parent_path();
    if (working_dir.filename() == L"build") {
        working_dir = working_dir.parent_path();
    }

    std::wstring cmd = L"\"" + server_exe + L"\" 8080";
    std::vector<wchar_t> cmd_buf(cmd.begin(), cmd.end());
    cmd_buf.push_back(L'\0');

    BOOL created = CreateProcessW(
        nullptr,
        cmd_buf.data(),
        nullptr,
        nullptr,
        FALSE,
        CREATE_NO_WINDOW | DETACHED_PROCESS,
        nullptr,
        working_dir.c_str(),
        &si,
        &pi
    );

    if (created) {
        CloseHandle(pi.hProcess);
        CloseHandle(pi.hThread);
        return true;
    }
    return false;
}

std::wstring find_browser_app_executable() {
    std::vector<std::wstring> candidates = {
        L"C:\\Program Files (x86)\\Microsoft\\Edge\\Application\\msedge.exe",
        L"C:\\Program Files\\Microsoft\\Edge\\Application\\msedge.exe",
        L"C:\\Program Files\\Google\\Chrome\\Application\\chrome.exe",
        L"C:\\Program Files (x86)\\Google\\Chrome\\Application\\chrome.exe"
    };

    wchar_t local_app_data[MAX_PATH];
    if (GetEnvironmentVariableW(L"LOCALAPPDATA", local_app_data, MAX_PATH) > 0) {
        candidates.push_back(std::wstring(local_app_data) + L"\\Microsoft\\Edge\\Application\\msedge.exe");
        candidates.push_back(std::wstring(local_app_data) + L"\\Google\\Chrome\\Application\\chrome.exe");
    }

    for (const auto& path : candidates) {
        if (std::filesystem::exists(path)) {
            return path;
        }
    }
    return L"";
}

} // namespace

int WINAPI WinMain(HINSTANCE, HINSTANCE, LPSTR, int) {
    const std::wstring module_dir = get_module_directory();

    int target_port = 8080;
    if (is_server_online(8000)) {
        target_port = 8000;
    } else if (is_server_online(8080)) {
        target_port = 8080;
    } else {
        start_server_process(module_dir);

        // Wait up to 3 seconds for server to come online
        for (int i = 0; i < 30; ++i) {
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
            if (is_server_online(8000)) {
                target_port = 8000;
                break;
            }
            if (is_server_online(8080)) {
                target_port = 8080;
                break;
            }
        }
    }

    std::wstring browser = find_browser_app_executable();
    const std::wstring url = L"http://localhost:" + std::to_wstring(target_port);

    if (!browser.empty()) {
        std::wstring profile_dir = module_dir + L"\\..\\data\\desktop_profile";
        std::wstring args = L"--app=" + url +
                            L" --window-size=1200,820" +
                            L" --user-data-dir=\"" + profile_dir + L"\"";

        HINSTANCE res = ShellExecuteW(nullptr, L"open", browser.c_str(), args.c_str(), nullptr, SW_SHOWNORMAL);
        if (reinterpret_cast<INT_PTR>(res) > 32) {
            return 0;
        }
    }

    // Fallback to default browser
    ShellExecuteW(nullptr, L"open", url.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
    return 0;
}
