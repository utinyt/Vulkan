#pragma once
#include "vulkan_utils.h"

/*
* generate pipeline
*/
class PipelineGenerator {
public:
	/** ctor */
	PipelineGenerator(VkDevice device);
	~PipelineGenerator() {
		resetAll();
	}
	/** @brief init / reset all create info */
	void resetAll();
	/** @brief only reset shader & vertex binding & attribute descriptions */
	void resetShaderVertexDescriptions();

	/** @brief add vertex input binding description */
	void addVertexInputBindingDescription(const std::vector<VkVertexInputBindingDescription>& bindings);
	/** @brief add vertex input attribute description */
	void addVertexInputAttributeDescription(const std::vector<VkVertexInputAttributeDescription>& attributes);
	/** @brief add shader */
	void addShader(VkShaderModule module, VkShaderStageFlagBits stage);
	/** @brief add push constant range */
	void addPushConstantRange(const std::vector<VkPushConstantRange>& ranges);
	/** @brief add descriptor set layout */
	void addDescriptorSetLayout(const std::vector<VkDescriptorSetLayout>& layouts);

	/** @brief set input topology */
	void setInputTopology(VkPrimitiveTopology topology);
	/** @brief (re)set rasterizer info */
	void setRasterizerInfo(
		VkPolygonMode polygonMode = VK_POLYGON_MODE_FILL,
		VkCullModeFlags cullMode = VK_CULL_MODE_BACK_BIT,
		VkFrontFace frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE);
	/** @brief (re)set color blend info */
	void setColorBlendInfo(VkBool32 blendEnable, uint32_t nbColorAttachment = 1);
	/** @brief (re)set color blend attachment state */
	void setColorBlendAttachmentState(const VkPipelineColorBlendAttachmentState& attachmentState, uint32_t nbColorAttachment = 1);
	/** @brief (re)set depth stencil state info */
	void setDepthStencilInfo(VkBool32 depthTest = VK_TRUE, VkBool32 depthWrite = VK_TRUE,
		VkCompareOp depthCompareOp = VK_COMPARE_OP_LESS);
	/** @brief set sample count */
	void setMultisampleInfo(VkSampleCountFlagBits sampleCount = VK_SAMPLE_COUNT_1_BIT,
		VkBool32 enableSampleShading = VK_FALSE, float minSampleShading = 0.f);

	/** @brief generate pipeline & pipeline layout */
	void generate(VkRenderPass renderPass,
		VkPipeline* outPipeline, VkPipelineLayout* outPipelineLayout);

	/** @brief struct getters */
	VkPipelineDepthStencilStateCreateInfo& getPipelineDepthStencilStateCreateInfo() {
		return depthStencilStateCreateInfo;
	}
	std::vector<VkPipelineShaderStageCreateInfo>& getShaderStageCreateInfo() {
		return shaderStages;
	}

private:
	/** logical device handle */
	VkDevice device = VK_NULL_HANDLE;
	/** vertex input bindings */
	std::vector<VkVertexInputBindingDescription>	vertexInputBindingDescs{};
	/** vertex input attributes */
	std::vector<VkVertexInputAttributeDescription>	vertexInputAttributeDescs{};
	/** vertex input state create info */
	VkPipelineVertexInputStateCreateInfo			vertexInputStateInfo{};
	/** input assembly state create info */
	VkPipelineInputAssemblyStateCreateInfo			inputAssemblyStateCreateInfo{};
	/** viewport state create info */
	VkPipelineViewportStateCreateInfo				viewportStateCreateInfo{};
	/** rasterization state create info */
	VkPipelineRasterizationStateCreateInfo			rasterizationStateCreateInfo{};
	/** multisample state create info */
	VkPipelineMultisampleStateCreateInfo			multisampleStateCreateInfo{};
	/** depth stencil state create info */
	VkPipelineDepthStencilStateCreateInfo			depthStencilStateCreateInfo{};
	/** color blend attachments */
	std::vector<VkPipelineColorBlendAttachmentState> colorBlendAttachmentStates{};
	/** color blend state create info */
	VkPipelineColorBlendStateCreateInfo				colorBlendStateCreateInfo{};
	/** dynamic states */
	std::vector<VkDynamicState>						dynamicStates{};
	/** dynamic states create info */
	VkPipelineDynamicStateCreateInfo				dynamicStateCreateInfo{};
	/** push constant range */
	std::vector<VkPushConstantRange>				pushConstantRanges{};
	/** descriptor set layout */
	std::vector<VkDescriptorSetLayout>				descriptorSetLayouts{};
	/** pipeline layout create info */
	VkPipelineLayoutCreateInfo						pipelineLayoutCreateInfo{};
	/** shader stages */
	std::vector<VkPipelineShaderStageCreateInfo>	shaderStages{};

	/** @brief destroy all shader module */
	void destroyShaderModule();
};