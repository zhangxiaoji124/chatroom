package com.chatroom.ui.runtime;

import java.util.LinkedHashMap;
import java.util.Map;
import java.util.regex.Matcher;
import java.util.regex.Pattern;

public final class ChatMediaParser {
    private static final Pattern MEDIA_PATTERN = Pattern.compile(
        "@@MEDIA\\|(?<type>image|file)\\|(?<name>[^|]+)\\|(?<url>[^|]+)\\|(?<size>\\d+)@@"
    );
    private static final Pattern USER_MSG_PATTERN = Pattern.compile("^\\[(?<user>[^\\]]+)]\\s*(?<body>.*)$");

    private ChatMediaParser() {
    }

    public static Map<String, Object> enrich(String rawText) {
        Map<String, Object> meta = new LinkedHashMap<>();
        meta.put("msgType", "text");
        meta.put("text", rawText == null ? "" : rawText);
        meta.put("fileName", "");
        meta.put("mediaUrl", "");
        meta.put("fileSize", 0L);
        meta.put("sender", "");

        if (rawText == null || rawText.isBlank()) {
            return meta;
        }

        String body = rawText;
        Matcher userMatcher = USER_MSG_PATTERN.matcher(rawText.trim());
        if (userMatcher.matches()) {
            meta.put("sender", userMatcher.group("user"));
            body = userMatcher.group("body").trim();
            meta.put("text", body);
        }

        if (body.startsWith("[system]") || body.contains("[system]")) {
            meta.put("msgType", "system");
            return meta;
        }
        if (body.startsWith("[private")) {
            meta.put("msgType", "system");
            return meta;
        }

        Matcher mediaMatcher = MEDIA_PATTERN.matcher(body);
        if (mediaMatcher.find()) {
            meta.put("msgType", mediaMatcher.group("type"));
            meta.put("fileName", mediaMatcher.group("name"));
            meta.put("mediaUrl", mediaMatcher.group("url"));
            meta.put("fileSize", Long.parseLong(mediaMatcher.group("size")));
            meta.put("text", body);
        }
        return meta;
    }

    public static String buildMediaLine(String type, String fileName, String mediaUrl, long fileSize) {
        return "@@MEDIA|" + type + "|" + sanitize(fileName) + "|" + sanitize(mediaUrl) + "|" + fileSize + "@@";
    }

    private static String sanitize(String value) {
        return value.replace("|", "_").replace("\n", "").replace("\r", "");
    }
}
