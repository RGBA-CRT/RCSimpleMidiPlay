cmake_minimum_required(VERSION 3.5)

set(TARGET_NAME MIDIIO)
project(${TARGET_NAME})

set(BASE_DIR ${CMAKE_CURRENT_SOURCE_DIR}/../../MIDIIOLib8.0)
set(GLOBAL_SRC_DIR ${BASE_DIR})

set(src_list ${BASE_DIR}/MIDIIO.c)

# if(WIN32) set(extra_srcs # ${BASE_DIR}/src/common/nsfsdk/npnez.def #
# ${BASE_DIR}/src/ui/nezplug/nezplug.rc ) else() message(FATAL_ERROR "currentry
# not supported") endif()

add_library(${TARGET_NAME}_static STATIC ${src_list} ${extra_srcs})
add_library(${TARGET_NAME} SHARED ${src_list} ${extra_srcs})

if(WIN32)
  target_link_libraries(${TARGET_NAME} winmm)
endif()

set_target_properties(${TARGET_NAME} PROPERTIES PUBLIC_HEADER
                                                ${BASE_DIR}/MIDIIO.h)

install(
  TARGETS ${TARGET_NAME}_static
  EXPORT ${TARGET_NAME}_static
  LIBRARY DESTINATION lib)
install(
  TARGETS ${TARGET_NAME}
  EXPORT ${TARGET_NAME}
  RUNTIME DESTINATION bin
  PUBLIC_HEADER DESTINATION include)

install(
  EXPORT ${TARGET_NAME}_static
  FILE ${TARGET_NAME}_static-config.cmake
  DESTINATION cmake)
install(
  EXPORT ${TARGET_NAME}
  FILE ${TARGET_NAME}-config.cmake
  DESTINATION cmake)
