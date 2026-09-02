#include "genplusgx/debug_analysis.h"

#include <array>
#include <cstdint>
#include <iostream>
#include <random>
#include <string>

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
  using namespace genplusgx;
  const std::array<std::uint8_t, 8> bytes{
    0x12U, 0x34U, 0xFFU, 0x80U, 0x00U, 0x00U, 0x00U, 0x02U};
  std::uint32_t value = 0U;
  if (!check(debugReadValue(bytes, 0U, DebugValueWidth::word,
          DebugValueEndian::big, value) && value == 0x1234U,
        "Big-endian word decoding failed") ||
      !check(debugReadValue(bytes, 0U, DebugValueWidth::word,
          DebugValueEndian::little, value) && value == 0x3412U,
        "Little-endian word decoding failed") ||
      !check(debugReadValue(bytes, 4U, DebugValueWidth::longWord,
          DebugValueEndian::big, value) && value == 2U,
        "Big-endian long decoding failed") ||
      !check(!debugReadValue(bytes, 7U, DebugValueWidth::word,
          DebugValueEndian::big, value),
        "Out-of-range value decoding succeeded") ||
      !check(debugInterpretValue(
          0xFFU, DebugValueWidth::byte, DebugValueFormat::signedInteger) == -1,
        "Signed byte interpretation failed") ||
      !check(debugInterpretValue(
          0xFFFFU, DebugValueWidth::word, DebugValueFormat::signedInteger) == -1,
        "Signed word interpretation failed") ||
      !check(debugInterpretValue(0xFFFFFFFFU, DebugValueWidth::longWord,
          DebugValueFormat::signedInteger) == -1,
        "Signed long interpretation failed")) {
    return 1;
  }

  DebugRamSearch search;
  if (!check(search.begin(bytes, DebugValueWidth::byte, DebugValueEndian::big),
        "RAM search could not start") ||
      !check(search.active() && search.candidates().size() == bytes.size(),
        "RAM search initialized the wrong candidate set") ||
      !check(search.filter(bytes, DebugRamComparison::equalTo,
          DebugValueFormat::unsignedInteger, 0x80),
        "Exact filter failed") ||
      !check(search.candidates().size() == 1U &&
          search.candidates().front().offset == 3U,
        "Exact filter retained the wrong address")) {
    return 2;
  }

  auto changed = bytes;
  changed[3] = 0x81U;
  if (!check(search.filter(changed, DebugRamComparison::increased,
          DebugValueFormat::unsignedInteger),
        "Increased filter failed") ||
      !check(search.candidates().size() == 1U &&
          search.candidates().front().previousValue == 0x81U,
        "Increased filter did not update the retained baseline") ||
      !check(search.filter(changed, DebugRamComparison::unchanged,
          DebugValueFormat::unsignedInteger),
        "Unchanged filter failed") ||
      !check(search.candidates().size() == 1U,
        "Unchanged filter discarded a stable candidate")) {
    return 3;
  }

  DebugRamSearch signedSearch;
  if (!check(signedSearch.begin(
          bytes, DebugValueWidth::byte, DebugValueEndian::big),
        "Signed RAM search could not start") ||
      !check(signedSearch.filter(bytes, DebugRamComparison::lessThan,
          DebugValueFormat::signedInteger, 0),
        "Signed comparison failed") ||
      !check(signedSearch.candidates().size() == 2U,
        "Signed comparison retained the wrong values")) {
    return 4;
  }

  const std::array<std::uint8_t, 3> sequence{1U, 2U, 3U};
  auto modified = sequence;
  modified[0] = 2U;
  modified[2] = 1U;
  DebugRamSearch comparisons;
  if (!check(comparisons.begin(
          sequence, DebugValueWidth::byte, DebugValueEndian::big) &&
        comparisons.filter(sequence, DebugRamComparison::greaterThan,
          DebugValueFormat::unsignedInteger, 1) &&
        comparisons.candidates().size() == 2U,
      "Greater-than filter failed") ||
      !check(comparisons.begin(
          sequence, DebugValueWidth::byte, DebugValueEndian::big) &&
        comparisons.filter(sequence, DebugRamComparison::notEqualTo,
          DebugValueFormat::unsignedInteger, 2) &&
        comparisons.candidates().size() == 2U,
      "Not-equal filter failed") ||
      !check(comparisons.begin(
          sequence, DebugValueWidth::byte, DebugValueEndian::big) &&
        comparisons.filter(modified, DebugRamComparison::changed,
          DebugValueFormat::unsignedInteger) &&
        comparisons.candidates().size() == 2U,
      "Changed filter failed") ||
      !check(comparisons.begin(
          sequence, DebugValueWidth::byte, DebugValueEndian::big) &&
        comparisons.filter(modified, DebugRamComparison::decreased,
          DebugValueFormat::unsignedInteger) &&
        comparisons.candidates().size() == 1U &&
        comparisons.candidates().front().offset == 2U,
      "Decreased filter failed")) {
    return 5;
  }

  std::array<std::uint8_t, DebugRamSearch::maximumCandidates + 1U> oversized{};
  if (!check(!search.begin(
          oversized, DebugValueWidth::byte, DebugValueEndian::big),
        "Oversized candidate set was accepted") ||
      !check(!search.active() && search.candidates().empty(),
        "Rejected search retained stale candidates")) {
    return 6;
  }

  search.clear();
  if (!check(!search.active() &&
      !search.filter(bytes, DebugRamComparison::changed,
        DebugValueFormat::unsignedInteger),
      "Cleared search accepted a filter")) {
    return 7;
  }

  DebugSymbolTable symbols;
  std::string symbolError;
  constexpr std::string_view symbolText{
    "# canonical mixed-CPU symbol file\n"
    "m68k 000200 ResetEntry\n"
    "z80 $0038 IrqVector\n"
    "0x000250 ControllerPoll ; defaults to m68k\n"
    "m68k 000200 ResetEntry\n"};
  if (!check(symbols.load(symbolText, symbolError) && symbolError.empty(),
        "Valid mixed-CPU symbols were rejected") ||
      !check(symbols.symbols().size() == 3U,
        "Duplicate symbols were not normalized") ||
      !check(symbols.find(CoreDebugCpu::m68k, 0x200U) != nullptr &&
          symbols.find(CoreDebugCpu::m68k, 0x200U)->name == "ResetEntry" &&
          symbols.find(CoreDebugCpu::z80, 0x38U) != nullptr &&
          symbols.find(CoreDebugCpu::z80, 0x38U)->name == "IrqVector" &&
          symbols.find(CoreDebugCpu::z80, 0x200U) == nullptr,
        "Symbol lookup lost CPU/address identity")) {
    return 8;
  }

  const auto acceptedSymbols = symbols.symbols();
  if (!check(!symbols.load("z80 10000 OutsideAddress\n", symbolError) &&
          !symbolError.empty(),
        "Out-of-range Z80 symbol was accepted") ||
      !check(symbols.symbols() == acceptedSymbols,
        "Failed symbol import replaced the active table") ||
      !check(!symbols.load("m68k 000200 name with spaces\n", symbolError),
        "Ambiguous symbol record was accepted") ||
      !check(!symbols.load(std::string(
          DebugSymbolTable::maximumFileBytes + 1U, 'x'), symbolError),
        "Oversized symbol content was accepted")) {
    return 9;
  }

  std::mt19937 generator{0x47505844U};
  std::uniform_int_distribution<std::size_t> mutationCount{1U, 4U};
  for (std::size_t iteration = 0U; iteration < 512U; ++iteration) {
    if (!check(symbols.load(symbolText, symbolError),
          "The deterministic symbol baseline stopped parsing")) {
      return 10;
    }
    const auto beforeMutation = symbols.symbols();
    std::string mutated{symbolText};
    std::uniform_int_distribution<std::size_t> position{0U, mutated.size() - 1U};
    std::uniform_int_distribution<unsigned int> byte{0U, 255U};
    const auto count = mutationCount(generator);
    for (std::size_t mutation = 0U; mutation < count; ++mutation) {
      mutated[position(generator)] = static_cast<char>(byte(generator));
    }
    const auto accepted = symbols.load(mutated, symbolError);
    if (!check(accepted || symbols.symbols() == beforeMutation,
          "A rejected mutated symbol file changed the active table") ||
        !check(!accepted || symbols.symbols().size() <=
            DebugSymbolTable::maximumSymbols,
          "A mutated symbol file bypassed the record bound")) {
      return 10;
    }
  }
  if (!check(symbols.load(symbolText, symbolError),
        "The canonical symbol table could not be restored after fuzzing")) {
    return 10;
  }

  const std::array trace{
    CoreDebugTraceEntry{1U, CoreDebugCpu::m68k, 0x200U, 28U},
    CoreDebugTraceEntry{2U, CoreDebugCpu::z80, 0x38U, 13U},
  };
  const auto json = debugTraceJson(trace, symbols, 7U);
  if (!check(json.find("\"schema\": 1") != std::string::npos &&
          json.find("\"droppedEntries\": 7") != std::string::npos &&
          json.find("\"cpu\": \"m68k\"") != std::string::npos &&
          json.find("\"address\": \"0x200\"") != std::string::npos &&
          json.find("\"symbol\": \"ResetEntry\"") != std::string::npos &&
          json.find("\"symbol\": \"IrqVector\"") != std::string::npos,
        "Versioned trace JSON omitted identity, loss, or symbols")) {
    return 11;
  }

  return 0;
}
