cmake_minimum_required (VERSION 3.8)

project (LearnVulkan)

include_directories("D:/include")
set(glfw "D:/lib/glfw/glfw3.lib")

#需要的package
find_package (Vulkan REQUIRED)


add_executable (${PROJECT_NAME} main.cpp)
set_target_properties (${PROJECT_NAME} PROPERTIES CXX_STANDARD 17)
#target链接库，包含的目录
target_link_libraries (${PROJECT_NAME} Vulkan::Vulkan ${glfw})
