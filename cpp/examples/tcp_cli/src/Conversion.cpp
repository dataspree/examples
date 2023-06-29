#include <Conversion.hpp>

#include <dataspree/inference/core/Utils.hpp>
#include <dataspree/inference/core/Item.hpp>

#include <spdlog/spdlog.h>

#include <boost/beast/core/detail/base64.hpp>

#include <msgpack.hpp>
#include <nlohmann/json.hpp>

#include <opencv2/imgcodecs.hpp>
#include <opencv2/imgproc.hpp>

using json = nlohmann::json;

template<typename T> struct TempAppend;

[[nodiscard]] auto encodeImage(cv::Mat const &image,
  std::string const &encoding,
  dataspree::inference::EncodingMode encodingMode) -> dataspree::inference::core::Item;

[[nodiscard]] auto item_to_msgpack(dataspree::inference::core::Item &item, std::string preferredImageEncoding)
  -> msgpack::sbuffer;

auto item_to_msgpack(dataspree::inference::core::Item &item,
  msgpack::packer<msgpack::sbuffer> &packer,
  TempAppend<std::string> const &path,
  dataspree::inference::core::Item &rootItem,
  std::string preferredImageEncoding) -> void;
auto msgpack_to_item(dataspree::inference::core::Item &rootItem, msgpack::v3::object const &root_result) -> void;

[[nodiscard]] auto item_to_json(dataspree::inference::core::Item &item, std::string preferredImageEncoding) -> json;

auto item_to_json(dataspree::inference::core::Item &item,
  json &json_object,
  TempAppend<std::string> const &path,
  dataspree::inference::core::Item &rootItem,
  std::string preferredImageEncoding) -> void;
auto json_to_item(dataspree::inference::core::Item &rootItem, json const &root_result) -> void;

[[nodiscard]] auto base64_decode(std::string const &src) -> std::vector<char>;

[[nodiscard]] auto base64_encode(std::vector<unsigned char> const &src) -> std::vector<char>;

auto dataspree::inference::encodeItem(core::Item &item, EncodingMode encodingMode, std::string preferredImageEncoding)
  -> Buffer {
  switch (encodingMode) {
  case EncodingMode::MSGPACK: {
    auto msg = item_to_msgpack(item, preferredImageEncoding);
    return Buffer{ std::move(msg) };
  }
  case EncodingMode::JSON: {
    auto json_root = item_to_json(item, preferredImageEncoding);
    auto msg = json_root.dump();
    return Buffer{ std::move(msg) };
  }
  default:
    throw std::runtime_error(
      fmt::format("Encoding mode {} not implemented.", dataspree::inference::core::getUnderlyingValue(encodingMode)));
  }
}

auto opencvToNumpy(int depth) -> char {
  switch (depth) {
  case CV_8U:
    [[fallthrough]];
  case CV_16U:
    return 'u';

  case CV_8S:
    [[fallthrough]];
  case CV_16S:
    [[fallthrough]];
  case CV_32S:
    return 'i';

  case CV_32F:
    [[fallthrough]];
  case CV_64F:
  //  [[fallthrough]];
  //case CV_16F:
    return 'f';

  default:
    return 0;
  }
}


