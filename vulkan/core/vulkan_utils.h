#pragma once
#include <stdexcept>
#include <iostream>
#include <vector>
#include <fstream>
#include <vulkan/vulkan.h>
#include "glm/mat4x4.hpp"

/*
* checks vulkan function result and throws runtime error if it is failed
* 
* @param func - function call to check
*/
#define VK_CHECK_RESULT(func){									\
	if (func != VK_SUCCESS) {									\
		std::cerr << __FILE__ << ", line " << __LINE__ << ": ";	\
		std::string str = #func;								\
		throw std::runtime_error(str + " call has been failed");\
	}															\
}

/*
* simple stderr print
* 
* @param str - string to print
*/
#define LOG(str){					\
	std::cerr << str << std::endl;	\
}

/*
* aligning function
* 
* @param x - integer to align
* @param a - alignment
*/
inline uint32_t alignUp(uint32_t x, size_t a) {
	return static_cast<uint32_t>((x + (a - 1)) & ~(a - 1));
}

namespace vktools {
	/** @brief read binary file and store to a char vector */
	std::vector<char> readFile(const std::string& filename);
	/** @brief compile & create shader module */
	VkShaderModule createShaderModule(VkDevice device, const std::vector<char>& code);
	/** @brief transit image layout */
	void setImageLayout(VkCommandBuffer commandBuffer, VkImage image,
		VkImageLayout oldLayout, VkImageLayout newLayout, VkImageSubresourceRange subresourceRange);
	/** @brief generate mipmaps */
	void generateMipmaps(VkCommandBuffer cmdBuf, VkPhysicalDevice physicalDevice, VkImage image,
		VkFormat format, int32_t texWidth, int32_t texHeight, uint32_t mipLevels,
		VkFilter filter);
	/** @brief insert image memory barrier (general version of setImageLayout) */
	void insertImageMemoryBarrier(VkCommandBuffer cmdBuf,
		VkImage image,
		VkAccessFlags srcAccessMask,
		VkAccessFlags dstAccessMask,
		VkImageLayout oldLayout,
		VkImageLayout newLayout,
		VkPipelineStageFlags srcStage,
		VkPipelineStageFlags dstStage,
		VkImageSubresourceRange subresourceRange);
	/** @brief create & return image view */
	VkImageView createImageView(VkDevice device, VkImage image, VkImageViewType viewType,
		VkFormat format, VkImageAspectFlags aspectFlags, uint32_t mipLevels);
	/** @brief return suitable image format */
	VkFormat findSupportedFormat(VkPhysicalDevice physicalDevice, const std::vector<VkFormat>& candidates,
		VkImageTiling tiling, VkFormatFeatureFlags features);
	/** @brief check if the format has depth component */
	bool hasDepthComponent(VkFormat format);
	/** @brief check if the format has stencil component */
	bool hasStencilComponent(VkFormat format);
	/** @brief create renderpass */
	VkRenderPass createRenderPass(VkDevice device,
		const std::vector<VkFormat>& colorAttachmentFormats,
		VkFormat depthAttachmentFormat,
		VkSampleCountFlagBits sampleCount,
		uint32_t subpassCount = 1,
		bool clearColor = true,
		bool clearDepth = true,
		VkImageLayout initialLayout = VK_IMAGE_LAYOUT_UNDEFINED,
		VkImageLayout finalLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR,
		VkPipelineStageFlags stageFlags = 0,
		VkAccessFlags dstAccessMask = 0);
	/** @brief allocate descriptor sets */
	std::vector<VkDescriptorSet> allocateDescriptorSets(VkDevice device, VkDescriptorSetLayout layout,
		VkDescriptorPool pool, uint32_t nbDescriptors);
	/** @brief set dynamic state (viewport & scissor) */
	void setViewportScissorDynamicStates(VkCommandBuffer cmdBuf, VkExtent2D extent);

