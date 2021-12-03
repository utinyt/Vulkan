#include <algorithm>
#include "vulkan_swapchain.h"
#include "GLFW/glfw3.h"

/*
* destroy swapchain - must be called in app destructor
*/
void VulkanSwapchain::cleanup() {
	for (auto& imageView : imageViews) {
		vkDestroyImageView(devices->device, imageView, nullptr);
	}
	vkDestroySwapchainKHR(devices->device, swapchain, nullptr);
}

/*
* get device handle from app
*/
void VulkanSwapchain::init(const VulkanDevice* devices, GLFWwindow* window) {
	this->devices = devices;
	this->window = window;
}

/*
* (re)create swapchain
*/
void VulkanSwapchain::create() {
	VkSwapchainKHR oldSwapchain = swapchain;

	const VulkanDevice::SwapchainSupportDetails& details = 
		devices->querySwapchainSupport(devices->physicalDevice, devices->surface);
	
	//format
	bool foundFormat = false;
	for (const auto& availableFormat : details.formats) {
		if (availableFormat.format == VK_FORMAT_B8G8R8_SRGB &&
			availableFormat.colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR) {
			surfaceFormat = availableFormat;
			foundFormat = true;
			break;
		}
	}
	if (!foundFormat) {
		surfaceFormat = details.formats[0];
	}

	//present mode
	VkPresentModeKHR presentMode;
	bool foundPresentMode = false;
	for (const auto& availableMode : details.presentModes) {
		if (availableMode == VK_PRESENT_MODE_MAILBOX_KHR) { //triple buffering
			presentMode = availableMode;
			foundPresentMode = true;
			break;
		}
	}
	if (!foundPresentMode) {
		presentMode = VK_PRESENT_MODE_FIFO_KHR; //double buffering
	}

	//extent
	if (details.capabilities.currentExtent.width != UINT32_MAX) { 
		//surface size is already defined, swapchain image extent must match
		extent = details.capabilities.currentExtent;
	}
	else {
		//surface size is not defined
		int width, height; //pixel
		glfwGetFramebufferSize(window, &width, &height);

		extent = { static_cast<uint32_t>(width), static_cast<uint32_t>(height) };
		extent.width = std::clamp(extent.width,
			details.capabilities.minImageExtent.width, details.capabilities.maxImageExtent.width);
		extent.height = std::clamp(extent.height,
			details.capabilities.minImageExtent.height, details.capabilities.maxImageExtent.height);
	}

	//swapchain image count
	uint32_t minimageCount = details.capabilities.minImageCount + 1;
	if (details.capabilities.maxImageCount > 0 && // maxImageCount == 0 -> no max limit
		minimageCount > details.capabilities.maxImageCount) {
		minimageCount = details.capabilities.maxImageCount;
	}

	//surface transformation
	VkSurfaceTransformFlagsKHR preTransform;
	if (details.capabilities.currentTransform & VK_SURFACE_TRANSFORM_IDENTITY_BIT_KHR) {
		//no transform - identity
		preTransform = VK_SURFACE_TRANSFORM_IDENTITY_BIT_KHR;
	}
	else {
		preTransform = details.capabilities.currentTransform;
	}

	//swapchain creation
	VkSwapchainCreateInfoKHR swapchainInfo{};
	swapchainInfo.sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR;
	swapchainInfo.surface = devices->surface;
	swapchainInfo.minImageCount = minimageCount;
	swapchainInfo.imageFormat = surfaceFormat.format;
	swapchainInfo.imageColorSpace = surfaceFormat.colorSpace;
	swapchainInfo.imageExtent = extent;
	swapchainInfo.imageArrayLayers = 1;
	swapchainInfo.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;

	const VulkanDevice::QueueFamilyIndices& indices = devices->indices;
	uint32_t queueFamilyIndices[] = { indices.graphicsFamily.value(), indices.presentFamily.value() };
	if (indices.graphicsFamily == indices.presentFamily) {
		swapchainInfo.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE; //best performance
		swapchainInfo.queueFamilyIndexCount = 0;
		swapchainInfo.pQueueFamilyIndices = nullptr;
	}
	else {
		swapchainInfo.imageSharingMode = VK_SHARING_MODE_CONCURRENT;
		swapchainInfo.queueFamilyIndexCount = 2;
		swapchainInfo.pQueueFamilyIndices = queueFamilyIndices;
	}

	swapchainInfo.preTransform = (VkSurfaceTransformFlagBitsKHR)preTransform;
	swapchainInfo.compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
	swapchainInfo.presentMode = presentMode;
	swapchainInfo.clipped = VK_TRUE;
	swapchainInfo.oldSwapchain = oldSwapchain;

	//enable transfer usage (src & dst) if supported
	if (details.capabilities.supportedUsageFlags & VK_IMAGE_USAGE_TRANSFER_SRC_BIT) {
		swapchainInfo.imageUsage |= VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
	}
	if (details.capabilities.supportedUsageFlags & VK_IMAGE_USAGE_TRANSFER_DST_BIT) {
		swapchainInfo.imageUsage |= VK_IMAGE_USAGE_TRANSFER_DST_BIT;
	}

	VK_CHECK_RESULT(vkCreateSwapchainKHR(devices->device, &swapchainInfo, nullptr, &swapchain));
	LOG("created:\tswapchain");

	//delete old swapchain & image views
	if (oldSwapchain != VK_NULL_HANDLE) {
		for (auto& imageView : imageViews) {
			vkDestroyImageView(devices->device, imageView, nullptr);
		}
		vkDestroySwapchainKHR(devices->device, oldSwapchain, nullptr);
	}

	//get swapchain images
	vkGetSwapchainImagesKHR(devices->device, swapchain, &imageCount, nullptr);
	images.resize(imageCount);
	vkGetSwapchainImagesKHR(devices->device, swapchain, &imageCount, images.data());

	//image views
	imageViews.resize(imageCount);
	for (uint32_t i = 0; i < imageCount; ++i) {
		imageViews[i] = vktools::createImageView(devices->device, images[i],
			VK_IMAGE_VIEW_TYPE_2D, surfaceFormat.format, VK_IMAGE_ASPECT_COLOR_BIT);
	}

	LOG("created:\timage views");
}

/*
* acquire available swapchain image
* 
* @param presentCompleteSemaphore - semaphore to wait
* @param imageIndex - return index of available swapchain image
* 
* @return VkResult from image acquisition
*/
VkResult VulkanSwapchain::acquireImage(VkSemaphore presentCompleteSamaphore, uint32_t& imageIndex) {
	return vkAcquireNextImageKHR(devices->device, swapchain, UINT64_MAX, presentCompleteSamaphore, VK_NULL_HANDLE, &imageIndex);
}

/*
* send render finished image to the present queue
* 
* @param imageIndex - index of image finished rendering
* @param renderCompleteSemaphore - semaphore to wait
* 
* @return VkResult from queue present
*/
VkResult VulkanSwapchain::queuePresent(uint32_t imageIndex, VkSemaphore renderCompleteSemaphore) {
	latestImageIndex = imageIndex;

	VkPresentInfoKHR presentInfo{};
	presentInfo.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
	presentInfo.waitSemaphoreCount = 1;
	presentInfo.pWaitSemaphores = &renderCompleteSemaphore;
	presentInfo.swapchainCount = 1;
	presentInfo.pSwapchains = &swapchain;
	presentInfo.pImageIndices = &imageIndex;
	presentInfo.pResults = nullptr;
	return vkQueuePresentKHR(devices->presentQueue, &presentInfo);
}
