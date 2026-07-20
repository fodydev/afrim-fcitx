cmake_minimum_required(VERSION 3.22)
project(afrim_fcitx5 VERSION 0.1.0 LANGUAGES C CXX)
set(AFRIM_LIBNAME libafrim)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

add_compile_options(-Wall -Wextra)

# Dependencies
find_package(PkgConfig REQUIRED)
find_package(ECM REQUIRED 1.0.0)
list(APPEND CMAKE_MODULE_PATH ${ECM_MODULE_PATH})

find_package(Fcitx5Core REQUIRED)

include(GNUInstallDirs)
include(FeatureSummary)
include(CPack)

find_package(Corrosion REQUIRED)

# Rust library

# Feature selection:
#   cmake .. -DAFRIM_FEATURES="rhai"     # override extra features
set(AFRIM_FEATURES "rhai,strsim"
    CACHE STRING "Comma-separated extra Cargo features (appended to defaults)")

# Import targets defined in a package or workspace manifest `Cargo.toml` file
corrosion_import_crate(
	MANIFEST_PATH "${CMAKE_CURRENT_SOURCE_DIR}/afrim/Cargo.toml"
	FEATURES ${AFRIM_FEATURES}
)
corrosion_experimental_cbindgen(
        TARGET afrim_capi
        HEADER_NAME afrim_ffi.h
)

# C++ library
add_library(${AFRIM_LIBNAME} MODULE
    "${CMAKE_CURRENT_SOURCE_DIR}/fcitx5/AfrimEngine.cpp"
)
set_target_properties(${AFRIM_LIBNAME} PROPERTIES PREFIX "")

add_dependencies(${AFRIM_LIBNAME} afrim_capi)

target_include_directories(${AFRIM_LIBNAME} PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/fcitx5"
)
target_link_libraries(${AFRIM_LIBNAME} PRIVATE
    Fcitx5::Core
    afrim_capi
)

# Installation
install(TARGETS ${AFRIM_LIBNAME}
    LIBRARY DESTINATION "${CMAKE_INSTALL_LIBDIR}/fcitx5"
)

configure_file("${CMAKE_CURRENT_SOURCE_DIR}/share/addon/afrim.conf.in" "${CMAKE_CURRENT_BINARY_DIR}/addon/afrim.conf")
install(FILES "${CMAKE_CURRENT_BINARY_DIR}/addon/afrim.conf"
    DESTINATION "${CMAKE_INSTALL_DATADIR}/fcitx5/addon"
)

configure_file("${CMAKE_CURRENT_SOURCE_DIR}/share/inputmethod/afrim.conf.in" "${CMAKE_CURRENT_BINARY_DIR}/inputmethod/afrim.conf")
install(FILES "${CMAKE_CURRENT_BINARY_DIR}/inputmethod/afrim.conf"
    DESTINATION "${CMAKE_INSTALL_DATADIR}/fcitx5/inputmethod"
)

feature_summary(WHAT ALL)
