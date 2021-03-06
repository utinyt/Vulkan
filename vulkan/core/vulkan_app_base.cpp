#include <fstream>
#include <algorithm>
#include <chrono>
#include <imgui/imgui.h>
#include <glm/glm.hpp>
#include "glm/gtc/matrix_transform.hpp"
#include "vulkan_app_base.h"
#include "vulkan_debug.h"
#include "stb_image_write.h"

#define GLM_FORCE_DEPTH_ZERO_TO_ONE

#ifdef NDEBUG
bool enableValidationLayer = false;
#else
bool enableValidationLayer = true;
#endif

/*
* app constructor
* 
* @param width - window width
* @param height - window height
* @param appName - application title
*/
VulkanAppBase::VulkanAppBase(int width, int height, const std::string& appName,
	VkSampleCountFlagBits sampleCount)
	: width(width), height(height), appName(appName), sampleCount(sampleCount) {
	enabledDeviceExtensions.push_back(VK_KHR_SWAPCHAIN_EXTENSION_NAME);
}

/*
* app destructor
*/
VulkanAppBase::~VulkanAppBase() {
	if (devices.device == VK_NULL_HANDLE) {
		return;
	}

	destroyMultisampleColorBuffer();
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
	vkdebug::messenger::destroyDebugUtilsMessengerEXT(instance, nullptr);
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
	updateCamera();
	LOG("application initialization completed\n");
}

/*
* called every frame - contain update & draw functions
*/
void VulkanAppBase::run() {
	while (!glfwWindowShouldClose(window) && !terminate) {
		glfwPollEvents();
		update();
		draw();
		currentFrame = (currentFrame + 1) % MAX_FRAMES_IN_FLIGHT;
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
	//glfwSetKeyCallback();
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
		vkdebug::messenger::setupDebugMessengerCreateInfo(debugCreateInfo);
		VK_CHECK_RESULT(vkdebug::messenger::createDebugUtilsMessengerEXT(instance, &debugCreateInfo, nullptr));
		LOG("created:\tdebug utils messenger");
	}

	//surface
	VK_CHECK_RESULT(glfwCreateWindowSurface(instance, window, nullptr, &surface));
	LOG("created:\tsurface");

	//physical & logical device
	devices.pickPhysicalDevice(instance, surface, enabledDeviceExtensions);
	devices.createLogicalDevice();
	devices.createCommandPool();
	vkdebug::marker::init(devices.device);

	swapchain.init(&devices, window);
	swapchain.create();
}

/*
* update camera position & front vector
*/
void VulkanAppBase::updateCamera() {
	//camera keyboard input
	float cameraSpeed = 2.5f * dt; // adjust accordingly
	if(glfwGetKey(window, GLFW_KEY_LEFT_SHIFT) == GLFW_PRESS)
		cameraSpeed = 35.0f * dt;

	if (glfwGetKey(window, GLFW_KEY_W) == GLFW_PRESS)
		camera.camPos += cameraSpeed * camera.camFront;
	if (glfwGetKey(window, GLFW_KEY_S) == GLFW_PRESS)
		camera.camPos -= cameraSpeed * camera.camFront;
	if (glfwGetKey(window, GLFW_KEY_A) == GLFW_PRESS)
		camera.camPos -= glm::normalize(glm::cross(camera.camFront, camera.camUp)) * cameraSpeed;
	if (glfwGetKey(window, GLFW_KEY_D) == GLFW_PRESS)
		camera.camPos += glm::normalize(glm::cross(camera.camFront, camera.camUp)) * cameraSpeed;

	//camera mouse input
	static bool firstCam = true;
	if (firstCam) {
		oldXPos = xpos;
		oldYPos = ypos;
		firstCam = false;
	}

	float xoffset = static_cast<float>(xpos - oldXPos);
	float yoffset = static_cast<float>(oldYPos - ypos);
	oldXPos = xpos;
	oldYPos = ypos;

	float sensitivity = 0.1f;
	xoffset *= sensitivity;
	yoffset *= sensitivity;

	yaw += xoffset;
	pitch += yoffset;

	if (pitch > 89.f) {
		pitch = 89.f;
	}
	if (pitch < -89.f) {
		pitch = -89.f;
	}

	glm::vec3 dir;
	dir.x = std::cos(glm::radians(yaw)) * std::cos(glm::radians(pitch));
	dir.y = std::sin(glm::radians(pitch));
	dir.z = std::sin(glm::radians(yaw)) * std::cos(glm::radians(pitch));
	camera.camFront = glm::normalize(dir);

	cameraMatrices.view = glm::lookAt(camera.camPos, camera.camPos + camera.camFront, camera.camUp);
	cameraMatrices.proj = glm::perspective(glm::radians(45.f),
		swapchain.extent.width / (float)swapchain.extent.height, 0.1f, 1000.f);
	cameraMatrices.proj[1][1] *= -1;
}

