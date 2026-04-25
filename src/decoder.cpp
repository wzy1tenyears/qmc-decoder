/*
 * Author: mayusheng - mayusheng@huawei.com
 * Last modified: 2020-06-29 10:56
 * Filename: decoder.cpp
 *
 * Description: qmc file auto decode
 *
 *
 */
#include <algorithm>
#include <array>
#include <cctype>
#include <cstdio>
#include <iostream>
#include <memory>
#include <new>
#include <string>
#include <utility>
#include <vector>

#ifdef _WIN32
#include <process.h>
#include <windows.h>
#else
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
#endif

#include "seed.hpp"

#if defined(_MSVC_LANG)
#define DECODER_CPLUSPLUS _MSVC_LANG
#else
#define DECODER_CPLUSPLUS __cplusplus
#endif

#if defined(DECODER_CPLUSPLUS) && DECODER_CPLUSPLUS >= 201703L && \
    defined(__has_include)
#if __has_include(<filesystem>)
#define GHC_USE_STD_FS
#include <filesystem>
namespace fs = std::filesystem;
#endif
#endif
#ifndef GHC_USE_STD_FS
#include <ghc/filesystem.hpp>
namespace fs = ghc::filesystem;
#endif

namespace {
void close_file(std::FILE* fp) {
  if (fp != nullptr) {
    std::fclose(fp);
  }
}

using smartFilePtr = std::unique_ptr<std::FILE, decltype(&close_file)>;

enum class openMode { read, write };
enum class audioFormat { mp3, flac, ogg, unsupported };

std::string path_to_display_string(const fs::path& path) {
#ifdef _WIN32
  return path.u8string();
#else
  return path.string();
#endif
}

smartFilePtr openFile(const fs::path& path, openMode aOpenMode) {
#ifndef _WIN32
  std::FILE* fp =
      std::fopen(path.string().c_str(),
                 aOpenMode == openMode::read ? "rb" : "wb");
#else
  std::FILE* fp = nullptr;
  const auto widePath = path.wstring();
  _wfopen_s(&fp, widePath.c_str(),
            aOpenMode == openMode::read ? L"rb" : L"wb");
#endif
  return smartFilePtr(fp, &close_file);
}

std::string trim(std::string value) {
  const auto begin = value.find_first_not_of(" \t\r\n");
  if (begin == std::string::npos) {
    return "";
  }

  const auto end = value.find_last_not_of(" \t\r\n");
  return value.substr(begin, end - begin + 1);
}

std::string normalize_user_input(std::string value) {
  value = trim(std::move(value));
  if (value.size() >= 2 && value.front() == '"' && value.back() == '"') {
    value = value.substr(1, value.size() - 2);
  }
  return value;
}

std::string to_lower_ascii(std::string value) {
  std::transform(value.begin(), value.end(), value.begin(),
                 [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
  return value;
}

audioFormat detect_format(const fs::path& path) {
  const auto ext = to_lower_ascii(path.extension().string());

  if (ext == ".qmc3" || ext == ".qmc0") {
    return audioFormat::mp3;
  }
  if (ext == ".qmcflac") {
    return audioFormat::flac;
  }
  if (ext == ".qmcogg") {
    return audioFormat::ogg;
  }

  return audioFormat::unsupported;
}

bool is_qmc_file(const fs::path& path) {
  return detect_format(path) != audioFormat::unsupported;
}

const char* decoded_extension(audioFormat format) {
  switch (format) {
    case audioFormat::mp3:
      return ".mp3";
    case audioFormat::flac:
      return ".flac";
    case audioFormat::ogg:
      return ".ogg";
    default:
      return "";
  }
}

fs::path make_decoded_output_path(const fs::path& input_path,
                                  const fs::path& input_root,
                                  const fs::path& output_root) {
  fs::path output_path = output_root / fs::relative(input_path, input_root);
  output_path.replace_extension(decoded_extension(detect_format(input_path)));
  return output_path;
}

fs::path make_single_file_output_path(const fs::path& input_path) {
  fs::path output_path = input_path;
  output_path.replace_extension(decoded_extension(detect_format(input_path)));
  return output_path;
}

fs::path make_mp3_output_path(const fs::path& input_path, const fs::path& input_root,
                              const fs::path& mp3_output_root) {
  fs::path output_path = mp3_output_root / fs::relative(input_path, input_root);
  output_path.replace_extension(".mp3");
  return output_path;
}

fs::path make_mp3_temp_output_path(const fs::path& output_mp3) {
  fs::path temp_path = output_mp3.parent_path() / output_mp3.stem();
  temp_path += ".transcoding.mp3";
  return temp_path;
}

bool decode_qmc_file(const fs::path& input_path, const fs::path& decoded_path) {
  auto infile = openFile(input_path, openMode::read);
  if (infile == nullptr) {
    std::cerr << "failed to read file: " << path_to_display_string(input_path)
              << std::endl;
    return false;
  }

  auto outfile = openFile(decoded_path, openMode::write);
  if (outfile == nullptr) {
    std::cerr << "failed to write file: "
              << path_to_display_string(decoded_path) << std::endl;
    return false;
  }

  std::vector<char> buffer(1 << 20);
  qmc_decoder::seed seed_;

  while (true) {
    const auto bytes_read =
        std::fread(buffer.data(), 1, buffer.size(), infile.get());

    if (bytes_read > 0) {
      for (size_t i = 0; i < bytes_read; ++i) {
        buffer[i] = static_cast<char>(seed_.next_mask() ^ buffer[i]);
      }

      const auto bytes_written =
          std::fwrite(buffer.data(), 1, bytes_read, outfile.get());
      if (bytes_written != bytes_read) {
        std::cerr << "write file error: "
                  << path_to_display_string(decoded_path) << std::endl;
        return false;
      }
    }

    if (bytes_read < buffer.size()) {
      if (std::ferror(infile.get()) != 0) {
        std::cerr << "read file error: "
                  << path_to_display_string(input_path) << std::endl;
        return false;
      }
      break;
    }
  }

  return true;
}

fs::path resolve_ffmpeg_path() {
#ifdef _WIN32
  wchar_t module_path[MAX_PATH] = {};
  const auto module_len = GetModuleFileNameW(nullptr, module_path, MAX_PATH);
  if (module_len > 0 && module_len < MAX_PATH) {
    fs::path candidate = fs::path(module_path).parent_path() / L"ffmpeg.exe";
    if (fs::exists(candidate) && fs::is_regular_file(candidate)) {
      return candidate;
    }
  }

  wchar_t resolved_path[MAX_PATH] = {};
  auto resolved_len =
      SearchPathW(nullptr, L"ffmpeg.exe", nullptr, MAX_PATH, resolved_path, nullptr);
  if (resolved_len > 0 && resolved_len < MAX_PATH) {
    return fs::path(resolved_path);
  }

  resolved_len =
      SearchPathW(nullptr, L"ffmpeg", L".exe", MAX_PATH, resolved_path, nullptr);
  if (resolved_len > 0 && resolved_len < MAX_PATH) {
    return fs::path(resolved_path);
  }

  return fs::path();
#else
  return fs::path("ffmpeg");
#endif
}

std::wstring quote_windows_arg(const std::wstring& arg) {
  if (arg.empty()) {
    return L"\"\"";
  }

  if (arg.find_first_of(L" \t\n\v\"") == std::wstring::npos) {
    return arg;
  }

  std::wstring quoted = L"\"";
  size_t backslash_count = 0;

  for (wchar_t ch : arg) {
    if (ch == L'\\') {
      ++backslash_count;
      continue;
    }

    if (ch == L'"') {
      quoted.append(backslash_count * 2 + 1, L'\\');
      quoted.push_back(ch);
      backslash_count = 0;
      continue;
    }

    quoted.append(backslash_count, L'\\');
    backslash_count = 0;
    quoted.push_back(ch);
  }

  quoted.append(backslash_count * 2, L'\\');
  quoted.push_back(L'"');
  return quoted;
}

bool run_ffmpeg(const fs::path& ffmpeg_path, const fs::path& input_audio_path,
                const fs::path& output_mp3) {
#ifdef _WIN32
  std::vector<std::wstring> args = {
      ffmpeg_path.wstring(), L"-hide_banner", L"-loglevel", L"error", L"-y",
      L"-i", input_audio_path.wstring(), L"-vn", L"-codec:a", L"libmp3lame",
      L"-q:a", L"0", output_mp3.wstring()};

  std::wstring command_line;
  for (size_t i = 0; i < args.size(); ++i) {
    if (i != 0) {
      command_line.push_back(L' ');
    }
    command_line += quote_windows_arg(args[i]);
  }

  STARTUPINFOW startup_info = {};
  startup_info.cb = sizeof(startup_info);
  PROCESS_INFORMATION process_info = {};

  if (!CreateProcessW(ffmpeg_path.wstring().c_str(), command_line.data(), nullptr,
                      nullptr, FALSE, 0, nullptr, nullptr, &startup_info,
                      &process_info)) {
    std::cerr << "failed to start ffmpeg: "
              << path_to_display_string(ffmpeg_path) << std::endl;
    return false;
  }

  WaitForSingleObject(process_info.hProcess, INFINITE);

  DWORD exit_code = 1;
  GetExitCodeProcess(process_info.hProcess, &exit_code);
  CloseHandle(process_info.hThread);
  CloseHandle(process_info.hProcess);

  if (exit_code != 0) {
    std::cerr << "ffmpeg convert failed: "
              << path_to_display_string(output_mp3) << std::endl;
    return false;
  }
#else
  std::vector<std::string> args = {
      ffmpeg_path.string(),
      "-hide_banner",
      "-loglevel",
      "error",
      "-y",
      "-i",
      input_audio_path.string(),
      "-vn",
      "-codec:a",
      "libmp3lame",
      "-q:a",
      "0",
      output_mp3.string()};

  std::vector<char*> argv;
  argv.reserve(args.size() + 1);
  for (auto& arg : args) {
    argv.push_back(&arg[0]);
  }
  argv.push_back(nullptr);

  const auto pid = fork();
  if (pid < 0) {
    std::cerr << "failed to start ffmpeg." << std::endl;
    return false;
  }

  if (pid == 0) {
    execvp("ffmpeg", argv.data());
    _exit(127);
  }

  int status = 0;
  if (waitpid(pid, &status, 0) == -1) {
    std::cerr << "failed waiting for ffmpeg." << std::endl;
    return false;
  }

  if (!WIFEXITED(status) || WEXITSTATUS(status) != 0) {
    std::cerr << "ffmpeg convert failed: "
              << path_to_display_string(output_mp3) << std::endl;
    return false;
  }
#endif

  return true;
}

bool ensure_directory(const fs::path& directory) {
  if (directory.empty()) {
    return true;
  }

  try {
    if (fs::exists(directory)) {
      if (!fs::is_directory(directory)) {
        std::cerr << "path is not a directory: "
                  << path_to_display_string(directory)
                  << std::endl;
        return false;
      }
      return true;
    }

    fs::create_directories(directory);
    return true;
  } catch (const fs::filesystem_error& e) {
    std::cerr << "directory error: " << e.what() << std::endl;
    return false;
  }
}

bool paths_match(const fs::path& lhs, const fs::path& rhs) {
  return lhs.lexically_normal() == rhs.lexically_normal();
}

bool convert_to_mp3(const fs::path& ffmpeg_path, const fs::path& decoded_path,
                    const fs::path& output_mp3) {
  if (!ensure_directory(output_mp3.parent_path())) {
    return false;
  }

  fs::path ffmpeg_output = output_mp3;
  bool replace_original = false;

  if (paths_match(decoded_path, output_mp3)) {
    ffmpeg_output = make_mp3_temp_output_path(output_mp3);
    replace_original = true;

    try {
      if (fs::exists(ffmpeg_output)) {
        fs::remove(ffmpeg_output);
      }
    } catch (const fs::filesystem_error& e) {
      std::cerr << "cleanup temp mp3 file failed: " << e.what() << std::endl;
      return false;
    }
  }

  if (!run_ffmpeg(ffmpeg_path, decoded_path, ffmpeg_output)) {
    return false;
  }

  if (!replace_original) {
    return true;
  }

  try {
    if (fs::exists(output_mp3)) {
      fs::remove(output_mp3);
    }
    fs::rename(ffmpeg_output, output_mp3);
  } catch (const fs::filesystem_error& e) {
    std::cerr << "replace mp3 file failed: " << e.what() << std::endl;
    return false;
  }

  return true;
}

bool decode_to_output(const fs::path& input_path, const fs::path& decoded_path) {
  const auto format = detect_format(input_path);
  if (format == audioFormat::unsupported) {
    std::cerr << "unsupported file: " << path_to_display_string(input_path)
              << std::endl;
    return false;
  }

  if (!ensure_directory(decoded_path.parent_path())) {
    return false;
  }

  std::cout << "decode: " << path_to_display_string(input_path) << std::endl;
  std::cout << "decoded output: " << path_to_display_string(decoded_path)
            << std::endl;

  return decode_qmc_file(input_path, decoded_path);
}

bool read_required_directory_from_console(const char* prompt, fs::path& path) {
  std::string input;
  std::cout << prompt << std::flush;
  if (!std::getline(std::cin, input)) {
    return false;
  }

  input = normalize_user_input(std::move(input));
  if (input.empty()) {
    return false;
  }

  path = fs::path(input);
  return true;
}

bool read_optional_directory_from_console(const char* prompt, fs::path& path) {
  std::string input;
  std::cout << prompt << std::flush;
  if (!std::getline(std::cin, input)) {
    return false;
  }

  input = normalize_user_input(std::move(input));
  path = input.empty() ? fs::path() : fs::path(input);
  return true;
}

int process_directory(const fs::path& input_dir, const fs::path& output_dir,
                      const fs::path& mp3_output_dir) {
  if (!fs::exists(input_dir) || !fs::is_directory(input_dir)) {
    std::cerr << "input directory not found: "
              << path_to_display_string(input_dir)
              << std::endl;
    return 1;
  }

  if (!ensure_directory(output_dir)) {
    return 1;
  }

  if (!mp3_output_dir.empty() && !ensure_directory(mp3_output_dir)) {
    return 1;
  }

  fs::path ffmpeg_path;
  if (!mp3_output_dir.empty()) {
    ffmpeg_path = resolve_ffmpeg_path();
    if (ffmpeg_path.empty()) {
      std::cerr
          << "ffmpeg.exe not found. Put ffmpeg.exe next to qmc-decoder.exe or "
             "make sure ffmpeg is in PATH, then restart Explorer or open a new shell."
          << std::endl;
      return 1;
    }

    std::cout << "ffmpeg: " << path_to_display_string(ffmpeg_path)
              << std::endl;
  }

  std::vector<fs::path> qmc_paths;
  try {
    for (auto& entry : fs::recursive_directory_iterator(input_dir)) {
      if (fs::is_regular_file(entry.path()) && is_qmc_file(entry.path())) {
        qmc_paths.emplace_back(entry.path());
      }
    }
  } catch (const fs::filesystem_error& e) {
    std::cerr << "scan directory failed: " << e.what() << std::endl;
    return 1;
  }

  if (qmc_paths.empty()) {
    std::cout << "no qmc files found in: " << path_to_display_string(input_dir)
              << std::endl;
    return 0;
  }

  std::vector<std::pair<fs::path, fs::path>> mp3_jobs;
  size_t decode_success_count = 0;
  size_t decode_failed_count = 0;
  for (const auto& qmc_path : qmc_paths) {
    try {
      const auto decoded_output =
          make_decoded_output_path(qmc_path, input_dir, output_dir);
      const auto mp3_output =
          mp3_output_dir.empty()
              ? fs::path()
              : make_mp3_output_path(qmc_path, input_dir, mp3_output_dir);

      if (decode_to_output(qmc_path, decoded_output)) {
        ++decode_success_count;
        if (!mp3_output.empty()) {
          mp3_jobs.emplace_back(decoded_output, mp3_output);
        }
      } else {
        ++decode_failed_count;
      }
    } catch (const fs::filesystem_error& e) {
      std::cerr << "file process failed: " << path_to_display_string(qmc_path)
                << std::endl;
      std::cerr << e.what() << std::endl;
      ++decode_failed_count;
    }
  }

  std::cout << "decode done. success: " << decode_success_count
            << ", failed: " << decode_failed_count << std::endl;

  size_t mp3_success_count = 0;
  size_t mp3_failed_count = 0;
  if (!mp3_jobs.empty()) {
    std::cout << "starting mp3 conversion..." << std::endl;
    for (const auto& job : mp3_jobs) {
      const auto& decoded_output = job.first;
      const auto& mp3_output = job.second;

      std::cout << "mp3 output: " << path_to_display_string(mp3_output)
                << std::endl;
      if (convert_to_mp3(ffmpeg_path, decoded_output, mp3_output)) {
        ++mp3_success_count;
      } else {
        ++mp3_failed_count;
      }
    }

    std::cout << "mp3 done. success: " << mp3_success_count
              << ", failed: " << mp3_failed_count << std::endl;
  }

  return (decode_failed_count == 0 && mp3_failed_count == 0) ? 0 : 2;
}

void wait_for_exit() {
  std::cout << "Press Enter to exit..." << std::endl;
  std::string ignored;
  std::getline(std::cin, ignored);
}

void print_usage() {
  std::cout
      << "Usage:\n"
      << "  qmc-decoder                                 # prompt for input/output/mp3 directories\n"
      << "  qmc-decoder INPUT_DIR OUTPUT_DIR            # batch decode only\n"
      << "  qmc-decoder INPUT_DIR OUTPUT_DIR MP3_DIR    # batch decode and convert to MP3 VBR q=0\n"
      << "  qmc-decoder /PATH/TO/SONG                   # decode one file next to the source\n";
}
}  // namespace

int main(int argc, char** argv) {
  const bool interactive_mode = argc == 1;

  auto finish = [interactive_mode](int code) {
    if (interactive_mode) {
      wait_for_exit();
    }
    return code;
  };

  if (argc > 4) {
    print_usage();
    return finish(1);
  }

  try {
    if (argc == 1) {
      fs::path input_dir;
      fs::path output_dir;
      fs::path mp3_output_dir;

      if (!read_required_directory_from_console("Input directory: ", input_dir)) {
        std::cerr << "input directory is required." << std::endl;
        return finish(1);
      }

      if (!read_required_directory_from_console("Output directory: ", output_dir)) {
        std::cerr << "output directory is required." << std::endl;
        return finish(1);
      }

      if (!read_optional_directory_from_console(
              "MP3 output directory (leave empty to skip conversion): ",
              mp3_output_dir)) {
        std::cerr << "failed to read mp3 output directory." << std::endl;
        return finish(1);
      }

      return finish(process_directory(input_dir, output_dir, mp3_output_dir));
    }

    if (argc == 2) {
      const fs::path input_path(argv[1]);
      if (!fs::exists(input_path) || !fs::is_regular_file(input_path)) {
        std::cerr << "input file not found: "
                  << path_to_display_string(input_path) << std::endl;
        return 1;
      }

      const auto decoded_output = make_single_file_output_path(input_path);
      return decode_to_output(input_path, decoded_output) ? 0 : 2;
    }

    if (argc == 3) {
      return process_directory(fs::path(argv[1]), fs::path(argv[2]), fs::path());
    }

    return process_directory(fs::path(argv[1]), fs::path(argv[2]),
                             fs::path(argv[3]));
  } catch (const fs::filesystem_error& e) {
    std::cerr << "filesystem error: " << e.what() << std::endl;
    return finish(2);
  } catch (const std::exception& e) {
    std::cerr << "unexpected error: " << e.what() << std::endl;
    return finish(2);
  } catch (...) {
    std::cerr << "unexpected unknown error." << std::endl;
    return finish(2);
  }
}
