
# Component support
#SET(CPACK_COMPONENTS_ALL Applications Libraries Headers)
SET(CPACK_COMPONENTS_ALL Libraries Headers)

# Display name
SET(CPACK_COMPONENT_APPLICATIONS_HIDDEN ON)
SET(CPACK_COMPONENT_APPLICATIONS_DISPLAY_NAME "Applictaions")
SET(CPACK_COMPONENT_HEADERS_DISPLAY_NAME "C/C++ Development Files")

# Descriptions
SET(CPACK_COMPONENT_APPLICATIONS_DESCRIPTION
   "Support applictaions to test the capabilities of the software")
SET(CPACK_COMPONENT_LIBRARIES_DESCRIPTION
   "Dynamically shared components of the software")
SET(CPACK_COMPONENT_HEADERS_DESCRIPTION
   "Development header files and library components for use with the software")

# Dependecies
SET(CPACK_COMPONENT_HEADERS_DEPENDS Libraries)
SET(CPACK_COMPONENT_APPLICATIONS_DEPENDS Libraries)

# Component grouping
IF(WIN32)
  # NSIS
  SET(CPACK_COMPONENT_APPLICATIONS_GROUP "Runtime")
  SET(CPACK_COMPONENT_LIBRARIES_GROUP "Runtime")
  SET(CPACK_COMPONENT_HEADERS_GROUP "Development")

  # Note Windows DLL are specified by RUNTIME
  SET(CPACK_COMPONENT_GROUP_RUNTIME_DESCRIPTION
     "Software required to run the software")
  SET(CPACK_COMPONENT_GROUP_DEVELOPMENT_DESCRIPTION
     "C/C++ Development headers and libraries")

  SET(CPACK_ALL_INSTALL_TYPES Runtime Developer)
  SET(CPACK_COMPONENT_LIBRARIES_INSTALL_TYPES Runtime Developer)
  SET(CPACK_COMPONENT_HEADERS_INSTALL_TYPES Developer)
  SET(CPACK_COMPONENT_APPLICATIONS_INSTALL_TYPES Runtime)
  
ELSE(WIN32)
  SET(CPACK_COMPONENT_APPLICATIONS_GROUP "bin")
  SET(CPACK_COMPONENT_LIBRARIES_GROUP "bin")
  SET(CPACK_COMPONENT_HEADERS_GROUP "dev")

  SET(CPACK_COMPONENT_GROUP_BIN_DESCRIPTION
     "Software required to run the software")
  SET(CPACK_COMPONENT_GROUP_DEV_DESCRIPTION
     "C/C++ Development headers and libraries")

  SET(CPACK_ALL_INSTALL_TYPES bin dev)
  SET(CPACK_COMPONENT_LIBRARIES_INSTALL_TYPES bin dev)
  SET(CPACK_COMPONENT_HEADERS_INSTALL_TYPES dev)
  SET(CPACK_COMPONENT_APPLICATIONS_INSTALL_TYPES bin)
ENDIF(WIN32)

macro (INSTALL_CUSTOM_FILES FILELIST destination)
  FILE(GLOB FILES ${FILELIST})
  FOREACH(instfile ${FILES})
#   message(STATUS "Adding ${instfile} to ${destination}")
    INSTALL(FILES ${instfile}
            DESTINATION "${destination}"
            CONFIGURATIONS Release Debug
            COMPONENT Runtime)
  ENDFOREACH(instfile)
endmacro()
