#pragma once
#include "vulkan_device.h"

class TextureBase {
public:
	/** @brief destroy image & imageView & sampler */
	void cleanup();
	/** @brief load texture from a file */
	virtual void load(VulkanDevice* devices, const std::string& path) = 0;
	/** @brief just get the handle from the user and init texture */
	void init(VulkanDevice* devices, VkImage image, VkImageView imageView,
		VkImageLayout imageLayout, VkSampler sampler = VK_NULL_HANDLE) {
		this->devices = devices;
		this->image = image;
		descriptor.imageView = imageView;
		descriptor.sampler = sampler;
		descriptor.imageLayout = imageLayout;
	}

	/** abstracted vulkan device handle */
	VulkanDevice* devices = nullptr;
	/** texture buffer handle */
	VkImage image = VK_NULL_HANDLE;
	/** image descriptor */
	VkDescriptorImageInfo descriptor{};
};

class Texture2D : public TextureBase {
public:
	/** @brief load texture from a file and create image & imageView & sampler */
	virtual void load(VulkanDevice* devices, const std::string& path) override;
	/** @brief load texture from a buffer */
	void load(VulkanDevice* devices, unsigned char* data,
		uint32_t texWidth, uint32_t texHeight, VkDeviceSize imageSize, VkFormat format,
		VkFilter filter = VK_FILTER_LINEAR, VkSamplerAddressMode mode = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE);
};

class TextureCube : public TextureBase {
public:
	/** @brief load texture from a file and create image & imageView & sampler */
	virtual void load(VulkanDevice* devices, const std::string& path) override;
};
