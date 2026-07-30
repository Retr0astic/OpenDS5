// SPDX-License-Identifier: MIT
// Copyright (C) 2026 Jihong Min <hurryman2212@gmail.com>
#pragma once

#include <fstream>
#include <mutex>
#include <string>
#include <string_view>

namespace vds {

#ifdef _WIN32
inline constexpr const char *kDefaultLogPath = R"(C:\ProgramData\vDS\vdsd.log)";
#else
inline constexpr const char *kDefaultLogPath = "/var/log/vdsd.log";
#endif

// Select the startup default without changing the explicit --log override.
// System/root operation retains the historical systemd path; ordinary users
// are redirected to their XDG state directory.
std::string default_log_path();

enum class LogLevel {
  Debug,
  Info,
  Warn,
  Error,
};

enum class LogScope {
  Bluetooth,
  Companion,
  Config,
  Control,
  Daemon,
  Hid,
  InputAudio,
  InputControl,
  Output,
  Port,
  Usb,
};

class Logger {
public:
  explicit Logger(const std::string &path);

  void reopen();
  void log(LogScope scope, LogLevel level, std::string_view message);

private:
  std::ofstream open_file() const;

  std::mutex mutex_;
  std::string path_;
  std::ofstream file_;
};

const char *log_level_name(LogLevel level);
const char *log_scope_name(LogScope scope);

} // namespace vds
