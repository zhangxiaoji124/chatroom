package com.chatroom.ui.monitor;

import org.springframework.http.MediaType;
import org.springframework.http.ResponseEntity;
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
@RequestMapping("/api")
public class MonitorApiController {
    private final LoadTestMonitorService monitorService;
    private final ScheduledExecutorService sseExecutor = Executors.newScheduledThreadPool(1);

    public MonitorApiController(LoadTestMonitorService monitorService) {
        this.monitorService = monitorService;
    }

    @GetMapping("/metrics")
    public Map<String, Object> metrics() {
        return monitorService.getSnapshot();
    }

    @PostMapping("/loadtest/start")
    public Map<String, Object> start(@RequestBody Map<String, Integer> body) {
        int users = body.getOrDefault("virtualUsers", 50);
        int rate = body.getOrDefault("msgRatePerSecond", 120);
        int duration = body.getOrDefault("durationSeconds", 60);
        return monitorService.startLoadTest(users, rate, duration);
    }

    @PostMapping("/loadtest/stop")
    public Map<String, Object> stop() {
        return monitorService.stopLoadTest();
    }

    @PostMapping("/chat/event")
    public ResponseEntity<Map<String, Object>> event(@RequestBody Map<String, String> body) {
        String direction = body.getOrDefault("direction", "SEND");
        String message = body.getOrDefault("message", "empty");
        monitorService.recordExternalEvent(direction, message);
        return ResponseEntity.ok(Map.of("ok", true));
    }

    @GetMapping(path = "/stream", produces = MediaType.TEXT_EVENT_STREAM_VALUE)
    public SseEmitter stream() {
        SseEmitter emitter = new SseEmitter(0L);
        sseExecutor.scheduleAtFixedRate(() -> {
            try {
                Object payload = Objects.requireNonNull(monitorService.getSnapshot());
                emitter.send(SseEmitter.event().name("metrics").data(payload));
            } catch (IOException e) {
                emitter.complete();
            }
        }, 0, 1, TimeUnit.SECONDS);
        return emitter;
    }
}
