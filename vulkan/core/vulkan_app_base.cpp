#include <fstream>
#include <imgui/imgui.h>
#include "vulkan_app_base.h"
#include "vulkan_debug.h"

#define GLM_FORCE_DEPTH_ZERO_TO_ONE

#ifdef NDEBUG
const bool enableValidationLayer = false;
#else
const bool enableValidationLayer = true;
#endif

/*
* app constructor
* 
* @param width - window width
* @param height - window height
* @param appName - application title
*/
VulkanAppBase::VulkanAppBase(int width, int height, const std::string& appName)
	: width(width), height(height), appName(appName) {
	enabledDeviceExtensions.push_back(VK_KHR_SWAPCHAIN_EXTENSION_NAME);
}

/*
* app destructor
*/
VulkanAppBase::~VulkanAppBase() {
	destroyDepthStencilImage();
	devices.memoryAllocator.cleanup();

	if (!presentCompleteSemaphores.empty()) {
		for (size_t i = 0; i < MAX_FRAMES_IN_FLIGHT; ++i) {
			vkDestroySemaphore(devices.device, presentCompleteSemaphores[i], nullptr);
			vkDestroySemaphore(devices.device, renderCompleteSemaphores[i], nullptr);
			vkDestroyFence(devices.device, frameLimitFences[i], nullptr);
		}
	}

	swapchain.cleanup();

	vkDestroyPipelineCache(devices.device, pipelineCache, nullptr);
	destroyCommandBuffers();

	devices.cleanup();
	vkDestroySurfaceKHR(instance, surface, nullptr);
	destroyDebugUtilsMessengerEXT(instance, nullptr);
	vkDestroyInstance(instance, nullptr);

	glfwDestroyWindow(window);
	glfwTerminate();
}

/*
* init program - window & vulkan & application
*/
void VulkanAppBase::init() {
	initWindow();
	LOG("window initialization completed\n");
	initVulkan();
	LOG("vulkan initialization completed\n");
	initApp();
	LOG("application initialization completed\n");
}

/*
* called every frame - contain update & draw functions
*/
void VulkanAppBase::run() {
	while (!glfwWindowShouldClose(window)) {
		glfwPollEvents();
		update();
		draw();
	}
	vkDeviceWaitIdle(devices.device);
}

/*
* glfw window initialization
*/
void VulkanAppBase::initWindow() {
	if (glfwInit() == GLFW_FALSE) {
		throw std::runtime_error("failed to init GLFW");
	}

	glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
	glfwWindowHint(GLFW_RESIZABLE, GL_TRUE);
	window = glfwCreateWindow(width, height, appName.c_str(), nullptr, nullptr);
	glfwSetWindowUserPointer(window, this);
	glfwSetFramebufferSizeCallback(window, windowResizeCallbck);
	LOG("initialized:\tglfw");
}

/*
* vulkan setup
*/
void VulkanAppBase::initVulkan() {
	//instance
	createInstance();

	//debug messenger
	if (enableValidationLayer) {
		VkDebugUtilsMessengerCreateInfoEXT debugCreateInfo{};
		setupDebugMessengerCreateInfo(debugCreateInfo);
		VK_CHECK_RESULT(createDebugUtilsMessengerEXT(instance, &debugCreateInfo, nullptr));
		LOG("created:\tdebug utils messenger");
	}

	//surface
	VK_CHECK_RESULT(glfwCreateWindowSurface(instance, window, nullptr, &surface));
	LOG("created:\tsurface");

	//physical & logical device
	devices.pickPhysicalDevice(instance, surface, enabledDeviceExtensions);
	devices.createLogicalDevice();
	devices.createCommandPool();

	swapchain.init(&devices, window);
	swapchain.create();
}

/*
* basic application setup
*/
void VulkanAppBase::initApp() {
	createCommandBuffers();
	createSyncObjects();
	createPipelineCache();
	createDepthStencilImage();
}

