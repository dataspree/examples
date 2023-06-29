#find_package(opencv REQUIRED)
find_package(OpenCV REQUIRED)
find_package(nlohmann_json CONFIG)
find_package(Boost REQUIRED)

add_executable(TcpTutorialPlayground main.cpp)
target_link_libraries(TcpTutorialPlayground PUBLIC project_options project_warnings nlohmann_json::nlohmann_json opencv::opencv Boost::boost ${OpenCV_LIBS})
