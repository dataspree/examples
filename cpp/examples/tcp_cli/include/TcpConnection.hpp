#ifndef DATASPREE_INFERENCE_CONNECTION_HPP
#define DATASPREE_INFERENCE_CONNECTION_HPP

#include <Conversion.hpp>

#include <dataspree/inference/core/Item.hpp>
#include <dataspree/inference/core/Utils.hpp>

#include <spdlog/spdlog.h>

#include <algorithm>
#include <bit>
#include <cassert>
#include <cstdio>
#include <cstring>
#include <iostream>
#include <map>
#include <sstream>
#include <string>
#include <vector>

#include <chrono>

#include <sys/types.h>

#if _WIN64
using SocketType = uint64_t;
#else
using SocketType = int;
#endif

extern SocketType invalidSocket;

namespace dataspree::inference {

// NOLINTNEXTLINE(altera-struct-pack-align)
struct [[nodiscard]] TcpConnection final {

  template<typename String, typename = std::enable_if_t<std::is_constructible_v<std::string, std::decay_t<String>>>>
  explicit TcpConnection(String &&remoteIp,
    uint16_t const remotePort,
    ReceiveProperties &&receiveProperties,
    std::size_t timeoutMs = 10500)
    : remoteIp(std::forward<String>(remoteIp)), remotePort(remotePort), receiveProperties(std::move(receiveProperties)),
      timeoutMs(timeoutMs) {
    establishConnection();
  }

  ~TcpConnection() { this->_disconnect(); }

  TcpConnection(TcpConnection const &) = delete;
  TcpConnection(TcpConnection &&) = delete;
  auto operator=(TcpConnection const &other) noexcept -> TcpConnection & = delete;
  auto operator=(TcpConnection &&other) noexcept -> TcpConnection & = delete;

  /// Reconnect
  inline auto establishConnection() -> void {
    _disconnect();
    _connect();
  }

  /// Transmit an item to the specified consumer
  template<typename String, typename = std::enable_if_t<std::is_constructible_v<std::string, std::decay_t<String>>>>
  auto sendItem(core::Item &item, String consumer_name) -> bool {
    core::Item messageItem{};
    messageItem["consumer_name"] = consumer_name;
    messageItem["item"] = item;
    return this->sendMessageItem(messageItem);
  }

  /// Send a message that updates the connection properties, which determine the format in which the Inference
  /// server replies.
  inline auto sendUpdateReceiveProperties(ReceiveProperties const updatedReceiveProperties) -> bool {
    this->receiveProperties = updatedReceiveProperties;
    return this->sendUpdateReceiveProperties();
  }

  /// Send a message that updates the connection properties, which determine the format in which the Inference
  /// server replies.
  inline auto sendUpdateReceiveProperties() -> bool {
    if (!this->receiveProperties.isReceiveConfigured()) { return true; }

    auto greetingItem = this->receiveProperties.toItem();
    return this->sendMessageItem(greetingItem);
  }

  [[nodiscard]] inline auto isReceiveConfigured() const noexcept -> bool {
    return this->receiveProperties.isReceiveConfigured();
  }

  /// Receive an item from the consumer specified int he connection properties.
  auto receiveItem() noexcept -> std::optional<core::Item> {
    if (auto &&[encodingMode, optionalBuffer] = this->receiveMessage();
        optionalBuffer.has_value() && encodingMode.has_value()) {
      return { decodeItem(optionalBuffer.value().data(), optionalBuffer.value().size(), encodingMode.value()) };
    }
    spdlog::warn("Timeout while receiving message for producer \"{}\"", this->receiveProperties.getProducerName());
    return std::nullopt;
  }

  [[nodiscard]] inline auto getFramerateReceived() const noexcept {
    auto const finish = std::chrono::steady_clock::now();
    auto const elapsedSeconds =
      std::chrono::duration_cast<std::chrono::duration<double>>(finish - this->timeReceived).count();
    return std::tuple{ static_cast<double>(numberOfMessagesReceived) / elapsedSeconds, finish };
  }

