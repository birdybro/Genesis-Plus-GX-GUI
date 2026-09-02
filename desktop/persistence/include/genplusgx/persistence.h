#pragma once

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace genplusgx {

enum class PersistenceError {
  none,
  invalidRoot,
  invalidGameIdentity,
  directoryCreationFailed,
  fileOpenFailed,
  fileReadFailed,
  fileWriteFailed,
  fileCommitFailed,
  dataTooLarge,
  invalidData,
  hashFailed,
  cancelled,
};

struct PersistenceStatus final {
  PersistenceError error{PersistenceError::none};
  std::string message;

  [[nodiscard]] bool ok() const noexcept { return error == PersistenceError::none; }
  [[nodiscard]] operator bool() const noexcept { return ok(); }
};

enum class ApplicationDataMode {
  custom,
  platform,
  portable,
};

[[nodiscard]] std::string_view applicationDataModeName(
  ApplicationDataMode mode) noexcept;
[[nodiscard]] std::filesystem::path portableApplicationDataRoot(
  const std::filesystem::path& executablePath);

class ApplicationPaths final {
public:
  explicit ApplicationPaths(
    std::filesystem::path root = {},
    ApplicationDataMode mode = ApplicationDataMode::custom);

  [[nodiscard]] static ApplicationPaths fromPlatform();
  [[nodiscard]] static ApplicationPaths fromPortableExecutable(
    const std::filesystem::path& executablePath);
  [[nodiscard]] PersistenceStatus initialize() const;

  [[nodiscard]] const std::filesystem::path& root() const noexcept;
  [[nodiscard]] ApplicationDataMode mode() const noexcept;
  [[nodiscard]] bool portable() const noexcept;
  [[nodiscard]] std::filesystem::path configDirectory() const;
  [[nodiscard]] std::filesystem::path savesDirectory() const;
  [[nodiscard]] std::filesystem::path statesDirectory() const;
  [[nodiscard]] std::filesystem::path screenshotsDirectory() const;
  [[nodiscard]] std::filesystem::path recordingsDirectory() const;
  [[nodiscard]] std::filesystem::path cloudDirectory() const;
  [[nodiscard]] std::filesystem::path libraryDirectory() const;
  [[nodiscard]] std::filesystem::path logsDirectory() const;
  [[nodiscard]] std::filesystem::path cacheDirectory() const;

private:
  std::filesystem::path root_;
  ApplicationDataMode mode_{ApplicationDataMode::custom};
};

struct GameIdentity final {
  std::string sha256;
  std::string titleSlug;

  [[nodiscard]] bool valid() const noexcept;
  [[nodiscard]] std::string directoryName() const;
};

struct GameIdentityResult final {
  PersistenceStatus status;
  GameIdentity identity;
};

using GameContentObserver = std::function<void(
  std::span<const std::uint8_t> bytes,
  std::uintmax_t offset)>;
using GameContentCancellation = std::function<bool()>;

struct GameContentHashResult final {
  PersistenceStatus status;
  std::string sha256;
  std::uintmax_t primaryFileSize{0U};
};

[[nodiscard]] std::string sanitizeFilename(
  std::string_view input,
  std::size_t maximumLength = 64U);
[[nodiscard]] GameIdentityResult identifyGame(
  const std::filesystem::path& path,
  std::string_view preferredTitle = {},
  const GameContentCancellation& cancellationRequested = {});
[[nodiscard]] GameContentHashResult hashGameContent(
  const std::filesystem::path& path,
  const GameContentObserver& primaryFileObserver = {},
  const GameContentCancellation& cancellationRequested = {});

enum class SaveRamKind {
  cartridge,
  scdInternal,
  scdRamCartridge,
};

struct PersistenceLoadResult final {
  PersistenceStatus status;
  bool exists{false};
  std::vector<std::uint8_t> data;
};

[[nodiscard]] PersistenceStatus writeFileAtomically(
  const std::filesystem::path& destination,
  std::span<const std::uint8_t> data,
  std::size_t maximumBytes);
[[nodiscard]] PersistenceLoadResult readFileBounded(
  const std::filesystem::path& source,
  std::size_t maximumBytes);

class PersistenceStore final {
public:
  static constexpr std::size_t maximumRamBytes = 8U * 1024U * 1024U;

  explicit PersistenceStore(ApplicationPaths paths);

  [[nodiscard]] PersistenceStatus initialize() const;
  [[nodiscard]] const ApplicationPaths& paths() const noexcept;
  [[nodiscard]] std::filesystem::path gameSaveDirectory(
    const GameIdentity& identity) const;
  [[nodiscard]] std::filesystem::path ramPath(
    const GameIdentity& identity,
    SaveRamKind kind) const;

  [[nodiscard]] PersistenceStatus saveRam(
    const GameIdentity& identity,
    SaveRamKind kind,
    std::span<const std::uint8_t> data) const;
  [[nodiscard]] PersistenceLoadResult loadRam(
    const GameIdentity& identity,
    SaveRamKind kind,
    std::size_t maximumBytes = maximumRamBytes) const;

private:
  ApplicationPaths paths_;
};

} // namespace genplusgx
