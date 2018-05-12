
# Default to release build type
IF(NOT CMAKE_BUILD_TYPE)
  SET(CMAKE_BUILD_TYPE "Release" CACHE INTERNAL "Release type")
ENDIF(NOT CMAKE_BUILD_TYPE)

message("CMAKE_SYSTEM_PROCESSOR: ${CMAKE_SYSTEM_PROCESSOR}")
message("CMAKE_GENERATOR: ${CMAKE_GENERATOR}")
message("CMAKE_C_FLAGS: ${CMAKE_C_FLAGS}")
message("WIN64: ${WIN64}")

IF(CMAKE_SYSTEM_PROCESSOR MATCHES AMD64|amd64.*|x86_64.* OR CMAKE_GENERATOR MATCHES "Visual Studio.*Win64")
  IF(CMAKE_C_FLAGS MATCHES -m32 OR CMAKE_CXX_FLAGS MATCHES -m32)
    SET(X86 1)
  ELSE(CMAKE_C_FLAGS MATCHES -m32 OR CMAKE_CXX_FLAGS MATCHES -m32)
    SET(X86_64 1)
  ENDIF(CMAKE_C_FLAGS MATCHES -m32 OR CMAKE_CXX_FLAGS MATCHES -m32)
ELSEIF(CMAKE_SYSTEM_PROCESSOR MATCHES i686.*|i386.*|x86.* OR WIN32)
  IF(CMAKE_C_FLAGS MATCHES -m64 OR CMAKE_CXX_FLAGS MATCHES -m64)
    SET(X86_64 1)
  ELSE(CMAKE_C_FLAGS MATCHES -m32 OR CMAKE_CXX_FLAGS MATCHES -m32)
    SET(X86 1)
  ENDIF(CMAKE_C_FLAGS MATCHES -m64 OR CMAKE_CXX_FLAGS MATCHES -m64)
ELSEIF(CMAKE_SYSTEM_PROCESSOR MATCHES arm.* AND CMAKE_SYSTEM_NAME STREQUAL "Linux")
    SET(ARM 1)
ELSEIF(CMAKE_SYSTEM_PROCESSOR MATCHES mips)
    SET(MIPS 1)
ENDIF()

IF ("${CMAKE_C_COMPILER_ID}" STREQUAL "Clang")
  # using Clang
  SET(CLANG 1)
ELSEIF ("${CMAKE_C_COMPILER_ID}" STREQUAL "TinyCC")
  # using TinyCC
  SET(TINYCC 1)
ELSEIF ("${CMAKE_C_COMPILER_ID}" STREQUAL "GNU")
  # using GCC
  SET(GCC 1)
ELSEIF ("${CMAKE_C_COMPILER_ID}" STREQUAL "Intel")
  # using Intel C++
  SET(INTELCC 1)
ELSEIF ("${CMAKE_C_COMPILER_ID}" STREQUAL "MSVC")
  # using Visual Studio C++
  SET(MSVC 1)
ELSEIF ("${CMAKE_C_COMPILER_ID}" STREQUAL "MIPSpro")
  # using SGI MIPSpro
  SET(MIPSPRO 1)
ENDIF()

# Set default libdir
IF(NOT DEFINED CMAKE_INSTALL_LIBDIR)
  SET(CMAKE_INSTALL_LIBDIR "lib" CACHE PATH "library destination directory")
ENDIF(NOT DEFINED CMAKE_INSTALL_LIBDIR)

# Set default bindir
IF(NOT DEFINED CMAKE_INSTALL_BINDIR)
  SET(CMAKE_INSTALL_BINDIR "bin" CACHE PATH "executable destination directory")
ENDIF(NOT DEFINED CMAKE_INSTALL_BINDIR)


# detect system type
IF(NOT DEFINED CPACK_SYSTEM_NAME)
  SET(CPACK_SYSTEM_NAME ${CMAKE_SYSTEM_NAME})
ENDIF(NOT DEFINED CPACK_SYSTEM_NAME)

IF (UNIX AND NOT WIN32)
  IF(X86)
    SET(CPACK_PACKAGE_ARCHITECTURE "i386")
  ELSEIF(X86_64)
    SET(CPACK_PACKAGE_ARCHITECTURE "x86_64")
  ELSEIF(ARM)
    SET(CPACK_PACKAGE_ARCHITECTURE "armhf")
  ELSE()
    SET(CPACK_PACKAGE_ARCHITECTURE ${CMAKE_SYSTEM_PROCESSOR})
  ENDIF()

  OPTION(MULTIARCH "Enable for multi-arch aware systems (debian)" ON)
  IF(MULTIARCH)
    IF(NOT DEFINED CMAKE_LIBRARY_ARCHITECTURE)
      SET(CMAKE_LIBRARY_ARCHITECTURE "${CPACK_PACKAGE_ARCHITECTURE}-linux-gnu")
    ENDIF(NOT DEFINED CMAKE_LIBRARY_ARCHITECTURE)
    SET(CMAKE_INSTALL_LIBDIR lib/${CMAKE_LIBRARY_ARCHITECTURE} CACHE PATH "Output directory for libraries" FORCE)
  ELSE(MULTIARCH)
    SET(CMAKE_INSTALL_LIBDIR lib CACHE PATH "Output directory for libraries" FORCE)
  ENDIF(MULTIARCH)
ENDIF(UNIX AND NOT WIN32)

