#include "genplusgx/platform/bios_manager.h"

#include <QTemporaryDir>

#include <algorithm>
#include <cstdint>
#include <filesystem>
#include <iostream>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace {

bool check(bool condition, const char* message)
{
  if (!condition) {
    std::cerr << message << '\n';
  }
  return condition;
}

bool writeBytes(
  const std::filesystem::path& path,
  std::span<const std::uint8_t> data)
{
  return genplusgx::writeFileAtomically(
    path, data, 4U * 1024U * 1024U);
}

bool writeText(const std::filesystem::path& path, std::string_view text)
{
  return writeBytes(path, std::span<const std::uint8_t>{
    reinterpret_cast<const std::uint8_t*>(text.data()), text.size()});
}

std::vector<std::uint8_t> firmware(std::size_t size)
{
  std::vector<std::uint8_t> bytes(size);
  for (std::size_t index = 0U; index < bytes.size(); ++index) {
    bytes[index] = static_cast<std::uint8_t>(
      ((index * 37U) + (index / 251U) + 11U) & 0xffU);
  }
  return bytes;
}

std::filesystem::path pathIn(
  const QTemporaryDir& directory,
  std::string_view name)
{
  return std::filesystem::path{directory.path().toStdString()} /
         std::string{name};
}

} // namespace

int main()
{
  using namespace genplusgx::platform;
  QTemporaryDir directory;
  if (!check(directory.isValid(), "Temporary directory was unavailable")) {
    return 1;
  }

  const auto unconfigured = validateBios(BiosSlot::segaCdUsa, {});
  const auto missing = validateBios(
    BiosSlot::segaCdUsa, pathIn(directory, "missing.bin"));
  if (!check(unconfigured.state == BiosValidationState::notConfigured &&
      missing.state == BiosValidationState::missing,
      "Missing and unconfigured firmware were not distinguished")) {
    return 2;
  }

  const auto segaCdPath = pathIn(directory, "segacd-us.bin");
  const auto segaCdBytes = firmware(128U * 1024U);
  if (!check(writeBytes(segaCdPath, segaCdBytes),
        "Generated Sega CD firmware fixture could not be written")) {
    return 3;
  }
  const auto segaCd = validateBios(BiosSlot::segaCdUsa, segaCdPath);
  if (!check(segaCd.valid() && segaCd.fileSize == segaCdBytes.size() &&
      segaCd.sha256.size() == 64U &&
      segaCd.detectedType.find("Sega CD") != std::string::npos,
      "Generated Sega CD firmware was not validated and identified")) {
    return 4;
  }

  const auto wrongCdPath = pathIn(directory, "segacd-wrong.bin");
  const auto wrongCdBytes = firmware(64U * 1024U);
  if (!check(writeBytes(wrongCdPath, wrongCdBytes) &&
      validateBios(BiosSlot::segaCdEurope, wrongCdPath).state ==
        BiosValidationState::invalidSize,
      "Wrong-sized Sega CD firmware was accepted")) {
    return 5;
  }

  const auto gameGearPath = pathIn(directory, "gamegear.bin");
  const auto gameGearBytes = firmware(1U * 1024U);
  const auto masterSystemPath = pathIn(directory, "mastersystem.bin");
  const auto masterSystemBytes = firmware(8U * 1024U);
  if (!check(writeBytes(gameGearPath, gameGearBytes) &&
      writeBytes(masterSystemPath, masterSystemBytes) &&
      validateBios(BiosSlot::gameGear, gameGearPath).valid() &&
      validateBios(BiosSlot::masterSystemJapan, masterSystemPath).valid(),
      "Valid 8-bit firmware shapes were rejected")) {
    return 6;
  }

  const auto repeatedPath = pathIn(directory, "repeated.bin");
  const std::vector<std::uint8_t> repeated(2U * 1024U, 0xffU);
  if (!check(writeBytes(repeatedPath, repeated) &&
      validateBios(BiosSlot::genesis, repeatedPath).state ==
        BiosValidationState::invalidContent,
      "Repeated-byte firmware content was accepted")) {
    return 7;
  }

  const auto longPath = std::filesystem::path{"/" + std::string(4'096U, 'x')};
  if (!check(validateBios(BiosSlot::genesis, longPath).state ==
      BiosValidationState::pathTooLong,
      "A path exceeding the core host boundary was accepted")) {
    return 8;
  }

  const auto storePath = pathIn(directory, "config/bios.json");
  BiosConfigurationStore store{storePath};
  const auto empty = store.load();
  if (!check(empty.status && empty.configuration == BiosConfiguration{},
      "A missing BIOS configuration did not load empty defaults")) {
    return 9;
  }
  BiosConfiguration configured;
  configured.setPath(BiosSlot::segaCdUsa, segaCdPath);
  configured.setPath(BiosSlot::gameGear, gameGearPath);
  if (!check(store.save(configured) &&
      store.load().configuration == configured,
      "BIOS configuration did not round-trip atomically")) {
    return 10;
  }

  const std::string legacy = std::string{R"json({
    "schemaVersion": 0,
    "genesis": "",
    "masterSystemUsa": "",
    "masterSystemEurope": "",
    "masterSystemJapan": "",
    "gameGear": ")json"} + gameGearPath.generic_string() + R"json(",
    "segaCdUsa": "",
    "segaCdEurope": "",
    "segaCdJapan": ""
  })json";
  if (!check(writeText(storePath, legacy),
      "Legacy BIOS configuration could not be staged")) {
    return 11;
  }
  const auto migrated = store.load();
  if (!check(migrated.status && migrated.migrated &&
      migrated.configuration.path(BiosSlot::gameGear) == gameGearPath,
      "Schema-zero BIOS configuration did not migrate")) {
    return 12;
  }

  if (!check(writeText(storePath, "{broken"),
      "Corrupt BIOS configuration could not be staged") ||
      !check(!store.load().status,
        "Corrupt BIOS configuration did not fail closed")) {
    return 13;
  }
  if (!check(writeText(storePath, R"json({"schemaVersion":999})json"),
      "Future BIOS schema could not be staged") ||
      !check(!store.load().status, "Future BIOS schema was accepted")) {
    return 14;
  }

  BiosManager manager{BiosConfigurationStore{storePath}};
  if (!check(manager.apply(configured) &&
      manager.snapshot().configuration == configured &&
      manager.snapshot().validation[static_cast<std::size_t>(
        BiosSlot::segaCdUsa)].valid(),
      "BIOS manager did not save and refresh validation atomically")) {
    return 15;
  }
  BiosManager reloaded{BiosConfigurationStore{storePath}};
  return check(reloaded.load() && reloaded.snapshot().configuration == configured &&
      reloaded.snapshot().validation[static_cast<std::size_t>(
        BiosSlot::gameGear)].valid(),
    "BIOS manager did not reload persisted valid firmware") ? 0 : 16;
}