/*
* called every frame - update application
*/
void VulkanAppBase::update() {
	//mouse info update
	glfwGetCursorPos(window, &xpos, &ypos);
	leftPressed		= (glfwGetMouseButton(window, GLFW_MOUSE_BUTTON_LEFT) == GLFW_PRESS);
	rightPressed	= (glfwGetMouseButton(window, GLFW_MOUSE_BUTTON_RIGHT) == GLFW_PRESS);

	//imgui mouse info update
	ImGuiIO& io = ImGui::GetIO();
	io.MousePos = ImVec2(static_cast<float>(xpos), static_cast<float>(ypos));
	io.MouseDown[0] = leftPressed;
	io.MouseDown[1] = rightPressed;

	imgui.newFrame();
	//imgui buffer updated || (mouse hovering imgui window && clicked)
	if (imgui.updateBuffers() || (ImGui::IsMouseDown(ImGuiMouseButton(0)) && io.WantCaptureKeyboard)) {
		resetCommandBuffer();
		recordCommandBuffer();
	}
}

/*
* image acquisition & check swapchain compatible
*/
uint32_t VulkanAppBase::prepareFrame() {
	vkWaitForFences(devices.device, 1, &frameLimitFences[currentFrame], VK_TRUE, UINT64_MAX);

	//prepare image
	uint32_t imageIndex;
	VkResult result = swapchain.acquireImage(presentCompleteSemaphores[currentFrame], imageIndex);
	if (result == VK_ERROR_OUT_OF_DATE_KHR || result == VK_SUBOPTIMAL_KHR) {
		resizeWindow();
	}
	else {
		VK_CHECK_RESULT(result);
	}

	//check current image is already in-flight
	if (inFlightImageFences[imageIndex] != VK_NULL_HANDLE) {
		vkWaitForFences(devices.device, 1, &inFlightImageFences[imageIndex], VK_TRUE, UINT64_MAX);
	}
	//update image status
	inFlightImageFences[imageIndex] = frameLimitFences[currentFrame];
	vkResetFences(devices.device, 1, &frameLimitFences[currentFrame]);

	return imageIndex;
}

/*
* image presentation & check swapchain compatible
*/
void VulkanAppBase::submitFrame(uint32_t imageIndex) {
	//present image
	VkResult result = swapchain.queuePresent(imageIndex, renderCompleteSemaphores[currentFrame]);
	if (result == VK_ERROR_OUT_OF_DATE_KHR || result == VK_SUBOPTIMAL_KHR || windowResized) {
		windowResized = false;
		resizeWindow();
	}
	else {
		VK_CHECK_RESULT(result);
	}
	currentFrame = (currentFrame + 1) % MAX_FRAMES_IN_FLIGHT;
}

/*
* handle window resize event - recreate swapchain and swaochain-dependent objects
*/
void VulkanAppBase::resizeWindow(bool recordCmdBuf) {
	//update window size
	int width = 0, height = 0;
	glfwGetFramebufferSize(window, &width, &height);
	while (width == 0 || height == 0) {
		glfwGetFramebufferSize(window, &width, &height);
		glfwWaitEvents();
	}

	//finish all command before destroy vk resources
	vkDeviceWaitIdle(devices.device);

	//swapchain
	swapchain.create();

	//depth stencil image
	destroyDepthStencilImage();
	createDepthStencilImage();

	//framebuffer
	createFramebuffers();

	//imgui displat size update
	ImGuiIO& io = ImGui::GetIO();
	io.DisplaySize = ImVec2(static_cast<float>(width), static_cast<float>(height));
	imgui.updateBuffers();

	//command buffers
	destroyCommandBuffers();
	createCommandBuffers();
	if (recordCmdBuf) {
		recordCommandBuffer();
	}
}

/*
* glfw window resize callback function
* 
* @param window - glfw window handle
* @param width
* @param height
*/
void VulkanAppBase::windowResizeCallbck(GLFWwindow* window, int width, int height) {
	auto app = reinterpret_cast<VulkanAppBase*>(glfwGetWindowUserPointer(window));
	app->windowResized = true;
}

/*
* destroy & recreate command buffer
*/
void VulkanAppBase::resetCommandBuffer() {
	destroyCommandBuffers();
	createCommandBuffers();
}