	namespace initializers {
		inline VkBufferCreateInfo bufferCreateInfo(VkDeviceSize size,
			VkBufferUsageFlags usage, VkSharingMode sharingMode = VK_SHARING_MODE_EXCLUSIVE) {
			VkBufferCreateInfo info{};
			info.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
			info.size = size;
			info.usage = usage;
			info.sharingMode = sharingMode;
			return info;
		}

		inline VkImageCreateInfo imageCreateInfo(
			VkExtent3D extent,
			VkFormat format,
			VkImageTiling tiling,
			VkImageUsageFlags usage,
			uint32_t mipLevels,
			VkSampleCountFlagBits numSamples = VK_SAMPLE_COUNT_1_BIT) {
			VkImageCreateInfo info{};
			info.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
			if (extent.depth == 1) {
				if (extent.height == 1) {
					info.imageType = VK_IMAGE_TYPE_1D;
				}
				info.imageType = VK_IMAGE_TYPE_2D;
			}
			else {
				info.imageType = VK_IMAGE_TYPE_3D;
			}
			info.extent = extent;
			info.mipLevels = mipLevels;
			info.arrayLayers = 1;
			info.format = format;
			info.tiling = tiling;
			info.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
			info.usage = usage;
			info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
			info.samples = numSamples;
			return info;
		}

		inline VkSamplerCreateInfo samplerCreateInfo(
			VkPhysicalDeviceFeatures2 availableFeatures,
			VkPhysicalDeviceProperties properties,
			VkFilter filter = VK_FILTER_LINEAR,
			VkSamplerAddressMode mode = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
			uint32_t mipLevels = 1) {

			VkSamplerCreateInfo samplerInfo{};
			samplerInfo.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
			samplerInfo.minFilter = filter;
			samplerInfo.magFilter = filter;
			samplerInfo.addressModeU = mode;
			samplerInfo.addressModeV = mode;
			samplerInfo.addressModeW = mode;
			if (availableFeatures.features.samplerAnisotropy == VK_TRUE) {
				samplerInfo.anisotropyEnable = VK_TRUE;
				samplerInfo.maxAnisotropy = properties.limits.maxSamplerAnisotropy;
			}
			else {
				samplerInfo.anisotropyEnable = VK_FALSE;
				samplerInfo.maxAnisotropy = 1.f;
			}
			samplerInfo.borderColor = VK_BORDER_COLOR_INT_OPAQUE_BLACK;
			samplerInfo.unnormalizedCoordinates = VK_FALSE;
			samplerInfo.compareEnable = VK_FALSE;
			samplerInfo.compareOp = VK_COMPARE_OP_ALWAYS;
			samplerInfo.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
			samplerInfo.mipLodBias = 0.f;
			samplerInfo.minLod = 0.f;
			samplerInfo.maxLod = static_cast<float>(mipLevels);
			return samplerInfo;
		}

		inline VkImageViewCreateInfo imageViewCreateInfo(VkImage image, VkImageViewType viewType,
			VkFormat format, VkImageSubresourceRange subresourceRange) {
			VkImageViewCreateInfo info{};
			info.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
			info.image = image;
			info.viewType = viewType;
			info.format = format;
			info.subresourceRange = subresourceRange;
			info.components = {
				VK_COMPONENT_SWIZZLE_R,
				VK_COMPONENT_SWIZZLE_G,
				VK_COMPONENT_SWIZZLE_B,
				VK_COMPONENT_SWIZZLE_A
			};
			return info;
		}

		inline VkBufferImageCopy bufferCopyRegion(
			VkExtent3D extent,
			VkImageAspectFlags aspect = VK_IMAGE_ASPECT_COLOR_BIT) {
			VkBufferImageCopy copy{};
			copy.imageSubresource.aspectMask = aspect;
			copy.imageSubresource.layerCount = 1;
			copy.imageExtent = extent;
			return copy;
		}

