#include "genplusgx/bounded_queue.h"

#include <iostream>
#include <stdexcept>
#include <string>

namespace {

bool check(bool condition, const char* message)
{
  if (!condition) {
    std::cerr << message << '\n';
  }
  return condition;
}

struct Value final {
  int kind{0};
  int payload{0};
};

} // namespace

int main()
{
  bool rejectedZeroCapacity = false;
  try {
    const genplusgx::BoundedQueue<int> invalid{0U};
    static_cast<void>(invalid);
  } catch (const std::invalid_argument&) {
    rejectedZeroCapacity = true;
  }
  if (!check(rejectedZeroCapacity, "A zero-capacity bounded queue was accepted")) {
    return 1;
  }

  genplusgx::BoundedQueue<Value> queue{3U};
  if (!check(queue.capacity() == 3U && queue.empty(), "Initial queue state was invalid") ||
      !check(queue.tryPush({1, 10}) && queue.tryPush({2, 20}) && queue.tryPush({1, 30}),
        "Queue rejected values below its fixed capacity") ||
      !check(queue.full() && !queue.tryPush({3, 40}),
        "Queue grew beyond its fixed capacity")) {
    return 2;
  }

  if (!check(queue.replaceNewestMatching(
          [](const Value& value) { return value.kind == 1; }, Value{1, 99}),
        "Newest-match replacement failed") ||
      !check(queue.size() == 3U, "Replacement changed queue depth")) {
    return 3;
  }

  const auto first = queue.pop();
  const auto second = queue.pop();
  const auto third = queue.pop();
  if (!check(first && first->payload == 10, "FIFO order changed before replacement") ||
      !check(second && second->payload == 20, "FIFO middle element changed") ||
      !check(third && third->payload == 99, "Newest matching element was not replaced") ||
      !check(queue.empty() && !queue.pop(), "Empty queue pop returned a value")) {
    return 4;
  }

  if (!check(queue.tryPush({1, 1}) && queue.tryPush({1, 2}) && queue.tryPush({1, 3}),
        "Queue refill failed")) {
    return 5;
  }
  const auto dropped = queue.dropOldestAndPush({1, 4});
  const auto afterDrop = queue.pop();
  if (!check(dropped && dropped->payload == 1, "Overflow did not report the oldest value") ||
      !check(queue.size() == 2U && afterDrop && afterDrop->payload == 2,
        "Drop-and-push violated its bounded FIFO contract")) {
    return 6;
  }

  queue.clear();
  if (!check(queue.empty(), "Queue clear retained values")) {
    return 7;
  }

  genplusgx::BoundedQueue<std::string> strings{2U};
  std::string replacement{"firmware/path.bin"};
  if (!check(strings.tryPush("unrelated") &&
        !strings.replaceNewestMatching(
          [](const std::string& value) { return value == "missing"; },
          std::move(replacement)) &&
        replacement == "firmware/path.bin",
      "A failed coalescing search consumed its path-bearing replacement")) {
    return 8;
  }
  return 0;
}
