package com.chatroom.ui.runtime;

import org.springframework.http.MediaType;
import org.springframework.web.bind.annotation.GetMapping;
import org.springframework.web.bind.annotation.PostMapping;
import org.springframework.web.bind.annotation.RequestBody;
import org.springframework.web.bind.annotation.RequestMapping;
import org.springframework.web.bind.annotation.RestController;
import org.springframework.web.servlet.mvc.method.annotation.SseEmitter;

import java.io.IOException;
import java.util.Map;
import java.util.Objects;
import java.util.concurrent.Executors;
import java.util.concurrent.ScheduledExecutorService;
import java.util.concurrent.TimeUnit;

@RestController
@RequestMapping("/api/runtime")
public class ChatRuntimeController {
    private final ChatRuntimeService runtimeService;
    private final ScheduledExecutorService sseExecutor = Executors.newScheduledThreadPool(1);

    public ChatRuntimeController(ChatRuntimeService runtimeService) {
        this.runtimeService = runtimeService;
    }

    @GetMapping("/server/status")
    public Map<String, Object> serverStatus() {
        return runtimeService.serverStatus();
    }

    @PostMapping("/server/start")
    public Map<String, Object> startServer(@RequestBody Map<String, Integer> body) {
        int port = body.getOrDefault("port", 9000);
        return runtimeService.startServer(port);
    }

    @PostMapping("/server/stop")
    public Map<String, Object> stopServer() {
        return runtimeService.stopServer();
    }

    @GetMapping("/client/status")
    public Map<String, Object> clientStatus(String clientId) {
        return runtimeService.clientStatus(clientId);
    }

    @PostMapping("/client/start")
    public Map<String, Object> startClient(@RequestBody Map<String, Object> body) {
        String clientId = String.valueOf(body.getOrDefault("clientId", "default"));
        String ip = String.valueOf(body.getOrDefault("ip", "127.0.0.1"));
        int port = Integer.parseInt(String.valueOf(body.getOrDefault("port", 9000)));
        String role = String.valueOf(body.getOrDefault("role", "user"));
        String username = String.valueOf(body.getOrDefault("username", ""));
        return runtimeService.startClient(clientId, ip, port, role, username);
    }

    @PostMapping("/client/stop")
    public Map<String, Object> stopClient(@RequestBody Map<String, String> body) {
        String clientId = body.getOrDefault("clientId", "default");
        return runtimeService.stopClient(clientId);
    }

    @PostMapping("/client/send")
    public Map<String, Object> sendClient(@RequestBody Map<String, String> body) {
        String clientId = body.getOrDefault("clientId", "default");
        return runtimeService.sendClientMessage(clientId, body.getOrDefault("message", ""));
    }

    @GetMapping(path = "/stream", produces = MediaType.TEXT_EVENT_STREAM_VALUE)
    public SseEmitter stream() {
        SseEmitter emitter = new SseEmitter(0L);
        sseExecutor.scheduleAtFixedRate(() -> {
            try {
                Object payload = Objects.requireNonNull(runtimeService.serverStatus());
                emitter.send(SseEmitter.event().name("server").data(payload));
            } catch (IOException e) {
                emitter.complete();
            }
        }, 0, 1, TimeUnit.SECONDS);
        return emitter;
    }
}
