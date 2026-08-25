#pragma once

#include "genplusgx/core_adapter.h"

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace genplusgx::screenshots {

enum class ScreenshotError : std::uint8_t {
  none,
  invalidRequest,
  invalidFrame,
  invalidDirectory,
  directoryCreationFailed,
  encodeFailed,
  fileWriteFailed,
  filenameExhausted,
  alreadyRunning,
  notRunning,
  queueFull,
  threadFailure,
};

struct ScreenshotStatus final {
  ScreenshotError error{ScreenshotError::none};
  std::string message;

  [[nodiscard]] bool ok() const noexcept { return error == ScreenshotError::none; }
  [[nodiscard]] operator bool() const noexcept { return ok(); }
};

struct ScreenshotResult final {
  ScreenshotStatus status;
  std::filesystem::path path;
};

[[nodiscard]] ScreenshotResult writeNativeScreenshot(
  const std::filesystem::path& directory,
  std::string_view gameTitle,
  const CoreVideoFrameInfo& frame,
  std::span<const std::uint16_t> rgb565Pixels,
  std::chrono::system_clock::time_point timestamp = std::chrono::system_clock::now());

enum class ScreenshotEventType : std::uint8_t {
  serviceStarted,
  screenshotSaved,
  screenshotFailed,
  serviceStopped,
};

struct ScreenshotEvent final {
  ScreenshotEventType type{ScreenshotEventType::screenshotFailed};
  std::uint64_t operationId{0};
  ScreenshotStatus status;
  std::filesystem::path path;

  [[nodiscard]] bool succeeded() const noexcept
  {
    return type == ScreenshotEventType::screenshotSaved && status.ok();
  }
};

class ScreenshotService final {
public:
  explicit ScreenshotService(
    std::size_t commandCapacity = 4U, std::size_t eventCapacity = 8U);
  ~ScreenshotService();

  ScreenshotService(const ScreenshotService&) = delete;
  ScreenshotService& operator=(const ScreenshotService&) = delete;
  ScreenshotService(ScreenshotService&&) = delete;
  ScreenshotService& operator=(ScreenshotService&&) = delete;

  [[nodiscard]] ScreenshotStatus start();
  [[nodiscard]] ScreenshotStatus request(std::uint64_t operationId,
    std::filesystem::path directory,
    std::string gameTitle,
    CoreVideoFrameInfo frame,
    std::vector<std::uint16_t> rgb565Pixels);
  [[nodiscard]] std::optional<ScreenshotEvent> pollEvent();
  [[nodiscard]] std::optional<ScreenshotEvent> waitForEvent(
    std::chrono::milliseconds timeout);
  [[nodiscard]] ScreenshotStatus stop();

private:
  class Private;
  std::unique_ptr<Private> private_;
};

} // namespace genplusgx::screenshots
