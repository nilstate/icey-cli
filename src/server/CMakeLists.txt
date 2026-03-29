add_executable(icey-server
  internal/app.cpp
  internal/config.cpp
  internal/httpfactory.cpp
  internal/media.cpp
  internal/turnserver.cpp
  media-server.cpp
)

target_link_libraries(icey-server PRIVATE
  icey::base icey::crypto icey::net icey::http icey::json
  icey::symple icey::av icey::speech icey::stun icey::turn icey::vision icey::webrtc
  icey::webrtc_support
)

target_compile_definitions(icey-server PRIVATE
  ICEY_SERVER_VERSION="${PROJECT_VERSION}"
)

if(MSVC)
  target_compile_options(icey-server PRIVATE /W4)
  if(ENABLE_WARNINGS_ARE_ERRORS)
    target_compile_options(icey-server PRIVATE /WX)
  endif()
elseif(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
  target_compile_options(icey-server PRIVATE -Wall -Wextra -Wno-unused-parameter)
  if(ENABLE_WARNINGS_ARE_ERRORS)
    target_compile_options(icey-server PRIVATE -Werror)
  endif()
endif()

target_include_directories(icey-server PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}/internal
)

# Copy config to build directory
configure_file(
  ${PROJECT_SOURCE_DIR}/config.json
  ${CMAKE_CURRENT_BINARY_DIR}/config.json COPYONLY)

install(TARGETS icey-server
  RUNTIME DESTINATION bin
  COMPONENT apps)
