package com.chatroom.ui.monitor;

import org.springframework.beans.factory.annotation.Value;
import org.springframework.stereotype.Service;

import java.time.Instant;
import java.util.ArrayDeque;
import java.util.ArrayList;
import java.util.Deque;
import java.util.LinkedHashMap;
import java.util.List;
import java.util.Map;
import java.util.concurrent.ExecutorService;
import java.util.concurrent.Executors;
import java.util.concurrent.ScheduledExecutorService;
import java.util.concurrent.ScheduledFuture;
import java.util.concurrent.TimeUnit;
import java.util.concurrent.atomic.AtomicBoolean;
import java.util.concurrent.atomic.AtomicLong;
import java.util.concurrent.atomic.LongAdder;

@Service
public class LoadTestMonitorService {
    private static final int MAX_TIMELINE = 180;
    private static final int MAX_LATENCY_POINTS = 120;
    private static final int MAX_EVENTS = 120;

    private final BenchmarkDatabaseService databaseService;
    private final ScheduledExecutorService scheduler = Executors.newScheduledThreadPool(2);
    private final ExecutorService workerPool = Executors.newCachedThreadPool();

    private final AtomicBoolean loadTesting = new AtomicBoolean(false);
    private final AtomicLong sentCount = new AtomicLong();
    private final AtomicLong dbWriteCount = new AtomicLong();
    private final AtomicLong failedCount = new AtomicLong();
    private final LongAdder windowLatencyMicrosSum = new LongAdder();
    private final LongAdder windowLatencySampleCount = new LongAdder();

    private final Deque<Map<String, Object>> recentEvents = new ArrayDeque<>();
    private final Deque<Map<String, Object>> timeline = new ArrayDeque<>();
    private final Deque<Map<String, Object>> latencySeries = new ArrayDeque<>();
    private final Object lock = new Object();

    private volatile long startEpochMs;
    private volatile long targetTotalMessages = 1_000_000L;
    private volatile int writerShards = 8;
    private volatile int batchSize = 2000;
    private volatile long lastSecondSent = 0;
    private volatile long lastSecondDbWrites = 0;
    private volatile double lastSecondThroughput = 0;
    private volatile double lastSecondAvgLatencyMs = 0;

    @Value("${chat.benchmark.defaultTotalMessages:1000000}")
    private int defaultTotalMessages;

    @Value("${chat.benchmark.defaultWriterShards:8}")
    private int defaultWriterShards;

    @Value("${chat.benchmark.defaultBatchSize:2000}")
    private int defaultBatchSize;

    public LoadTestMonitorService(BenchmarkDatabaseService databaseService) {
        this.databaseService = databaseService;
        scheduler.scheduleAtFixedRate(this::captureTimelinePoint, 1, 1, TimeUnit.SECONDS);
    }

    public Map<String, Object> startMillionScaleTest(int totalMessages, int shards, int batchSizePerWrite) {
        stopLoadTest();
        if (!databaseService.isReady()) {
            throw new IllegalStateException("Benchmark database is not ready");
        }

        targetTotalMessages = Math.max(10_000L, totalMessages);
        writerShards = Math.max(1, Math.min(shards, databaseService.shardCount()));
        batchSize = Math.max(100, batchSizePerWrite);

        resetMetrics();
        loadTesting.set(true);
        startEpochMs = System.currentTimeMillis();

        addEvent("SYSTEM", "START", "百万级压测启动: 目标=" + targetTotalMessages
            + ", 分片=" + writerShards + ", 批量=" + batchSize
            + ", 引擎=" + databaseService.mode().toUpperCase());

        try {
            databaseService.resetForRun();
        } catch (Exception e) {
            loadTesting.set(false);
            throw new IllegalStateException("Failed to reset benchmark tables: " + e.getMessage(), e);
        }

        long perShardTarget = (targetTotalMessages + writerShards - 1) / writerShards;
        for (int shard = 0; shard < writerShards; shard++) {
            final int shardId = shard;
            workerPool.submit(() -> runShardWriter(shardId, perShardTarget));
        }

        return getSnapshot();
    }

