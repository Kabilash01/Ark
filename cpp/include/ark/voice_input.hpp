#pragma once

#include <filesystem>
#include <functional>
#include <string>
#include <vector>

namespace ark {

struct ProcessResult { int exit_code = -1; std::string output; };
using VoiceProcess = std::function<ProcessResult(const std::vector<std::string>&)>;

class VoiceInput {
public:
    VoiceInput(std::string recorder, std::string whisper_cli, std::filesystem::path model,
               VoiceProcess process = {});
    std::filesystem::path capture(int seconds) const;
    std::string transcribe(const std::filesystem::path& audio) const;
    std::string capture_and_transcribe(int seconds) const;
private:
    std::string recorder_;
    std::string whisper_cli_;
    std::filesystem::path model_;
    VoiceProcess process_;
};

} // namespace ark
