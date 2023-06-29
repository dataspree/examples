#ifndef DATASPREE_INFERENCE_CORE_ITEM_HPP
#define DATASPREE_INFERENCE_CORE_ITEM_HPP

#include <dataspree/inference/core/Exception.hpp>
#include <dataspree/inference/core/Utils.hpp>
#include <dataspree/inference/core/Type.hpp>

#include <opencv2/core/mat.hpp>

#include <any>
#include <functional>
#include <iomanip>
#include <iostream>
#include <limits>
#include <map>
#include <memory>
#include <ranges>
#include <sstream>
#include <type_traits>
#include <typeindex>
#include <typeinfo>
#include <unordered_map>
#include <vector>

namespace dataspree::inference::core {

struct Item final {

  [[nodiscard]] inline Item() noexcept : content(std::map<std::string, Item>{}), contentType(ItemType::OBJECT) {}

  template<typename T>
  [[nodiscard]] explicit inline Item(T content) noexcept : content(content), contentType(deriveItemType<T>()) {}

  [[nodiscard]] explicit inline Item(std::map<std::string, Item> &&map) noexcept
    : content(std::forward<std::map<std::string, Item>>(map)), contentType(ItemType::OBJECT) {}

  template<typename T>
  [[nodiscard]] explicit inline Item(std::map<std::string, T> map) noexcept : contentType(ItemType::OBJECT) {
    auto item_map = std::map<std::string, Item>{};

#pragma unroll 10
    for (auto &&[key, value] : map) { item_map[key] = Item(value); }
    this->content = item_map;
  }

  template<typename T>
  [[nodiscard]] explicit inline Item(std::vector<T> &&vec) noexcept : contentType(ItemType::ARRAY) {
    if constexpr (std::is_same_v<std::decay_t<T>, Item>) {
      this->content = std::forward<std::vector<T>>(vec);

    } else if constexpr (std::is_same_v<std::decay_t<T>, unsigned char>) {
      this->content = std::forward<std::vector<unsigned char>>(vec);
      this->contentType = ItemType::BYTE_ARRAY;

    } else {
      auto out_vec = std::vector<Item>{};
      // std::copy(vec.begin(), vec.end(), std::back_inserter(out_vec)); <- not possible
      for (auto &&val : vec) { out_vec.emplace_back(val); }
      this->content = out_vec;
    }
  }

  template<typename T>
  [[nodiscard]] explicit inline Item(std::vector<T> const &vec) noexcept : contentType(ItemType::ARRAY) {
    if constexpr (std::is_same_v<std::decay_t<T>, Item>) {
      this->content = std::forward<std::vector<T>>(vec);

    } else if constexpr (std::is_same_v<std::decay_t<T>, unsigned char>) {
      this->content = std::forward<std::vector<unsigned char>>(vec);
      this->contentType = ItemType::BYTE_ARRAY;

    } else {
      auto out_vec = std::vector<Item>{};
#pragma unroll 10
      for (auto &&val : vec) { out_vec.emplace_back(val); }
      this->content = out_vec;
    }
  }

  //[[nodiscard]] inline Item(Item &&item) noexcept : content(std::move(item.content)), contentType(item.contentType) {}
  [[nodiscard]] inline Item(Item &&item) noexcept = default;

  //[[nodiscard]] inline Item(Item const &item) noexcept : content(item.content), contentType(item.contentType) {}
  [[nodiscard]] inline Item(Item const &item) noexcept = default;

  //[[nodiscard]] inline Item(Item &item) noexcept : content(item.content), contentType(item.contentType) {}
  [[nodiscard]] inline Item(Item &item) noexcept = default;

  inline auto operator=(Item const &other) noexcept -> Item & {
    this->content = other.content;
    this->contentType = other.contentType;
    return *this;
  }

  inline auto operator=(Item &&other) noexcept -> Item & {
    this->content = std::move(other.content);
    this->contentType = other.contentType;
    return *this;
  }

  template<typename T, typename = std::enable_if_t<!std::is_same_v<std::decay_t<T>, Item>>>
  inline auto operator=(T &&other) noexcept -> Item & {
    this->content = std::forward<T>(other);
    this->contentType = deriveItemType<T>();
    return *this;
  }

