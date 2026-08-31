#include "genplusgx/core_adapter.h"
#include "genplusgx/emulation_worker.h"
#include "synthetic_rom.h"

#include <chrono>
#include <cstdint>
#include <iostream>
#include <optional>
#include <thread>
#include <vector>

namespace {

using namespace std::chrono_literals;

bool check(bool condition, const char* message)
{
  if (!condition) {
    std::cerr << message << '\n';
  }
  return condition;
}

std::optional<genplusgx::EmulationEvent> waitForOperation(
  genplusgx::EmulationWorker& worker,
  std::uint64_t operationId)
{
  const auto deadline = std::chrono::steady_clock::now() + 3s;
  while (std::chrono::steady_clock::now() < deadline) {
    auto event = worker.waitForEvent(100ms);
    if (event && event->operationId == operationId) {
      return event;
    }
  }
  return std::nullopt;
}

bool submitAndWait(
  genplusgx::EmulationWorker& worker,
  genplusgx::EmulationCommand command,
  genplusgx::EmulationEvent& event)
{
  const auto operationId = command.operationId;
  if (!worker.submit(std::move(command))) {
    return false;
  }
  auto result = waitForOperation(worker, operationId);
  if (!result) {
    return false;
  }
  event = std::move(*result);
  return true;
}

} // namespace