		inline VkVertexInputBindingDescription vertexInputBindingDescription(uint32_t binding,
			uint32_t stride,
			VkVertexInputRate inputRate = VK_VERTEX_INPUT_RATE_VERTEX) {
			VkVertexInputBindingDescription desc{};
			desc.binding = binding;
			desc.stride = stride;
			desc.inputRate = inputRate;
			return desc;
		}

		inline VkVertexInputAttributeDescription vertexInputAttributeDescription(
			uint32_t binding, uint32_t location, VkFormat format, uint32_t offset) {
			VkVertexInputAttributeDescription desc{};
			desc.binding = binding;
			desc.location = location;
			desc.format = format;
			desc.offset = offset;
			return desc;
		}

		/*
		* pipeline-related create infos
		*/

		inline VkPipelineVertexInputStateCreateInfo pipelineVertexInputStateCreateInfo(
			std::vector<VkVertexInputBindingDescription>& vertexBindingDescriptions,
			std::vector<VkVertexInputAttributeDescription>& vertexAttributeDescriptions) {
			//shrink
			vertexBindingDescriptions.shrink_to_fit();
			vertexAttributeDescriptions.shrink_to_fit();

			VkPipelineVertexInputStateCreateInfo info{};
			info.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
			info.vertexBindingDescriptionCount = static_cast<uint32_t>(vertexBindingDescriptions.size());
			info.pVertexBindingDescriptions = vertexBindingDescriptions.data();
			info.vertexAttributeDescriptionCount = static_cast<uint32_t>(vertexAttributeDescriptions.size());
			info.pVertexAttributeDescriptions = vertexAttributeDescriptions.data();
			return info;
		}

		inline VkPipelineInputAssemblyStateCreateInfo pipelineInputAssemblyStateCreateInfo(
			VkPrimitiveTopology topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST) {
			VkPipelineInputAssemblyStateCreateInfo info{};
			info.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
			info.topology = topology;
			info.primitiveRestartEnable = VK_FALSE;
			return info;
		}

		inline VkPipelineViewportStateCreateInfo pipelineViewportStateCreateInfo(
			uint32_t viewportCount = 1, uint32_t scissorCount = 1) {
			VkPipelineViewportStateCreateInfo info{};
			info.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
			info.viewportCount = viewportCount;
			info.scissorCount = scissorCount;
			return info;
		}

		inline VkPipelineDynamicStateCreateInfo pipelineDynamicStateCreateInfo(
			VkDynamicState* states, uint32_t stateCount) {
			VkPipelineDynamicStateCreateInfo info{};
			info.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
			info.dynamicStateCount = stateCount;
			info.pDynamicStates = states;
			return info;
		}

		inline VkPipelineRasterizationStateCreateInfo pipelineRasterizationStateCreateInfo(
			VkPolygonMode polygonMode = VK_POLYGON_MODE_FILL,
			VkCullModeFlags cullMode = VK_CULL_MODE_BACK_BIT,
			VkFrontFace frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE) {
			VkPipelineRasterizationStateCreateInfo info{};
			info.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
			info.depthClampEnable = VK_FALSE; //require gpu feature
			info.rasterizerDiscardEnable = VK_FALSE;
			info.polygonMode = polygonMode;
			info.lineWidth = 1.f; //require gpu feature to set this above 1.f
			info.cullMode = cullMode;
			info.frontFace = frontFace;
			info.depthBiasEnable = VK_FALSE;
			return info;
		}

		inline VkPipelineMultisampleStateCreateInfo pipelineMultisampleStateCreateInfo(
			VkSampleCountFlagBits sampleCount = VK_SAMPLE_COUNT_1_BIT,
			VkBool32 enableSampleShading = VK_FALSE,
			float minSampleShading = 0.f) {
			VkPipelineMultisampleStateCreateInfo info{};
			info.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
			info.rasterizationSamples = sampleCount;
			info.sampleShadingEnable = enableSampleShading;
			info.minSampleShading = minSampleShading;
			return info;
		}

