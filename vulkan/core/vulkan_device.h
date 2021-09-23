#pragma once
#include <optional>
#include "vulkan_utils.h"
#include "vulkan_memory_allocator.h"

struct VulkanDevice {
	VulkanDevice() {}
	void pickPhysicalDevice(VkInstance instance, VkSurfaceKHR surface,
		const std::vector<const char*>& requiredExtensions);
	void createLogicalDevice();
	void cleanup();
	void createCommandPool();
	
	/** @brief create buffer & buffer memory */
	MemoryAllocator::HostVisibleMemory createBuffer(VkBuffer& buffer, VkDeviceSize size,
		VkBufferUsageFlags usage,
		VkMemoryPropertyFlags properties = VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
	/** @brief copy data to another buffer */
	void copyBuffer(VkCommandPool commandPool, VkBuffer srcBuffer, VkBuffer dstBuffer,
		VkDeviceSize size) const;
	/** @brief create image & image memory */
	MemoryAllocator::HostVisibleMemory createImage(VkImage& image, VkExtent3D extent, VkFormat format,
		VkImageTiling tiling, VkImageUsageFlags usage,
		VkMemoryPropertyFlags properties = VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
		VkSampleCountFlagBits numSamples = VK_SAMPLE_COUNT_1_BIT);
	/** @brief copy data to an image */
	void copyBufferToImage(VkBuffer buffer, VkImage image, VkOffset3D offset, VkExtent3D extent) const;
	/** @brief create & start one-time submit command buffer */
	VkCommandBuffer beginOneTimeSubmitCommandBuffer() const;
	/** @brief submit command to the queue, end & destroy one-time submit command buffer */
	void endOneTimeSubmitCommandBuffer(VkCommandBuffer commandBuffer) const;
	/** @brief get max sample count */
	VkSampleCountFlagBits getMaxSampleCount() const;

	/** GPU handle */
	VkPhysicalDevice physicalDevice;
	/** logical device handle */
	VkDevice device = VK_NULL_HANDLE;
	/** abstracted handle for native platform surface */
	VkSurfaceKHR surface;
	/** collection of queue family indices */
	struct QueueFamilyIndices {
		std::optional<uint32_t> graphicsFamily;
		std::optional<uint32_t> presentFamily;

		bool isComplete() const {
			return graphicsFamily.has_value() && presentFamily.has_value();
		}
	} indices;
	/** handle to the graphics queue */
	VkQueue graphicsQueue;
	/** handle to the present queue (usually the same as graphics queue)*/
	VkQueue presentQueue;
	/** memory properties of the current physical device */
	VkPhysicalDeviceMemoryProperties memProperties;
	/** current physical device properties */
	VkPhysicalDeviceProperties properties;
	/** available device features */
	VkPhysicalDeviceFeatures2 availableFeatures{ VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2 };
	/** vulkan 1.2 features */
	VkPhysicalDeviceVulkan12Features vk12Features{ VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES };
	/** command pool - graphics */
	VkCommandPool commandPool = VK_NULL_HANDLE;
	/** custom memory allocator */
	MemoryAllocator memoryAllocator;
	/** max sample count */
	uint32_t maxSampleCount;
	/** VK_MEMORY_PROPERTY_LAZILY_ALLOCATED_BIT support */
	bool lazilyAllocatedMemoryTypeExist = false;

	/** swapchain support details - used for swapchain creation*/
	struct SwapchainSupportDetails {
		VkSurfaceCapabilitiesKHR capabilities;
		std::vector<VkSurfaceFormatKHR> formats;
		std::vector<VkPresentModeKHR> presentModes;
	};

	static SwapchainSupportDetails querySwapchainSupport(VkPhysicalDevice physicalDevice,
		VkSurfaceKHR surface);

private:
	static bool checkPhysicalDevice(VkPhysicalDevice physicalDevice, VkSurfaceKHR surface,
		const std::vector<const char*>& requiredExtensions,
		QueueFamilyIndices& dstIndices, SwapchainSupportDetails& dstSwapchainDetails);
	static QueueFamilyIndices findQueueFamilyIndices(VkPhysicalDevice physicalDevice,
		VkSurfaceKHR surface);
	
	/** list of required device extensions */
	std::vector<const char*> requiredExtensions;
};
