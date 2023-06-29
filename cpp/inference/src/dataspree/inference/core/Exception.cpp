
#include <dataspree/inference/core/Exception.hpp>

#include <fmt/core.h>
#include <fmt/ranges.h>

#include <algorithm>

auto dataspree::inference::core::InvalidContent::what() const noexcept -> char const * {
  if (names.empty()) {
    this->why = fmt::format("Error accessing Item {}", reason);
  } else {
    auto names_reversed = this->names;
    std::reverse(names_reversed.begin(), names_reversed.end());

    this->why = fmt::format("Error accessing Item[{}] {}", fmt::join(names_reversed, ", "), reason);
  }
  return this->why.data();
}
