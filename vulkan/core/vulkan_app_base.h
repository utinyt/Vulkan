#pragma once
#include "vulkan_device.h"
#include "vulkan_swapchain.h"
#include "GLFW/glfw3.h"
#include "vulkan_imgui.h"

class VulkanAppBase {
public:
	VulkanAppBase(int width, int height, const std::string& appName,
		VkSampleCountFlagBits sampleCount = VK_SAMPLE_COUNT_1_BIT);
	virtual ~VulkanAppBase();

	void init();
	void run();

protected:
	virtual void initApp();
	virtual void draw() = 0;
	virtual void update();

	uint32_t prepareFrame();
	void submitFrame(uint32_t imageIndex);
	
	virtual void resizeWindow(bool recordCommandBuffer = true);
	static void windowResizeCallbck(GLFWwindow* window, int width, int height);
	
	void resetCommandBuffer();
	virtual void createFramebuffers() = 0;
	virtual void recordCommandBuffer() = 0;

	//depth buffering
	void createDepthStencilImage(VkSampleCountFlagBits sampleCount);
	void destroyDepthStencilImage();
	//msaa
	void createMultisampleColorBuffer(VkSampleCountFlagBits sampleCount);
	void destroyMultisampleColorBuffer();

	/** glfw window handle */
	GLFWwindow* window;
	/** window extent */
	int width, height;
	/** glfw mouse pos */
	double xpos = 0, ypos = 0;
	/** glfw mouse pressed */
	bool leftPressed = false, rightPressed = false;
	/** imgui vulkan integration */
	ImguiBase* imguiBase = nullptr;
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
	VkImage depthImage = VK_NULL_HANDLE;
	/** depth image view handle */
	VkImageView depthImageView = VK_NULL_HANDLE;
	/** multisample color buffer */
	VkImage multisampleColorImage = VK_NULL_HANDLE;
	/** multisample color image view*/
	VkImageView multisampleColorImageView = VK_NULL_HANDLE;
	/** image sample count */
	VkSampleCountFlagBits sampleCount = VK_SAMPLE_COUNT_1_BIT;
	/** frame time & elapsed time*/
	float dt = 0, oldTime = 0;

	/** camera structs */
	struct Camera {
		glm::vec3 camPos = glm::vec3(0, 1, 3);
		glm::vec3 camFront = glm::vec3(0, 0, -1);
		glm::vec3 camUp = glm::vec3(0, 1, 0);
	} camera;
	struct CameraMatrices {
		glm::mat4 view;
		glm::mat4 proj;
	} cameraMatrices;

private:
	/** glfw mouse position */
	double oldXPos = 0, oldYPos = 0;
	/** glfw mouse rotation */
	float yaw = -90.f, pitch = 0;
	/** glfw capture mouse */
	bool captureMouse = false;

	void initWindow();
	void initVulkan();
	void updateCamera();

	void createInstance();
	void createCommandBuffers();
	void destroyCommandBuffers();
	void createSyncObjects();
	void createPipelineCache();
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
