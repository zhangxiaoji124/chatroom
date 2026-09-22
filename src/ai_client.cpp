#include "chat/ai_client.hpp"

#include <windows.h>

#include <algorithm>
#include <atomic>
#include <cctype>
#include <fstream>
#include <optional>
#include <string_view>
#include <system_error>
#include <vector>

namespace {

constexpr DWORD kCurlProcessTimeoutMs = 17000;
std::atomic_uint64_t temp_file_sequence{0};

std::string trim_ascii(std::string value) {
    const auto is_space = [](unsigned char character) {
        return std::isspace(character) != 0;
    };
    const auto first = std::find_if_not(value.begin(), value.end(), is_space);
    if (first == value.end()) {
        return {};
    }
    const auto last = std::find_if_not(value.rbegin(), value.rend(), is_space)
                          .base();
    return std::string(first, last);
}

std::string curl_config_quote(std::string_view value) {
    std::string quoted;
    quoted.reserve(value.size() + 2);
    quoted.push_back('"');
    for (const char character : value) {
        if (character == '\\' || character == '"') {
            quoted.push_back('\\');
        }
        quoted.push_back(character);
    }
    quoted.push_back('"');
    return quoted;
}

class Handle {
public:
    Handle() = default;
    explicit Handle(HANDLE handle) : handle_(handle) {}
    ~Handle() { reset(); }

    Handle(const Handle&) = delete;
    Handle& operator=(const Handle&) = delete;

    Handle(Handle&& other) noexcept : handle_(other.release()) {}
    Handle& operator=(Handle&& other) noexcept {
        if (this != &other) {
            reset(other.release());
        }
        return *this;
    }

    HANDLE get() const { return handle_; }
    HANDLE release() {
        const HANDLE result = handle_;
        handle_ = nullptr;
        return result;
    }
    void reset(HANDLE handle = nullptr) {
        if (handle_ != nullptr && handle_ != INVALID_HANDLE_VALUE) {
            CloseHandle(handle_);
        }
        handle_ = handle;
    }

private:
    HANDLE handle_ = nullptr;
};

class TemporaryFilePair {
public:
    TemporaryFilePair() {
        const auto id = std::to_string(GetCurrentProcessId()) + "_" +
                        std::to_string(temp_file_sequence.fetch_add(1));
        const auto directory = std::filesystem::temp_directory_path();
        request_path = directory / ("chatroom_ai_request_" + id + ".json");
        response_path = directory / ("chatroom_ai_response_" + id + ".json");
    }

    ~TemporaryFilePair() {
        std::error_code error;
        std::filesystem::remove(request_path, error);
        error.clear();
        std::filesystem::remove(response_path, error);
    }

