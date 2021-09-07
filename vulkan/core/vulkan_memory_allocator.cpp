#include <algorithm>
#include <string>
#include "vulkan_memory_allocator.h"

/*
* get device & chunk size info
*
* @param device - logical device handle
* @param bufferImageGranularity
*/
void MemoryAllocator::init(VkDevice device, VkDeviceSize bufferImageGranularity,
	const VkPhysicalDeviceMemoryProperties& memProperties, VkMemoryAllocateFlags allocateFlags,
	uint32_t defaultChunkSize) {
	this->memProperties = memProperties;
	this->device = device;
	this->bufferImageGranularity = bufferImageGranularity;
	this->allocateFlags = allocateFlags;
	memoryPools.resize(memProperties.memoryTypeCount);

	//assign memory type index & chunk size to individual memory pool
	for (uint32_t i = 0; i < memProperties.memoryTypeCount; ++i) {
		uint32_t heapIndex = memProperties.memoryTypes[i].heapIndex;
		VkDeviceSize heapSize = memProperties.memoryHeaps[heapIndex].size;

		//chunk size
		if (heapSize < 1000000000) { //1GB
			memoryPools[i].defaultChunkSize = heapSize / 8;
		}
		else {
			memoryPools[i].defaultChunkSize = defaultChunkSize;
		}

		//memory type index
		memoryPools[i].memoryTypeIndex = i;
	}
}

/*
* (sub)allocate to pre-allocated memory
*
* @param buffer - buffer handle to allocate (bind) memory
* @param properties - memory properties needed for memory type search
* 
* @return HostVisibleMemory - contain device memory handle, size, offset
*/
MemoryAllocator::HostVisibleMemory MemoryAllocator::allocateBufferMemory(VkBuffer buffer, VkMemoryPropertyFlags properties) {
	VkMemoryRequirements memRequirements;
	vkGetBufferMemoryRequirements(device, buffer, &memRequirements);
	uint32_t memoryTypeIndex = findMemoryType(memRequirements.memoryTypeBits, properties, memProperties);

	MemoryPool& pool = memoryPools[memoryTypeIndex];
	//find suitable memory chunk
	for (size_t i = 0; i < pool.memoryChunks.size(); ++i) {
		if (pool.memoryChunks[i].currentSize > memRequirements.size) {
			MemoryBlock memoryBlock{};
			if (pool.memoryChunks[i].findSuitableMemoryLocation(memRequirements, bufferImageGranularity, memoryBlock)) {
				pool.memoryChunks[i].addBufferMemoryBlock(device, buffer, memoryBlock);
				if(properties & (VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT))
					return { pool.memoryChunks[i].memoryHandle, memoryBlock.size, memoryBlock.offset };
				else {
					return{};
				}
			}
		}
	}

	//failed to find suitable memory location - add new memory chunk
	pool.allocateChunk(device, allocateFlags);
	MemoryBlock memoryBlock{};
	if (pool.memoryChunks.back().findSuitableMemoryLocation(memRequirements, bufferImageGranularity, memoryBlock)) {
		pool.memoryChunks.back().addBufferMemoryBlock(device, buffer, memoryBlock);
		if (properties & (VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT))
			return { pool.memoryChunks.back().memoryHandle, memoryBlock.size, memoryBlock.offset };
		else {
			return{};
		}
	}

	return {};
}

/*
* basically the same as allocateBufferMemory but for VkImage
*
* @param image - image handle to allocate (bind) memory
* @param properties - memory properties needed for memory type search
*
* @return HostVisibleMemory - contain device memory handle, size, offset
*/
MemoryAllocator::HostVisibleMemory MemoryAllocator::allocateImageMemory(VkImage image, VkMemoryPropertyFlags properties) {
	VkMemoryRequirements memRequirements;
	vkGetImageMemoryRequirements(device, image, &memRequirements);
	uint32_t memoryTypeIndex = findMemoryType(memRequirements.memoryTypeBits, properties, memProperties);

	MemoryPool& pool = memoryPools[memoryTypeIndex];
	//find suitable memory chunk
	for (size_t i = 0; i < pool.memoryChunks.size(); ++i) {
		if (pool.memoryChunks[i].currentSize > memRequirements.size) {
			MemoryBlock memoryBlock{};
			if (pool.memoryChunks[i].findSuitableMemoryLocation(memRequirements, bufferImageGranularity, memoryBlock)) {
				pool.memoryChunks[i].addImageMemoryBlock(device, image, memoryBlock);
				if (properties & (VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT))
					return { pool.memoryChunks[i].memoryHandle, memoryBlock.size, memoryBlock.offset };
				else {
					return{};
				}
			}
		}
	}

	//failed to find suitable memory location - add new memory chunk
	pool.allocateChunk(device, allocateFlags);
	MemoryBlock memoryBlock{};
	if (pool.memoryChunks.back().findSuitableMemoryLocation(memRequirements, bufferImageGranularity, memoryBlock)) {
		pool.memoryChunks.back().addImageMemoryBlock(device, image, memoryBlock);
		if (properties & (VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT))
			return { pool.memoryChunks.back().memoryHandle, memoryBlock.size, memoryBlock.offset };
		else {
			return{};
		}
	}

	return {};
}

