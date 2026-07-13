cmake_minimum_required(VERSION 3.28...3.30)

include("${CMAKE_CURRENT_SOURCE_DIR}/cmake/common/bootstrap.cmake" NO_POLICY_SCOPE)

project(${_name} VERSION ${_version})

if(NOT WIN32)
  message(FATAL_ERROR "Steam Controller Gamepad Viewer native OBS source currently supports Windows only.")
endif()

include(compilerconfig)
include(defaults)
include(helpers)

add_library(${CMAKE_PROJECT_NAME} MODULE)

find_package(libobs REQUIRED)

if(TARGET plugin-support)
  target_link_libraries(plugin-support PUBLIC OBS::libobs)
endif()

target_sources(
  ${CMAKE_PROJECT_NAME}
  PRIVATE
    src/backend-manager.cpp
    src/backend-manager.hpp
    src/controller-source.cpp
    src/controller-source.hpp
    src/plugin-main.cpp
    src/svg-path.cpp
    src/svg-path.hpp
)

target_compile_features(${CMAKE_PROJECT_NAME} PRIVATE cxx_std_20)
target_compile_definitions(${CMAKE_PROJECT_NAME} PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN UNICODE _UNICODE)
target_link_libraries(${CMAKE_PROJECT_NAME} PRIVATE OBS::libobs gdiplus winhttp)

set_target_properties_plugin(${CMAKE_PROJECT_NAME} PROPERTIES OUTPUT_NAME ${_name})

install(FILES "${CMAKE_CURRENT_SOURCE_DIR}/LICENSE-OBS-PLUGIN" DESTINATION ${CMAKE_PROJECT_NAME})
install(FILES "${CMAKE_CURRENT_SOURCE_DIR}/../LICENSE" DESTINATION ${CMAKE_PROJECT_NAME})
install(FILES "${CMAKE_CURRENT_SOURCE_DIR}/../THIRD_PARTY_NOTICES.md" DESTINATION ${CMAKE_PROJECT_NAME})
install(FILES "${CMAKE_CURRENT_SOURCE_DIR}/OBS-PLUGIN.md" DESTINATION "." RENAME README.md)
install(FILES "${CMAKE_CURRENT_SOURCE_DIR}/LICENSE-OBS-PLUGIN" DESTINATION ".")
install(FILES "${CMAKE_CURRENT_SOURCE_DIR}/../LICENSE" DESTINATION ".")
install(FILES "${CMAKE_CURRENT_SOURCE_DIR}/../THIRD_PARTY_NOTICES.md" DESTINATION ".")
install(PROGRAMS "${CMAKE_CURRENT_SOURCE_DIR}/install-obs-plugin.bat" DESTINATION ".")

find_program(DOTNET_EXECUTABLE NAMES dotnet dotnet.exe REQUIRED)
set(BACKEND_PROJECT "${CMAKE_CURRENT_SOURCE_DIR}/../src/SteamControllerGamepadViewer/SteamControllerGamepadViewer.csproj")
set(BACKEND_PUBLISH_DIR "${CMAKE_CURRENT_BINARY_DIR}/backend-publish")
set(BACKEND_EXE "${BACKEND_PUBLISH_DIR}/SteamControllerGamepadViewer.exe")
file(
  GLOB_RECURSE BACKEND_SOURCES
  CONFIGURE_DEPENDS
  "${CMAKE_CURRENT_SOURCE_DIR}/../src/SteamControllerGamepadViewer/*.cs"
  "${CMAKE_CURRENT_SOURCE_DIR}/../src/SteamControllerGamepadViewer/*.csproj"
  "${CMAKE_CURRENT_SOURCE_DIR}/../src/SteamControllerGamepadViewer/wwwroot/*"
)
list(FILTER BACKEND_SOURCES EXCLUDE REGEX "/(bin|obj)/")

add_custom_command(
  OUTPUT "${BACKEND_EXE}"
  COMMAND "${DOTNET_EXECUTABLE}" publish "${BACKEND_PROJECT}" -c Release -r win-x64 --self-contained true
          -o "${BACKEND_PUBLISH_DIR}" -p:PublishSingleFile=true -p:IncludeNativeLibrariesForSelfExtract=true
          -p:DebugType=None -p:DebugSymbols=false -p:Version=${PROJECT_VERSION}
  COMMAND "${CMAKE_COMMAND}" -E rm -rf "${BACKEND_PUBLISH_DIR}/wwwroot"
  COMMAND "${CMAKE_COMMAND}" -E rm -f "${BACKEND_PUBLISH_DIR}/web.config"
  COMMAND "${CMAKE_COMMAND}" -E rm -f "${BACKEND_PUBLISH_DIR}/SteamControllerGamepadViewer.pdb"
  DEPENDS ${BACKEND_SOURCES}
  COMMENT "Publishing bundled Steam Controller backend"
  VERBATIM
)

add_custom_target(publish_backend ALL DEPENDS "${BACKEND_EXE}")
add_dependencies(${CMAKE_PROJECT_NAME} publish_backend)

install(PROGRAMS "${BACKEND_EXE}" DESTINATION "${CMAKE_PROJECT_NAME}/data/backend")

add_custom_command(
  TARGET ${CMAKE_PROJECT_NAME}
  POST_BUILD
  COMMAND "${CMAKE_COMMAND}" -E make_directory
          "${CMAKE_CURRENT_BINARY_DIR}/rundir/$<CONFIG>/${CMAKE_PROJECT_NAME}/backend"
  COMMAND "${CMAKE_COMMAND}" -E copy_if_different "${BACKEND_EXE}"
          "${CMAKE_CURRENT_BINARY_DIR}/rundir/$<CONFIG>/${CMAKE_PROJECT_NAME}/backend/SteamControllerGamepadViewer.exe"
  COMMENT "Copy bundled backend to rundir"
  VERBATIM
)