    std::filesystem::path request_path;
    std::filesystem::path response_path;
};

bool write_all(HANDLE pipe, std::string_view content) {
    std::size_t offset = 0;
    while (offset < content.size()) {
        const auto remaining = content.size() - offset;
        const DWORD chunk = static_cast<DWORD>(
            std::min<std::size_t>(remaining, MAXDWORD));
        DWORD written = 0;
        if (!WriteFile(pipe, content.data() + offset, chunk, &written, nullptr) ||
            written == 0) {
            return false;
        }
        offset += written;
    }
    return true;
}

std::string read_pipe(HANDLE pipe) {
    std::string output;
    char buffer[1024];
    DWORD read = 0;
    while (ReadFile(pipe, buffer, sizeof(buffer), &read, nullptr) && read != 0) {
        output.append(buffer, read);
    }
    return output;
}

std::optional<long> trailing_http_status(const std::string& output) {
    std::size_t end = output.size();
    while (end != 0 && std::isspace(static_cast<unsigned char>(output[end - 1]))) {
        --end;
    }
    if (end < 3) {
        return std::nullopt;
    }
    const std::string_view digits{output.data() + end - 3, 3};
    if (!std::all_of(digits.begin(), digits.end(), [](unsigned char character) {
            return std::isdigit(character) != 0;
        })) {
        return std::nullopt;
    }
    return static_cast<long>((digits[0] - '0') * 100 +
                             (digits[1] - '0') * 10 +
                             (digits[2] - '0'));
}

struct CurlResult {
    bool launched = false;
    bool timed_out = false;
    DWORD exit_code = 1;
    std::optional<long> http_status;
    std::string body;
    std::string diagnostic;
};

CurlResult run_curl(const std::string& api_key, const std::string& request_body) {
    CurlResult result;
    TemporaryFilePair files;

    {
        std::ofstream request(files.request_path,
                              std::ios::binary | std::ios::trunc);
        request.write(request_body.data(),
                      static_cast<std::streamsize>(request_body.size()));
        if (!request) {
            result.diagnostic = "cannot write curl request file";
            return result;
        }
    }

    SECURITY_ATTRIBUTES attributes{};
    attributes.nLength = sizeof(attributes);
    attributes.bInheritHandle = TRUE;

    HANDLE stdin_read_raw = nullptr;
    HANDLE stdin_write_raw = nullptr;
    HANDLE output_read_raw = nullptr;
    HANDLE output_write_raw = nullptr;
    if (!CreatePipe(&stdin_read_raw, &stdin_write_raw, &attributes, 0) ||
        !CreatePipe(&output_read_raw, &output_write_raw, &attributes, 0)) {
        if (stdin_read_raw != nullptr) CloseHandle(stdin_read_raw);
        if (stdin_write_raw != nullptr) CloseHandle(stdin_write_raw);
        if (output_read_raw != nullptr) CloseHandle(output_read_raw);
        if (output_write_raw != nullptr) CloseHandle(output_write_raw);
        result.diagnostic = "cannot create curl pipes";
        return result;
    }

    Handle stdin_read{stdin_read_raw};
    Handle stdin_write{stdin_write_raw};
    Handle output_read{output_read_raw};
    Handle output_write{output_write_raw};
    SetHandleInformation(stdin_write.get(), HANDLE_FLAG_INHERIT, 0);
    SetHandleInformation(output_read.get(), HANDLE_FLAG_INHERIT, 0);

    STARTUPINFOW startup{};
    startup.cb = sizeof(startup);
    startup.dwFlags = STARTF_USESTDHANDLES;
    startup.hStdInput = stdin_read.get();
    startup.hStdOutput = output_write.get();
    startup.hStdError = output_write.get();

    PROCESS_INFORMATION process{};
    std::vector<wchar_t> command{
        L'c', L'u', L'r', L'l', L'.', L'e', L'x', L'e', L' ', L'-', L'-',
        L'c', L'o', L'n', L'f', L'i', L'g', L' ', L'-', L'\0'};
    if (!CreateProcessW(nullptr, command.data(), nullptr, nullptr, TRUE,
                        CREATE_NO_WINDOW, nullptr, nullptr, &startup, &process)) {
        result.diagnostic = "cannot start curl.exe";
        return result;
    }
    result.launched = true;
    Handle process_handle{process.hProcess};
    Handle thread_handle{process.hThread};
    stdin_read.reset();
    output_write.reset();

    const std::string request_path = files.request_path.generic_string();
    const std::string response_path = files.response_path.generic_string();
    const std::string config =
        "url = \"https://api.deepseek.com/chat/completions\"\n"
        "request = \"POST\"\n"
        "header = " + curl_config_quote("Authorization: Bearer " + api_key) +
        "\nheader = \"Content-Type: application/json\"\n"
        "data-binary = " + curl_config_quote("@" + request_path) +
        "\noutput = " + curl_config_quote(response_path) +
        "\nconnect-timeout = 5\nmax-time = 15\n"
        "silent\nshow-error\nwrite-out = \"\\n%{http_code}\"\n";

    const bool config_written = write_all(stdin_write.get(), config);
    stdin_write.reset();
    if (!config_written) {
        TerminateProcess(process_handle.get(), 1);
        WaitForSingleObject(process_handle.get(), 1000);
        result.diagnostic = "cannot send configuration to curl";
        return result;
    }

    const DWORD wait_result =
        WaitForSingleObject(process_handle.get(), kCurlProcessTimeoutMs);
    if (wait_result == WAIT_TIMEOUT) {
        result.timed_out = true;
        TerminateProcess(process_handle.get(), 1);
        WaitForSingleObject(process_handle.get(), 1000);
    }
    GetExitCodeProcess(process_handle.get(), &result.exit_code);
    result.diagnostic = trim_ascii(read_pipe(output_read.get()));
    result.http_status = trailing_http_status(result.diagnostic);

    std::ifstream response(files.response_path, std::ios::binary);
    if (response) {
        result.body.assign(std::istreambuf_iterator<char>(response),
                           std::istreambuf_iterator<char>());
    }
    return result;
}

} // namespace