/*
* basic application setup
*/
void VulkanAppBase::initApp() {
	createCommandBuffers();
	createSyncObjects();
	createPipelineCache();
	createDepthStencilImage(sampleCount);
	createMultisampleColorBuffer(sampleCount);
}

/*
* called every frame - update application
*/
void VulkanAppBase::update() {
	//escape
	if (glfwGetKey(window, GLFW_KEY_ESCAPE) == GLFW_PRESS) {
		terminate = true;
	}

	//update dt
	static auto startTime = std::chrono::high_resolution_clock::now();
	auto currentTime = std::chrono::high_resolution_clock::now();
	float time = std::chrono::duration<float, std::chrono::seconds::period>(currentTime - startTime).count();
	dt = time - oldTime;
	oldTime = time;

	//mouse info update
	glfwGetCursorPos(window, &xpos, &ypos);
	leftPressed		= (glfwGetMouseButton(window, GLFW_MOUSE_BUTTON_LEFT) == GLFW_PRESS);
	rightPressed	= (glfwGetMouseButton(window, GLFW_MOUSE_BUTTON_RIGHT) == GLFW_PRESS);

	//update camera only when mouse is captured
	static int oldState = GLFW_RELEASE;
	if (glfwGetKey(window, GLFW_KEY_GRAVE_ACCENT) == GLFW_PRESS && oldState == GLFW_RELEASE) {
		captureMouse = !captureMouse;
		glfwSetInputMode(window, GLFW_CURSOR, captureMouse ? GLFW_CURSOR_DISABLED : GLFW_CURSOR_NORMAL);
		oldState = GLFW_PRESS;
	}
	if (glfwGetKey(window, GLFW_KEY_GRAVE_ACCENT) == GLFW_RELEASE && oldState == GLFW_PRESS) {
		oldState = GLFW_RELEASE;
	}

	//screenshot
	static int oldPrintKeyState = GLFW_RELEASE;
	if (glfwGetKey(window, GLFW_KEY_F5) == GLFW_PRESS && oldPrintKeyState == GLFW_RELEASE) {
		oldPrintKeyState = GLFW_PRESS;
		saveScreenshot("screenshot.png");
	}
	if (glfwGetKey(window, GLFW_KEY_F5) == GLFW_RELEASE && oldPrintKeyState == GLFW_PRESS) {
		oldPrintKeyState = GLFW_RELEASE;
	}

	if (captureMouse == true) {
		updateCamera();
	}

	//imgui mouse info update
	ImGuiIO& io = ImGui::GetIO();
	io.MousePos = ImVec2(static_cast<float>(xpos), static_cast<float>(ypos));
	io.MouseDown[0] = leftPressed;
	io.MouseDown[1] = rightPressed;

	imguiBase->newFrame();
	//imgui buffer updated || (mouse hovering imgui window && clicked)
	if (imguiBase->updateBuffers() || (ImGui::IsMouseDown(ImGuiMouseButton(0)) && io.WantCaptureKeyboard)) {
		if (imguiBase->deferCommandBufferRecord) {
			//defer command buffer record
			return;
		}
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
		resizeWindow(sampleCount);
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
		resizeWindow(sampleCount);
	}
	else {
		VK_CHECK_RESULT(result);
	}
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
	createDepthStencilImage(sampleCount);
	//multisample color buffer
	destroyMultisampleColorBuffer();
	createMultisampleColorBuffer(sampleCount);

	//framebuffer
	createFramebuffers();

	//imgui displat size update
	ImGuiIO& io = ImGui::GetIO();
	io.DisplaySize = ImVec2(static_cast<float>(width), static_cast<float>(height));
	imguiBase->updateBuffers();

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

	//layer setting
	uint32_t layerCount;
	vkEnumerateInstanceLayerProperties(&layerCount, nullptr);
	std::vector<VkLayerProperties> availableLayers(layerCount);
	vkEnumerateInstanceLayerProperties(&layerCount, availableLayers.data());
	
	uint32_t enabledLayerCount = 0;
	std::vector<const char*> enabledLayerNames{};

	//vailidation layer settings
	VkDebugUtilsMessengerCreateInfoEXT debugCreateInfo{};
	if (enableValidationLayer) {
		const char* requiredValidationLayer = "VK_LAYER_KHRONOS_validation";

		// -- support check --
		auto layerIt = std::find_if(availableLayers.begin(), availableLayers.end(),
			[&requiredValidationLayer](const VkLayerProperties& properties) {
				return strcmp(properties.layerName, requiredValidationLayer) == 0;
			});

		if (layerIt == availableLayers.end()) {
			LOG("VK_LAYER_KHRONOS_validation is not supported - continue without debug utils");
			enableValidationLayer = false;
		}
		else {
			enabledLayerCount++;
			enabledLayerNames.push_back(requiredValidationLayer);
			vkdebug::messenger::setupDebugMessengerCreateInfo(debugCreateInfo);
			instanceInfo.pNext = &debugCreateInfo;
		}
	}

	//fps counter
	/*const char* lunargMonitor = "VK_LAYER_LUNARG_monitor";

	auto layerIt = std::find_if(availableLayers.begin(), availableLayers.end(),
		[&lunargMonitor](const VkLayerProperties& properties) {
			return strcmp(properties.layerName, lunargMonitor) == 0;
		});

	if (layerIt == availableLayers.end()) {
		LOG("VK_LAYER_LUNARG_monitor is not supported - continue without fps counter");
	}
	else {
		enabledLayerCount++;
		enabledLayerNames.push_back(lunargMonitor);
	}*/

	instanceInfo.enabledLayerCount = enabledLayerCount;
	instanceInfo.ppEnabledLayerNames = enabledLayerNames.data();

	//instance extension settings
	// -- GLFW extensions --
	uint32_t glfwExtensionCount = 0;
	const char** glfwExtensions = glfwGetRequiredInstanceExtensions(&glfwExtensionCount);
	std::vector<const char*> requiredInstanceExtensions(glfwExtensions, glfwExtensions + glfwExtensionCount);
	// -- required extensions specified by user --
	requiredInstanceExtensions.insert(requiredInstanceExtensions.end(),
		enabledInstanceExtensions.begin(), enabledInstanceExtensions.end());
	//debug utils also can be used in release mode - perf makers
	requiredInstanceExtensions.push_back(VK_EXT_DEBUG_UTILS_EXTENSION_NAME);

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
			if (strcmp(extensionIt->extensionName, VK_EXT_DEBUG_UTILS_EXTENSION_NAME) == 0) {
				LOG("debug utils are not supported - continue without perf markers");
				continue;
			}
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
void VulkanAppBase::createDepthStencilImage(VkSampleCountFlagBits sampleCount) {
	depthFormat = vktools::findSupportedFormat(devices.physicalDevice,
		{ VK_FORMAT_D32_SFLOAT_S8_UINT, VK_FORMAT_D32_SFLOAT, VK_FORMAT_D24_UNORM_S8_UINT},
		VK_IMAGE_TILING_OPTIMAL,
		VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT
	);

	devices.createImage(depthImage, { swapchain.extent.width,swapchain.extent.height, 1 },
		depthFormat,
		VK_IMAGE_TILING_OPTIMAL,
		VK_IMAGE_USAGE_TRANSIENT_ATTACHMENT_BIT | VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT, 1,
		devices.lazilyAllocatedMemoryTypeExist ? VK_MEMORY_PROPERTY_LAZILY_ALLOCATED_BIT : VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
		sampleCount
	);

	VkImageAspectFlags aspectMask = 0;
	if (vktools::hasDepthComponent(depthFormat)) {
		aspectMask |= VK_IMAGE_ASPECT_DEPTH_BIT;
	}
	if (vktools::hasStencilComponent(depthFormat)) {
		aspectMask |= VK_IMAGE_ASPECT_STENCIL_BIT;
	}

	depthImageView = vktools::createImageView(devices.device, depthImage,
		VK_IMAGE_VIEW_TYPE_2D, depthFormat, aspectMask, 1);

	VkCommandBuffer cmdBuf = devices.beginCommandBuffer();
	vktools::setImageLayout(cmdBuf, depthImage, VK_IMAGE_LAYOUT_UNDEFINED,
		VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL, { aspectMask, 0, 1, 0, 1});
	devices.endCommandBuffer(cmdBuf);

	LOG("created:\tdepth stencil image");
}

/*
* destroy depth & stencil related resources
*/
void VulkanAppBase::destroyDepthStencilImage() {
	vkDestroyImageView(devices.device, depthImageView, nullptr);
	devices.memoryAllocator.freeImageMemory(depthImage,
		devices.lazilyAllocatedMemoryTypeExist ? VK_MEMORY_PROPERTY_LAZILY_ALLOCATED_BIT : VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
	vkDestroyImage(devices.device, depthImage, nullptr);
}

/*
* create multisample buffers for color / depth images
*/
void VulkanAppBase::createMultisampleColorBuffer(VkSampleCountFlagBits sampleCount) {	
	if (sampleCount == VK_SAMPLE_COUNT_1_BIT) {
		return;
	}

	//create multisample color buffer
	devices.createImage(multisampleColorImage,
		{ swapchain.extent.width,swapchain.extent.height, 1 },
		swapchain.surfaceFormat.format,
		VK_IMAGE_TILING_OPTIMAL,
		VK_IMAGE_USAGE_TRANSIENT_ATTACHMENT_BIT | VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT, 1, 
		devices.lazilyAllocatedMemoryTypeExist ? VK_MEMORY_PROPERTY_LAZILY_ALLOCATED_BIT : VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
		sampleCount
	);

	multisampleColorImageView = vktools::createImageView(devices.device,
		multisampleColorImage,
		VK_IMAGE_VIEW_TYPE_2D,
		swapchain.surfaceFormat.format,
		VK_IMAGE_ASPECT_COLOR_BIT,
		1
	);
}

/*
* destroy multisample (color buffer) resources
*/
void VulkanAppBase::destroyMultisampleColorBuffer() {
	vkDestroyImageView(devices.device, multisampleColorImageView, nullptr);
	devices.memoryAllocator.freeImageMemory(multisampleColorImage,
		devices.lazilyAllocatedMemoryTypeExist ? VK_MEMORY_PROPERTY_LAZILY_ALLOCATED_BIT : VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
	vkDestroyImage(devices.device, multisampleColorImage, nullptr);

	multisampleColorImageView = VK_NULL_HANDLE;
	multisampleColorImage = VK_NULL_HANDLE;
}

/*
* copy & save image from last swapchain image
*
* @param filename
*/
void VulkanAppBase::saveScreenshot(const std::string& filename) {
	bool blitSupport = true;
	//check if the device supports blitting from optimal images
	VkFormatProperties formatProperties;
	vkGetPhysicalDeviceFormatProperties(devices.physicalDevice, swapchain.surfaceFormat.format, &formatProperties);
	if ((formatProperties.optimalTilingFeatures & VK_FORMAT_FEATURE_BLIT_SRC_BIT) == 0) {
		LOG("Blitting from optimal image is not supported");
		blitSupport = false;
	}
	//check if the device support blitting to linear images
	vkGetPhysicalDeviceFormatProperties(devices.physicalDevice, VK_FORMAT_R8G8B8A8_UNORM, &formatProperties);
	if ((formatProperties.linearTilingFeatures & VK_FORMAT_FEATURE_BLIT_DST_BIT) == 0) {
		LOG("Blitting to linear image is not supported");
		blitSupport = false;
	}

	//handle the situation where blit is not supported
	if (blitSupport == false) {
		std::vector<VkFormat> BGRFormats = { VK_FORMAT_B8G8R8A8_SRGB, VK_FORMAT_B8G8R8A8_UNORM, VK_FORMAT_B8G8R8A8_SNORM };
		if (std::find(BGRFormats.begin(), BGRFormats.end(), swapchain.surfaceFormat.format) == BGRFormats.end()) {
			//current swapchain image format is something complicated - just return
			LOG("abort saveScreenshot() due to the current swapchain image format");
			return;
		}
	}

	//found number of image channels
	int nbChannel = -1;
	if (swapchain.surfaceFormat.format >= VK_FORMAT_R8G8B8A8_UNORM && swapchain.surfaceFormat.format <= VK_FORMAT_B8G8R8A8_SRGB) {
		nbChannel = 4;
	}
	else if (swapchain.surfaceFormat.format >= VK_FORMAT_R8G8B8_UNORM && swapchain.surfaceFormat.format <= VK_FORMAT_B8G8R8_SRGB) {
		nbChannel = 3;
	}
	else {
		LOG("abort saveScreenshot() due to the current swapchain image format");
		return;
	}

	//source image (latest swapchain image)
	VkImage srcImage = swapchain.images[swapchain.latestImageIndex];

	//create dst image
	VkImage dstImage = VK_NULL_HANDLE;
	MemoryAllocator::HostVisibleMemory imageMemory = devices.createImage(dstImage, { swapchain.extent.width, swapchain.extent.height, 1 },
		VK_FORMAT_R8G8B8A8_UNORM,
		VK_IMAGE_TILING_LINEAR,
		VK_IMAGE_USAGE_TRANSFER_DST_BIT,
		1,
		VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT
	);

	VkCommandBuffer cmdBuf = devices.beginCommandBuffer();

	//dst image layout transition (undefined -> transfer dst optimal)
	vktools::insertImageMemoryBarrier(cmdBuf,
		dstImage,
		0,
		VK_ACCESS_TRANSFER_WRITE_BIT,
		VK_IMAGE_LAYOUT_UNDEFINED,
		VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
		VK_PIPELINE_STAGE_TRANSFER_BIT,
		VK_PIPELINE_STAGE_TRANSFER_BIT,
		{ VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 }
	);

	//src image layout transition (present source -> transfer src optimal)
	vktools::insertImageMemoryBarrier(cmdBuf,
		srcImage,
		VK_ACCESS_MEMORY_READ_BIT, //wait until finishing presentation
		VK_ACCESS_TRANSFER_READ_BIT, //so that transfer read can start
		VK_IMAGE_LAYOUT_PRESENT_SRC_KHR,
		VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
		VK_PIPELINE_STAGE_TRANSFER_BIT,
		VK_PIPELINE_STAGE_TRANSFER_BIT,
		{ VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 }
	);

	//blit (or copy) image from src to dst
	if (blitSupport) {
		VkOffset3D blitSize{};
		blitSize.x = width;
		blitSize.y = height;
		blitSize.z = 1;
		VkImageBlit imageBlitRegion{};
		imageBlitRegion.srcSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
		imageBlitRegion.srcSubresource.layerCount = 1;
		//imageBlitRegion.srcOffsets[0] -> [0, 0, 0]
		imageBlitRegion.srcOffsets[1] = blitSize; // [width, height, 1]
		imageBlitRegion.dstSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
		imageBlitRegion.dstSubresource.layerCount = 1;
		//imageBlitRegion.srcOffsets[0] -> [0, 0, 0]
		imageBlitRegion.dstOffsets[1] = blitSize; // [width, height, 1]

		//blit
		vkCmdBlitImage(cmdBuf,
			srcImage, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
			dstImage, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
			1, &imageBlitRegion,
			VK_FILTER_NEAREST
		);
	}
	else {
		VkImageCopy imageCopyRegion{};
		imageCopyRegion.srcSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
		imageCopyRegion.srcSubresource.layerCount = 1;
		imageCopyRegion.dstSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
		imageCopyRegion.dstSubresource.layerCount = 1;
		imageCopyRegion.extent = { swapchain.extent.width, swapchain.extent.height, 1 };

		//copy
		vkCmdCopyImage(cmdBuf,
			srcImage, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
			dstImage, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
			1, &imageCopyRegion
		);
	}

	//dst image layout transition (transfer dst optimal -> general)
	vktools::insertImageMemoryBarrier(cmdBuf,
		dstImage,
		VK_ACCESS_TRANSFER_WRITE_BIT,
		VK_ACCESS_MEMORY_READ_BIT,
		VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
		VK_IMAGE_LAYOUT_GENERAL,
		VK_PIPELINE_STAGE_TRANSFER_BIT,
		VK_PIPELINE_STAGE_TRANSFER_BIT,
		{ VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 }
	);

	//revert src image layout (transfer src optimal -> present source)
	vktools::insertImageMemoryBarrier(cmdBuf,
		srcImage,
		VK_ACCESS_TRANSFER_READ_BIT,
		VK_ACCESS_MEMORY_READ_BIT,
		VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
		VK_IMAGE_LAYOUT_PRESENT_SRC_KHR,
		VK_PIPELINE_STAGE_TRANSFER_BIT,
		VK_PIPELINE_STAGE_TRANSFER_BIT,
		{ VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 }
	);

	devices.endCommandBuffer(cmdBuf);

	//get image layout
	VkImageSubresource subresource{ VK_IMAGE_ASPECT_COLOR_BIT, 0, 0 };
	VkSubresourceLayout subresourceLayout;
	vkGetImageSubresourceLayout(devices.device, dstImage, &subresource, &subresourceLayout);

	//map image memory
	const uint8_t* data = reinterpret_cast<const uint8_t*>(imageMemory.getHandle(devices.device));
	data += subresourceLayout.offset;

	if (!blitSupport) {
		//manually convert BGR(A) to RGB(A)
		uint8_t* convertedPixels = new uint8_t[width * height * nbChannel];
		for (int y = 0; y < height; ++y) {
			for (int x = 0; x < width; ++x) {
				int byteLocation = (y * width + x) * nbChannel;
				if (nbChannel == 4) {
					//uint8_t a = static_cast<uint8_t>(*(data + 3));
					convertedPixels[byteLocation + 3] = 255; //A
				}
				convertedPixels[byteLocation + 2] = static_cast<uint8_t>(*data);			//B
				convertedPixels[byteLocation + 1] = static_cast<uint8_t>(*(data + 1));	//G
				convertedPixels[byteLocation] = static_cast<uint8_t>(*(data + 2));		//R
				data += nbChannel;
			}
			//data += subresourceLayout.rowPitch;
		}
		stbi_write_png(filename.c_str(), width, height, nbChannel, convertedPixels, width * nbChannel);
		delete[] convertedPixels;
	}
	else {
		//write image to a png file
		stbi_write_png(filename.c_str(), width, height, nbChannel, data, width * nbChannel);
	}

	//cleanup
	imageMemory.unmap(devices.device);
	devices.memoryAllocator.freeImageMemory(dstImage, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
	vkDestroyImage(devices.device, dstImage, nullptr);
	LOG("save image file: " + filename);
}