  template<typename T, std::enable_if_t<!std::is_same_v<std::decay_t<T>, Item>>>
  inline auto operator=(T const &other) noexcept -> Item & {
    this->content = other;
    this->contentType = deriveItemType<T>();
    return *this;
  }

  template<typename T, std::enable_if_t<!std::is_same_v<std::decay_t<T>, Item>>>
  inline auto operator=(T &&other) noexcept -> Item & {
    this->content = other;
    this->contentType = deriveItemType<T>();
    return *this;
  }

  template<typename T = Item, typename... Ts> [[nodiscard]] inline auto find_at(Ts... args) noexcept -> T * {
    return this->template _value<std::decay_t<T>, true>(std::forward<Ts>(args)...);
  }

  template<typename T = Item, typename... Ts>
  [[nodiscard]] inline auto find_at(Ts... args) const noexcept -> T const * {
    return this->template _value<std::decay_t<T>, true>(std::forward<Ts>(args)...);
  }

  template<typename T> [[nodiscard]] inline auto as() -> T & {
    return *(this->template _value<std::decay_t<T>, false>());
  }

  template<typename T> [[nodiscard]] inline auto as() const -> T const & {
    return *(this->template _value<std::decay_t<T>, false>());
  }

  template<typename T = Item, typename... Ts> [[nodiscard]] inline auto at(Ts... args) -> T & {
    return *(this->template _value<std::decay_t<T>, false>(std::forward<Ts>(args)...));
  }

  template<typename T = Item, typename... Ts> [[nodiscard]] inline auto at(Ts... args) const -> T const & {
    return *(this->template _value<std::decay_t<T>, false>(std::forward<Ts>(args)...));
  }

  [[nodiscard]] inline auto operator[](std::string &&value) -> Item & { return this->at(std::move(value)); }

  [[nodiscard]] inline auto operator[](std::string const &value) -> Item & { return this->at(value); }

  [[nodiscard]] auto begin() const { return (this)->template _value<std::vector<Item>>()->begin(); }

  [[nodiscard]] auto end() const { return this->template _value<std::vector<Item>>()->end(); }

  [[nodiscard]] auto begin() { return (this)->template _value<std::vector<Item>>()->begin(); }

  [[nodiscard]] auto end() { return this->template _value<std::vector<Item>>()->end(); }

  [[nodiscard]] auto size() const { return this->template _value<std::vector<Item>>()->size(); }

  template<typename... Ts> auto emplace_back(Ts &&...args) -> void {
    this->template _value<std::vector<Item>>()->emplace_back(std::forward<Ts>(args)...);
  }

  [[nodiscard]] auto items() const -> std::map<std::string, Item> const & {
    return this->at<std::map<std::string, Item>>();
  }

  [[nodiscard]] auto items() -> std::map<std::string, Item> & { return this->at<std::map<std::string, Item>>(); }

  [[nodiscard]] auto contains(auto &&val) const noexcept -> bool { return this->items().contains(val); }

  template<typename T> [[nodiscard]] inline auto erase(T &&val) { this->at<std::map<std::string, Item>>().erase(val); }

  /// Return the stored content type.
  [[nodiscard]] auto getType() const noexcept { return this->contentType; }

  /// Return the underlying content type.
  [[nodiscard]] inline auto type() const noexcept { return std::type_index(content.type()); }

