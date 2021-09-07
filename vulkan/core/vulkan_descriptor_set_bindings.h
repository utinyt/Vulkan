#pragma once
#include "vulkan_utils.h"

/*
* helper class for creating descriptor pool & layout
*/
class DescriptorSetBindings {
public:
	/** add descriptor info */
	void addBinding(uint32_t binding,		//slot to which the descriptor will be bound, 
											//corresponding to the layout binding index in the shader
		VkDescriptorType descriptorType,	//type of the bound descriptor(s)
		uint32_t count,						//number of descriptors
		VkShaderStageFlags stageFlags,		//shader stages at which the bound resources will be available
		const VkSampler* pImmutableSampler = nullptr) {
		bindings.push_back({ binding, descriptorType, count, stageFlags, pImmutableSampler });
	}

	/** @brief create descriptor pool based on added bindings */
	VkDescriptorPool createDescriptorPool(VkDevice device, uint32_t maxSets = 1,
		VkDescriptorPoolCreateFlags flags = 0) const;
	/** @brief create descriptor set layout */
	VkDescriptorSetLayout createDescriptorSetLayout(VkDevice device) const;
	/** @brief create make write structure - VkAccelerationStructureKHR */
	VkWriteDescriptorSet makeWrite(VkDescriptorSet dstSet, uint32_t dstBinding,
		const VkWriteDescriptorSetAccelerationStructureKHR* pAccel, uint32_t arrayElement = 0);
	/** @brief create make write structure - VkDescriptorImageInfo */
	VkWriteDescriptorSet makeWrite(VkDescriptorSet dstSet, uint32_t dstBinding,
		const VkDescriptorImageInfo* pImageInfo, uint32_t arrayElement = 0);
	/** @brief create make write structure - VkDescriptorBufferInfo */
	VkWriteDescriptorSet makeWrite(VkDescriptorSet dstSet, uint32_t dstBinding,
		const VkDescriptorBufferInfo* pBufferInfo, uint32_t arrayElement = 0);

private:
	/** @brief return descriptor pool sizes built from added bindings */
	std::vector<VkDescriptorPoolSize> getRequiredPoolSizes(uint32_t numSets = 1) const;
	/** helper function for makeWrite*** */
	VkWriteDescriptorSet makeWrite(VkDescriptorSet dstSet, uint32_t dstBinding, uint32_t arrayElement = 0);

	/** contains descriptor info & requirements*/
	std::vector<VkDescriptorSetLayoutBinding> bindings;
};