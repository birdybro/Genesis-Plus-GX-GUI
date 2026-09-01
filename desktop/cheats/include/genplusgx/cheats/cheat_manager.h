#pragma once

#include "genplusgx/core_cheat.h"
#include "genplusgx/persistence.h"

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

namespace genplusgx::cheats {

enum class CheatSystem {
  genesis,
  masterSystem,
};

enum class CheatFormat {
  genesisGameGenie,
  genesisActionReplay,
  masterSystemGameGenie,
  masterSystemActionReplay,
  fusionRam,
  fusionRom,
};

enum class CheatError {
  none,
  emptyCode,
  invalidFormat,
  tooManyPatches,
  invalidDefinition,
  invalidImport,
};

struct CheatStatus final {
  CheatError error{CheatError::none};
  std::string message;

  [[nodiscard]] bool ok() const noexcept { return error == CheatError::none; }
  [[nodiscard]] operator bool() const noexcept { return ok(); }
};

struct CheatParseResult final {
  CheatStatus status;
  std::string normalizedCode;
  std::vector<CoreCheatPatch> patches;
  std::vector<CheatFormat> formats;
};

struct CheatDefinition final {
  std::string name;
  std::string code;
  bool enabled{false};

  [[nodiscard]] bool operator==(const CheatDefinition&) const = default;
};

struct CheatConfiguration final {
  std::vector<CheatDefinition> entries;

  [[nodiscard]] bool operator==(const CheatConfiguration&) const = default;
};

inline constexpr std::size_t maximumCheatDefinitions = 150U;
inline constexpr std::size_t maximumCheatNameBytes = 120U;
inline constexpr std::size_t maximumCheatCodeBytes = 256U;
inline constexpr std::size_t maximumCheatImportBytes = 128U * 1024U;

enum class CheatListFormat {
  autoDetect,
  retroArch,
  plainText,
};

struct CheatImportResult final {
  CheatStatus status;
  CheatConfiguration configuration;
  CheatListFormat format{CheatListFormat::autoDetect};
};

[[nodiscard]] CheatParseResult parseCheatCode(
  CheatSystem system, std::string_view code);
[[nodiscard]] CheatParseResult makeRamCheatCode(
  CheatSystem system, std::uint32_t offset, std::uint32_t value);
[[nodiscard]] CheatStatus validateCheatConfiguration(CheatSystem system,
  const CheatConfiguration& configuration,
  std::vector<CoreCheatPatch>* enabledPatches = nullptr);
[[nodiscard]] CheatImportResult parseCheatList(CheatSystem system,
  std::string_view text,
  CheatListFormat format = CheatListFormat::autoDetect);
[[nodiscard]] CheatImportResult importCheatList(
  CheatSystem system, const std::filesystem::path& path);

struct CheatLoadResult final {
  PersistenceStatus status;
  CheatConfiguration configuration;
};

class CheatStore final {
public:
  static constexpr std::uint32_t schemaVersion = 1U;
  static constexpr std::size_t maximumFileBytes = 128U * 1024U;

  explicit CheatStore(std::filesystem::path root);

  [[nodiscard]] const std::filesystem::path& root() const noexcept;
  [[nodiscard]] std::filesystem::path pathFor(const GameIdentity& identity) const;
  [[nodiscard]] CheatLoadResult load(
    const GameIdentity& identity, CheatSystem system) const;
  [[nodiscard]] PersistenceStatus save(const GameIdentity& identity,
    CheatSystem system,
    const CheatConfiguration& configuration) const;

private:
  std::filesystem::path root_;
};

} // namespace genplusgx::cheats
