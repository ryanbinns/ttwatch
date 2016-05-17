cmake_minimum_required (VERSION 2.8)
project (TTWatch C CXX)

option(unsafe "Enable unsafe operations" off)

if(unsafe)
  message( STATUS "Enabled unsafe operations")
  add_definitions(-DUNSAFE)
endif(unsafe)

include_directories(include)
include_directories(src)
include_directories(manifest)

set(CMAKE_MODULE_PATH ${CMAKE_MODULE_PATH} "${CMAKE_CURRENT_SOURCE_DIR}/cmake_modules/")
find_package(CURL)
find_package(OpenSSL)
find_package(LibUSB)

include_directories(${LIBUSB_1_INCLUDE_DIRS} ${OPENSSL_INCLUDE_DIR} ${CURL_INCLUDE_DIRS})

set(LIBTTWATCH_SRC src/libttwatch.cpp src/libttwatch_cpp.cpp)
add_library(ttwatch-common STATIC ${LIBTTWATCH_SRC})

set(TTBIN_SRC src/export_csv.c src/export_gpx.c src/export_kml.c src/export_tcx.c src/ttbin.c)
add_library(ttbin STATIC ${TTBIN_SRC})
target_link_libraries(ttbin ${CURL_LIBRARIES})

add_executable(ttbincnv src/ttbincnv.c)
target_link_libraries(ttbincnv ttbin m)

add_executable(ttbinmod src/ttbinmod.c)
target_link_libraries(ttbinmod ttbin m ${CURL_LIBRARIES})

set(TTWATCH_SRC src/ttwatch.c src/log.c src/options.c)
add_executable(ttwatch ${TTWATCH_SRC})
target_link_libraries(ttwatch ttwatch-common ttbin ${LIBUSB_1_LIBRARIES} ${OPENSSL_LIBRARIES})
add_custom_target(manifest
  COMMAND ./make_manifest.pl < manifest.txt > manifest_definitions.h
  COMMAND ./make_manifest.pl < manifest_00010113.txt > manifest_definitions_00010113.h
  COMMAND ./make_manifest.pl < manifest_0001082e.txt > manifest_definitions_0001082e.h
  WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/manifest)
add_dependencies(ttwatch manifest)

install(TARGETS ttwatch ttbincnv ttbinmod DESTINATION bin)
install(FILES ttbin2mysports DESTINATION bin)

if(${CMAKE_SYSTEM_NAME} EQUAL "Linux")
  add_custom_target(install_udev
    COMMAND cp 99-tomtom.rules /etc/udev/rules.d
    COMMAND udevadm control --reload-rules
    COMMAND addgroup usb
    COMMAND usermod -a -Gusb `logname`
    WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR})
endif()