auto numpyToOpencv(char kind, uint8_t size, uint64_t channels) -> std::pair<std::size_t, std::size_t> {
  switch (kind) {
  case 'b':
    return { CV_MAKETYPE(cv::DataType<bool>::type, channels), sizeof(bool) };

  case 'i':
    switch (size) {
    case 1:
      return { CV_MAKETYPE(cv::DataType<int8_t>::type, channels), sizeof(int8_t) };
    case 2:
      return { CV_MAKETYPE(cv::DataType<int16_t>::type, channels), sizeof(int16_t) };
    case 4:
      return { CV_MAKETYPE(cv::DataType<int32_t>::type, channels), sizeof(int32_t) };
    default:
      return { 0, 0 };
    }

  case 'u':
    switch (size) {
    case 1:
      return { CV_MAKETYPE(cv::DataType<uint8_t>::type, channels), sizeof(uint8_t) };
    case 2:
      return { CV_MAKETYPE(cv::DataType<uint16_t>::type, channels), sizeof(uint16_t) };
    default:
      return { 0, 0 };
    }

  case 'f':
    switch (size) {
    //case 2:
    //  return { CV_MAKETYPE(cv::DataType<cv::float16_t>::type, channels), sizeof(float) };
    case 4:
      return { CV_MAKETYPE(cv::DataType<float>::type, channels), sizeof(float) };
    case 8:
      return { CV_MAKETYPE(cv::DataType<double>::type, channels), sizeof(double) };
    default:
      return { 0, 0 };
    }

  default:
    return { 0, 0 };
  }
}

auto dataspree::inference::decodeItem(char const *const buffer,
  std::size_t const bufferSize,
  EncodingMode const encodingMode) -> core::Item {

  core::Item item{};
  if (encodingMode == EncodingMode::MSGPACK) {
    msgpack::unpacked upd{};
    msgpack::unpack(upd, buffer, bufferSize, nullptr);

    msgpack_to_item(item, upd.get());

  } else if (encodingMode == EncodingMode::JSON) {
    auto spannedSource = std::span(buffer, bufferSize);
    auto const parsed_result = json::parse(spannedSource.begin(), spannedSource.end());

    assert(parsed_result.is_object());
    json_to_item(item, parsed_result);

  } else {
    throw std::runtime_error(
      fmt::format("Encoding mode {} not implemented.", dataspree::inference::core::getUnderlyingValue(encodingMode)));
  }

  if (item.contains("item")) {

    auto &encodedElements = item.at<std::vector<dataspree::inference::core::Item>>("item", "encoded_elements");

    for (auto &&encodedElement : encodedElements) {
      auto const &encodedPath = encodedElement.at(0U);
      auto const encoding = encodedElement.at<std::string>(1U);

      auto content = &item["item"];
      for (auto &&p : encodedPath) {
        if (content == nullptr) { break; }
        content = content->find_at(p.at<std::string>());
      }

      if (content != nullptr) {
        try {
          auto &image = *content;
          auto &payload = image.as<std::string>();
          Buffer data = encodingMode == EncodingMode::JSON ? Buffer(base64_decode(payload)) : Buffer(payload);

          if (encoding == "MAT_RAW") {

            auto const type = dataspree::inference::core::as<char>(data.get());
            auto const type_size = dataspree::inference::core::as<uint8_t>(data.get(), 1);
            auto const dimensions = dataspree::inference::core::as<uint64_t>(data.get(), 2);

            if (dimensions == 3) {
              auto const width = dataspree::inference::core::as<uint64_t>(data.get(), 2 + sizeof(uint64_t) * 1);
              auto const height = dataspree::inference::core::as<uint64_t>(data.get(), 2 + sizeof(uint64_t) * 2);
              auto const channels = dataspree::inference::core::as<uint64_t>(data.get(), 2 + sizeof(uint64_t) * 3);

              auto const [cvType, size] = numpyToOpencv(type, type_size, channels);

              if (size == type_size and size != 0 and cvType > 0 and cvType <= std::numeric_limits<int>::max()) {

                if (width < std::numeric_limits<int>::max() && height < std::numeric_limits<int>::max()) {
                  cv::Mat mat(static_cast<int>(width), static_cast<int>(height), static_cast<int>(cvType));
                  memcpy(mat.data,
                    &data.get()[2 + sizeof(uint64_t) * 4],
                    static_cast<uint32_t>(width * height * channels * type_size));
                  cv::cvtColor(mat, mat, cv::COLOR_RGB2BGR);
                  image = mat;

                } else {
                  spdlog::warn("Could not decode image; width ({}) or height ({}) too large.", width, height);
                }

              } else {
                spdlog::warn("Could not decode raw mat: {} {} -> {} {}.", type, type_size, cvType, size);
              }
            } else {
              spdlog::warn("Could not decode raw mat: dimensions ({}) != 3.", dimensions);
            }


          } else {

            cv::Mat matImg;
            matImg = cv::imdecode(cv::Mat(1, static_cast<int>(data.size()), CV_8UC1, static_cast<void *>(data.get())),
              cv::IMREAD_UNCHANGED);
            image = matImg;
          }
        } catch (core::InvalidContent const &e) { spdlog::warn("Could not acquire image from payload {}.", e.what()); }
      } else {
        std::vector<std::string> path;
        for (auto &&p : encodedPath) { path.push_back(p.at<std::string>()); }
        spdlog::warn("Could not decode item {}", fmt::join(path, ", "));
      }
    }
  }

  return item;
}