		inline VkPipelineDepthStencilStateCreateInfo pipelineDepthStencilStateCreateInfo(
			VkBool32 depthTest = VK_TRUE, VkBool32 depthWrite = VK_TRUE, VkCompareOp depthCompareOp = VK_COMPARE_OP_LESS) {
			VkPipelineDepthStencilStateCreateInfo info{};
			info.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
			info.depthTestEnable = depthTest;
			info.depthWriteEnable = depthWrite;
			info.depthCompareOp = depthCompareOp;
			info.depthBoundsTestEnable = VK_FALSE;
			info.stencilTestEnable = VK_FALSE;
			return info;
		}

		inline VkPipelineColorBlendAttachmentState pipelineColorBlendAttachment(VkBool32 blendEnable) {
			VkPipelineColorBlendAttachmentState state{};
			state.blendEnable = blendEnable;
			state.colorWriteMask =
				VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
				VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;

			if(blendEnable == VK_TRUE){
				//alpha blending
				state.srcColorBlendFactor	= VK_BLEND_FACTOR_SRC_ALPHA;
				state.dstColorBlendFactor	= VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
				state.colorBlendOp			= VK_BLEND_OP_ADD;
				state.srcAlphaBlendFactor	= VK_BLEND_FACTOR_ONE;
				state.dstAlphaBlendFactor	= VK_BLEND_FACTOR_ZERO;
				state.alphaBlendOp			= VK_BLEND_OP_ADD;
			}

			return state;
		}

		inline VkPipelineColorBlendStateCreateInfo pipelineColorBlendStateCreateInfo(
			uint32_t attachmentCount, VkPipelineColorBlendAttachmentState* pAttachmentStates,
			VkBool32 logicOpEnable = VK_FALSE,
			float blendConstant1 = 0.f,
			float blendConstant2 = 0.f,
			float blendConstant3 = 0.f,
			float blendConstant4 = 0.f) {
			VkPipelineColorBlendStateCreateInfo info{};
			info.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
			info.logicOpEnable = logicOpEnable;
			info.attachmentCount = attachmentCount;
			info.pAttachments = pAttachmentStates;
			return info;
		}

		inline VkPipelineLayoutCreateInfo pipelineLayoutCreateInfo(
			VkDescriptorSetLayout* setLayouts, uint32_t setLayoutsSize) {
			VkPipelineLayoutCreateInfo info{};
			info.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
			info.setLayoutCount = setLayoutsSize;
			info.pSetLayouts = setLayouts;
			return info;
		}

		inline VkPipelineLayoutCreateInfo pipelineLayoutCreateInfo(
			std::vector<VkDescriptorSetLayout>& setLayouts,
			std::vector<VkPushConstantRange>& pushConstantRanges) {
			//shrink
			setLayouts.shrink_to_fit();
			pushConstantRanges.shrink_to_fit();

			VkPipelineLayoutCreateInfo info =
				pipelineLayoutCreateInfo(setLayouts.data(), static_cast<uint32_t>(setLayouts.size()));
			info.pushConstantRangeCount = static_cast<uint32_t>(pushConstantRanges.size());
			info.pPushConstantRanges = pushConstantRanges.data();
			return info;
		}

		inline VkPipelineShaderStageCreateInfo pipelineShaderStageCreateInfo(
			VkShaderStageFlagBits shaderStage, VkShaderModule shaderModule) {
			VkPipelineShaderStageCreateInfo info{};
			info.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
			info.stage = shaderStage;
			info.module = shaderModule;
			info.pName = "main";
			return info;
		}

		inline VkGraphicsPipelineCreateInfo graphicsPipelineCreateInfo(
			VkPipelineLayout layout, VkRenderPass renderPass) {
			VkGraphicsPipelineCreateInfo info{};
			info.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
			info.renderPass = renderPass;
			info.layout = layout;
			return info;
		}
	}
}
