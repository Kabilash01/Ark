#include "ark/voice_input.hpp"

#include "ark/message.hpp"

#include <algorithm>
#include <cerrno>
#include <cctype>
#include <fcntl.h>
#include <stdexcept>
#include <sys/wait.h>
#include <unistd.h>
#include <spawn.h>

extern char** environ;

namespace ark {
namespace {

ProcessResult run_process(const std::vector<std::string>& args) {
    if (args.empty() || args.front().empty()) throw std::invalid_argument("Voice process executable is empty");
    int pipes[2];
    if (pipe(pipes) != 0) throw std::runtime_error("Could not create process output pipe");
    const auto close_read = [&] { if (pipes[0] >= 0) { close(pipes[0]); pipes[0] = -1; } };
    const auto close_write = [&] { if (pipes[1] >= 0) { close(pipes[1]); pipes[1] = -1; } };
    posix_spawn_file_actions_t actions;
    if (posix_spawn_file_actions_init(&actions) != 0) { close_read(); close_write(); throw std::runtime_error("Could not initialize process actions"); }
    const auto destroy_actions = [&] { posix_spawn_file_actions_destroy(&actions); };
    int setup = posix_spawn_file_actions_adddup2(&actions, pipes[1], STDOUT_FILENO);
    if (setup == 0) setup = posix_spawn_file_actions_addclose(&actions, pipes[0]);
    if (setup == 0) setup = posix_spawn_file_actions_addclose(&actions, pipes[1]);
    if (setup == 0) setup = posix_spawn_file_actions_addopen(&actions, STDERR_FILENO, "/dev/null", O_WRONLY, 0);
    if (setup != 0) { destroy_actions(); close_read(); close_write(); throw std::runtime_error("Could not configure voice process pipes"); }
    std::vector<char*> argv;
    argv.reserve(args.size() + 1);
    for (const auto& arg : args) argv.push_back(const_cast<char*>(arg.c_str()));
    argv.push_back(nullptr);
    pid_t child = -1;
    const int spawn_error = posix_spawnp(&child, argv.front(), &actions, nullptr, argv.data(), environ);
    destroy_actions();
    close_write();
    if (spawn_error != 0) { close_read(); throw std::runtime_error("Could not start voice SDK process: " + std::string(std::strerror(spawn_error))); }
    std::string output;
    char buffer[4096];
    for (;;) {
        const auto count = read(pipes[0], buffer, sizeof(buffer));
        if (count == 0) break;
        if (count < 0) { if (errno == EINTR) continue; close_read(); throw std::runtime_error("Could not read voice SDK output"); }
        if (output.size() + static_cast<std::size_t>(count) > 1024 * 1024) {
            close_read(); kill(child, SIGKILL); waitpid(child, nullptr, 0);
            throw std::runtime_error("Voice SDK output exceeded 1 MiB");
        }
        output.append(buffer, static_cast<std::size_t>(count));
    }
    close_read();
    int status = 0;
    while (waitpid(child, &status, 0) < 0) if (errno != EINTR) throw std::runtime_error("Could not wait for voice SDK process");
    const int code = WIFEXITED(status) ? WEXITSTATUS(status) : 128 + (WIFSIGNALED(status) ? WTERMSIG(status) : 0);
    return {code, std::move(output)};
}

std::string trim(std::string value) {
    const auto not_space = [](unsigned char c) { return !std::isspace(c); };
    value.erase(value.begin(), std::find_if(value.begin(), value.end(), not_space));
    value.erase(std::find_if(value.rbegin(), value.rend(), not_space).base(), value.end());
    return value;
}

} // namespace

VoiceInput::VoiceInput(std::string recorder, std::string whisper_cli, std::filesystem::path model, VoiceProcess process)
    : recorder_(std::move(recorder)), whisper_cli_(std::move(whisper_cli)), model_(std::move(model)),
      process_(process ? std::move(process) : VoiceProcess(run_process)) {}

std::filesystem::path VoiceInput::capture(int seconds) const {
    if (seconds < 1 || seconds > 60) throw std::invalid_argument("Voice capture duration must be 1 to 60 seconds");
    if (recorder_.empty()) throw std::invalid_argument("Set ARK_AUDIO_RECORDER to arecord or pw-record");
    auto path = std::filesystem::temp_directory_path() / ("ark-voice-" + random_uuid() + ".wav");
    std::vector<std::string> args;
    if (std::filesystem::path(recorder_).filename() == "pw-record") {
        args = {recorder_, "--rate", "16000", "--channels", "1", "--format", "s16", "--container", "wav",
                "--sample-count", std::to_string(seconds * 16000), path.string()};
    } else {
        args = {recorder_, "-q", "-t", "wav", "-f", "S16_LE", "-r", "16000", "-c", "1",
                "-d", std::to_string(seconds), path.string()};
    }
    const auto result = process_(args);
    if (result.exit_code != 0 || !std::filesystem::is_regular_file(path) || std::filesystem::file_size(path) < 44) {
        std::error_code ignored; std::filesystem::remove(path, ignored);
        throw std::runtime_error("Microphone capture failed; check the audio device and recorder configuration");
    }
    return path;
}

std::string VoiceInput::transcribe(const std::filesystem::path& audio) const {
    if (whisper_cli_.empty() || model_.empty()) throw std::invalid_argument("Set ARK_WHISPER_CLI and ARK_WHISPER_MODEL for local transcription");
    if (!std::filesystem::is_regular_file(audio) || std::filesystem::file_size(audio) == 0)
        throw std::invalid_argument("Voice recording does not exist or is empty");
    const auto result = process_({whisper_cli_, "-m", model_.string(), "-f", audio.string(), "-nt", "-np"});
    if (result.exit_code != 0) throw std::runtime_error("Local Whisper transcription failed with exit code " + std::to_string(result.exit_code));
    auto transcript = trim(result.output);
    if (transcript.empty()) throw std::runtime_error("No speech was recognized in the recording");
    return transcript;
}

std::string VoiceInput::capture_and_transcribe(int seconds) const {
    const auto audio = capture(seconds);
    try {
        auto result = transcribe(audio);
        std::filesystem::remove(audio);
        return result;
    } catch (...) {
        std::error_code ignored; std::filesystem::remove(audio, ignored);
        throw;
    }
}

} // namespace ark