/*
* free buffer memory block
* 
* @param buffer - buffer to be deallocated
* @param properties - used to identify memory type; 
*	if it is not provided then memory allocator should search the whole memory pools
*/
void MemoryAllocator::freeBufferMemory(VkBuffer buffer, VkMemoryPropertyFlags properties) {
	if (buffer == VK_NULL_HANDLE) {
		return;
	}

	if (properties == VK_MEMORY_PROPERTY_FLAG_BITS_MAX_ENUM) {
		//memory properties are not provided; search the whole memory pools
		for (uint32_t memoryTypeIndex = 0; memoryTypeIndex < memoryPools.size(); ++memoryTypeIndex) {
			if (findAndEraseBufferMemoryBlock(buffer, memoryTypeIndex)) {
				return;
			}
		}
	}
	else {
		//identify memory type
		VkMemoryRequirements memRequirements;
		vkGetBufferMemoryRequirements(device, buffer, &memRequirements); //make sure to free memory before you free VkBuffer
		uint32_t memoryTypeIndex = findMemoryType(memRequirements.memoryTypeBits, properties, memProperties);

		if (findAndEraseBufferMemoryBlock(buffer, memoryTypeIndex)) {
			return;
		}
	}

	throw std::runtime_error("MemoryAllocator::freeBufferMemory(): there is no matching buffer");
}

/*
* basically the same as freeBufferMemory but for VkImage
*
* @param image - image to be deallocated
* @param properties - used to identify memory type;
*	if it is not provided then memory allocator should search the whole memory pools
*/
void MemoryAllocator::freeImageMemory(VkImage image, VkMemoryPropertyFlags properties) {
	if (image == VK_NULL_HANDLE) {
		return;
	}

	if (properties == VK_MEMORY_PROPERTY_FLAG_BITS_MAX_ENUM) {
		//memory properties are not provided; search the whole memory pools
		for (uint32_t memoryTypeIndex = 0; memoryTypeIndex < memoryPools.size(); ++memoryTypeIndex) {
			if (findAndEraseImageMemoryBlock(image, memoryTypeIndex)) {
				return;
			}
		}
	}
	else {
		//identify memory type
		VkMemoryRequirements memRequirements;
		vkGetImageMemoryRequirements(device, image, &memRequirements); //make sure to free memory before you free VkImage
		uint32_t memoryTypeIndex = findMemoryType(memRequirements.memoryTypeBits, properties, memProperties);

		if (findAndEraseImageMemoryBlock(image, memoryTypeIndex)) {
			return;
		}
	}

	throw std::runtime_error("MemoryAllocator::freeImageMemory(): there is no matching image");
}

/*
* free all allocated memory
*/
void MemoryAllocator::cleanup() {
	std::vector<size_t> reminaingMemoryBlockNums;
	for (size_t i = 0; i < memoryPools.size(); ++i) {
		reminaingMemoryBlockNums.push_back(memoryPools[i].cleanup(device));
	}

	bool remaining = false;
	for (size_t i = 0; i < reminaingMemoryBlockNums.size(); ++i) {
		if (reminaingMemoryBlockNums[i] != 0) {
			if (remaining == false) {
				remaining = true;
				LOG("*******************************************************");
				LOG("Some memories are still active");
				LOG("*******************************************************");
				LOG("Memory Pool Index / Number of Remaining Memories");
			}
			std::string str = std::to_string(i) + "\t\t" + std::to_string(reminaingMemoryBlockNums[i]);
			LOG(str);
			
		}
	}
	if (!remaining) {
		LOG("all buffer / image memories are freed properly");
	}
}

