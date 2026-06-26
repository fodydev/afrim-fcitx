cmake_minimum_required(VERSION 3.19)
project(afrim_fcitx5 VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# ── Dependencies ─────────────────────────────────────────────────────────────
find_package(PkgConfig REQUIRED)
find_package(ECM REQUIRED 1.0.0)
list(APPEND CMAKE_MODULE_PATH ${ECM_MODULE_PATH})

find_package(Fcitx5Core REQUIRED)

include(GNUInstallDirs)
include(FeatureSummary)

# ── Rust / Cargo build ───────────────────────────────────────────────────────
# Feature selection:
#   cmake .. -DAFRIM_INHIBIT=ON          # enable inhibit for non-Latin scripts
#   cmake .. -DAFRIM_FEATURES="rhai"     # override extra features
option(AFRIM_INHIBIT
    "Build with the 'inhibit' feature (suppresses intermediate Latin chars)"
    OFF)
set(AFRIM_FEATURES "rhai,strsim"
    CACHE STRING "Comma-separated extra Cargo features (appended to defaults)")

# Build the full feature string.
if(AFRIM_INHIBIT)
    set(_CARGO_FEATURES "inhibit,${AFRIM_FEATURES}")
else()
    set(_CARGO_FEATURES "${AFRIM_FEATURES}")
endif()

set(CARGO_MANIFEST  "${CMAKE_CURRENT_SOURCE_DIR}/Cargo.toml")
set(RUST_STATICLIB  "${CMAKE_CURRENT_SOURCE_DIR}/target/release/libafrim_fcitx5.a")

add_custom_command(
    OUTPUT "${RUST_STATICLIB}"
    COMMAND cargo build
            --release
            --manifest-path "${CARGO_MANIFEST}"
            --no-default-features
            --features "${_CARGO_FEATURES}"
    WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
    COMMENT "Compiling Rust afrim-fcitx5 static library…"
    VERBATIM
)
add_custom_target(afrim_rust_build ALL DEPENDS "${RUST_STATICLIB}")

# ── C++ fcitx5 shared library ─────────────────────────────────────────────────
add_library(afrim-fcitx5 MODULE
    fcitx5/AfrimEngine.cpp
)
set_target_properties(afrim-fcitx5 PROPERTIES PREFIX "")

add_dependencies(afrim-fcitx5 afrim_rust_build)

target_include_directories(afrim-fcitx5 PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/fcitx5"
)

target_link_libraries(afrim-fcitx5 PRIVATE
    Fcitx5::Core
    "${RUST_STATICLIB}"
    # System libraries needed by the Rust standard library.
    pthread
    dl
    m
)

# ── Installation ─────────────────────────────────────────────────────────────
install(TARGETS afrim-fcitx5
    LIBRARY DESTINATION "${CMAKE_INSTALL_LIBDIR}/fcitx5"
)

install(FILES addon/afrim.conf
    DESTINATION "${CMAKE_INSTALL_DATADIR}/fcitx5/addon"
)

install(FILES inputmethod/afrim.conf
    DESTINATION "${CMAKE_INSTALL_DATADIR}/fcitx5/inputmethod"
)

feature_summary(WHAT ALL)
