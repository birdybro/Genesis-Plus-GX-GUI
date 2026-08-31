#include "genplusgx/debug_analysis.h"

#include <array>
#include <cstdint>
#include <iostream>

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

  return 0;
}
