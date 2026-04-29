package com.chatroom.ui.runtime;

import org.springframework.beans.factory.annotation.Value;
import org.springframework.stereotype.Service;

import jakarta.annotation.PreDestroy;
import java.io.BufferedReader;
import java.io.BufferedWriter;
import java.io.IOException;
import java.io.InputStreamReader;
import java.io.OutputStreamWriter;
import java.nio.charset.StandardCharsets;
import java.nio.file.Files;
import java.nio.file.Path;
import java.nio.file.Paths;
import java.time.Instant;
import java.util.ArrayDeque;
import java.util.ArrayList;
import java.util.Deque;
import java.util.HashMap;
import java.util.LinkedHashMap;
import java.util.List;
import java.util.Map;
import java.util.concurrent.ExecutorService;
import java.util.concurrent.Executors;
import java.util.concurrent.atomic.AtomicLong;
import java.util.regex.Matcher;
import java.util.regex.Pattern;

@Service
public class ChatRuntimeService {
    private static final int MAX_LOGS = 300;
    private static final Pattern JOINED_PATTERN = Pattern.compile("^(.+)\\(#(\\d+)\\) joined\\.$");
    private static final Pattern DISCONNECTED_PATTERN = Pattern.compile("^(.+)\\(#(\\d+)\\) disconnected\\.$");

    private final Object lock = new Object();
    private final Deque<Map<String, Object>> serverLogs = new ArrayDeque<>();
    private final Map<Integer, String> serverOnlineUserMap = new LinkedHashMap<>();
    private final Map<String, ClientSession> clients = new HashMap<>();
    private final ExecutorService ioExecutor = Executors.newCachedThreadPool();

    private Process serverProcess;
    private final AtomicLong serverOnlineUsers = new AtomicLong();
    private final AtomicLong serverTotalConnections = new AtomicLong();

    @Value("${chat.runtime.workspaceRoot:..}")
    private String workspaceRootConfig;

    @Value("${chat.runtime.exePath:../cmake-build-release/Release/untitled18.exe}")
    private String exePathConfig;

    public Map<String, Object> startServer(int port) {
        stopServer();
        try {
            Process process = createProcess();
            BufferedWriter writer = createWriter(process);
            writer.write("2");
            writer.newLine();
            writer.write(String.valueOf(port));
            writer.newLine();
            writer.flush();
            serverOnlineUsers.set(0);
            serverTotalConnections.set(0);
            synchronized (lock) {
                serverOnlineUserMap.clear();
            }
            serverProcess = process;
            pumpLogs(process, "SERVER", serverLogs);
            addLog(serverLogs, "SYSTEM", "服务端启动成功，端口=" + port);
            return serverStatus();
        } catch (IOException e) {
            addLog(serverLogs, "ERROR", "服务端启动失败: " + e.getMessage());
            throw new RuntimeException("服务端启动失败: " + e.getMessage(), e);
        }
    }

    public Map<String, Object> stopServer() {
        if (serverProcess != null) {
            serverProcess.destroy();
            addLog(serverLogs, "SYSTEM", "服务端已停止");
            serverProcess = null;
        }
        serverOnlineUsers.set(0);
        synchronized (lock) {
            serverOnlineUserMap.clear();
        }
        return serverStatus();
    }

    public Map<String, Object> startClient(String clientId, String ip, int port) {
        return startClient(clientId, ip, port, "user", "");
    }

    public Map<String, Object> startClient(String clientId, String ip, int port, String role, String username) {
        stopClient(clientId);
        try {
            Process process = createProcess();
            BufferedWriter writer = createWriter(process);
            writer.write("3");
            writer.newLine();
            writer.write(ip);
            writer.newLine();
            writer.write(String.valueOf(port));
            writer.newLine();
            writer.flush();
            if (username != null && !username.isBlank()) {
                writer.write("/name " + username.trim());
                writer.newLine();
                writer.flush();
            }
            ClientSession session = new ClientSession(process, writer);
            session.role = (role == null || role.isBlank()) ? "user" : role.trim();
            session.username = (username == null || username.isBlank()) ? "匿名用户" : username.trim();
            synchronized (lock) {
                clients.put(clientId, session);
            }
            pumpClientLogs(clientId, process);
            addClientLog(clientId, "SYSTEM", "客户端已连接请求发送: " + ip + ":" + port +
                "，角色=" + session.role + "，用户名=" + session.username);
            return clientStatus(clientId);
        } catch (IOException e) {
            addClientLog(clientId, "ERROR", "客户端启动失败: " + e.getMessage());
            throw new RuntimeException("客户端启动失败: " + e.getMessage(), e);
        }
    }

