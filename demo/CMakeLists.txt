cmake_minimum_required(VERSION 3.10)
project(tracker_demo)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_executable(tracker_demo tracker_demo.cpp)

#target_include_directories(tracker_demo PRIVATE
#	"${CMAKE_CURRENT_SOURCE_DIR}/../include"
#)
#target_link_directories(tracker_demo PRIVATE
#	"${CMAKE_CURRENT_SOURCE_DIR}/../bin/linux/${TARGET_ARCH}"
#)
target_link_libraries(tracker_demo PRIVATE ultimate-tracker)

set_target_properties(tracker_demo PROPERTIES
	INSTALL_RPATH "$ORIGIN" # sets the runtime search path (rpath), $ORIGIN = location of the exe 
	BUILD_WITH_INSTALL_RPATH TRUE
)
