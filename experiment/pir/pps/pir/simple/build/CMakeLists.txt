cmake_minimum_required (VERSION 3.8)

project (SIMPLEPIR)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_STANDARD 11)

link_directories("/usr/local/lib")
add_executable (SIMPLEPIR_DEMO ../pir_test.cpp ../include/generate_rand.cpp ../include/inner_product.cpp ../include/data_transmit.cpp 
../include/pir_server.cpp ../include/pir_client.cpp)

target_link_libraries(SIMPLEPIR_DEMO pthread)