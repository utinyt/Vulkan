#include "vulkan_utils.h"

namespace vktools {
	/*
	* read binary file
	*
	* @param filename - name of the file
	* @return raw binary data stored in a vector
	*/
	std::vector<char> readFile(const std::string& filename) {
		std::ifstream file(filename, std::ios::ate | std::ios::binary);
		if (!file.is_open()) {
			throw std::runtime_error("failed to open file: " + filename);
		}

		size_t fileSize = static_cast<size_t>(file.tellg());
		std::vector<char> buffer(fileSize);
		file.seekg(0, std::ios::beg);
		file.read(buffer.data(), fileSize);
		file.close();

		return buffer;
	}

	/*
	* create shader module
	*
	* @param code - compiled shader code (raw binary data, .spv)
	* @return VkShaderModule - constructed shader module
	*/
	VkShaderModule createShaderModule(VkDevice device, const std::vector<char>& code) {
		VkShaderModuleCreateInfo shaderModuleInfo{};
		shaderModuleInfo.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
		shaderModuleInfo.codeSize = code.size();
		shaderModuleInfo.pCode = reinterpret_cast<const uint32_t*>(code.data());

		VkShaderModule shaderModule;
		VK_CHECK_RESULT(vkCreateShaderModule(device, &shaderModuleInfo, nullptr, &shaderModule));
		return shaderModule;
	}

	/*
	* set pipeline barrier for image layout transition
	* record command to the command buffer
	*
	* @param commandBuffer - command buffer to record
	* @param image - image to transit layout
	* @param oldLayout
	* @param newLayout
	* @param aspect
	*/
	void setImageLayout(VkCommandBuffer commandBuffer, VkImage image,
		VkImageLayout oldLayout, VkImageLayout newLayout, VkImageAspectFlags aspect) {
		VkImageMemoryBarrier imageBarrier{};
		imageBarrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
		imageBarrier.oldLayout = oldLayout;
		imageBarrier.newLayout = newLayout;
		imageBarrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
		imageBarrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
		imageBarrier.image = image;
		imageBarrier.subresourceRange.aspectMask = aspect;
		imageBarrier.subresourceRange.baseMipLevel = 0;
		imageBarrier.subresourceRange.levelCount = 1;
		imageBarrier.subresourceRange.baseArrayLayer = 0;
		imageBarrier.subresourceRange.layerCount = 1;

		VkPipelineStageFlags srcStage;
		VkPipelineStageFlags dstStage;
		
		if (oldLayout == VK_IMAGE_LAYOUT_UNDEFINED &&
			newLayout == VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL) {
			imageBarrier.srcAccessMask = 0;
			imageBarrier.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
			srcStage = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
			dstStage = VK_PIPELINE_STAGE_TRANSFER_BIT;
		}
		else if (oldLayout == VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL &&
			newLayout == VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL) {
			imageBarrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
			imageBarrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
			srcStage = VK_PIPELINE_STAGE_TRANSFER_BIT;
			dstStage = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
		}
		else if (oldLayout == VK_IMAGE_LAYOUT_UNDEFINED &&
			newLayout == VK_IMAGE_LAYOUT_GENERAL) {
			imageBarrier.srcAccessMask = 0;
			imageBarrier.dstAccessMask = 0;
			srcStage = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
			dstStage = VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT;
		}
		else if (oldLayout == VK_IMAGE_LAYOUT_UNDEFINED &&
			newLayout == VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL) {
			imageBarrier.srcAccessMask = 0;
			imageBarrier.dstAccessMask = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
			srcStage = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
			dstStage = VK_PIPELINE_STAGE_ALL_COMMANDS_BIT;
		}
		else {
			std::invalid_argument("VulkanTextureBase::setImageLayout(): unsupported layout transition");
		}

		vkCmdPipelineBarrier(commandBuffer,
			srcStage, dstStage,
			0,
			0, nullptr,
			0, nullptr,
			1, &imageBarrier);
	}

	/*
	* create & return image view
	* 
	* @param image - image handle
	* @param viewtype - specify image view type (1D / 2D / 3D / ...)
	* @param format
	* @return VkImageView - created image view
	*/
	VkImageView createImageView(VkDevice device, VkImage image, VkImageViewType viewType,
		VkFormat format, VkImageAspectFlags aspectFlags) {
		VkImageViewCreateInfo viewInfo{};
		viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
		viewInfo.image = image;
		viewInfo.viewType = viewType;
		viewInfo.format = format;
		viewInfo.subresourceRange.aspectMask = aspectFlags;
		viewInfo.subresourceRange.baseMipLevel = 0;
		viewInfo.subresourceRange.levelCount = 1;
		viewInfo.subresourceRange.baseArrayLayer = 0;
		viewInfo.subresourceRange.layerCount = 1;

		VkImageView imageView;
		VK_CHECK_RESULT(vkCreateImageView(device, &viewInfo, nullptr, &imageView));
		return imageView;
	}