  template<typename T> inline auto operator==(T const &other) const -> bool {
    if constexpr (std::is_same_v<T, Item>) {
      if (contentType != other.contentType) { return false; }
    }

    switch (contentType) {
    case ItemType::OBJECT: {

      if constexpr (!is_map_v<T>) {
        return false;

      } else {
        auto const map = this->items();

        if (auto const mapSize = map.size(); mapSize != other.size()) {
          return false;
        }

#pragma unroll 10
        for (auto const &[key, value] : map) {
          if (!other.contains(key) || other.at(key) != value) { return false; }
        }
      }
    }

    case ItemType::ARRAY:
      if constexpr (!is_vector_v<T>) {
        return false;

      } else {

        if (auto const vectorSize = this->size(); vectorSize != other.size()) {
          return false;
        } else {
#pragma unroll 10
          for (std::size_t i{ 0 }; i < vectorSize; ++i) {
            if (this->at(i) != other.at(i)) { return false; }
          }
          return true;
        }
      }

    case ItemType::F32: {
      auto const otherVal = uni_at<float>(other);
      return otherVal != nullptr && at<float>() == *otherVal;
    }
    case ItemType::F64: {
      auto const otherVal = uni_at<double>(other);
      return otherVal != nullptr && at<double>() == *otherVal;
    }
    case ItemType::F128: {
      auto const otherVal = uni_at<long double>(other);
      return otherVal != nullptr && at<long double>() == *otherVal;
    }
    case ItemType::UINT8: {
      auto const otherVal = uni_at<uint8_t>(other);
      return otherVal != nullptr && at<uint8_t>() == *otherVal;
    }
    case ItemType::UINT16: {
      auto const otherVal = uni_at<uint16_t>(other);
      return otherVal != nullptr && at<uint16_t>() == *otherVal;
    }

    case ItemType::UINT32: {
      auto const otherVal = uni_at<uint32_t>(other);
      return otherVal != nullptr && at<uint32_t>() == *otherVal;
    }

    case ItemType::UINT64: {
      auto const otherVal = uni_at<uint64_t>(other);
      return otherVal != nullptr && at<uint64_t>() == *otherVal;
    }

    case ItemType::INT8: {
      auto const otherVal = uni_at<int8_t>(other);
      return otherVal != nullptr && at<int8_t>() == *otherVal;
    }

    case ItemType::INT16: {

      auto const otherVal = uni_at<int16_t>(other);
      return otherVal != nullptr && at<int16_t>() == *otherVal;
    }

    case ItemType::INT32: {
      auto const otherVal = uni_at<int32_t>(other);
      return otherVal != nullptr && at<int32_t>() == *otherVal;
    }

    case ItemType::INT64: {
      auto const otherVal = uni_at<int64_t>(other);
      return otherVal != nullptr && at<int64_t>() == *otherVal;
    }

    case ItemType::BOOL: {
      auto const otherVal = uni_at<bool>(other);
      return otherVal != nullptr && at<bool>() == *otherVal;
    }

    case ItemType::STRING: {
      auto const otherVal = uni_at<std::string>(other);
      return otherVal != nullptr && at<std::string>() == *otherVal;
    }

    case ItemType::NULL_TERMINATED_STRING: {
      auto const otherVal = uni_at<char const *>(other);
      return otherVal != nullptr && at<char const *>() == *otherVal;
    }
    default:
      return false;
    }
  }

private:
  template<typename T, bool no_except = false>
  [[nodiscard]] inline auto _value() const noexcept(no_except) -> std::decay_t<T> const * {
    if (auto *result = try_any_cast<std::decay_t<T>, true>(this->content); result) {
      return result;

    } else {
      if constexpr (!no_except) {
        // bug in clang-tidy
        throw InvalidContent(
          "Invalid Content. got: ", readableTypeName(this->content), " queried ", readableTypeName<T>(), '.');
      } else {
        return result;
      }
    }
  }

  template<typename T, bool no_except = false>
  [[nodiscard]] inline auto _value() noexcept(no_except) -> std::decay_t<T> * {
    if (auto *result = try_any_cast<std::decay_t<T>, true, true>(this->content); result) {
      return result;

    } else {
      if constexpr (!no_except) {
        // bug in clang-tidy
        // NOLINTNEXTLINE(throwInNoexceptFunction)
        throw InvalidContent(
          "Invalid Content. got: ", readableTypeName(this->content), " queried ", readableTypeName<T>(), '.');
      } else {
        return result;
      }
    }
  }

