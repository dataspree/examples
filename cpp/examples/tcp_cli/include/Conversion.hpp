#ifndef DATASPREE_INFERENCE_CONVERSION_HPP
#define DATASPREE_INFERENCE_CONVERSION_HPP

#include <dataspree/inference/core/Item.hpp>

namespace dataspree::inference {

struct [[nodiscard]] Buffer {

  template <typename T, typename = std::enable_if_t<!std::is_same_v<Buffer, std::decay_t<T>>>>
  explicit Buffer(T &&content): buffer(std::make_unique<BufferModel<T>>(std::forward<T>(content))) {}
  Buffer(Buffer const &) = delete;
  inline Buffer(Buffer &&other) : buffer(std::move(other.buffer)) { }

  ~Buffer() = default;

  auto operator=(Buffer const &) = delete;
  auto operator=(Buffer && other) { buffer = std::move(other.buffer); }


  [[nodiscard]] inline auto get() noexcept -> char * { return buffer->get(); }
  [[nodiscard]] inline auto size() const noexcept -> std::size_t { return buffer->size(); }

  struct BufferConcept {
    virtual ~BufferConcept() = default;
    [[nodiscard]] virtual auto get() noexcept -> char * = 0;
    [[nodiscard]] virtual auto size() const noexcept -> std::size_t = 0;
  };

  template< typename T> struct BufferModel final : BufferConcept {
    static_assert(requires(T content){content.data();} &&  requires(T content){content.size();},
      "Template type is required to have data() and size() functions.");

    inline explicit BufferModel(T&& content) : buffer(std::forward<T>(content)) {}
    ~BufferModel() final = default;

    [[nodiscard]] inline auto size() const noexcept -> std::size_t final { return buffer.size(); }
    [[nodiscard]] inline auto get() noexcept -> char * final { return buffer.data(); }

    T buffer;
  };

private:
  std::unique_ptr<BufferConcept > buffer;

};


/// Supported message encodings.
enum class EncodingMode : uint8_t { JSON = 0, MSGPACK = 1 };

/// Helper struct that allows the user to define the configuration message for default receive configurations.
// NOLINTNEXTLINE(altera-struct-pack-align)
struct ReceiveProperties {

  /// Initialize helper struct.
  /// \tparam String Helper template to accept both rvalues and lvalues in a single constructor.
  /// \param producerName the name of the producer that is registered in the running Dataspree Inference instance.
  /// \param encodingMode The encoding that we want Dataspree Inference to use for subsequent messages.
  /// \param maxSendIntervalMs If configured, specifies the smallest possible send interval.
  /// \param sendImage Receive image (if present) in default path.
  /// \param sendPointCloud Receive point cloud (if present) in the default path.
  /// \param sendInference Receive inference results (if present) in the default path.
  template<typename String, typename = std::enable_if_t<std::is_constructible_v<std::string, std::decay_t<String>>>>
  [[nodiscard]] explicit ReceiveProperties(String &&producerName,
    EncodingMode encodingMode = EncodingMode::JSON,
    uint32_t maxSendIntervalMs = 0,
    bool sendImage = true,
    bool sendPointCloud = true,
    bool sendInference = true) noexcept
    : producerName(std::forward<String>(producerName)), maxSendIntervalMs(maxSendIntervalMs),
      encodingMode(encodingMode) {

    if (sendPointCloud) {
      this->includedPaths.push_back({ "point_cloud" });
      this->encodingMap["point_cloud"] = "EncodingType.PointCloud";
    }

    if (sendInference) {
      this->includedPaths.push_back({ "inference" });
      this->includedPaths.push_back({ "rois" });
      this->excludedPaths.push_back({ "inference", "detection", "*", "localization" });
    }

    if (sendImage) {
      this->includedPaths.push_back({ "image" });
      //this->encodingMap["image"] = "IMAGE_PNG";
      this->encodingMap["image"] = "MAT_RAW";
    }
  }

  /// \return if this configuration indicates that the client would like to receive data from the server, i.e., the
  ///         server requires a configuration message to be sent.
  [[nodiscard]] inline bool isReceiveConfigured() const noexcept { return !this->producerName.empty(); }

  /// Convert the current configuration to an item, that can be sent to the Dataspree Inference instance to configure
  /// the connection.
  [[nodiscard]] inline auto toItem() const noexcept -> core::Item {
    core::Item item{};
    item["producer_name"] = this->producerName;
    item["stream_options"] = core::Item();
    item["stream_options"]["included_paths"] = core::Item(this->includedPaths);
    item["stream_options"]["excluded_paths"] = core::Item(this->excludedPaths);
    item["stream_options"]["encoding_tree"] = core::Item(this->encodingMap);
    item["stream_options"]["max_send_interval_ms"] = core::Item(this->maxSendIntervalMs);
    item["stream_options"]["encoding_mode"] = core::getUnderlyingValue(this->encodingMode);
    return item;
  }

  [[nodiscard]] constexpr auto getProducerName() const noexcept -> std::string const & { return producerName; }

  [[nodiscard]] constexpr auto getEncodingMode() const noexcept -> EncodingMode { return encodingMode; }

private:
  std::string producerName;
  uint32_t maxSendIntervalMs;
  EncodingMode encodingMode;

  std::vector<std::vector<std::string>> includedPaths{};
  std::vector<std::vector<std::string>> excludedPaths{};
  std::map<std::string, std::string> encodingMap;
};

[[nodiscard]] auto encodeItem(core::Item &item, EncodingMode encodingMode,
  std::string preferredImageEncoding = "") -> Buffer;

[[nodiscard]] auto decodeItem(char const *buffer, std::size_t bufferSize, EncodingMode encodingMode) -> core::Item;


}// namespace dataspree::inference

#endif// DATASPREE_INFERENCE_CONVERSION_HPP