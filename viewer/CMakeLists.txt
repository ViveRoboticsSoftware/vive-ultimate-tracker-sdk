cmake_minimum_required(VERSION 3.10)
project(tracker_viewer)

#message(STATUS "build tracker_viewer TARGET_ARCH: ${TARGET_ARCH}")

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(COMMON_INCLUDES
	#"${CMAKE_CURRENT_SOURCE_DIR}/../include"
	"${CMAKE_CURRENT_SOURCE_DIR}/utils"
	"${CMAKE_CURRENT_SOURCE_DIR}/balai/include"
	"${CMAKE_CURRENT_SOURCE_DIR}/imgui"
	/usr/include/opencv4  # or create a link... # ln -s /usr/include/opencv4/opencv2 /usr/include/
)

set(COMMON_SOURCES
	"${CMAKE_CURRENT_SOURCE_DIR}/utils/src/image_io.cc"
	"${CMAKE_CURRENT_SOURCE_DIR}/utils/src/linux_toolkit.cc"
	"${CMAKE_CURRENT_SOURCE_DIR}/utils/src/tracker_model.cc"
	"${CMAKE_CURRENT_SOURCE_DIR}/utils/src/vive_utils.cc"
	"${CMAKE_CURRENT_SOURCE_DIR}/imgui/imgui.cpp"
	"${CMAKE_CURRENT_SOURCE_DIR}/imgui/imgui_draw.cpp"
	"${CMAKE_CURRENT_SOURCE_DIR}/imgui/imgui_impl_glfw.cpp"
	"${CMAKE_CURRENT_SOURCE_DIR}/imgui/imgui_impl_opengl3.cpp"
	"${CMAKE_CURRENT_SOURCE_DIR}/imgui/imgui_tables.cpp"
	"${CMAKE_CURRENT_SOURCE_DIR}/imgui/imgui_widgets.cpp"
)

set(COMMON_LIBRARIES
    ultimate-tracker
	"${CMAKE_CURRENT_SOURCE_DIR}/balai/lib/linux/${TARGET_ARCH}/libbalai.a"
	opencv_core opencv_imgcodecs opencv_imgproc GL GLEW glfw # X11 dl pthread
)

# tracker_viewer
add_executable(tracker_viewer tracker_viewer.cpp ${COMMON_SOURCES})
target_include_directories(tracker_viewer PRIVATE ${COMMON_INCLUDES})
#target_link_directories(tracker_viewer PRIVATE
#	"${CMAKE_CURRENT_SOURCE_DIR}/../bin/linux/${TARGET_ARCH}"
#)
target_link_libraries(tracker_viewer PRIVATE ${COMMON_LIBRARIES})
set_target_properties(tracker_viewer PROPERTIES
	INSTALL_RPATH "$ORIGIN" # sets the runtime search path (rpath), $ORIGIN = location of the exe 
	BUILD_WITH_INSTALL_RPATH TRUE
)

# tracker_viewer_standalone
add_executable(tracker_viewer_standalone tracker_viewer_standalone.cpp ${COMMON_SOURCES})
target_include_directories(tracker_viewer_standalone PRIVATE ${COMMON_INCLUDES})
#target_link_directories(tracker_viewer_standalone PRIVATE
#	"${CMAKE_CURRENT_SOURCE_DIR}/../bin/linux/${TARGET_ARCH}"
#)
target_link_libraries(tracker_viewer_standalone PRIVATE ${COMMON_LIBRARIES})
set_target_properties(tracker_viewer_standalone PROPERTIES
	INSTALL_RPATH "$ORIGIN" # sets the runtime search path (rpath), $ORIGIN = location of the exe 
	BUILD_WITH_INSTALL_RPATH TRUE
)

# cache ultimate tracker model
install(FILES "${CMAKE_CURRENT_SOURCE_DIR}/Vive_Ultimate_Tracker.model"
	DESTINATION ${INSTALL_DIR}
	PERMISSIONS OWNER_READ GROUP_READ WORLD_READ
)
