cmake_minimum_required(VERSION 3.19)
project(Penelope VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)

# Extra CMake Modules (for KDE macros)
find_package(ECM 6.0 REQUIRED NO_MODULE)
set(CMAKE_MODULE_PATH ${ECM_MODULE_PATH})

include(KDEInstallDirs)
include(KDECMakeSettings)

# Qt
find_package(Qt6 6.6 REQUIRED COMPONENTS Core Gui Widgets PrintSupport Network)
qt_standard_project_setup()

# KDE Frameworks
find_package(KF6CoreAddons REQUIRED)
find_package(KF6I18n REQUIRED)
find_package(KF6XmlGui REQUIRED)
find_package(KF6WidgetsAddons REQUIRED)
find_package(KF6IconThemes REQUIRED)
find_package(KF6Config REQUIRED)
find_package(KF6ConfigWidgets REQUIRED)
find_package(KF6KIO REQUIRED)
find_package(KF6SyntaxHighlighting REQUIRED)

# Optional: single-instance via DBus (Linux/KDE)
find_package(KF6DBusAddons QUIET)
if(KF6DBusAddons_FOUND)
    add_compile_definitions(HAVE_KDBUSSERVICE)
endif()

# PDF rendering pipeline dependencies (Phase 4)
find_package(Poppler REQUIRED COMPONENTS Qt6)
find_package(Freetype REQUIRED)
find_package(ICU REQUIRED COMPONENTS uc i18n)
find_package(ZLIB REQUIRED)

include(FindPkgConfig)
pkg_check_modules(FONTCONFIG REQUIRED fontconfig)
pkg_check_modules(HARFBUZZ REQUIRED harfbuzz)
pkg_check_modules(HARFBUZZ_ICU REQUIRED harfbuzz-icu)
pkg_check_modules(HARFBUZZ_SUBSET REQUIRED harfbuzz-subset)

add_subdirectory(src)

# ── Packaging (CPack) ─────────────────────────────────────────────────
set(CPACK_PACKAGE_NAME "penelope")
set(CPACK_PACKAGE_VERSION "${PROJECT_VERSION}")
set(CPACK_PACKAGE_DESCRIPTION_SUMMARY "A beautiful paginated markdown reader")
set(CPACK_PACKAGE_CONTACT "clintonthegeek")
set(CPACK_PACKAGE_HOMEPAGE_URL "https://github.com/clintonthegeek/Penelope")
set(CPACK_RESOURCE_FILE_LICENSE "${CMAKE_SOURCE_DIR}/LICENSE")

set(CPACK_GENERATOR "DEB")
set(CPACK_DEBIAN_PACKAGE_SECTION "text")
set(CPACK_DEBIAN_FILE_NAME DEB-DEFAULT)
set(CPACK_DEBIAN_PACKAGE_SHLIBDEPS ON)

include(CPack)
