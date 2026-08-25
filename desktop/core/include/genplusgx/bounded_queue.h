#pragma once

#include <cstddef>
#include <deque>
#include <optional>
#include <stdexcept>
#include <utility>

namespace genplusgx {

template<typename Value>
class BoundedQueue final {
public:
  explicit BoundedQueue(std::size_t capacity)
    : capacity_(capacity)
  {
    if (capacity == 0U) {
      throw std::invalid_argument{"A bounded queue requires nonzero capacity."};
    }
  }

  [[nodiscard]] std::size_t capacity() const noexcept { return capacity_; }
  [[nodiscard]] std::size_t size() const noexcept { return values_.size(); }
  [[nodiscard]] bool empty() const noexcept { return values_.empty(); }
  [[nodiscard]] bool full() const noexcept { return values_.size() == capacity_; }

  [[nodiscard]] bool tryPush(Value value)
  {
    if (full()) {
      return false;
    }
    values_.push_back(std::move(value));
    return true;
  }

  template<typename Predicate>
  [[nodiscard]] bool replaceNewestMatching(Predicate predicate, Value&& replacement)
  {
    for (auto iterator = values_.rbegin(); iterator != values_.rend(); ++iterator) {
      if (predicate(*iterator)) {
        *iterator = std::move(replacement);
        return true;
      }
    }
    return false;
  }

  [[nodiscard]] std::optional<Value> pop()
  {
    if (empty()) {
      return std::nullopt;
    }
    Value result = std::move(values_.front());
    values_.pop_front();
    return result;
  }

  [[nodiscard]] std::optional<Value> dropOldestAndPush(Value value)
  {
    std::optional<Value> dropped;
    if (full()) {
      dropped = std::move(values_.front());
      values_.pop_front();
    }
    values_.push_back(std::move(value));
    return dropped;
  }

  void clear() noexcept { values_.clear(); }

private:
  std::size_t capacity_;
  std::deque<Value> values_;
};

} // namespace genplusgx
