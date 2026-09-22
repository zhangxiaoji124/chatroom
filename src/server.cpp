#include "chat/server.hpp"
#include "chat/ai_client.hpp"

#include "httplib.h"

#include <algorithm>
#include <atomic>
#include <cctype>
#include <charconv>
#include <condition_variable>
#include <deque>
#include <filesystem>
#include <fstream>
#include <ctime>
#include <iomanip>
#include <iostream>
#include <memory>
#include <mutex>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <string_view>
#include <thread>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

namespace {

using WebSocket = httplib::ws::WebSocket;

struct AiTask {
    std::string nickname;
    std::string question;
};

struct RuntimeState {
    std::mutex mutex;
    std::unordered_map<std::string, WebSocket*> connections;
    std::unordered_map<std::string, std::unordered_set<std::string>> groups;
    std::unordered_map<std::string, std::unordered_set<std::string>> memberships;
    std::mutex ai_mutex;
    std::condition_variable ai_condition;
    std::deque<AiTask> ai_tasks;
    std::unordered_map<std::string,
                       std::deque<std::pair<std::string, std::string>>>
        ai_history;
    std::string ai_api_key;
    bool ai_stopping = false;
    std::thread ai_worker;
    int64_t started_at = chat::now_ms();
    chat::StorageManager* storage = nullptr;
};

// ChatServer's public header deliberately keeps cpp-httplib and the extension
// state out of the class layout. Keep both concrete objects in this translation
// unit instead.
std::mutex server_instances_mutex;
std::unordered_map<chat::ChatServer*, std::unique_ptr<httplib::Server>>
    server_instances;
std::unordered_map<chat::ChatServer*, std::unique_ptr<RuntimeState>>
    runtime_states;

// All readers and writers of data/messages.jsonl share this lock.
std::mutex message_file_mutex;

constexpr std::size_t kMaxAdminMessageLimit = 10000;
constexpr std::size_t kMaxHistoryMessageLimit = 1000;
constexpr std::size_t kMaxVoiceUploadBytes = 5U * 1024U * 1024U;
constexpr std::size_t kMaxImageUploadBytes = 10U * 1024U * 1024U;
constexpr std::string_view kVoiceUrlPrefix = "/data/audio/";
constexpr std::string_view kImageUrlPrefix = "/data/images/";
constexpr std::string_view kAiUnavailableMessage =
    "AI助手暂时无法响应，请稍后再试";
constexpr std::string_view kAiSystemPrompt =
    "你是简洁友好的中文客服助手，回答控制在150字内。";
const std::filesystem::path kDeepSeekApiKeyPath{
    R"(C:\Users\zhangxiaoji\Desktop\dp_apikey.txt)"};

std::atomic_uint64_t voice_file_sequence{0};
std::atomic_uint64_t image_file_sequence{0};

const std::unordered_map<std::string, std::string> stickers{
    {"happy", "😄"},
    {"sad", "😢"},
    {"like", "👍"},
    {"angry", "😠"},
    {"wow", "😮"},
};

std::string trim_ascii_whitespace(std::string_view value) {
    const auto first = std::find_if_not(
        value.begin(), value.end(),
        [](unsigned char character) { return std::isspace(character) != 0; });
    if (first == value.end()) {
        return {};
    }
    const auto last = std::find_if_not(
        value.rbegin(), value.rend(),
        [](unsigned char character) { return std::isspace(character) != 0; });
    return std::string(first, last.base());
}

std::optional<std::string> ai_question_from(const std::string& text) {
    const std::string trimmed = trim_ascii_whitespace(text);
    constexpr std::string_view prefixes[]{"@机器人", "@AI", "@ai"};
    for (const auto prefix : prefixes) {
        if (trimmed.starts_with(prefix)) {
            std::string question =
                trim_ascii_whitespace(std::string_view{trimmed}.substr(prefix.size()));
            if (question.empty()) {
                question = "你好，请介绍一下你自己。";
            }
            return question;
        }
    }
    return std::nullopt;
}

std::string normalized_media_type(const httplib::Request& req) {
    std::string content_type = req.get_header_value("Content-Type");
    const auto parameters = content_type.find(';');
    if (parameters != std::string::npos) {
        content_type.erase(parameters);
    }

    const auto first = content_type.find_first_not_of(" \t");
    if (first == std::string::npos) {
        return {};
    }
    const auto last = content_type.find_last_not_of(" \t");
    content_type = content_type.substr(first, last - first + 1);
    std::transform(content_type.begin(), content_type.end(),
                   content_type.begin(), [](unsigned char character) {
                       return static_cast<char>(std::tolower(character));
                   });
    return content_type;
}

std::optional<std::string> voice_extension(const std::string& content_type) {
    if (content_type == "audio/webm" ||
        content_type == "application/octet-stream") {
        return "webm";
    }
    if (content_type == "audio/ogg") {
        return "ogg";
    }
    if (content_type == "audio/mpeg" || content_type == "audio/mp3") {
        return "mp3";
    }
    if (content_type == "audio/mp4" || content_type == "audio/x-m4a") {
        return "m4a";
    }
    if (content_type == "audio/wav" || content_type == "audio/x-wav" ||
        content_type == "audio/wave") {
        return "wav";
    }
    return std::nullopt;
}

bool is_decimal(std::string_view value) {
    return !value.empty() &&
           std::all_of(value.begin(), value.end(), [](unsigned char character) {
               return std::isdigit(character) != 0;
           });
}

bool is_valid_voice_url(const std::string& url) {
    if (!url.starts_with(kVoiceUrlPrefix)) {
        return false;
    }

    const std::string_view filename{url.data() + kVoiceUrlPrefix.size(),
                                    url.size() - kVoiceUrlPrefix.size()};
    const auto underscore = filename.find('_');
    const auto dot = filename.find('.', underscore == std::string_view::npos
                                            ? 0
                                            : underscore + 1);
    if (underscore == std::string_view::npos || dot == std::string_view::npos ||
        filename.find_first_of("/\\?#%") != std::string_view::npos ||
        !is_decimal(filename.substr(0, underscore)) ||
        !is_decimal(filename.substr(underscore + 1, dot - underscore - 1))) {
        return false;
    }

    const std::string_view extension = filename.substr(dot + 1);
    if (extension != "webm" && extension != "ogg" && extension != "mp3" &&
        extension != "m4a" && extension != "wav") {
        return false;
    }

    std::error_code error;
    const auto path = std::filesystem::path{"data"} / "audio" /
                      std::string{filename};
    return std::filesystem::is_regular_file(path, error) && !error;
}

std::optional<std::string> image_extension(const std::string& content_type) {
    if (content_type == "image/png" || content_type == "image/x-png") {
        return "png";
    }
    if (content_type == "image/jpeg" || content_type == "image/jpg" ||
        content_type == "image/pjpeg") {
        return "jpg";
    }
    if (content_type == "image/gif") {
        return "gif";
    }
    if (content_type == "image/webp") {
        return "webp";
    }
    if (content_type == "image/svg+xml" || content_type == "image/svg") {
        return "svg";
    }
    if (content_type == "image/bmp" || content_type == "image/x-ms-bmp") {
        return "bmp";
    }
    return std::nullopt;
}

bool is_valid_image_url(const std::string& url) {
    if (!url.starts_with(kImageUrlPrefix)) {
        return false;
    }

    const std::string_view filename{url.data() + kImageUrlPrefix.size(),
                                    url.size() - kImageUrlPrefix.size()};
    const auto underscore = filename.find('_');
    const auto dot = filename.find('.', underscore == std::string_view::npos
                                            ? 0
                                            : underscore + 1);
    if (underscore == std::string_view::npos || dot == std::string_view::npos ||
        filename.find_first_of("/\\?#%") != std::string_view::npos ||
        !is_decimal(filename.substr(0, underscore)) ||
        !is_decimal(filename.substr(underscore + 1, dot - underscore - 1))) {
        return false;
    }

    const std::string_view extension = filename.substr(dot + 1);
    if (extension != "png" && extension != "jpg" && extension != "jpeg" &&
        extension != "gif" && extension != "webp" && extension != "svg" &&
        extension != "bmp") {
        return false;
    }

    std::error_code error;
    const auto path = std::filesystem::path{"data"} / "images" /
                      std::string{filename};
    return std::filesystem::is_regular_file(path, error) && !error;
}


httplib::Server& server_for(chat::ChatServer* owner) {
    std::lock_guard<std::mutex> lock(server_instances_mutex);
    const auto it = server_instances.find(owner);
    if (it == server_instances.end()) {
        throw std::logic_error("ChatServer has no HTTP server instance");
    }
    return *it->second;
}

RuntimeState& runtime_for(chat::ChatServer* owner) {
    std::lock_guard<std::mutex> lock(server_instances_mutex);
    const auto it = runtime_states.find(owner);
    if (it == runtime_states.end()) {
        throw std::logic_error("ChatServer has no runtime state");
    }
    return *it->second;
}

void append_message(const chat::json& message, chat::StorageManager* storage = nullptr, int is_read = 1) {
    if (storage) {
        storage->save_message(message, is_read);
    }

    std::lock_guard<std::mutex> lock(message_file_mutex);
    const std::filesystem::path data_dir{"data"};
    std::filesystem::create_directories(data_dir);

    std::ofstream output(data_dir / "messages.jsonl", std::ios::app);
    if (!output) {
        std::cerr << "Failed to open data/messages.jsonl for appending\n";
        return;
    }
    output << message.dump() << '\n';
}

void enqueue_ai_task(RuntimeState& state, std::string nickname,
                     std::string question) {
    {
        std::lock_guard<std::mutex> lock(state.ai_mutex);
        if (state.ai_stopping) {
            return;
        }
        state.ai_tasks.push_back(
            AiTask{std::move(nickname), std::move(question)});
    }
    state.ai_condition.notify_one();
}

chat::json ai_messages_for(RuntimeState& state, const AiTask& task) {
    chat::json messages = chat::json::array();
    messages.push_back({{"role", "system"},
                        {"content", std::string{kAiSystemPrompt}}});
    const auto history_it = state.ai_history.find(task.nickname);
    if (history_it != state.ai_history.end()) {
        for (const auto& [question, answer] : history_it->second) {
            messages.push_back({{"role", "user"}, {"content", question}});
            messages.push_back(
                {{"role", "assistant"}, {"content", answer}});
        }
    }
    messages.push_back({{"role", "user"}, {"content", task.question}});
    return messages;
}

void run_ai_worker(chat::ChatServer* owner, RuntimeState& state) {
    while (true) {
        AiTask task;
        {
            std::unique_lock<std::mutex> lock(state.ai_mutex);
            state.ai_condition.wait(lock, [&state] {
                return state.ai_stopping || !state.ai_tasks.empty();
            });
            if (state.ai_stopping) {
                return;
            }
            task = std::move(state.ai_tasks.front());
            state.ai_tasks.pop_front();
        }

        try {
            if (state.ai_api_key.empty()) {
                owner->ws().broadcast_raw(
                    chat::json{{"type", "system"},
                               {"msg", "AI 未配置，请联系管理员"}}
                        .dump());
                continue;
            }

            const auto result = chat::request_deepseek_completion(
                state.ai_api_key, ai_messages_for(state, task));
            if (!result.ok) {
                std::cerr << "DeepSeek request failed: " << result.diagnostic
                          << '\n';
                owner->ws().broadcast_raw(
                    chat::json{{"type", "system"},
                               {"msg", std::string{kAiUnavailableMessage}}}
                        .dump());
                continue;
            }

            auto& history = state.ai_history[task.nickname];
            history.emplace_back(task.question, result.reply);
            constexpr std::size_t kMaxHistoryRounds = 6;
            while (history.size() > kMaxHistoryRounds) {
                history.pop_front();
            }

            const chat::ChatMessage reply{"AI助手", result.reply,
                                           chat::now_ms()};
            append_message(reply.to_json(), state.storage, 1);
            std::cout << "[AI] AI助手: " << reply.text << '\n';
            owner->ws().broadcast(reply);
        } catch (const std::exception& error) {
            std::cerr << "AI worker error: " << error.what() << '\n';
            owner->ws().broadcast_raw(
                chat::json{{"type", "system"},
                           {"msg", std::string{kAiUnavailableMessage}}}
                    .dump());
        } catch (...) {
            std::cerr << "Unknown AI worker error\n";
            owner->ws().broadcast_raw(
                chat::json{{"type", "system"},
                           {"msg", std::string{kAiUnavailableMessage}}}
                    .dump());
        }
    }
}

void stop_ai_worker(RuntimeState& state) {
    {
        std::lock_guard<std::mutex> lock(state.ai_mutex);
        state.ai_stopping = true;
        state.ai_tasks.clear();
    }
    state.ai_condition.notify_all();
    if (state.ai_worker.joinable()) {
        state.ai_worker.join();
    }
}

std::size_t requested_message_limit(const httplib::Request& req) {
    if (!req.has_param("limit")) {
        return 100;
    }

    const std::string value = req.get_param_value("limit");
    std::size_t limit = 0;
    const auto [end, error] =
        std::from_chars(value.data(), value.data() + value.size(), limit);
    if (error != std::errc{} || end != value.data() + value.size()) {
        return 100;
    }
    return std::min(limit, kMaxAdminMessageLimit);
}

chat::json recent_messages(std::size_t limit,
                           const std::string& channel = "",
                           const std::string& group = "",
                           chat::StorageManager* storage = nullptr) {
    if (storage) {
        auto list = storage->get_recent_messages(limit, channel, group);
        chat::json result = chat::json::array();
        for (auto& item : list) {
            result.push_back(std::move(item));
        }
        return result;
    }

    std::lock_guard<std::mutex> lock(message_file_mutex);
    std::ifstream input(std::filesystem::path{"data"} / "messages.jsonl");
    std::deque<chat::json> recent;
    std::string line;

    while (std::getline(input, line)) {
        if (line.empty()) {
            continue;
        }
        try {
            chat::json message = chat::json::parse(line);
            if (!group.empty()) {
                if (message.value("group", "") != group) {
                    continue;
                }
            } else if (channel == "global") {
                if (message.contains("group") || message.value("type", "") == "dm") {
                    continue;
                }
            }
            if (limit != 0) {
                recent.push_back(std::move(message));
                if (recent.size() > limit) {
                    recent.pop_front();
                }
            }
        } catch (const chat::json::exception&) {
            // A malformed historical line should not make the admin API fail.
        }
    }

    chat::json result = chat::json::array();
    for (auto& message : recent) {
        result.push_back(std::move(message));
    }
    return result;
}

std::size_t message_count(chat::StorageManager* storage = nullptr) {
    if (storage) {
        return storage->message_count();
    }
    std::lock_guard<std::mutex> lock(message_file_mutex);
    std::ifstream input(std::filesystem::path{"data"} / "messages.jsonl");
    std::size_t count = 0;
    std::string line;
    while (std::getline(input, line)) {
        ++count;
    }
    return count;
}

void record_connection(RuntimeState& state, const std::string& nickname,
                       WebSocket* socket) {
    std::lock_guard<std::mutex> lock(state.mutex);
    state.connections[nickname] = socket;
}

void remove_connection(RuntimeState& state, const std::string& nickname) {
    std::lock_guard<std::mutex> lock(state.mutex);
    state.connections.erase(nickname);

    const auto memberships_it = state.memberships.find(nickname);
    if (memberships_it == state.memberships.end()) {
        return;
    }

    for (const auto& group_name : memberships_it->second) {
        const auto group_it = state.groups.find(group_name);
        if (group_it == state.groups.end()) {
            continue;
        }
        group_it->second.erase(nickname);
        if (group_it->second.empty()) {
            state.groups.erase(group_it);
        }
    }
    state.memberships.erase(memberships_it);
}

// The state lock remains held while sending. This ensures a WebSocket pointer
// cannot be removed and leave its handler scope during a targeted send.
void broadcast_group_locked(RuntimeState& state, chat::WsHandler& handler,
                            const std::string& group_name,
                            const std::string& payload) {
    const auto group_it = state.groups.find(group_name);
    if (group_it == state.groups.end()) {
        return;
    }
    for (const auto& member : group_it->second) {
        handler.send_raw(member, payload);
    }
}

} // namespace

