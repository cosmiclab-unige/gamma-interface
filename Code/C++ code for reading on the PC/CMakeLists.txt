cmake_minimum_required(VERSION 3.10)
project(unige_tactile_sensors_cpp)

set(CMAKE_CXX_STANDARD 17)

# 1. Sensor library
include_directories(include)

# Only include sensor_serial.cpp
set(SENSOR_SRC
    src/sensor_serial.cpp
)

# 2. Add IMGUI + IMPLOT + GLFW
include_directories(
    imgui-master
    imgui-master/backends
    implot-master
    glfw-master/include
)

file(GLOB IMGUI_SRC
    imgui-master/*.cpp
    imgui-master/backends/imgui_impl_glfw.cpp
    imgui-master/backends/imgui_impl_opengl3.cpp
)

file(GLOB IMPLOT_SRC
    implot-master/*.cpp
)

file(GLOB GLFW_SRC
    glfw-master/src/*.c
)

# 3. Create executable (GUI test app)
add_executable(run_sensor_gui
    test/run_sensor_gui.cpp
    ${SENSOR_SRC}
    ${IMGUI_SRC}
    ${IMPLOT_SRC}
    ${GLFW_SRC}
)

# 4. Link required libraries
find_package(OpenGL REQUIRED)

target_link_libraries(run_sensor_gui
    pthread
    OpenGL::GL
)