/*
* helper function - creates vulkan instance
*/
void VulkanAppBase::createInstance() {
	VkApplicationInfo appInfo{};
	appInfo.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
	appInfo.pApplicationName = appName.c_str();
	appInfo.pEngineName = appName.c_str();
	appInfo.apiVersion = VK_API_VERSION_1_2;

	VkInstanceCreateInfo instanceInfo{};
	instanceInfo.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
	instanceInfo.pApplicationInfo = &appInfo;
	instanceInfo.enabledLayerCount = 0;
	instanceInfo.ppEnabledLayerNames = nullptr;

	//vailidation layer settings
	VkDebugUtilsMessengerCreateInfoEXT debugCreateInfo{};
	if (enableValidationLayer) {
		uint32_t layerCount;
		vkEnumerateInstanceLayerProperties(&layerCount, nullptr);
		std::vector<VkLayerProperties> availableLayers(layerCount);
		vkEnumerateInstanceLayerProperties(&layerCount, availableLayers.data());

		const char* requiredValidationLayer = "VK_LAYER_KHRONOS_validation";

		// -- support check --
		auto layerIt = std::find_if(availableLayers.begin(), availableLayers.end(),
			[&requiredValidationLayer](const VkLayerProperties& properties) {
				return strcmp(properties.layerName, requiredValidationLayer) == 0;
			});

		if (layerIt == availableLayers.end()) {
			throw std::runtime_error("valiation layer is not supported");
		}

		instanceInfo.enabledLayerCount = 1;
		instanceInfo.ppEnabledLayerNames = &requiredValidationLayer;
		setupDebugMessengerCreateInfo(debugCreateInfo);
		instanceInfo.pNext = &debugCreateInfo;
	}

	//instance extension settings
	// -- GLFW extensions --
	uint32_t glfwExtensionCount = 0;
	const char** glfwExtensions = glfwGetRequiredInstanceExtensions(&glfwExtensionCount);
	std::vector<const char*> requiredInstanceExtensions(glfwExtensions, glfwExtensions + glfwExtensionCount);
	// -- required extensions specified by user --
	requiredInstanceExtensions.insert(requiredInstanceExtensions.end(),
		enabledInstanceExtensions.begin(), enabledInstanceExtensions.end());
	if (enableValidationLayer) {
		requiredInstanceExtensions.push_back(VK_EXT_DEBUG_UTILS_EXTENSION_NAME);
	}

	uint32_t availableInstanceExtensionCount = 0;
	vkEnumerateInstanceExtensionProperties(nullptr, &availableInstanceExtensionCount, nullptr);
	std::vector<VkExtensionProperties> availableInstanceExtensions(availableInstanceExtensionCount);
	vkEnumerateInstanceExtensionProperties(nullptr, &availableInstanceExtensionCount, availableInstanceExtensions.data());

	// -- support check --
	for (auto& requiredEXT : requiredInstanceExtensions) {
		auto extensionIt = std::find_if(availableInstanceExtensions.begin(), availableInstanceExtensions.end(),
			[&requiredEXT](const VkExtensionProperties& properties) {
				return strcmp(requiredEXT, properties.extensionName) == 0;
			});

		if (extensionIt == availableInstanceExtensions.end()) {
			throw std::runtime_error(std::string(requiredEXT)+" instance extension is not supported");
		}
	}

	instanceInfo.enabledExtensionCount = static_cast<uint32_t>(requiredInstanceExtensions.size());
	instanceInfo.ppEnabledExtensionNames = requiredInstanceExtensions.data();

	VK_CHECK_RESULT(vkCreateInstance(&instanceInfo, nullptr, &instance));
	LOG("created:\tvulkan instance");
}