namespace chat {

std::string load_deepseek_api_key(const std::filesystem::path& path) {
    // 1. 优先读取系统环境变量 DEEPSEEK_API_KEY
    if (const char* env_key = std::getenv("DEEPSEEK_API_KEY")) {
        std::string key = trim_ascii(env_key);
        if (key.starts_with("sk-") && key.size() > 3) {
            return key;
        }
    }

    // 2. 读取配置文件 data/config.json
    try {
        std::error_code ec;
        if (std::filesystem::exists("data/config.json", ec)) {
            std::ifstream cfg_file("data/config.json");
            if (cfg_file.is_open()) {
                json cfg;
                cfg_file >> cfg;
                if (cfg.contains("ai_api_key") && cfg["ai_api_key"].is_string()) {
                    std::string key = trim_ascii(cfg["ai_api_key"].get<std::string>());
                    if (key.starts_with("sk-") && key.size() > 3) {
                        return key;
                    }
                }
            }
        }
    } catch (...) {
    }

    // 3. 兜底读取本地文件
    std::ifstream input(path);
    std::string key;
    if (!input || !std::getline(input, key)) {
        return {};
    }
    key = trim_ascii(std::move(key));
    if (!key.starts_with("sk-") || key.size() <= 3) {
        return {};
    }
    return key;
}

AiCompletionResult request_deepseek_completion(const std::string& api_key,
                                               const json& messages) {
    if (api_key.empty()) {
        return {false, {}, "DeepSeek API key is not configured"};
    }

    const json request{{"model", "deepseek-chat"},
                       {"messages", messages},
                       {"stream", false}};
    const CurlResult curl = run_curl(api_key, request.dump());
    if (!curl.launched) {
        return {false, {}, curl.diagnostic};
    }
    if (curl.timed_out) {
        return {false, {}, "DeepSeek request timed out"};
    }
    if (curl.exit_code != 0) {
        return {false, {}, "curl failed: " + curl.diagnostic};
    }
    if (!curl.http_status || *curl.http_status != 200) {
        const std::string status = curl.http_status
                                       ? std::to_string(*curl.http_status)
                                       : "unknown";
        return {false, {}, "DeepSeek returned HTTP " + status};
    }

    try {
        const json response = json::parse(curl.body);
        const auto choices = response.find("choices");
        if (choices == response.end() || !choices->is_array() ||
            choices->empty()) {
            return {false, {}, "DeepSeek response has no choices"};
        }
        const auto message = choices->at(0).find("message");
        if (message == choices->at(0).end() || !message->is_object()) {
            return {false, {}, "DeepSeek response has no message"};
        }
        const auto content = message->find("content");
        if (content == message->end() || !content->is_string()) {
            return {false, {}, "DeepSeek response has no text content"};
        }
        std::string reply = trim_ascii(content->get<std::string>());
        if (reply.empty()) {
            return {false, {}, "DeepSeek returned an empty response"};
        }
        return {true, std::move(reply), {}};
    } catch (const json::exception& error) {
        return {false, {}, std::string{"cannot parse DeepSeek response: "} +
                               error.what()};
    }
}

} // namespace chat
