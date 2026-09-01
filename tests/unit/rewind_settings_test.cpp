#include "genplusgx/settings/rewind_settings.h"

#include "genplusgx/persistence.h"

#include <QTemporaryDir>

#include <cstdlib>
#include <iostream>
#include <span>
#include <string>

namespace {

bool check(bool condition, const std::string& message)
{
  if (!condition) {
    std::cerr << message << '\n';
  }
  return condition;
}

} // namespace

int main()
{
  using namespace genplusgx;
  using namespace genplusgx::settings;
  constexpr std::size_t mebibyte = 1024U * 1024U;

  QTemporaryDir directory;
  if (!check(directory.isValid(), "Could not create rewind settings directory")) {
    return EXIT_FAILURE;
  }
  const auto path = std::filesystem::path{directory.path().toStdString()} /
    "nested" / "rewind-settings.json";
  RewindSettingsStore store{path};
  const auto missing = store.load();
  const RewindConfiguration customized{
    .enabled = false,
    .captureIntervalFrames = 4U,
    .memoryLimitBytes = 256U * mebibyte,
  };
  if (!check(missing.status && missing.settings == defaultRewindSettings(),
        "Missing rewind settings did not use safe defaults") ||
      !check(store.save(customized), "Valid rewind settings could not be saved")) {
    return EXIT_FAILURE;
  }
  const auto loaded = store.load();
  if (!check(loaded.status && loaded.settings == customized,
        "Rewind settings did not round-trip exactly") ||
      !check(!store.save({
          .enabled = true,
          .captureIntervalFrames = 0U,
          .memoryLimitBytes = 16U * mebibyte,
        }), "Invalid rewind settings were persisted")) {
    return EXIT_FAILURE;
  }

  const std::string corrupt = "{\"schemaVersion\":1,\"rewind\":{";
  if (!check(writeFileAtomically(path,
        std::span<const std::uint8_t>{
          reinterpret_cast<const std::uint8_t*>(corrupt.data()), corrupt.size()},
        RewindSettingsStore::maximumFileBytes),
        "Could not write corrupt rewind settings fixture")) {
    return EXIT_FAILURE;
  }
  const auto rejected = store.load();
  if (!check(!rejected.status &&
        rejected.status.error == PersistenceError::invalidData &&
        rejected.settings == defaultRewindSettings(),
        "Corrupt rewind settings were not rejected safely")) {
    return EXIT_FAILURE;
  }

  return EXIT_SUCCESS;
}
