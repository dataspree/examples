
#include <opencv2/highgui.hpp>
#include <opencv2/imgproc.hpp>

#include <boost/beast/core/detail/base64.hpp>

#include <nlohmann/json.hpp>


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

#else

#include <arpa/inet.h>
#include <bits/socket.h>
#include <netinet/in.h>
#include <sys/socket.h>

auto socket_read(int fd, char *buf, std::size_t count) { return read(fd, buf, count); }
auto socket_send(int fd, void const *buf, std::size_t count) { return send(fd, buf, count, MSG_NOSIGNAL); }

#endif


#include <algorithm>
#include <iostream>
#include <limits>
#include <numbers>
#include <string>
#include <type_traits>


using json = nlohmann::json;

/// \param requiredBytes Number of bytes the caller would like to receive.
/// \return vector of size requiredBytes
/// \throws std::runtime_error on socket read error.
[[nodiscard]] auto receive(auto const fdSocket, uint32_t const requiredBytes) -> std::vector<char> {
  auto buffer = std::vector<char>(requiredBytes);
  for (std::size_t readBytes = 0; readBytes < requiredBytes; ) {
    if (auto const newBytes = socket_read(fdSocket, &buffer[readBytes], requiredBytes - readBytes); newBytes < 0) {
        throw std::runtime_error("Connection error.");
    } else {
        readBytes += static_cast<std::size_t>(newBytes);
    }
  }
  return buffer;
}

/// Receive data of type T.
/// \tparam T the type of the target
/// \return an instance of T, received via the socket.
template<typename T, typename = std::enable_if_t<std::is_trivially_copyable_v<T> && std::is_default_constructible_v<T>>>
[[nodiscard]] auto receive(auto const fdSocket) -> T {
  // Receive buffer
  auto const buffer = receive(fdSocket, sizeof(T));
  // @dev: This copy avoids UB and is optimized away.
  T trivial;
  std::memcpy(&trivial, buffer.data(), sizeof(T));
  return trivial;
}

/// \param src base64 encoded string
/// \return vector that contains decoded byte array.
[[nodiscard]] auto base64decode(std::string const &src) -> std::vector<unsigned char> {
  auto const targetSize = boost::beast::detail::base64::decoded_size(src.size());
  auto vec = std::vector<unsigned char>(targetSize);
  boost::beast::detail::base64::decode(static_cast<void *>(vec.data()), src.data(), src.size());
  return vec;
}

// NOLINTNEXTLINE(bugprone-exception-escape)
auto main() -> int {

#if defined(_WIN64)
    // Initialize Winsock.
    WSADATA d;
    if (WSAStartup(MAKEWORD(2, 2), &d)) {
        fprintf(stderr, "Failed to initialize.\n");
        return 1;
    }
#endif


  // Create a socket for IPv4 TCP communication.
  auto const fdSocket = socket(AF_INET, SOCK_STREAM, 0);
  // if (fdSocket == INVALID_SOCKET) return 1;  // windows XXX
  if (fdSocket < 0) { return 1; }

  // Connect
  struct sockaddr_in address {};
  address.sin_family = AF_INET;
  address.sin_port = htons(6729);
  if (auto const success = inet_pton(AF_INET, "127.0.0.1", &address.sin_addr); success <= 0) { return 1; }
  auto const addr = std::bit_cast<sockaddr>(address);
  auto const fdClient = connect(fdSocket, &addr, sizeof(addr));
  if (fdClient < 0) { return 1; }

  // Send connection properties
  // @see dataspree.inference.ReceiveProperties
  constexpr uint8_t encodingMode {0};// 0 (JSON), 1 (MSGPACK)
  auto const configMessage = json{ { "producer_name", "model" },
    { "stream_options",
      json{
        { "encoding_mode", encodingMode },
        { "included_paths", { { "image" }, { "inference" } } },
        {
          "encoding_tree",
          json{ { "image", "IMAGE_PNG" } },
        },
      } } }.dump();
  assert(configMessage.size() <= std::numeric_limits<int>::max());
  auto const encodedMessageSize = htonl(static_cast<uint32_t>(configMessage.size()));
  if (socket_send(fdSocket, &encodedMessageSize, 4) != 4 ||//< message body size
      socket_send(fdSocket, &encodingMode, 1) != 1 ||//< send encoding of this message
      socket_send(fdSocket, configMessage.c_str(), configMessage.size())
        != static_cast<int64_t>(configMessage.size())) {//< send body
    return 2;
  }

  // Receive content from the sender.
  try {
    for (;;) {
      // receive an entire message
      auto const sizeBlob = htonl(receive<uint32_t>(fdSocket));
      auto const receivedEncodingMode = receive<uint8_t>(fdSocket);
      auto const blob = receive(fdSocket, sizeBlob);
      if (encodingMode != receivedEncodingMode) { return 4; }

      // parse result
      auto spannedSource = std::span(blob.data(), sizeBlob);
      auto const parsedResult = json::parse(spannedSource.begin(), spannedSource.end());

      if (parsedResult.contains("error")) { throw std::runtime_error{parsedResult["error"]}; }

      auto const &item = parsedResult["item"];

      // Decode Image
      auto const &imageEncoded = item["image"];
      assert(imageEncoded.is_string());
      auto imageString = base64decode(imageEncoded.template get<std::string>());
      cv::Mat image = cv::imdecode(
        cv::Mat(1, static_cast<int>(imageString.size()), CV_8UC1, static_cast<void *>(imageString.data())),
        cv::IMREAD_UNCHANGED);

      if (item.contains("inference")) {
        std::array<cv::Point2f, 4> vertices;
        for (auto const &det : item["inference"]["detection"]) {
          auto const centerX = static_cast<int>(std::max(det["x"].template get<double>() * image.cols, 0.0));
          auto const centerY = static_cast<int>(std::max(det["y"].template get<double>() * image.rows, 0.0));
          auto const width = static_cast<int>(std::max(det["width"].template get<double>() * image.cols, 0.0));
          auto const height = static_cast<int>(std::max(det["height"].template get<double>() * image.rows, 0.0));
          auto const orient = static_cast<float>(-(det["orientation"].template get<double>()) / std::numbers::pi * 180);

          auto const rect = cv::RotatedRect(cv::Point2f(static_cast<float>(centerX), static_cast<float>(centerY)),
                                            cv::Size2f(static_cast<float>(width), static_cast<float>(height)),
                                            orient);
          rect.points(vertices.data());
          for (std::size_t i = 0; i < 4; ++i) {
            // NOLINTNEXTLINE(cppcoreguidelines-pro-bounds-constant-array-index)
            line(image, vertices[i], vertices[(i + 1) % 4], cv::Scalar(0, 255, 0), 2);
          }
        }
      }

      cv::imshow("visualization", image);
      cv::waitKey(1);
    }
  } catch (std::runtime_error const &re) {
    std::cerr << re.what() << std::endl;
    return 3;
  }
}