	/*
	* find suitable image format
	* 
	* @param physicalDevice
	* @param candidates - list of formats to choose from
	* @param tiling - image tiling mode
	* @param features - format feature
	* @return VkFormat - suitable format
	*/
	VkFormat findSupportedFormat(VkPhysicalDevice physicalDevice,
		const std::vector<VkFormat>& candidates, VkImageTiling tiling, VkFormatFeatureFlags features) {
		for (VkFormat format : candidates) {
			VkFormatProperties properties;
			vkGetPhysicalDeviceFormatProperties(physicalDevice, format, &properties);

			if (tiling == VK_IMAGE_TILING_LINEAR &&
				(properties.linearTilingFeatures & features) == features) {
				return format;
			}
			else if (tiling == VK_IMAGE_TILING_OPTIMAL &&
				(properties.optimalTilingFeatures & features) == features) {
				return format;
			}
		}
		throw std::runtime_error("findSupportedFormat(): can't find supported format");
	}

	/*
	* check if the format has stencil component
	* 
	* @param format
	* @return bool
	*/
	bool hasStencilComponent(VkFormat format) {
		return format == VK_FORMAT_D32_SFLOAT_S8_UINT ||
			format == VK_FORMAT_D24_UNORM_S8_UINT;
	}

	/*
	* get buffer address
	* 
	* @param device - logical device handle
	* @param buffer - buffer handle
	* 
	* @return VkDeviceAddress - buffer address
	*/
	VkDeviceAddress getBufferDeviceAddress(VkDevice device, VkBuffer buffer) {
		VkBufferDeviceAddressInfo bufferInfo{};
		bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_DEVICE_ADDRESS_INFO;
		bufferInfo.buffer = buffer;
		return vkGetBufferDeviceAddress(device, &bufferInfo);
	}

	/*
	* convert glm mat4 to VkTransformMatrixKHR
	* 
	* @param mat - glm matrix to convert
	* 
	* @return VkTransformMatrixKHR - converted vk matrix
	*/
	VkTransformMatrixKHR toTransformMatrixKHR(const glm::mat4& mat) {//column major
		glm::mat4 transposed = glm::transpose(mat);
		VkTransformMatrixKHR vkMat; //row major
		memcpy(&vkMat, &transposed, sizeof(VkTransformMatrixKHR));
		return vkMat;
	}

