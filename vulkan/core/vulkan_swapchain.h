#pragma once
#include "vulkan_utils.h"
#include "vulkan_device.h"

struct GLFWwindow;
class VulkanSwapchain {
public:
	VulkanSwapchain() {}
	void cleanup();
	void init(const VulkanDevice* devices, GLFWwindow* window);
	void create();

	VkResult acquireImage(VkSemaphore presentCompleteSamaphore, uint32_t& imageIndex);
	VkResult queuePresent(uint32_t imageIndex, VkSemaphore renderCompleteSemaphore);

	/** swapchain handle */
	VkSwapchainKHR swapchain = VK_NULL_HANDLE;
	/** swapchain image format & color space */
	VkSurfaceFormatKHR surfaceFormat;
	/** swapchain extent */
	VkExtent2D extent;
	/** swapchain image count */
	uint32_t imageCount = 0;
	/** swapchain image collection */
	std::vector<VkImage> images;
	/** swapchain image view collection */
	std::vector<VkImageView> imageViews;
	/** index of last swapchain image finished presenting */
	uint32_t latestImageIndex = 0;

private:
	/** abstracted vulkan device collection handle */
	const VulkanDevice* devices;
	/** glfw window handle */
	GLFWwindow* window = nullptr;
};