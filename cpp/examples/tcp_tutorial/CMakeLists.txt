find_package(nlohmann_json CONFIG)
find_package(Boost REQUIRED)
find_package(OpenCV REQUIRED)

set(CMAKE_CXX_STANDARD 20)

add_executable(TcpTutorial main.cpp)
target_link_libraries( TcpTutorial PUBLIC project_options project_warnings nlohmann_json::nlohmann_json opencv::opencv Boost::boost ${OpenCV_LIBS})