auto encodeImage(cv::Mat const &image, std::string const &encoding, dataspree::inference::EncodingMode encodingMode)
  -> dataspree::inference::core::Item {

  std::vector<unsigned char> image_bytes{};
  if (encoding == "IMAGE_PNG" or encoding == "IMAGE_JSON") {
    cv::Mat dest{};
    cv::cvtColor(image, dest, cv::COLOR_BGR2RGB);

    cv::imencode(encoding == "IMAGE_PNG" ? ".png" : ".jpg", image, image_bytes);

  } else if (encoding == "MAT_RAW") {
    auto const width = image.rows;
    auto const height = image.cols;
    auto const channels = image.channels();
    auto const dataTypeSize = image.elemSize1();

    if (width < 0 || height < 0 || channels < 0 || dataTypeSize > std::numeric_limits<uint8_t>::max()) {
      return dataspree::inference::core::Item{};
    }
    cv::Mat transformedImage;
    cv::cvtColor(image, transformedImage, cv::COLOR_RGB2BGR);

    auto const imageDataSize =
      static_cast<uint64_t>(width) * static_cast<uint64_t>(height) * static_cast<uint64_t>(channels) * dataTypeSize;
    auto const dataSize = 2 + 8 * 4 + imageDataSize;
    image_bytes = std::vector<unsigned char>(dataSize);
    assert(image_bytes.size() == dataSize);

    if (auto const numpyType = opencvToNumpy(image.depth()); numpyType != 0) {
      image_bytes[0] = static_cast<unsigned char>(numpyType);

      image_bytes[1] = static_cast<uint8_t>(dataTypeSize);

      auto const lenShape = dataspree::inference::core::toLittleEndian(3);
      std::memcpy(&(image_bytes[2]), &lenShape, 8);

      auto const convWidth = dataspree::inference::core::toLittleEndian(width);
      std::memcpy(&(image_bytes[2 + 8]), &convWidth, 8);

      auto const convHeight = dataspree::inference::core::toLittleEndian(height);
      std::memcpy(&(image_bytes[2 + 8 * 2]), &convHeight, 8);

      auto const convChannels = dataspree::inference::core::toLittleEndian(channels);
      std::memcpy(&(image_bytes[2 + 8 * 3]), &convChannels, 8);

      auto imageDataStart = &(image_bytes[2 + 8 * 4]);
      std::memcpy(imageDataStart, transformedImage.data, imageDataSize);
      dataspree::inference::core::toLittleEndian(imageDataStart, imageDataSize, dataTypeSize);

    } else {
      spdlog::error("Type {} not convertible to numpy yet.", image.type());
      return dataspree::inference::core::Item{};
    }

  } else {

    spdlog::error("Encoding {} not implemented yet.", encoding);
    return dataspree::inference::core::Item{};
  }

  dataspree::inference::core::Item item;
  if (encodingMode == dataspree::inference::EncodingMode::JSON) {
    auto encoded = base64_encode(image_bytes);
    item = std::string(encoded.data(), encoded.size());
  } else {
    item = image_bytes;
  }
  return item;
}

