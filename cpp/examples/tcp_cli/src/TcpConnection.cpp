#include <TcpConnection.hpp>

#ifdef _WIN64
#include <Winsock2.h>
#include <Ws2tcpip.h>

#ifdef max
#undef max
#undef min
#endif

template<typename... T> auto socket_read(SOCKET fd, char *buf, std::size_t count) {
  assert(count < std::numeric_limits<int>::max());
  return recv(fd, buf, static_cast<int>(count), 0);
}

auto socket_send(SOCKET fd, void const *buf, std::size_t count) {
  assert(count < std::numeric_limits<int>::max());
  return send(fd, static_cast<char const *>(buf), static_cast<int>(count), 0);
}

auto socketClose(SOCKET fd) { return closesocket(fd); }

#else

#include <arpa/inet.h>
#include <bits/socket.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>
static constexpr SocketType INVALID_SOCKET = -1;

auto socket_read(int fd, char *buf, std::size_t count) { return read(fd, buf, count); }
auto socket_send(int fd, void const *buf, std::size_t count) { return send(fd, buf, count, MSG_NOSIGNAL); }

auto socketClose(int fd) { return close(fd); }

#endif


SocketType invalidSocket = INVALID_SOCKET;


auto dataspree::inference::TcpConnection::sendData(char const *data, std::size_t size) const -> bool {
  // encoding code
  assert(size < std::numeric_limits<uint32_t>::max());
  auto const messageSize{ static_cast<uint32_t>(size) };

  // send the size
  int64_t bytesJustSent {0};
  auto const messageSizeChunk = core::toBigEndian(messageSize);
  static_assert(messageSizeChunk.size() == 4);
  for (int64_t total = 0; static_cast<std::size_t>(total) < 4;
       total += (bytesJustSent = socket_send(fdSocket, messageSizeChunk.data(), 4))) {
    if (bytesJustSent < 0) {
      spdlog::warn("Error sending message (part 1: size) {}.", bytesJustSent);
      return false;
    }
  }

  // send the encoding
  auto const encodingChunk = core::toBigEndian(core::getUnderlyingValue(this->receiveProperties.getEncodingMode()));
  static_assert(encodingChunk.size() == 1);

  for (int64_t total = 0; static_cast<std::size_t>(total) < 1;
       total += (bytesJustSent = socket_send(fdSocket, encodingChunk.data(), 1))) {
    if (bytesJustSent < 0) {
      spdlog::warn("Error sending message (part 2: encoding) {}.", bytesJustSent);
      return false;
    }
  }

  // send the message
  for (int64_t total = 0; static_cast<std::size_t>(total) < size;
       total += (bytesJustSent = socket_send(fdSocket, data, size))) {
    if (bytesJustSent < 0) {
      spdlog::warn("Error sending message (part 3: data) {}.", bytesJustSent);
      return false;
    }
  }

  ++this->numberOfMessagesSentSinceStart;
  if (++this->numberOfMessagesSent % 100 == 0) {
    auto const [framerate, now] = this->getFramerateSent();
    spdlog::debug("Sent out message #{} of size {}; {}fps {}ms.",
      numberOfMessagesSentSinceStart,
      size,
      framerate,
      1000 / framerate);

    this->numberOfMessagesSent = 0;
    this->timeSent = now;
  } else {
    spdlog::debug("Sent out message #{} of size {}.", numberOfMessagesSentSinceStart, size);
  }

  return true;
}


[[nodiscard]] auto dataspree::inference::TcpConnection::receiveNumberOfBytes(
  std::size_t const numberRequiredBytes) const noexcept -> std::optional<std::vector<char>> {

  auto const start = std::chrono::steady_clock::now();

  if (!connected()) { return std::nullopt; }

  auto buffer = std::vector<char>(numberRequiredBytes);
  auto numberReceivedBytes = std::size_t{ 0 };

  // NOLINTNEXTLINE(altera-unroll-loops)
  while (numberReceivedBytes < numberRequiredBytes) {
    auto const numberMissingBytes = numberRequiredBytes - numberReceivedBytes;
    if (auto const numberNewBytes = socket_read(fdSocket, &buffer[numberReceivedBytes], numberMissingBytes);
        numberNewBytes < 0) {
      return std::nullopt;

    } else if (numberNewBytes == 0 and this->timeoutMs > 0) {
      auto const finish = std::chrono::steady_clock::now();
      auto const elapsedSeconds = std::chrono::duration_cast<std::chrono::duration<double>>(finish - start).count();
      if (elapsedSeconds * 1000 > static_cast<double>(this->timeoutMs)) { return std::nullopt; }

    } else {
      numberReceivedBytes += static_cast<std::size_t>(numberNewBytes);
    }
  }

  assert(numberReceivedBytes == numberRequiredBytes);
  return buffer;
}