    public Map<String, Object> startLoadTest(int virtualUsers, int msgRatePerSecond, int durationSeconds) {
        long estimated = (long) msgRatePerSecond * durationSeconds;
        if (estimated < 100_000) {
            estimated = defaultTotalMessages;
        }
        return startMillionScaleTest((int) Math.min(estimated, Integer.MAX_VALUE), defaultWriterShards, defaultBatchSize);
    }

    public Map<String, Object> stopLoadTest() {
        loadTesting.set(false);
        addEvent("SYSTEM", "STOP", "压测停止，已写入 " + dbWriteCount.get() + " 条");
        return getSnapshot();
    }

    private void runShardWriter(int shardId, long shardTarget) {
        String payload = "shard-" + shardId + "-payload-xxxxxxxx";
        long written = 0;
        try (BenchmarkDatabaseService.ShardWriter writer = databaseService.openWriter(shardId, batchSize)) {
            while (loadTesting.get() && written < shardTarget) {
                int currentBatch = (int) Math.min(batchSize, shardTarget - written);
                if (currentBatch <= 0) {
                    break;
                }
                long begin = System.nanoTime();
                try {
                    writer.writeBatch(payload, 0L, currentBatch);
                    long elapsedMicros = (System.nanoTime() - begin) / 1000L;
                    sentCount.addAndGet(currentBatch);
                    dbWriteCount.addAndGet(currentBatch);
                    windowLatencyMicrosSum.add(elapsedMicros);
                    windowLatencySampleCount.increment();
                    written += currentBatch;
                } catch (Exception ex) {
                    failedCount.addAndGet(currentBatch);
                    addEvent("shard-" + shardId, "FAIL", ex.getMessage());
                    break;
                }
            }
        } catch (Exception ex) {
            addEvent("shard-" + shardId, "FAIL", "分片初始化失败: " + ex.getMessage());
            return;
        }
        if (written >= shardTarget) {
            addEvent("shard-" + shardId, "DONE", "分片完成写入 " + written + " 条");
        }
    }

    private void captureTimelinePoint() {
        long sent = sentCount.get();
        long dbWrites = dbWriteCount.get();
        long elapsedMs = startEpochMs == 0 ? 0 : Math.max(0, System.currentTimeMillis() - startEpochMs);
        double elapsedSec = Math.max(1.0, elapsedMs / 1000.0);

        long deltaSent = sent - lastSecondSent;
        long deltaDb = dbWrites - lastSecondDbWrites;
        lastSecondSent = sent;
        lastSecondDbWrites = dbWrites;

        lastSecondThroughput = deltaSent;
        long windowSamples = windowLatencySampleCount.sum();
        lastSecondAvgLatencyMs = windowSamples == 0
            ? 0
            : (windowLatencyMicrosSum.sum() / (double) windowSamples) / 1000.0;
        windowLatencyMicrosSum.reset();
        windowLatencySampleCount.reset();

        Map<String, Object> point = new LinkedHashMap<>();
        point.put("timestamp", Instant.now().toString());
        point.put("elapsedSeconds", elapsedMs / 1000);
        point.put("sent", sent);
        point.put("dbWrites", dbWrites);
        point.put("throughput", lastSecondThroughput);
        point.put("dbThroughput", deltaDb);
        point.put("avgLatencyMs", lastSecondAvgLatencyMs);
        point.put("failed", failedCount.get());

        Map<String, Object> latencyPoint = new LinkedHashMap<>();
        latencyPoint.put("messageIndex", sent);
        latencyPoint.put("latencyMs", lastSecondAvgLatencyMs);
        latencyPoint.put("throughput", lastSecondThroughput);

        synchronized (lock) {
            timeline.addLast(point);
            while (timeline.size() > MAX_TIMELINE) {
                timeline.removeFirst();
            }
            if (loadTesting.get() || sent > 0) {
                latencySeries.addLast(latencyPoint);
                while (latencySeries.size() > MAX_LATENCY_POINTS) {
                    latencySeries.removeFirst();
                }
            }
        }

        if (loadTesting.get() && sent >= targetTotalMessages) {
            loadTesting.set(false);
            addEvent("SYSTEM", "COMPLETE", "达到目标写入量: " + sent);
        }
    }