// Structures for transforming data from json and msgpack representation into internal Item representation.

/// @dev: transform to iterative call.
// NOLINTNEXTLINE(misc-no-recursion)
auto json_to_item(dataspree::inference::core::Item &rootItem, json const &root_result) -> void {

  if (root_result.is_object()) {
#pragma unroll 10
    for (auto const &[key, value] : root_result.items()) { json_to_item(rootItem[key], value); }
  } else if (root_result.is_array()) {
    auto vec_item = std::vector<dataspree::inference::core::Item>{};

#pragma unroll 10
    for (auto const &value : root_result) {
      vec_item.emplace_back();
      json_to_item(vec_item[vec_item.size() - 1], value);
    }
    rootItem = vec_item;
  } else {

    if (root_result.is_number_float()) {
      rootItem = root_result.template get<double>();

    } else if (root_result.is_number_integer()) {
      rootItem = root_result.template get<int64_t>();

    } else if (root_result.is_number_unsigned()) {
      rootItem = root_result.template get<uint64_t>();

    } else if (root_result.is_null()) {
      rootItem = nullptr;

    } else if (root_result.is_string()) {
      rootItem = root_result.template get<std::string>();

    } else if (root_result.is_boolean()) {
      rootItem = root_result.template get<bool>();

    } else if (root_result.is_binary()) {
      spdlog::warn("Could not parse binary content {}.", root_result);
      rootItem = root_result;

    } else if (root_result.is_discarded()) {
      spdlog::warn("Could not parse discarded content {}.", root_result);
      rootItem = root_result;

    } else {
      spdlog::warn("Could not parse binary content {}.", root_result);
      rootItem = root_result;
    }
  }
}

// NOLINTNEXTLINE(altera-struct-pack-align)
template<typename T = std::string> struct [[nodiscard]] TempAppend final {

  explicit inline TempAppend(std::vector<T> *data) noexcept : data(data), start(true) {}

  explicit inline TempAppend(TempAppend<T> const &other, std::string const &branch) noexcept
    : TempAppend(other.data, branch) {}

  explicit inline TempAppend(std::vector<T> *data, std::string const &branch) noexcept : data(data) {
    data->push_back(branch);
  }

  TempAppend(TempAppend<T> const &other) = delete;

  TempAppend(TempAppend<T> &&other) = delete;

  inline auto operator=(TempAppend<T> const &other) noexcept -> TempAppend<T> & = delete;

  inline auto operator=(TempAppend<T> &&other) noexcept -> TempAppend<T> & = delete;

  constexpr ~TempAppend() noexcept {
    if (this->data != nullptr) {
      if (!start) {
        data->pop_back();
      } else {
        assert(data->size() == startSize);
      }
    }
  }

  [[nodiscard]] auto begin() const { return this->data->begin(); }

  [[nodiscard]] auto end() const { return this->data->end(); }

  std::vector<T> *data;

  std::size_t const startSize = data->size();

  bool const start = false;
};

