#include "genplusgx/cheats/cheat_manager.h"

#include <QCoreApplication>
#include <QTemporaryDir>

#include <algorithm>
#include <array>
#include <cstdint>
#include <filesystem>
#include <iostream>
#include <random>
#include <span>
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

std::filesystem::path temporaryPath(const QTemporaryDir& directory)
{
#if defined(Q_OS_WIN)
  return std::filesystem::path{directory.path().toStdWString()};
#else
  return std::filesystem::path{directory.path().toUtf8().constData()};
#endif
}

bool writeText(const std::filesystem::path& path, std::string_view text)
{
  return genplusgx::writeFileAtomically(path,
    std::span<const std::uint8_t>{
      reinterpret_cast<const std::uint8_t*>(text.data()), text.size()},
    genplusgx::cheats::CheatStore::maximumFileBytes);
}

} // namespace

int main(int argc, char* argv[])
{
  QCoreApplication application{argc, argv};
  using namespace genplusgx::cheats;

  const auto actionReplay = parseCheatCode(CheatSystem::genesis, "123456:abcd");
  if (!check(actionReplay.status && actionReplay.normalizedCode == "123456:ABCD" &&
               actionReplay.patches.size() == 1U &&
               actionReplay.formats.front() == CheatFormat::genesisActionReplay &&
               actionReplay.patches.front().address == 0x123456U &&
               actionReplay.patches.front().data == 0xabcdU &&
               actionReplay.patches.front().width == genplusgx::CoreCheatWidth::word,
        "Genesis Action Replay decoding failed")) {
    return 1;
  }
  const auto gameGenie = parseCheatCode(CheatSystem::genesis, "  baaa-aaaa  ");
  if (!check(gameGenie.status && gameGenie.normalizedCode == "BAAA-AAAA" &&
               gameGenie.patches.front().address == 0U &&
               gameGenie.patches.front().data == 8U &&
               gameGenie.formats.front() == CheatFormat::genesisGameGenie,
        "Genesis Game Genie decoding or normalization failed") ||
      !check(!parseCheatCode(CheatSystem::genesis, "IAAA-AAAA").status &&
               !parseCheatCode(CheatSystem::genesis, "123456:ABCDjunk").status,
        "Invalid or trailing Genesis cheat data was accepted")) {
    return 2;
  }

  const auto fusionRam = parseCheatCode(CheatSystem::masterSystem, "C000:7f");
  const auto fusionRom = parseCheatCode(CheatSystem::masterSystem, "12ABCD:EF");
  const auto masterActionReplay =
    parseCheatCode(CheatSystem::masterSystem, "00C0-3E2A");
  const auto masterGameGenie = parseCheatCode(CheatSystem::masterSystem, "00A-000");
  const auto referenceGameGenie =
    parseCheatCode(CheatSystem::masterSystem, "00A-000-000");
  if (!check(fusionRam.status && fusionRam.patches.front().address == 0xff0000U &&
               fusionRam.patches.front().data == 0x7fU &&
               fusionRam.formats.front() == CheatFormat::fusionRam,
        "Fusion RAM decoding failed") ||
      !check(fusionRom.status && fusionRom.patches.front().address == 0xabcdU &&
               fusionRom.patches.front().data == 0xefU &&
               fusionRom.patches.front().reference == 0x12U &&
               fusionRom.patches.front().referenceRequired,
        "Fusion ROM decoding failed") ||
      !check(masterActionReplay.status &&
               masterActionReplay.patches.front().address == 0xff003eU &&
               masterActionReplay.patches.front().data == 0x2aU,
        "Master System Action Replay decoding failed") ||
      !check(masterGameGenie.status &&
               masterGameGenie.patches.front().address == 0xff1a00U &&
               !masterGameGenie.patches.front().referenceRequired &&
               referenceGameGenie.status &&
               referenceGameGenie.patches.front().reference == 0xbaU &&
               referenceGameGenie.patches.front().referenceRequired,
        "Master System Game Genie decoding failed")) {
    return 3;
  }

  const auto multipart =
    parseCheatCode(CheatSystem::genesis, "BAAA-AAAA + 123456:abcd");
  if (!check(multipart.status && multipart.patches.size() == 2U &&
               multipart.normalizedCode == "BAAA-AAAA+123456:ABCD",
        "Multi-part cheat decoding failed") ||
      !check(!parseCheatCode(CheatSystem::genesis, "BAAA-AAAA+").status &&
               !parseCheatCode(CheatSystem::masterSystem, "GGGG:00").status &&
               !parseCheatCode(CheatSystem::masterSystem, "00A-000-0G0").status,
        "Malformed multi-part or Master System cheats were accepted")) {
    return 4;
  }

  constexpr std::string_view fuzzAlphabet =
    "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789:-+ _/";
  std::minstd_rand random{0x47584755U};
  for (std::size_t iteration = 0U; iteration < 10'000U; ++iteration) {
    const auto length = static_cast<std::size_t>(random() % 40U);
    std::string candidate;
    candidate.reserve(length);
    for (std::size_t index = 0U; index < length; ++index) {
      candidate.push_back(fuzzAlphabet[random() % fuzzAlphabet.size()]);
    }
    const auto system =
      (random() & 1U) == 0U ? CheatSystem::genesis : CheatSystem::masterSystem;
    const auto parsed = parseCheatCode(system, candidate);
    if (parsed.status) {
      const auto reparsed = parseCheatCode(system, parsed.normalizedCode);
      if (!check(reparsed.status && reparsed.patches == parsed.patches &&
                   reparsed.formats == parsed.formats,
            "A bounded fuzz input did not decode deterministically")) {
        return 5;
      }
    }
  }

  CheatConfiguration configuration{
    .entries =
      {
        {.name = "Infinite lives", .code = "baaa-aaaa", .enabled = true},
        {.name = "Alternate mode", .code = "123456:ABCD", .enabled = false},
      },
  };
  std::vector<genplusgx::CoreCheatPatch> enabledPatches;
  const CheatConfiguration nameless{
    .entries = {{.name = "", .code = "BAAA-AAAA", .enabled = true}},
  };
  if (!check(validateCheatConfiguration(
               CheatSystem::genesis, configuration, &enabledPatches) &&
               enabledPatches.size() == 1U,
        "Cheat configuration did not select only enabled patches") ||
      !check(!validateCheatConfiguration(CheatSystem::genesis, nameless),
        "A nameless cheat definition was accepted")) {
    return 6;
  }
  CheatConfiguration excessive;
  for (std::size_t index = 0U; index < 76U; ++index) {
    excessive.entries.push_back({
      .name = "Two patches",
      .code = "BAAA-AAAA+CAAA-AAAA",
      .enabled = true,
    });
  }
  if (!check(validateCheatConfiguration(CheatSystem::genesis, excessive).error ==
               CheatError::tooManyPatches,
        "An aggregate patch count above the core limit was accepted")) {
    return 7;
  }

  const auto generatedGenesis = makeRamCheatCode(
    CheatSystem::genesis, 0x20U, 0xCAFEU);
  const auto generatedEightBit = makeRamCheatCode(
    CheatSystem::masterSystem, 0x123U, 0x7FU);
  if (!check(generatedGenesis.status &&
        generatedGenesis.normalizedCode == "FF0020:CAFE" &&
        generatedGenesis.patches.front().address == 0xFF0020U &&
        generatedGenesis.patches.front().data == 0xCAFEU,
      "Genesis RAM search result did not produce a typed cheat") ||
    !check(generatedEightBit.status &&
        generatedEightBit.normalizedCode == "C123:7F" &&
        generatedEightBit.patches.front().address == 0xFF0123U &&
        generatedEightBit.patches.front().width == genplusgx::CoreCheatWidth::byte,
      "8-bit RAM search result did not produce a typed cheat") ||
    !check(!makeRamCheatCode(CheatSystem::genesis, 1U, 1U).status &&
        !makeRamCheatCode(CheatSystem::genesis, 0x10000U, 1U).status &&
        !makeRamCheatCode(CheatSystem::masterSystem, 0x2000U, 1U).status &&
        !makeRamCheatCode(CheatSystem::masterSystem, 0U, 0x100U).status,
      "An unaligned or out-of-range RAM search result became a cheat")) {
    return 8;
  }

  constexpr std::string_view retroArchList = R"(
# Emulator-handled RetroArch entries. Other metadata is intentionally ignored.
cheats = "2"
cheat0_desc = "Infinite \"lives\""
cheat0_code = "BAAA-AAAA"
cheat0_enable = "true"
cheat0_handler = "1"
cheat1_desc = "Alternate mode"
cheat1_code = "123456:abcd"
cheat1_enable = "0"
)";
  const auto retroImported = parseCheatList(
    CheatSystem::genesis, retroArchList, CheatListFormat::retroArch);
  const auto bomRetroImported = parseCheatList(
    CheatSystem::genesis,
    std::string{"\xEF\xBB\xBF"} + std::string{retroArchList},
    CheatListFormat::autoDetect);
  const auto plainImported = parseCheatList(CheatSystem::masterSystem,
    "# local list\nInfinite energy | C000:7f\nC123:42\n",
    CheatListFormat::plainText);
  if (!check(retroImported.status &&
        retroImported.format == CheatListFormat::retroArch &&
        retroImported.configuration.entries.size() == 2U &&
        retroImported.configuration.entries.front().name == "Infinite \"lives\"" &&
        retroImported.configuration.entries.front().code == "BAAA-AAAA" &&
        !retroImported.configuration.entries.front().enabled,
      "A bounded RetroArch cheat list was not imported disabled") ||
    !check(bomRetroImported.status &&
        bomRetroImported.configuration == retroImported.configuration,
      "A UTF-8 BOM prevented RetroArch cheat-list import") ||
    !check(plainImported.status &&
        plainImported.format == CheatListFormat::plainText &&
        plainImported.configuration.entries.size() == 2U &&
        plainImported.configuration.entries[0].name == "Infinite energy" &&
        plainImported.configuration.entries[1].name == "Imported cheat 2" &&
        plainImported.configuration.entries[1].code == "C123:42" &&
        !plainImported.configuration.entries[1].enabled,
      "A bounded plain-text cheat list was not imported disabled")) {
    return 9;
  }

  std::string invalidUtf8{"Name | C000:7F\n"};
  invalidUtf8.push_back(static_cast<char>(0xC0));
  invalidUtf8.push_back(static_cast<char>(0x80));
  std::string embeddedNull{"Name | C000:7F"};
  embeddedNull.push_back('\0');
  embeddedNull += "ignored";
  const std::string oversizedImport(maximumCheatImportBytes + 1U, 'A');
  const std::string overlongLine(4U * 1024U + 1U, 'A');
  std::string tooManyPlainEntries;
  for (std::size_t index = 0U; index <= maximumCheatDefinitions; ++index) {
    tooManyPlainEntries += "C000:00\n";
  }
  const std::array invalidImports{
    parseCheatList(CheatSystem::genesis,
      "cheats = 1\ncheat0_desc = Direct\ncheat0_address = 16\n",
      CheatListFormat::retroArch),
    parseCheatList(CheatSystem::genesis,
      "cheats = 1\ncheat0_code = BAAA-AAAA\ncheat0_code = CAAA-AAAA\n",
      CheatListFormat::retroArch),
    parseCheatList(CheatSystem::genesis,
      "cheats = 151\n", CheatListFormat::retroArch),
    parseCheatList(CheatSystem::genesis,
      "cheats = 1\ncheat0_code = BAAA-AAAA\ncheat0_enable = maybe\n",
      CheatListFormat::retroArch),
    parseCheatList(CheatSystem::masterSystem,
      "Genesis code | BAAA-AAAR\n", CheatListFormat::plainText),
    parseCheatList(CheatSystem::masterSystem, invalidUtf8),
    parseCheatList(CheatSystem::masterSystem, embeddedNull),
    parseCheatList(CheatSystem::masterSystem, oversizedImport),
    parseCheatList(CheatSystem::masterSystem, overlongLine),
    parseCheatList(CheatSystem::masterSystem, tooManyPlainEntries),
  };
  for (std::size_t index = 0U; index < invalidImports.size(); ++index) {
    if (invalidImports[index].status ||
        !invalidImports[index].configuration.entries.empty()) {
      std::cerr << "Invalid imported list " << index
                << " produced partial cheats\n";
      return 10;
    }
  }

  for (std::size_t iteration = 0U; iteration < 2'000U; ++iteration) {
    const auto length = static_cast<std::size_t>(random() % 512U);
    std::string candidate;
    candidate.reserve(length);
    for (std::size_t index = 0U; index < length; ++index) {
      candidate.push_back(fuzzAlphabet[random() % fuzzAlphabet.size()]);
    }
    const auto imported = parseCheatList(
      CheatSystem::genesis, candidate, CheatListFormat::autoDetect);
    if (imported.status &&
        (!validateCheatConfiguration(
           CheatSystem::genesis, imported.configuration) ||
         std::ranges::any_of(imported.configuration.entries,
           [](const auto& entry) { return entry.enabled; }))) {
      std::cerr << "A fuzzed cheat list bypassed validation or safe-disable\n";
      return 11;
    }
  }

  QTemporaryDir temporary;
  if (!check(temporary.isValid(), "Temporary directory was unavailable")) {
    return 12;
  }
  const genplusgx::GameIdentity identity{
    .sha256 = std::string(64U, 'a'),
    .titleSlug = "Synthetic_Game",
  };
  const genplusgx::GameIdentity otherIdentity{
    .sha256 = std::string(64U, 'b'),
    .titleSlug = "Other_Game",
  };
  CheatStore store{temporaryPath(temporary) / "config" / "cheats"};
  const auto importedPath = temporaryPath(temporary) / "local.cht";
  if (!check(writeText(importedPath, retroArchList),
        "The temporary import fixture could not be written") ||
      !check(importCheatList(CheatSystem::genesis, importedPath).status &&
          importCheatList(CheatSystem::genesis, importedPath)
            .configuration.entries.size() == 2U &&
          !importCheatList(CheatSystem::genesis,
            temporaryPath(temporary) / "missing.cht").status &&
          !importCheatList(CheatSystem::genesis,
            std::filesystem::path{"relative.cht"}).status,
        "File-backed cheat import did not enforce absolute bounded files")) {
    return 13;
  }
  if (!check(store.pathFor(identity).filename() == identity.directoryName() + ".json",
        "Per-game cheat path was not collision resistant") ||
      !check(store.save(identity, CheatSystem::genesis, configuration),
        "Cheat configuration could not be saved")) {
    return 14;
  }
  const auto loaded = store.load(identity, CheatSystem::genesis);
  configuration.entries.front().code = "BAAA-AAAA";
  if (!check(loaded.status && loaded.configuration == configuration,
        "Cheat configuration did not normalize and round-trip") ||
      !check(
        !store.load(identity, CheatSystem::masterSystem).status &&
          store.load(otherIdentity, CheatSystem::genesis).status &&
          store.load(otherIdentity, CheatSystem::genesis).configuration.entries.empty(),
        "Wrong-system or missing-game cheat storage did not fail safely")) {
    return 15;
  }
  const CheatConfiguration invalidConfiguration{
    .entries = {{.name = "Broken", .code = "not-a-code", .enabled = true}},
  };
  if (
    !check(
      writeText(store.pathFor(identity), "{broken") &&
        !store.load(identity, CheatSystem::genesis).status &&
        writeText(store.pathFor(identity),
          R"({"schemaVersion":999,"gameSha256":"aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa","system":"genesis","entries":[]})") &&
        !store.load(identity, CheatSystem::genesis).status,
      "Corrupt or future cheat storage did not fail closed") ||
    !check(!store.save(identity, CheatSystem::genesis, invalidConfiguration),
      "Invalid cheat data was persisted")) {
    return 16;
  }
  return 0;
}
