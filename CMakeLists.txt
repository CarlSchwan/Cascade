# SPDX-FileCopyrightText: 2025 Carl Schwan <carl@carlschwan.eu>
# SPDX-License-Identifier: BSD-2-Clause

cmake_minimum_required(VERSION 3.20)

project(cascade VERSION 1.0.0)

set(KF_MIN_VERSION "6.12.0")
set(QT_MIN_VERSION "6.8.0")

include(FeatureSummary)

find_package(ECM ${KF_MIN_VERSION} NO_MODULE)
set_package_properties(ECM PROPERTIES
    TYPE REQUIRED
    URL "https://api.kde.org/ecm/"
    DESCRIPTION "extra cmake modules")

set(CMAKE_MODULE_PATH ${ECM_MODULE_PATH} ${CMAKE_SOURCE_DIR}/cmake)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(Qt6 ${QT_MIN_VERSION} NO_MODULE COMPONENTS
    Widgets
    Core
)
set_package_properties(Qt6 PROPERTIES
    TYPE REQUIRED
    PURPOSE "Required application components"
)

find_package(KF6 ${KF_MIN_VERSION} COMPONENTS I18n Config CoreAddons ColorScheme IconThemes)
set_package_properties(KF6 PROPERTIES
    TYPE REQUIRED
    PURPOSE "Required application components"
)

find_package(OpenImageIO REQUIRED)
find_package(OpenColorIO REQUIRED)
find_package(SPIRV-Tools REQUIRED)
find_package(glslang REQUIRED)
find_package(TBB REQUIRED)

feature_summary(WHAT ALL INCLUDE_QUIET_PACKAGES FATAL_ON_MISSING_REQUIRED_PACKAGES)

include(ECMSetupVersion)
include(KDEInstallDirs)
include(KDEClangFormat)
include(ECMQtDeclareLoggingCategory)
include(KDECMakeSettings NO_POLICY_SCOPE)
#include(KDECompilerSettings NO_POLICY_SCOPE)
include(ECMAddTests)
include(KDEGitCommitHooks)
include(ECMDeprecationSettings)
include(ECMCheckOutboundLicense)

ecm_setup_version(${PROJECT_VERSION}
    VARIABLE_PREFIX CASCADE
    VERSION_HEADER ${CMAKE_CURRENT_BINARY_DIR}/cascade-version.h
)

add_subdirectory(src)

ki18n_install(po)

file(GLOB_RECURSE ALL_CLANG_FORMAT_SOURCE_FILES src/*.cpp src/*.h)
kde_clang_format(${ALL_CLANG_FORMAT_SOURCE_FILES})

#kde_configure_git_pre_commit_hook(CHECKS CLANG_FORMAT)

#file(GLOB_RECURSE ALL_SOURCE_FILES *.cpp *.h *.qml)
## CI installs dependency headers to _install and _build, which break the reuse check
## Fixes the test by excluding this directory
#list(FILTER ALL_SOURCE_FILES EXCLUDE REGEX [[.*(install|build).*/.*]])
#ecm_check_outbound_license(LICENSES GPL-3.0-only FILES ${ALL_SOURCE_FILES})