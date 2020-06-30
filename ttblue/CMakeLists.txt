cmake_minimum_required (VERSION 2.8)
project (TTBlue C)

add_executable(ttblue ttblue.c bbatt.c ttops.c util.c version.c bbatt.h
  ttops.h att-types.h util.h version.h)
target_link_libraries(ttblue curl bluetooth popt)
set_target_properties(ttblue PROPERTIES COMPILE_FLAGS
  "--std=c99 -O2 -Wall -Wtype-limits -Wno-missing-braces")

add_custom_target(setcap
  COMMAND echo 'This will give ttblue permissions to create raw'
  COMMAND echo 'network sockets and thereby improve the speed of'
  COMMAND echo 'the BLE connection.'
  COMMAND sudo setcap 'cap_net_raw,cap_net_admin+eip' ttblue)

install(TARGETS ttblue DESTINATION bin)