// NOLINTNEXTLINE(misc-no-recursion)
auto item_to_json(dataspree::inference::core::Item &item,
  json &json_object,
  TempAppend<std::string> const &path,
  dataspree::inference::core::Item &rootItem,
  std::string preferredImageEncoding) -> void {

  // NOLINTBEGIN(altera-unroll-loops)
  switch (item.getType()) {
  case dataspree::inference::core::ItemType::OBJECT:
    json_object = json::object();
    for (auto &[key, value] : item.items()) {

      if (std::addressof(item) != std::addressof(rootItem) || key != "encoded_elements") {
        TempAppend<std::string> const tap{ path, key };
        item_to_json(value, json_object[key.c_str()], tap, rootItem, preferredImageEncoding);
      }
    }
    break;

  case dataspree::inference::core::ItemType::ARRAY:

    json_object = json::array();
    for (auto &value : item) {
      json_object.push_back("");
      item_to_json(value, json_object[json_object.size() - 1], path, rootItem, preferredImageEncoding);
    }
    break;

  case dataspree::inference::core::ItemType::F32:
    json_object = item.template at<float>();
    break;

  case dataspree::inference::core::ItemType::F64:
    json_object = item.template at<double>();
    break;

  case dataspree::inference::core::ItemType::F128:
    json_object = item.template at<long double>();
    break;

  case dataspree::inference::core::ItemType::UINT8:
    json_object = item.template at<uint8_t>();
    break;

  case dataspree::inference::core::ItemType::UINT16:
    json_object = item.template at<uint16_t>();
    break;
  case dataspree::inference::core::ItemType::UINT32:
    json_object = item.template at<uint32_t>();
    break;

  case dataspree::inference::core::ItemType::UINT64:
    json_object = item.template at<uint64_t>();
    break;

  case dataspree::inference::core::ItemType::INT8:
    json_object = item.template at<int8_t>();
    break;

  case dataspree::inference::core::ItemType::INT16:
    json_object = item.template at<int16_t>();
    break;

  case dataspree::inference::core::ItemType::INT32:
    json_object = item.template at<int32_t>();
    break;

  case dataspree::inference::core::ItemType::INT64:
    json_object = item.template at<int64_t>();
    break;

  case dataspree::inference::core::ItemType::BOOL:
    json_object = item.template at<bool>();
    break;

  case dataspree::inference::core::ItemType::STRING:
    json_object = item.template at<std::string>();

    break;
  case dataspree::inference::core::ItemType::NULL_TERMINATED_STRING:
    json_object = item.template at<char const *>();
    break;

  case dataspree::inference::core::ItemType::BYTE_ARRAY:
    json_object = item.template at<std::vector<unsigned char>>();
    break;

  case dataspree::inference::core::ItemType::MAT: {
    auto const &mat = item.template at<cv::Mat>();

    auto &encodedElements = rootItem.at<std::vector<dataspree::inference::core::Item>>("encoded_elements");

    bool found{ false };
    std::string encoding = preferredImageEncoding.empty() ? "MAT_RAW" : preferredImageEncoding;
    for (auto &&encodedElement : encodedElements) {
      auto const &encodedPath = encodedElement.at(0U);

      if (encodedPath == *(path.data)) {
        encoding = encodedElement.at<std::string>(1U);
        found = true;
        break;
      }
    }

    // Write encoding into the item if not already there
    if (!found) {
      encodedElements.emplace_back(std::vector<dataspree::inference::core::Item>{
        dataspree::inference::core::Item{ *path.data }, dataspree::inference::core::Item{ encoding } });
    }
    dataspree::inference::core::Item encoded_item =
      encodeImage(mat, encoding, dataspree::inference::EncodingMode::JSON);
    item_to_json(encoded_item, json_object, path, encoded_item, preferredImageEncoding);

    break;
  }
  case dataspree::inference::core::ItemType::OTHER:
    [[fallthrough]];
  default:
    json_object = "[n/a]";
    spdlog::warn("Could not be deserialized \n");
    break;
  }
  // NOLINTEND(altera-unroll-loops)
}

auto item_to_json(dataspree::inference::core::Item &item, std::string preferredImageEncoding) -> json {
  json json_root{};
  std::vector<std::string> path;
  auto tap = TempAppend(&path);

  for (auto &[key, value] : item.items()) {
    item_to_json(value, json_root[key.c_str()], tap, value, preferredImageEncoding);
  }

  (void)item.at<std::vector<dataspree::inference::core::Item>>("item", "encoded_elements");
  item_to_json(
    item["item"]["encoded_elements"], json_root["item"]["encoded_elements"], tap, item, preferredImageEncoding);

  return json_root;
}