int main()
{
  const genplusgx::test::TemporaryFixture fixture{
    genplusgx::test::makeGenesisRamMarkerRom(), ".bin"};
  genplusgx::CoreAdapter adapter;
  genplusgx::CoreDebugResponse response;

  if (!check(adapter.initialize(), "Core initialization failed") ||
      !check(adapter.debugRequest({}, response).error ==
          genplusgx::CoreError::noGameLoaded,
        "Debug capture without a game did not fail closed") ||
      !check(adapter.loadGame(fixture.path()), "Synthetic ROM load failed")) {
    return 1;
  }

  genplusgx::CoreDebugRequest readRom;
  readRom.type = genplusgx::CoreDebugRequestType::readMemory;
  readRom.region = genplusgx::CoreDebugMemoryRegion::rom;
  readRom.offset = 0U;
  readRom.size = 8U;
  if (!check(adapter.debugRequest(readRom, response),
        "Logical ROM read failed") ||
      !check(response.bytes == std::vector<std::uint8_t>(
          {0x00U, 0xFFU, 0xFFU, 0x00U, 0x00U, 0x00U, 0x02U, 0x00U}),
        "ROM bytes were not exposed in emulated big-endian order") ||
      !check(adapter.runFrame(true), "Synthetic frame execution failed")) {
    return 2;
  }

  genplusgx::CoreDebugRequest capture;
  if (!check(adapter.debugRequest(capture, response), "Debug snapshot failed") ||
      !check(response.snapshot != nullptr, "Snapshot response has no payload") ||
      !check(response.snapshot->frameNumber == 1U &&
          response.snapshot->romSize == 64U * 1024U &&
          response.snapshot->m68kActive,
        "Snapshot identity is incorrect") ||
      !check(response.snapshot->m68kRam[0] == 0x13U &&
          response.snapshot->m68kRam[1] == 0x57U &&
          response.snapshot->m68kRam[2] == 0x9BU &&
          response.snapshot->m68kRam[3] == 0xDFU &&
          response.snapshot->m68kRam[4] == 0xCAU &&
          response.snapshot->m68kRam[5] == 0xFEU &&
          response.snapshot->m68kRam[6] == 0x42U,
        "68K RAM snapshot lost logical byte order") ||
      !check(response.snapshot->vdp.registers[1] == 0x74U &&
          response.snapshot->vdp.registers[12] == 0x81U &&
          response.snapshot->vdp.cram[0] != 0U,
        "VDP state did not reflect the generated ROM") ||
      !check(response.snapshot->sound.psgRegisters[0] == 0x0FE,
        "PSG register state did not reflect the generated ROM")) {
    return 3;
  }

  const auto first = response.snapshot;
  genplusgx::CoreDebugRequest writeRam;
  writeRam.type = genplusgx::CoreDebugRequestType::writeMemory;
  writeRam.region = genplusgx::CoreDebugMemoryRegion::m68kRam;
  writeRam.offset = 0x20U;
  writeRam.bytes = {0x12U, 0x34U, 0x56U, 0x78U};
  if (!check(adapter.debugRequest(writeRam, response),
        "Bounded debug memory write failed") ||
      !check(adapter.debugRequest(capture, response),
        "Snapshot after memory write failed") ||
      !check(response.snapshot->m68kRam[0x20U] == 0x12U &&
          response.snapshot->m68kRam[0x21U] == 0x34U &&
          response.snapshot->m68kRam[0x22U] == 0x56U &&
          response.snapshot->m68kRam[0x23U] == 0x78U,
        "Debug memory write did not preserve logical byte order") ||
      !check(first->m68kRam[0x20U] != 0x12U,
        "Published debug snapshots were mutable aliases")) {
    return 4;
  }

  auto registers = response.snapshot->m68k;
  registers.data[7] = 0xA5A55A5AU;
  genplusgx::CoreDebugRequest setRegisters;
  setRegisters.type = genplusgx::CoreDebugRequestType::setM68kRegisters;
  setRegisters.m68k = registers;
  genplusgx::CoreDebugRequest setVdp;
  setVdp.type = genplusgx::CoreDebugRequestType::setVdpRegister;
  setVdp.vdpRegister = 7U;
  setVdp.vdpValue = 0x2AU;
  if (!check(adapter.debugRequest(setRegisters, response),
        "68K register edit failed") ||
      !check(adapter.debugRequest(setVdp, response), "VDP register edit failed") ||
      !check(adapter.debugRequest(capture, response),
        "Snapshot after register edits failed") ||
      !check(response.snapshot->m68k.data[7] == 0xA5A55A5AU &&
          response.snapshot->vdp.registers[7] == 0x2AU,
        "Register edits were not visible in the next snapshot")) {
    return 5;
  }

  auto invalidRead = readRom;
  invalidRead.size = static_cast<std::uint32_t>(
    genplusgx::CoreAdapter::maximumDebugTransferBytes + 1U);
  auto invalidWrite = writeRam;
  invalidWrite.offset = 0xFFFFU;
  if (!check(adapter.debugRequest(invalidRead, response).error ==
          genplusgx::CoreError::invalidDebugRequest,
        "Oversized memory read was accepted") ||
      !check(adapter.debugRequest(invalidWrite, response).error ==
          genplusgx::CoreError::invalidDebugRequest,
        "Out-of-range memory write was accepted")) {
    return 6;
  }

  genplusgx::CoreResult wrongThreadResult;
  std::thread wrongThread{[&] {
    genplusgx::CoreDebugResponse crossThreadResponse;
    wrongThreadResult = adapter.debugRequest(capture, crossThreadResponse);
  }};
  wrongThread.join();
  if (!check(wrongThreadResult.error == genplusgx::CoreError::wrongThread,
        "A foreign thread read core debug globals") ||
      !check(adapter.shutdown(), "Core shutdown failed")) {
    return 7;
  }

  genplusgx::EmulationWorker worker;
  genplusgx::EmulationEvent event;
  if (!check(worker.start(), "Worker start failed") ||
      !check(submitAndWait(worker,
          genplusgx::EmulationCommand::load(10U, fixture.path()), event) &&
          event.succeeded(),
        "Worker fixture load failed") ||
      !check(submitAndWait(worker,
          genplusgx::EmulationCommand::debug(11U, capture), event) &&
          event.succeeded() &&
          event.type == genplusgx::EmulationEventType::debugResponse &&
          event.debug.snapshot != nullptr,
        "Worker did not return a paused debug snapshot") ||
      !check(submitAndWait(worker,
          genplusgx::EmulationCommand::simple(
            genplusgx::EmulationCommandType::start, 12U), event) &&
          event.succeeded(),
        "Worker emulation start failed") ||
      !check(submitAndWait(worker,
          genplusgx::EmulationCommand::debug(13U, writeRam), event) &&
          !event.succeeded() &&
          event.error == genplusgx::EmulationWorkerError::invalidTransition,
        "A debug write raced running emulation") ||
      !check(submitAndWait(worker,
          genplusgx::EmulationCommand::debug(14U, capture), event) &&
          event.succeeded() && event.debug.snapshot != nullptr,
        "Running snapshot request was not serialized between frames") ||
      !check(worker.stop(), "Worker shutdown failed")) {
    return 8;
  }

  const auto regions = genplusgx::coreDebugMemoryRegions(0x1234U);
  return check(regions.front().size == 0x1234U &&
        regions.back().size == 0x20U &&
        genplusgx::coreDebugCramColor(0x01FFU) == 0xFFFFFFFFU,
      "Debug region metadata or CRAM color conversion is incorrect")
    ? 0
    : 9;
}
