#include <iostream>
#include <thread>
#include <memory>
#include <vector>
#include <ctime>
#include <string>
#include <cstddef>
#include <random>
#include <unordered_map>
#include <mutex>
#include <iomanip>
#include <chrono>
#include <algorithm>
#include <limits>
#include <atomic>
#include <cstring>
#include <stdexcept>
#include <sstream>
#include <filesystem>
#include <fstream>
#define NOMINMAX
#include <winsock2.h>
#include <ws2tcpip.h>
#include <libloaderapi.h>
#include "Thread_Pools.h"

#pragma comment(lib, "Ws2_32.lib")

int pickServerRoundRobin(const std::vector<int>& servers) {
    static std::size_t next = 0;
    if (servers.empty()) {
        return -1;
    }

    int selected = servers[next % servers.size()];
    ++next;
    return selected;
}

struct RoundResult {
    long long elapsedMs = 0;
    std::unordered_map<int, int> hitCount;
    std::unordered_map<int, std::size_t> byteCount;
};

class Connection {
private:
    int id;
public:
    Connection(int id) : id(id) {
        if (id < 10) {
            std::cout << "Connection " << id << " created." << std::endl;
        }
    }
    ~Connection() {
        if (id < 10) {
            std::cout << "Connection" << id << " destroyed." << std::endl;
        }
    }
    void send(const std::string& msg) {
        std::cout << "Sent message: " << msg << std::endl;
        std::cout << "Send time: " << std::time(nullptr) << std::endl;
    }
};

class WinSockSession {
public:
    WinSockSession() {
        WSADATA wsaData{};
        if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0) {
            throw std::runtime_error("WSAStartup failed");
        }
    }

    ~WinSockSession() {
        WSACleanup();
    }
};

bool sendLine(SOCKET socketFd, const std::string& text) {
    std::string withNewline = text + "\n";
    int total = 0;
    while (total < static_cast<int>(withNewline.size())) {
        int sent = send(socketFd, withNewline.c_str() + total, static_cast<int>(withNewline.size()) - total, 0);
        if (sent <= 0) {
            return false;
        }
        total += sent;
    }
    return true;
}

struct ChatClient {
    int id = 0;
    SOCKET socketFd = INVALID_SOCKET;
    std::string name;
    std::mutex sendMutex;
};

bool sendToClient(const std::shared_ptr<ChatClient>& client, const std::string& text) {
    std::lock_guard<std::mutex> lock(client->sendMutex);
    return sendLine(client->socketFd, text);
}

void runServer(unsigned short port);

bool pathExists(const std::filesystem::path& p) {
    std::error_code ec;
    return std::filesystem::exists(p, ec);
}

std::string quotePath(const std::filesystem::path& p) {
    return "\"" + p.string() + "\"";
}

std::filesystem::path executableDir() {
    constexpr DWORD kPathBufSize = 260;
    char buffer[kPathBufSize] = {0};
    DWORD len = GetModuleFileNameA(nullptr, buffer, kPathBufSize);
    if (len == 0 || len >= kPathBufSize) {
        return std::filesystem::current_path();
    }
    return std::filesystem::path(buffer).parent_path();
}

std::string nowText() {
    auto now = std::chrono::system_clock::now();
    std::time_t tt = std::chrono::system_clock::to_time_t(now);
    std::tm tmLocal{};
    localtime_s(&tmLocal, &tt);
    std::ostringstream oss;
    oss << std::put_time(&tmLocal, "%Y-%m-%d %H:%M:%S");
    return oss.str();
}

void writeExitReport(int mode, const std::string& errorText) {
    std::filesystem::path reportPath = executableDir() / "exit-error-report.log";
    std::ofstream out(reportPath.string(), std::ios::app);
    if (!out) {
        return;
    }
    out << "=== Exit Error Report ===\n";
    out << "time: " << nowText() << "\n";
    out << "mode: " << mode << "\n";
    out << "cwd: " << std::filesystem::current_path().string() << "\n";
    out << "wsa_last_error: " << WSAGetLastError() << "\n";
    out << "message: " << errorText << "\n\n";
}