// NOLINTNEXTLINE(misc-no-recursion)
auto msgpack_to_item(dataspree::inference::core::Item &rootItem, msgpack::v3::object const &root_result) -> void {
  // xx NOLINTBEGIN(cppcoreguidelines-pro-type-union-access,altera-unroll-loops)

  switch (root_result.type) {
  case msgpack::v3::type::MAP:

    for (auto const &[key, value] : root_result.via.map) {
      auto const keyString = std::string(key.via.str.ptr, key.via.str.size);
      msgpack_to_item(rootItem[keyString], value);
    }
    break;

  case msgpack::v3::type::ARRAY: {
    auto vec_item = std::vector<dataspree::inference::core::Item>{};
    for (const auto &value : root_result.via.array) {
      vec_item.emplace_back();
      msgpack_to_item(vec_item[vec_item.size() - 1], value);
    }
    rootItem = vec_item;
    break;
  }

  case msgpack::v3::type::BIN:
    rootItem = std::string(root_result.via.bin.ptr, root_result.via.bin.size);
    break;

  case msgpack::v3::type::BOOLEAN:
    rootItem = root_result.via.boolean;
    break;

  case msgpack::v3::type::EXT:
    rootItem = std::string(root_result.via.ext.ptr, root_result.via.ext.size);
    break;

  case msgpack::v3::type::FLOAT32:
    rootItem = static_cast<float>(root_result.via.f64);
    break;

  case msgpack::v3::type::FLOAT64:
    rootItem = root_result.via.f64;
    break;

  case msgpack::v3::type::NEGATIVE_INTEGER:
    rootItem = root_result.via.i64;
    break;

  case msgpack::v3::type::NIL:
    rootItem = nullptr;
    break;

  case msgpack::v3::type::POSITIVE_INTEGER:
    rootItem = root_result.via.u64;
    break;

  case msgpack::v3::type::STR:
    rootItem = std::string(root_result.via.str.ptr, root_result.via.str.size);
    break;

  default:
    spdlog::warn("Could not parse content of type {}.", root_result.type);
    rootItem = nullptr;
    break;
  }
  // xx NOLINTEND(cppcoreguidelines-pro-type-union-access,altera-unroll-loops)
}