    public Map<String, Object> stopClient(String clientId) {
        ClientSession session;
        synchronized (lock) {
            session = clients.get(clientId);
        }
        if (session == null) {
            return clientStatus(clientId);
        }
        try {
            if (session.inputWriter != null) {
                session.inputWriter.write("quit");
                session.inputWriter.newLine();
                session.inputWriter.flush();
            }
        } catch (IOException ignored) {
            // Ignore and continue destroy.
        }
        session.process.destroy();
        addClientLog(clientId, "SYSTEM", "客户端已停止");
        synchronized (lock) {
            clients.remove(clientId);
        }
        return clientStatus(clientId);
    }

    public Map<String, Object> sendClientMessage(String clientId, String message) {
        ClientSession session;
        synchronized (lock) {
            session = clients.get(clientId);
        }
        if (session == null || !session.process.isAlive() || session.inputWriter == null) {
            throw new RuntimeException("客户端未运行，无法发送消息");
        }
        try {
            session.inputWriter.write(message);
            session.inputWriter.newLine();
            session.inputWriter.flush();
            session.sentMessages.incrementAndGet();
            addClientLog(clientId, "SEND", message);
            return clientStatus(clientId);
        } catch (IOException e) {
            throw new RuntimeException("发送消息失败: " + e.getMessage(), e);
        }
    }

    public Map<String, Object> serverStatus() {
        Map<String, Object> status = new LinkedHashMap<>();
        status.put("running", serverProcess != null && serverProcess.isAlive());
        status.put("pid", serverProcess != null ? serverProcess.pid() : -1);
        status.put("totalConnections", serverTotalConnections.get());
        synchronized (lock) {
            status.put("logs", new ArrayList<>(serverLogs));
            List<Map<String, Object>> onlineUserList = new ArrayList<>();
            for (Map.Entry<Integer, String> entry : serverOnlineUserMap.entrySet()) {
                Map<String, Object> row = new LinkedHashMap<>();
                row.put("id", entry.getKey());
                row.put("name", entry.getValue());
                onlineUserList.add(row);
            }
            long uiOnlineUsers = clients.values().stream().filter(s -> s.process.isAlive()).count();
            status.put("onlineUsers", Math.max(serverOnlineUsers.get(), uiOnlineUsers));
            if (onlineUserList.isEmpty()) {
                int idx = 1;
                for (Map.Entry<String, ClientSession> entry : clients.entrySet()) {
                    ClientSession s = entry.getValue();
                    if (!s.process.isAlive()) {
                        continue;
                    }
                    Map<String, Object> row = new LinkedHashMap<>();
                    row.put("id", idx++);
                    row.put("name", s.username + " (" + entry.getKey() + ")");
                    onlineUserList.add(row);
                }
            }
            status.put("onlineUserList", onlineUserList);
        }
        return status;
    }

    public Map<String, Object> clientStatus(String clientId) {
        ClientSession session;
        synchronized (lock) {
            session = clients.get(clientId);
        }
        Map<String, Object> status = new LinkedHashMap<>();
        status.put("clientId", clientId);
        status.put("running", session != null && session.process.isAlive());
        status.put("pid", session != null ? session.process.pid() : -1);
        status.put("sentMessages", session != null ? session.sentMessages.get() : 0);
        status.put("receivedMessages", session != null ? session.receivedMessages.get() : 0);
        status.put("role", session != null ? session.role : "user");
        status.put("username", session != null ? session.username : "匿名用户");
        status.put("logs", session != null ? new ArrayList<>(session.logs) : new ArrayList<>());
        return status;
    }

    public List<Map<String, Object>> onlineClients() {
        List<Map<String, Object>> list = new ArrayList<>();
        synchronized (lock) {
            for (Map.Entry<String, ClientSession> entry : clients.entrySet()) {
                String clientId = entry.getKey();
                ClientSession s = entry.getValue();
                Map<String, Object> row = new LinkedHashMap<>();
                row.put("clientId", clientId);
                row.put("running", s.process.isAlive());
                row.put("pid", s.process.pid());
                row.put("username", s.username);
                row.put("role", s.role);
                row.put("sentMessages", s.sentMessages.get());
                row.put("receivedMessages", s.receivedMessages.get());
                list.add(row);
            }
        }
        return list;
    }

