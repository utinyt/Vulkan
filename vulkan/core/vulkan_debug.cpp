#include "vulkan_debug.h"

namespace {
	/** global debug messenger */
	VkDebugUtilsMessengerEXT debugMessenger;
}

namespace vkdebug {

	namespace messenger {
		/*
		* proxy for vkCreateDebugUtilsMessengerEXT
		*
		* @param instance - vulkan instance to use
		* @param pCreateInfo - valid debug utils messenger create info struct
		* @param pAllocator - custom allocator
		*/
		VkResult createDebugUtilsMessengerEXT(VkInstance instance,
			const VkDebugUtilsMessengerCreateInfoEXT* pCreateInfo,
			const VkAllocationCallbacks* pAllocator) {
			auto func = (PFN_vkCreateDebugUtilsMessengerEXT)vkGetInstanceProcAddr(instance,
				"vkCreateDebugUtilsMessengerEXT");
			if (func != nullptr) {
				return func(instance, pCreateInfo, pAllocator, &debugMessenger);
			}
			else {
				return VK_ERROR_EXTENSION_NOT_PRESENT;
			}
		}

		/*
		* proxy for vkDestroyDebugUtilsMessengerEXT
		*
		* @param instance - vulkan instance to use
		* @param pAllocator - custom allocator
		*/
		void destroyDebugUtilsMessengerEXT(VkInstance instance,
			const VkAllocationCallbacks* pAllocator) {
			auto func = (PFN_vkDestroyDebugUtilsMessengerEXT)vkGetInstanceProcAddr(instance,
				"vkDestroyDebugUtilsMessengerEXT");
			if (func != nullptr) {
				func(instance, debugMessenger, pAllocator);
			}
		}

		/*
		* custom debug utils messenger callback
		*
		* @param messageSeverity
		* @param messageType
		* @param pCallbackData - structure returned to the callback
		* @param pUserData - pointer that was specified during the debug messenger setup
		*/
		VKAPI_ATTR VkBool32 VKAPI_CALL debugCallback(
			VkDebugUtilsMessageSeverityFlagBitsEXT messageSeverity,
			VkDebugUtilsMessageTypeFlagsEXT messageType,
			const VkDebugUtilsMessengerCallbackDataEXT* pCallbackData,
			void* pUserData) {
			switch (messageSeverity) {
			case VK_DEBUG_UTILS_MESSAGE_SEVERITY_INFO_BIT_EXT:
				std::cerr << "INFO ";
				break;
			case VK_DEBUG_UTILS_MESSAGE_SEVERITY_VERBOSE_BIT_EXT:
				std::cerr << "VERBOSE ";
				break;
			case VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT:
				std::cerr << "WARNING ";
				break;
			case VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT:
				std::cerr << "ERROR ";
				break;
			}
			std::cerr << "validation layer: " << pCallbackData->pMessage << std::endl;
			return VK_FALSE;
		}

		/*
		* fills debug utils messenger create info
		*
		* @param createInfo - VkDebugUtilsMessengerCreateInfoEXT struct to fill
		*/
		void setupDebugMessengerCreateInfo(VkDebugUtilsMessengerCreateInfoEXT& createInfo) {
			createInfo = {};
			createInfo.sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT;
			createInfo.messageSeverity =
				//VK_DEBUG_UTILS_MESSAGE_SEVERITY_VERBOSE_BIT_EXT |
				//VK_DEBUG_UTILS_MESSAGE_SEVERITY_INFO_BIT_EXT |
				VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT |
				VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT;
			createInfo.messageType =
				VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT |
				VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT |
				VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT;
			createInfo.pfnUserCallback = debugCallback;
			//createInfo.flags = VK_DEBUG_REPORT_ERROR_BIT_EXT; // synchronization validation
		}
	}

	namespace marker {
		PFN_vkCmdBeginDebugUtilsLabelEXT vkCmdBeginDebugUtilsLabelEXT = VK_NULL_HANDLE;
		PFN_vkCmdEndDebugUtilsLabelEXT vkCmdEndDebugUtilsLabelEXT = VK_NULL_HANDLE;

		/*
		* get function pointers from debug utils extension
		*
		* @param device - logical device handle for vkGetInstanceProcAddr
		*/
		void init(VkDevice device) {
			vkCmdBeginDebugUtilsLabelEXT = (PFN_vkCmdBeginDebugUtilsLabelEXT)vkGetDeviceProcAddr(device, "vkCmdBeginDebugUtilsLabelEXT");
			vkCmdEndDebugUtilsLabelEXT = (PFN_vkCmdEndDebugUtilsLabelEXT)vkGetDeviceProcAddr(device, "vkCmdEndDebugUtilsLabelEXT");
		}

		/*
		* begin region (label)
		*
		* @param cmdBuf
		* @param name - name of the region
		*/
		void beginLabel(VkCommandBuffer cmdbuf, const char* name) {
			if (!vkCmdBeginDebugUtilsLabelEXT) {
				return;
			}
			VkDebugUtilsLabelEXT label{ VK_STRUCTURE_TYPE_DEBUG_UTILS_LABEL_EXT };
			label.pLabelName = name;
			vkCmdBeginDebugUtilsLabelEXT(cmdbuf, &label);
		}

		/*
		* end region (label)
		*
		* @param cmdBuf
		*/
		void endLabel(VkCommandBuffer cmdbuf) {
			if (!vkCmdEndDebugUtilsLabelEXT) {
				return;
			}
			vkCmdEndDebugUtilsLabelEXT(cmdbuf);
		}
	}
}