include(InstallRequiredSystemLibraries)

# For help take a look at:
# http://www.cmake.org/Wiki/CMake:CPackConfiguration

### general settings
set(CPACK_PACKAGE_NAME ${APPLICATION_NAME})
set(CPACK_PACKAGE_VENDOR "${APPLICATION_NAME} developers")
set(CPACK_PACKAGE_DESCRIPTION_SUMMARY "The SSH library")
set(CPACK_PACKAGING_INSTALL_PREFIX "libssh")
set(CPACK_PACKAGE_VENDOR "The SSH Libaray Development Team")

set(CPACK_RESOURCE_FILE_LICENSE "${CMAKE_SOURCE_DIR}/COPYING")
set(CPACK_TOPLEVEL_DIRECTORY ${CMAKE_BINARY_DIR}/nsis)

### versions
set(CPACK_PACKAGE_VERSION_MAJOR "0")
set(CPACK_PACKAGE_VERSION_MINOR "2")
set(CPACK_PACKAGE_VERSION_PATCH "90")
set(CPACK_PACKAGE_VERSION "${CPACK_PACKAGE_VERSION_MAJOR}.${CPACK_PACKAGE_VERSION_MINOR}.${CPACK_PACKAGE_VERSION_PATCH}")

### source package settings
set(CPACK_SOURCE_GENERATOR "TGZ")
set(CPACK_SOURCE_IGNORE_FILES "~$;[.]swp$;/[.]svn/;/[.]git/;.gitignore;/build/;tags;cscope.*")
set(CPACK_SOURCE_PACKAGE_FILE_NAME "${CPACK_PACKAGE_NAME}-${CPACK_PACKAGE_VERSION}")

### install dirs
# C:\Program Files\name
set(CPACK_PACKAGE_INSTALL_DIRECTORY ${CPACK_PACKAGE_NAME})

### nsis
set(CPACK_GENERATOR "NSIS")

set(CPACK_NSIS_DISPLAY_NAME "The SSH Library")
#set(CPACK_NSIS_COMPRESSOR "/SOLID lzma")
set(CPACK_NSIS_COMPRESSOR "/SOLID zlib")

set(CPACK_COMPONENT_LIBRARIES_DISPLAY_NAME "Libraries")
set(CPACK_COMPONENT_HEADERS_DISPLAY_NAME "C/C++ Headers")

set(CPACK_COMPONENT_LIBRARIES_DESCRIPTION
  "Libraries used to build programs which use libssh")
set(CPACK_COMPONENT_HEADERS_DESCRIPTION
  "C/C++ header files for use with libssh")

set(CPACK_COMPONENT_HEADERS_DEPENDS libraries)

#set(CPACK_COMPONENT_APPLICATIONS_GROUP "Runtime")
#set(CPACK_COMPONENT_LIBRARIES_GROUP "Development")
#set(CPACK_COMPONENT_HEADERS_GROUP "Development")

set(CPACK_COMPONENTS_ALL "libraries;headers")

set(CPACK_NSIS_MENU_LINKS "http://0xbadc0de.be/wiki/libssh:libssh" "libssh homepage")

include(CPack)