// NOLINTNEXTLINE(altera-unroll-loops, misc-no-recursion)
auto item_to_msgpack(dataspree::inference::core::Item &item,
  msgpack::packer<msgpack::sbuffer> &packer,
  TempAppend<std::string> const &path,
  dataspree::inference::core::Item &rootItem,
  std::string preferredImageEncoding) -> void {

  // NOLINTBEGIN(altera-unroll-loops)
  switch (item.getType()) {
  case dataspree::inference::core::ItemType::OBJECT:
    if (auto const mapSize = item.items().size(); mapSize < std::numeric_limits<uint32_t>::max()) {
      packer.pack_map(static_cast<uint32_t>(mapSize));
      for (auto &[key, value] : item.items()) {

        if (std::addressof(item) != std::addressof(rootItem) || key != "encoded_elements") {
          packer.pack(key);
          item_to_msgpack(value, packer, TempAppend<std::string>(path, key), rootItem, preferredImageEncoding);
        }
      }
    } else {
      constexpr std::string_view str = "[n/a] (Too large map)";
      static_assert(str.size() <= std::numeric_limits<uint32_t>::max());
      packer.pack_str(static_cast<uint32_t>(str.size()));
      packer.pack_str_body(str.data(), static_cast<uint32_t>(str.size()));
    }
    break;

  case dataspree::inference::core::ItemType::ARRAY:

    if (auto const vecSize = item.size(); vecSize < std::numeric_limits<uint32_t>::max()) {
      packer.pack_array(static_cast<uint32_t>(vecSize));
      for (auto &value : item) { item_to_msgpack(value, packer, path, rootItem, preferredImageEncoding); }
    } else {
      constexpr std::string_view str = "[n/a] (Too large vector)";
      static_assert(str.size() <= std::numeric_limits<uint32_t>::max());
      packer.pack_str(static_cast<uint32_t>(str.size()));
      packer.pack_str_body(str.data(), static_cast<uint32_t>(str.size()));
    }
    break;

  case dataspree::inference::core::ItemType::F32:
    packer.pack_float(item.template at<float>());
    break;

  case dataspree::inference::core::ItemType::F64:
    packer.pack_double(item.template at<double>());
    break;

  case dataspree::inference::core::ItemType::F128:
    packer.pack_double(static_cast<double>(item.template at<long double>()));
    break;

  case dataspree::inference::core::ItemType::UINT8:
    packer.pack_uint8(item.template at<uint8_t>());
    break;

  case dataspree::inference::core::ItemType::UINT16:
    packer.pack_uint16(item.template at<uint16_t>());
    break;
  case dataspree::inference::core::ItemType::UINT32:
    packer.pack_uint32(item.template at<uint32_t>());
    break;

  case dataspree::inference::core::ItemType::UINT64:
    packer.pack_uint64(item.template at<uint64_t>());
    break;

  case dataspree::inference::core::ItemType::INT8:
    packer.pack_int8(item.template at<int8_t>());
    break;

  case dataspree::inference::core::ItemType::INT16:
    packer.pack_int16(item.template at<int16_t>());
    break;

  case dataspree::inference::core::ItemType::INT32:
    packer.pack_int32(item.template at<int32_t>());
    break;

  case dataspree::inference::core::ItemType::INT64:
    packer.pack_int64(item.template at<int64_t>());
    break;

  case dataspree::inference::core::ItemType::BOOL:
    if (item.template at<bool>()) {
      packer.pack_true();
    } else {
      packer.pack_false();
    }
    break;

  case dataspree::inference::core::ItemType::STRING:
    if (auto const content = item.template at<std::string>(); content.size() < std::numeric_limits<uint32_t>::max()) {
      auto const stringSize = static_cast<uint32_t>(content.size());
      packer.pack_str(stringSize);
      packer.pack_str_body(content.c_str(), stringSize);
    } else {
      constexpr std::string_view str = "[n/a] (Too large string)";
      static_assert(str.size() <= std::numeric_limits<uint32_t>::max());
      packer.pack_str(static_cast<uint32_t>(str.size()));
      packer.pack_str_body(str.data(), static_cast<uint32_t>(str.size()));
    }
    break;
  case dataspree::inference::core::ItemType::NULL_TERMINATED_STRING: {
    auto const *const content = item.template at<char const *>();
    if (auto const strlen = std::strlen(content); strlen < std::numeric_limits<uint32_t>::max()) {
      auto const stringSize = static_cast<uint32_t>(strlen);
      packer.pack_str(stringSize);
      packer.pack_str_body(content, stringSize);
    } else {
      constexpr std::string_view str = "[n/a] (Too large string)";
      static_assert(str.size() <= std::numeric_limits<uint32_t>::max());
      packer.pack_str(static_cast<uint32_t>(str.size()));
      packer.pack_str_body(str.data(), static_cast<uint32_t>(str.size()));
    }
    break;
  }
  case dataspree::inference::core::ItemType::BYTE_ARRAY:
    if (auto const content = item.template at<std::vector<unsigned char>>();
        content.size() < std::numeric_limits<uint32_t>::max()) {
      auto const contentSize = static_cast<uint32_t>(content.size());
      // NOLINTNEXTLINE(cppcoreguidelines-pro-type-reinterpret-cast)
      packer.pack_bin_body(reinterpret_cast<char const *>(content.data()), contentSize);
    } else {
      constexpr std::string_view str = "[n/a] (Too large byte array)";
      static_assert(str.size() <= std::numeric_limits<uint32_t>::max());
      packer.pack_str(static_cast<uint32_t>(str.size()));
      packer.pack_str_body(str.data(), static_cast<uint32_t>(str.size()));
    }
    break;

  case dataspree::inference::core::ItemType::MAT: {
    auto const &mat = item.template at<cv::Mat>();

    bool found{ false };
    std::string encoding = preferredImageEncoding.empty() ? "MAT_RAW" : preferredImageEncoding;
    auto &encodedElements = rootItem.at<std::vector<dataspree::inference::core::Item>>("encoded_elements");

    /// NOLINTNEXTLINE(altera-unroll-loops)
    for (auto &&encodedElement : encodedElements) {
      auto const &encodedPath = encodedElement.at(0U);

      if (encodedPath == *(path.data)) {
        encoding = encodedElement.at<std::string>(1U);
        found = true;
        break;
      }
    }

    // Write encoding into the item if not already there
    if (!found) {
      encodedElements.emplace_back(std::vector<dataspree::inference::core::Item>{
        dataspree::inference::core::Item{ *path.data }, dataspree::inference::core::Item{ encoding } });
    }

    dataspree::inference::core::Item encoded_item =
      encodeImage(mat, encoding, dataspree::inference::EncodingMode::JSON);
    item_to_msgpack(encoded_item, packer, path, encoded_item, preferredImageEncoding);

    break;
  }
  case dataspree::inference::core::ItemType::OTHER:
    [[fallthrough]];
  default: {
    constexpr std::string_view str = "[n/a]";
    static_assert(str.size() <= std::numeric_limits<uint32_t>::max());
    packer.pack_str(static_cast<uint32_t>(str.size()));
    packer.pack_str_body(str.data(), static_cast<uint32_t>(str.size()));
    spdlog::warn("could not be deserialized.");
    break;
  }
  }
  // NOLINTEND(altera-unroll-loops)
}