bool launchMonitorUi() {
    std::filesystem::path root = executableDir();
    std::filesystem::path chatUiDir = root / "chat-ui";
    std::filesystem::path jarPath = chatUiDir / "target" / "chat-ui-1.0.0.jar";
    std::filesystem::path localMavenCmd = std::filesystem::path(std::getenv("USERPROFILE") ? std::getenv("USERPROFILE") : "")
        / "tools" / "apache-maven-3.9.9" / "bin" / "mvn.cmd";

    if (pathExists(jarPath)) {
        std::string cmd = "cmd /c start \"chat-ui\" java -jar " + quotePath(jarPath);
        int code = std::system(cmd.c_str());
        return code == 0;
    }

    std::string mavenRun = "mvn spring-boot:run";
    if (pathExists(localMavenCmd)) {
        mavenRun = "& '" + localMavenCmd.string() + "' spring-boot:run";
    }
    std::string fallbackCmd = "cmd /c start \"chat-ui\" powershell -NoExit -Command \"Set-Location -LiteralPath '"
        + chatUiDir.string() + "'; " + mavenRun + "\"";
    int fallbackCode = std::system(fallbackCmd.c_str());
    return fallbackCode == 0;
}

void openMonitorInBrowser() {
    // Open the monitor home page in the system default browser.
    std::system("cmd /c start \"\" \"http://127.0.0.1:8088/\"");
}

void runAllInOne() {
    unsigned short port = 9000;
    std::cout << "Chat server port (default 9000): ";
    std::string portText;
    std::getline(std::cin, portText);
    if (!portText.empty()) {
        port = static_cast<unsigned short>(std::stoi(portText));
    }

    std::cout << "Launching monitor UI..." << std::endl;
    if (!launchMonitorUi()) {
        std::cout << "Failed to launch monitor UI automatically." << std::endl;
        std::cout << "Please run UI manually: chat-ui/target/chat-ui-1.0.0.jar or mvn spring-boot:run" << std::endl;
    } else {
        std::cout << "Monitor UI launch command sent. Open http://localhost:8088" << std::endl;
        // Give Spring Boot a short moment, then open browser automatically.
        std::this_thread::sleep_for(std::chrono::milliseconds(1500));
        openMonitorInBrowser();
    }

    std::cout << "Starting chat server on port " << port << " ..." << std::endl;
    runServer(port);
}

void runLoadTest() {
    Thread_Pools pool(8);
    std::vector<int> servers = {101, 102, 103};
    const int requestCount = 100000;
    const int rounds = 10;
    std::mt19937 rng(static_cast<unsigned int>(std::time(nullptr)));
    std::discrete_distribution<int> groupDist({50, 35, 15});
    std::uniform_int_distribution<int> smallDist(64, 1024);
    std::uniform_int_distribution<int> mediumDist(1025, 8192);
    std::uniform_int_distribution<int> largeDist(8193, 32768);
    std::vector<RoundResult> allRounds;
    allRounds.reserve(rounds);

    std::cout << "\n=== Continuous Load Test Start ===\n";
    std::cout << "Rounds: " << rounds << ", Requests per round: " << requestCount << "\n";

    for (int round = 1; round <= rounds; ++round) {
        std::vector<std::future<void>> results;
        results.reserve(requestCount);
        std::unordered_map<int, int> hitCount;
        std::unordered_map<int, std::size_t> byteCount;
        std::mutex hitMutex;

        auto begin = std::chrono::steady_clock::now();
        for (int i = 0; i < requestCount; ++i) {
            int targetServer = pickServerRoundRobin(servers);
            int payloadSize = 0;
            int group = groupDist(rng);
            if (group == 0) {
                payloadSize = smallDist(rng);
            } else if (group == 1) {
                payloadSize = mediumDist(rng);
            } else {
                payloadSize = largeDist(rng);
            }

            results.emplace_back(pool.submit([i, round, targetServer, payloadSize, &hitCount, &byteCount, &hitMutex](){
                std::string payload(payloadSize, 'x');
                Connection conn(i);
                if (round == 1 && i < 10) {
                    conn.send("round#" + std::to_string(round) +
                              ", request#" + std::to_string(i) +
                              ", payload_size=" + std::to_string(payload.size()) +
                              ", routed_to=" + std::to_string(targetServer));
                }
                {
                    std::lock_guard<std::mutex> lock(hitMutex);
                    hitCount[targetServer]++;
                    byteCount[targetServer] += payload.size();
                }
            }));
        }

        for (auto& result : results) {
            result.get();
        }
        auto end = std::chrono::steady_clock::now();
        auto elapsedMs = std::chrono::duration_cast<std::chrono::milliseconds>(end - begin).count();

        RoundResult rr;
        rr.elapsedMs = elapsedMs;
        rr.hitCount = std::move(hitCount);
        rr.byteCount = std::move(byteCount);
        allRounds.push_back(std::move(rr));

        std::cout << "\n[Round " << round << "] elapsed: " << elapsedMs << " ms\n";
        for (int serverId : servers) {
            int count = allRounds.back().hitCount[serverId];
            std::size_t bytes = allRounds.back().byteCount[serverId];
            double ratio = static_cast<double>(count) * 100.0 / static_cast<double>(requestCount);
            std::cout << "Server " << serverId
                      << " -> " << count << " requests ("
                      << std::fixed << std::setprecision(2) << ratio << "%), "
                      << bytes << " bytes\n";
        }
    }

    long long totalMs = 0;
    long long minMs = std::numeric_limits<long long>::max();
    long long maxMs = 0;
    std::unordered_map<int, long long> totalHits;
    std::unordered_map<int, std::size_t> totalBytes;
    for (const auto& rr : allRounds) {
        totalMs += rr.elapsedMs;
        minMs = std::min(minMs, rr.elapsedMs);
        maxMs = std::max(maxMs, rr.elapsedMs);
        for (int serverId : servers) {
            totalHits[serverId] += rr.hitCount.at(serverId);
            totalBytes[serverId] += rr.byteCount.at(serverId);
        }
    }

    const long long totalRequestsAllRounds = static_cast<long long>(requestCount) * rounds;
    std::cout << "\n=== Continuous Load Test Summary ===\n";
    std::cout << "Total rounds: " << rounds << "\n";
    std::cout << "Total requests: " << totalRequestsAllRounds << "\n";
    std::cout << "Time(ms): avg=" << (totalMs / rounds) << ", min=" << minMs << ", max=" << maxMs << "\n";
    for (int serverId : servers) {
        double ratio = static_cast<double>(totalHits[serverId]) * 100.0 /
                       static_cast<double>(totalRequestsAllRounds);
        std::cout << "Server " << serverId
                  << " -> " << totalHits[serverId]
                  << " requests (" << std::fixed << std::setprecision(2) << ratio << "%), "
                  << totalBytes[serverId] << " bytes total\n";
    }
}

