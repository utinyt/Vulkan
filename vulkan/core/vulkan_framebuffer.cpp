#include "vulkan_framebuffer.h"
#include "vulkan_device.h"

/*
* free all resources
*/
void Framebuffer::cleanup() {
	if (devices == nullptr) {
		return;
	}

	//images
	for (auto& attachment : attachments) {
		devices->memoryAllocator.freeImageMemory(attachment.image, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
		vkDestroyImage(devices->device, attachment.image, nullptr);
		vkDestroyImageView(devices->device, attachment.imageView, nullptr);
	}
	attachments.clear();

	//render pass & framebuffer
	vkDestroyFramebuffer(devices->device, framebuffer, nullptr);
}

/*
* create image & image view, and fill attachment description based on the input info
* 
* @param imageCreateInfo - info needed to create VkImage
* @param memoryProperties - needed for allocateImageMemory()
*/
void Framebuffer::addAttachment(VkImageCreateInfo imageCreateInfo, VkMemoryPropertyFlags memoryProperties) {
	Attachment attachment{};

	//create image
	devices->createImage(attachment.image,
		imageCreateInfo.extent,
		imageCreateInfo.format,
		imageCreateInfo.tiling,
		imageCreateInfo.usage,
		memoryProperties,
		imageCreateInfo.samples);

	//check image aspect
	VkImageAspectFlags imageAspect = 0;
	if (imageCreateInfo.usage & VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT) {
		imageAspect = VK_IMAGE_ASPECT_COLOR_BIT;
	}
	else if (imageCreateInfo.usage & VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT) {
		if (vktools::hasDepthComponent(imageCreateInfo.format)) {
			imageAspect = VK_IMAGE_ASPECT_DEPTH_BIT;
		}
		if (vktools::hasStencilComponent(imageCreateInfo.format)) {
			imageAspect |= VK_IMAGE_ASPECT_STENCIL_BIT;
		}
	}
	
	//create image view
	attachment.imageView = vktools::createImageView(devices->device,
		attachment.image,
		VK_IMAGE_VIEW_TYPE_2D,
		imageCreateInfo.format,
		imageAspect);

	//fill attachment description
	attachment.description.format			= imageCreateInfo.format;
	attachment.description.samples			= imageCreateInfo.samples;
	attachment.description.loadOp			= VK_ATTACHMENT_LOAD_OP_CLEAR;
	attachment.description.storeOp			= (imageCreateInfo.usage & VK_IMAGE_USAGE_SAMPLED_BIT) ? 
												VK_ATTACHMENT_STORE_OP_STORE : VK_ATTACHMENT_STORE_OP_DONT_CARE;
	attachment.description.stencilLoadOp	= VK_ATTACHMENT_LOAD_OP_DONT_CARE;
	attachment.description.stencilStoreOp	= VK_ATTACHMENT_STORE_OP_DONT_CARE;
	attachment.description.initialLayout	= VK_IMAGE_LAYOUT_UNDEFINED;
	
	if (imageAspect & VK_IMAGE_ASPECT_DEPTH_BIT || imageAspect & VK_IMAGE_ASPECT_STENCIL_BIT) {
		attachment.description.finalLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
	}
	else {
		attachment.description.finalLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
	}

	attachments.push_back(attachment);
}

/*
* create render pass & framebuffer based on added attachments
*/
VkRenderPass Framebuffer::createRenderPass(const std::vector<VkSubpassDependency>& dependencies) {
	std::vector<VkAttachmentDescription> attachmentDescriptions;
	std::vector<VkAttachmentReference> colorReferences{};
	VkAttachmentReference depthReference{};
	bool colorAttachmentFound = true, depthAttachmentFound = false;

	for (size_t i = 0; i < attachments.size(); ++i) {
		attachmentDescriptions.push_back(attachments[i].description);
		if (vktools::hasDepthComponent(attachments[i].description.format)) {
			if (depthAttachmentFound == true) {
				throw std::runtime_error("Framebuffer::create(): found more than 1 depth attachment");
			}

			depthAttachmentFound = true;
			depthReference.attachment = static_cast<uint32_t>(i);
			depthReference.layout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
		}
		else {
			colorAttachmentFound = true;
			colorReferences.push_back({
				static_cast<uint32_t>(i),
				VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL
			});
		}
	}

	//subpass
	VkSubpassDescription subpass{};
	subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
	subpass.colorAttachmentCount = static_cast<uint32_t>(colorReferences.size());
	subpass.pColorAttachments = colorReferences.data();
	subpass.pDepthStencilAttachment = depthAttachmentFound ? &depthReference : nullptr;

	//create renderpass
	VkRenderPassCreateInfo renderPassInfo{};
	renderPassInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
	renderPassInfo.attachmentCount = static_cast<uint32_t>(attachments.size());
	renderPassInfo.pAttachments = attachmentDescriptions.data();
	renderPassInfo.subpassCount = 1;
	renderPassInfo.pSubpasses = &subpass;
	renderPassInfo.dependencyCount = static_cast<uint32_t>(dependencies.size());
	renderPassInfo.pDependencies = dependencies.data();

	VkRenderPass renderPass = VK_NULL_HANDLE;
	VK_CHECK_RESULT(vkCreateRenderPass(devices->device, &renderPassInfo, nullptr, &renderPass));
	return renderPass;
}

/*
* create framebuffer
* 
* @param extent - extent of framebuffer
*/
void Framebuffer::createFramebuffer(VkExtent2D extent, VkRenderPass renderPass) {
	std::vector<VkImageView> attachmentViews{};
	for (auto& attachment : attachments) {
		attachmentViews.push_back(attachment.imageView);
	}
	attachmentViews.shrink_to_fit();

	VkFramebufferCreateInfo framebufferInfo{};
	framebufferInfo.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
	framebufferInfo.renderPass = renderPass;
	framebufferInfo.attachmentCount = static_cast<uint32_t>(attachmentViews.size());
	framebufferInfo.pAttachments = attachmentViews.data();
	framebufferInfo.width = extent.width;
	framebufferInfo.height = extent.height;
	framebufferInfo.layers = 1;
	VK_CHECK_RESULT(vkCreateFramebuffer(devices->device, &framebufferInfo, nullptr, &framebuffer));
}