auto item_to_msgpack(dataspree::inference::core::Item &item, std::string preferredImageEncoding) -> msgpack::sbuffer {
  msgpack::sbuffer sbuf;
  msgpack::packer<msgpack::sbuffer> packer(sbuf);

  std::vector<std::string> path;
  packer.pack_map(static_cast<uint32_t>(item.items().size()));
  for (auto &[key, value] : item.items()) {
    if (key != "item") {
      packer.pack(key);
      item_to_msgpack(value, packer, TempAppend(&path), value, preferredImageEncoding);
    }
  }

  if (item.contains("item")) {
    (void)item.at<std::vector<dataspree::inference::core::Item>>("item", "encoded_elements");
    assert(item["item"].contains("encoded_elements"));

    packer.pack("item");
    item_to_msgpack(item["item"], packer, TempAppend(&path), item["item"], preferredImageEncoding);

    packer.pack("encoded_elements");
    item_to_msgpack(item["item"]["encoded_elements"], packer, TempAppend(&path), item["item"], preferredImageEncoding);
  }

  return sbuf;
}

[[nodiscard]] auto base64_decode(std::string const &src) -> std::vector<char> {
  auto const targetSize = boost::beast::detail::base64::decoded_size(src.size());
  auto vec = std::vector<char>(targetSize);
  assert(vec.size() == targetSize);
  boost::beast::detail::base64::decode(static_cast<void *>(vec.data()), src.data(), src.size());
  assert(vec.size() == targetSize);
  return vec;
}

[[nodiscard]] auto base64_encode(std::vector<unsigned char> const &src) -> std::vector<char> {
  auto const targetSize = boost::beast::detail::base64::encoded_size(src.size());
  auto vec = std::vector<char>(targetSize);
  assert(vec.size() == targetSize);
  boost::beast::detail::base64::encode(
    static_cast<void *>(vec.data()), static_cast<void const *>(src.data()), src.size());
  assert(vec.size() == targetSize);
  return vec;
}