void dataspree::inference::TcpConnection::_disconnect() {
#ifdef _WIN64
  try {
    if (this->fdClient >= 0) {
      shutdown(this->fdSocket, SD_SEND);
    }
  } catch (std::exception const &e) {
    spdlog::warn("Encountered an exception while attempting to close client {}. ", e.what());
  }
#endif
  this->fdClient = -1;


  try {
    if (socketConnected()) { socketClose(this->fdSocket); }
  } catch (std::exception const &e) {
    spdlog::warn("Encountered an exception while attempting to close socket {}.", e.what());
  }
  this->fdSocket = invalidSocket;

#ifdef _WIN64
  WSACleanup();
#endif
}

auto dataspree::inference::TcpConnection::_connect() -> void {
  if (!connected()) {
#ifdef _WIN64
    WSADATA d;
    if (WSAStartup(MAKEWORD(2, 2), &d)) { throw std::runtime_error("Could not initialize Winsock"); }
#endif

    this->fdSocket = socket(AF_INET, SOCK_STREAM, 0);
    if (!socketConnected()) {
      spdlog::warn("TcpConnection to socket failed.");
      return;
    }

    struct sockaddr_in serv_addr {};
    serv_addr.sin_family = AF_INET;
    serv_addr.sin_port = htons(remotePort);
    if (auto const successCode = inet_pton(AF_INET, remoteIp.c_str(), &serv_addr.sin_addr); successCode <= 0) {
      throw std::runtime_error("Invalid address!");
    }

    auto const addr = std::bit_cast<sockaddr>(serv_addr);
    this->fdClient = connect(fdSocket, &addr, sizeof(addr));
    if (!connected()) {
      spdlog::warn("TcpConnection failed.");
      this->_disconnect();
      return;
    }

#if _WIN64
    assert(this->timeoutMs <= std::numeric_limits<DWORD>::max());
    DWORD timeout = static_cast<DWORD>(this->timeoutMs);
    setsockopt(this->fdSocket, SOL_SOCKET, SO_RCVTIMEO, reinterpret_cast<const char *>(&timeout), sizeof(timeout));
    setsockopt(this->fdSocket, SOL_SOCKET, SO_SNDTIMEO, reinterpret_cast<const char *>(&timeout), sizeof(timeout));
#else
    struct timeval timeval {
      .tv_sec = static_cast<decltype(timeval.tv_sec)>(this->timeoutMs / 1000),
      .tv_usec = static_cast<decltype(timeval.tv_usec)>(1000 * (this->timeoutMs % 1000)),
    };
    setsockopt(this->fdSocket, SOL_SOCKET, SO_RCVTIMEO, static_cast<const void *>(&timeval), sizeof(timeval));
    setsockopt(this->fdSocket, SOL_SOCKET, SO_SNDTIMEO, static_cast<const void *>(&timeval), sizeof(timeval));
#endif

    this->sendUpdateReceiveProperties();

    this->numberOfMessagesReceivedSinceStart = 0;
    this->numberOfMessagesSentSinceStart = 0;
    this->numberOfMessagesReceived = 0;
    this->numberOfMessagesSent = 0;
    this->timeSent = std::chrono::steady_clock::now();
    this->timeReceived = std::chrono::steady_clock::now();
  }
}

[[nodiscard]] auto dataspree::inference::TcpConnection::socketConnected() const noexcept -> bool {
#ifdef _WIN64
  return this->fdSocket != INVALID_SOCKET;
#else
  return this->fdSocket >= 0;
#endif
}
