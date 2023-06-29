#pragma once

#ifndef DATASPREE_INFERENCE_CORE_EXCEPTION_HPP
#define DATASPREE_INFERENCE_CORE_EXCEPTION_HPP

#include <fmt/core.h>

#include <exception>
#include <string>
#include <vector>

namespace dataspree::inference::core {


// XXX: noexcept copy constructor

/// Exception thrown when attempting to access content in item that does not exist.
// NOLINTNEXTLINE(altera-struct-pack-align)
struct [[nodiscard]] InvalidContent final : public std::exception {

  /// Utility constructor to construct a reason from multiple arguments.
  template <typename... T>
  explicit inline InvalidContent(T &&...args) noexcept : InvalidContent(fmt::format("{}", args...)) {}

  explicit inline InvalidContent(std::string &&reason) noexcept : reason(std::move(reason)) {}

  [[nodiscard]] auto what() const noexcept -> char const * final;

  /// Add argument to call stack.
  template <typename T> inline void add(T &&name) noexcept {
    if constexpr (std::is_same_v<std::decay_t<T>, std::string> || std::is_same_v<std::decay_t<T>, char const *>) {
      this->names.push_back(fmt::format("\"{}\"", name));
    } else {
      this->names.push_back(std::to_string(name));
    }
  }

private:
  /// Problem that caused the InvalidContent error.
  std::string reason;


  /// Formatted reason for the exception that shows the call stack.
  mutable std::string why{};

  /// Call stack.
  std::vector<std::string> names{};

};

} // namespace dataspree::inference::core

#endif // DATASPREE_INFERENCE_CORE_EXCEPTION_HPP