  template<typename T = Item, bool no_except = false>
  [[nodiscard]] inline auto _value(std::string &&name) const noexcept(no_except) -> std::decay_t<T> const * {

    try {
      try {
        auto parent = this->template _value<std::map<std::string, Item>, no_except>();
        if constexpr (no_except) {
          if (!parent || !parent->contains(name)) { return nullptr; }
        }

        auto &child = parent->at(name);

        if constexpr (std::is_same_v<std::decay_t<T>, Item>) {
          return &child;
        } else {
          return child.template _value<std::decay_t<T>, no_except>();
        }
      } catch (std::out_of_range const &) {
        if constexpr (no_except) {
          return nullptr;
        } else {
          // bug in clang-tidy
          // NOLINTNEXTLINE(throwInNoexceptFunction)
          throw InvalidContent("Key \"", name, "\" not registered \n");
        }
      }
    } catch (InvalidContent &e) {
      if constexpr (no_except) {
        return nullptr;
      } else {
        e.add(name);
        // bug in clang-tidy
        // NOLINTNEXTLINE(throwInNoexceptFunction)
        throw e;
      }
    }
  }

  template<typename T = Item, bool no_except = false>
  [[nodiscard]] inline auto _value(std::string &&name) noexcept(no_except) -> std::decay_t<T> * {
    try {
      auto parent = this->template _value<std::map<std::string, Item>, no_except>();
      if constexpr (no_except) {
        if (!parent || !parent->contains(name)) { return nullptr; }
      }

      auto const newContent = !parent->contains(name);
      Item &child = (*parent)[name];

      // Initialize the new value as the desired type if not already inside the Item.
      if (newContent) {
        if constexpr(std::is_default_constructible_v<T>) {
          child = T{};
        }
      }

      if constexpr (std::is_same_v<std::decay_t<T>, Item>) {
        return &child;
      } else {
        return child.template _value<std::decay_t<T>, no_except>();
      }

    } catch (std::out_of_range const &) {
      if constexpr (no_except) {
        return nullptr;
      } else {
        throw InvalidContent("Key \"", name, "\" not registered \n");
      }
    } catch (InvalidContent &e) {
      if constexpr (no_except) {
        return nullptr;
      } else {
        e.add(name);
        // bug in clang-tidy
        // NOLINTNEXTLINE(throwInNoexceptFunction)
        throw e;
      }
    }
  }

  template<typename T = Item, bool no_except = false>
  [[nodiscard]] inline auto _value(std::size_t identifier) const noexcept(no_except) -> std::decay_t<T> const * {
    try {
      auto parent = this->template _value<std::vector<Item>, no_except>();
      if constexpr (no_except) {
        if (!parent || identifier >= parent->size()) { return nullptr; }
      }

      auto &child = (*parent)[identifier];
      if constexpr (std::is_same_v<std::decay_t<T>, Item>) {
        return &child;
      } else {
        return child.template _value<std::decay_t<T>, no_except>();
      }
    } catch (InvalidContent &e) {
      if constexpr (no_except) {
        return nullptr;
      } else {
        e.add(identifier);
        // bug in clang-tidy
        // NOLINTNEXTLINE(throwInNoexceptFunction)
        throw e;
      }
    }
  }

  template<typename T = Item, bool no_except = false>
  [[nodiscard]] inline auto _value(std::size_t identifier) noexcept(no_except) -> std::decay_t<T> * {
    try {
      auto parent = this->template _value<std::vector<Item>, no_except>();
      if constexpr (no_except) {
        if (!parent || identifier >= parent->size()) { return nullptr; }
      }

      auto &child = (*parent)[identifier];

      if constexpr (std::is_same_v<std::decay_t<T>, Item>) {
        return &child;
      } else {
        return child.template _value<std::decay_t<T>, no_except>();
      }
    } catch (InvalidContent &e) {
      if constexpr (no_except) {
        return nullptr;
      } else {
        e.add(identifier);
        // bug in clang-tidy
        // NOLINTNEXTLINE(throwInNoexceptFunction)
        throw e;
      }
    }
  }

  template<typename T = Item, bool no_except = false, typename... Ts, typename = std::enable_if_t<sizeof...(Ts) >= 1>>
  [[nodiscard]] inline auto _value(std::size_t first, Ts... second) const noexcept(no_except)
    -> std::decay_t<T> const * {
    try {
      auto const parent = this->template _value<std::vector<Item>, no_except>();

      if constexpr (no_except) {
        if (!parent || first >= parent->size()) { return nullptr; }
      }
      return (*parent)[first].template _value<std::decay_t<T>, no_except>(second...);

    } catch (InvalidContent &e) {
      if constexpr (no_except) {
        return nullptr;
      } else {
        e.add(first);
        // bug in clang-tidy
        // NOLINTNEXTLINE(throwInNoexceptFunction)
        throw e;
      }
    }
  }

