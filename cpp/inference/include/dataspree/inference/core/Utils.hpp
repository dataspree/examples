#ifndef DATASPREE_INFERENCE_CORE_UTILS_HPP
#define DATASPREE_INFERENCE_CORE_UTILS_HPP

#include <algorithm>
#include <any>
#include <array>
#include <bit>
#include <cassert>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <map>
#include <memory>
#include <optional>
#include <sstream>
#include <string>
#include <type_traits>
#include <typeinfo>
#include <vector>

#ifdef _WIN64
#define _WINSOCKAPI_
#include <windows.h>
#else
// FIXME: I think this might be GNU specific
#include <cxxabi.h>
#endif

#include <span>

namespace dataspree::inference::core {

static constexpr bool isBigEndian = std::endian::native == std::endian::big;
static constexpr bool isLittleEndian = std::endian::native == std::endian::little;
static_assert(isBigEndian || isLittleEndian, "Middle Endian is not supported.");

/// Byte-swap array with the stride of #sizeType
/// \tparam ByteType Byte type (f.i., std::byte, char, unsigned char). Has to be of size 1.
/// \param buffer Byte buffer.
/// \param bufferSize Size of byte buffer.
/// \param sizeType Size of types contained in the byte buffer; stride of byteswapping.
    template <typename ByteType, typename = std::enable_if_t<sizeof(ByteType) == 1>>
    void swap(ByteType *buffer, std::size_t bufferSize, std::size_t sizeType) {
        if (sizeType > 1)  [[likely]] {

#pragma unroll 8
            for (std::size_t pos{0}; pos < bufferSize; pos+=sizeType) {
                std::ranges::reverse(std::span(&(buffer[pos]), sizeType));
            }
        }
    }

/// Convert content of a buffer to little endian.
/// \tparam ByteType Byte type (f.i., std::byte, char, unsigned char). Has to be of size 1.
/// \param buffer buffer on which this function operates.
/// \param bufferSize size of the buffer.
/// \param sizeType size of instances of the type contained in the buffer.
template <typename ByteType> auto toLittleEndian(ByteType *buffer, std::size_t bufferSize, std::size_t sizeType)
-> ByteType* requires(sizeof(ByteType) == 1 && std::has_unique_object_representations_v<ByteType>) {
  if constexpr (isBigEndian) {
    swap(buffer, bufferSize, sizeType);
  }
  return buffer;
}

///
/// \tparam PodType
/// \tparam ByteType
/// \param value
/// \return
template<typename PodType, typename ByteType = char>
[[nodiscard]] inline auto toLittleEndian(PodType value) noexcept -> std::array<ByteType, sizeof(PodType)>
requires(std::is_trivial_v<PodType> && std::is_standard_layout_v<PodType> && sizeof(ByteType) == 1) {
  auto memory = std::bit_cast<std::array<ByteType, sizeof(PodType)>>(value);
  if constexpr (isBigEndian) { std::ranges::reverse(memory); }
  return memory;
}

/// Convert content of a buffer to big endian.
/// \tparam ByteType Byte type (f.i., std::byte, char, unsigned char). Has to be of size 1.
/// \param buffer buffer on which this function operates.
/// \param bufferSize size of the buffer.
/// \param sizeType size of instances of the type contained in the buffer.
template <typename ByteType, typename = std::enable_if_t<sizeof(ByteType) == 1>>
  auto toBigEndian(ByteType *buffer, std::size_t bufferSize, std::size_t sizeType) -> ByteType * {
  if constexpr (isLittleEndian) {
    swap(buffer, bufferSize, sizeType);
  }
  return buffer;
}

///
/// \tparam PodType
/// \tparam ByteType
/// \param value
/// \return
template<typename PodType, typename ByteType = char>
[[nodiscard]] inline auto toBigEndian(PodType value) noexcept -> std::array<ByteType, sizeof(PodType)>
requires(std::is_trivial_v<PodType> && std::is_standard_layout_v<PodType> && sizeof(ByteType) == 1) {
  auto memory = std::bit_cast<std::array<ByteType, sizeof(PodType)>>(value);
  if constexpr (isLittleEndian) { std::ranges::reverse(memory); }
  return memory;
}

/// Interpret chunk of an array as containing values of type #PodType and return instance of #PodType.
/// \tparam PodType Desired return type.
/// \tparam ByteType Byte type (f.i., std::byte, char, unsigned char). Has to be of size 1.
/// \param value array containing instance of #PodType.
/// \param offset offset in buffer.
/// \return decoded instance of #PodType.
template<typename PodType, typename ByteType> inline auto as(ByteType *const value, int offset=0) noexcept -> PodType {
  PodType pod;
  std::memcpy(&pod, &(value[offset]), sizeof(PodType));
  return pod;
}

/// Read a value represented in big endian in the endianness supported by this machine.
/// \tparam PodType Desired return type.
/// \tparam ByteType Byte type (f.i., std::byte, char, unsigned char). Has to be of size 1.
/// \param value value represented in big endian.
/// \return the decoded instance of #PodType
template<typename PodType, typename ByteType>
[[nodiscard]] inline auto readBigEndian(ByteType *const value) noexcept -> PodType
  requires(std::is_trivial_v<PodType> && std::is_standard_layout_v<PodType> && sizeof(ByteType) == 1) {
      //return std::bit_cast<PodType>(toBigEndian(value, sizeof(PodType), sizeof(PodType)));
        return as<PodType>(toBigEndian(value, sizeof(PodType), sizeof(PodType)));

}

/// Read a value represented in little endian in the endianness supported by this machine.
/// \tparam PodType Desired return type.
/// \tparam ByteType Byte type (f.i., std::byte, char, unsigned char). Has to be of size 1.
/// \param value value represented in little endian.
/// \return the decoded instance of #PodType
template<typename PodType, typename ByteType>
[[nodiscard]] inline auto readLittleEndian(ByteType *const value) noexcept -> ByteType
  requires(std::is_trivial_v<PodType> && std::is_standard_layout_v<PodType> && sizeof(ByteType) == 1) {
        return std::bit_cast<PodType>(toLittleEndian(value, sizeof(PodType), sizeof(PodType)));
}

/// Return the value from an enum.
inline auto getUnderlyingValue(auto enum_value) noexcept {
  return static_cast<typename std::underlying_type<decltype(enum_value)>::type>(enum_value);
}

///
/// \tparam A
/// \tparam Ref
template<typename A, template<typename...> class Ref> struct is_specialization : std::false_type {};

///
/// \tparam Ref
/// \tparam Args
template<template<typename...> class Ref, typename... Args>
struct is_specialization<Ref<Args...>, Ref> : std::true_type {};

/// \tparam T Evaluates to true if T is a templated vector.
template<typename T> static constexpr auto is_vector_v = is_specialization<T, std::vector>::value;

/// \tparam T Evaluates to true if T is a templated map.
template<typename T> static constexpr auto is_map_v = is_specialization<T, std::map>::value;

[[nodiscard]] inline auto demangle(const char *mangled) -> std::string {
#ifdef _WIN64
        return std::string(mangled);
        // throw std::runtime_error("Error decoding length");
#else
        int status{ 0 };
  // NOLINTNEXTLINE
  std::unique_ptr<char[], void (*)(void *)> const result(
    abi::__cxa_demangle(mangled, nullptr, nullptr, &status), std::free);
  return result != nullptr ? std::string(result.get()) : "error occurred";
#endif
    }

template<typename T> [[nodiscard]] constexpr auto readableTypeName() noexcept -> std::string {
    return fmt::format("{} {} {} {}",
                       []() constexpr -> std::string_view {
                           if constexpr (std::is_const_v<T>) {
                               return "const";
                           } else {
                               return "";
                           }
                       }(),
                       []() constexpr -> std::string_view {
                           if constexpr (std::is_volatile_v<T>) {
                               return "volatile";
                           } else {
                               return "";
                           }
                       }(),
                       []() constexpr -> std::string_view {
                           if constexpr (std::is_lvalue_reference_v<T>) {
                               return "&";
                           } else if constexpr (std::is_rvalue_reference_v<T>) {
                               return "&&";
                           } else {
                               return "";
                           }
                       }(),
                       demangle(typeid(T).name()));
}

template<typename T> [[nodiscard]] inline auto readableTypeName(T const &variable) {
  return demangle(typeid(variable).name());
}

[[nodiscard]] inline auto readableTypeName(std::any const &variable) { return demangle(variable.type().name()); }

template<typename T, bool no_except = false, bool access = false>
auto try_any_cast(std::conditional_t<access, std::any &, std::any const &> anyVariable) noexcept(no_except)
-> std::conditional_t<no_except,
            std::conditional_t<access, std::decay_t<T> *, std::decay_t<T> const *>,
            std::conditional_t<access, std::decay_t<T> &, std::decay_t<T> const &>> {
        if constexpr (no_except) {
            if constexpr (access) {
                return std::any_cast<std::decay_t<T>>(&anyVariable);
            } else {
                return std::any_cast<std::decay_t<T> const>(&anyVariable);
            }
        } else {
            if constexpr (access) {
                return std::any_cast<std::decay_t<T> &>(anyVariable);
            } else {
                return std::any_cast<std::decay_t<T> const &>(anyVariable);
            }
        }
}



}// namespace dataspree::inference::core

#endif// DATASPREE_INFERENCE_CORE_UTILS_HPP
