#pragma once

#include "genplusgx/backup_memory.h"
#include "genplusgx/persistence.h"

#include <optional>

namespace genplusgx {

class PerGameBackupStore final : public BackupMemoryPersistence {
public:
  explicit PerGameBackupStore(PersistenceStore store);

  [[nodiscard]] BackupPersistenceStatus beginGame(
    const std::filesystem::path& path,
    const BackupPersistenceCancellation& cancellationRequested = {}) override;
  [[nodiscard]] BackupPersistenceLoadResult load(
    BackupMemoryKind kind,
    std::size_t expectedSize) override;
  [[nodiscard]] BackupPersistenceStatus save(
    BackupMemoryKind kind,
    std::span<const std::uint8_t> data) override;
  void endGame() noexcept override;

  [[nodiscard]] bool hasActiveGame() const noexcept;

private:
  PersistenceStore store_;
  std::optional<GameIdentity> activeIdentity_;
};

} // namespace genplusgx
