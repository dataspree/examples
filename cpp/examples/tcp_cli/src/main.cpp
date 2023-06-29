#include <TcpConnection.hpp>

#include <dataspree/inference/core/Exception.hpp>
#include <dataspree/inference/core/Item.hpp>

#include <spdlog/spdlog.h>

#include <boost/program_options.hpp>

#include <opencv2/imgproc.hpp>
#include <opencv2/highgui.hpp>

#include <numbers>
#include <chrono>
#include <iostream>
#include <string>
#include <thread>
#include <type_traits>

// NOLINTNEXTLINE(bugprone-exception-escape)
auto main(int argc, char const *const *argv) -> int {

  using EncodingModeType = typename std::underlying_type<dataspree::inference::EncodingMode>::type;
  static_assert(std::is_same_v<EncodingModeType, uint8_t>);

  boost::program_options::options_description description("Allowed options");
  description.add_options()("help", "produce help message")(
    "sendImage", boost::program_options::value<bool>()->default_value(true))(
    "producerName", boost::program_options::value<std::string>()->default_value(""),
    "Name of a registered Dataspree Inference Producer.")(
    "consumerName", boost::program_options::value<std::string>()->default_value(""),
    "Name of a registered Dataspree Inference Consumer or empty.")(
    "maxSendIntervalMs", boost::program_options::value<uint32_t>()->default_value(0))(
    "timeoutMs", boost::program_options::value<std::size_t>()->default_value(3500))(
    "ip", boost::program_options::value<std::string>()->default_value("127.0.0.1"),
    "")("port", boost::program_options::value<uint16_t>()->default_value(6729), "")(
    "encoding",
    boost::program_options::value<int>()->default_value(
      dataspree::inference::core::getUnderlyingValue(dataspree::inference::EncodingMode::MSGPACK)),
    "Encoding (0) JSON (1) MSGPACK.");

  boost::program_options::variables_map variableMap;
  boost::program_options::store(boost::program_options::parse_command_line(argc, argv, description), variableMap);
  boost::program_options::notify(variableMap);

  if (variableMap.contains("help")) {
    std::cout << description << std::endl;
    return 0;
  }
  auto const consumerName = variableMap["consumerName"].as<std::string>();
  auto connection = dataspree::inference::TcpConnection(
    variableMap["ip"].as<std::string>(), variableMap["port"].as<uint16_t>(),
    dataspree::inference::ReceiveProperties(
      variableMap["producerName"].as<std::string>(),
      dataspree::inference::EncodingMode(static_cast<EncodingModeType>(variableMap["encoding"].as<int>())),
      variableMap["maxSendIntervalMs"].as<uint32_t>(),
      variableMap["sendImage"].as<bool>()),
    variableMap["timeoutMs"].as<std::size_t>());


  spdlog::set_level(spdlog::level::debug);

  while (true) {

    bool visualized = false;
    std::size_t messageCount = 0;
    cv::Mat displayImage;

    for (; ; ++messageCount) {

      // receive Item if the user configured a producer from which they want to receive data.
      if (connection.isReceiveConfigured()) {
        auto optionalItem = connection.receiveItem();
        if (!optionalItem.has_value()) { break; }

        auto &message = optionalItem.value();

        // Error message ->
        if (auto const *error = message.template find_at<std::string>("error"); error) {
          throw std::runtime_error(error->c_str());
        }

        auto &item = message.at("item");

        // Visualize image and inference results.
        if (auto const *image = item.find_at<cv::Mat>("image"); image) {
          displayImage = image->clone();

          if (auto const *detections = item.find_at("inference", "detection"); detections) {
            for (auto const &det : *detections) {
              auto const centerX = static_cast<int>(std::max(det.template at<double>("x") * image->cols, 0.0));
              auto const centerY = static_cast<int>(std::max(det.template at<double>("y") * image->rows, 0.0));
              auto const width = static_cast<int>(std::max(det.template at<double>("width") * image->cols, 0.0));
              auto const height = static_cast<int>(std::max(det.template at<double>("height") * image->rows, 0.0));
              // auto const confidence = det.template at<double>("confidence");

              auto const orientation =
                static_cast<float>(-(det.template at<double>("orientation")) / std::numbers::pi * 180);

              auto const rect = cv::RotatedRect(cv::Point2f(static_cast<float>(centerX), static_cast<float>(centerY)),
                                                cv::Size2f(static_cast<float>(width), static_cast<float>(height)),
                                                orientation);
              std::array<cv::Point2f, 4> vertices;
              rect.points(vertices.data());
#pragma unroll
              for (std::size_t i = 0; i < 4; ++i) {
                // NOLINTNEXTLINE(cppcoreguidelines-pro-bounds-constant-array-index)
                line(displayImage, vertices[i], vertices[(i + 1) % 4], cv::Scalar(0, 255, 0), 2);
              }
            }
          }
          visualized = true;
          cv::imshow("visualization", displayImage);
          cv::waitKey(1);
        }
      }


      // send out the item that was just received to a new producer.
      if (!consumerName.empty() ) {

        // Read image from camera 0 if no image received.
        if (displayImage.empty()) {
          auto cam = cv::VideoCapture(0);
          cam.read(displayImage);
          cam.release();
        }

        dataspree::inference::core::Item item{};
        item["image"] = displayImage;
        item["id"] = messageCount;
        if (!connection.sendItem(item, consumerName)) {
          break;
        }
      }
    }

    if (visualized) {
      cv::destroyWindow("visualization");
      cv::waitKey(1);
    }

    // Attempt to reconnect
    std::this_thread::sleep_for(std::chrono::seconds(2));
    spdlog::info("Reconnecting.");
    connection.establishConnection();
  }
}
