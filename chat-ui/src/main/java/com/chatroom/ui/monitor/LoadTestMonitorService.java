package com.chatroom.ui.monitor;

import org.springframework.stereotype.Service;

import java.time.Instant;
import java.util.ArrayDeque;
import java.util.ArrayList;
import java.util.Deque;
import java.util.LinkedHashMap;
import java.util.Map;
import java.util.Random;
import java.util.concurrent.Executors;
import java.util.concurrent.ScheduledExecutorService;
import java.util.concurrent.ScheduledFuture;
import java.util.concurrent.TimeUnit;
import java.util.concurrent.atomic.AtomicBoolean;
import java.util.concurrent.atomic.AtomicLong;

@Service
public class LoadTestMonitorService {
    private static final int MAX_EVENTS = 200;
    private static final int MAX_TIMELINE = 120;

    private final ScheduledExecutorService executor = Executors.newScheduledThreadPool(2);
    private final AtomicBoolean loadTesting = new AtomicBoolean(false);
    private final AtomicLong sentCount = new AtomicLong();
    private final AtomicLong receivedCount = new AtomicLong();
    private final AtomicLong failedCount = new AtomicLong();
    private final AtomicLong onlineUsers = new AtomicLong();
    private final AtomicLong activeConnections = new AtomicLong();
    private final Random random = new Random();

    private final Deque<Map<String, Object>> recentEvents = new ArrayDeque<>();
    private final Deque<Map<String, Object>> timeline = new ArrayDeque<>();
    private final Object lock = new Object();

    private ScheduledFuture<?> testTask;
    private long startEpochMs;
    private int targetDurationSeconds;
    private int targetRatePerSecond;

    public LoadTestMonitorService() {
        executor.scheduleAtFixedRate(this::appendTimelinePoint, 0, 1, TimeUnit.SECONDS);
    }

    public Map<String, Object> startLoadTest(int virtualUsers, int msgRatePerSecond, int durationSeconds) {
        stopLoadTest();
        loadTesting.set(true);
        targetDurationSeconds = durationSeconds;
        targetRatePerSecond = msgRatePerSecond;
        startEpochMs = System.currentTimeMillis();
        onlineUsers.set(virtualUsers);
        activeConnections.set(Math.max(1, virtualUsers / 2));

        addEvent("SYSTEM", "START", "压测开始，虚拟用户=" + virtualUsers + "，速率=" + msgRatePerSecond + "/s");
        testTask = executor.scheduleAtFixedRate(() -> tickLoadTest(virtualUsers), 0, 200, TimeUnit.MILLISECONDS);
        executor.schedule(this::stopLoadTest, durationSeconds, TimeUnit.SECONDS);
        return getSnapshot();
    }

    public Map<String, Object> stopLoadTest() {
        loadTesting.set(false);
        if (testTask != null) {
            testTask.cancel(false);
            testTask = null;
        }
        addEvent("SYSTEM", "STOP", "压测停止");
        return getSnapshot();
    }

    private void tickLoadTest(int virtualUsers) {
        if (!loadTesting.get()) {
            return;
        }
        int perTick = Math.max(1, targetRatePerSecond / 5);
        for (int i = 0; i < perTick; i++) {
            long currentSent = sentCount.incrementAndGet();
            boolean failed = random.nextInt(100) < 3;
            if (failed) {
                failedCount.incrementAndGet();
                addEvent("user" + (1 + random.nextInt(Math.max(1, virtualUsers))), "SEND_FAIL", "消息发送失败，模拟网络抖动");
            } else {
                receivedCount.incrementAndGet();
                addEvent("user" + (1 + random.nextInt(Math.max(1, virtualUsers))), "SEND", "消息#" + currentSent + " 已发送并接收");
            }
        }
        activeConnections.set(Math.max(1, virtualUsers / 2 + random.nextInt(Math.max(2, virtualUsers / 2 + 1))));
    }

    private void appendTimelinePoint() {
        Map<String, Object> point = new LinkedHashMap<>();
        point.put("timestamp", Instant.now().toString());
        point.put("sent", sentCount.get());
        point.put("received", receivedCount.get());
        point.put("failed", failedCount.get());
        point.put("onlineUsers", onlineUsers.get());
        point.put("activeConnections", activeConnections.get());
        synchronized (lock) {
            timeline.addLast(point);
            while (timeline.size() > MAX_TIMELINE) {
                timeline.removeFirst();
            }
        }
    }

    public Map<String, Object> getSnapshot() {
        Map<String, Object> snapshot = new LinkedHashMap<>();
        long now = System.currentTimeMillis();
        long elapsedMs = startEpochMs == 0 ? 0 : Math.max(0, now - startEpochMs);
        double elapsedSec = Math.max(1d, elapsedMs / 1000d);
        long sent = sentCount.get();
        long received = receivedCount.get();
        long failed = failedCount.get();
        snapshot.put("running", loadTesting.get());
        snapshot.put("sent", sent);
        snapshot.put("received", received);
        snapshot.put("failed", failed);
        snapshot.put("throughput", Math.round((sent / elapsedSec) * 100.0) / 100.0);
        snapshot.put("successRate", sent == 0 ? 100.0 : Math.round(((double) received / (double) sent) * 10000.0) / 100.0);
        snapshot.put("elapsedSeconds", elapsedMs / 1000);
        snapshot.put("targetDurationSeconds", targetDurationSeconds);
        snapshot.put("targetRatePerSecond", targetRatePerSecond);
        snapshot.put("onlineUsers", onlineUsers.get());
        snapshot.put("activeConnections", activeConnections.get());
        synchronized (lock) {
            snapshot.put("recentEvents", new ArrayList<>(recentEvents));
            snapshot.put("timeline", new ArrayList<>(timeline));
        }
        return snapshot;
    }

    private void addEvent(String actor, String type, String detail) {
        Map<String, Object> event = new LinkedHashMap<>();
        event.put("time", Instant.now().toString());
        event.put("actor", actor);
        event.put("type", type);
        event.put("detail", detail);
        synchronized (lock) {
            recentEvents.addFirst(event);
            while (recentEvents.size() > MAX_EVENTS) {
                recentEvents.removeLast();
            }
        }
    }

    public void recordExternalEvent(String direction, String message) {
        if ("SEND".equalsIgnoreCase(direction)) {
            sentCount.incrementAndGet();
        } else if ("RECEIVE".equalsIgnoreCase(direction)) {
            receivedCount.incrementAndGet();
        } else if ("FAIL".equalsIgnoreCase(direction)) {
            failedCount.incrementAndGet();
        }
        addEvent("external", direction.toUpperCase(), message);
    }
}
