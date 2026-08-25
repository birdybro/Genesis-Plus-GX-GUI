#include "genplusgx/core_adapter.h"
#include "genplusgx/persistence.h"
#include "genplusgx/state_manager.h"
#include "synthetic_rom.h"

#include <QTemporaryDir>

#include <chrono>
#include <cstdint>
#include <filesystem>
#include <iostream>
#include <string>
#include <vector>

extern "C" {
#include "shared.h"
}

namespace {

bool check(bool condition, const char* message)
{
  if (!condition) {
    std::cerr << message << '\n';
  }
  return condition;
}

} // namespace

int main()
{
  const genplusgx::test::TemporaryFixture fixture{
    genplusgx::test::makeGenesisRamMarkerRom(), ".bin"};
  const auto identityResult = genplusgx::identifyGame(fixture.path(), "Core State Test");
  QTemporaryDir stateRoot;
  if (!check(identityResult.status && stateRoot.isValid(),
        "Core state test fixtures could not be initialized")) {
    return 1;
  }

  genplusgx::SaveStateManager manager{genplusgx::ApplicationPaths{
    std::filesystem::path{stateRoot.path().toStdString()} / "states"}};
  genplusgx::CoreAdapter adapter;
  if (!check(manager.initialize(), "Core state manager initialization failed") ||
      !check(adapter.initialize(), "Core state adapter initialization failed") ||
      !check(adapter.loadGame(fixture.path()), "Core state fixture load failed") ||
      !check(adapter.runFrame(false), "Core state fixture frame failed")) {
    return 2;
  }

  work_ram[0x100] = 0x5AU;
  m68k_set_reg(M68K_REG_D3, 0x1234ABCDU);
  const auto savedProgramCounter = m68k_get_reg(M68K_REG_PC);
  std::vector<std::uint8_t> rawState;
  if (!check(adapter.saveRawState(rawState), "Raw core state save failed") ||
      !check(rawState.size() > 16U && rawState.size() <
          genplusgx::SaveStateManager::maximumPayloadBytes,
        "Raw core state size was invalid")) {
    return 3;
  }

  const auto fixedTimestamp = std::chrono::system_clock::time_point{
    std::chrono::milliseconds{1'750'000'000'000LL}};
  if (!check(manager.saveSlot(
          identityResult.identity, 3U, adapter.hardware(), adapter.frameCount(),
          rawState, fixedTimestamp),
        "Wrapped core state save failed")) {
    return 4;
  }

  work_ram[0x100] = 0xA5U;
  m68k_set_reg(M68K_REG_D3, 0U);
  if (!check(adapter.runFrame(false), "Post-state mutation frame failed")) {
    return 5;
  }
  const auto loaded = manager.loadSlot(identityResult.identity, 3U, adapter.hardware());
  if (!check(loaded.status && loaded.rawPayload == rawState,
        "Wrapped manager changed the raw core payload") ||
      !check(loaded.metadata.slot == 3U && loaded.metadata.emulatedFrameNumber == 1U &&
          loaded.metadata.timestamp == fixedTimestamp,
        "Wrapped core state metadata was incorrect") ||
      !check(adapter.loadRawState(loaded.rawPayload), "Raw core state restore failed") ||
      !check(work_ram[0x100] == 0x5AU &&
          m68k_get_reg(M68K_REG_D3) == 0x1234ABCDU &&
          m68k_get_reg(M68K_REG_PC) == savedProgramCounter,
        "Raw core state did not restore semantic machine state") ||
      !check(adapter.frameCount() == 0U,
        "Frontend frame generation was not reset after state load")) {
    return 6;
  }

  auto truncated = rawState;
  truncated.pop_back();
  work_ram[0x100] = 0x44U;
  if (!check(adapter.loadRawState(truncated).error ==
          genplusgx::CoreError::invalidStatePayload,
        "Truncated raw state reached the unsafe core loader") ||
      !check(work_ram[0x100] == 0x44U,
        "Rejected raw state mutated machine state")) {
    return 7;
  }

  auto invalidSignature = rawState;
  invalidSignature.front() ^= 0x01U;
  if (!check(adapter.loadRawState(invalidSignature).error ==
          genplusgx::CoreError::stateLoadFailed,
        "Core-signature corruption was accepted") ||
      !check(work_ram[0x100] == 0x44U,
        "A core-rejected state was not rolled back transactionally")) {
    return 8;
  }

  std::vector<std::uint8_t> firstContinuation;
  std::vector<std::uint8_t> secondContinuation;
  if (!check(adapter.loadRawState(rawState), "First deterministic restore failed") ||
      !check(adapter.runFrame(false), "First deterministic continuation frame failed") ||
      !check(adapter.saveRawState(firstContinuation), "First continuation save failed") ||
      !check(adapter.loadRawState(rawState), "Second deterministic restore failed") ||
      !check(adapter.runFrame(false), "Second deterministic continuation frame failed") ||
      !check(adapter.saveRawState(secondContinuation), "Second continuation save failed") ||
      !check(firstContinuation == secondContinuation,
        "Restored core state did not continue deterministically")) {
    return 9;
  }

  const genplusgx::GameIdentity wrongIdentity{
    .sha256 = std::string(64U, 'f'),
    .titleSlug = "wrong-game",
  };
  if (!check(manager.loadStateFile(
          manager.statePath(identityResult.identity, 3U), wrongIdentity,
          adapter.hardware()).status.error == genplusgx::SaveStateError::wrongGame,
        "Wrong-game state was not rejected before core access")) {
    return 10;
  }

  return adapter.shutdown() ? 0 : 11;
}
