cmake_minimum_required(VERSION 3.16)

set(GOTV_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../..")
file(STRINGS "${GOTV_ROOT}/VERSION" GOTV_VERSION LIMIT_COUNT 1)
project(gotv-fcitx5 VERSION "${GOTV_VERSION}" LANGUAGES C CXX)

find_package(ECM 1.0.0 REQUIRED)
find_package(Fcitx5Core REQUIRED)
find_package(PkgConfig REQUIRED)
pkg_check_modules(GIO REQUIRED IMPORTED_TARGET gio-2.0)

include(GNUInstallDirs)
include("${FCITX_INSTALL_CMAKECONFIG_DIR}/Fcitx5Utils/Fcitx5CompilerSettings.cmake")

file(GLOB GOTV_ENGINE_SOURCES CONFIGURE_DEPENDS "${GOTV_ROOT}/engine/*.c")

add_fcitx5_addon(gotv engine.cpp ${GOTV_ENGINE_SOURCES})
target_include_directories(gotv PRIVATE "${GOTV_ROOT}/engine")
target_link_libraries(gotv Fcitx5::Core PkgConfig::GIO m)

configure_file(gotv-inputmethod.conf.in gotv-inputmethod.conf @ONLY)
configure_file(gotv-addon.conf.in gotv-addon.conf @ONLY)

install(TARGETS gotv DESTINATION "${CMAKE_INSTALL_LIBDIR}/fcitx5")
install(FILES "${CMAKE_CURRENT_BINARY_DIR}/gotv-inputmethod.conf"
    DESTINATION "${CMAKE_INSTALL_DATADIR}/fcitx5/inputmethod"
    RENAME gotv.conf)
install(FILES "${CMAKE_CURRENT_BINARY_DIR}/gotv-addon.conf"
    DESTINATION "${FCITX_INSTALL_PKGDATADIR}/addon"
    RENAME gotv.conf)
