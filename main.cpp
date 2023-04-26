	//glfw库会自动包含vulkan的头文件
#define GLFW_INCLUDE_VULKAN
#include <GLFW/glfw3.h>

#include <iostream>
#include <stdexcept>
#include <functional>
#include <cstdlib>
#include <vector>
#include <string>
#include <array>
#include <set>
#include <unordered_set>
#include <fstream>
#include <chrono>

#define STB_IMAGE_IMPLEMENTATION //stb_image.h默认只定义的了函数的原型，此定义将实现包含进来
#include "stb_image.h"

#define GLM_FORCE_RADIANS
#define GLM_FORCE_DEPTH_ZERO_TO_ONE //使用vulkan的深度范围
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

#define TINYOBJLOADER_IMPLEMENTATION
#include "tiny_obj_loader.h"

const uint32_t WIDTH = 800;
const uint32_t HEIGHT = 600;
const std::string shaderRootDir = "D:/VulkanTutorial/code/shaders";
const std::string textureRootDir = "D:/VulkanTutorial/code/textures";
const std::string modelRootDir = "D:/VulkanTutorial/code/models";


const int MAX_FRAMES_IN_FLIGHT = 3; //三帧并行渲染


#if NDEBUG
const bool enableValidationLayers = false;	
#else
const bool enableValidationLayers = true;
#endif

//lunarG的vulkan SDK允许通过 "VK_LAYER_KHRONOS_validation" 过来隐式地开启所有可用的校验层
std::vector<const char* > validationLayers = {
	"VK_LAYER_KHRONOS_validation"
};

//设备层需要的扩展列表,需要保证全部支持
std::vector<const char*> deviceExtentions = {
	VK_KHR_SWAPCHAIN_EXTENSION_NAME
};


bool checkValidationLayerSupport() {
	uint32_t layerCount = 0;
	vkEnumerateInstanceLayerProperties(&layerCount, nullptr);
	std::vector<VkLayerProperties> layerProperties(layerCount);	
	vkEnumerateInstanceLayerProperties(&layerCount, layerProperties.data());

	/*
	std::cout << "validation layer support:========================" << std::endl;
	for (auto&& layer : layerProperties) {
		std::cout << layer.layerName << "\t"
			<< layer.implementationVersion << "\t"
			<< layer.specVersion << "\t"
			<< layer.description << std::endl;
	}*/

	for (auto&& validationLayer : validationLayers) {
		bool layerFound = false;

		for (auto&& properties : layerProperties) {
			//找到对应的校验层
			if (strcmp(validationLayer, properties.layerName) == 0) {
				layerFound = true;
				break;
			}
		}
		if (!layerFound) {
			return false;
		}
	}

	return true;
}
bool checkDeviceExtentionSupport(VkPhysicalDevice phyDevice) {
	uint32_t extCount;
	vkEnumerateDeviceExtensionProperties(phyDevice, nullptr, &extCount, nullptr);
	std::vector<VkExtensionProperties> exts(extCount);
	vkEnumerateDeviceExtensionProperties(phyDevice, nullptr, &extCount, exts.data());

	//检查支持所有的device
	std::unordered_set<std::string> set(deviceExtentions.begin(), deviceExtentions.end());

	for (auto&& extension : exts) {
		set.erase(extension.extensionName);
	}
	return set.empty();
}

//获取创建实例需要的扩展名称，以及数量，包括glfw库的窗口交互扩展，和VK debug扩展
std::vector<const char*> getRequeiredExtetions() {
	//接下来，我们需要指定需要的全局扩展。之前提到，vulkan是平台无关的，所以需要一个和窗口系统交互的扩展。glfw库包含了一个可以返回这一扩展的函数，我们可以直接使用它
	uint32_t glfwCount = 0;
	const char** glfwExtentions = glfwGetRequiredInstanceExtensions(&glfwCount);
	std::vector<const char*> exts(glfwExtentions, glfwExtentions + glfwCount);
	
	//仅仅启用校验层并没有任何用处，我们不能得到任何有用的调试信息。为了获得调试信息，我们需要使用"VK_EXT_debug_utils"扩展，设置回调函数来接受调试信息。
	if (enableValidationLayers) {
		exts.push_back(VK_EXT_DEBUG_UTILS_EXTENSION_NAME); //vulkan中的扩选项是通过 字符串 名字来开启的
	}

	return exts;
}

//Debug回调函数, 为PFN_vkDebugUtilsMessengerCallbackEXT类型
static VKAPI_ATTR VkBool32 VKAPI_CALL debugCallback(
	VkDebugUtilsMessageSeverityFlagBitsEXT           messageSeverity, //消息级别
	VkDebugUtilsMessageTypeFlagsEXT                  messageTypes,    //消息类型
	const VkDebugUtilsMessengerCallbackDataEXT* pCallbackData,		  //回调发生时的信息
	void* pUserData													  //设置回调函数时传递的指针
){				
	//pCallbackData->pObjects[pCallbackData->objectCount] 存储有和消息相管vulkan对象句柄的数组
	std::cerr << "[validation layer]:\t" << pCallbackData->pMessage << std::endl;
	return VK_FALSE; //回调函数返回了一个布尔值，用来表示引发校验层处理的Vulkan API用是否被中断。
}





class HelloTriangleApplication {
public:
	void run() {
		initWindow();
		initVulkan();
		mainLoop();
		cleanup();
	}

private:

	//之后可能会保存多个队列族的索引
	struct QueueFamilyInices {
		int graphicsFamily = -1;
		int presentFamily = -1; //保设备可以在我创建的surface上显示图像
		bool isComplete() {
			return graphicsFamily >= 0 && presentFamily >= 0;
		}
	};

	struct SwapChainSupportDetails {
		VkSurfaceCapabilitiesKHR capbilities;
		std::vector<VkSurfaceFormatKHR> formats;
		std::vector<VkPresentModeKHR> presentModes;
	};

	struct Vertex {
		glm::vec3 position;
		glm::vec3 color;
		glm::vec2 texCoord;

		//描述的是vulkan如何将顶点数据的格式传递给vertex shader，shader知道了数据格式之后，才能在GPU的内存中读取不同部位的数据
		static VkVertexInputBindingDescription getBindingDescription() {
			VkVertexInputBindingDescription bindingDescription{};//指定单个顶点的缓冲区信息，相当于VBO，指定指定一片缓冲区的信息，其中的数据是每个顶点的数据
			bindingDescription.binding = 0; //TODO，				//相当于VBO的ID
			bindingDescription.stride = sizeof(Vertex); //那么如何区分position和color
			bindingDescription.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;

			return bindingDescription;
		}

		static std::array<VkVertexInputAttributeDescription, 3> getAttributeDescriptions() {
			std::array<VkVertexInputAttributeDescription, 3> attributeDescriptions{}; //相当于bufferData，描述但各顶点属性的信息，就是单个顶点缓冲中有position，color，normal等，但是如何将这些属性区分开，就需要AttributeDescription，指定各属性的格式、在缓冲中的偏移
			attributeDescriptions[0].binding = 0;	//说明这个AttributeDescription描述的是哪一块缓冲，也即那一个VBO，不像OpenGL，它是状态机，上一步绑定了VBO，下一步就调用bufferData就直接描述的是上一步的VBO，VK需要指定
			attributeDescriptions[0].location = 0;
			/*
				float: VK_FORMAT_R32_SFLOATg
				vec2: VK_FORMAT_R32G32_SFLOAT
				vec3: VK_FORMAT_R32G32B32_SFLOAT
				vec4: VK_FORMAT_R32G32B32A32_SFLOAT
			*/
			attributeDescriptions[0].format = VK_FORMAT_R32G32B32_SFLOAT;
			attributeDescriptions[0].offset = offsetof(Vertex, position);

			attributeDescriptions[1].binding = 0;
			attributeDescriptions[1].location = 1;

			attributeDescriptions[1].format = VK_FORMAT_R32G32B32_SFLOAT;
			attributeDescriptions[1].offset = offsetof(Vertex, color);

			attributeDescriptions[2].binding = 0;
			attributeDescriptions[2].location = 2;
			attributeDescriptions[2].format = VK_FORMAT_R32G32_SFLOAT;
			attributeDescriptions[2].offset = offsetof(Vertex, texCoord);
			return attributeDescriptions;
		}
	};
	

	struct UniformBufferObjcet {	
		glm::mat4 model;
		glm::mat4 view;
		glm::mat4 projection;
	};
	
	static void frameBufferResizeCallback(GLFWwindow* window, int width, int height) {
		HelloTriangleApplication* app = reinterpret_cast<HelloTriangleApplication*>(glfwGetWindowUserPointer(window));
		app->frameBufferResized = true;
	}

	void initWindow() {
		glfwInit();

		glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);  // Because GLFW was originally designed to create an OpenGL context, we need to tell it to not create an OpenGL context with a
		//glfwWindowHint(GLFW_RESIZABLE, GLFW_TRUE); //窗口大小变化地处理需要注意很多地方,我们会在以后介绍它，暂时我们先禁止窗口大小改变：

