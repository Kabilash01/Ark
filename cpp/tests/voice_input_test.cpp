#include "ark/voice_input.hpp"

#include "ark/message.hpp"

#include <fstream>
#include <iostream>
#include <stdexcept>

namespace {
void check(bool condition, const char* message) { if (!condition) throw std::runtime_error(message); }

void transcribes_with_local_whisper_sdk() {
    const auto audio = std::filesystem::temp_directory_path() / ("ark-voice-test-" + ark::random_uuid() + ".wav");
    { std::ofstream file(audio, std::ios::binary); file << "WAVE fixture"; }
    bool called = false;
    ark::VoiceInput voice("arecord", "whisper-cli", "/models/ggml-medium.bin",
        [&](const std::vector<std::string>& args) {
            called = true;
            check(args.size() == 7 && args[0] == "whisper-cli" && args[1] == "-m"
                && args[2] == "/models/ggml-medium.bin" && args[3] == "-f" && args[4] == audio.string()
                && args[5] == "-nt" && args[6] == "-np", "voice input used unexpected Whisper CLI arguments");
            return ark::ProcessResult{0, "  research quantum computing\n"};
        });
    check(voice.transcribe(audio) == "research quantum computing" && called,
          "voice input did not return the local transcript");
    std::filesystem::remove(audio);
}

void captures_then_transcribes_and_cleans_up() {
    std::filesystem::path capture_path;
    int calls = 0;
    ark::VoiceInput voice("arecord", "whisper-cli", "model.bin",
        [&](const std::vector<std::string>& args) {
            ++calls;
            if (args.front() == "arecord") {
                check(args.size() == 13 && args[1] == "-q" && args[2] == "-t" && args[3] == "wav"
                    && args[4] == "-f" && args[5] == "S16_LE" && args[6] == "-r" && args[7] == "16000"
                    && args[8] == "-c" && args[9] == "1" && args[10] == "-d" && args[11] == "3",
                    "microphone capture used the wrong format or duration");
                capture_path = args.back();
                std::ofstream file(capture_path, std::ios::binary); file << std::string(64, 'x');
                return ark::ProcessResult{0, {}};
            }
            check(args.front() == "whisper-cli" && std::filesystem::exists(capture_path),
                  "transcription did not use the captured audio");
            return ark::ProcessResult{0, "research quantum computing"};
        });
    check(voice.capture_and_transcribe(3) == "research quantum computing" && calls == 2,
          "microphone recording was not transcribed");
    check(!std::filesystem::exists(capture_path), "temporary microphone audio was not removed");
}

void rejects_invalid_input() {
    ark::VoiceInput voice("arecord", "whisper-cli", "model.bin", [](const auto&) { return ark::ProcessResult{}; });
    bool bad_duration = false;
    try { (void)voice.capture(0); } catch (const std::invalid_argument&) { bad_duration = true; }
    check(bad_duration, "invalid recording duration was accepted");
    bool empty_audio = false;
    try { (void)voice.transcribe("/missing/voice-fixture.wav"); } catch (const std::invalid_argument&) { empty_audio = true; }
    check(empty_audio, "missing recording was accepted");
}

void supports_pipewire_recorder() {
    std::filesystem::path capture_path;
    ark::VoiceInput voice("/usr/bin/pw-record", "whisper-cli", "model.bin",
        [&](const std::vector<std::string>& args) {
            check(args.size() == 12 && args[0] == "/usr/bin/pw-record" && args[1] == "--rate"
                && args[2] == "16000" && args[3] == "--channels" && args[4] == "1"
                && args[5] == "--format" && args[6] == "s16" && args[7] == "--container"
                && args[8] == "wav" && args[9] == "--sample-count" && args[10] == "80000",
                "PipeWire recorder did not request the expected WAV format");
            capture_path = args.back();
            std::ofstream file(capture_path, std::ios::binary); file << std::string(64, 'x');
            return ark::ProcessResult{0, {}};
        });
    const auto path = voice.capture(5);
    check(path == capture_path, "PipeWire capture path changed");
    std::filesystem::remove(path);
}
}

int main() {
    try { transcribes_with_local_whisper_sdk(); captures_then_transcribes_and_cleans_up(); rejects_invalid_input(); supports_pipewire_recorder(); }
    catch (const std::exception& e) { std::cerr << e.what() << '\n'; return 1; }
}
