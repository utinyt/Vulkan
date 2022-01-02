#pragma once
#include "vulkan_utils.h"

namespace vkdebug {
	namespace messenger {
		VkResult createDebugUtilsMessengerEXT(VkInstance instance,
			const VkDebugUtilsMessengerCreateInfoEXT* pCreateInfo,
			const VkAllocationCallbacks* pAllocator);
		void destroyDebugUtilsMessengerEXT(VkInstance instance,
			const VkAllocationCallbacks* pAllocator);
		void setupDebugMessengerCreateInfo(VkDebugUtilsMessengerCreateInfoEXT& createInfo);
	}

	namespace marker {

		/** @brief get function pointers from debug utils extension */
		void init(VkDevice device);
		/** @brief begin region (label) */
		void beginLabel(VkCommandBuffer cmdbuf, const char* name);
		/** @brief end region (label) */
		void endLabel(VkCommandBuffer cmdbuf);
	}
}