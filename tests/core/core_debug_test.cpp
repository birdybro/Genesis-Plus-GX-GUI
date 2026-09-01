#include "genplusgx/core_adapter.h"
#include "genplusgx/emulation_worker.h"
#include "synthetic_rom.h"

#include <chrono>
#include <cstdint>
#include <filesystem>
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

std::optional<genplusgx::EmulationEvent> waitForBreakpoint(
  genplusgx::EmulationWorker& worker)
{
  const auto deadline = std::chrono::steady_clock::now() + 3s;
  while (std::chrono::steady_clock::now() < deadline) {
    auto event = worker.waitForEvent(100ms);
    if (event && event->type ==
        genplusgx::EmulationEventType::debugBreakpointHit) {
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
  constexpr std::uint8_t z80Marker = 0xA7U;
  const genplusgx::test::TemporaryFixture z80Fixture{
    genplusgx::test::makeZ80RamMarkerRom(z80Marker), ".sms"};
  constexpr std::uint8_t sgMarker = 0xB6U;
  const genplusgx::test::TemporaryFixture sgFixture{
    genplusgx::test::makeZ80RamMarkerRom(sgMarker), ".sg"};
  constexpr std::uint8_t ggMarker = 0xC5U;
  const genplusgx::test::TemporaryFixture ggFixture{
    genplusgx::test::makeZ80RamMarkerRom(ggMarker), ".gg"};
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
  capture.clientToken = 0xCAFEU;
  if (!check(adapter.debugRequest(capture, response), "Debug snapshot failed") ||
      !check(response.snapshot != nullptr, "Snapshot response has no payload") ||
      !check(response.clientToken == capture.clientToken &&
          response.snapshot->frameNumber == 1U &&
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
  genplusgx::CoreDebugRequest workerOwned;
  workerOwned.type = genplusgx::CoreDebugRequestType::setFrameBreakpoints;
  if (!check(adapter.debugRequest(invalidRead, response).error ==
          genplusgx::CoreError::invalidDebugRequest,
        "Oversized memory read was accepted") ||
      !check(adapter.debugRequest(invalidWrite, response).error ==
          genplusgx::CoreError::invalidDebugRequest,
        "Out-of-range memory write was accepted") ||
      !check(adapter.debugRequest(workerOwned, response).error ==
          genplusgx::CoreError::invalidDebugRequest,
        "Core adapter accepted worker-owned breakpoints")) {
    return 6;
  }

  genplusgx::CoreDebugRequest readZ80Ram;
  readZ80Ram.type = genplusgx::CoreDebugRequestType::readMemory;
  readZ80Ram.region = genplusgx::CoreDebugMemoryRegion::z80Ram;
  readZ80Ram.offset = 0U;
  readZ80Ram.size = 1U;
  genplusgx::CoreDebugRequest writeZ80Ram;
  writeZ80Ram.type = genplusgx::CoreDebugRequestType::writeMemory;
  writeZ80Ram.region = genplusgx::CoreDebugMemoryRegion::z80Ram;
  writeZ80Ram.offset = 1U;
  writeZ80Ram.bytes = {0x3CU};
  auto inactiveM68kRead = readZ80Ram;
  inactiveM68kRead.region = genplusgx::CoreDebugMemoryRegion::m68kRam;
  if (!check(adapter.unloadGame(), "Genesis fixture unload failed") ||
      !check(adapter.loadGame(z80Fixture.path()), "Z80 fixture load failed") ||
      !check(adapter.runFrame(true), "Z80 fixture execution failed") ||
      !check(adapter.debugRequest(capture, response),
        "Z80 debug snapshot failed") ||
      !check(response.snapshot && !response.snapshot->m68kActive &&
          response.snapshot->z80.programCounter == 0x0009U &&
          response.snapshot->z80Ram[0] == z80Marker &&
          response.snapshot->m68kRam[0] == 0U,
        "The active 8-bit system RAM or CPU identity was not captured") ||
      !check(adapter.debugRequest(inactiveM68kRead, response).error ==
          genplusgx::CoreError::invalidDebugRequest,
        "An inactive 68000 RAM region aliased the 8-bit system RAM") ||
      !check(adapter.debugRequest(readZ80Ram, response) &&
          response.bytes == std::vector<std::uint8_t>({z80Marker}),
        "The active 8-bit system RAM was not exposed through Z80 RAM") ||
      !check(adapter.debugRequest(writeZ80Ram, response),
        "The active 8-bit system RAM write failed") ||
      !check(adapter.debugRequest(capture, response) && response.snapshot &&
          response.snapshot->z80Ram[1] == 0x3CU,
        "The active 8-bit system RAM write was not reflected in a snapshot")) {
    return 7;
  }

  const auto verifyEightBitDebugMemory = [&adapter, &capture, &response](
    const std::filesystem::path& path, std::uint8_t marker) {
    return adapter.unloadGame() && adapter.loadGame(path) &&
      adapter.runFrame(true) && adapter.debugRequest(capture, response) &&
      response.snapshot && !response.snapshot->m68kActive &&
      response.snapshot->z80.programCounter == 0x0009U &&
      response.snapshot->z80Ram[0] == marker;
  };
  if (!check(verifyEightBitDebugMemory(sgFixture.path(), sgMarker),
        "SG-1000 debug memory did not use its active Z80 work RAM") ||
      !check(verifyEightBitDebugMemory(ggFixture.path(), ggMarker),
        "Game Gear debug memory did not use its active Z80 work RAM")) {
    return 8;
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
    return 9;
  }

  genplusgx::EmulationWorker worker;
  genplusgx::EmulationEvent event;
  auto oversizedBreakpoints = workerOwned;
  oversizedBreakpoints.breakpoints.resize(
    genplusgx::maximumCoreDebugBreakpoints + 1U);
  if (!check(worker.start(), "Worker start failed") ||
      !check(!worker.submit(genplusgx::EmulationCommand::debug(
          9U, oversizedBreakpoints)) &&
          worker.metrics().commandQueueDepth == 0U,
        "Worker queued an oversized breakpoint payload") ||
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
      !check(submitAndWait(worker,
          genplusgx::EmulationCommand::simple(
            genplusgx::EmulationCommandType::pause, 15U), event) &&
          event.succeeded(),
        "Worker did not pause before breakpoint configuration")) {
    return 10;
  }

  genplusgx::CoreDebugRequest breakpoints;
  breakpoints.type = genplusgx::CoreDebugRequestType::setFrameBreakpoints;
  breakpoints.clientToken = 0xBEEFU;
  breakpoints.breakpoints = {
    {genplusgx::CoreDebugCpu::m68k, 0x250U},
    {genplusgx::CoreDebugCpu::m68k, 0x256U},
    {genplusgx::CoreDebugCpu::m68k, 0x25AU},
  };
  if (!check(submitAndWait(worker,
          genplusgx::EmulationCommand::debug(16U, breakpoints), event) &&
          event.succeeded() &&
          event.type == genplusgx::EmulationEventType::debugResponse &&
          event.debug.clientToken == breakpoints.clientToken,
        "Frame breakpoint configuration failed") ||
      !check(submitAndWait(worker,
          genplusgx::EmulationCommand::simple(
            genplusgx::EmulationCommandType::resume, 17U), event) &&
          event.succeeded(),
        "Worker did not resume for breakpoint test")) {
    return 11;
  }
  auto breakpoint = waitForBreakpoint(worker);
  if (!check(breakpoint.has_value() && breakpoint->debug.breakpointHit.has_value() &&
          breakpoint->debug.clientToken == breakpoints.clientToken,
        "Frame breakpoint did not pause the generated ROM") ||
      !check(breakpoint->workerState ==
          genplusgx::EmulationWorkerState::paused &&
          breakpoint->debug.breakpointHit->cpu ==
            genplusgx::CoreDebugCpu::m68k,
        "Breakpoint hit did not report a paused 68K address") ||
      !check(worker.state() == genplusgx::EmulationWorkerState::paused,
        "Worker continued after a breakpoint hit") ||
      !check(worker.stop(), "Worker shutdown failed")) {
    return 12;
  }

  const auto regions = genplusgx::coreDebugMemoryRegions(0x1234U);
  return check(regions.front().size == 0x1234U &&
        regions.back().size == 0x20U &&
        genplusgx::coreDebugCramColor(0x01FFU) == 0xFFFFFFFFU,
      "Debug region metadata or CRAM color conversion is incorrect")
    ? 0
    : 13;
}