    private Process createProcess() throws IOException {
        Path exePath = resolveExePath();
        if (!Files.exists(exePath)) {
            throw new IOException("未找到可执行文件: " + exePath);
        }
        ProcessBuilder pb = new ProcessBuilder(exePath.toString());
        pb.directory(resolveWorkspaceRoot().toFile());
        pb.redirectErrorStream(true);
        return pb.start();
    }

    private Path resolveWorkspaceRoot() {
        return Paths.get(workspaceRootConfig).toAbsolutePath().normalize();
    }

    private Path resolveExePath() {
        return Paths.get(exePathConfig).toAbsolutePath().normalize();
    }

    private BufferedWriter createWriter(Process process) {
        return new BufferedWriter(new OutputStreamWriter(process.getOutputStream(), StandardCharsets.UTF_8));
    }

    private void pumpLogs(Process process, String source, Deque<Map<String, Object>> target) {
        ioExecutor.submit(() -> {
            try (BufferedReader reader = new BufferedReader(new InputStreamReader(process.getInputStream(), StandardCharsets.UTF_8))) {
                String line;
                while ((line = reader.readLine()) != null) {
                    processLogLine(source, line);
                    addLog(target, source, line);
                }
            } catch (IOException e) {
                addLog(target, "ERROR", "日志读取失败: " + e.getMessage());
            }
        });
    }

    private void pumpClientLogs(String clientId, Process process) {
        ioExecutor.submit(() -> {
            try (BufferedReader reader = new BufferedReader(new InputStreamReader(process.getInputStream(), StandardCharsets.UTF_8))) {
                String line;
                while ((line = reader.readLine()) != null) {
                    processClientLine(clientId, line);
                    addClientLog(clientId, "CLIENT", line);
                }
            } catch (IOException e) {
                addClientLog(clientId, "ERROR", "日志读取失败: " + e.getMessage());
            }
        });
    }

    private void processLogLine(String source, String line) {
        if ("SERVER".equals(source)) {
            Matcher joinedMatcher = JOINED_PATTERN.matcher(line);
            if (joinedMatcher.matches()) {
                int userId = Integer.parseInt(joinedMatcher.group(2));
                String userName = joinedMatcher.group(1);
                serverOnlineUsers.incrementAndGet();
                serverTotalConnections.incrementAndGet();
                synchronized (lock) {
                    serverOnlineUserMap.put(userId, userName);
                }
                return;
            }

            Matcher disconnectedMatcher = DISCONNECTED_PATTERN.matcher(line);
            if (disconnectedMatcher.matches()) {
                int userId = Integer.parseInt(disconnectedMatcher.group(2));
                long curr = serverOnlineUsers.get();
                if (curr > 0) {
                    serverOnlineUsers.decrementAndGet();
                }
                synchronized (lock) {
                    serverOnlineUserMap.remove(userId);
                }
            }
        }
    }

    private void processClientLine(String clientId, String line) {
        if (line.startsWith("[server]")) {
            synchronized (lock) {
                ClientSession session = clients.get(clientId);
                if (session != null) {
                    session.receivedMessages.incrementAndGet();
                }
            }
        }
    }

    private void addLog(Deque<Map<String, Object>> target, String source, String text) {
        Map<String, Object> row = new LinkedHashMap<>();
        row.put("time", Instant.now().toString());
        row.put("source", source);
        row.put("text", text);
        synchronized (lock) {
            target.addFirst(row);
            while (target.size() > MAX_LOGS) {
                target.removeLast();
            }
        }
    }

    private void addClientLog(String clientId, String source, String text) {
        Map<String, Object> row = new LinkedHashMap<>();
        row.put("time", Instant.now().toString());
        row.put("source", source);
        row.put("text", text);
        synchronized (lock) {
            ClientSession session = clients.get(clientId);
            if (session == null) {
                return;
            }
            session.logs.addFirst(row);
            while (session.logs.size() > MAX_LOGS) {
                session.logs.removeLast();
            }
        }
    }

    @PreDestroy
    public void shutdownAll() {
        synchronized (lock) {
            for (String clientId : new ArrayList<>(clients.keySet())) {
                stopClient(clientId);
            }
        }
        stopServer();
        ioExecutor.shutdownNow();
    }

    private static class ClientSession {
        private final Process process;
        private final BufferedWriter inputWriter;
        private final Deque<Map<String, Object>> logs = new ArrayDeque<>();
        private final AtomicLong sentMessages = new AtomicLong();
        private final AtomicLong receivedMessages = new AtomicLong();
        private String role = "user";
        private String username = "匿名用户";

        private ClientSession(Process process, BufferedWriter inputWriter) {
            this.process = process;
            this.inputWriter = inputWriter;
        }
    }
}
