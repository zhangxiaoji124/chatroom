#pragma once

#include "chat/types.hpp"

#include <filesystem>
#include <string>

namespace chat {

struct AiCompletionResult {
    bool ok = false;
    std::string reply;
    std::string diagnostic;
};

// Reads and validates the DeepSeek key without ever logging its value.
std::string load_deepseek_api_key(const std::filesystem::path& path);

// Calls DeepSeek through the system curl executable. The supplied messages
// must use the OpenAI-compatible {role, content} schema.
AiCompletionResult request_deepseek_completion(const std::string& api_key,
                                               const json& messages);

} // namespace chat
