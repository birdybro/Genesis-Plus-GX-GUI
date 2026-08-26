#include "genplusgx/backup_store.h"

#include <utility>

namespace genplusgx {
namespace {

SaveRamKind persistenceKind(BackupMemoryKind kind)
{
  switch (kind) {
    case BackupMemoryKind::cartridgeSram:
      return SaveRamKind::cartridge;
    case BackupMemoryKind::scdInternalBram:
      return SaveRamKind::scdInternal;
    case BackupMemoryKind::scdRamCartridge:
      return SaveRamKind::scdRamCartridge;
  }
  return SaveRamKind::cartridge;
}

BackupPersistenceStatus failure(
  BackupPersistenceError error,
  std::string message)
{
  return {.error = error, .message = std::move(message)};
}

} // namespace

PerGameBackupStore::PerGameBackupStore(PersistenceStore store)
  : store_(std::move(store))
{
}

BackupPersistenceStatus PerGameBackupStore::beginGame(
  const std::filesystem::path& path,
  const BackupPersistenceCancellation& cancellationRequested)
{
  activeIdentity_.reset();
  const auto identity = identifyGame(path, {}, cancellationRequested);
  if (!identity.status) {
    return failure(BackupPersistenceError::identityFailed, identity.status.message);
  }
  activeIdentity_ = identity.identity;
  return {};
}

BackupPersistenceLoadResult PerGameBackupStore::load(
  BackupMemoryKind kind,
  std::size_t expectedSize)
{
  if (!activeIdentity_) {
    return {
      .status = failure(
        BackupPersistenceError::noActiveGame,
        "Backup memory was requested without an active game identity."),
      .exists = false,
      .data = {},
    };
  }
  auto loaded = store_.loadRam(
    *activeIdentity_, persistenceKind(kind), expectedSize);
  if (!loaded.status) {
    return {
      .status = failure(BackupPersistenceError::loadFailed, loaded.status.message),
      .exists = loaded.exists,
      .data = {},
    };
  }
  return {
    .status = {},
    .exists = loaded.exists,
    .data = std::move(loaded.data),
  };
}

BackupPersistenceStatus PerGameBackupStore::save(
  BackupMemoryKind kind,
  std::span<const std::uint8_t> data)
{
  if (!activeIdentity_) {
    return failure(
      BackupPersistenceError::noActiveGame,
      "Backup memory was saved without an active game identity.");
  }
  const auto saved = store_.saveRam(
    *activeIdentity_, persistenceKind(kind), data);
  if (!saved) {
    return failure(BackupPersistenceError::saveFailed, saved.message);
  }
  return {};
}

void PerGameBackupStore::endGame() noexcept
{
  activeIdentity_.reset();
}

bool PerGameBackupStore::hasActiveGame() const noexcept
{
  return activeIdentity_.has_value();
}

} // namespace genplusgx