void runServer(unsigned short port) {
    WinSockSession wsa;
    Thread_Pools broadcastPool(8);

    SOCKET listenFd = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (listenFd == INVALID_SOCKET) {
        throw std::runtime_error("create listen socket failed");
    }

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);
    addr.sin_addr.s_addr = htonl(INADDR_ANY);
    if (bind(listenFd, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) == SOCKET_ERROR) {
        closesocket(listenFd);
        throw std::runtime_error("bind failed");
    }
    if (listen(listenFd, SOMAXCONN) == SOCKET_ERROR) {
        closesocket(listenFd);
        throw std::runtime_error("listen failed");
    }

    std::cout << "Server listening on 0.0.0.0:" << port << std::endl;
    std::atomic<bool> running{true};
    std::mutex clientsMutex;
    std::vector<std::shared_ptr<ChatClient>> clients;
    std::vector<std::thread> recvThreads;
    std::atomic<int> clientIdGen{1};

    auto broadcastMessage = [&](int senderId, const std::string& text) {
        broadcastPool.submit([&, senderId, text]() {
            std::vector<std::shared_ptr<ChatClient>> snapshot;
            {
                std::lock_guard<std::mutex> lock(clientsMutex);
                snapshot = clients;
            }

            for (const auto& c : snapshot) {
                if (senderId != 0 && c->id == senderId) {
                    continue;
                }
                if (!sendToClient(c, text)) {
                    // Ignore send errors here; disconnected clients are cleaned by recv thread.
                }
            }
        });
    };

    auto sendUserList = [&](const std::shared_ptr<ChatClient>& toClient) {
        std::ostringstream oss;
        oss << "[system] Online users:";
        {
            std::lock_guard<std::mutex> lock(clientsMutex);
            for (const auto& c : clients) {
                oss << " " << c->name << "(#" << c->id << ")";
            }
        }
        sendToClient(toClient, oss.str());
    };

    auto sendPrivateMessage = [&](const std::shared_ptr<ChatClient>& fromClient,
                                  const std::string& target,
                                  const std::string& text) {
        std::shared_ptr<ChatClient> toClient;
        {
            std::lock_guard<std::mutex> lock(clientsMutex);
            bool numericTarget = !target.empty() &&
                                 std::all_of(target.begin(), target.end(), [](char ch) { return ch >= '0' && ch <= '9'; });
            for (const auto& c : clients) {
                if (numericTarget) {
                    if (c->id == std::stoi(target)) {
                        toClient = c;
                        break;
                    }
                } else if (c->name == target) {
                    toClient = c;
                    break;
                }
            }
        }

        if (!toClient) {
            sendToClient(fromClient, "[system] target user not found: " + target);
            return;
        }
        if (toClient->id == fromClient->id) {
            sendToClient(fromClient, "[system] cannot send private message to yourself.");
            return;
        }

        sendToClient(toClient, "[private from " + fromClient->name + "] " + text);
        sendToClient(fromClient, "[private to " + toClient->name + "] " + text);
    };

    std::thread acceptThread([&]() {
        while (running.load()) {
            SOCKET clientFd = accept(listenFd, nullptr, nullptr);
            if (clientFd == INVALID_SOCKET) {
                if (running.load()) {
                    std::cerr << "Accept failed." << std::endl;
                }
                break;
            }

            auto client = std::make_shared<ChatClient>();
            client->id = clientIdGen.fetch_add(1);
            client->socketFd = clientFd;
            client->name = "user" + std::to_string(client->id);
            {
                std::lock_guard<std::mutex> lock(clientsMutex);
                clients.push_back(client);
            }

            std::cout << client->name << "(#" << client->id << ") joined." << std::endl;
            sendToClient(client, "Welcome! your name is " + client->name + " (#" + std::to_string(client->id) + ")");
            sendToClient(client, "Commands: /name <newName>, /list, /msg <name|id> <text>, quit, /shutdown");
            broadcastMessage(0, "[system] " + client->name + " joined.");
            sendUserList(client);

            recvThreads.emplace_back([&, client]() {
                char buf[1024];
                std::string pending;
                while (running.load()) {
                    int n = recv(client->socketFd, buf, sizeof(buf) - 1, 0);
                    if (n <= 0) {
                        break;
                    }
                    buf[n] = '\0';
                    pending += buf;

                    std::size_t pos = 0;
                    while ((pos = pending.find('\n')) != std::string::npos) {
                        std::string line = pending.substr(0, pos);
                        pending.erase(0, pos + 1);
                        if (!line.empty() && line.back() == '\r') {
                            line.pop_back();
                        }

                        if (line == "quit") {
                            break;
                        }
                        if (line == "/shutdown") {
                            broadcastMessage(0, "[system] server shutdown requested.");
                            running.store(false);
                            closesocket(listenFd);
                            break;
                        }
                        if (line == "/list") {
                            sendUserList(client);
                            continue;
                        }
                        if (line.rfind("/name ", 0) == 0) {
                            std::string newName = line.substr(6);
                            if (newName.empty()) {
                                sendToClient(client, "[system] usage: /name <newName>");
                                continue;
                            }
                            bool duplicated = false;
                            {
                                std::lock_guard<std::mutex> lock(clientsMutex);
                                for (const auto& c : clients) {
                                    if (c->name == newName) {
                                        duplicated = true;
                                        break;
                                    }
                                }
                                if (!duplicated) {
                                    std::string old = client->name;
                                    client->name = newName;
                                    broadcastMessage(0, "[system] " + old + " renamed to " + newName);
                                }
                            }
                            if (duplicated) {
                                sendToClient(client, "[system] name already taken.");
                            }
                            continue;
                        }
                        if (line.rfind("/msg ", 0) == 0) {
                            std::string body = line.substr(5);
                            std::size_t sp = body.find(' ');
                            if (sp == std::string::npos || sp == 0 || sp == body.size() - 1) {
                                sendToClient(client, "[system] usage: /msg <name|id> <text>");
                                continue;
                            }
                            std::string target = body.substr(0, sp);
                            std::string text = body.substr(sp + 1);
                            sendPrivateMessage(client, target, text);
                            continue;
                        }

                        std::string msg = "[" + client->name + "] " + line;
                        std::cout << msg << std::endl;
                        broadcastMessage(client->id, msg);
                    }
                }

                closesocket(client->socketFd);
                {
                    std::lock_guard<std::mutex> lock(clientsMutex);
                    clients.erase(
                        std::remove_if(clients.begin(), clients.end(),
                                       [&](const std::shared_ptr<ChatClient>& c) { return c->id == client->id; }),
                        clients.end());
                }
                broadcastMessage(0, "[system] " + client->name + " left.");
                std::cout << client->name << "(#" << client->id << ") disconnected." << std::endl;
            });
        }
    });

    std::cout << "Multi-client chat started." << std::endl;
    std::cout << "Client send `quit` to disconnect, `/shutdown` to stop server." << std::endl;

    while (running.load()) {
        std::this_thread::sleep_for(std::chrono::milliseconds(200));
    }

    running.store(false);
    closesocket(listenFd);
    if (acceptThread.joinable()) {
        acceptThread.join();
    }
    {
        std::lock_guard<std::mutex> lock(clientsMutex);
        for (const auto& c : clients) {
            shutdown(c->socketFd, SD_BOTH);
            closesocket(c->socketFd);
        }
    }
    for (auto& t : recvThreads) {
        if (t.joinable()) {
            t.join();
        }
    }
}

