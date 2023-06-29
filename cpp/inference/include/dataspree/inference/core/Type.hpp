#ifndef DATASPREE_INFERENCE_CORE_TYPE_HPP
#define DATASPREE_INFERENCE_CORE_TYPE_HPP

#include <opencv2/core/mat.hpp>

#include <cstdint>
#include <limits>
#include <map>
#include <string>
#include <type_traits>
#include <vector>

namespace dataspree::inference::core {


struct Item;


enum class ItemType : uint8_t {
  /// Map [str -> Object]
  OBJECT = 0,
  /// Variable sized object array
  ARRAY = 1,
  /// float
  F32 = 2,
  /// double
  F64 = 3,
  /// long double
  F128 = 4,
  /// unsigned char
  UINT8 = 5,
  /// unsigned short
  UINT16 = 6,
  /// unsigned int
  UINT32 = 7,
  /// unsigned long int
  UINT64 = 8,
  /// char
  INT8 = 9,
  /// short
  INT16 = 10,
  /// int
  INT32 = 11,
  /// long
  INT64 = 12,
  /// bool
  BOOL = 13,
  /// std::string
  STRING = 14,
  /// char const *
  NULL_TERMINATED_STRING = 15,
  /// cv::Mat
  MAT = 16,
  BYTE_ARRAY = 17,

  OTHER = std::numeric_limits<uint8_t>::max(),
};

template <typename T> static consteval auto deriveItemType() noexcept -> ItemType {
  using DT = std::decay_t<T>;
  if constexpr (std::is_same_v<DT, std::map<std::string, Item>>) {
    return ItemType::OBJECT;
  }
  if constexpr (std::is_same_v<DT, std::vector<Item>>) {
    return ItemType::ARRAY;
  }
  if constexpr (std::is_same_v<DT, float>) {
    return ItemType::F32;
  }
  if constexpr (std::is_same_v<DT, double>) {
    return ItemType::F64;
  }
  if constexpr (std::is_same_v<DT, long double>) {
    return ItemType::F128;
  }
  if constexpr (std::is_same_v<DT, uint8_t>) {
    return ItemType::UINT8;
  }
  if constexpr (std::is_same_v<DT, uint16_t>) {
    return ItemType::UINT16;
  }
  if constexpr (std::is_same_v<DT, uint32_t>) {
    return ItemType::UINT32;
  }
  if constexpr (std::is_same_v<DT, uint64_t>) {
    return ItemType::UINT64;
  }
  if constexpr (std::is_same_v<DT, int8_t>) {
    return ItemType::INT8;
  }
  if constexpr (std::is_same_v<DT, int16_t>) {
    return ItemType::INT16;
  }
  if constexpr (std::is_same_v<DT, int32_t>) {
    return ItemType::INT32;
  }
  if constexpr (std::is_same_v<DT, int64_t>) {
    return ItemType::INT64;
  }
  if constexpr (std::is_same_v<DT, bool>) {
    return ItemType::BOOL;
  }
  if constexpr (std::is_same_v<DT, std::string>) {
    return ItemType::STRING;
  }
  // if constexpr (std::is_same_v<DT, char *>) {
  //   return ItemType::NULL_TERMINATED_STRING;
  // }
  if constexpr (std::is_same_v<DT, cv::Mat>) {
    return ItemType::MAT;
  }
  if constexpr (std::is_same_v<DT, std::vector<unsigned char>>) {
    return ItemType::BYTE_ARRAY;
  }

  if constexpr (std::is_same_v<DT, char const *>) {
    return ItemType::NULL_TERMINATED_STRING;
  }
  return ItemType::OTHER;
}


} // namespace dataspree::inference::core

#endif// DATASPREE_INFERENCE_CORE_TYPE_HPP
