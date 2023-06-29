project(DataspreeInference VERSION 0.0.1 DESCRIPTION "" HOMEPAGE_URL "https://www.data-spree.com/" LANGUAGES CXX C)

configure_file("Version.hpp.in" "${CMAKE_BINARY_DIR}/configured_files/include/dataspree/inference/Version.hpp" ESCAPE_QUOTES)

find_package(fmt CONFIG)
find_package(spdlog CONFIG)
find_package(OpenCV CONFIG)

add_library(${PROJECT_NAME} src/dataspree/inference/core/Exception.cpp include/dataspree/inference/core/Type.hpp)
target_link_libraries(${PROJECT_NAME} PUBLIC project_options project_warnings opencv::opencv_core
        ${OpenCV_LIBS}
        PRIVATE fmt::fmt spdlog::spdlog)
target_include_directories(${PROJECT_NAME} PUBLIC ${PROJECT_SOURCE_DIR}/include "${CMAKE_BINARY_DIR}/include")


add_library(Dataspree::Inference ALIAS ${PROJECT_NAME})




