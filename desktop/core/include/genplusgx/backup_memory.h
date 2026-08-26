#pragma once

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <span>
#include <string>
#include <vector>

namespace genplusgx {

enum class BackupMemoryKind {
  cartridgeSram,
  scdInternalBram,
  scdRamCartridge,
};

struct BackupMemoryInfo final {
  BackupMemoryKind kind{BackupMemoryKind::cartridgeSram};
  std::size_t size{0};
  bool available{false};
};

enum class BackupPersistenceError {
  none,
  noActiveGame,
  identityFailed,
  loadFailed,
  saveFailed,
};

struct BackupPersistenceStatus final {
  BackupPersistenceError error{BackupPersistenceError::none};
  std::string message;

  [[nodiscard]] bool ok() const noexcept
  {
    return error == BackupPersistenceError::none;
  }
  [[nodiscard]] operator bool() const noexcept { return ok(); }
};

struct BackupPersistenceLoadResult final {
  BackupPersistenceStatus status;
  bool exists{false};
  std::vector<std::uint8_t> data;
};

using BackupPersistenceCancellation = std::function<bool()>;

class BackupMemoryPersistence {
public:
  virtual ~BackupMemoryPersistence() = default;

  [[nodiscard]] virtual BackupPersistenceStatus beginGame(
    const std::filesystem::path& path,
    const BackupPersistenceCancellation& cancellationRequested = {}) = 0;
  [[nodiscard]] virtual BackupPersistenceLoadResult load(
    BackupMemoryKind kind,
    std::size_t expectedSize) = 0;
  [[nodiscard]] virtual BackupPersistenceStatus save(
    BackupMemoryKind kind,
    std::span<const std::uint8_t> data) = 0;
  virtual void endGame() noexcept = 0;
};

} // namespace genplusgx