namespace chat {

ChatServer::ChatServer(int port, const std::string& node_id, const std::vector<std::string>& peers)
    : port_(port), running_(false), storage_("data/chatroom.db") {
    storage_.init();
    storage_.migrate_from_json_files("data/users.json", "data/messages.jsonl");
    users_.set_storage(&storage_);

    cluster_.init_node(node_id, "127.0.0.1", port);
    for (const auto& p : peers) {
        cluster_.add_peer(p);
    }

    cluster_.set_event_dispatcher([this](const std::string& type, const json& data) {
        auto& state = runtime_for(this);
        if (type == "chat") {
            ws_.broadcast_raw(data.dump());
            append_message(data, &storage_, 1);
        } else if (type == "group_chat") {
            std::string group = data.value("group", "");
            if (!group.empty()) {
                std::lock_guard<std::mutex> lock(state.mutex);
                broadcast_group_locked(state, ws_, group, data.dump());
                append_message(data, &storage_, 1);
            }
        } else if (type == "dm") {
            std::string to = data.value("to", "");
            if (!to.empty()) {
                if (ws_.is_registered(to)) {
                    ws_.send_raw(to, data.dump());
                }
                storage_.save_message(data, 0);
            }
        } else if (type == "recall") {
            std::string group = data.value("group", "");
            std::string to = data.value("to", "");
            if (!group.empty()) {
                std::lock_guard<std::mutex> lock(state.mutex);
                broadcast_group_locked(state, ws_, group, data.dump());
            } else if (!to.empty()) {
                if (ws_.is_registered(to)) {
                    ws_.send_raw(to, data.dump());
                }
                std::string from = data.value("from", "");
                if (from != to && ws_.is_registered(from)) {
                    ws_.send_raw(from, data.dump());
                }
            } else {
                ws_.broadcast_raw(data.dump());
            }
            std::string msg_id = data.value("msg_id", "");
            std::string from = data.value("from", "");
            std::string err;
            json out;
            storage_.recall_message(msg_id, from, true, err, out);
        } else if (type == "reaction") {
            std::string group = data.value("group", "");
            std::string to = data.value("to", "");
            if (!group.empty()) {
                std::lock_guard<std::mutex> lock(state.mutex);
                broadcast_group_locked(state, ws_, group, data.dump());
            } else if (!to.empty()) {
                if (ws_.is_registered(to)) {
                    ws_.send_raw(to, data.dump());
                }
                std::string from = data.value("from", "");
                if (from != to && ws_.is_registered(from)) {
                    ws_.send_raw(from, data.dump());
                }
            } else {
                ws_.broadcast_raw(data.dump());
            }
        } else if (type == "voice" || type == "image") {
            std::string group = data.value("group", "");
            std::string to = data.value("to", "");
            if (!group.empty()) {
                std::lock_guard<std::mutex> lock(state.mutex);
                broadcast_group_locked(state, ws_, group, data.dump());
            } else if (!to.empty()) {
                if (ws_.is_registered(to)) {
                    ws_.send_raw(to, data.dump());
                }
            } else {
                ws_.broadcast_raw(data.dump());
            }
            append_message(data, &storage_, 1);
        } else if (type == "broadcast") {
            ws_.broadcast_raw(data.dump());
        }
    });

    {
        std::lock_guard<std::mutex> lock(server_instances_mutex);
        server_instances.emplace(this, std::make_unique<httplib::Server>());
        auto state = std::make_unique<RuntimeState>();
        state->ai_api_key = load_deepseek_api_key(kDeepSeekApiKeyPath);
        state->storage = &storage_;
        runtime_states.emplace(this, std::move(state));
    }

    try {
        setup_routes();
        auto& state = runtime_for(this);
        if (state.ai_api_key.empty()) {
            std::cerr << "Warning: DeepSeek API key file is missing or empty; "
                         "the chat server will continue without AI replies\n";
        }
        state.ai_worker = std::thread([this, &state] {
            run_ai_worker(this, state);
        });
    } catch (...) {
        std::lock_guard<std::mutex> lock(server_instances_mutex);
        server_instances.erase(this);
        runtime_states.erase(this);
        throw;
    }
}

ChatServer::~ChatServer() {
    std::unique_ptr<httplib::Server> server;
    RuntimeState* state = nullptr;
    {
        std::lock_guard<std::mutex> lock(server_instances_mutex);
        const auto state_it = runtime_states.find(this);
        if (state_it != runtime_states.end()) {
            state = state_it->second.get();
        }
        const auto it = server_instances.find(this);
        if (it != server_instances.end()) {
            server = std::move(it->second);
            server_instances.erase(it);
        }
    }

    cluster_.stop();

    if (state != nullptr) {
        stop_ai_worker(*state);
    }

    if (server) {
        server->stop();
        server.reset();
    }
    {
        std::lock_guard<std::mutex> lock(server_instances_mutex);
        runtime_states.erase(this);
    }
    running_ = false;
}

void ChatServer::setup_routes() {
    cluster_.start();

    ws_.set_message_sink(
        [this](const ChatMessage& msg) { persist_and_log(msg); });

    auto& server = server_for(this);

    const std::filesystem::path audio_dir =
        std::filesystem::path{"data"} / "audio";
    std::error_code directory_error;
    std::filesystem::create_directories(audio_dir, directory_error);
    if (directory_error ||
        !server.set_mount_point("/data/audio", audio_dir.string())) {
        throw std::runtime_error("Failed to create or mount data/audio");
    }

    const std::filesystem::path images_dir =
        std::filesystem::path{"data"} / "images";
    std::error_code images_dir_error;
    std::filesystem::create_directories(images_dir, images_dir_error);
    if (images_dir_error ||
        !server.set_mount_point("/data/images", images_dir.string())) {
        throw std::runtime_error("Failed to create or mount data/images");
    }

    server.set_mount_point("/", "web");
    server.set_file_extension_and_mimetype_mapping("webm", "audio/webm");
    server.set_file_extension_and_mimetype_mapping("ogg", "audio/ogg");
    server.set_file_extension_and_mimetype_mapping("mp3", "audio/mpeg");
    server.set_file_extension_and_mimetype_mapping("m4a", "audio/mp4");
    server.set_file_extension_and_mimetype_mapping("wav", "audio/wav");

    server.set_file_extension_and_mimetype_mapping("png", "image/png");
    server.set_file_extension_and_mimetype_mapping("jpg", "image/jpeg");
    server.set_file_extension_and_mimetype_mapping("jpeg", "image/jpeg");
    server.set_file_extension_and_mimetype_mapping("gif", "image/gif");
    server.set_file_extension_and_mimetype_mapping("webp", "image/webp");
    server.set_file_extension_and_mimetype_mapping("svg", "image/svg+xml");
    server.set_file_extension_and_mimetype_mapping("bmp", "image/bmp");

    server.set_pre_routing_handler([](const httplib::Request& req, httplib::Response& res) {
        if (req.path.ends_with(".js") || req.path.ends_with(".css") || req.path == "/") {
            res.set_header("Cache-Control", "no-cache, no-store, must-revalidate");
        }
        return httplib::Server::HandlerResponse::Unhandled;
    });

    server.set_payload_max_length(kMaxImageUploadBytes);

    server.Post("/api/upload/image", [images_dir](const httplib::Request& req,
                                                  httplib::Response& res) {
        if (req.body.size() > kMaxImageUploadBytes) {
            res.status = httplib::StatusCode::PayloadTooLarge_413;
            res.set_content(json{{"error", "image upload exceeds 10 MiB"}}.dump(),
                            "application/json");
            return;
        }
        if (req.body.empty()) {
            res.status = httplib::StatusCode::BadRequest_400;
            res.set_content(json{{"error", "image upload is empty"}}.dump(),
                            "application/json");
            return;
        }

        const std::string content_type = normalized_media_type(req);
        const auto extension = image_extension(content_type);
        if (!extension) {
            res.status = httplib::StatusCode::UnsupportedMediaType_415;
            res.set_content(json{{"error", "unsupported image content type"}}
                                .dump(),
                            "application/json");
            return;
        }

        const std::string filename =
            std::to_string(now_ms()) + "_" +
            std::to_string(image_file_sequence.fetch_add(1)) + "." +
            *extension;
        const std::filesystem::path output_path = images_dir / filename;
        std::ofstream output(output_path, std::ios::binary | std::ios::trunc);
        if (!output) {
            res.status = httplib::StatusCode::InternalServerError_500;
            res.set_content(json{{"error", "failed to store image upload"}}
                                .dump(),
                            "application/json");
            return;
        }
        output.write(req.body.data(), static_cast<std::streamsize>(req.body.size()));
        output.close();
        if (!output) {
            std::error_code remove_error;
            std::filesystem::remove(output_path, remove_error);
            res.status = httplib::StatusCode::InternalServerError_500;
            res.set_content(json{{"error", "failed to store image upload"}}
                                .dump(),
                            "application/json");
            return;
        }

        const json result{{"url", std::string{kImageUrlPrefix} + filename},
                          {"size", req.body.size()},
                          {"format", content_type}};
        res.set_content(result.dump(), "application/json");
    });

    server.Post("/api/upload/voice", [audio_dir](const httplib::Request& req,
                                                  httplib::Response& res) {
        if (req.body.size() > kMaxVoiceUploadBytes) {
            res.status = httplib::StatusCode::PayloadTooLarge_413;
            res.set_content(json{{"error", "voice upload exceeds 5 MiB"}}.dump(),
                            "application/json");
            return;
        }
        if (req.body.empty()) {
            res.status = httplib::StatusCode::BadRequest_400;
            res.set_content(json{{"error", "voice upload is empty"}}.dump(),
                            "application/json");
            return;
        }

        const std::string content_type = normalized_media_type(req);
        const auto extension = voice_extension(content_type);
        if (!extension) {
            res.status = httplib::StatusCode::UnsupportedMediaType_415;
            res.set_content(json{{"error", "unsupported audio content type"}}
                                .dump(),
                            "application/json");
            return;
        }

        const std::string filename =
            std::to_string(now_ms()) + "_" +
            std::to_string(voice_file_sequence.fetch_add(1)) + "." +
            *extension;
        const std::filesystem::path output_path = audio_dir / filename;
        std::ofstream output(output_path, std::ios::binary | std::ios::trunc);
        if (!output) {
            res.status = httplib::StatusCode::InternalServerError_500;
            res.set_content(json{{"error", "failed to store voice upload"}}
                                .dump(),
                            "application/json");
            return;
        }
        output.write(req.body.data(), static_cast<std::streamsize>(req.body.size()));
        output.close();
        if (!output) {
            std::error_code remove_error;
            std::filesystem::remove(output_path, remove_error);
            res.status = httplib::StatusCode::InternalServerError_500;
            res.set_content(json{{"error", "failed to store voice upload"}}
                                .dump(),
                            "application/json");
            return;
        }

        const json result{{"url", std::string{kVoiceUrlPrefix} + filename},
                          {"size", req.body.size()},
                          {"format", content_type}};
        res.set_content(result.dump(), "application/json");
    });

    server.Get("/api/status", [this](const httplib::Request&,
                                      httplib::Response& res) {
        const json status{{"status", "ok"},
                          {"online_count", users_.online_count()},
                          {"ws_connections", ws_.connection_count()}};
        res.set_content(status.dump(), "application/json");
    });

    server.Get("/api/stickers", [](const httplib::Request&,
                                    httplib::Response& res) {
        const json available = json::array({
            {{"key", "happy"}, {"emoji", "😄"}},
            {{"key", "sad"}, {"emoji", "😢"}},
            {{"key", "like"}, {"emoji", "👍"}},
            {{"key", "angry"}, {"emoji", "😠"}},
            {{"key", "wow"}, {"emoji", "😮"}},
        });
        res.set_content(json{{"stickers", available}}.dump(),
                        "application/json");
    });

    server.Get("/api/admin/stats", [this](const httplib::Request&,
                                           httplib::Response& res) {
        json online = json::array();
        for (const auto& user : users_.online_users()) {
            online.push_back({{"nickname", user.nickname},
                              {"address", user.address},
                              {"connect_ts", user.connect_ts},
                              {"online", true}});
        }

        const auto& state = runtime_for(this);
        const json stats{{"status", "ok"},
                         {"users", std::move(online)},
                         {"message_count", message_count(&storage_)},
                         {"server_uptime_ms", now_ms() - state.started_at}};
        res.set_content(stats.dump(), "application/json");
    });

    server.Get("/api/admin/messages", [this](const httplib::Request& req,
                                          httplib::Response& res) {
        res.set_content(recent_messages(requested_message_limit(req), "", "", &storage_).dump(),
                        "application/json");
    });

    server.Post("/api/admin/broadcast", [this](const httplib::Request& req,
                                               httplib::Response& res) {
        try {
            const auto body = json::parse(req.body);
            const std::string text = body.value("message", "");
            if (text.empty()) {
                res.status = httplib::StatusCode::BadRequest_400;
                res.set_content(json{{"error", "broadcast message is empty"}}.dump(),
                                "application/json");
                return;
            }
            const json payload{
                {"type", "system"},
                {"msg", "📢 [系统公告] " + text},
                {"ts", now_ms()}
            };
            ws_.broadcast_raw(payload.dump());
            cluster_.broadcast_event("broadcast", payload);
            res.set_content(json{{"status", "ok"}}.dump(), "application/json");
        } catch (...) {
            res.status = httplib::StatusCode::BadRequest_400;
            res.set_content(json{{"error", "invalid json"}}.dump(), "application/json");
        }
    });

    server.Post("/api/admin/kick", [this](const httplib::Request& req,
                                          httplib::Response& res) {
        try {
            const auto body = json::parse(req.body);
            const std::string target = body.value("nickname", "");
            if (target.empty() || !users_.is_online(target)) {
                res.status = httplib::StatusCode::BadRequest_400;
                res.set_content(json{{"error", "user not found or not online"}}.dump(),
                                "application/json");
                return;
            }
            const json notice{
                {"type", "error"},
                {"msg", "你已被管理员移出聊天室"}
            };
            ws_.send_raw(target, notice.dump());
            users_.user_offline(target);
            ws_.unregister_ws(target);

            const json bcast{
                {"type", "system"},
                {"msg", "⚠️ " + target + " 已被管理员移出聊天室"},
                {"ts", now_ms()}
            };
            ws_.broadcast_raw(bcast.dump());
            cluster_.broadcast_event("broadcast", bcast);
            res.set_content(json{{"status", "ok"}}.dump(), "application/json");
        } catch (...) {
            res.status = httplib::StatusCode::BadRequest_400;
            res.set_content(json{{"error", "invalid json"}}.dump(), "application/json");
        }
    });

    server.Get("/api/history", [this](const httplib::Request& req,
                                      httplib::Response& res) {
        const auto limit =
            std::min(requested_message_limit(req), kMaxHistoryMessageLimit);
        const std::string channel = req.has_param("channel") ? req.get_param_value("channel") : "";
        const std::string group = req.has_param("group") ? req.get_param_value("group") : "";
        res.set_content(recent_messages(limit, channel, group, &storage_).dump(),
                        "application/json");
    });

    server.Get("/api/health", [this](const httplib::Request&,
                                     httplib::Response& res) {
        auto& state = runtime_for(this);
        const int64_t uptime_s = (now_ms() - state.started_at) / 1000;
        json body{
            {"status", "ok"},
            {"uptime_seconds", uptime_s},
            {"online_users", users_.online_count()},
            {"registered_accounts", users_.account_count()},
            {"active_connections", ws_.connection_count()},
            {"stored_messages", storage_.message_count()},
            {"ai_configured", !state.ai_api_key.empty()}
        };
        res.set_content(body.dump(), "application/json");
    });

    server.Get("/api/users", [this](const httplib::Request&,
                                    httplib::Response& res) {
        auto accounts = users_.all_accounts();
        json arr = json::array();
        for (const auto& acc : accounts) {
            const bool online = users_.is_online(acc.username);
            arr.push_back({
                {"username", acc.username},
                {"online", online},
                {"created_at", acc.created_at},
                {"last_login", acc.last_login},
                {"recovery_key", acc.recovery_key}
            });
        }
        res.set_content(arr.dump(), "application/json");
    });

    server.Post("/api/auth/reset_password", [this](const httplib::Request& req,
                                                   httplib::Response& res) {
        const std::string client_ip = req.remote_addr;
        if (!rate_limiter_.allow_login_attempt(client_ip)) {
            const int64_t remaining = rate_limiter_.get_login_locked_remaining_seconds(client_ip);
            res.status = httplib::StatusCode::TooManyRequests_429;
            res.set_content(json{
                {"error", "尝试重置密码过于频繁，已被风控临时锁定 " + std::to_string(remaining) + " 秒"}
            }.dump(), "application/json");
            return;
        }

        try {
            json body = json::parse(req.body);
            const std::string username = body.value("username", "");
            const std::string recovery_key = body.value("recovery_key", "");
            const std::string new_password = body.value("new_password", "");

            if (username.empty() || recovery_key.empty() || new_password.empty()) {
                res.status = httplib::StatusCode::BadRequest_400;
                res.set_content(json{{"error", "用户名、密保恢复码和新密码均不能为空"}}.dump(), "application/json");
                return;
            }

            std::string err;
            if (!users_.verify_and_reset_password(username, recovery_key, new_password, err)) {
                rate_limiter_.record_login_failure(client_ip);
                res.status = httplib::StatusCode::BadRequest_400;
                res.set_content(json{{"error", err}}.dump(), "application/json");
                return;
            }

            rate_limiter_.reset_login_failure(client_ip);
            std::cout << "[AUTH] Password reset via recovery_key for user: " << username << '\n';
            res.set_content(json{{"status", "ok"}, {"msg", "密码重置成功，请使用新密码登录"}}.dump(), "application/json");
        } catch (...) {
            res.status = httplib::StatusCode::BadRequest_400;
            res.set_content(json{{"error", "无效的请求格式"}}.dump(), "application/json");
        }
    });

    server.Post("/api/admin/reset_password", [this](const httplib::Request& req,
                                                    httplib::Response& res) {
        try {
            json body = json::parse(req.body);
            const std::string username = body.value("username", "");
            const std::string new_password = body.value("new_password", "");
            if (username.empty() || new_password.empty()) {
                res.status = httplib::StatusCode::BadRequest_400;
                res.set_content(json{{"error", "缺少用户名或新密码"}}.dump(), "application/json");
                return;
            }
            std::string err;
            if (!users_.reset_password(username, new_password, err)) {
                res.status = httplib::StatusCode::BadRequest_400;
                res.set_content(json{{"error", err}}.dump(), "application/json");
                return;
            }
            std::cout << "[ADMIN] Password reset for user: " << username << '\n';
            res.set_content(json{{"status", "ok"}, {"msg", "密码重置成功"}}.dump(), "application/json");
        } catch (...) {
            res.status = httplib::StatusCode::BadRequest_400;
            res.set_content(json{{"error", "无效的请求格式"}}.dump(), "application/json");
        }
    });

    server.Post("/api/admin/delete_user", [this](const httplib::Request& req,
                                                 httplib::Response& res) {
        try {
            json body = json::parse(req.body);
            const std::string username = body.value("username", "");
            if (username.empty()) {
                res.status = httplib::StatusCode::BadRequest_400;
                res.set_content(json{{"error", "缺少用户名"}}.dump(), "application/json");
                return;
            }
            std::string err;
            if (!users_.delete_account(username, err)) {
                res.status = httplib::StatusCode::BadRequest_400;
                res.set_content(json{{"error", err}}.dump(), "application/json");
                return;
            }
            std::cout << "[ADMIN] User deleted: " << username << '\n';
            res.set_content(json{{"status", "ok"}, {"msg", "账号已删除"}}.dump(), "application/json");
        } catch (...) {
            res.status = httplib::StatusCode::BadRequest_400;
            res.set_content(json{{"error", "无效的请求格式"}}.dump(), "application/json");
        }
    });

    // ========== 分布式集群网格接口 ==========

    server.Get("/api/cluster/ping", [this](const httplib::Request&, httplib::Response& res) {
        json j{
            {"status", "ok"},
            {"node_id", cluster_.node_id()},
            {"port", cluster_.port()},
            {"online_users", users_.online_count()}
        };
        res.set_content(j.dump(), "application/json");
    });

    server.Post("/api/cluster/event", [this](const httplib::Request& req, httplib::Response& res) {
        try {
            auto body = json::parse(req.body);
            const std::string event_id = body.value("event_id", "");
            const std::string origin_node_id = body.value("origin_node_id", "");
            const std::string type = body.value("type", "");
            const json data = body.value("data", json::object());

            if (event_id.empty() || type.empty()) {
                res.status = httplib::StatusCode::BadRequest_400;
                res.set_content(json{{"error", "invalid cluster event"}}.dump(), "application/json");
                return;
            }

            bool processed = cluster_.handle_incoming_event(event_id, origin_node_id, type, data);
            res.set_content(json{{"status", "ok"}, {"processed", processed}}.dump(), "application/json");
        } catch (...) {
            res.status = httplib::StatusCode::BadRequest_400;
            res.set_content(json{{"error", "malformed json"}}.dump(), "application/json");
        }
    });

    server.Get("/api/cluster/nodes", [this](const httplib::Request&, httplib::Response& res) {
        auto nodes = cluster_.get_all_nodes(users_.online_count());
        json arr = json::array();
        for (const auto& n : nodes) {
            arr.push_back(n.to_json());
        }
        json resp{
            {"status", "ok"},
            {"self_node_id", cluster_.node_id()},
            {"clustered", cluster_.is_clustered()},
            {"nodes", arr}
        };
        res.set_content(resp.dump(), "application/json");
    });

    server.Get("/api/unread", [this](const httplib::Request& req,
                                     httplib::Response& res) {
        if (!req.has_param("username")) {
            res.status = httplib::StatusCode::BadRequest_400;
            res.set_content(json{{"error", "missing username parameter"}}.dump(), "application/json");
            return;
        }
        const std::string username = req.get_param_value("username");
        auto counts = storage_.get_unread_counts(username);
        json counts_json = json::object();
        int total = 0;
        for (const auto& [sender, count] : counts) {
            counts_json[sender] = count;
            total += count;
        }
        res.set_content(json{
            {"username", username},
            {"total", total},
            {"counts", counts_json}
        }.dump(), "application/json");
    });

    server.Post("/api/mark_read", [this](const httplib::Request& req,
                                         httplib::Response& res) {
        try {
            json body = json::parse(req.body);
            const std::string to_user = body.value("to", "");
            const std::string from_user = body.value("from", "");
            if (to_user.empty()) {
                res.status = httplib::StatusCode::BadRequest_400;
                res.set_content(json{{"error", "missing 'to' user"}}.dump(), "application/json");
                return;
            }
            storage_.mark_dms_read(to_user, from_user);
            res.set_content(json{{"status", "ok"}}.dump(), "application/json");
        } catch (...) {
            res.status = httplib::StatusCode::BadRequest_400;
            res.set_content(json{{"error", "invalid json"}}.dump(), "application/json");
        }
    });

    server.Get("/api/online", [this](const httplib::Request&,
                                     httplib::Response& res) {
        json online = json::array();
        for (const auto& user : users_.online_users()) {
            online.push_back({{"nickname", user.nickname},
                              {"address", user.address},
                              {"connect_ts", user.connect_ts}});
        }
        res.set_content(json{{"oneline", std::move(online)}}.dump(),
                        "application/json");
    });

    server.Get("/api/search", [this](const httplib::Request& req,
                                     httplib::Response& res) {
        if (!req.has_param("q") || req.get_param_value("q").empty()) {
            res.status = httplib::StatusCode::BadRequest_400;
            res.set_content(json{{"error", "missing query parameter 'q'"}}.dump(), "application/json");
            return;
        }
        const std::string q = req.get_param_value("q");
        const std::string channel = req.has_param("channel") ? req.get_param_value("channel") : "";
        const std::string group = req.has_param("group") ? req.get_param_value("group") : "";
        std::size_t limit = 50;
        if (req.has_param("limit")) {
            try {
                limit = std::max<std::size_t>(1, std::stoul(req.get_param_value("limit")));
            } catch (...) {}
        }
        auto results = storage_.search_messages(q, channel, group, limit);
        json body{
            {"query", q},
            {"count", results.size()},
            {"results", results}
        };
        res.set_content(body.dump(), "application/json");
    });

    server.Get("/api/metrics", [this](const httplib::Request&,
                                      httplib::Response& res) {
        auto& state = runtime_for(this);
        const int64_t uptime_s = (now_ms() - state.started_at) / 1000;
        std::ostringstream oss;
        oss << "# HELP chatroom_uptime_seconds Total server uptime in seconds\n"
            << "# TYPE chatroom_uptime_seconds gauge\n"
            << "chatroom_uptime_seconds " << uptime_s << "\n\n"
            << "# HELP chatroom_online_users Current online users count\n"
            << "# TYPE chatroom_online_users gauge\n"
            << "chatroom_online_users " << users_.online_count() << "\n\n"
            << "# HELP chatroom_registered_accounts Total registered accounts\n"
            << "# TYPE chatroom_registered_accounts gauge\n"
            << "chatroom_registered_accounts " << users_.account_count() << "\n\n"
            << "# HELP chatroom_active_connections Current active WebSocket connections\n"
            << "# TYPE chatroom_active_connections gauge\n"
            << "chatroom_active_connections " << ws_.connection_count() << "\n\n"
            << "# HELP chatroom_stored_messages Total messages stored in database\n"
            << "# TYPE chatroom_stored_messages counter\n"
            << "chatroom_stored_messages " << storage_.message_count() << "\n";
        res.set_content(oss.str(), "text/plain; version=0.0.4");
    });

    auto format_time_str = [](int64_t ts) -> std::string {
        std::time_t sec = ts / 1000;
        std::tm tm_buf;
#ifdef _WIN32
        localtime_s(&tm_buf, &sec);
#else
        localtime_r(&sec, &tm_buf);
#endif
        char buf[32];
        std::strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M:%S", &tm_buf);
        return buf;
    };

    server.Get("/api/db/changes", [this, format_time_str](const httplib::Request& req,
                                                          httplib::Response& res) {
        int64_t since_id = 0;
        std::size_t limit = 50;
        if (req.has_param("since_id")) {
            try { since_id = std::stoll(req.get_param_value("since_id")); } catch (...) {}
        }
        if (req.has_param("limit")) {
            try { limit = std::clamp<std::size_t>(std::stoul(req.get_param_value("limit")), 1, 200); } catch (...) {}
        }

        auto records = storage_.get_db_messages(since_id, limit);
        json arr = json::array();
        for (const auto& r : records) {
            json item{
                {"id", r.id},
                {"msg_id", r.msg_id},
                {"type", r.msg_type},
                {"from", r.from_user},
                {"to", r.to_user.empty() ? nullptr : json(r.to_user)},
                {"group", r.group_name.empty() ? nullptr : json(r.group_name)},
                {"channel", r.channel},
                {"text", r.text},
                {"ts", r.timestamp},
                {"time", format_time_str(r.timestamp)},
                {"is_read", r.is_read},
                {"is_recalled", r.is_recalled}
            };
            arr.push_back(std::move(item));
        }

        json body{
            {"total_messages", storage_.message_count()},
            {"since_id", since_id},
            {"count", records.size()},
            {"records", arr}
        };
        res.set_content(body.dump(2), "application/json; charset=utf-8");
    });

    server.Get("/api/db/tail", [this, format_time_str](const httplib::Request& req,
                                                       httplib::Response& res) {
        std::size_t limit = 20;
        if (req.has_param("limit")) {
            try { limit = std::clamp<std::size_t>(std::stoul(req.get_param_value("limit")), 1, 100); } catch (...) {}
        }

        auto records = storage_.get_db_messages(0, limit);
        std::ostringstream oss;
        oss << "==============================================================================================================\n"
            << "                                     CHATROOM DATABASE MESSAGES (SQLite)                                      \n"
            << "==============================================================================================================\n";
        oss << std::left
            << std::setw(6) << "ID"
            << " | " << std::setw(19) << "TIME"
            << " | " << std::setw(8) << "CHANNEL"
            << " | " << std::setw(8) << "TYPE"
            << " | " << std::setw(14) << "FROM"
            << " | " << std::setw(16) << "TO / GROUP"
            << " | CONTENT\n";
        oss << "--------------------------------------------------------------------------------------------------------------\n";

        for (const auto& r : records) {
            std::string to_target = "-";
            if (!r.group_name.empty()) {
                to_target = "#" + r.group_name;
            } else if (!r.to_user.empty()) {
                to_target = "@" + r.to_user + (r.is_read ? " [READ]" : " [UNREAD]");
            }
            std::string content = r.is_recalled ? "[已撤回]" : (r.text.empty() ? ("[" + r.msg_type + "]") : r.text);
            if (content.size() > 40) {
                content = content.substr(0, 37) + "...";
            }

            oss << std::left
                << std::setw(6) << r.id
                << " | " << std::setw(19) << format_time_str(r.timestamp)
                << " | " << std::setw(8) << r.channel
                << " | " << std::setw(8) << r.msg_type
                << " | " << std::setw(14) << r.from_user
                << " | " << std::setw(16) << to_target
                << " | " << content << "\n";
        }

        oss << "==============================================================================================================\n"
            << "Total messages in DB: " << storage_.message_count() << " | Showing: " << records.size()
            << " | Use /api/db/changes?since_id=... for raw JSON\n";

        res.set_content(oss.str(), "text/plain; charset=utf-8");
    });

    server.WebSocket(
        "/ws", [this](const httplib::Request& req,
                       httplib::ws::WebSocket& socket) {
            std::string nickname;
            int64_t connected_at = 0;
            auto& state = runtime_for(this);
            const std::string address =
                req.remote_addr + ":" + std::to_string(req.remote_port);
            const auto send_error = [this, &nickname, &socket](
                                        const std::string& message) {
                const std::string payload =
                    json{{"type", "error"}, {"msg", message}}.dump();
                if (nickname.empty()) {
                    socket.send(payload);
                } else {
                    ws_.send_raw(nickname, payload);
                }
            };

            try {
                std::string payload;
                while (true) {
                    const auto read_result = socket.read(payload);
                    if (read_result == httplib::ws::Fail) {
                        break;
                    }
                    if (read_result != httplib::ws::Text) {
                        send_error("仅支持 JSON 文本帧");
                        continue;
                    }

                    json frame;
                    try {
                        frame = json::parse(payload);
                    } catch (const json::exception&) {
                        send_error("无效的 JSON");
                        continue;
                    }

                    if (!frame.is_object()) {
                        send_error("无效的消息格式");
                        continue;
                    }

                    const auto type_it = frame.find("type");
                    if (type_it == frame.end() || !type_it->is_string()) {
                        send_error("消息类型必须是字符串");
                        continue;
                    }

                    const std::string type = type_it->get<std::string>();
                    if (type == "register") {
                        const auto username_it = frame.find("username");
                        const auto password_it = frame.find("password");
                        if (username_it == frame.end() || !username_it->is_string() ||
                            password_it == frame.end() || !password_it->is_string()) {
                            send_error("用户名和密码必须是非空字符串");
                            continue;
                        }
                        const std::string username = username_it->get<std::string>();
                        const std::string password = password_it->get<std::string>();
                        std::string err;
                        std::string recovery_key;
                        if (!users_.register_account(username, password, err, &recovery_key)) {
                            send_error(err);
                            continue;
                        }
                        const std::string token = users_.issue_token(username);
                        socket.send(json{
                            {"type", "register_success"},
                            {"username", username},
                            {"token", token},
                            {"recovery_key", recovery_key},
                            {"msg", "注册成功"}
                        }.dump());
                        continue;
                    } else if (type == "auth_token") {
                        if (!nickname.empty()) {
                            send_error("当前连接已登录");
                            continue;
                        }
                        const auto token_it = frame.find("token");
                        if (token_it == frame.end() || !token_it->is_string()) {
                            send_error("Token 必须是有效字符串");
                            continue;
                        }
                        const std::string token = token_it->get<std::string>();
                        const std::string verified_user = users_.verify_token(token);
                        if (verified_user.empty()) {
                            send_error("会话凭据已过期或无效，请重新登录");
                            continue;
                        }
                        if (!users_.user_online(verified_user, address)) {
                            send_error("该账号当前已在线");
                            continue;
                        }
                        nickname = verified_user;
                        connected_at = now_ms();
                        for (const auto& user : users_.online_users()) {
                            if (user.nickname == nickname) {
                                connected_at = user.connect_ts;
                                users_.log_online(user);
                                break;
                            }
                        }
                        record_connection(state, nickname, &socket);
                        ws_.register_ws(nickname, &socket);
                        socket.send(json{
                            {"type", "login_success"},
                            {"nickname", nickname},
                            {"token", token},
                            {"node_id", cluster_.node_id()},
                            {"port", port_}
                        }.dump());
                        ws_.broadcast_raw(
                            json{{"type", "system"},
                                 {"msg", nickname + " 上线了"}}
                                .dump());

                        auto unread_counts = storage_.get_unread_counts(nickname);
                        if (!unread_counts.empty()) {
                            auto unread_msgs = storage_.get_unread_dms(nickname);
                            json counts_json = json::object();
                            int total_unread = 0;
                            for (const auto& [sender, count] : unread_counts) {
                                counts_json[sender] = count;
                                total_unread += count;
                            }
                            socket.send(json{
                                {"type", "unread_sync"},
                                {"total", total_unread},
                                {"counts", counts_json},
                                {"messages", unread_msgs}
                            }.dump());
                        }
                        continue;
                    } else if (type == "login") {
                        if (!nickname.empty()) {
                            send_error("当前连接已登录");
                            continue;
                        }

                        const auto nickname_it = frame.find("nickname");
                        if (nickname_it == frame.end() ||
                            !nickname_it->is_string()) {
                            send_error("昵称必须是字符串");
                            continue;
                        }

                        const std::string requested_nickname =
                            nickname_it->get<std::string>();

                        const std::string client_ip = req.remote_addr;
                        if (!rate_limiter_.allow_login_attempt(client_ip)) {
                            send_error("登录失败过多，该 IP 已被临时锁定 5 分钟，请稍后再试");
                            continue;
                        }

                        const auto password_it = frame.find("password");
                        if (password_it != frame.end() && password_it->is_string() &&
                            !password_it->get<std::string>().empty()) {
                            std::string err;
                            if (!users_.authenticate_account(requested_nickname,
                                                             password_it->get<std::string>(),
                                                             err)) {
                                rate_limiter_.record_login_failure(client_ip);
                                send_error(err);
                                continue;
                            }
                            rate_limiter_.reset_login_failure(client_ip);
                        } else {
                            if (users_.has_account(requested_nickname)) {
                                send_error("该昵称已注册账号，请输入密码登录");
                                continue;
                            }
                        }

                        if (!users_.user_online(requested_nickname, address)) {
                            send_error("昵称已被占用");
                            continue;
                        }

                        nickname = requested_nickname;
                        connected_at = now_ms();
                        for (const auto& user : users_.online_users()) {
                            if (user.nickname == nickname) {
                                connected_at = user.connect_ts;
                                users_.log_online(user);
                                break;
                            }
                        }

                        // This is the server-layer nickname -> WebSocket* map
                        // used for targeted group delivery. The pointer remains
                        // valid for the lifetime of this blocking handler.
                        record_connection(state, nickname, &socket);
                        ws_.register_ws(nickname, &socket);
                        const std::string token = users_.issue_token(nickname);
                        socket.send(json{
                            {"type", "login_success"},
                            {"nickname", nickname},
                            {"token", token},
                            {"node_id", cluster_.node_id()},
                            {"port", port_}
                        }.dump());
                        ws_.broadcast_raw(
                            json{{"type", "system"},
                                 {"msg", nickname + " 上线了"}}
                                .dump());

                        auto unread_counts = storage_.get_unread_counts(nickname);
                        if (!unread_counts.empty()) {
                            auto unread_msgs = storage_.get_unread_dms(nickname);
                            json counts_json = json::object();
                            int total_unread = 0;
                            for (const auto& [sender, count] : unread_counts) {
                                counts_json[sender] = count;
                                total_unread += count;
                            }
                            socket.send(json{
                                {"type", "unread_sync"},
                                {"total", total_unread},
                                {"counts", counts_json},
                                {"messages", unread_msgs}
                            }.dump());
                        }
                    } else if (type == "chat") {
                        if (nickname.empty()) {
                            send_error("请先登录");
                            continue;
                        }

                        if (!rate_limiter_.allow_message(nickname)) {
                            send_error("发送消息过于频繁，已被限流（上限每秒5条）");
                            continue;
                        }

                        const auto text_it = frame.find("text");
                        if (text_it == frame.end() || !text_it->is_string()) {
                            send_error("聊天内容必须是字符串");
                            continue;
                        }

                        const std::string text = text_it->get<std::string>();
                        const auto sticker_it = frame.find("sticker");
                        const bool has_sticker = (sticker_it != frame.end() && sticker_it->is_string() &&
                                                  stickers.contains(sticker_it->get<std::string>()));
                        const bool has_reply_to = (frame.contains("reply_to") && frame["reply_to"].is_object());
                        const bool has_msg_id = (frame.contains("msg_id") && frame["msg_id"].is_string());

                        if (has_sticker || has_reply_to || has_msg_id) {
                            json message{{"type", "chat"},
                                         {"from", nickname},
                                         {"text", text},
                                         {"ts", now_ms()}};
                            if (has_sticker) {
                                message["sticker"] = sticker_it->get<std::string>();
                            }
                            if (has_reply_to) {
                                message["reply_to"] = frame["reply_to"];
                            }
                            if (has_msg_id) {
                                message["msg_id"] = frame["msg_id"].get<std::string>();
                            } else {
                                message["msg_id"] = std::to_string(now_ms()) + "_" + nickname;
                            }
                            append_message(message, &storage_, 1);
                            ws_.send_raw(nickname, json{{"type", "ack"},
                                                        {"msg_id", message["msg_id"]},
                                                        {"status", "ok"},
                                                        {"ts", now_ms()}}.dump());
                            std::cout << "[消息] " << nickname << ": " << text << '\n';
                            ws_.broadcast_raw(message.dump());
                            cluster_.broadcast_event("chat", message);
                        } else {
                            std::string auto_id = std::to_string(now_ms()) + "_" + nickname;
                            ws_.send_raw(nickname, json{{"type", "ack"},
                                                        {"msg_id", auto_id},
                                                        {"status", "ok"},
                                                        {"ts", now_ms()}}.dump());
                            // Preserve the original global chat path exactly.
                            ws_.handle_text(nickname, text);
                        }

                        const auto question = ai_question_from(text);
                        if (question) {
                            ws_.broadcast_raw(
                                json{{"type", "system"},
                                     {"msg", "AI助手正在思考…"}}
                                    .dump());

                            enqueue_ai_task(state, nickname, *question);
                        }

                        // Mentions are ephemeral, targeted notifications. Match
                        // complete online nicknames only: the character after a
                        // nickname must be a space or the end of the message.
                        const auto& mention_text =
                            text_it->get_ref<const std::string&>();
                        std::vector<std::string> mentioned_users;
                        {
                            std::lock_guard<std::mutex> lock(state.mutex);
                            for (const auto& [online_nickname, connection] :
                                 state.connections) {
                                (void)connection;
                                if (online_nickname == "机器人" ||
                                    online_nickname == "AI" ||
                                    online_nickname == "ai") {
                                    continue;
                                }

                                const std::string token = "@" + online_nickname;
                                std::size_t position = mention_text.find(token);
                                while (position != std::string::npos) {
                                    const std::size_t end = position + token.size();
                                    if (end == mention_text.size() ||
                                        mention_text[end] == ' ') {
                                        mentioned_users.push_back(online_nickname);
                                        break;
                                    }
                                    position = mention_text.find(token, position + 1);
                                }
                            }
                        }

                        if (!mentioned_users.empty()) {
                            const std::string mention_payload =
                                json{{"type", "mention"},
                                     {"from", nickname},
                                     {"text", mention_text},
                                     {"ts", now_ms()}}
                                    .dump();
                            for (const auto& mentioned_user : mentioned_users) {
                                ws_.send_raw(mentioned_user, mention_payload);
                            }
                        }
                    } else if (type == "image_send") {
                        if (nickname.empty()) {
                            send_error("请先登录");
                            continue;
                        }

                        if (!rate_limiter_.allow_message(nickname)) {
                            send_error("发送消息过于频繁，已被限流（上限每秒5条）");
                            continue;
                        }

                        const auto url_it = frame.find("url");
                        if (url_it == frame.end() || !url_it->is_string() ||
                            !is_valid_image_url(url_it->get_ref<const std::string&>())) {
                            send_error("无效的图片 URL");
                            continue;
                        }

                        json message{{"type", "image"},
                                     {"from", nickname},
                                     {"url", url_it->get<std::string>()},
                                     {"ts", now_ms()}};
                        if (frame.contains("msg_id") && frame["msg_id"].is_string()) {
                            message["msg_id"] = frame["msg_id"].get<std::string>();
                        } else {
                            message["msg_id"] = std::to_string(now_ms()) + "_" + nickname;
                        }
                        if (frame.contains("reply_to") && frame["reply_to"].is_object()) {
                            message["reply_to"] = frame["reply_to"];
                        }

                        const auto group_it = frame.find("group");
                        const auto to_it = frame.find("to");

                        if (group_it != frame.end()) {
                            if (!group_it->is_string() ||
                                group_it->get_ref<const std::string&>().empty()) {
                                send_error("群名必须是非空字符串");
                                continue;
                            }
                            const std::string group_name = group_it->get<std::string>();
                            std::lock_guard<std::mutex> lock(state.mutex);
                            const auto membership_it = state.memberships.find(nickname);
                            if (membership_it == state.memberships.end() ||
                                !membership_it->second.contains(group_name)) {
                                send_error("请先加入群 " + group_name);
                                continue;
                            }
                            message["group"] = group_name;
                            append_message(message, &storage_, 1);
                            broadcast_group_locked(state, ws_, group_name,
                                                   message.dump());
                            cluster_.broadcast_event("image", message);
                        } else if (to_it != frame.end()) {
                            if (!to_it->is_string() ||
                                to_it->get_ref<const std::string&>().empty()) {
                                send_error("接收方必须是非空字符串");
                                continue;
                            }
                            const std::string target = to_it->get<std::string>();
                            if (target == nickname) {
                                send_error("不能给自己发送私聊图片");
                                continue;
                            }
                            std::lock_guard<std::mutex> lock(state.mutex);
                            const bool is_target_online = state.connections.contains(target);
                            if (!is_target_online && !cluster_.is_clustered()) {
                                send_error("对方不在线");
                                continue;
                            }
                            message["to"] = target;
                            storage_.save_message(message, 0);
                            if (is_target_online) {
                                ws_.send_raw(target, message.dump());
                            }
                            ws_.send_raw(nickname, message.dump());
                            cluster_.broadcast_event("image", message);
                        } else {
                            append_message(message, &storage_, 1);
                            ws_.broadcast_raw(message.dump());
                            cluster_.broadcast_event("image", message);
                        }
                    } else if (type == "reaction") {
                        if (nickname.empty()) {
                            send_error("请先登录");
                            continue;
                        }

                        const auto msg_id_it = frame.find("msg_id");
                        const auto emoji_it = frame.find("emoji");
                        if (msg_id_it == frame.end() || !msg_id_it->is_string() ||
                            msg_id_it->get_ref<const std::string&>().empty()) {
                            send_error("消息ID必须是非空字符串");
                            continue;
                        }
                        if (emoji_it == frame.end() || !emoji_it->is_string() ||
                            emoji_it->get_ref<const std::string&>().empty()) {
                            send_error("表情符号必须是非空字符串");
                            continue;
                        }

                        json reaction_msg{{"type", "reaction"},
                                          {"from", nickname},
                                          {"msg_id", msg_id_it->get<std::string>()},
                                          {"emoji", emoji_it->get<std::string>()},
                                          {"ts", now_ms()}};

                        const auto group_it = frame.find("group");
                        const auto to_it = frame.find("to");
                        if (group_it != frame.end() && group_it->is_string() &&
                            !group_it->get_ref<const std::string&>().empty()) {
                            const std::string group_name = group_it->get<std::string>();
                            reaction_msg["group"] = group_name;
                            std::lock_guard<std::mutex> lock(state.mutex);
                            broadcast_group_locked(state, ws_, group_name,
                                                   reaction_msg.dump());
                        } else if (to_it != frame.end() && to_it->is_string() &&
                                   !to_it->get_ref<const std::string&>().empty()) {
                            const std::string target = to_it->get<std::string>();
                            reaction_msg["to"] = target;
                            std::lock_guard<std::mutex> lock(state.mutex);
                            if (state.connections.contains(target)) {
                                ws_.send_raw(target, reaction_msg.dump());
                            }
                            ws_.send_raw(nickname, reaction_msg.dump());
                        } else {
                            ws_.broadcast_raw(reaction_msg.dump());
                        }
                        cluster_.broadcast_event("reaction", reaction_msg);
                    } else if (type == "typing") {
                        if (nickname.empty()) {
                            continue;
                        }

                        json typing_msg{{"type", "typing"},
                                        {"from", nickname}};
                        const auto group_it = frame.find("group");
                        const auto to_it = frame.find("to");

                        if (group_it != frame.end() && group_it->is_string() &&
                            !group_it->get_ref<const std::string&>().empty()) {
                            const std::string group_name = group_it->get<std::string>();
                            typing_msg["group"] = group_name;
                            std::lock_guard<std::mutex> lock(state.mutex);
                            const auto g_it = state.groups.find(group_name);
                            if (g_it != state.groups.end()) {
                                for (const auto& member : g_it->second) {
                                    if (member != nickname) {
                                        ws_.send_raw(member, typing_msg.dump());
                                    }
                                }
                            }
                        } else if (to_it != frame.end() && to_it->is_string() &&
                                   !to_it->get_ref<const std::string&>().empty()) {
                            const std::string target = to_it->get<std::string>();
                            typing_msg["to"] = target;
                            std::lock_guard<std::mutex> lock(state.mutex);
                            if (state.connections.contains(target)) {
                                ws_.send_raw(target, typing_msg.dump());
                            }
                        } else {
                            typing_msg["channel"] = "global";
                            std::lock_guard<std::mutex> lock(state.mutex);
                            for (const auto& [online_nick, conn] : state.connections) {
                                (void)conn;
                                if (online_nick != nickname) {
                                    ws_.send_raw(online_nick, typing_msg.dump());
                                }
                            }
                        }
                    } else if (type == "voice_send") {
                        if (nickname.empty()) {
                            send_error("请先登录");
                            continue;
                        }

                        if (!rate_limiter_.allow_message(nickname)) {
                            send_error("发送消息过于频繁，已被限流（上限每秒5条）");
                            continue;
                        }

                        const auto url_it = frame.find("url");
                        if (url_it == frame.end() || !url_it->is_string() ||
                            !is_valid_voice_url(url_it->get_ref<const std::string&>())) {
                            send_error("无效的语音 URL");
                            continue;
                        }

                        json message{{"type", "voice"},
                                     {"from", nickname},
                                     {"url", url_it->get<std::string>()},
                                     {"ts", now_ms()}};
                        if (frame.contains("msg_id") && frame["msg_id"].is_string()) {
                            message["msg_id"] = frame["msg_id"].get<std::string>();
                        } else {
                            message["msg_id"] = std::to_string(now_ms()) + "_" + nickname;
                        }

                        const auto group_it = frame.find("group");
                        if (group_it == frame.end()) {
                            append_message(message, &storage_, 1);
                            ws_.send_raw(nickname, json{{"type", "ack"},
                                                        {"msg_id", message["msg_id"]},
                                                        {"status", "ok"},
                                                        {"ts", now_ms()}}.dump());
                            ws_.broadcast_raw(message.dump());
                            cluster_.broadcast_event("voice", message);
                            continue;
                        }
                        if (!group_it->is_string() ||
                            group_it->get_ref<const std::string&>().empty()) {
                            send_error("群名必须是非空字符串");
                            continue;
                        }

                        const std::string group_name = group_it->get<std::string>();
                        std::lock_guard<std::mutex> lock(state.mutex);
                        const auto membership_it = state.memberships.find(nickname);
                        if (membership_it == state.memberships.end() ||
                            !membership_it->second.contains(group_name)) {
                            send_error("请先加入群 " + group_name);
                            continue;
                        }
                        message["group"] = group_name;
                        append_message(message, &storage_, 1);
                        ws_.send_raw(nickname, json{{"type", "ack"},
                                                    {"msg_id", message["msg_id"]},
                                                    {"status", "ok"},
                                                    {"ts", now_ms()}}.dump());
                        broadcast_group_locked(state, ws_, group_name,
                                               message.dump());
                        cluster_.broadcast_event("voice", message);
                    } else if (type == "join_group") {
                        if (nickname.empty()) {
                            send_error("请先登录");
                            continue;
                        }

                        const auto name_it = frame.find("name");
                        if (name_it == frame.end() || !name_it->is_string() ||
                            name_it->get_ref<const std::string&>().empty()) {
                            send_error("群名必须是非空字符串");
                            continue;
                        }

                        const std::string group_name = name_it->get<std::string>();
                        std::lock_guard<std::mutex> lock(state.mutex);
                        const bool inserted =
                            state.groups[group_name].insert(nickname).second;
                        state.memberships[nickname].insert(group_name);
                        if (!inserted) {
                            ws_.send_raw(
                                nickname,
                                json{{"type", "system"},
                                     {"msg", "你已在群 " + group_name + " 中"}}
                                    .dump());
                            continue;
                        }

                        ws_.send_raw(
                            nickname,
                            json{{"type", "system"},
                                 {"msg", "你已加入群 " + group_name}}
                                .dump());
                        broadcast_group_locked(
                            state, ws_, group_name,
                            json{{"type", "system"},
                                 {"msg", nickname + " 加入了群 " + group_name}}
                                .dump());
                    } else if (type == "group_chat") {
                        if (nickname.empty()) {
                            send_error("请先登录");
                            continue;
                        }

                        if (!rate_limiter_.allow_message(nickname)) {
                            send_error("发送消息过于频繁，已被限流（上限每秒5条）");
                            continue;
                        }

                        const auto group_it = frame.find("group");
                        const auto text_it = frame.find("text");
                        if (group_it == frame.end() || !group_it->is_string() ||
                            group_it->get_ref<const std::string&>().empty()) {
                            send_error("群名必须是非空字符串");
                            continue;
                        }
                        if (text_it == frame.end() || !text_it->is_string()) {
                            send_error("聊天内容必须是字符串");
                            continue;
                        }

                        const std::string group_name = group_it->get<std::string>();
                        std::lock_guard<std::mutex> lock(state.mutex);
                        const auto membership_it = state.memberships.find(nickname);
                        if (membership_it == state.memberships.end() ||
                            !membership_it->second.contains(group_name)) {
                            send_error("请先加入群 " + group_name);
                            continue;
                        }

                        json group_msg{{"type", "group_chat"},
                                       {"group", group_name},
                                       {"from", nickname},
                                       {"text", text_it->get<std::string>()},
                                       {"ts", now_ms()}};
                        if (frame.contains("msg_id") && frame["msg_id"].is_string()) {
                            group_msg["msg_id"] = frame["msg_id"].get<std::string>();
                        } else {
                            group_msg["msg_id"] = std::to_string(now_ms()) + "_" + nickname;
                        }
                        if (frame.contains("reply_to") && frame["reply_to"].is_object()) {
                            group_msg["reply_to"] = frame["reply_to"];
                        }

                        ws_.send_raw(nickname, json{{"type", "ack"},
                                                    {"msg_id", group_msg["msg_id"]},
                                                    {"status", "ok"},
                                                    {"ts", now_ms()}}.dump());
                        append_message(group_msg, &storage_, 1);
                        broadcast_group_locked(
                            state, ws_, group_name,
                            group_msg.dump());
                        cluster_.broadcast_event("group_chat", group_msg);
                    } else if (type == "dm") {
                        if (nickname.empty()) {
                            send_error("请先登录");
                            continue;
                        }

                        if (!rate_limiter_.allow_message(nickname)) {
                            send_error("发送消息过于频繁，已被限流（上限每秒5条）");
                            continue;
                        }

                        const auto to_it = frame.find("to");
                        const auto text_it = frame.find("text");
                        if (to_it == frame.end() || !to_it->is_string() ||
                            to_it->get_ref<const std::string&>().empty()) {
                            send_error("接收方必须是非空字符串");
                            continue;
                        }
                        if (text_it == frame.end() || !text_it->is_string()) {
                            send_error("聊天内容必须是字符串");
                            continue;
                        }

                        const std::string target = to_it->get<std::string>();
                        const std::string text = text_it->get<std::string>();
                        if (target == nickname) {
                            send_error("不能给自己发送私聊");
                            continue;
                        }

                        const int64_t ts = now_ms();
                        json dm_payload{{"type", "dm"},
                                        {"from", nickname},
                                        {"to", target},
                                        {"text", text},
                                        {"ts", ts}};
                        if (frame.contains("msg_id") && frame["msg_id"].is_string()) {
                            dm_payload["msg_id"] = frame["msg_id"].get<std::string>();
                        } else {
                            dm_payload["msg_id"] = std::to_string(now_ms()) + "_" + nickname;
                        }
                        if (frame.contains("reply_to") && frame["reply_to"].is_object()) {
                            dm_payload["reply_to"] = frame["reply_to"];
                        }
                        const std::string dm_payload_str = dm_payload.dump();

                        bool is_target_online = false;
                        {
                            std::lock_guard<std::mutex> lock(state.mutex);
                            is_target_online = state.connections.contains(target);
                        }

                        if (!is_target_online && !users_.has_account(target) && !cluster_.is_clustered()) {
                            send_error("对方不在线");
                            continue;
                        }

                        if (is_target_online) {
                            ws_.send_raw(target, dm_payload_str);
                        }
                        ws_.send_raw(nickname, dm_payload_str);

                        // 持久化存储至 SQLite（公共 messages.jsonl 不混入私聊）
                        storage_.save_message(dm_payload, 0);
                        cluster_.broadcast_event("dm", dm_payload);

                        if (is_target_online) {
                            ws_.send_raw(nickname, json{{"type", "ack"},
                                                        {"msg_id", dm_payload["msg_id"]},
                                                        {"status", "ok"},
                                                        {"ts", now_ms()}}.dump());
                        } else {
                            ws_.send_raw(nickname, json{{"type", "ack"},
                                                        {"msg_id", dm_payload["msg_id"]},
                                                        {"status", "offline_queued"},
                                                        {"info", "对方当前离线，已保存为离线留言"},
                                                        {"ts", now_ms()}}.dump());
                        }
                    } else if (type == "mark_read") {
                        if (nickname.empty()) continue;
                        const std::string from_user = frame.value("from", "");
                        storage_.mark_dms_read(nickname, from_user);
                        socket.send(json{
                            {"type", "mark_read_ack"},
                            {"from", from_user}
                        }.dump());
                    } else if (type == "recall") {
                        if (nickname.empty()) {
                            send_error("请先登录");
                            continue;
                        }
                        const std::string msg_id = frame.value("msg_id", "");
                        if (msg_id.empty()) {
                            send_error("缺少撤回消息ID");
                            continue;
                        }
                        std::string err;
                        json recalled_msg;
                        const bool is_admin = (nickname == "admin");
                        if (!storage_.recall_message(msg_id, nickname, is_admin, err, recalled_msg)) {
                            send_error(err);
                            continue;
                        }

                        json recall_msg{
                            {"type", "recall"},
                            {"msg_id", msg_id},
                            {"from", nickname},
                            {"ts", now_ms()}
                        };
                        const std::string group_name = recalled_msg.value("group", frame.value("group", ""));
                        const std::string to_user = recalled_msg.value("to", frame.value("to", ""));

                        if (!group_name.empty()) {
                            recall_msg["group"] = group_name;
                            std::lock_guard<std::mutex> lock(state.mutex);
                            broadcast_group_locked(state, ws_, group_name, recall_msg.dump());
                        } else if (!to_user.empty()) {
                            recall_msg["to"] = to_user;
                            const std::string sender = recalled_msg.value("from", nickname);
                            std::lock_guard<std::mutex> lock(state.mutex);
                            if (state.connections.contains(to_user)) {
                                ws_.send_raw(to_user, recall_msg.dump());
                            }
                            if (sender != to_user && state.connections.contains(sender)) {
                                ws_.send_raw(sender, recall_msg.dump());
                            }
                        } else {
                            ws_.broadcast_raw(recall_msg.dump());
                        }
                        cluster_.broadcast_event("recall", recall_msg);
                    }

                }
            } catch (const std::exception& error) {
                std::cerr << "WebSocket handler error: " << error.what()
                          << '\n';
            }

            if (!nickname.empty()) {
                // Remove targeted-delivery state before the socket leaves this
                // handler scope and its pointer becomes invalid.
                remove_connection(state, nickname);
                users_.log_offline(nickname, now_ms() - connected_at);
                users_.user_offline(nickname);
                ws_.unregister_ws(nickname);
                ws_.broadcast_raw(
                    json{{"type", "system"},
                         {"msg", nickname + " 离线了"}}
                        .dump());
            }
        });
}

void ChatServer::run() {
    auto& server = server_for(this);
    running_ = true;
    std::cout << "Chatroom server listening on port " << port_ << "\n";
    const bool listened = server.listen("0.0.0.0", port_);
    running_ = false;

    if (!listened) {
        std::cerr << "Failed to listen on port " << port_ << "\n";
    }
}

void ChatServer::persist_and_log(const ChatMessage& msg) {
    append_message(msg.to_json(), &storage_, 1);
    std::cout << "[消息] " << msg.from << ": " << msg.text << '\n';
    cluster_.broadcast_event("chat", msg.to_json());
}

} // namespace chat