SET(CPACK_PACKAGE_NAME "${PACKAGE}")
SET(CPACK_PACKAGE_VENDOR "Adalin B.V." CACHE INTERNAL "Vendor name")
SET(CPACK_PACKAGE_CONTACT "tech@adalin.org" CACHE INTERNAL "Contact")
SET(CPACK_RESOURCE_FILE_LICENSE "${PROJECT_SOURCE_DIR}/COPYING.v2" CACHE INTERNAL "Copyright" FORCE)

#  read 'description` file into a variable
file(STRINGS description descriptionFile)
STRING(REGEX REPLACE "; \\.?" "\n" rpmDescription "${descriptionFile}")
STRING(REGEX REPLACE ";" "\n" debDescription "${descriptionFile}")

IF(WIN32)
  SET(CPACK_PACKAGE_INSTALL_DIRECTORY "Adalin\\\\AeonWave")
  SET(CPACK_NSIS_CONTACT "info@adalin.com")
  SET(CPACK_PACKAGE_INSTALL_REGISTRY_KEY "${PACKAGE_NAME}")
  SET(CPACK_NSIS_DISPLAY_NAME "${PACKAGE_NAME}")
  SET(CPACK_PACKAGE_FILE_NAME "${PACKAGE_NAME}-${CPACK_PACKAGE_VERSION}")
  SET(CPACK_NSIS_ENABLE_UNINSTALL_BEFORE_INSTALL ON)
# SET(CPACK_NSIS_MODIFY_PATH ON)
# SET(CPACK_STRIP_FILES 1)
  SET(CPACK_GENERATOR NSIS)

ELSE(WIN32)
  SET(CPACK_PACKAGE_FILE_NAME "${CPACK_PACKAGE_NAME}-${CPACK_PACKAGE_VERSION}.el6.${CPACK_PACKAGE_ARCHITECTURE}")
  SET(CPACK_GENERATOR "DEB;RPM;STGZ")

  # DEBIAN
  IF(X86)
    SET(CPACK_DEBIAN_ARCHITECTURE "i386")
    SET(CPACK_RPM_PACKAGE_ARCHITECTURE "i686")
  ELSEIF(X86_64)
    SET(CPACK_DEBIAN_ARCHITECTURE "amd64")
    SET(CPACK_RPM_PACKAGE_ARCHITECTURE "amd64")
  ELSEIF(ARM)
    SET(CPACK_DEBIAN_ARCHITECTURE "armhf")
    SET(CPACK_RPM_PACKAGE_ARCHITECTURE "armhf")
  ELSE()
    SET(CPACK_DEBIAN_ARCHITECTURE ${CMAKE_SYSTEM_PROCESSOR})
    SET(CPACK_RPM_PACKAGE_ARCHITECTURE ${CMAKE_SYSTEM_PROCESSOR})
  ENDIF()
  SET(CPACK_DEBIAN_PACKAGE_DESCRIPTION ${debDescription})
  IF(MULTIARCH)
    SET(CPACK_DEBIAN_PACKAGE_PREDEPENDS "multiarch-support")
  ENDIF(MULTIARCH)
  SET(CPACK_DEBIAN_PACKAGE_MAINTAINER "${CPACK_PACKAGE_VENDOR} <${CPACK_PACKAGE_CONTACT}>")
  SET(CPACK_DEBIAN_CHANGELOG_FILE "${PROJECT_SOURCE_DIR}/ChangeLog")
  SET(CPACK_DEB_COMPONENT_INSTALL ON)

  INSTALL(FILES
          debian/copyright
          DESTINATION /usr/share/doc/${PACKAGE}-bin
          COMPONENT Libraries
  )
  INSTALL(FILES
          debian/copyright
          DESTINATION /usr/share/doc/${PACKAGE}-dev
          COMPONENT Headers
  )

  EXECUTE_PROCESS(COMMAND "cp" -f -p ChangeLog debian/ChangeLog
                  COMMAND "gzip" -f -9 debian/ChangeLog
                 WORKING_DIRECTORY ${PROJECT_SOURCE_DIR} RESULT_VARIABLE varRes)
  INSTALL(FILES
          debian/ChangeLog.gz
          DESTINATION /usr/share/doc/${PACKAGE}-bin
          RENAME changelog.gz
          COMPONENT Libraries
  )
  INSTALL(FILES
          debian/ChangeLog.gz
          DESTINATION /usr/share/doc/${PACKAGE}-dev
          RENAME changelog.gz
          COMPONENT Headers
  )

  # RPM
  SET(CPACK_RPM_PACKAGE_ARCHITECTURE ${PACK_PACKAGE_ARCHITECTURE})
  SET(CPACK_RPM_PACKAGE_DESCRIPTION ${rpmDescription})
  SET(CPACK_RPM_CHANGELOG_FILE "${PROJECT_SOURCE_DIR}/ChangeLog")
  SET(CPACK_RPM_COMPONENT_INSTALL ON)
ENDIF(WIN32)

# ZIP
IF(EXISTS ${PROJECT_SOURCE_DIR}/.gitignore)
  FILE(STRINGS ${PROJECT_SOURCE_DIR}/.gitignore CPACK_SOURCE_IGNORE_FILES)
ENDIF(EXISTS ${PROJECT_SOURCE_DIR}/.gitignore)

SET(CPACK_SOURCE_GENERATOR "ZIP")
SET(CPACK_SOURCE_IGNORE_FILES
    "^${PROJECT_SOURCE_DIR}/.git;\\\\.gitignore;Makefile.am;~$;${CPACK_SOURCE_IGNORE_FILES}" CACHE INTERNAL "Ignore files" FORCE)