/*
* allocate empty command buffers
*/
void VulkanAppBase::createCommandBuffers() {
	commandBuffers.resize(swapchain.imageCount * MAX_FRAMES_IN_FLIGHT);

	VkCommandBufferAllocateInfo commandBufferInfo{};
	commandBufferInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
	commandBufferInfo.commandPool = devices.commandPool;
	commandBufferInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
	commandBufferInfo.commandBufferCount = static_cast<uint32_t>(commandBuffers.size());

	VK_CHECK_RESULT(vkAllocateCommandBuffers(devices.device, &commandBufferInfo, commandBuffers.data()));
	LOG("created:\tcommand buffers");
}

/*
* helper function - free command buffers
*/
void VulkanAppBase::destroyCommandBuffers() {
	if (!commandBuffers.empty()) {
		vkFreeCommandBuffers(devices.device, devices.commandPool,
			static_cast<uint32_t>(commandBuffers.size()), commandBuffers.data());
	}
}

/*
* create semaphore & fence
*/
void VulkanAppBase::createSyncObjects() {
	presentCompleteSemaphores.resize(MAX_FRAMES_IN_FLIGHT);
	renderCompleteSemaphores.resize(MAX_FRAMES_IN_FLIGHT);
	frameLimitFences.resize(MAX_FRAMES_IN_FLIGHT);
	inFlightImageFences.resize(swapchain.imageCount, VK_NULL_HANDLE);

	VkSemaphoreCreateInfo semaphoreInfo{};
	semaphoreInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;

	VkFenceCreateInfo fenceInfo{};
	fenceInfo.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
	fenceInfo.flags = VK_FENCE_CREATE_SIGNALED_BIT;

	for (size_t i = 0; i < MAX_FRAMES_IN_FLIGHT; ++i) {
		VK_CHECK_RESULT(vkCreateSemaphore(devices.device, &semaphoreInfo, nullptr, &presentCompleteSemaphores[i]));
		VK_CHECK_RESULT(vkCreateSemaphore(devices.device, &semaphoreInfo, nullptr, &renderCompleteSemaphores[i]));
		VK_CHECK_RESULT(vkCreateFence(devices.device, &fenceInfo, nullptr, &frameLimitFences[i]));
	}
	LOG("created:\tsync objects");
}

/*
* create pipeline cache to optimize subsequent pipeline creation
*/
void VulkanAppBase::createPipelineCache() {
	VkPipelineCacheCreateInfo pipelineCacheInfo{};
	pipelineCacheInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_CACHE_CREATE_INFO;
	VK_CHECK_RESULT(vkCreatePipelineCache(devices.device, &pipelineCacheInfo, nullptr, &pipelineCache));
	LOG("created:\tpipeline cache");
}

/*
* setup depth & stencil buffers
*/
void VulkanAppBase::createDepthStencilImage() {
	depthFormat = vktools::findSupportedFormat(devices.physicalDevice,
		{VK_FORMAT_D32_SFLOAT, VK_FORMAT_D32_SFLOAT_S8_UINT, VK_FORMAT_D24_UNORM_S8_UINT},
		VK_IMAGE_TILING_OPTIMAL,
		VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT
	);

	devices.createImage(depthImage, { swapchain.extent.width,swapchain.extent.height, 1 },
		depthFormat,
		VK_IMAGE_TILING_OPTIMAL,
		VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT,
		VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);

	depthImageView = vktools::createImageView(devices.device, depthImage,
		VK_IMAGE_VIEW_TYPE_2D, depthFormat, VK_IMAGE_ASPECT_DEPTH_BIT);

	VkCommandBuffer cmdBuf = devices.beginOneTimeSubmitCommandBuffer();
	vktools::setImageLayout(cmdBuf, depthImage, VK_IMAGE_LAYOUT_UNDEFINED,
		VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL, VK_IMAGE_ASPECT_DEPTH_BIT);
	devices.endOneTimeSubmitCommandBuffer(cmdBuf);

	LOG("created:\tdepth stencil image");
}

/*
* destroy depth & stencil related resources
*/
void VulkanAppBase::destroyDepthStencilImage() {
	vkDestroyImageView(devices.device, depthImageView, nullptr);
	devices.memoryAllocator.freeImageMemory(depthImage, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
	vkDestroyImage(devices.device, depthImage, nullptr);
}