	/*
	* create render pass
	* 
	* @param device - logical device handle
	* @param colorAttachmentFormats
	* @param depthAttachmentFormat
	* @param subpassCount
	* @param clearColor
	* @param depthColor
	* @param initialLayout
	* @param finalLayout
	*/
	VkRenderPass createRenderPass(VkDevice	device,
		const std::vector<VkFormat>&		colorAttachmentFormats, 
		VkFormat							depthAttachmentFormat,
		uint32_t							subpassCount, 
		bool								clearColor, 
		bool								clearDepth, 
		VkImageLayout						initialLayout, 
		VkImageLayout						finalLayout,
		VkPipelineStageFlags				stageFlags,
		VkAccessFlags						dstAccessMask) {
		std::vector<VkAttachmentDescription> allAttachments;
		std::vector<VkAttachmentReference> colorAttachmentsRef;
		bool hasDepth = (depthAttachmentFormat != VK_FORMAT_UNDEFINED);

		//color attachments
		for (const auto& format : colorAttachmentFormats) {
			VkAttachmentDescription colorAttachment{};
			colorAttachment.format			= format;
			colorAttachment.samples			= VK_SAMPLE_COUNT_1_BIT;
			colorAttachment.loadOp			= clearColor ?
												VK_ATTACHMENT_LOAD_OP_CLEAR : (initialLayout == VK_IMAGE_LAYOUT_UNDEFINED ?
													VK_ATTACHMENT_LOAD_OP_DONT_CARE : VK_ATTACHMENT_LOAD_OP_LOAD);
			colorAttachment.storeOp			= VK_ATTACHMENT_STORE_OP_STORE;
			colorAttachment.stencilLoadOp	= VK_ATTACHMENT_LOAD_OP_DONT_CARE;
			colorAttachment.stencilStoreOp	= VK_ATTACHMENT_STORE_OP_DONT_CARE;
			colorAttachment.initialLayout	= initialLayout;
			colorAttachment.finalLayout		= finalLayout;

			VkAttachmentReference colorAttachmentRef{};
			colorAttachmentRef.attachment	= static_cast<uint32_t>(allAttachments.size());
			colorAttachmentRef.layout		= VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

			allAttachments.push_back(colorAttachment);
			colorAttachmentsRef.push_back(colorAttachmentRef);
		}

		//depth attachment
		VkAttachmentReference depthAttachmentRef{};
		if (hasDepth) {
			VkAttachmentDescription depthAttachment{};
			depthAttachment.format			= depthAttachmentFormat;
			depthAttachment.samples			= VK_SAMPLE_COUNT_1_BIT;
			depthAttachment.loadOp			= clearDepth ? VK_ATTACHMENT_LOAD_OP_CLEAR : VK_ATTACHMENT_LOAD_OP_LOAD;
			depthAttachment.storeOp			= VK_ATTACHMENT_STORE_OP_STORE;
			depthAttachment.stencilLoadOp	= VK_ATTACHMENT_LOAD_OP_DONT_CARE;
			depthAttachment.stencilStoreOp	= VK_ATTACHMENT_STORE_OP_DONT_CARE;
			depthAttachment.initialLayout	= VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
			depthAttachment.finalLayout		= VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

			depthAttachmentRef.attachment	= static_cast<uint32_t>(allAttachments.size());
			depthAttachmentRef.layout		= VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
			 
			allAttachments.push_back(depthAttachment);
		}

		//subpass dependency
		std::vector<VkSubpassDescription> subpasses;
		std::vector<VkSubpassDependency> subpassDependencies;

		for (uint32_t i = 0; i < subpassCount; ++i) {
			VkSubpassDescription subpass{};
			subpass.pipelineBindPoint		= VK_PIPELINE_BIND_POINT_GRAPHICS;
			subpass.colorAttachmentCount	= static_cast<uint32_t>(colorAttachmentsRef.size());
			subpass.pColorAttachments		= colorAttachmentsRef.data();
			subpass.pDepthStencilAttachment = hasDepth ? &depthAttachmentRef : VK_NULL_HANDLE;

			VkSubpassDependency dependency{};
			dependency.srcSubpass			= i == 0 ? VK_SUBPASS_EXTERNAL : (i - 1);
			dependency.dstSubpass			= i;
			dependency.srcStageMask			= stageFlags;
			dependency.dstStageMask			= stageFlags;
			dependency.srcAccessMask		= 0;
			dependency.dstAccessMask		= dstAccessMask;

			subpasses.push_back(subpass);
			subpassDependencies.push_back(dependency);
		}

		//create renderpass
		VkRenderPassCreateInfo renderPassInfo{};
		renderPassInfo.sType				= VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
		renderPassInfo.attachmentCount		= static_cast<uint32_t>(allAttachments.size());
		renderPassInfo.pAttachments			= allAttachments.data();
		renderPassInfo.subpassCount			= static_cast<uint32_t>(subpasses.size());
		renderPassInfo.pSubpasses			= subpasses.data();
		renderPassInfo.dependencyCount		= static_cast<uint32_t>(subpassDependencies.size());
		renderPassInfo.pDependencies		= subpassDependencies.data();

		VkRenderPass renderPass = VK_NULL_HANDLE;
		VK_CHECK_RESULT(vkCreateRenderPass(device, &renderPassInfo, nullptr, &renderPass));
		return renderPass;
	}

	/*
	* allocate descriptor sets
	* 
	* @param device - logical device handle
	* @param layout - descriptor layout
	* @param pool - descriptor pool
	* @param nbDescriptor - number of descriptors to allocate
	* 
	* @return std::vector<VkDescriptorSet> - vector of allocated descriptor sets
	*/
	std::vector<VkDescriptorSet> allocateDescriptorSets(VkDevice device, VkDescriptorSetLayout layout,
		VkDescriptorPool pool, uint32_t nbDescriptorSets) {
		std::vector<VkDescriptorSet> descriptorSets;
		descriptorSets.assign(nbDescriptorSets, {});

		std::vector<VkDescriptorSetLayout> layouts(nbDescriptorSets, layout);
		VkDescriptorSetAllocateInfo descInfo{};
		descInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
		descInfo.descriptorPool = pool;
		descInfo.descriptorSetCount = nbDescriptorSets;
		descInfo.pSetLayouts = layouts.data();
		VK_CHECK_RESULT(vkAllocateDescriptorSets(device, &descInfo, descriptorSets.data()));

		return descriptorSets;
	}

	/*
	* set dynamic state (viewport & scissor)
	* 
	* @param cmdBuf - command buffer to record
	*/
	void setViewportScissorDynamicStates(VkCommandBuffer cmdBuf, VkExtent2D extent) {
		//dynamic states
		VkViewport viewport{};
		viewport.x = 0.f;
		viewport.y = 0.f;
		viewport.width = static_cast<float>(extent.width);
		viewport.height = static_cast<float>(extent.height);
		viewport.minDepth = 0.f;
		viewport.maxDepth = 1.f;
		vkCmdSetViewport(cmdBuf, 0, 1, &viewport);

		VkRect2D scissor{};
		scissor.offset = { 0, 0 };
		scissor.extent = extent;
		vkCmdSetScissor(cmdBuf, 0, 1, &scissor);
	}
}
