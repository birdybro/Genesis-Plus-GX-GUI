#pragma once

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace genplusgx::cloud {

inline constexpr std::size_t maximumFiles = 4'096U;
inline constexpr std::size_t maximumManifestBytes = 1024U * 1024U;
inline constexpr std::size_t maximumTransferBytes = 16U * 1024U * 1024U;
inline constexpr std::uint64_t maximumTotalBytes = 512U * 1024U * 1024U;

enum class Error : std::uint8_t {
  none,
  invalidSettings,
  invalidManifest,
  invalidPath,
  invalidData,
  dataTooLarge,
  ioError,
  authenticationFailed,
  transportFailed,
  remoteChanged,
  busy,
  notRunning,
  queueFull,
  cancelled,
  threadFailure,
};

struct Status final {
  Error error{Error::none};
  std::string message;

  [[nodiscard]] bool ok() const noexcept { return error == Error::none; }
  [[nodiscard]] operator bool() const noexcept { return ok(); }
};

enum class Action : std::uint8_t {
  unchanged,
  upload,
  download,
  conflict,
};

struct ItemResult final {
  std::string key;
  Action action{Action::unchanged};
  std::filesystem::path conflictPath;
};

struct Summary final {
  std::uint32_t unchanged{0U};
  std::uint32_t uploaded{0U};
  std::uint32_t downloaded{0U};
  std::uint32_t conflicts{0U};
  std::uint64_t uploadedBytes{0U};
  std::uint64_t downloadedBytes{0U};
  std::vector<ItemResult> items;
};

struct SyncResult final {
  Status status;
  Summary summary;
};

} // namespace genplusgx::cloud
