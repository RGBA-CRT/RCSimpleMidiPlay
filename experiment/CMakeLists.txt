cmake_minimum_required(VERSION 3.5)

set(TARGET_NAME experiment)
project(${TARGET_NAME})

set(BASE_DIR ${CMAKE_CURRENT_SOURCE_DIR})
set(GLOBAL_SRC_DIR ${BASE_DIR})

set(src_list ${BASE_DIR}/test.c)

# if(WIN32) set(extra_srcs # ${BASE_DIR}/src/common/nsfsdk/npnez.def #
# ${BASE_DIR}/src/ui/nezplug/nezplug.rc ) else() message(FATAL_ERROR "currentry
# not supported") endif()

find_package(MIDIClock_static REQUIRED)
find_package(MIDIIO_static REQUIRED)
find_package(MIDIData_static REQUIRED)
add_executable(${TARGET_NAME} ${src_list} ${extra_srcs})

target_link_libraries(${TARGET_NAME} MIDIClock_static MIDIIO_static MIDIData_static winmm)

install(
  TARGETS ${TARGET_NAME}
  RUNTIME DESTINATION bin
  PUBLIC_HEADER DESTINATION include)
