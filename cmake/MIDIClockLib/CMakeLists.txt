cmake_minimum_required(VERSION 3.5)

set(TARGET_NAME MIDIClock)
project(${TARGET_NAME})

set(CMAKE_INCLUDE_CURRENT_DIR_IN_INTERFACE ON)

set(BASE_DIR ${CMAKE_CURRENT_SOURCE_DIR}/../../MIDIClockLib8.0)
set(GLOBAL_SRC_DIR ${BASE_DIR})

set(src_list ${BASE_DIR}/MIDIClock.c)

# if(WIN32) set(extra_srcs # ${BASE_DIR}/src/common/nsfsdk/npnez.def #
# ${BASE_DIR}/src/ui/nezplug/nezplug.rc ) else() message(FATAL_ERROR "currentry
# not supported") endif()

add_library(${TARGET_NAME}_static STATIC ${src_list} ${extra_srcs})
add_library(${TARGET_NAME} SHARED ${src_list} ${extra_srcs})

if(WIN32)
  target_link_libraries(${TARGET_NAME} winmm)
endif()

set_target_properties(${TARGET_NAME} PROPERTIES PUBLIC_HEADER
                                                ${BASE_DIR}/MIDIClock.h)
set_target_properties(${TARGET_NAME}_static PROPERTIES PUBLIC_HEADER
                                                       ${BASE_DIR}/MIDIClock.h)

install(
  TARGETS ${TARGET_NAME}_static
  EXPORT ${TARGET_NAME}_static_export
  LIBRARY DESTINATION lib
  INCLUDES
  DESTINATION include
  PUBLIC_HEADER DESTINATION include)
install(
  TARGETS ${TARGET_NAME}
  EXPORT ${TARGET_NAME}_export
  RUNTIME DESTINATION bin
  INCLUDES
  DESTINATION include
  PUBLIC_HEADER DESTINATION include)

install(
  EXPORT ${TARGET_NAME}_static_export
  FILE ${TARGET_NAME}_staticConfig.cmake
  DESTINATION cmake)
install(
  EXPORT ${TARGET_NAME}_export
  FILE ${TARGET_NAME}Config.cmake
  DESTINATION cmake)
