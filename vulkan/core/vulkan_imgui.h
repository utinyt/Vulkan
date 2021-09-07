#pragma once
#include "vulkan_texture.h"
#include "vulkan_descriptor_set_bindings.h"

/*
* Imgui & vulkan integration
*/
class Imgui {
private:
	//UI params
	struct PushConstBlock {
		glm::vec2 scale;
		glm::vec2 translate;
	} pushConstBlock;

	/** vertex & index buffer */
	VkBuffer vertexBuffer						= VK_NULL_HANDLE;
	VkBuffer indexBuffer						= VK_NULL_HANDLE;
	MemoryAllocator::HostVisibleMemory vertexMem;
	MemoryAllocator::HostVisibleMemory indexMem;
	int32_t vertexCount							= 0;
	int32_t indexCount							= 0;
	/** font image */
	Texture2D fontImage;
	/** pipeline */
	VkPipelineLayout pipelineLayout				= VK_NULL_HANDLE;
	VkPipeline pipeline							= VK_NULL_HANDLE;
	/** descriptor sets */
	DescriptorSetBindings bindings;
	VkDescriptorPool descriptorPool				= VK_NULL_HANDLE;
	VkDescriptorSetLayout descriptorSetLayout	= VK_NULL_HANDLE;
	std::vector<VkDescriptorSet> descriptorSets;
	VulkanDevice* devices						= nullptr;

public:
	/** @brief init context & style & resources */
	void init(VulkanDevice* devices, int width, int height,
		VkRenderPass renderPass, uint32_t MAX_FRAMES_IN_FLIGHT);
	/** @brief destroy all resources */
	void cleanup();
	/** @brief start imgui frame */
	void newFrame();
	/** @brief update vertex & index buffer */
	void updateBuffers();
	/** @brief record imgui draw commands */
	void drawFrame(VkCommandBuffer cmdBuf, size_t currentFrame);
};