		window = glfwCreateWindow(WIDTH, HEIGHT, "Vulkan", nullptr, nullptr);
		glfwSetFramebufferSizeCallback(window, frameBufferResizeCallback);
		glfwSetWindowUserPointer(window, this); //可以通过window获取到this指针
	}

	void loadModel() {
		std::string modelPath = modelRootDir + "/viking_room.obj";


		tinyobj::attrib_t attrib;
		std::vector<tinyobj::shape_t> shapes;
		std::vector<tinyobj::material_t> matrials;
		std::string err;
		if (!tinyobj::LoadObj(&attrib, &shapes, &matrials, &err, modelPath.c_str())) {
			throw std::runtime_error(err);
		}

		/*int n = attrib.vertices.size() / 3;
		vertices.resize(n);*/
		/*for (int i = 0; i < n; ++i) {
			vertices[i].position = { attrib.vertices[i * 3], attrib.vertices[i * 3 + 1], attrib.vertices[i * 3 + 2] };
			vertices[i].texCoord = { attrib.texcoords[i * 2], attrib.texcoords[i * 2 + 1] };
			vertices[i].color = { 1.f, 0.f, 0.f };
		}*/
		
		vertices.clear();
		vertexIndices.clear();
		for (auto&& shape : shapes) {
			for (auto&& index : shape.mesh.indices) {
				Vertex vertex{};
				vertex.position = { attrib.vertices[3 * index.vertex_index], attrib.vertices[3 * index.vertex_index + 1], attrib.vertices[3 * index.vertex_index + 2] };
				vertex.texCoord = { attrib.texcoords[2 * index.texcoord_index], 1- attrib.texcoords[2 * index.texcoord_index + 1] };
				vertices.emplace_back(vertex);
				vertexIndices.emplace_back(vertices.size() - 1);
			}
		}
	}


	void initVulkan() {

		//加载模型`
		loadModel();

		//创建VkInstance
		createInstance();

		//设置校验层的debug回调函数
		setupDebugCallback();

		//创建窗口surface，surface具体指什么？
		createWindowSurface();

		//选择物理设备
		pickPhysicalDevice();

		//创建逻辑设备
		createLogicalDevice();

		//创建交换链
		createSwapChain();

		//创建swap chain image view对象
		createSwapChainImageViews();

		//创建render pass 对象
		createRenderPass();

		////创建descriptor
		createDescriptorSetLayout();

		//创建渲染图形管线
		createGraphicsPipeline();

		//创建command pool
		createCommandPool();

		//创建深度监测相关对象
		createDepthResources();
		
		//为交换链中的所有图像创建帧缓冲
		createFramebuffers();

		//创建图像缓冲
		createTextureImage();


		//创建textureImageview
		createTextureImageView();

		//创建采样器对象
		createTextureSampler();


		//创建顶点缓冲
		createVertexBuffer();

		//创建顶点索引缓冲
		createIndexBuffer();

		//创建uniform 缓冲
		createUniformBuffers();
		
		////创建descriptor pool用来分配decriptor sets
		createDescriptorPool();

		//创建descriptor set对象
		createDescriptorSets();

		//分配指令缓冲对象，使用它记录绘制指令
		createCommandBuffers();
		
		//创建信号量和fence对象
		createSyncObjects();

	}

	void createInstance() {
		//填写应用程序信息，这些信息的填写不是必须的，但填写的信息可能会作为驱动程序的优化依据，
		VkApplicationInfo appInfo{};
		appInfo.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO; //指定结构体的类型
		appInfo.pApplicationName = "hello triangle";
		appInfo.applicationVersion = VK_MAKE_VERSION(0, 0, 1);
		appInfo.pEngineName = "no engine";
		appInfo.engineVersion = VK_MAKE_VERSION(0, 0, 0);
		appInfo.apiVersion = VK_API_VERSION_1_3;

		//这个结构体是必须的，它告诉Vulkan驱动程序需要使用的 全局扩展 和 校验层, 全局是指这里的设置对于整个应用程序都有效，而不仅仅对一个设备有效
		VkInstanceCreateInfo createInfo{};
		createInfo.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
		createInfo.pApplicationInfo = &appInfo;

		//全局校验层
		if (enableValidationLayers && !checkValidationLayerSupport()) {
			throw std::runtime_error("validation layer requested, but not avaliable");
		}
		if (enableValidationLayers) {
			createInfo.enabledLayerCount = validationLayers.size();
			createInfo.ppEnabledLayerNames = validationLayers.data();
		}
		else {
			createInfo.enabledLayerCount = 0;
		}

		//设置扩展
		std::vector<const char*> exts = getRequeiredExtetions();
		createInfo.enabledExtensionCount = exts.size();
		createInfo.ppEnabledExtensionNames = exts.data();

		//TEST 打印支持的全部扩展
		/*uint32_t extNum = 0;
		vkEnumerateInstanceExtensionProperties(nullptr, &extNum, nullptr);
		std::vector<VkExtensionProperties> exts(extNum);
		vkEnumerateInstanceExtensionProperties(nullptr, &extNum, exts.data());
		for (auto&& extProp : exts) {
			std::cout << extProp.extensionName << "\t" << extProp.specVersion << std::endl;
		}
		*/

		if (VkResult result = vkCreateInstance(&createInfo, nullptr, &instance); result != VK_SUCCESS) {
			throw std::runtime_error("create VKinstance failed");
		}
	}

	//Debug回调
	void setupDebugCallback() {
		if (!enableValidationLayers)
			return;
		VkDebugUtilsMessengerCreateInfoEXT createInfo{};
		//结构体类型
		createInfo.sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT;
		//消息级别
		createInfo.messageSeverity = VK_DEBUG_UTILS_MESSAGE_SEVERITY_VERBOSE_BIT_EXT
			| VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT
			| VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT; //verbose and warning and error
		//消息类型
		createInfo.messageType = VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT
			| VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT
			| VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT;

		createInfo.pfnUserCallback = debugCallback;
		createInfo.pUserData = nullptr;

		//vkCreateDebugUtilsMessengerEXT() //此函数是一个扩展函数，不会vulkan库自动加载 直接使用错误 unresolved symble  vkCreateDebugUtilsMessengerEXT(instance, &createInfo, nullptr, &callback);
		//所以先加载这个函数的地址，然后转化为PNF,之后再调用
		auto func = (PFN_vkCreateDebugUtilsMessengerEXT)vkGetInstanceProcAddr(instance, "vkCreateDebugUtilsMessengerEXT");
		if (func == nullptr || func(instance, &createInfo, nullptr, &callback) != VK_SUCCESS) {
			throw std::runtime_error("failed to set up debug callback");
		}

	}


	/*物理设备和队列族*/
	//检查显卡是否可用，是物理设备的层面
	bool isDeviceSuitable(VkPhysicalDevice phyDevice) {
		//物理设备不支持特定的命令队列族
		indices = findQueueFamilies(phyDevice);
		if (!indices.isComplete())
			return false;
		//物理设备是否支持一定的扩展，例如swap chain
		if (!checkDeviceExtentionSupport(phyDevice)) {
			return false;
		}
		//需要交换链至少支持一种图像格式和一种支持窗口表面的呈现模式
		swapChainSupport = querySwapChainSupport(phyDevice);
		if (swapChainSupport.formats.empty() || swapChainSupport.presentModes.empty()) {
			return false;
		}

		//为了选择合适的设备，我们需要获取更加详细的设备信息
		VkPhysicalDeviceProperties deviceProperties;
		vkGetPhysicalDeviceProperties(phyDevice, &deviceProperties);
		//纹理压缩，戶戴位浮点和多视口渲染(常用于VR)等特性
		VkPhysicalDeviceFeatures deviceFeatures;
		vkGetPhysicalDeviceFeatures(phyDevice, &deviceFeatures);

		//支持独立显卡和 geometry shader就行
		return deviceProperties.deviceType == VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU && deviceFeatures.geometryShader;
	}

	/*
		从绘制到加载纹理都需要将操作
		指令提交给一个队列，然后才能执行。vulkan有多种不同类型的队列，它
		们属于不同的队列族，每个队列族的队列只允许执行特定的一部分指令。
		比如，可能存在只允许执行计算相关指令的队列族和只允许执行内存传输
		相关指令的队列族。当前只需要找到支持图形指令的队列族
		队列族是与一个物理设备相关的，所以需要传入一个VKPhysicalDevice
	*/
	QueueFamilyInices findQueueFamilies(VkPhysicalDevice phyDevice) {
		uint32_t queueFamilyCount;
		vkGetPhysicalDeviceQueueFamilyProperties(phyDevice, &queueFamilyCount, nullptr);
		std::vector<VkQueueFamilyProperties> queueFamilies(queueFamilyCount);
		vkGetPhysicalDeviceQueueFamilyProperties(phyDevice, &queueFamilyCount, queueFamilies.data());

		//获取到的支持两种功能的队列族可以相同也可以不相同，一下逻辑查找的是支持两种功能的队列族，可以显式地指定绘制和呈现队列族是同一个的物理设备来提高性能表现
		for (int i = 0, n = queueFamilies.size(); i < n; ++i) {

			VkBool32 presentSupport = false;
			vkGetPhysicalDeviceSurfaceSupportKHR(phyDevice, i, surface, &presentSupport);
			//物理设备的队列族必须支持在surface上进行显示，并且支持图形绘制指令
			if (presentSupport && queueFamilies[i].queueCount > 0 && queueFamilies[i].queueFlags & VK_QUEUE_GRAPHICS_BIT) {
				return QueueFamilyInices{ i, i };
			}
		}
		return {};
	}

	void pickPhysicalDevice() {
		//使用VkPhysicalDevice对象来存储我们选择使用的显卡信息。这一对象可以在VkInstance进行清除操作时，自动清除自己，所以我们不需要再cleanup函数中对它进行清除。
		phyDevice = VK_NULL_HANDLE;
		//请求显卡列表
		uint32_t deviceCount;
		vkEnumeratePhysicalDevices(instance, &deviceCount, nullptr);
		if (deviceCount == 0) {
			throw std::runtime_error("failed to find GPU with vulkan support");
		}
		std::vector<VkPhysicalDevice> devices(deviceCount);
		vkEnumeratePhysicalDevices(instance, &deviceCount, devices.data());

		for (auto&& device : devices) {
			if (isDeviceSuitable(device)) {
				phyDevice = device;
				break;
			}
		}

		if (phyDevice == VK_NULL_HANDLE) {
			throw std::runtime_error("failed to find suitable GPU");
		}


	}

	//逻辑设备，选择物理设备后，我们还需要一个逻辑设备来作为和物理设备交互的接口
	void createLogicalDevice() {
		//VkDeviceQueueCreateInfo描述了针对一个队列族我们所需的队列数量。
		float queuePriorities = 1.f;
		std::vector<VkDeviceQueueCreateInfo> queueCreateInfos;
		std::set<int>  uniqueQueueIndices{ indices.graphicsFamily, indices.presentFamily };
		for (auto index : uniqueQueueIndices) {
			VkDeviceQueueCreateInfo createInfo{};
			createInfo.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
			createInfo.queueFamilyIndex = index;
			createInfo.queueCount = 1;
			createInfo.pQueuePriorities = &queuePriorities;
			queueCreateInfos.emplace_back(createInfo);
		}


		//接下来，我们要指定应用程序使用的设备特性。
		VkPhysicalDeviceFeatures phyDeviceFeatures{};
		phyDeviceFeatures.samplerAnisotropy = VK_TRUE;
		//创建逻辑设备，扩展和全局校验和 VKInstance创建相同
		VkDeviceCreateInfo deviceCreateInfo{};
		deviceCreateInfo.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
		deviceCreateInfo.pQueueCreateInfos = queueCreateInfos.data();
		deviceCreateInfo.queueCreateInfoCount = queueCreateInfos.size();
		deviceCreateInfo.pEnabledFeatures = &phyDeviceFeatures;
		//设置扩展
		//TODO暂时不需要其他，只需要校验层即可
		//添加swap chain 扩展
		deviceCreateInfo.enabledExtensionCount = deviceExtentions.size();
		deviceCreateInfo.ppEnabledExtensionNames = deviceExtentions.data();

		//全局校验层,们可以对设备和扖扵扬扫扡扮实例使用相同地校验层，不需要额外的扩展支持, 一维validation layer support在创建VKInstance时已经检查过了，当前不需要再进行检查
		if (enableValidationLayers) {
			deviceCreateInfo.enabledLayerCount = validationLayers.size();
			deviceCreateInfo.ppEnabledLayerNames = validationLayers.data();
		}
		else {
			deviceCreateInfo.enabledLayerCount = 0;
		}
		if (VkResult result = vkCreateDevice(phyDevice, &deviceCreateInfo, nullptr, &logiDevice); result != VK_SUCCESS) {
			throw std::runtime_error("failed to create logical device");
		}

		//创建logiDevice时指定的队列也会被创建，这个函数取到队列的句柄 (logiDevice, 队列族, 队列索引, 返回值)
		vkGetDeviceQueue(logiDevice, indices.graphicsFamily, 0, &graphicsQueue);
		vkGetDeviceQueue(logiDevice, indices.presentFamily, 0, &presentQueue);
	}

	////创建窗口surface
	void createWindowSurface() {
		if (glfwCreateWindowSurface(instance, window, nullptr, &surface) != VK_SUCCESS) {
			throw std::runtime_error("failed to create window surface");
		}
	}

	//创建swap chain之前的检查, swap chain是与物理设备和surface强相关的
	SwapChainSupportDetails querySwapChainSupport(VkPhysicalDevice phyDevice) {
		SwapChainSupportDetails details;
		//1. 查询基础表面的特性
		vkGetPhysicalDeviceSurfaceCapabilitiesKHR(phyDevice, surface, &details.capbilities);
		//2. 查询表面支持的格式
		uint32_t formatCount;
		vkGetPhysicalDeviceSurfaceFormatsKHR(phyDevice, surface, &formatCount, nullptr);
		details.formats.resize(formatCount);
		vkGetPhysicalDeviceSurfaceFormatsKHR(phyDevice, surface, &formatCount, details.formats.data());
		//3. 表面支持的呈现模式
		uint32_t presentModeCount;
		vkGetPhysicalDeviceSurfacePresentModesKHR(phyDevice, surface, &presentModeCount, nullptr);
		details.presentModes.resize(presentModeCount);
		vkGetPhysicalDeviceSurfacePresentModesKHR(phyDevice, surface, &presentModeCount, details.presentModes.data());
		return details;
	}
	 

	//选择swap chain 的格式
	VkSurfaceFormatKHR chooseSwapSurfaceFormat(const std::vector<VkSurfaceFormatKHR>& formats) {
		//说明vk没有默认的颜色格式，返回默认的
		if (formats.size() == 1 && formats[0].format == VK_FORMAT_UNDEFINED) {
			return { VK_FORMAT_B8G8R8A8_UNORM, VK_COLOR_SPACE_SRGB_NONLINEAR_KHR };
		}

		for (auto&& avaliableFormat : formats) {
			if (avaliableFormat.format == VK_FORMAT_B8G8R8A8_UNORM && avaliableFormat.colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR) {
				return avaliableFormat;
			}
		}

		//若没有期望的format， 返回第一个
		return formats[0];
	}
	/*
	呈现模式可以说是交换链中最重要的设置。它决定了什么条件下图像才会显示到屏幕。vk提供了四种可用的呈现模式：
	1. VK_PRESENT_MODE_IMMEDIATE_KHR 应用程序提交的图像会被立即传输到屏幕上，可能会导致撕裂现象
	2. VK_PRESENT_MODE_FIFO_KHR 交换链变成一个先进先出的队列，每次从队列头部取出一张图像进行显示，应用程序渲染的图像提交给交换链后，会被放在队列尾部。当队列为满时，应用程序需要进行等待。这一模式非常类似现在常用的垂直同步。刷新显示的时刻也被叫做垂直回扫。
	3. VK_PRESENT_MODE_FIFO_RELAXED_KHR 这一模式和上一模式的唯一区别是，如果应用程序延迟，导致交换链的队列在上一次垂直回扫时为空，那么，如果应用程序在下一次垂直回扫前提交图像，图像会立即被显示。这一模式可能会导致撕裂现象。
	4. VK_PRESENT_MODE_MAILBOX_KHR这一模式是第二种模式的另一个变种。它不会在交换链的队列满时阻塞应用程序，队列中的图像会被直接替换为应用程序新提交的图像。这一模式可以用来实现三倍缓冲，避免撕裂现象的同时减小了延迟问题。其中只有2是保证可用的，需要查找最佳呈现模式
	*/
	//选择swap chain 的呈现模式
	VkPresentModeKHR chooseSwapPresentMode(const std::vector<VkPresentModeKHR>& presentModes) {
		VkPresentModeKHR ret = VK_PRESENT_MODE_FIFO_KHR;
		for (auto&& mode : presentModes) {
			if (mode == VK_PRESENT_MODE_MAILBOX_KHR) {
				return mode;
			}
			//Vulkan编程指南中说驱动对FIFO的支持不好，所以最好选immediate模式
			else if (mode == VK_PRESENT_MODE_IMMEDIATE_KHR) {
				ret = VK_PRESENT_MODE_IMMEDIATE_KHR;
			}
		}
		return ret;
	}

	//选择swap chain交换范围，交换范围是交换链中图像的分辨率，它几乎总是和要显示图像的窗口的分辨率相同。
	VkExtent2D chooseSwapExtent(const VkSurfaceCapabilitiesKHR& capabilities) {
		//适合当前窗口的交换范围，若一些窗口系统会使用一个特殊值，变量类型的最大值，表示允许我们自己选择对于窗口最合适的交换范围，但我们选择的交换范围需要在与的范围内
		//if (capabilities.currentExtent.width != std::numeric_limits<uint32_t>::max()) {
		//	return capabilities.currentExtent;
		//}

		int width, height;
		glfwGetFramebufferSize(window, &width, &height);



		return { static_cast<uint32_t>(width), static_cast<uint32_t>(height)};
	}

	//创建swap chain对象
	void createSwapChain() {
		surfaceFormat = chooseSwapSurfaceFormat(swapChainSupport.formats);
		presentMode = chooseSwapPresentMode(swapChainSupport.presentModes);
		extent = chooseSwapExtent(swapChainSupport.capbilities);
		//交换链的队列可以容纳的图像个数
		//使用这个数量实现3倍缓冲
		uint32_t imageCount = swapChainSupport.capbilities.minImageCount + 1;
		if (swapChainSupport.capbilities.maxImageCount > 0 && imageCount > swapChainSupport.capbilities.maxImageCount) {
			imageCount = swapChainSupport.capbilities.maxImageCount;
		}

		VkSwapchainCreateInfoKHR createInfo{};
		createInfo.sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR;
		createInfo.surface = surface;   //swap chain的目的地是surface
		createInfo.imageFormat = surfaceFormat.format;
		createInfo.imageColorSpace = surfaceFormat.colorSpace;
		createInfo.presentMode = presentMode;
		createInfo.imageExtent = extent;
		createInfo.minImageCount = imageCount;
		createInfo.imageArrayLayers = 1; //图像包含的层次
		createInfo.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT; //TODO
		//TODO 通过图形队列在交换链图像上进行绘制操作, 将图像提交给呈现队列来显示。p91
		uint32_t queueFamilyIndices[] = { static_cast<uint32_t>(indices.graphicsFamily), static_cast<uint32_t>(indices.presentFamily) };
		if (indices.graphicsFamily == indices.presentFamily) {
			//两个队列族是同一个，一张图像同一时间只能被一个队列族所拥有，在另一队列族使用它之前，必须显式地改变图像所有权。因为是同一个队列所拥有，所以操作更高效一点 TODO: 理解对吗？
			createInfo.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
			createInfo.queueFamilyIndexCount = 0; //optional
			createInfo.pQueueFamilyIndices = nullptr;
		}
		else {
			createInfo.imageSharingMode = VK_SHARING_MODE_CONCURRENT;
			createInfo.queueFamilyIndexCount = 2; //至少两个队列族
			createInfo.pQueueFamilyIndices = queueFamilyIndices;
		}

		createInfo.preTransform = swapChainSupport.capbilities.currentTransform; //可以对图像进行变换，例如旋转180，90等
		createInfo.compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR; //关闭alpha通道
		createInfo.clipped = VK_TRUE; //不关心被窗口系统中的其它窗口遮挡的像素的颜色
		createInfo.oldSwapchain = VK_NULL_HANDLE;//应用程序在运行过程中交换链可能会失效。比如，改变窗口大小后，交换链需要重建，重建时需要之前的交换链

		if (vkCreateSwapchainKHR(logiDevice, &createInfo, nullptr, &swapChain) != VK_SUCCESS) {
			throw std::runtime_error("failed to create swap chain");
		}

		//获取swap chain 图像句柄
		uint32_t realSwapImageCount;
		vkGetSwapchainImagesKHR(logiDevice, swapChain, &realSwapImageCount, nullptr);
		swapChainImages.resize(realSwapImageCount);
		vkGetSwapchainImagesKHR(logiDevice, swapChain, &realSwapImageCount, swapChainImages.data());
	}

	//imageview描述 VkImage 中的哪些部分应该用作图像的哪些方面（例如颜色、深度、模板等），以及如何处理这些部分（例如使用哪种格式、如何进行采样等）
	void createSwapChainImageViews() {
		swapChainImageViews.resize(swapChainImages.size());
		for (int i = 0, n = swapChainImageViews.size(); i < n; ++i) {
			swapChainImageViews[i] = createImageView(swapChainImages[i], surfaceFormat.format);
		}
	}

	VkImageView createImageView(VkImage image, VkFormat format, VkImageAspectFlags aspectFlags = VK_IMAGE_ASPECT_COLOR_BIT) {
		VkImageViewCreateInfo createInfo{};
		createInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
		createInfo.image = image;
		createInfo.viewType = VK_IMAGE_VIEW_TYPE_2D; //指定纹理的解释方式，一维、二维、三维、立方体贴图等
		createInfo.format = format;

		//进行图像颜色通道的映射
		createInfo.components.r = VK_COMPONENT_SWIZZLE_IDENTITY;
		createInfo.components.g = VK_COMPONENT_SWIZZLE_IDENTITY;
		createInfo.components.b = VK_COMPONENT_SWIZZLE_IDENTITY;
		createInfo.components.a = VK_COMPONENT_SWIZZLE_IDENTITY;
		
		//图像的用途以及那一部分可以被访问
		createInfo.subresourceRange.aspectMask = aspectFlags; //图像只被作为渲染目标
		createInfo.subresourceRange.baseArrayLayer = 0;
		createInfo.subresourceRange.layerCount = 1;  //只有一个图层
		createInfo.subresourceRange.baseMipLevel = 0;
		createInfo.subresourceRange.levelCount = 1;  //没有细分级别，只有一层mipmap

		VkImageView imageView{};
		if (vkCreateImageView(logiDevice, &createInfo, nullptr, &imageView) != VK_SUCCESS) {
			throw std::runtime_error("failed to create swap chain image views");
		}
		return imageView;	
	}

	//创建渲染管线
	void createGraphicsPipeline() {
		auto vertShaderCode = readFile(shaderRootDir + "/sampler_vert.spv");
		auto fragShaderCode = readFile(shaderRootDir + "/sampler_frag.spv");

		//着色器模块对象试只是对shader 字节码的一个封装，只在管线创建时需要
		VkShaderModule vertShaderModule = createShaderModule(vertShaderCode);
		VkShaderModule fragShaderModule = createShaderModule(fragShaderCode);

		//指定shader module在管线处理哪一阶段被使用
		VkPipelineShaderStageCreateInfo vertShaderStageCreateInfo{};
		vertShaderStageCreateInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
		vertShaderStageCreateInfo.module = vertShaderModule;
		vertShaderStageCreateInfo.pName = "main";
		vertShaderStageCreateInfo.stage = VK_SHADER_STAGE_VERTEX_BIT;
		vertShaderStageCreateInfo.pSpecializationInfo = nullptr; //可以指定shader常量，可令编译器进行优化

		VkPipelineShaderStageCreateInfo fragShaderStageCreateInfo{};
		fragShaderStageCreateInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
		fragShaderStageCreateInfo.module = fragShaderModule;
		fragShaderStageCreateInfo.pName = "main";
		fragShaderStageCreateInfo.stage = VK_SHADER_STAGE_FRAGMENT_BIT;
		fragShaderStageCreateInfo.pSpecializationInfo = nullptr;

		VkPipelineShaderStageCreateInfo shaderStageCreateInfos[] = { vertShaderStageCreateInfo, fragShaderStageCreateInfo };

		//======================配置管线的固定功能阶段========================//

		//1.顶点数据的组织和顶点的属性state
		VkPipelineVertexInputStateCreateInfo vertInputCreateInfo{};
		vertInputCreateInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
		//TODO 绑定顶点数据和顶点属性
		VkVertexInputBindingDescription bindingDescription = Vertex::getBindingDescription();
		auto attributeDescription = Vertex::getAttributeDescriptions();
		vertInputCreateInfo.vertexBindingDescriptionCount = 1;
		vertInputCreateInfo.pVertexBindingDescriptions = &bindingDescription;	
		vertInputCreateInfo.vertexAttributeDescriptionCount = static_cast<uint32_t>(attributeDescription.size());
		vertInputCreateInfo.pVertexAttributeDescriptions = attributeDescription.data();
		//2. 输入装配阶段, 描述两个信息：顶点数据定义了哪种类型的几何图元，以及是否启用几何图元重启。抽象了GPU的input assembler组件，它描述了将一个个离散的vertex按照topology的方式组织成primitives
		VkPipelineInputAssemblyStateCreateInfo inputAssemblyCreateInfo{};
		inputAssemblyCreateInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
		inputAssemblyCreateInfo.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
		inputAssemblyCreateInfo.primitiveRestartEnable = false;

		//3. viewport配置
		/*
			创建swapchian中指定的extent就是render出来的图像的原本大小
			1. 指定viewport相当于一次transform，它将render出来的图像变换到frambuffer上，就是说画frambuffer这个大画布的哪一部分，
				会将swapchain中的图像完整的画到frambuffer中指定的区域，那明显就会出现一些拉伸压缩之类的情况，除非指定的viewport和frambuffer和swapchain中的extent大小都相等
			2. 当画到frambuffer之后(可能已经发生了拉伸或者压缩情况)，然后再scissor选择剪裁出framebuffer的哪一部分
		*/
		VkViewport viewport{};
		viewport.x = 0;
		viewport.y = 0;
		viewport.width = extent.width ;
		viewport.height = extent.height;
		viewport.minDepth = 0.f;
		viewport.maxDepth = 1.f;
		//4. clipp 空间
		VkRect2D scissor{};
		scissor.offset = {0, 0};//{ (int32_t)extent.width/2, (int32_t)extent.height/2 };
		scissor.extent = extent;

		//5. 视口创建state
		VkPipelineViewportStateCreateInfo viewportCreateInfo{};
		viewportCreateInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
		viewportCreateInfo.viewportCount = 1;
		viewportCreateInfo.pViewports = &viewport;
		viewportCreateInfo.scissorCount = 1;
		viewportCreateInfo.pScissors = &scissor;

		//6. 光栅化state
		VkPipelineRasterizationStateCreateInfo rasterizationCreateInfo{};
		rasterizationCreateInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
		rasterizationCreateInfo.depthClampEnable = VK_FALSE; //是否将近平面远平面之外的物体深度投射到近、远平面上
		rasterizationCreateInfo.polygonMode = VK_POLYGON_MODE_FILL; //GPU若支持，还可以画点、线框
		rasterizationCreateInfo.lineWidth = 1.f;
		rasterizationCreateInfo.cullMode = VK_CULL_MODE_BACK_BIT; //面剔除，禁用、正面、背面
		rasterizationCreateInfo.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;//VK_FRONT_FACE_CLOCKWISE; //指定什么是正面
		rasterizationCreateInfo.depthBiasEnable = VK_FALSE; //以下与深度的矫正有关
		rasterizationCreateInfo.depthBiasConstantFactor = 0.f;
		rasterizationCreateInfo.depthBiasSlopeFactor = 0.f;
		rasterizationCreateInfo.depthBiasClamp = 0.f;

		//7. Multi sampling state
		VkPipelineMultisampleStateCreateInfo multisamplingCreateInfo{};
		multisamplingCreateInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
		multisamplingCreateInfo.sampleShadingEnable = VK_FALSE;
		multisamplingCreateInfo.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;
		multisamplingCreateInfo.minSampleShading = 1.f;
		multisamplingCreateInfo.pSampleMask = nullptr;
		multisamplingCreateInfo.alphaToCoverageEnable = VK_FALSE;
		multisamplingCreateInfo.alphaToOneEnable = VK_FALSE;

		//8. 深度缓冲和模板
		VkPipelineDepthStencilStateCreateInfo depthStencilCI{};
		depthStencilCI.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
		depthStencilCI.depthTestEnable = VK_TRUE;
		depthStencilCI.depthWriteEnable = VK_TRUE;
		depthStencilCI.depthCompareOp = VK_COMPARE_OP_LESS;
		depthStencilCI.depthBoundsTestEnable = VK_FALSE;  //不需要设置深度边界
		depthStencilCI.stencilTestEnable = VK_FALSE;	//不开启模板检测


		//9. color blending
		/*
			1. 混合旧值和新值产生最终的颜色 2. 位运算组合旧值和新值
			主要目的是通过alpha通道产生半透明效果
		*/

		//每个绑定的帧缓冲进行单独的颜色混合配置
		VkPipelineColorBlendAttachmentState colorBlendAttachment{};
		colorBlendAttachment.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
		/*
			if (blendEnable) {
				finalColor.rgb = (srcColorBlendFactor * newColor.rgb) <colorBlendOp> (dstColorBlendFactor * oldColor.rgb);
				finalColor.a = (srcAlphaBlendFactor * newColor.a) <alphaBlendOp> (dstAlphaBlendFactor * oldColor.a);
			} else {
				finalColor = newColor;
			}

			finalColor = finalColor & colorWriteMask;
		*/
		colorBlendAttachment.blendEnable = VK_FALSE;
		colorBlendAttachment.srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;   //实现混合 alpha * srcColor + (1-alpha) * dstColor
		colorBlendAttachment.dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
		colorBlendAttachment.colorBlendOp = VK_BLEND_OP_ADD; // Optional
		colorBlendAttachment.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE; // Optional
		colorBlendAttachment.dstAlphaBlendFactor = VK_BLEND_FACTOR_ZERO; // Optional
		colorBlendAttachment.alphaBlendOp = VK_BLEND_OP_ADD; // Optional
		//配置所有帧缓冲的color blending 以及设置全局常量
		VkPipelineColorBlendStateCreateInfo colorBlendCreateInfo{};
		colorBlendCreateInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
		colorBlendCreateInfo.attachmentCount = 1;
		colorBlendCreateInfo.pAttachments = &colorBlendAttachment;
		colorBlendCreateInfo.logicOpEnable = VK_FALSE;		//VK_TRUE时指定第二种混合模式
		colorBlendCreateInfo.logicOp = VK_LOGIC_OP_COPY; //TODO, 这个和下面4个什么含义
		colorBlendCreateInfo.blendConstants[0] = 0.f;
		colorBlendCreateInfo.blendConstants[1] = 0.f;
		colorBlendCreateInfo.blendConstants[2] = 0.f;
		colorBlendCreateInfo.blendConstants[3] = 0.f;
		//10. 管线的动态状态, 会导致上面设置的状态失效，需要在渲染时重新指定
		/*只有非常有限的管线状态可以在不重建管线的情况下进行动态修改。这包括视口大小，线宽和混合常量。*/
		VkDynamicState dynamicState[] = {VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_LINE_WIDTH };
		VkPipelineDynamicStateCreateInfo dynamicCreateInfo{};
		dynamicCreateInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
		dynamicCreateInfo.dynamicStateCount = 2;
		dynamicCreateInfo.pDynamicStates = dynamicState;

		//11. 创建 pipeline layout对象，指定pipeline中使用到的uniform全局变量,Pipeline Layout 定义了 Shader 和 Descriptor Set 之间的接口
		VkPipelineLayoutCreateInfo layoutCreateInfo{}; //TODO;
		layoutCreateInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
		//layoutCreateInfo.setLayoutCount = 0;
		//layoutCreateInfo.pSetLayouts = nullptr;
		layoutCreateInfo.setLayoutCount = 1; //添加descriptorSetLayout，一个pipeline中为什么需要多个layout呢？
		layoutCreateInfo.pSetLayouts = &descriptorSetLayout;
		layoutCreateInfo.pushConstantRangeCount = 0;
		layoutCreateInfo.pPushConstantRanges = nullptr;

		if (vkCreatePipelineLayout(logiDevice, &layoutCreateInfo, nullptr, &pipelineLayout) != VK_SUCCESS) {
			throw std::runtime_error("failed to create pipeline layout");
		}

		//12. 创建pipeline 对象
		VkGraphicsPipelineCreateInfo pipelineCreateInfo{};
		pipelineCreateInfo.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
		pipelineCreateInfo.stageCount = 2;
		pipelineCreateInfo.pStages = shaderStageCreateInfos;
		pipelineCreateInfo.pVertexInputState = &vertInputCreateInfo;
		pipelineCreateInfo.pInputAssemblyState = &inputAssemblyCreateInfo;
		pipelineCreateInfo.pViewportState = &viewportCreateInfo;
		pipelineCreateInfo.pMultisampleState = &multisamplingCreateInfo;
		pipelineCreateInfo.pRasterizationState = &rasterizationCreateInfo;
		pipelineCreateInfo.pDepthStencilState = &depthStencilCI;
		pipelineCreateInfo.pColorBlendState = &colorBlendCreateInfo;
		pipelineCreateInfo.pDynamicState = nullptr;		//暂时不使用动态状态
		pipelineCreateInfo.layout = pipelineLayout;
		pipelineCreateInfo.basePipelineHandle = VK_NULL_HANDLE; //是否从哪个管线中继承
		pipelineCreateInfo.basePipelineIndex = -1; //自己是否可以被继承
		pipelineCreateInfo.pTessellationState = nullptr; //TODO


		pipelineCreateInfo.renderPass = renderPass;
		pipelineCreateInfo.subpass = 0; //引用之前创建的渲染流程对象和图形管线使用的子流程在子流程数组中的索引

		if (vkCreateGraphicsPipelines(logiDevice, VK_NULL_HANDLE, 1, &pipelineCreateInfo, nullptr, &graphicsPipeline) != VK_SUCCESS) {
			throw std::runtime_error("failed to create graphics pipeline");
		}

		vkDestroyShaderModule(logiDevice, vertShaderModule, nullptr);
		vkDestroyShaderModule(logiDevice, fragShaderModule, nullptr);
	}

	//创建shader模块
	VkShaderModule createShaderModule(const std::vector<char>& shaderCode) {
		VkShaderModuleCreateInfo createInfo{};
		createInfo.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
		createInfo.codeSize = shaderCode.size();
		createInfo.pCode = reinterpret_cast<const uint32_t*>(shaderCode.data()); //使用uint32_t* 只针对目的是让指针指向的数据符合uint32的对齐方式，vector<char>是符合的，所以只需要重新解释一下vector<char>::data()的返回的指针为uint32_t

		VkShaderModule shaderModule{};
		if (vkCreateShaderModule(logiDevice, &createInfo, nullptr, &shaderModule) != VK_SUCCESS) {
			throw std::runtime_error("failed to create shader module");
		}
		return shaderModule;
	}

	//读取二进制Sparc-V shader
	std::vector<char> readFile(std::string path) {
		std::ifstream file(path, std::ios::ate | std::ios::binary); //at end用来获取文件大小, binary以二进制形式打开
		if (!file.is_open()) {
			throw std::runtime_error("faile to open file: " + path);
		}
		size_t fileSize = file.tellg();
		file.seekg(0);
		std::vector<char> ret(fileSize);
		file.read(ret.data(), fileSize);
		file.close();
		return ret;
	}

	/*在进行管线创建之前，我们还需要设置用于渲染的帧缓冲attachment。
	需要指定使用的颜色和深度缓冲，以及采样数，渲染操作如何处理缓冲的内容。
	所有这些信息被vk包装为一个渲染流程对象*/

	void createRenderPass() {
		//代表swap chain 图像的 color attachment, 描述了渲染通道的输出(格式，采样。。。信息) 的数组(因为还有depth attachment)，
		VkAttachmentDescription colorAttachment{};
		colorAttachment.format = surfaceFormat.format;
		colorAttachment.samples = VK_SAMPLE_COUNT_1_BIT; //采样频率
		colorAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR; //指定渲染前将color attachment中的像素清零 黑色
		colorAttachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE; //渲染后内容会被存起来，之后会读取
		//TODOvk中的纹理和帧缓冲由特定像素格式的VkImage对象来表示。图像的像素数据在内存中的分布取决于我们要对图像进行的操作
		colorAttachment.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;		//attchment进入renderpass之前的layout (状态)
		colorAttachment.finalLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR; //离开renderpass之后的layoout

		//为渲染流程创建 depth attachment描述
		VkAttachmentDescription depthAttachment{};
		depthAttachment.format = findDepthFormat();
		depthAttachment.samples = VK_SAMPLE_COUNT_1_BIT;

		depthAttachment.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
		depthAttachment.finalLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
		depthAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
		depthAttachment.storeOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;

		std::array<VkAttachmentDescription, 2> attachments{ colorAttachment, depthAttachment };

		//子流程，attachment reference，子流程访问
		/*
		Vulkan中的subpass使用VkAttachmentReference来引用attachment。每个subpass都会使用一组attachment来进行渲染，并通过VkAttachmentReference结构体来指定subpass中使用的attachment的索引和使用方式。
		这些attachment的定义是在渲染流程中的VkRenderPass对象中进行的，而subpass则指定了哪些attachment用于该子渲染过程以及如何使用它们。这种设计允许我们在不同的子渲染过程中重用相同的attachment，从而提高了渲染效率。
		*/
		VkAttachmentReference colorAttachmentRef{};
		colorAttachmentRef.attachment = 0;	//对于Attachment的索引
		colorAttachmentRef.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL; //TODO 
		
		VkAttachmentReference depthAttachmentRef{};
		depthAttachmentRef.attachment = 1;
		depthAttachmentRef.layout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;


		VkSubpassDescription subpass{};
		subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;  //指定这是图形渲染子流程
		subpass.colorAttachmentCount = 1;
		subpass.pColorAttachments = &colorAttachmentRef;
		subpass.pDepthStencilAttachment = &depthAttachmentRef;


		//TODO子流程依赖
		VkSubpassDependency dependency{};
		dependency.srcSubpass = VK_SUBPASS_EXTERNAL;
		dependency.dstSubpass = 0;
		//指定需要等待的管线阶段和子流程将进行的操作类型
		dependency.srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
		dependency.srcAccessMask = 0;
		dependency.dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
		dependency.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT; //子流程将会进行颜色附着的读写操作。这样设置后，图像布局变换直到必要时才会进行

		VkRenderPassCreateInfo renderPassCreateInfo{};
		renderPassCreateInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
		renderPassCreateInfo.attachmentCount = static_cast<uint32_t>(attachments.size());
		renderPassCreateInfo.pAttachments = attachments.data();
		renderPassCreateInfo.subpassCount = 1;
		renderPassCreateInfo.pSubpasses = &subpass;
		renderPassCreateInfo.dependencyCount = 1;
		renderPassCreateInfo.pDependencies = &dependency;

		if (vkCreateRenderPass(logiDevice, &renderPassCreateInfo, nullptr, &renderPass) != VK_SUCCESS) {
			throw std::runtime_error("failed to create render pass");
		}
	}

	void createFramebuffers() {
		swapChainFrambuffers.resize(swapChainImageViews.size());
		for (int i = 0, n = swapChainFrambuffers.size(); i < n; ++i) {
			//为交换链的每一个imageView对象创建对应的帧缓冲
			std::array< VkImageView, 2> attachments = { swapChainImageViews[i], depthImageView };

			VkFramebufferCreateInfo createInfo{};
			createInfo.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
			createInfo.renderPass = renderPass; //先指定帧缓冲需要兼容的渲染流程对象
			createInfo.width = extent.width;
			createInfo.height = extent.height;
			createInfo.attachmentCount = static_cast<uint32_t>(attachments.size());
			createInfo.pAttachments = attachments.data();
			createInfo.layers = 1; //使用的交换链中的图像都是单层的

			if (vkCreateFramebuffer(logiDevice, &createInfo, nullptr, &swapChainFrambuffers[i])) {
				throw std::runtime_error("faile to create framebuffer");
			}
		}
	}

	void createCommandPool() {
		VkCommandPoolCreateInfo createInfo{};
		createInfo.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
		//指令池分配的的指令缓冲对象只能提交给一个特定类型的队列
		createInfo.queueFamilyIndex = indices.graphicsFamily;
		createInfo.flags = 0; 



		if (vkCreateCommandPool(logiDevice, &createInfo, nullptr, &commandPool) != VK_SUCCESS) {
			throw std::runtime_error("failed to create command pool");
		}
	}

	void createCommandBuffers() {
		commandBuffers.resize(swapChainFrambuffers.size()); //TODO为什么与framebuffer数量相同
		//从commandpool中分配空间
		VkCommandBufferAllocateInfo allocInfo{};
		allocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
		allocInfo.commandBufferCount = commandBuffers.size();
		allocInfo.commandPool = commandPool;
		allocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;//定分配的指令缓冲对象是主要指令缓冲对象还是辅助指令缓冲对象，主指令提交到队列之后可进行执行，辅助指令可以被其他主指令进行引用
		if (vkAllocateCommandBuffers(logiDevice, &allocInfo, commandBuffers.data()) != VK_SUCCESS) {
			throw std::runtime_error("failed to allocate command bufferes");
		}

		for (int i = 0, n = commandBuffers.size(); i < n; ++i) {
			VkCommandBufferBeginInfo beginInfo{};
			beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
			beginInfo.flags = VK_COMMAND_BUFFER_USAGE_SIMULTANEOUS_USE_BIT; //这使得可以在上一帧还未结束渲染时，提交下一帧的渲染指令
			beginInfo.pInheritanceInfo = nullptr;

			//开始指令缓冲的记录操作
			if (vkBeginCommandBuffer(commandBuffers[i], &beginInfo) != VK_SUCCESS) {
				throw std::runtime_error("failed to begin command buffer");
			}
			
			VkRenderPassBeginInfo renderPassInfo{};
			renderPassInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
			renderPassInfo.renderPass = renderPass; //指定使用的渲染流程对象renderPass
			renderPassInfo.framebuffer = swapChainFrambuffers[i]; //指定使用的framebuffer
			//指定渲染的区域，设置为和使用的attachment大小相同
			renderPassInfo.renderArea.offset = { 0, 0 };
			renderPassInfo.renderArea.extent = extent;
			
			//指定attachment load 即渲染前需要对图像进行清除的颜色
			VkClearValue clearColor{ 0.f, 0.f, 0.f, 1.f };
	

			//指定depth attachment渲染前需要对图像进行清除的颜色
			VkClearValue clearDepth{ 1};

			std::array<VkClearValue, 2> clearValues{ clearColor, clearDepth };
			
			renderPassInfo.clearValueCount = static_cast<uint32_t>(clearValues.size());
			renderPassInfo.pClearValues = clearValues.data();


			//记录指令到指令缓冲的函数的函数名都带有一个vkCmd前缀
			vkCmdBeginRenderPass(commandBuffers[i], &renderPassInfo, VK_SUBPASS_CONTENTS_INLINE); //第三个参数指定所有要执行的指令都在主要指令缓冲中，没有辅助指令缓冲需要执行。
			
			vkCmdBindPipeline(commandBuffers[i], VK_PIPELINE_BIND_POINT_GRAPHICS, graphicsPipeline); //VK_PIPELINE_BIND_POINT_GRAPHICS指定管线是图形管线，因为还有计算管线
			VkBuffer vertexBuffers[] = { vertexBuffer }; //一个绘制命令可能绑定多个顶点缓冲，所以使用VertexBuffer数组，并且offsets数组指定顶点缓冲在顶点缓冲数组中的偏移
			VkDeviceSize offsets[] = { 0 };

			vkCmdBindVertexBuffers(commandBuffers[i], 0, 1, vertexBuffers, offsets);
			vkCmdBindIndexBuffer(commandBuffers[i], indexBuffer, 0, VK_INDEX_TYPE_UINT32);

			//为每个交换链图像绑定对应的desciptor set
			vkCmdBindDescriptorSets(commandBuffers[i], VK_PIPELINE_BIND_POINT_GRAPHICS, pipelineLayout, 0, 1, &descriptorSets[i], 0, nullptr);

			vkCmdDrawIndexed(commandBuffers[i], static_cast<uint32_t>(vertexIndices.size()), 1, 0, 0, 0);
			//vkCmdDraw(commandBuffers[i], static_cast<uint32_t>(vertices.size()), 1, 0, 0);
			
			vkCmdEndRenderPass(commandBuffers[i]);


			//结束指令记录到指令缓冲操作
			if (vkEndCommandBuffer(commandBuffers[i]) != VK_SUCCESS) { 
				throw std::runtime_error("failed to record command buffer");
			}
		}
	}
	/*void recordCommmand(VkCommandBuffer commandBuffer, uint32_t imageIndex) {
		VkCommandBufferBeginInfo beginInfo{};
		beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
		beginInfo.flags = VK_COMMAND_BUFFER_USAGE_SIMULTANEOUS_USE_BIT; //这使得可以在上一帧还未结束渲染时，提交下一帧的渲染指令
		beginInfo.pInheritanceInfo = nullptr;

		//开始指令缓冲的记录操作
		if (vkBeginCommandBuffer(commandBuffer, &beginInfo) != VK_SUCCESS) {
			throw std::runtime_error("failed to begin command buffer");
		}

		VkRenderPassBeginInfo renderPassInfo{};
		renderPassInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
		renderPassInfo.renderPass = renderPass; //指定使用的渲染流程对象renderPass
		renderPassInfo.framebuffer = swapChainFrambuffers[imageIndex]; //指定使用的framebuffer
		//指定渲染的区域，设置为和使用的attachment大小相同
		renderPassInfo.renderArea.offset = { 0, 0 };
		renderPassInfo.renderArea.extent = extent;

		//指定attachment load 即渲染前需要对图像进行清除的颜色
		VkClearValue clearColor{ 0.f, 0.f, 0.f, 1.f };
		renderPassInfo.clearValueCount = 1;
		renderPassInfo.pClearValues = &clearColor;


		//记录指令到指令缓冲的函数的函数名都带有一个vkCmd前缀
		vkCmdBeginRenderPass(commandBuffer, &renderPassInfo, VK_SUBPASS_CONTENTS_INLINE); //第三个参数指定所有要执行的指令都在主要指令缓冲中，没有辅助指令缓冲需要执行。

		vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, graphicsPipeline); //VK_PIPELINE_BIND_POINT_GRAPHICS指定管线是图形管线，因为还有计算管线
		VkBuffer vertexBuffers[] = { vertexBuffer }; //一个绘制明亮可能绑定多个顶点缓冲，所以使用VertexBuffer数组，并且offsets数组指定顶点缓冲在顶点缓冲数组中的偏移
		VkDeviceSize offsets[] = { 0 };

		vkCmdBindVertexBuffers(commandBuffer, 0, 1, vertexBuffers, offsets);
		vkCmdBindIndexBuffer(commandBuffer, indexBuffer, 0, VK_INDEX_TYPE_UINT32);

		//为每个交换链图像绑定对应的desciptor set
		vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, pipelineLayout, 0, 1, &descriptorSets[imageIndex], 0, nullptr);

		vkCmdDrawIndexed(commandBuffer, static_cast<uint32_t>(vertexIndices.size()), 1, 0, 0, 0);
		//vkCmdDraw(commandBuffers[i], static_cast<uint32_t>(vertices.size()), 1, 0, 0);

		vkCmdEndRenderPass(commandBuffer);


		//结束指令记录到指令缓冲操作
		if (vkEndCommandBuffer(commandBuffer) != VK_SUCCESS) {
			throw std::runtime_error("failed to record command buffer");
		}

	}
	*/

	void createSyncObjects() {
		imageAvaliableSemaphores.resize(MAX_FRAMES_IN_FLIGHT);
		renderFinishedSemaphores.resize(MAX_FRAMES_IN_FLIGHT);
		inFlightFences.resize(MAX_FRAMES_IN_FLIGHT);

		VkSemaphoreCreateInfo semaCreateInfo{};
		semaCreateInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;

		VkFenceCreateInfo fenceCreateInfo{};
		fenceCreateInfo.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
		//fence对象初始时信号因该是被设置的，否则第一次绘制无法开始
		fenceCreateInfo.flags = VK_FENCE_CREATE_SIGNALED_BIT;
		
		


		for (int i = 0; i < MAX_FRAMES_IN_FLIGHT; ++i) {
			if (vkCreateSemaphore(logiDevice, &semaCreateInfo, nullptr, &imageAvaliableSemaphores[i]) != VK_SUCCESS) {
				throw std::runtime_error("failed to create semaphore");
			}
			if (vkCreateSemaphore(logiDevice, &semaCreateInfo, nullptr, &renderFinishedSemaphores[i]) != VK_SUCCESS) {
				throw std::runtime_error("failed to create semaphore");
			}
			if (vkCreateFence(logiDevice, &fenceCreateInfo, nullptr, &inFlightFences[i]) != VK_SUCCESS) {
				throw std::runtime_error("failed to create fence");
			}
		}


		
	}

	void drawFrame() {



		//0. 等待当前帧的队列是否可用
		vkWaitForFences(logiDevice, 1, &inFlightFences[currentFrame], VK_TRUE, std::numeric_limits<uint64_t>::max());
		//1. 从交换链中获取一张图像
		uint32_t imageIndex;//输出可用的交换链图像的索引，使用此索引获取对应的交换链中的image以及对应的指令缓冲
		VkResult result = vkAcquireNextImageKHR(logiDevice, swapChain, std::numeric_limits<uint64_t>::max(), imageAvaliableSemaphores[currentFrame], VK_NULL_HANDLE, &imageIndex); //可以使用 semaphore 和 fence进行同步
		if (result == VK_ERROR_OUT_OF_DATE_KHR ) {
			recreateSwapChain();
			return; //此时返回合理，因为没有重置fence的状态，可以下次wait成功
		}
		else if (result != VK_SUCCESS && result != VK_SUBOPTIMAL_KHR) {
			throw std::runtime_error("failed to accquire swapchain image");
		}

		vkResetFences(logiDevice, 1, &inFlightFences[currentFrame]);	//需要手动将fence改为未发出信号阶段
		
		//TODO
		updateUniformBuffer(currentFrame);

		//2. 通过VkSubmitInfo结构体来提交信息给指令队列：
		VkSubmitInfo submitInfo{};
		submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
		
		VkSemaphore waitSemaphores[] = { imageAvaliableSemaphores[currentFrame]};
		VkPipelineStageFlags waitStages[] = { VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT }; //等待图形管线到达可以写入color attachment的阶段， 等待阶段与waitSemaphore一一对应

		submitInfo.waitSemaphoreCount = 1;
		submitInfo.pWaitSemaphores = waitSemaphores;
		submitInfo.pWaitDstStageMask = waitStages;

		//指定实际被提交执行的指令缓冲对象
		submitInfo.commandBufferCount = 1;
		submitInfo.pCommandBuffers = &commandBuffers[imageIndex];

		//指定在指令缓冲执行结束后发出信号的信号量对象
		VkSemaphore signalSemaphores[] = { renderFinishedSemaphores[currentFrame]};
		submitInfo.signalSemaphoreCount = 1;
		submitInfo.pSignalSemaphores = signalSemaphores;

		//3. 提交指令缓冲给图形队列，   指定在指令缓冲执行结束后需要发起信号的fence对象, 然后说明当前帧指令已经被提交，所以下一次drawFrame可以继续对当前队列继续提交指令
		if (vkQueueSubmit(graphicsQueue, 1, &submitInfo, inFlightFences[currentFrame]) != VK_SUCCESS) {
			throw std::runtime_error("failed to submit command to graphics queue");
		}
	
		//4. 渲染的图像返回给交换链进行呈现操作
		VkPresentInfoKHR presentInfo{};
		presentInfo.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
		//指定开始呈现操作需要等待的信号量
		presentInfo.waitSemaphoreCount = 1;
		presentInfo.pWaitSemaphores = signalSemaphores;
		//指定呈现图像的交换链，以及呈现图像在交换链中的索引, TODO: 呈现的交换链还可以指定多个？
		VkSwapchainKHR swapChains[] = { swapChain };
		presentInfo.swapchainCount = 1;
		presentInfo.pSwapchains = swapChains;
		presentInfo.pImageIndices = &imageIndex;
		presentInfo.pResults = nullptr;

		result = vkQueuePresentKHR(presentQueue, & presentInfo);
		if (result == VK_ERROR_OUT_OF_DATE_KHR || result == VK_SUBOPTIMAL_KHR || frameBufferResized) { //VK_SUBOPTIMAL_KHR选择链不完全匹配时也重建交换链
			frameBufferResized = false;
			recreateSwapChain();
		}
		else if (result != VK_SUCCESS) {
			throw std::runtime_error("failed to present swapchain image");
		}
		currentFrame = (currentFrame + 1) % MAX_FRAMES_IN_FLIGHT;
	}

	void updateUniformBuffer(uint32_t currentImage) {
		static auto startTime = std::chrono::high_resolution_clock::now();

		auto currentTime = std::chrono::high_resolution_clock::now();
		float time = std::chrono::duration<float, std::chrono::seconds::period>(currentTime - startTime).count();
		UniformBufferObjcet ubo{};
		ubo.model = glm::rotate(glm::mat4(1.0f), time * glm::radians(90.0f), glm::vec3(0.0f, 0.0f, 1.0f));

		ubo.view = glm::lookAt(glm::vec3(2.0f, 2.0f, 2.0f), glm::vec3(0.0f, 0.0f, 0.0f), glm::vec3(0.0f, 0.0f, 1.0f));

		ubo.projection = glm::perspective(glm::radians(45.0f), extent.width / (float)extent.height, 0.1f, 10.0f);
		ubo.projection[1][1] *= -1;
		memcpy(uniformBuffersMapped[currentImage], &ubo, sizeof(ubo));
		
		//UniformBufferObjcet ubo{};
		//ubo.model = glm::translate(glm::mat4(1.f), glm::vec3(0, 0, 1));

		//ubo.view = glm::lookAt(glm::vec3( 0.f, 0.f, -1.f ), { 0.f, 0.f, 0.f }, { 0.f, 0.f, -1.f });
		//
		////ubo.projection = glm::perspective(45.f, extent.width / (float)extent.height, 0.1f, 100.f);
		//ubo.projection = glm::mat4(1.f);
		//memcpy(uniformBuffersMapped[currentImage], &ubo, sizeof(ubo));
	}

	//窗口大小改变时，交换链需要重新创建，并且依赖于交换链的对象也需要重新创建
	void recreateSwapChain() {
		//处理最小化情况，停止渲染
		int width = 0, height = 0;
		while (width == 0 || height == 0) {
			glfwGetFramebufferSize(window, &width, &height);
			glfwWaitEvents();
		}

		vkDeviceWaitIdle(logiDevice);
		//销毁
		cleanupSwapChain();

		//重新创建
		// 
		//创建交换链
		createSwapChain();

		//创建swap chain image view对象
		createSwapChainImageViews();

		//创建深度缓冲相关资源
		createDepthResources();

		//创建render pass 对象
		createRenderPass(); // 渲染流程依赖于交换链图像的格式, 虽然窗口大小改变，格式不会变，为什么还要重建

		//创建渲染图形管线
		createGraphicsPipeline(); // 视口和裁剪矩形在管线创建时被指定，窗口大小改变，这些设置也需要修改, TODO： 使用dynamic state


		//为交换链中的所有图像创建帧缓冲
		createFramebuffers();

		//分配指令缓冲对象，使用它记录绘制指令
		createCommandBuffers();
	}

	/// <summary>
	/// 查询buffer和应用需要的device memory type
	/// </summary>
	/// <param name="typeFilters">buffer需要的内存类型</param>
	/// <param name="properties">应用指定的内存类型</param>
	/// <returns>类型的索引</returns>
	uint32_t findMemoryType(uint32_t typeFilters, VkMemoryPropertyFlags properties) { //指定
		VkPhysicalDeviceMemoryProperties memProperties; //memProperties.memoryTypes[i].propertyFlags指顶memory的属性，VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT表示设备内存可以映射，所以CPU能够写
		vkGetPhysicalDeviceMemoryProperties(phyDevice, &memProperties);
		for (int i = 0; i < memProperties.memoryTypeCount; ++i) {
			if (typeFilters & (1 << i) && (memProperties.memoryTypes[i].propertyFlags & properties) == properties) {
				return i;
			}
		}

		throw std::runtime_error("failed to find suitable memory type");

	}

	void createVertexBuffer() {

		VkDeviceSize bufferSize = sizeof(vertices[0]) * vertices.size();

		//创建stage buffer，用作中转将RAM中的数据传递到GPU local内存(因为这部分内存不允许映射)
		VkBuffer stagingBuffer;
		VkDeviceMemory stagingMemory;
		createBuffer(bufferSize, VK_BUFFER_USAGE_TRANSFER_SRC_BIT, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, stagingBuffer, stagingMemory);
		//映射内对象使CPU可访问,内存这里创建的内存对象都是GPU中的，CPU想要访问就需要使用内存映射，将GPU内存映射到RAM中
		//但是GPU驱动不会立即将内存中的数据写到GPU内存中，创建buffer时使用VK_MEMORY_PROPERTY_HOST_COHERENT_BIT要求有保证一致性的内存
		void* data;
		vkMapMemory(logiDevice, stagingMemory, 0, bufferSize, 0, &data);
		memcpy(data, vertices.data(), bufferSize);
		vkUnmapMemory(logiDevice, stagingMemory);

		//vertexBuffer指定的内存属性位device local，所以对于GPU进行读取的效率更高，而之前的可能VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT内存效率并不高
		createBuffer(bufferSize, VK_BUFFER_USAGE_TRANSFER_DST_BIT|VK_BUFFER_USAGE_VERTEX_BUFFER_BIT, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, vertexBuffer, vertexBufferMemory);
		
		//从GPU staging buffer 将顶点数据拷贝到 device local区域
		copyBuffer(stagingBuffer, vertexBuffer, bufferSize);


		//释放staging buffer
		vkDestroyBuffer(logiDevice, stagingBuffer, nullptr);
		vkFreeMemory(logiDevice, stagingMemory, nullptr);

	}
	void createIndexBuffer() {

		VkDeviceSize bufferSize = sizeof(vertexIndices[0]) * vertexIndices.size();

		//创建stage buffer，用作中转将RAM中的数据传递到GPU local内存(因为这部分内存不允许映射)
		VkBuffer stagingBuffer;
		VkDeviceMemory stagingMemory;
		createBuffer(bufferSize, VK_BUFFER_USAGE_TRANSFER_SRC_BIT, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, stagingBuffer, stagingMemory);
		//映射内对象使CPU可访问,内存这里创建的内存对象都是GPU中的，CPU想要访问就需要使用内存映射，将GPU内存映射到RAM中
		//但是GPU驱动不会立即将内存中的数据写到GPU内存中，创建buffer时使用VK_MEMORY_PROPERTY_HOST_COHERENT_BIT要求有保证一致性的内存
		void* data;
		vkMapMemory(logiDevice, stagingMemory, 0, bufferSize, 0, &data);
		memcpy(data, vertexIndices.data(), bufferSize);
		vkUnmapMemory(logiDevice, stagingMemory);

		//vertexBuffer指定的内存属性位device local，所以对于GPU进行读取的效率更高，而之前的可能VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT内存效率并不高
		createBuffer(bufferSize, VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_INDEX_BUFFER_BIT, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, indexBuffer, indexBufferMemory);

		//从GPU staging buffer 将顶点数据拷贝到 device local区域
		copyBuffer(stagingBuffer, indexBuffer, bufferSize);


		//释放staging buffer
		vkDestroyBuffer(logiDevice, stagingBuffer, nullptr);
		vkFreeMemory(logiDevice, stagingMemory, nullptr);

	}

	void createUniformBuffers() {
		int size = swapChainImages.size();
		uniformBuffersMapped.resize(size);
		uniformBuffers.resize(size);
		uniformBuffersMemory.resize(size);
		uint32_t bufferSize = sizeof(UniformBufferObjcet);
		for (int i = 0; i < size; ++i) {
			createBuffer(bufferSize, VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, uniformBuffers[i], uniformBuffersMemory[i]);
			//uniform对象每一帧都会进行改变，所以需要整个程序的生命周期都需要映射
			vkMapMemory(logiDevice, uniformBuffersMemory[i], 0, bufferSize, 0, &uniformBuffersMapped[i]);
		}
	}
	//创建Buffer： bufferObj，memoryObj，Bind
	void createBuffer(VkDeviceSize size, VkBufferUsageFlags usage, VkMemoryPropertyFlags properties, VkBuffer& buffer, VkDeviceMemory& bufferMemory) {
		
		VkBufferCreateInfo bufferCreateInfo{};
		bufferCreateInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
		//bufferCreateInfo.queueFamilyIndexCount = ; 不用指定 TODO在创建buffer时这两个参数的意思？
		//bufferCreateInfo.pQueueFamilyIndices = ;
		bufferCreateInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE; //TODO
		bufferCreateInfo.size = size;
		bufferCreateInfo.usage = usage; // TODO: VK_BUFFER_USAGE_VERTEX_BUFFER_BIT什么意思
		//创建VkBuffer
		if (vkCreateBuffer(logiDevice, &bufferCreateInfo, nullptr, &buffer) != VK_SUCCESS) {
			throw std::runtime_error("failed to allocate buffer");
		}

		//获取memory requirement
		VkMemoryRequirements memRequirements{}; // memoryTypeBits中的位表示 memory types that are suitable for the buffer.
		vkGetBufferMemoryRequirements(logiDevice, buffer, &memRequirements);
		

		//显卡可以分配不同类型的内存作为缓冲使用。不同类型的内存所允许进行的操作以及操作的效率有所不同
		VkMemoryAllocateInfo allocInfo{};
		allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
		allocInfo.allocationSize = memRequirements.size; //需要的分配的size 不一定时 createInfo中的size
		allocInfo.memoryTypeIndex = findMemoryType(memRequirements.memoryTypeBits, properties);

		//分配内存
		if (vkAllocateMemory(logiDevice, &allocInfo, nullptr, &bufferMemory) != VK_SUCCESS) {
			throw std::runtime_error("failed to allocate buffer memory");
		}

		//buffer与memory的绑定
		vkBindBufferMemory(logiDevice, buffer, bufferMemory, 0);
	}

	void copyBuffer(VkBuffer srcBuffer, VkBuffer dstBuffer, VkDeviceSize size) {
		//GPU操作需要用命令来声明
		//创建临时命令，使用原来的command pool，TODO：创建专门的command pool可以进行优化
		//VkCommandBufferAllocateInfo allocInfo{};
		//allocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
		//allocInfo.commandPool = commandPool;
		//allocInfo.commandBufferCount = 1;
		//allocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;

		//VkCommandBuffer commandBuffer{};
		//if (vkAllocateCommandBuffers(logiDevice, &allocInfo, &commandBuffer) != VK_SUCCESS) {
		//	throw std::runtime_error("failed to allocate command bufferes");
		//}

		////start recording the command buffer
		//VkCommandBufferBeginInfo beginInfo{};
		//beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
		//beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT; //临时命令，只使用一次

		//if (vkBeginCommandBuffer(commandBuffer, &beginInfo) != VK_SUCCESS) {
		//	throw std::runtime_error("failed to begin command buffer");
		//}
		//VkBufferCopy copyRegion{};
		//copyRegion.size = size;
		//copyRegion.srcOffset = 0;
		//copyRegion.dstOffset = 0;
		//vkCmdCopyBuffer(commandBuffer, srcBuffer, dstBuffer, 1, &copyRegion);
		//if (vkEndCommandBuffer(commandBuffer) != VK_SUCCESS) {
		//	throw std::runtime_error("failed to record command buffer");
		//}
		//
		//VkSubmitInfo submitInfo{};
		//submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
		//submitInfo.commandBufferCount = 1;
		//submitInfo.pCommandBuffers = &commandBuffer;

		//if (vkQueueSubmit(graphicsQueue, 1, &submitInfo, VK_NULL_HANDLE) != VK_SUCCESS) {
		//	throw std::runtime_error("failed to submit command to graphics queue");
		//}
		////等待数据transfer结束 也可以使用 vkWaitForFences
		//vkQueueWaitIdle(graphicsQueue);

		//vkFreeCommandBuffers(logiDevice, commandPool, 1, &commandBuffer);

		VkCommandBuffer commandBuffer = beginSigleTimeCommands();
		VkBufferCopy copyRegion{};
		copyRegion.size = size;
		copyRegion.srcOffset = 0;
		copyRegion.dstOffset = 0;
		vkCmdCopyBuffer(commandBuffer, srcBuffer, dstBuffer, 1, &copyRegion);
		endSigleTimeCommands(commandBuffer);
	}
	

	VkCommandBuffer beginSigleTimeCommands() {
		//GPU操作需要用命令来声明
		//创建临时命令，使用原来的command pool，TODO：创建专门的command pool可以进行优化
		VkCommandBufferAllocateInfo allocInfo{};
		allocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
		allocInfo.commandPool = commandPool;
		allocInfo.commandBufferCount = 1;
		allocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;

		VkCommandBuffer commandBuffer{};
		if (vkAllocateCommandBuffers(logiDevice, &allocInfo, &commandBuffer) != VK_SUCCESS) {
			throw std::runtime_error("failed to allocate command bufferes");
		}

		//start recording the command buffer
		VkCommandBufferBeginInfo beginInfo{};
		beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
		beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT; //临时命令，只使用一次

		if (vkBeginCommandBuffer(commandBuffer, &beginInfo) != VK_SUCCESS) {
			throw std::runtime_error("failed to begin command buffer");
		}
		return commandBuffer;
	}

	void endSigleTimeCommands(VkCommandBuffer commandBuffer) {
		if (vkEndCommandBuffer(commandBuffer) != VK_SUCCESS) {
			throw std::runtime_error("failed to record command buffer");
		}

		VkSubmitInfo submitInfo{};
		submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
		submitInfo.commandBufferCount = 1;
		submitInfo.pCommandBuffers = &commandBuffer;


		VkFence fence{};
		VkFenceCreateInfo fenceCI{};
		fenceCI.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
		vkCreateFence(logiDevice, &fenceCI, nullptr, &fence);

		vkQueueSubmit(graphicsQueue, 1, &submitInfo, fence);

		vkWaitForFences(logiDevice, 1, &fence, VK_TRUE, UINT64_MAX);
		vkDestroyFence(logiDevice, fence, nullptr);

		vkFreeCommandBuffers(logiDevice, commandPool, 1, &commandBuffer);
	}

	void createDescriptorSetLayout() {
		//每一个绑定都需要 VkDescriptorSetLayoutBinding来描述
		VkDescriptorSetLayoutBinding uboLayoutBinding;
		uboLayoutBinding.descriptorCount = 1;
		uboLayoutBinding.binding = 0;     //标识的内存编号为0    //layout (binding=0) uniform UniformBufferObject{...};
		uboLayoutBinding.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER; //descriptor == 资源  --> 资源的类型: uniform buffer
		uboLayoutBinding.stageFlags = VK_SHADER_STAGE_VERTEX_BIT; //uniform对象在那个shader stage使用，opengl中所有uniform都是全局的 VK_SHADER_STAGE_ALL_GRAPHICS
		uboLayoutBinding.pImmutableSamplers = nullptr; //在纹理采样点时候可能用到

		//创建*组合*图像采样器描述符 sampler descriptor的binding
		VkDescriptorSetLayoutBinding samplerLayoutBinding{};
		samplerLayoutBinding.binding = 1;
		samplerLayoutBinding.descriptorCount = 1;
		samplerLayoutBinding.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
		samplerLayoutBinding.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
		samplerLayoutBinding.pImmutableSamplers = nullptr;

		std::array<VkDescriptorSetLayoutBinding, 2> bindings = { uboLayoutBinding, samplerLayoutBinding };




		//所有binding组合成一个descriptorSetLayout，用于指定可以被管线访问的资源类型
		VkDescriptorSetLayoutCreateInfo createInfo{};
		createInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
		createInfo.bindingCount = static_cast<uint32_t>(bindings.size());
		createInfo.pBindings = bindings.data();
		if (vkCreateDescriptorSetLayout(logiDevice, &createInfo, nullptr, &descriptorSetLayout) != VK_SUCCESS) {
			throw std::runtime_error("failed to create descriptor set layout");
		}

	}

	void createDescriptorPool() {
		int size = swapChainImages.size();

		std::array<VkDescriptorPoolSize, 2> poolSizes = {};

		//descirptor的类型以及数量, 对描述符池可以分配的描述符集进行定义
		VkDescriptorPoolSize poolSize{};
		poolSize.type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;  
		poolSize.descriptorCount = static_cast<uint32_t>(size); //描述了 VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER类型的描述符的数量

		poolSizes[0] = poolSize;

		//TODO添加分配VK_DESCRIPTOR_TYPE_COMBIND_SAMPLER的descriptorpoolsize, 好像都能运行，当poolsize.type指定为VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER时Descriptor pool也能分配conbined sampler
		poolSize.type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
		poolSize.descriptorCount = static_cast<uint32_t>(size);
		poolSizes[1] = poolSize;

		VkDescriptorPoolCreateInfo poolCreateInfo{};
		poolCreateInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
		poolCreateInfo.poolSizeCount = static_cast<uint32_t>(poolSizes.size());
		poolCreateInfo.pPoolSizes = poolSizes.data();		//这里的poolSize可以指定一个数组，说明一个descriptor pool可以用来分配多种不同的descriptor set
		poolCreateInfo.maxSets = static_cast<uint32_t>(size); //指定了该描述符池中可以分配的最大描述符集数量

		if (vkCreateDescriptorPool(logiDevice, &poolCreateInfo, nullptr, &descriptorPool) != VK_SUCCESS) {
			throw std::runtime_error("failed to create descriptor pool");
		}
		
	}

	//TODO整块流程
	void createDescriptorSets() {
		int size = MAX_FRAMES_IN_FLIGHT;//swapChainImages.size();
		std::vector<VkDescriptorSetLayout> layouts(size, descriptorSetLayout);

		VkDescriptorSetAllocateInfo allocInfo{};
		allocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
		allocInfo.descriptorPool = descriptorPool;
		allocInfo.descriptorSetCount = static_cast<uint32_t>(size);
		allocInfo.pSetLayouts = layouts.data(); //TODO怎么理解？每一个描述符对象使用的layout是怎样的。create one descriptor set for each frame in flight

		descriptorSets.resize(size);
		if (vkAllocateDescriptorSets(logiDevice, &allocInfo, descriptorSets.data()) != VK_SUCCESS) {
			throw std::runtime_error("failed to allocate descriptor sets");
		}

		//配置每个descriptor set
		for (int i = 0; i < size; ++i) {
			//绑定ubo buffer到descriptor set中的desciptor中
			VkDescriptorBufferInfo bufferInfo{};
			bufferInfo.buffer = uniformBuffers[i];
			bufferInfo.offset = 0;
			bufferInfo.range = sizeof(UniformBufferObjcet);

			//绑定图像和图像采样器到descriptor set中的descriptor
			VkDescriptorImageInfo imageInfo{};
			imageInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
			imageInfo.imageView = textureImageView;
			imageInfo.sampler = textureSampler;


			//VkWriteDescriptorSet descriptorWrite{};
			//descriptorWrite.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
			//descriptorWrite.dstSet = descriptorSets[i];		
			//descriptorWrite.dstBinding = 0;
			//descriptorWrite.dstArrayElement = 0;

			//descriptorWrite.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER | VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
			//descriptorWrite.descriptorCount = 1;

			//descriptorWrite.pBufferInfo = &bufferInfo;
			//descriptorWrite.pImageInfo = nullptr; // Optional
			//descriptorWrite.pTexelBufferView = nullptr; // Optional

			//对于两个不同的descriptor，处于同一个descriptor set也要用两VkWriteDescriptorSet来描述？
			std::array<VkWriteDescriptorSet, 2> descriptorWrites{};
			descriptorWrites[0].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
			descriptorWrites[0].dstSet = descriptorSets[i];
			descriptorWrites[0].dstBinding = 0;
			descriptorWrites[0].dstArrayElement = 0;

			descriptorWrites[0].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
			descriptorWrites[0].descriptorCount = 1;

			descriptorWrites[0].pBufferInfo = &bufferInfo;

			descriptorWrites[1].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
			descriptorWrites[1].dstSet = descriptorSets[i];
			descriptorWrites[1].dstBinding = 1;
			descriptorWrites[1].dstArrayElement = 0;

			descriptorWrites[1].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
			descriptorWrites[1].descriptorCount = 1;
			descriptorWrites[1].pImageInfo = &imageInfo;

			vkUpdateDescriptorSets(logiDevice, static_cast<uint32_t>(descriptorWrites.size()), descriptorWrites.data(), 0, nullptr);

			//vkUpdateDescriptorSets(logiDevice, 1, &descriptorWrite, 0, nullptr);
		}

	}


	void cleanupSwapChain() {
		//销毁深度缓冲相对象
		vkDestroyImageView(logiDevice, depthImageView, nullptr);
		vkFreeMemory(logiDevice, depthImageMemory, nullptr);
		vkDestroyImage(logiDevice, depthImage, nullptr);

		//释放commandBuffers
		vkFreeCommandBuffers(logiDevice, commandPool, static_cast<uint32_t>(commandBuffers.size()), commandBuffers.data());


		//销毁swap chain Frambuffer对象
		for (auto&& frameBuffer : swapChainFrambuffers) {
			vkDestroyFramebuffer(logiDevice, frameBuffer, nullptr);
		}
		//销毁render pass
		vkDestroyRenderPass(logiDevice, renderPass, nullptr);
		//销毁pipeline layout 对象
		vkDestroyPipelineLayout(logiDevice, pipelineLayout, nullptr); //layout 对象在createPipeline中创建
		//销毁pipeline 对象
		vkDestroyPipeline(logiDevice, graphicsPipeline, nullptr);
		//销毁VkImageView对象
		for (auto&& imageView : swapChainImageViews) {
			vkDestroyImageView(logiDevice, imageView, nullptr);
		}
		//All child objects created on device must have been destroyed prior to destroying device
		vkDestroySwapchainKHR(logiDevice, swapChain, nullptr);
	}

	void createImage( uint32_t width, uint32_t height, VkFormat format, VkImageTiling tiling, VkImageUsageFlags usage, VkMemoryPropertyFlagBits properties, VkImage &image, VkDeviceMemory &memory){
		VkImageCreateInfo imageCI{};
		imageCI.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
		imageCI.imageType = VK_IMAGE_TYPE_2D;
		imageCI.extent.width = width;
		imageCI.extent.height = height;
		imageCI.extent.depth = 1;
		imageCI.mipLevels = 1;
		imageCI.arrayLayers = 1;
		imageCI.format = format;
		imageCI.tiling = tiling; //对方问优化的方式排列
		imageCI.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED; ///TODO
		imageCI.usage = usage;
		imageCI.sharingMode = VK_SHARING_MODE_EXCLUSIVE; //该image只会被支持传输操作的队列族使用，使用独占模式
		imageCI.samples = VK_SAMPLE_COUNT_1_BIT; //对纹理不用多重采样，它可能使用的是线性插值
		if (vkCreateImage(logiDevice, &imageCI, nullptr, &image) != VK_SUCCESS) {
			throw std::runtime_error("failed to load texture");
		}
		//获取memory requirement
		VkMemoryRequirements memRequirements{}; // memoryTypeBits中的位表示 memory types that are suitable for the buffer.
		vkGetImageMemoryRequirements(logiDevice, image, &memRequirements);


		//显卡可以分配不同类型的内存作为缓冲使用。不同类型的内存所允许进行的操作以及操作的效率有所不同
		VkMemoryAllocateInfo allocInfo{};
		allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
		allocInfo.allocationSize = memRequirements.size; //需要的分配的size 不一定时 createInfo中的size
		allocInfo.memoryTypeIndex = findMemoryType(memRequirements.memoryTypeBits, properties);


		//分配内存
		if (vkAllocateMemory(logiDevice, &allocInfo, nullptr, &memory) != VK_SUCCESS) {
			throw std::runtime_error("failed to allocate buffer memory");
		}

		//buffer与memory的绑定
		vkBindImageMemory(logiDevice, image, memory, 0);
	}

	//加载图像到vk对象中
	void createTextureImage() {
		//需要使用指令缓冲来完成加载
		int texWidth, texHeight, texChannels;
		stbi_uc* pixels = stbi_load((textureRootDir + "/viking_room.png").c_str(), &texWidth, &texHeight, &texChannels, STBI_rgb_alpha); //强制加载进alpha通道
		if (!pixels) {
			throw std::runtime_error("failed to load texture");
		}
		VkDeviceSize imageSize = texWidth * texHeight * 4;

		//创建textureImage和textureImageMemory，并将两者绑定
		createImage(texWidth, texHeight, VK_FORMAT_R8G8B8A8_SRGB, VK_IMAGE_TILING_OPTIMAL, VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, textureImage, textureImageMemory);
		
		
		/*
		尽管，我们可以在着色器直接访问缓冲中的像素数据，但使用vk的
		图像对象会更好。VkImage的图像对象允许我们使用二维坐标来快速获取颜
		色数据。图像对象的像素数据也被叫做纹素。VkImage可以进行快速sampler*/

		//使用staging buffer将主存中的数据最终转移到GPU的local区域
		VkBuffer stagingBuffer;
		VkDeviceMemory stagingMemory;
		createBuffer(imageSize, VK_BUFFER_USAGE_TRANSFER_SRC_BIT, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, stagingBuffer, stagingMemory);
		
		void* data;
		vkMapMemory(logiDevice, stagingMemory, 0, imageSize, 0, &data);
		memcpy(data, pixels, imageSize);
		vkUnmapMemory(logiDevice, stagingMemory);
		
		stbi_image_free(pixels);

		

		transitionImageLayout(textureImage, VK_FORMAT_R8G8B8A8_SRGB, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);
		copyBufferToImage(stagingBuffer, textureImage, texWidth, texHeight);
		transitionImageLayout(textureImage, VK_FORMAT_R8G8B8A8_SRGB, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
		
		vkFreeMemory(logiDevice, stagingMemory, nullptr);
		vkDestroyBuffer(logiDevice, stagingBuffer, nullptr);
	}

	void createTextureImageView() {
		textureImageView = createImageView(textureImage, VK_FORMAT_R8G8B8A8_SRGB);
	}
	//change the image layout
	void transitionImageLayout(VkImage image, VkFormat format, VkImageLayout oldLayout, VkImageLayout newLayout) {
		VkCommandBuffer commandBuffer =  beginSigleTimeCommands();
		//pipeline barrire
		VkImageMemoryBarrier barrier{};
		barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
		barrier.image = image;
		barrier.oldLayout = oldLayout;
		barrier.newLayout = newLayout;
		barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
		barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED; //用来传递队列的所有权TODO
		barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;//用于指定需要进行同步的图像资源的哪个方面（颜色、深度、模板）的标志位。

		//TODO barrier中的这些字段都是什么？subresource是什么？
		if (newLayout == VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL) {
			barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT;
			if (hasStencilComponent(format)) {
				barrier.subresourceRange.aspectMask |= VK_IMAGE_ASPECT_STENCIL_BIT;
			}
		}
		else {
			barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
		}

		barrier.subresourceRange.layerCount = 1;
		barrier.subresourceRange.baseArrayLayer = 0;
		barrier.subresourceRange.levelCount = 1;
		barrier.subresourceRange.baseMipLevel = 0;


		VkPipelineStageFlags srcStage;
		VkPipelineStageFlags dstStage;

		if (oldLayout == VK_IMAGE_LAYOUT_UNDEFINED && newLayout == VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL) {
			srcStage = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
			dstStage = VK_PIPELINE_STAGE_TRANSFER_BIT;
			barrier.srcAccessMask = 0;
			barrier.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
		}
		else if (oldLayout == VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL && newLayout == VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL) {
			srcStage = VK_PIPELINE_STAGE_TRANSFER_BIT;
			dstStage = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
			barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
			barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
		}
		else if (oldLayout == VK_IMAGE_LAYOUT_UNDEFINED && newLayout == VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL) {
			srcStage = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
			dstStage = VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT; //这个stage会读取深度缓冲，所以在这个阶段之前需要设置屏障， VK_PIPELINE_STAGE_LATER_FRAGMENT_TESTS_BIT写入新的深度值
			barrier.srcAccessMask = 0;
			barrier.dstAccessMask = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
		}
		else {
			throw std::runtime_error("unsupported layout transition");
		}

		vkCmdPipelineBarrier(commandBuffer, srcStage, dstStage, 0, 0, nullptr, 0, nullptr, 1, &barrier);

		endSigleTimeCommands(commandBuffer);
	}

	void copyBufferToImage(VkBuffer buffer, VkImage image, uint32_t width, uint32_t height) {
		VkCommandBuffer commandBuffer = beginSigleTimeCommands();

		VkBufferImageCopy region{};
		region.bufferOffset = 0;
		region.bufferRowLength = 0;
		region.bufferImageHeight = 0;

		region.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
		region.imageSubresource.baseArrayLayer = 0;
		region.imageSubresource.layerCount = 1;
		region.imageSubresource.mipLevel = 0;
	
		region.imageOffset = { 0, 0, 0 };
		region.imageExtent = { width, height, 1 };

		//图像布局为最适合作为 transfer destination的布局
		vkCmdCopyBufferToImage(commandBuffer, buffer, image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);

		endSigleTimeCommands(commandBuffer);
	}
	
	void createTextureSampler() {
		VkSamplerCreateInfo samplerCI{};
		samplerCI.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
		samplerCI.magFilter = VK_FILTER_LINEAR;
		samplerCI.minFilter = VK_FILTER_LINEAR;

		//纹理环绕
		samplerCI.addressModeU = VK_SAMPLER_ADDRESS_MODE_REPEAT;
		samplerCI.addressModeV = VK_SAMPLER_ADDRESS_MODE_REPEAT;
		samplerCI.addressModeW = VK_SAMPLER_ADDRESS_MODE_REPEAT;
		//各向异性过滤
		samplerCI.anisotropyEnable = VK_TRUE;
		samplerCI.maxAnisotropy = 16;
		
		samplerCI.borderColor = VK_BORDER_COLOR_INT_OPAQUE_BLACK;

		samplerCI.unnormalizedCoordinates = VK_FALSE; //将纹理坐标变化到[0, 1)之间

		samplerCI.compareEnable = VK_FALSE; //TODO
		samplerCI.compareOp = VK_COMPARE_OP_ALWAYS;

		samplerCI.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
		samplerCI.mipLodBias = 0.f; //TODO
		samplerCI.minLod = 0.f; 
		samplerCI.maxLod = 0.f;

		if (vkCreateSampler(logiDevice, &samplerCI, nullptr, &textureSampler) != VK_SUCCESS) {
			throw std::runtime_error("unsupported create sampler");
		}

	}

	VkFormat findSupportedFormat(const std::vector<VkFormat>& candidates, VkImageTiling tiling, VkFormatFeatureFlags features) {
		for (VkFormat format : candidates) {
			VkFormatProperties props;
			vkGetPhysicalDeviceFormatProperties(phyDevice, format, &props);

			if (tiling == VK_IMAGE_TILING_LINEAR && (props.linearTilingFeatures & features) == features) {
				return format;
			}
			else if (tiling == VK_IMAGE_TILING_OPTIMAL && (props.optimalTilingFeatures & features) == features) {
				return format;
			}
		}

		throw std::runtime_error("failed to find supported format!");
	}

	VkFormat findDepthFormat() {
		return findSupportedFormat(
			{ VK_FORMAT_D32_SFLOAT, VK_FORMAT_D32_SFLOAT_S8_UINT, VK_FORMAT_D24_UNORM_S8_UINT },
			VK_IMAGE_TILING_OPTIMAL,
			VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT
		);
	}

	bool hasStencilComponent(VkFormat format) {
		return format == VK_FORMAT_D32_SFLOAT_S8_UINT || format == VK_FORMAT_D24_UNORM_S8_UINT;
	}


	void createDepthResources() {
		VkFormat depthForamt = findDepthFormat();
		//usage VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT 指定该image作为深度图使用
		createImage(extent.width, extent.height, depthForamt, VK_IMAGE_TILING_OPTIMAL, VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, depthImage, depthImageMemory);

		depthImageView =  createImageView(depthImage, depthForamt, VK_IMAGE_ASPECT_DEPTH_BIT);

		//更改depth image的layout使得它使用与作为深度图
		transitionImageLayout(depthImage, depthForamt, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL);
	}
	void cleanup() {


		//销毁texutre相关的对象
		vkDestroySampler(logiDevice, textureSampler, nullptr);
		vkDestroyImageView(logiDevice, textureImageView, nullptr);
		vkFreeMemory(logiDevice, textureImageMemory, nullptr);
		vkDestroyImage(logiDevice, textureImage, nullptr);



		//销毁uniform缓冲对象，释放设备内存，取消映射
		for (int i = 0, n = swapChainImages.size(); i < n; ++i) {
			vkUnmapMemory(logiDevice, uniformBuffersMemory[i]);
			vkFreeMemory(logiDevice, uniformBuffersMemory[i], nullptr);
			vkDestroyBuffer(logiDevice, uniformBuffers[i], nullptr);
		}
		//销毁Descirptor pool
		vkDestroyDescriptorPool(logiDevice, descriptorPool, nullptr);

		////销毁descriptorSetLayout
		vkDestroyDescriptorSetLayout(logiDevice, descriptorSetLayout, nullptr);
		

		//销毁与交换链相关的所有对象
		cleanupSwapChain();

		//销毁顶点缓冲对象，释放缓冲占用设备内存
		vkFreeMemory(logiDevice, vertexBufferMemory, nullptr);
		vkDestroyBuffer(logiDevice, vertexBuffer, nullptr);

		////销毁顶点索引缓冲对象，释放缓冲占用设备内存
		vkFreeMemory(logiDevice, indexBufferMemory, nullptr);
		vkDestroyBuffer(logiDevice, indexBuffer, nullptr);

		//销毁每一帧的信号量对象和fence对象
		for (int i = 0; i < MAX_FRAMES_IN_FLIGHT; ++i) {
			vkDestroySemaphore(logiDevice, imageAvaliableSemaphores[i], nullptr);
			vkDestroySemaphore(logiDevice, renderFinishedSemaphores[i], nullptr);
			vkDestroyFence(logiDevice, inFlightFences[i], nullptr);
		}


		//销毁commmand pool对象
		vkDestroyCommandPool(logiDevice, commandPool, nullptr);


		//逻辑设备对象创建后，应用程序结束前，需要手动清除
		vkDestroyDevice(logiDevice, nullptr);
		//销毁surface
		vkDestroySurfaceKHR(instance, surface, nullptr);
		
		//销毁validation layer对象
		if (enableValidationLayers) {
			//同样扩展函数vkDestroyDebugUtilsMessengerEXT也不会被vulkan自动加载，需要手动进行
			auto func = (PFN_vkDestroyDebugUtilsMessengerEXT)vkGetInstanceProcAddr(instance, "vkDestroyDebugUtilsMessengerEXT");
			if (func != nullptr) {
				func(instance, callback, nullptr);
			}
		}

		//销毁instance
		vkDestroyInstance(instance, nullptr);

        glfwDestroyWindow(window);

        glfwTerminate();
    }

	void mainLoop() {
		while (!glfwWindowShouldClose(window)) {
			glfwPollEvents();
			drawFrame();
		}
		vkDeviceWaitIdle(logiDevice);
	}

private:
	GLFWwindow* window;
	VkInstance instance;

	//此对象存储回调函数信息，再vkCreateDebugUtilsMessengerEXT vkDestroyDebugUtilsMessengerEXT
	VkDebugUtilsMessengerEXT callback;
	//物理设备，队列族索引
	VkPhysicalDevice  phyDevice;
	QueueFamilyInices indices;

	//逻辑设备，选择物理设备后，我们还需要一个逻辑设备来作为和物理设备交互的接口
	VkDevice logiDevice;
	//创建逻辑设备时指定的队列会随着逻辑设备一同被创建，为了方便，我们添加了一个成员变量来直接存储逻辑设备的队列句柄
	VkQueue graphicsQueue;
	VkQueue presentQueue;

	//surface
	VkSurfaceKHR surface;
	//支持swap chain的扩展
	SwapChainSupportDetails swapChainSupport;
	//交换链对象
	VkSwapchainKHR swapChain;
	//创建交换链对象是选择的扩展
	VkSurfaceFormatKHR surfaceFormat;
	VkPresentModeKHR  presentMode;
	VkExtent2D  extent;

	//获取交换链图像的图像句柄。之后使用这些图像句柄进行渲染操作。
	std::vector<VkImage> swapChainImages;
	//imageView描述了访问图像的方式，以及那一部分可以被访问
	std::vector<VkImageView> swapChainImageViews;

	//pipline layout用来让pipeline访问descriptor set，它定义了管线定义的shader stage和其使用的resource的接口 
	VkPipelineLayout pipelineLayout;

	//descriptorSetLayout定义了 Shader 使用的资源类型、绑定编号和内存布局等信息
	VkDescriptorSetLayout descriptorSetLayout;


	//render pass
	VkRenderPass renderPass;

	//graphics pipeline， pipeline state object，就是配置所有影响渲染/计算管线的状态，在Vulkan中状态的改变一般需要重新创建管线，而在OpenGL中状态是可以随时改变的
	VkPipeline graphicsPipeline;

	//帧缓冲引用了用于表示attachment的VkImageView对象
	//为交换链中的每个图像创建对应的帧缓冲，在渲染时，渲染到对应的帧缓冲上
	std::vector<VkFramebuffer> swapChainFrambuffers;

	/*
	vk下的指令，比如绘制指令和内存传输指令并不是直接通过函数
	调用执行的。我们需要将所有要执行的操作记录在一个指令缓冲对象，然
	后提交给可以执行这些操作的队列才能执行。这使得我们可以在程序初始
	化时就准备好所有要指定的指令序列，在渲染时直接提交执行。也使得多
	线程提交指令变得更加容易。我们只需要在需要指定执行的使用，将指令
	缓冲对象提交给vk处理接口。*/
	//指令池对象用于管理指令缓冲对象使用的内存，并负责指令缓冲对象的分配
	VkCommandPool commandPool;

	//指令缓冲对象，记录绘制指令
	std::vector<VkCommandBuffer> commandBuffers;

	//信号量：用于通知可渲染，可呈现的事件
	//VkSemaphore imageAvaliableSemaphore;
	//VkSemaphore renderFinishedSemaphore;
	std::vector<VkSemaphore> imageAvaliableSemaphores;
	std::vector<VkSemaphore> renderFinishedSemaphores;

	//记录当前渲染的是哪一个帧
	int currentFrame = 0;

	//使用fence在CPU和GPU之间的同步，来防止有超过MAX_FRAMES_IN_FLIGHT帧的指令同时被提交执行, 同步机制确保不会有超过我们设定数量的帧会被异步执行,从而防止内存不断增长
	std::vector<VkFence> inFlightFences;

	//frambuffer size 改变，重建交换链
	bool frameBufferResized = false	;

	//顶点
	std::vector<Vertex> vertices = {
		{{-0.5f, -0.5f, 0.f},{1.0f, 0.0f, 0.0f},{0.f, 0.f}},
		{{0.5f, -0.5f, 0.f}, {0.0f, 1.0f, 0.0f}, {1.f, 0.f}},
		{{0.5f, 0.5f, 0.f},	{0.0f, 0.0f, 0.0f}, {1.f, 1.f}},
		{{-0.5f, 0.5f, 0.f}, {1.0f, 1.0f, 0.0f},{0.f, 1.f}},

		{{-0.5f, -0.5f, -0.5f},{1.0f, 0.0f, -0.5f},{0.f, 0.f}},
		{{0.5f, -0.5f, -0.5f}, {0.0f, 1.0f, -0.5f}, {1.f, 0.f}},
		{{0.5f, 0.5f, -0.5f},  {0.0f, 0.0f, -0.5f}, {1.f, 1.f}},
		{{-0.5f, 0.5f, -0.5f}, {1.0f, 1.0f, -0.5f},{0.f, 1.f}},
	};

	std::vector<uint32_t> vertexIndices = {
		0,1,2,2,3,0,
		4,5,6,6,7,4
	};

	//vk的缓冲是可以存储任意数据的可以被显卡读取的内存。
	//顶点缓冲句柄
	VkBuffer vertexBuffer;
	//顶点缓冲所使用的内存对象
	VkDeviceMemory vertexBufferMemory;

	//顶点索引缓冲
	VkBuffer indexBuffer;
	//顶点索引缓冲所使用的内存对象
	VkDeviceMemory indexBufferMemory;

	//uinform 对象缓冲
	//为并行渲染的多个帧都创建uniformbuffer，因为一个线程在读uniform对象时，为另一个线程准备数据可能会修改它
	std::vector<VkBuffer> uniformBuffers;
	std::vector<VkDeviceMemory> uniformBuffersMemory;
	std::vector<void*> uniformBuffersMapped;


	//资源描述
	VkDescriptorPool descriptorPool;
	//描述符是用来在着色器中访问缓冲和图像数据的一种方式，指定渲染管线中的着色器程序所需资源的集合，包括缓冲区、图像、采样器等
	std::vector<VkDescriptorSet> descriptorSets;

	
	VkImage textureImage;
	VkDeviceMemory textureImageMemory;
	VkImageView textureImageView;
	VkSampler textureSampler;

	//深度像相关
	VkImage depthImage;
	VkDeviceMemory depthImageMemory;
	VkImageView depthImageView;


};



int main() {
    HelloTriangleApplication app;

    try {
        app.run();
    }
    catch (const std::exception& e) {
        std::cerr << e.what() << std::endl;
        return EXIT_FAILURE;
    }

    return EXIT_SUCCESS;
}