    public Map<String, Object> getSnapshot() {
        Map<String, Object> snapshot = new LinkedHashMap<>();
        long now = System.currentTimeMillis();
        long elapsedMs = startEpochMs == 0 ? 0 : Math.max(0, now - startEpochMs);
        double elapsedSec = Math.max(1.0, elapsedMs / 1000.0);

        long sent = sentCount.get();
        long dbWrites = dbWriteCount.get();
        long failed = failedCount.get();
        double progress = targetTotalMessages == 0 ? 0
            : Math.min(100.0, (sent * 100.0) / targetTotalMessages);

        snapshot.put("running", loadTesting.get());
        snapshot.put("sent", sent);
        snapshot.put("received", dbWrites);
        snapshot.put("dbWrites", dbWrites);
        snapshot.put("failed", failed);
        snapshot.put("targetTotalMessages", targetTotalMessages);
        snapshot.put("progressPercent", Math.round(progress * 100.0) / 100.0);
        snapshot.put("throughput", Math.round(lastSecondThroughput * 100.0) / 100.0);
        snapshot.put("avgThroughput", Math.round((sent / elapsedSec) * 100.0) / 100.0);
        snapshot.put("dbThroughput", Math.round(lastSecondThroughput * 100.0) / 100.0);
        snapshot.put("avgLatencyMs", Math.round(lastSecondAvgLatencyMs * 1000.0) / 1000.0);
        snapshot.put("successRate", sent == 0 ? 100.0
            : Math.round(((double) (sent - failed) / sent) * 10000.0) / 100.0);
        snapshot.put("elapsedSeconds", elapsedMs / 1000);
        snapshot.put("writerShards", writerShards);
        snapshot.put("batchSize", batchSize);
        snapshot.put("databaseMode", databaseService.mode());
        snapshot.put("distributedShards", writerShards);
        snapshot.put("onlineUsers", writerShards);
        snapshot.put("activeConnections", writerShards);

        long rowCount = 0;
        try {
            rowCount = databaseService.countRows();
        } catch (Exception ignored) {
            // ignore during heavy writes
        }
        snapshot.put("dbRowCount", rowCount);

        synchronized (lock) {
            snapshot.put("recentEvents", new ArrayList<>(recentEvents));
            snapshot.put("timeline", new ArrayList<>(timeline));
            snapshot.put("latencySeries", new ArrayList<>(latencySeries));
        }
        return snapshot;
    }

    public void recordExternalEvent(String direction, String message) {
        if ("SEND".equalsIgnoreCase(direction)) {
            sentCount.incrementAndGet();
        } else if ("RECEIVE".equalsIgnoreCase(direction)) {
            dbWriteCount.incrementAndGet();
        } else if ("FAIL".equalsIgnoreCase(direction)) {
            failedCount.incrementAndGet();
        }
        addEvent("external", direction.toUpperCase(), message);
    }

    private void resetMetrics() {
        sentCount.set(0);
        dbWriteCount.set(0);
        failedCount.set(0);
        windowLatencyMicrosSum.reset();
        windowLatencySampleCount.reset();
        lastSecondSent = 0;
        lastSecondDbWrites = 0;
        lastSecondThroughput = 0;
        lastSecondAvgLatencyMs = 0;
        synchronized (lock) {
            timeline.clear();
            latencySeries.clear();
            recentEvents.clear();
        }
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
}
