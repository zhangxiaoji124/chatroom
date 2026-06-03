package com.chatroom.ui.monitor;

import jakarta.annotation.PostConstruct;
import jakarta.annotation.PreDestroy;
import org.springframework.beans.factory.annotation.Value;
import org.springframework.stereotype.Service;

import java.nio.file.Files;
import java.nio.file.Path;
import java.nio.file.Paths;
import java.sql.Connection;
import java.sql.DriverManager;
import java.sql.PreparedStatement;
import java.sql.SQLException;
import java.sql.Statement;
import java.util.ArrayList;
import java.util.List;
import java.util.concurrent.atomic.AtomicBoolean;

@Service
public class BenchmarkDatabaseService {
    private final AtomicBoolean ready = new AtomicBoolean(false);
    private final List<ShardConnection> shards = new ArrayList<>();

    @Value("${chat.benchmark.database:h2}")
    private String databaseMode;

    @Value("${chat.benchmark.sqlitePath:../cmake-build-release/Release/data/benchmark_million.db}")
    private String sqlitePath;

    @Value("${chat.benchmark.writerShards:8}")
    private int writerShards;

    @PostConstruct
    public void init() {
        try {
            prepareSchema();
            ready.set(true);
        } catch (SQLException e) {
            throw new IllegalStateException("Benchmark database init failed: " + e.getMessage(), e);
        }
    }

    @PreDestroy
    public void shutdown() {
        for (ShardConnection shard : shards) {
            shard.closeQuietly();
        }
        shards.clear();
        ready.set(false);
    }

    public boolean isReady() {
        return ready.get();
    }

    public int shardCount() {
        return shards.size();
    }

    public String mode() {
        return databaseMode;
    }

    public void resetForRun() throws SQLException {
        boolean h2 = "h2".equalsIgnoreCase(databaseMode);
        for (ShardConnection shard : shards) {
            try (Statement stmt = shard.connection().createStatement()) {
                if (h2) {
                    stmt.execute("TRUNCATE TABLE bench_messages");
                } else {
                    stmt.execute("DELETE FROM bench_messages");
                }
            }
        }
    }

    public long countRows() throws SQLException {
        long total = 0;
        for (ShardConnection shard : shards) {
            try (Statement stmt = shard.connection().createStatement();
                 var rs = stmt.executeQuery("SELECT COUNT(*) FROM bench_messages")) {
                if (rs.next()) {
                    total += rs.getLong(1);
                }
            }
        }
        return total;
    }

    public ShardWriter openWriter(int shardId, int batchSize) throws SQLException {
        if (!ready.get() || shardId < 0 || shardId >= shards.size()) {
            throw new SQLException("Shard not available: " + shardId);
        }
        ShardConnection shard = shards.get(shardId);
        Connection conn = shard.connection();
        conn.setAutoCommit(false);
        PreparedStatement ps = conn.prepareStatement(
            "INSERT INTO bench_messages(shard_id, payload, latency_us) VALUES (?, ?, ?)"
        );
        return new ShardWriter(shardId, conn, ps, batchSize);
    }

    private void prepareSchema() throws SQLException {
        shutdown();
        shards.clear();

        if ("sqlite".equalsIgnoreCase(databaseMode)) {
            initSqliteShards();
        } else {
            initH2Shards();
        }
    }

    private void initH2Shards() throws SQLException {
        for (int i = 0; i < writerShards; i++) {
            String url = "jdbc:h2:mem:chatbench" + i + ";DB_CLOSE_DELAY=-1;MODE=MySQL";
            Connection conn = DriverManager.getConnection(url, "sa", "");
            applyPragmas(conn, true);
            createTable(conn);
            shards.add(new ShardConnection(i, conn));
        }
    }

    private void initSqliteShards() throws SQLException {
        Path base = Paths.get(sqlitePath).toAbsolutePath().getParent();
        if (base != null) {
            try {
                Files.createDirectories(base);
            } catch (java.io.IOException e) {
                throw new SQLException("Failed to create sqlite directory", e);
            }
        }
        for (int i = 0; i < writerShards; i++) {
            Path dbFile = Paths.get(sqlitePath).toAbsolutePath().getParent()
                .resolve("benchmark_shard_" + i + ".db");
            String url = "jdbc:sqlite:" + dbFile;
            Connection conn = DriverManager.getConnection(url);
            applyPragmas(conn, false);
            createTable(conn);
            shards.add(new ShardConnection(i, conn));
        }
    }

    private void applyPragmas(Connection conn, boolean h2) throws SQLException {
        try (Statement stmt = conn.createStatement()) {
            if (h2) {
                stmt.execute("SET LOCK_MODE 0");
            } else {
                stmt.execute("PRAGMA journal_mode=WAL");
                stmt.execute("PRAGMA synchronous=NORMAL");
                stmt.execute("PRAGMA busy_timeout=5000");
            }
        }
    }

    private void createTable(Connection conn) throws SQLException {
        try (Statement stmt = conn.createStatement()) {
            stmt.execute("""
                CREATE TABLE IF NOT EXISTS bench_messages (
                  id BIGINT AUTO_INCREMENT PRIMARY KEY,
                  shard_id INT NOT NULL,
                  payload VARCHAR(96) NOT NULL,
                  latency_us BIGINT NOT NULL,
                  created_at TIMESTAMP DEFAULT CURRENT_TIMESTAMP
                )
                """);
        }
    }

    public static final class ShardWriter implements AutoCloseable {
        private final int shardId;
        private final Connection connection;
        private final PreparedStatement statement;
        private final int batchSize;
        private int pending;

        private ShardWriter(int shardId, Connection connection, PreparedStatement statement, int batchSize) {
            this.shardId = shardId;
            this.connection = connection;
            this.statement = statement;
            this.batchSize = batchSize;
        }

        public int shardId() {
            return shardId;
        }

        public void writeBatch(String payload, long latencyMicros) throws SQLException {
            writeBatch(payload, latencyMicros, batchSize);
        }

        public void writeBatch(String payload, long latencyMicros, int rows) throws SQLException {
            for (int i = 0; i < rows; i++) {
                statement.setInt(1, shardId);
                statement.setString(2, payload);
                statement.setLong(3, latencyMicros);
                statement.addBatch();
            }
            statement.executeBatch();
            connection.commit();
            pending += rows;
        }

        public int pendingCount() {
            return pending;
        }

        @Override
        public void close() throws SQLException {
            statement.close();
            connection.setAutoCommit(true);
        }
    }

    private static final class ShardConnection {
        private final int shardId;
        private final Connection connection;

        private ShardConnection(int shardId, Connection connection) {
            this.shardId = shardId;
            this.connection = connection;
        }

        private Connection connection() {
            return connection;
        }

        private void closeQuietly() {
            try {
                connection.close();
            } catch (SQLException ignored) {
                // ignore
            }
        }
    }
}
