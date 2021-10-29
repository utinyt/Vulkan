#pragma once
#include "vulkan_utils.h"

struct VulkanDevice;
class Framebuffer {
public:
	struct Attachment {
		VkImage image = VK_NULL_HANDLE;
		VkImageView imageView = VK_NULL_HANDLE;
		VkAttachmentDescription description{};
	};

	/** @brief get vulkan device handle */
	void init(VulkanDevice* devices) { this->devices = devices; }
	/** @brief clean up */
	void cleanup();
	/** @brief add attachment and create actual image */
	void addAttachment(VkImageCreateInfo imageCreateInfo, VkMemoryPropertyFlags memoryProperties);
	/** @brief create render pass based on the added attachments*/
	VkRenderPass createRenderPass(const std::vector<VkSubpassDependency>& dependencies);
	/** @brief create framebuffer */
	void createFramebuffer(VkExtent2D extent, VkRenderPass renderPass);

	/** resources created after create() */
	VkFramebuffer framebuffer = VK_NULL_HANDLE;
	/** attachment collection */
	std::vector<Attachment> attachments;

private:
	/** devices handle */
	VulkanDevice* devices = nullptr;
};
