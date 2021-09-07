#define STB_IMAGE_IMPLEMENTATION
#include <stb_image.h>
#include "vulkan_texture.h"


/*
* clean image & image memory
* must be called if texture is not null
*/
void TextureBase::cleanup() {
	vkDestroySampler(devices->device, descriptor.sampler, nullptr);
	vkDestroyImageView(devices->device, descriptor.imageView, nullptr);
	devices->memoryAllocator.freeImageMemory(image, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
	vkDestroyImage(devices->device, image, nullptr);
}

/*
* load 2d texture from a file
* 
* @param devices - abstracted vulkan device handle
* @param path - texture file path
*/
void Texture2D::load(VulkanDevice* devices, const std::string& path) {
	this->devices = devices;
	
	//image load
	int width, height, channels;
	stbi_uc* pixels = stbi_load(path.c_str(), &width, &height, &channels, STBI_rgb_alpha);
	VkDeviceSize imageSize = width * height * 4;

	if (!pixels) {
		throw std::runtime_error("failed to load texture: " + path);
	}

	load(devices, pixels, width, height, imageSize, VK_FORMAT_R8G8B8A8_SRGB);
	stbi_image_free(pixels);
}

/*
* load 2d texture from a file
*
* @param devices - abstracted vulkan device handle
* @param data - buffer storing image
* @param texWidth
* @param texHeight
* @param imageSize - image size in bytes
* @param format - image format
*/
void Texture2D::load(VulkanDevice* devices, unsigned char* data,
	uint32_t texWidth, uint32_t texHeight, VkDeviceSize imageSize, VkFormat format) {
	//image creation
	this->devices = devices;

	devices->createImage(image, { texWidth, texHeight, 1 },
		format,
		VK_IMAGE_TILING_OPTIMAL,
		VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
		VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
	
	//image view creation
	descriptor.imageView = vktools::createImageView(devices->device, image,
		VK_IMAGE_VIEW_TYPE_2D, format, VK_IMAGE_ASPECT_COLOR_BIT);

	//staging
	VkBuffer stagingBuffer;
	VkBufferCreateInfo stagingBufferCreateInfo = vktools::initializers::bufferCreateInfo(
		imageSize, VK_BUFFER_USAGE_TRANSFER_SRC_BIT);
	VK_CHECK_RESULT(vkCreateBuffer(devices->device, &stagingBufferCreateInfo, nullptr, &stagingBuffer));

	VkMemoryPropertyFlags properties = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;
	MemoryAllocator::HostVisibleMemory hostVisibleMemory = devices->memoryAllocator.allocateBufferMemory(
		stagingBuffer, properties);
	hostVisibleMemory.mapData(devices->device, data);

	//image transfer
	VkCommandBuffer cmdBuf = devices->beginOneTimeSubmitCommandBuffer();
	vktools::setImageLayout(cmdBuf,
		image,
		VK_IMAGE_LAYOUT_UNDEFINED,
		VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);

	VkBufferImageCopy copy = vktools::initializers::bufferCopyRegion({ texWidth, texHeight, 1 });
	vkCmdCopyBufferToImage(cmdBuf,
		stagingBuffer,
		image,
		VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
		1,
		&copy
	);
	
	vktools::setImageLayout(cmdBuf, image,
		VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
		VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);

	devices->endOneTimeSubmitCommandBuffer(cmdBuf);
	devices->memoryAllocator.freeBufferMemory(stagingBuffer, properties);
	vkDestroyBuffer(devices->device, stagingBuffer, nullptr);

	//sampler
	VkSamplerCreateInfo samplerInfo = vktools::initializers::samplerCreateInfo(
		devices->availableFeatures, devices->properties);
	VK_CHECK_RESULT(vkCreateSampler(devices->device, &samplerInfo, nullptr, &descriptor.sampler));
	descriptor.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
}