void runClient(const std::string& ip, unsigned short port) {
    WinSockSession wsa;

    SOCKET sock = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (sock == INVALID_SOCKET) {
        throw std::runtime_error("create client socket failed");
    }

    sockaddr_in serverAddr{};
    serverAddr.sin_family = AF_INET;
    serverAddr.sin_port = htons(port);
    if (inet_pton(AF_INET, ip.c_str(), &serverAddr.sin_addr) != 1) {
        closesocket(sock);
        throw std::runtime_error("invalid ip address");
    }

    if (connect(sock, reinterpret_cast<sockaddr*>(&serverAddr), sizeof(serverAddr)) == SOCKET_ERROR) {
        closesocket(sock);
        throw std::runtime_error("connect failed");
    }

    std::cout << "Connected to " << ip << ":" << port << ". Start chat, input quit to stop." << std::endl;
    std::atomic<bool> running{true};
    std::thread recvThread([&]() {
        char buf[1024];
        while (running.load()) {
            int n = recv(sock, buf, sizeof(buf) - 1, 0);
            if (n <= 0) {
                running.store(false);
                break;
            }
            buf[n] = '\0';
            std::cout << "[server] " << buf << std::flush;
        }
    });

    std::string line;
    while (running.load() && std::getline(std::cin, line)) {
        if (!sendLine(sock, line)) {
            running.store(false);
            break;
        }
        if (line == "quit") {
            running.store(false);
            break;
        }
    }

    shutdown(sock, SD_BOTH);
    closesocket(sock);
    if (recvThread.joinable()) {
        recvThread.join();
    }
}