  [[nodiscard]] inline auto getFramerateSent() const noexcept {
    auto const finish = std::chrono::steady_clock::now();
    auto const elapsedSeconds =
      std::chrono::duration_cast<std::chrono::duration<double>>(finish - this->timeSent).count();
    return std::tuple{ static_cast<double>(numberOfMessagesSent) / elapsedSeconds, finish };
  }

private:

  /// Receive an encoded message via the socket.
  [[nodiscard]] auto receiveMessage() const noexcept
    -> std::pair<std::optional<EncodingMode>, std::optional<std::vector<char>>> {
    // Unsigned int 4,  big endian
    std::optional<std::vector<char>> sizeBuffer = receiveNumberOfBytes(4);
    if (!sizeBuffer.has_value()) {
      spdlog::warn("Error receiving message (part 1: size).");
      return std::make_pair(std::nullopt, std::nullopt);
    }
    assert(sizeBuffer->size() == 4);
    auto const messageSize = core::readBigEndian<uint32_t>(sizeBuffer.value().data());

    std::optional<std::vector<char>> encodingModeBuffer = receiveNumberOfBytes(1);
    if (!encodingModeBuffer.has_value()) {
      spdlog::warn("Error receiving message (part 2: encoding).");
      return std::make_pair(std::nullopt, std::nullopt);
    }
    assert(encodingModeBuffer->size() == 1);
    auto const encodingMode = core::readBigEndian<uint8_t>(encodingModeBuffer.value().data());

    std::optional<std::vector<char>> message = receiveNumberOfBytes(messageSize);
    if (!message.has_value()) {
      spdlog::warn("Error receiving message (part 3: data).");
      return std::make_pair(std::nullopt, std::nullopt);
    }

    ++this->numberOfMessagesReceivedSinceStart;
    if (++this->numberOfMessagesReceived % 100 == 0) {
      auto const [framerate, now] = this->getFramerateReceived();
      spdlog::debug("Received message #{} of size {}; {}fps {}ms.",
        numberOfMessagesReceivedSinceStart,
        messageSize,
        framerate,
        1000 / framerate);

      this->numberOfMessagesReceived = 0;
      this->timeReceived = now;
    } else {
      spdlog::debug("Received message #{} of size {}.", numberOfMessagesReceivedSinceStart, messageSize);
    }

    return std::make_pair(static_cast<EncodingMode>(encodingMode), message);
  }

  /// Read exact number of required bytes from socket.
  [[nodiscard]] auto receiveNumberOfBytes(std::size_t const number_required_bytes) const noexcept
    -> std::optional<std::vector<char>>;

  auto sendMessageItem(core::Item &messageItem) -> bool {
    if (!connected()) { return false; }

    auto buffer = encodeItem(messageItem, this->receiveProperties.getEncodingMode());
    return this->sendData(buffer.get(), buffer.size());
  }

  auto sendData(char const *data, std::size_t size) const -> bool;

  [[nodiscard]] auto socketConnected() const noexcept -> bool;

  [[nodiscard]] inline auto connected() const noexcept -> bool { return this->fdClient >= 0 && socketConnected(); }

  void _disconnect();

  void _connect();

  std::string remoteIp;
  uint16_t remotePort;
  ReceiveProperties receiveProperties;
  std::size_t timeoutMs;

  SocketType fdSocket{invalidSocket};
  int fdClient{ -1 };

  mutable std::size_t numberOfMessagesReceived = 0;
  mutable std::size_t numberOfMessagesSent = 0;
  mutable std::size_t numberOfMessagesReceivedSinceStart = 0;
  mutable std::size_t numberOfMessagesSentSinceStart = 0;
  mutable std::chrono::steady_clock::time_point timeSent{};
  mutable std::chrono::steady_clock::time_point timeReceived{};
};

}// namespace dataspree::inference
#endif// DATASPREE_INFERENCE_CONNECTION_HPP