/*
* find a memory in 'memoryTypeBitsRequirements' that includes all of 'requiredProperties'
*
* @param memoryTypeBitsRequirements- bitfield that sets a bit for every memory type that is supported for the resource
* @param requiredProperties - required memory properties
*
* @return uint32_t - index of suitable memory type
*/
uint32_t MemoryAllocator::findMemoryType(uint32_t memoryTypeBitsRequirements,
	VkMemoryPropertyFlags requiredProperties, const VkPhysicalDeviceMemoryProperties& memProperties) {
	for (uint32_t memoryTypeIndex = 0; memoryTypeIndex < memProperties.memoryTypeCount; ++memoryTypeIndex) {
		bool isRequiredMemoryType = memoryTypeBitsRequirements & (1 << memoryTypeIndex);
		if (isRequiredMemoryType && (memProperties.memoryTypes[memoryTypeIndex].propertyFlags & requiredProperties) == requiredProperties) {
			return memoryTypeIndex;
		}
	}

	throw std::runtime_error("VulkanDevice::findMemoryType() - failed to find suitable memory type");
}

/*
* helper function for freeBufferMemory
* 
* @param buffer - buffer handle bound to the memory to be deallocated
* @param memoryTypeIndex - index of memoryType used by the buffer
*/
bool MemoryAllocator::findAndEraseBufferMemoryBlock(VkBuffer buffer, uint32_t memoryTypeIndex) {
	//find & remove 
	for (auto& memoryChunk : memoryPools[memoryTypeIndex].memoryChunks) {
		auto it = std::find_if(memoryChunk.memoryBlocks.begin(), memoryChunk.memoryBlocks.end(),
			[&buffer](const MemoryBlock& memoryBlock) {
				return buffer == memoryBlock.handle.bufferHandle;
			});

		if (it != memoryChunk.memoryBlocks.end()) {
			memoryChunk.currentSize += (it->blockEndLocation - it->offset);
			memoryChunk.memoryBlocks.erase(it);
			return true;
		}
	}
	return false;
}

/*
* helper function for freeImageMemory
*
* @param image - image handle bound to the memory to be deallocated
* @param memoryTypeIndex - index of memoryType used by the image
*/
bool MemoryAllocator::findAndEraseImageMemoryBlock(VkImage image, uint32_t memoryTypeIndex) {
	//find & remove 
	for (auto& memoryChunk : memoryPools[memoryTypeIndex].memoryChunks) {
		auto it = std::find_if(memoryChunk.memoryBlocks.begin(), memoryChunk.memoryBlocks.end(),
			[&image](const MemoryBlock& memoryBlock) {
				return image == memoryBlock.handle.imageHandle;
			});

		if (it != memoryChunk.memoryBlocks.end()) {
			memoryChunk.currentSize += (it->blockEndLocation - it->offset);
			memoryChunk.memoryBlocks.erase(it);
			return true;
		}
	}
	return false;
}

/*
* pre-allocate big chunk of memory
*
* @param device - logical device handle needed for vkAllocateMemory
*/
void MemoryAllocator::MemoryPool::allocateChunk(VkDevice device, VkMemoryAllocateFlags allocateFlags) {
	VkMemoryAllocateInfo allocInfo{};
	allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
	allocInfo.allocationSize = defaultChunkSize;
	allocInfo.memoryTypeIndex = memoryTypeIndex;
	VkMemoryAllocateFlagsInfo flagsInfo{};
	if (allocateFlags & VK_MEMORY_ALLOCATE_DEVICE_ADDRESS_BIT) {
		flagsInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_FLAGS_INFO;
		flagsInfo.flags = allocateFlags;
		//flagsInfo.deviceMask = 1;
		allocInfo.pNext = &flagsInfo;
	}
	MemoryChunk newChunk{ VK_NULL_HANDLE, defaultChunkSize, defaultChunkSize };
	VK_CHECK_RESULT(vkAllocateMemory(device, &allocInfo, nullptr, &newChunk.memoryHandle));
	memoryChunks.push_back(newChunk);
}

/*
* clean up all pre-allocated chunk of memory
*
* @param device - logical device handle needed for vkAllocateMemory
* 
* @return int - number of memoryblocks which are still active
*/
size_t MemoryAllocator::MemoryPool::cleanup(VkDevice device) {
	size_t activeMemoryNum = 0;
	for (auto& memoryChunk : memoryChunks) {
		activeMemoryNum += memoryChunk.memoryBlocks.size();
		vkFreeMemory(device, memoryChunk.memoryHandle, nullptr);
	}
	return activeMemoryNum;
}