int main() {
    while (true) {
        int mode = -1;
        try {
            std::cout << "\nSelect mode:\n"
                      << "1) Continuous load test\n"
                      << "2) TCP server\n"
                      << "3) TCP client\n"
                      << "4) One-click start (UI + server)\n"
                      << "0) Exit program\n"
                      << "Input: ";

            if (!(std::cin >> mode)) {
                throw std::runtime_error("invalid mode input, please enter a number.");
            }
            std::cin.ignore(std::numeric_limits<std::streamsize>::max(), '\n');

            if (mode == 0) {
                std::cout << "Bye." << std::endl;
                break;
            } else if (mode == 1) {
                runLoadTest();
            } else if (mode == 2) {
                unsigned short port = 9000;
                std::cout << "Server port (default 9000): ";
                std::string portText;
                std::getline(std::cin, portText);
                if (!portText.empty()) {
                    port = static_cast<unsigned short>(std::stoi(portText));
                }
                runServer(port);
            } else if (mode == 3) {
                std::string ip = "127.0.0.1";
                unsigned short port = 9000;
                std::cout << "Server IP (default 127.0.0.1): ";
                std::string ipText;
                std::getline(std::cin, ipText);
                if (!ipText.empty()) {
                    ip = ipText;
                }
                std::cout << "Server port (default 9000): ";
                std::string portText;
                std::getline(std::cin, portText);
                if (!portText.empty()) {
                    port = static_cast<unsigned short>(std::stoi(portText));
                }
                runClient(ip, port);
            } else if (mode == 4) {
                runAllInOne();
            } else {
                std::cout << "Unknown mode." << std::endl;
            }
        } catch (const std::exception& ex) {
            std::string msg = ex.what();
            writeExitReport(mode, msg);
            std::cerr << "Error: " << msg << std::endl;
            std::cerr << "Exit report: " << (executableDir() / "exit-error-report.log").string() << std::endl;
            std::cerr << "Back to menu..." << std::endl;
            std::cin.clear();
            std::cin.ignore(std::numeric_limits<std::streamsize>::max(), '\n');
        } catch (...) {
            std::string msg = "Unknown non-std exception.";
            writeExitReport(mode, msg);
            std::cerr << "Error: " << msg << std::endl;
            std::cerr << "Exit report: " << (executableDir() / "exit-error-report.log").string() << std::endl;
            std::cerr << "Back to menu..." << std::endl;
            std::cin.clear();
            std::cin.ignore(std::numeric_limits<std::streamsize>::max(), '\n');
        }
    }

    return 0;
}
