package com.chatroom.ui.runtime;

import org.springframework.beans.factory.annotation.Value;
import org.springframework.stereotype.Service;
import org.springframework.web.multipart.MultipartFile;

import jakarta.annotation.PostConstruct;
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
import java.nio.file.StandardCopyOption;
import java.time.Instant;
import java.util.Comparator;
import java.util.HashSet;
import java.util.Locale;
import java.util.Set;
import java.util.UUID;
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
    private static final Pattern SYSTEM_JOIN_PATTERN = Pattern.compile("^\\[system\\] (.+?) joined\\.?$");
    private static final Pattern SYSTEM_LEFT_PATTERN = Pattern.compile("^\\[system\\] (.+?) left\\.?$");
    private static final Pattern SYSTEM_RENAME_PATTERN = Pattern.compile("^\\[system\\] (.+?) renamed to (.+)$");
    private static final Pattern USER_IN_LIST_PATTERN = Pattern.compile("([^\\s(]+)\\(#(\\d+)\\)");

    private final Object lock = new Object();
    private final Deque<Map<String, Object>> serverLogs = new ArrayDeque<>();
    private final Map<Integer, String> serverOnlineUserMap = new LinkedHashMap<>();
    private final Map<Integer, UserRecord> userRegistry = new LinkedHashMap<>();
    private final Map<String, Integer> userNameToId = new HashMap<>();
    private final Map<String, ClientSession> clients = new HashMap<>();
    private final ExecutorService ioExecutor = Executors.newCachedThreadPool();

    private Process serverProcess;
    private final AtomicLong serverOnlineUsers = new AtomicLong();
    private final AtomicLong serverTotalConnections = new AtomicLong();

    @Value("${chat.runtime.workspaceRoot:..}")
    private String workspaceRootConfig;

    @Value("${chat.runtime.exePath:../cmake-build-release/Release/untitled18.exe}")
    private String exePathConfig;

    @Value("${chat.media.uploadDir:uploads}")
    private String uploadDirConfig;

    @Value("${chat.media.maxFileSizeMb:10}")
    private long maxFileSizeMb;

    private Path uploadRoot;

    @PostConstruct
    public void initUploadDir() throws IOException {
        uploadRoot = Paths.get(uploadDirConfig).toAbsolutePath().normalize();
        Files.createDirectories(uploadRoot);
    }

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
                userRegistry.clear();
                userNameToId.clear();
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
            markAllUsersOffline();
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
            writer.write("/list");
            writer.newLine();
            writer.flush();
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
            addClientLog(clientId, "SEND", message, ChatMediaParser.enrich(message));
            return clientStatus(clientId);
        } catch (IOException e) {
            throw new RuntimeException("发送消息失败: " + e.getMessage(), e);
        }
    }

    public Map<String, Object> uploadAndSendMedia(String clientId, MultipartFile file) {
        if (file == null || file.isEmpty()) {
            throw new RuntimeException("请选择要发送的文件");
        }
        long maxBytes = maxFileSizeMb * 1024L * 1024L;
        if (file.getSize() > maxBytes) {
            throw new RuntimeException("文件过大，最大允许 " + maxFileSizeMb + " MB");
        }

        String originalName = file.getOriginalFilename() == null ? "file.bin" : file.getOriginalFilename();
        String ext = "";
        int dot = originalName.lastIndexOf('.');
        if (dot >= 0) {
            ext = originalName.substring(dot).toLowerCase(Locale.ROOT);
        }

        boolean isImage = file.getContentType() != null && file.getContentType().startsWith("image/");
        if (!isImage) {
            isImage = ext.matches("\\.(png|jpg|jpeg|gif|webp|bmp|svg)$");
        }
        String mediaType = isImage ? "image" : "file";

        try {
            String storedName = UUID.randomUUID() + ext;
            Path target = uploadRoot.resolve(storedName);
            Files.copy(file.getInputStream(), target, StandardCopyOption.REPLACE_EXISTING);

            String mediaUrl = "/uploads/" + storedName;
            String mediaLine = ChatMediaParser.buildMediaLine(mediaType, originalName, mediaUrl, file.getSize());
            Map<String, Object> meta = ChatMediaParser.enrich(mediaLine);
            meta.put("fileName", originalName);
            meta.put("mediaUrl", mediaUrl);
            meta.put("fileSize", file.getSize());
            meta.put("msgType", mediaType);

            sendClientMessage(clientId, mediaLine);
            return clientStatus(clientId);
        } catch (IOException e) {
            throw new RuntimeException("文件上传失败: " + e.getMessage(), e);
        }
    }

    public Map<String, Object> serverStatus() {
        Map<String, Object> status = new LinkedHashMap<>();
        status.put("running", serverProcess != null && serverProcess.isAlive());
        status.put("pid", serverProcess != null ? serverProcess.pid() : -1);
        status.put("totalConnections", serverTotalConnections.get());
        synchronized (lock) {
            status.put("logs", new ArrayList<>(serverLogs));
            appendUserLists(status);
            long uiOnlineUsers = clients.values().stream().filter(s -> s.process.isAlive()).count();
            @SuppressWarnings("unchecked")
            List<Map<String, Object>> onlineList = (List<Map<String, Object>>) status.get("onlineUserList");
            status.put("onlineUsers", Math.max(onlineList.size(), uiOnlineUsers));
            if (onlineList.isEmpty()) {
                int idx = 1;
                List<Map<String, Object>> fallback = new ArrayList<>();
                for (Map.Entry<String, ClientSession> entry : clients.entrySet()) {
                    ClientSession s = entry.getValue();
                    if (!s.process.isAlive()) {
                        continue;
                    }
                    Map<String, Object> row = new LinkedHashMap<>();
                    row.put("id", idx++);
                    row.put("name", s.username);
                    row.put("status", "online");
                    fallback.add(row);
                }
                status.put("onlineUserList", fallback);
                status.put("onlineUsers", fallback.size());
            }
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
        synchronized (lock) {
            appendUserLists(status);
        }
        return status;
    }

    public Map<String, Object> userPresenceSnapshot() {
        Map<String, Object> snapshot = new LinkedHashMap<>();
        synchronized (lock) {
            appendUserLists(snapshot);
        }
        return snapshot;
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
                    if (line.startsWith("[server]")) {
                        processClientLine(clientId, line);
                    } else if (!isClientProcessNoise(line)) {
                        addClientLog(clientId, "SYSTEM", line);
                    }
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
                serverTotalConnections.incrementAndGet();
                synchronized (lock) {
                    recordUserOnline(userId, userName);
                    refreshOnlineCount();
                }
                return;
            }

            Matcher disconnectedMatcher = DISCONNECTED_PATTERN.matcher(line);
            if (disconnectedMatcher.matches()) {
                int userId = Integer.parseInt(disconnectedMatcher.group(2));
                synchronized (lock) {
                    recordUserOffline(userId);
                    refreshOnlineCount();
                }
            }
        }
    }

    private void appendUserLists(Map<String, Object> target) {
        List<Map<String, Object>> online = new ArrayList<>();
        List<Map<String, Object>> offline = new ArrayList<>();
        for (UserRecord user : userRegistry.values()) {
            Map<String, Object> row = user.toMap();
            if (user.online) {
                online.add(row);
            } else {
                offline.add(row);
            }
        }
        online.sort(Comparator.comparingInt(u -> (Integer) u.get("id")));
        offline.sort((a, b) -> {
            String ta = String.valueOf(a.getOrDefault("offlineAt", ""));
            String tb = String.valueOf(b.getOrDefault("offlineAt", ""));
            return tb.compareTo(ta);
        });
        target.put("onlineUserList", online);
        target.put("offlineUserList", offline);
        target.put("offlineUsers", offline.size());
    }

    private void recordUserOnline(int userId, String userName) {
        Integer existingId = userNameToId.get(userName);
        if (existingId != null && !existingId.equals(userId)) {
            userRegistry.remove(existingId);
            serverOnlineUserMap.remove(existingId);
        }
        UserRecord record = userRegistry.computeIfAbsent(userId, ignored -> new UserRecord());
        record.id = userId;
        record.name = userName;
        record.online = true;
        record.lastSeen = Instant.now();
        record.offlineAt = null;
        userNameToId.put(userName, userId);
        if (userId > 0) {
            serverOnlineUserMap.put(userId, userName);
        }
    }

    private void recordUserOffline(int userId) {
        UserRecord record = userRegistry.get(userId);
        if (record != null) {
            record.online = false;
            record.offlineAt = Instant.now();
        }
        serverOnlineUserMap.remove(userId);
    }

    private void recordUserOfflineByName(String userName) {
        Integer userId = userNameToId.get(userName);
        if (userId != null) {
            recordUserOffline(userId);
            return;
        }
        for (UserRecord record : userRegistry.values()) {
            if (userName.equals(record.name)) {
                record.online = false;
                record.offlineAt = Instant.now();
                serverOnlineUserMap.remove(record.id);
                break;
            }
        }
    }

    private void recordUserRename(String oldName, String newName) {
        Integer userId = userNameToId.remove(oldName);
        if (userId == null) {
            return;
        }
        userNameToId.put(newName, userId);
        UserRecord record = userRegistry.get(userId);
        if (record != null) {
            record.name = newName;
            record.lastSeen = Instant.now();
            if (record.online) {
                serverOnlineUserMap.put(userId, newName);
            }
        }
    }

    private void syncUsersFromListLine(String payload) {
        if (!payload.contains("Online users:")) {
            return;
        }
        Set<Integer> seen = new HashSet<>();
        Matcher matcher = USER_IN_LIST_PATTERN.matcher(payload);
        while (matcher.find()) {
            int userId = Integer.parseInt(matcher.group(2));
            String userName = matcher.group(1);
            recordUserOnline(userId, userName);
            seen.add(userId);
        }
        for (UserRecord record : userRegistry.values()) {
            if (record.online && record.id > 0 && !seen.isEmpty() && !seen.contains(record.id)) {
                record.online = false;
                record.offlineAt = Instant.now();
                serverOnlineUserMap.remove(record.id);
            }
        }
        refreshOnlineCount();
    }

    private void processUserPresenceMessage(String payload) {
        Matcher joinMatcher = SYSTEM_JOIN_PATTERN.matcher(payload);
        if (joinMatcher.matches()) {
            String name = joinMatcher.group(1).trim();
            Integer userId = userNameToId.get(name);
            if (userId != null) {
                recordUserOnline(userId, name);
            } else {
                boolean revived = false;
                for (UserRecord record : userRegistry.values()) {
                    if (name.equals(record.name)) {
                        record.online = true;
                        record.lastSeen = Instant.now();
                        record.offlineAt = null;
                        userNameToId.put(name, record.id);
                        if (record.id > 0) {
                            serverOnlineUserMap.put(record.id, name);
                        }
                        revived = true;
                        break;
                    }
                }
                if (!revived) {
                    int tempId = -(userRegistry.size() + 1);
                    recordUserOnline(tempId, name);
                }
            }
            refreshOnlineCount();
            return;
        }

        Matcher leftMatcher = SYSTEM_LEFT_PATTERN.matcher(payload);
        if (leftMatcher.matches()) {
            recordUserOfflineByName(leftMatcher.group(1).trim());
            refreshOnlineCount();
            return;
        }

        Matcher renameMatcher = SYSTEM_RENAME_PATTERN.matcher(payload);
        if (renameMatcher.matches()) {
            recordUserRename(renameMatcher.group(1).trim(), renameMatcher.group(2).trim());
            return;
        }

        if (payload.contains("Online users:")) {
            syncUsersFromListLine(payload);
        }
    }

    private void refreshOnlineCount() {
        long count = userRegistry.values().stream().filter(u -> u.online).count();
        serverOnlineUsers.set(count);
    }

    private void markAllUsersOffline() {
        Instant now = Instant.now();
        for (UserRecord record : userRegistry.values()) {
            if (record.online) {
                record.online = false;
                record.offlineAt = now;
            }
        }
    }

    private boolean isClientProcessNoise(String line) {
        if (line == null || line.isBlank()) {
            return true;
        }
        String t = line.trim();
        return t.startsWith("Select mode")
            || t.startsWith("Input:")
            || t.startsWith("Server IP")
            || t.startsWith("Server port")
            || t.startsWith("Connected to")
            || t.startsWith("Bye.")
            || t.equals("0) Exit program")
            || t.matches("\\d+\\).*");
    }

    private void processClientLine(String clientId, String line) {
        if (line.startsWith("[server]")) {
            synchronized (lock) {
                ClientSession session = clients.get(clientId);
                if (session != null) {
                    session.receivedMessages.incrementAndGet();
                }
            }
            String payload = line.substring(8).trim();
            synchronized (lock) {
                processUserPresenceMessage(payload);
            }
            addClientLog(clientId, "CLIENT", payload, ChatMediaParser.enrich(payload));
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
        addClientLog(clientId, source, text, ChatMediaParser.enrich(text));
    }

    private void addClientLog(String clientId, String source, String text, Map<String, Object> meta) {
        Map<String, Object> row = new LinkedHashMap<>();
        row.put("time", Instant.now().toString());
        row.put("source", source);
        row.put("text", text);
        row.put("msgType", meta.getOrDefault("msgType", "text"));
        row.put("fileName", meta.getOrDefault("fileName", ""));
        row.put("mediaUrl", meta.getOrDefault("mediaUrl", ""));
        row.put("fileSize", meta.getOrDefault("fileSize", 0L));
        row.put("sender", meta.getOrDefault("sender", ""));
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

    private static class UserRecord {
        private int id;
        private String name = "";
        private boolean online;
        private Instant lastSeen;
        private Instant offlineAt;

        private Map<String, Object> toMap() {
            Map<String, Object> row = new LinkedHashMap<>();
            row.put("id", id);
            row.put("name", name);
            row.put("status", online ? "online" : "offline");
            row.put("lastSeen", lastSeen != null ? lastSeen.toString() : "");
            row.put("offlineAt", offlineAt != null ? offlineAt.toString() : "");
            return row;
        }
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