  template<typename T = Item, bool no_except = false, typename... Ts, typename = std::enable_if_t<sizeof...(Ts) >= 1>>
  [[nodiscard]] inline auto _value(std::size_t first, Ts... second) noexcept(no_except) -> std::decay_t<T> * {
    try {
      auto const parent = this->template _value<std::vector<Item>, no_except>();

      if constexpr (no_except) {
        if (!parent || first >= parent->size()) { return nullptr; }
      }
      return (*parent)[first].template _value<std::decay_t<T>, no_except>(second...);

    } catch (InvalidContent &e) {
      if constexpr (no_except) {
        return nullptr;
      } else {
        e.add(first);
        // bug in clang-tidy
        // NOLINTNEXTLINE(throwInNoexceptFunction)
        throw e;
      }
    }
  }

  template<typename T = Item, bool no_except = false, typename... Ts, typename = std::enable_if_t<sizeof...(Ts) >= 1>>
  [[nodiscard]] inline auto _value(std::string &&first, Ts... second) const noexcept(no_except)
    -> std::decay_t<T> const * {
    try {
      auto const parent = this->template _value<std::map<std::string, Item>, no_except>();

      if constexpr (no_except) {
        if (!parent || !parent->contains(first)) { return nullptr; }
      }
      try {
        return parent->at(first).template _value<std::decay_t<T>, no_except>(second...);
      } catch (std::out_of_range const &e) { throw InvalidContent("Key \"", first, "\" not registered \n"); }

    } catch (InvalidContent &e) {
      if constexpr (no_except) {
        return nullptr;
      } else {
        e.add(first);
        // bug in clang-tidy
        // NOLINTNEXTLINE(throwInNoexceptFunction)
        throw e;
      }
    }
  }

  template<typename T = Item, bool no_except = false, typename... Ts, typename = std::enable_if_t<sizeof...(Ts) >= 1>>
  [[nodiscard]] inline auto _value(std::string &&first, Ts... second) noexcept(no_except) -> std::decay_t<T> * {
    try {
      auto const parent = this->template _value<std::map<std::string, Item>, no_except>();

      if constexpr (no_except) {
        if (!parent || !parent->contains(first)) { return nullptr; }
      }
      try {
        return parent->at(first).template _value<std::decay_t<T>, no_except>(second...);
      } catch (std::out_of_range const &) { throw InvalidContent("Key \"", first, "\" not registered \n"); }

    } catch (InvalidContent & e) {
      if constexpr (no_except) {
        (void) e;
        return nullptr;
      } else {
        e.add(first);
        // bug in clang-tidy
        // NOLINTNEXTLINE(throwInNoexceptFunction)
        throw e;
      }
    }
  }

  template<typename T = Item, bool no_except = false, typename... Ts>
  [[nodiscard]] inline auto _value(char const *const first, Ts... second) const noexcept(no_except)
    -> std::decay_t<T> const * {
    return this->template _value<std::decay_t<T>, no_except, Ts...>(std::string(first), second...);
  }

  template<typename T = Item, bool no_except = false, typename... Ts>
  [[nodiscard]] inline auto _value(char const *const first, Ts... second) noexcept(no_except) -> std::decay_t<T> * {
    return this->template _value<std::decay_t<T>, no_except, Ts...>(std::string(first), second...);
  }

  template<typename T, typename J> [[nodiscard]] static inline auto uni_at(J const &value) noexcept -> T const * {
    if constexpr (std::is_same_v<J, Item>) {
      return &(value.template at<T>());
    } else {
      if constexpr (std::is_same_v<J, T>) {
        return &value;
      } else {
        return nullptr;
      }
    }
  }

  std::any content{};
  ItemType contentType{};
}; //__attribute__((packed, aligned(32)));


}// namespace dataspree::inference::core

#endif// DATASPREE_INFERENCE_CORE_ITEM_HPP
