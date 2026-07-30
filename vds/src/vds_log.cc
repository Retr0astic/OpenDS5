// SPDX-License-Identifier: MIT
// Copyright (C) 2026 Jihong Min <hurryman2212@gmail.com>

#include <chrono>
#include <algorithm>
#include <cstdlib>
#include <ctime>
#include <filesystem>
#include <iomanip>
#include <ostream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>

#ifndef _WIN32
#include <array>
#include <cerrno>
#include <cstring>

#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>
#endif

#include "vds_log.hh"

namespace {

#ifndef _WIN32
constexpr mode_t kVdsLogFileMode = S_IRUSR | S_IWUSR | S_IRGRP | S_IWGRP;
constexpr mode_t kUserLogFileMode = S_IRUSR | S_IWUSR;
constexpr mode_t kUserLogDirectoryMode = S_IRWXU;
#endif

std::string local_timestamp() {
  const auto now = std::chrono::system_clock::now();
  const auto seconds = std::chrono::time_point_cast<std::chrono::seconds>(now);
  const auto milliseconds =
      std::chrono::duration_cast<std::chrono::milliseconds>(now - seconds)
          .count();
  const std::time_t time = std::chrono::system_clock::to_time_t(now);

  std::tm local_time{};
#ifdef _WIN32
  if (localtime_s(&local_time, &time) != 0) {
    throw std::runtime_error("failed to get local time");
  }

  std::tm utc_time{};
  if (gmtime_s(&utc_time, &time) != 0) {
    throw std::runtime_error("failed to get UTC time");
  }

  const long offset_seconds = static_cast<long>(
      std::difftime(_mkgmtime(&local_time), _mkgmtime(&utc_time)));
#else
  if (!::localtime_r(&time, &local_time)) {
    throw std::runtime_error("failed to get local time");
  }

  const long offset_seconds = local_time.tm_gmtoff;
#endif
  const char offset_sign = offset_seconds < 0 ? '-' : '+';
  const long abs_offset = std::labs(offset_seconds);
  const long offset_hours = abs_offset / 3600;
  const long offset_minutes = (abs_offset % 3600) / 60;

  std::ostringstream out;
  out << std::put_time(&local_time, "%Y-%m-%dT%H:%M:%S") << '.' << std::setw(3)
      << std::setfill('0') << milliseconds << offset_sign << std::setw(2)
      << std::setfill('0') << offset_hours << ':' << std::setw(2)
      << std::setfill('0') << offset_minutes;
  return out.str();
}

void write_log_message(std::ostream &out, std::string_view message) {
  for (const char ch : message) {
    if (ch == '\n' || ch == '\r' || ch == '|') {
      out << ' ';
    } else {
      out << ch;
    }
  }
}

void prepare_log_file(const std::string &path, unsigned int mode) {
#ifdef _WIN32
  (void)path;
  (void)mode;
#else
  const int fd = ::open(path.c_str(), O_WRONLY | O_CREAT | O_APPEND | O_CLOEXEC,
                        static_cast<mode_t>(mode));
  if (fd < 0) {
    throw std::runtime_error("failed to open log file: " + path + ": " +
                             std::strerror(errno));
  }
  if (::fchmod(fd, static_cast<mode_t>(mode)) < 0) {
    const int error = errno;
    (void)::close(fd);
    throw std::runtime_error("failed to chmod log file: " + path + ": " +
                             std::strerror(error));
  }
  if (::close(fd) < 0) {
    throw std::runtime_error("failed to close log file: " + path + ": " +
                             std::strerror(errno));
  }
#endif
}

} // namespace

namespace vds {

std::string default_log_path() {
#ifdef _WIN32
  return kDefaultLogPath;
#else
  if (::geteuid() == 0) return kDefaultLogPath;
  const char *xdg_state = std::getenv("XDG_STATE_HOME");
  if (xdg_state && *xdg_state) {
    return (std::filesystem::path(xdg_state) / "vds" / "vdsd.log").string();
  }
  const char *home = std::getenv("HOME");
  if (home && *home) {
    return (std::filesystem::path(home) / ".local" / "state" / "vds" /
            "vdsd.log").string();
  }
  struct PrivateLogPath {
    std::string path;
    ~PrivateLogPath() {
      std::error_code error;
      std::filesystem::remove_all(std::filesystem::path(path).parent_path(), error);
    }
  };
  static const PrivateLogPath private_log_path = [] {
    std::array<char, 32> template_path{};
    const std::string prefix = "/tmp/vdsd-";
    std::copy(prefix.begin(), prefix.end(), template_path.begin());
    std::copy_n("XXXXXX", 6, template_path.begin() + prefix.size());
    char *directory = ::mkdtemp(template_path.data());
    if (!directory) {
      throw std::runtime_error(
          "failed to create a private vDS log directory: " +
          std::string(std::strerror(errno)));
    }
    return PrivateLogPath{(std::filesystem::path(directory) / "vdsd.log").string()};
  }();
  return private_log_path.path;
#endif
}

Logger::Logger(const std::string &path) : path_(path) { file_ = open_file(); }

std::ofstream Logger::open_file() const {
  const std::filesystem::path log_path(path_);
  const std::filesystem::path directory = log_path.parent_path();
  if (!directory.empty()) {
    std::filesystem::create_directories(directory);
  }
  const bool system_log = path_ == kDefaultLogPath;
#ifndef _WIN32
  const bool managed_user_directory =
      !system_log &&
      (directory.filename() == "vds" ||
       directory.string().rfind("/tmp/vdsd-", 0) == 0);
  if (managed_user_directory) {
    if (::chmod(directory.c_str(), kUserLogDirectoryMode) < 0) {
      throw std::runtime_error("failed to chmod log directory: " +
                               directory.string() + ": " + std::strerror(errno));
    }
  }
  prepare_log_file(path_, system_log ? kVdsLogFileMode : kUserLogFileMode);
#else
  prepare_log_file(path_, 0);
#endif
  std::ofstream file(path_, std::ios::app);
  if (!file) {
    throw std::runtime_error("failed to open log file: " + path_);
  }
  return file;
}

void Logger::reopen() {
  std::ofstream file = open_file();

  std::lock_guard guard(mutex_);
  file_.flush();
  file_.close();
  file_ = std::move(file);
}

void Logger::log(LogScope scope, LogLevel level, std::string_view message) {
  std::lock_guard guard(mutex_);
  file_ << local_timestamp() << '|' << log_scope_name(scope) << '|'
        << log_level_name(level) << '|';
  write_log_message(file_, message);
  file_ << '\n';
  file_.flush();
}

const char *log_level_name(LogLevel level) {
  switch (level) {
  case LogLevel::Debug:
    return "DEBUG";
  case LogLevel::Info:
    return "INFO";
  case LogLevel::Warn:
    return "WARN";
  case LogLevel::Error:
    return "ERROR";
  }
  return "ERROR";
}

const char *log_scope_name(LogScope scope) {
  switch (scope) {
  case LogScope::Bluetooth:
    return "bluetooth";
  case LogScope::Companion:
    return "companion";
  case LogScope::Config:
    return "config";
  case LogScope::Control:
    return "control";
  case LogScope::Daemon:
    return "daemon";
  case LogScope::Hid:
    return "hid";
  case LogScope::InputAudio:
    return "input-audio";
  case LogScope::InputControl:
    return "input-control";
  case LogScope::Output:
    return "output";
  case LogScope::Port:
    return "port";
  case LogScope::Usb:
    return "usb";
  }
  return "daemon";
}

} // namespace vds
