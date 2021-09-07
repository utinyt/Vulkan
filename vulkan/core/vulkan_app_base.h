#pragma once
#include "vulkan_device.h"
#include "vulkan_swapchain.h"
#include "GLFW/glfw3.h"

class VulkanAppBase {
public:
	VulkanAppBase(int width, int height, const std::string& appName);
	virtual ~VulkanAppBase();

	void init();
	void run();

protected:
	virtual void initApp();
	virtual void draw() = 0;

	uint32_t prepareFrame();
	void submitFrame(uint32_t imageIndex);
	
	virtual void resizeWindow(bool recordCommandBuffer = true);
	static void windowResizeCallbck(GLFWwindow* window, int width, int height);
	virtual void createFramebuffers() = 0;
	virtual void recordCommandBuffer() = 0;

	/** glfw window handle */
	GLFWwindow* window;
	/** window extent */
	int width, height;
	/** application title */
	std::string appName;
	/** list of enalbed (required) instance extensions */
	std::vector<const char*> enabledInstanceExtensions;
	/** list of enalbed (required) device extensions */
	std::vector<const char*> enabledDeviceExtensions;
	/** vulkan instance */
	VkInstance instance;
	/** abstracted handle to the native platform surface */
	VkSurfaceKHR surface;
	/** contains physical & logical device handles, device specific info */
	VulkanDevice devices;
	/** abstracted swapchain object - contains swapchain image views */
	VulkanSwapchain swapchain;
	/** command buffers - per swapchain */
	std::vector<VkCommandBuffer> commandBuffers;
	/** sync image acquisition */
	std::vector<VkSemaphore> presentCompleteSemaphores;
	/** sync image presentation */
	std::vector<VkSemaphore> renderCompleteSemaphores;
	/** limits maximum frames in flight */
	std::vector<VkFence> frameLimitFences;
	/** tracks all swapchain images if they are being used */
	std::vector<VkFence> inFlightImageFences;
	/** pipeline cache */
	VkPipelineCache pipelineCache;
	/** max number of frames processed in GPU */
	int MAX_FRAMES_IN_FLIGHT = 2;
	/** current frame - index for MAX_FRAMES_IN_FLIGHT */
	size_t currentFrame = 0;
	/** window resize check */
	bool windowResized = false;
	/** depth format */
	VkFormat depthFormat;
	/** depth image handle */
	VkImage depthImage;
	/** depth image view handle */
	VkImageView depthImageView;

private:
	void initWindow();
	void initVulkan();

	void createInstance();
	void createCommandBuffers();
	void destroyCommandBuffers();
	void createSyncObjects();
	void createPipelineCache();
	void createDepthStencilImage();
	void destroyDepthStencilImage();
};

/*
* entry point
*/
#define RUN_APPLICATION_MAIN(Application, WIDTH, HEIGHT, appName)	\
int main(void) {													\
	try {															\
		Application app(WIDTH, HEIGHT, appName);					\
		app.init();													\
		app.run();													\
	}										\
	catch (const std::exception& e) {		\
		std::cerr << e.what() << std::endl;	\
		return EXIT_FAILURE;				\
	}										\
	return EXIT_SUCCESS;					\
}											\