/*
* return suitable memory location (offset) in current memory chunk
*
* @param memRequirements
* @param bufferImageGranularity
* @param memoryBlock - out parameter containing block layout if return value is true
*
* @return bool - true when found, false when there is none
*/
bool MemoryAllocator::MemoryChunk::findSuitableMemoryLocation(
	const VkMemoryRequirements& memRequirements, VkDeviceSize bufferImageGranularity, MemoryBlock& memoryBlock) {
	//TODO: consider bufferImageGranularity only when linear and non-linear resources are placed in adjacent memory
	//memory chunk is empty
	if (memoryBlocks.empty()) {
		//bufferImageGranularity check
		VkDeviceSize blockEndLocation = memRequirements.size;
		if (bufferImageGranularity > memRequirements.alignment) {
			if (VkDeviceSize remainder = memRequirements.size % bufferImageGranularity; remainder != 0) {
				VkDeviceSize padding = bufferImageGranularity - remainder;
				blockEndLocation += padding;
			}
		}
		memoryBlock = { VK_NULL_HANDLE, 0, memRequirements.size, memRequirements.alignment, blockEndLocation };
		return true;
	}

	//iterate all memory blocks
	for (size_t i = 0; i < memoryBlocks.size(); ++i) {
		VkDeviceSize location = memoryBlocks[i].blockEndLocation;
		VkDeviceSize blockSize = memRequirements.size;

		//alignment check
		if (VkDeviceSize remainder = location % memRequirements.alignment; remainder != 0) {
			//some padding is added to take account for alignment
			VkDeviceSize padding = memRequirements.alignment - remainder;
			location += padding;
			blockSize += padding;
		}

		//bufferImageGranularity check
		if (bufferImageGranularity > memRequirements.alignment) {
			if (VkDeviceSize remainder = (location + memRequirements.size) % bufferImageGranularity; remainder != 0) {
				VkDeviceSize padding = bufferImageGranularity - remainder;
				blockSize += padding;
			}
		}

		VkDeviceSize nextBlockEndLocation = (i + 1 == memoryBlocks.size()) ?
			chunkSize : memoryBlocks[i + 1].offset;

		VkDeviceSize spaceInBetween = nextBlockEndLocation - memoryBlocks[i].blockEndLocation;
		if (spaceInBetween > blockSize) {
			memoryBlock = { VK_NULL_HANDLE,
				location,
				memRequirements.size,
				memRequirements.alignment,
				memoryBlocks[i].blockEndLocation + blockSize };
			return true;
		}
	}

	return false;
}

/*
* sort memory block vector in real memory location order
*/
void MemoryAllocator::MemoryChunk::sort() {
	std::sort(memoryBlocks.begin(), memoryBlocks.end(),
		[](const MemoryBlock& l, const MemoryBlock& r) {
			return l.offset < r.offset;
		});
}

/*
* add new memory block to this memory chunk
*
* @param device - logical device handle
* @param buffer - buffer handle owning memoryBlock
* @param memoryBlock - memory block to add
*/
void MemoryAllocator::MemoryChunk::addBufferMemoryBlock(VkDevice device, VkBuffer buffer,
	MemoryBlock& memoryBlock) {
	memoryBlock.handle.bufferHandle = buffer;
	memoryBlocks.push_back(memoryBlock);
	sort();
	currentSize -= (memoryBlock.blockEndLocation - memoryBlock.offset);
	vkBindBufferMemory(device, buffer, memoryHandle, memoryBlock.offset);
}

/*
* basically same as addBufferMemoryBlock but for VkImage
*
* @param device - logical device handle
* @param image - image handle owning memoryBlock
* @param memoryBlock - memory block to add
*/
void MemoryAllocator::MemoryChunk::addImageMemoryBlock(VkDevice device, VkImage image,
	MemoryBlock& memoryBlock) {
	memoryBlock.handle.imageHandle = image;
	memoryBlocks.push_back(memoryBlock);
	sort();
	currentSize -= (memoryBlock.blockEndLocation - memoryBlock.offset);
	vkBindImageMemory(device, image, memoryHandle, memoryBlock.offset);
}

/*
* memcpy bufferData to device memory
* 
* @param device - logical device  handle needed for vkMapMemory
* @param bufferData - data to be copied
*/
void MemoryAllocator::HostVisibleMemory::mapData(VkDevice device, const void* bufferData) {
	void* data;
	vkMapMemory(device, memory, offset, size, 0, &data);
	memcpy(data, bufferData, (size_t)size);
	vkUnmapMemory(device, memory);
}

/*
* return data pointer
* 
* @return void* - data pointer
*/
void* MemoryAllocator::HostVisibleMemory::getHandle(VkDevice device) {
	void* data;
	vkMapMemory(device, memory, offset, size, 0, &data);
	return data;
}

/*
* unmap memory - vkUnmapMemory
*/
void MemoryAllocator::HostVisibleMemory::unmap(VkDevice device) {
	vkUnmapMemory(device, memory);
}
