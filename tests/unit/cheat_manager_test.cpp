#include "genplusgx/cheats/cheat_manager.h"

#include <QCoreApplication>
#include <QTemporaryDir>

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

  QTemporaryDir temporary;
  if (!check(temporary.isValid(), "Temporary directory was unavailable")) {
    return 8;
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
  if (!check(store.pathFor(identity).filename() == identity.directoryName() + ".json",
        "Per-game cheat path was not collision resistant") ||
      !check(store.save(identity, CheatSystem::genesis, configuration),
        "Cheat configuration could not be saved")) {
    return 9;
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
    return 10;
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
    return 11;
  }
  return 0;
}
