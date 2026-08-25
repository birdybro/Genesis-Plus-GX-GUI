#pragma once

#include "genplusgx/input_snapshot.h"

#include <functional>

namespace genplusgx::input {

class InputAggregator final {
public:
  using SnapshotSink = std::function<void(const InputSnapshot&)>;

  void setSnapshotSink(SnapshotSink sink);

  [[nodiscard]] bool updateKeyboard(const InputSnapshot& snapshot);
  [[nodiscard]] bool updateControllers(const InputSnapshot& snapshot);
  void clear();

  [[nodiscard]] InputSnapshot snapshot() const noexcept;

private:
  [[nodiscard]] bool updateSource(
    InputSnapshot& destination,
    std::uint64_t& acceptedSequence,
    const InputSnapshot& source);
  void rebuildSnapshot();

  SnapshotSink sink_;
  InputSnapshot keyboard_;
  InputSnapshot controllers_;
  InputSnapshot snapshot_;
  std::uint64_t keyboardSequence_{0U};
  std::uint64_t controllerSequence_{0U};
};

} // namespace genplusgx::input
