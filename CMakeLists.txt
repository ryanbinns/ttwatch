cmake_minimum_required (VERSION 2.8)
project (TTWatch C CXX)

option(unsafe "Enable unsafe operations" off)
option(daemon "Enable daemon operations" on)

if(unsafe)
  message( STATUS "Enabled unsafe operations")
  add_definitions(-DUNSAFE)
endif(unsafe)

if(daemon)
  message( STATUS "Enabled daemon function")
  add_definitions(-DDAEMON)
endif(daemon)

include(TestBigEndian)
test_big_endian(is_big_endian)
if(is_big_endian)
  add_definitions(-DTT_BIG_ENDIAN)
else()
  add_definitions(-DTT_LITTLE_ENDIAN)
endif(is_big_endian)

include_directories(include src ${CMAKE_CURRENT_BINARY_DIR})

set(CMAKE_MODULE_PATH ${CMAKE_MODULE_PATH} "${CMAKE_CURRENT_SOURCE_DIR}/cmake_modules/")
find_package(CURL)
find_package(OpenSSL)
find_package(LibUSB)

include_directories(${LIBUSB_1_INCLUDE_DIRS} ${OPENSSL_INCLUDE_DIR} ${CURL_INCLUDE_DIRS})

set(LIBTTWATCH_SRC src/libttwatch.cpp src/libttwatch_cpp.cpp)
add_library(libttwatch STATIC ${LIBTTWATCH_SRC})
set_target_properties(libttwatch PROPERTIES OUTPUT_NAME ttwatch)

set(TTBIN_SRC src/export_csv.c src/export_gpx.c src/export_kml.c src/export_tcx.c src/ttbin.c)
add_library(libttbin STATIC ${TTBIN_SRC})
target_link_libraries(libttbin ${CURL_LIBRARIES})
set_target_properties(libttbin PROPERTIES OUTPUT_NAME ttbin)

add_executable(ttbincnv src/ttbincnv.c)
target_link_libraries(ttbincnv libttbin m)

add_executable(ttbinmod src/ttbinmod.c)
target_link_libraries(ttbinmod libttbin m ${CURL_LIBRARIES})

set(MANIFEST_DIR "${CMAKE_CURRENT_SOURCE_DIR}/manifest")

add_custom_target(manifest
  DEPENDS manifest_definitions.h manifest_definitions_00010113.h manifest_definitions_0001082e.h)
add_custom_command(OUTPUT manifest_definitions.h
  DEPENDS ${MANIFEST_DIR}/manifest.txt ${MANIFEST_DIR}/make_manifest.pl
  COMMAND ${MANIFEST_DIR}/make_manifest.pl < ${MANIFEST_DIR}/manifest.txt > manifest_definitions.h)

add_custom_command(OUTPUT manifest_definitions_00010113.h
  DEPENDS ${MANIFEST_DIR}/manifest_00010113.txt ${MANIFEST_DIR}/make_manifest.pl
  COMMAND ${MANIFEST_DIR}/make_manifest.pl < ${MANIFEST_DIR}/manifest_00010113.txt > manifest_definitions_00010113.h)

add_custom_command(OUTPUT manifest_definitions_0001082e.h
  DEPENDS ${MANIFEST_DIR}/manifest_0001082e.txt ${MANIFEST_DIR}/make_manifest.pl
  COMMAND ${MANIFEST_DIR}/make_manifest.pl < ${MANIFEST_DIR}/manifest_0001082e.txt > manifest_definitions_0001082e.h)

set(TTWATCH_SRC src/ttwatch.c src/log.c src/options.c src/json.c)
add_executable(ttwatch ${TTWATCH_SRC})
target_link_libraries(ttwatch libttwatch libttbin ${LIBUSB_1_LIBRARIES} ${OPENSSL_LIBRARIES})
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
