cmake_minimum_required(VERSION 3.10)
project(vive-ultimate-tracker-linux)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# install path
set(INSTALL_DIR "${CMAKE_BINARY_DIR}/bin")

set(TARGET_ARCH "unknown")
set(SETUP_PROGRAM "tracker_setup.x86_64")
if(CMAKE_SYSTEM_PROCESSOR MATCHES "^(aarch64|arm64|ARM64)")
	set(TARGET_ARCH "arm64")
	set(SETUP_PROGRAM "tracker_setup.arm64")
	message(STATUS "Target: ARM64")
	add_compile_definitions(TARGET_ARM64)
elseif(CMAKE_SYSTEM_PROCESSOR MATCHES "^(x86_64|AMD64|amd64)")
	set(TARGET_ARCH "x64")
	message(STATUS "Target: x86-64")
	add_compile_definitions(TARGET_X64)
else()
	message(STATUS "Target: ${CMAKE_SYSTEM_PROCESSOR}")
endif()

if(CMAKE_SIZEOF_VOID_P EQUAL 8)
	message(STATUS "64-bit architecture")
else()
	message(STATUS "32-bit architecture")
endif()

###############################################################################
# ultimate tracker (prebuilt) lib
add_library(ultimate-tracker SHARED IMPORTED)

# Full path to the prebuilt shared object
set(ULTIMATE_TRACKER_SHARED_LIB "${CMAKE_CURRENT_SOURCE_DIR}/bin/linux/${TARGET_ARCH}/libultimate-tracker.so")
set_target_properties(ultimate-tracker PROPERTIES
	IMPORTED_LOCATION ${ULTIMATE_TRACKER_SHARED_LIB}
	INTERFACE_INCLUDE_DIRECTORIES "${CMAKE_CURRENT_SOURCE_DIR}/include"
)

install(FILES ${ULTIMATE_TRACKER_SHARED_LIB} DESTINATION ${INSTALL_DIR})
###############################################################################

# Add subdirectories
add_subdirectory(demo)
add_subdirectory(viewer)

file(ARCHIVE_EXTRACT
	INPUT "${CMAKE_CURRENT_SOURCE_DIR}/tracker_setup.7z"
	DESTINATION ${INSTALL_DIR}
)

install(TARGETS tracker_demo tracker_viewer tracker_viewer_standalone
	RUNTIME DESTINATION ${INSTALL_DIR}
	LIBRARY DESTINATION ${INSTALL_DIR}
)

install(PROGRAMS "${INSTALL_DIR}/tracker_setup/${SETUP_PROGRAM}"
	DESTINATION "${INSTALL_DIR}/tracker_setup" RENAME tracker_setup
)
