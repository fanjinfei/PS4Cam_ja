project(PS4EYECam)
cmake_minimum_required(VERSION 3.0.2)

include(package.cmake)

set (CMAKE_CXX_STANDARD 11)

include_directories(driver/include openframeworks)
include_directories(~/of_v0.9.8_linux64_release/libs/openFrameworks
        ~/of_v0.9.8_linux64_release/libs/openFrameworks/utils
        ~/of_v0.9.8_linux64_release/libs/openFrameworks/types
        ~/of_v0.9.8_linux64_release/libs/openFrameworks/math
        ~/of_v0.9.8_linux64_release/libs/openFrameworks/events
        ~/of_v0.9.8_linux64_release/libs/tess2/include
        ~/of_v0.9.8_linux64_release/libs/utf8cpp/include
        ~/of_v0.9.8_linux64_release/libs/openFrameworks/3d
        ~/of_v0.9.8_linux64_release/libs/openFrameworks/gl
        ~/of_v0.9.8_linux64_release/libs/openFrameworks/communication
        ~/of_v0.9.8_linux64_release/libs/openFrameworks/sound
        ~/of_v0.9.8_linux64_release/libs/openFrameworks/video
        ~/of_v0.9.8_linux64_release/libs/openFrameworks/graphics
        ~/of_v0.9.8_linux64_release/libs/openFrameworks/app
        ~/of_v0.9.8_linux64_release/libs/kiss/include
        ~/of_v0.9.8_linux64_release/libs/poco/include
        /usr/include/cairo
        /usr/include/gstreamer-1.0
        /usr/include/glib-2.0
        /usr/lib/x86_64-linux-gnu/glib-2.0/include
        /usr/lib/x86_64-linux-gnu/gstreamer-1.0/include
        )

link_directories(
        ~/of_v0.9.8_linux64_release/libs/openFrameworksCompiled/lib/linux64
        ~/of_v0.9.8_linux64_release/libs/glfw/lib/linux64/
        ~/of_v0.9.8_linux64_release/exports/linux64/libs
        ~/of_v0.9.8_linux64_release/libs/linux
        ~/of_v0.9.8_linux64_release/libs/kiss/lib/linux64
        ~/of_v0.9.8_linux64_release/libs/tess2/lib/linux64
        ~/of_v0.9.8_linux64_release/libs/linux64
        ~/of_v0.9.8_linux64_release/libs/poco/lib/linux64
)
find_package(OpenCV REQUIRED)

add_library(PS4EyeLib
        driver/include/ps4eye.h driver/include/ps4eye_regs.h driver/src/ps4eye.cpp)

add_executable(PS4Eye openframeworks/main.cpp openframeworks/ofApp.cpp)
target_link_libraries(PS4Eye PS4EyeLib)
target_link_libraries(PS4Eye ${OpenCV_LIBS} usb-1.0 pthread X11 GLEW glfw3 Xxf86vm Xcursor Xrandr Xi openFrameworks pthread
        GL boost_system mpg123 openal kiss sndfile PocoFoundation PocoNet PocoNetSSL PocoUtil PocoJSON PocoXML ssl crypto PocoCrypto
        fontconfig freetype tess2 cairo freeimage boost_filesystem jpeg
        )
