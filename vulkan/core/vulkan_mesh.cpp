#include <tiny_obj_loader.h>
#include "vulkan_mesh.h"
#include "vulkan_device.h"

/*
* constructor - simply calls load()
* 
* @param path - path to the mesh file
*/
Mesh::Mesh(const std::string& path) {
	load(path);
}

/*
* parse vertex data
*
* @param path - path to the mesh file
*/
void Mesh::load(const std::string& path) {
	vertices.cleanup();
	indices.clear();

	//model load
	tinyobj::attrib_t attrib;
	std::vector<tinyobj::shape_t> shapes;
	std::vector<tinyobj::material_t> materials;
	std::string error;

	if (!tinyobj::LoadObj(&attrib, &shapes, &materials, &error, path.c_str())) {
		throw std::runtime_error(error);
	}

	//vertex size
	hasNormalAttribute = !attrib.normals.empty();
	hasTexcoordAttribute = !attrib.texcoords.empty();
	vertexSize = sizeof(glm::vec3);
	if (hasNormalAttribute) {
		vertexSize += sizeof(glm::vec3);
	}
	if (hasTexcoordAttribute) {
		vertexSize += sizeof(glm::vec2);
	}

	//vertex count
	vertexCount = 0;
	for (const auto& shape : shapes) {
		vertexCount += shape.mesh.indices.size();
	}

	vertices.allocate(vertexSize * vertexCount);

	//copy data to the (cpu) buffer
	for (const auto& shape : shapes) {
		for (const auto& index : shape.mesh.indices) {
			glm::vec3 pos = {
				attrib.vertices[3 * index.vertex_index + 0],
				attrib.vertices[3 * index.vertex_index + 1],
				attrib.vertices[3 * index.vertex_index + 2]
			};
			vertices.push(&pos, sizeof(pos));

			if (hasNormalAttribute) {
				glm::vec3 normal = {
				attrib.normals[3 * index.normal_index + 0],
				attrib.normals[3 * index.normal_index + 1],
				attrib.normals[3 * index.normal_index + 2]
				};
				vertices.push(&normal, sizeof(normal));
			}

			if (hasTexcoordAttribute) {
				glm::vec2 texcoord = {
				attrib.texcoords[2 * index.texcoord_index + 0],
				attrib.texcoords[2 * index.texcoord_index + 1]
				};
				vertices.push(&texcoord, sizeof(texcoord));
			}

			indices.push_back(static_cast<uint32_t>(indices.size()));
		}
	}
}

/*
* create vertex+index buffer 
* 
* @return VkBuffer - created buffer
*/
VkBuffer Mesh::createModelBuffer(VulkanDevice* devices) {
	VkDeviceSize vertexBufferSize = vertices.bufferSize;
	VkDeviceSize indexBufferSize = sizeof(indices[0]) * indices.size();
	VkDeviceSize totalSize = vertexBufferSize + indexBufferSize;
	Mesh::Buffer buffer;
	buffer.allocate(totalSize);
	buffer.push(vertices.data(), vertexBufferSize);
	buffer.push(indices.data(), indexBufferSize);

	//create staging buffer
	VkBuffer stagingBuffer;
	VkBufferCreateInfo stagingBufferCreateInfo = vktools::initializers::bufferCreateInfo(
		totalSize, VK_BUFFER_USAGE_TRANSFER_SRC_BIT);
	VK_CHECK_RESULT(vkCreateBuffer(devices->device, &stagingBufferCreateInfo, nullptr, &stagingBuffer));

	//suballocate
	VkMemoryPropertyFlags properties = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;
	MemoryAllocator::HostVisibleMemory hostVisibleMemory = devices->memoryAllocator.allocateBufferMemory(
		stagingBuffer, properties);

	hostVisibleMemory.mapData(devices->device, buffer.data());

	//create vertex & index buffer
	VkBuffer vertexIndexBuffer;
	VkBufferCreateInfo bufferCreateInfo = vktools::initializers::bufferCreateInfo(
		totalSize, VK_BUFFER_USAGE_TRANSFER_DST_BIT | 
		VK_BUFFER_USAGE_VERTEX_BUFFER_BIT | 
		VK_BUFFER_USAGE_INDEX_BUFFER_BIT);
	VK_CHECK_RESULT(vkCreateBuffer(devices->device, &bufferCreateInfo, nullptr, &vertexIndexBuffer));

	//suballocation
	devices->memoryAllocator.allocateBufferMemory(vertexIndexBuffer, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);

	//host visible -> device local
	devices->copyBuffer(devices->commandPool, stagingBuffer, vertexIndexBuffer, totalSize);

	devices->memoryAllocator.freeBufferMemory(stagingBuffer, properties);
	vkDestroyBuffer(devices->device, stagingBuffer, nullptr);
	buffer.cleanup();

	return vertexIndexBuffer;
}

VkVertexInputBindingDescription Mesh::getBindingDescription() const{
	if (vertices.buffer == nullptr) {
		throw std::runtime_error("Mesh::getBindingDescription(): current mesh is empty");
	}
	
	uint32_t stride = sizeof(glm::vec3); //position
	if (hasNormalAttribute) {
		stride += sizeof(glm::vec3); //normal
	}
	if (hasTexcoordAttribute) {
		stride += sizeof(glm::vec2); //texcoord
	}

	return vktools::initializers::vertexInputBindingDescription(0, stride);
}

std::vector<VkVertexInputAttributeDescription> Mesh::getAttributeDescriptions() const{
	if (vertices.buffer == nullptr) {
		throw std::runtime_error("Mesh::getAttributeDescriptions(): current mesh is empty");
	}

	size_t attributeCount = 1 + hasNormalAttribute + hasTexcoordAttribute;
	std::vector<VkVertexInputAttributeDescription> attributeDescriptions(attributeCount);

	//position
	attributeDescriptions[0] = 
		vktools::initializers::vertexInputAttributeDescription(0, 0, VK_FORMAT_R32G32B32_SFLOAT, 0);
	uint32_t currentAttributeIndex = 1;
	uint32_t offset = sizeof(glm::vec3);

	//normal
	if (hasNormalAttribute) {
		attributeDescriptions[currentAttributeIndex] =
			vktools::initializers::vertexInputAttributeDescription(0, currentAttributeIndex, VK_FORMAT_R32G32B32_SFLOAT, offset);
		currentAttributeIndex++;
		offset += sizeof(glm::vec3);
	}

	//texcoord
	if (hasTexcoordAttribute) {
		attributeDescriptions[currentAttributeIndex] =
			vktools::initializers::vertexInputAttributeDescription(0, currentAttributeIndex, VK_FORMAT_R32G32_SFLOAT, offset);
		currentAttributeIndex++;
		offset += sizeof(glm::vec2);
	}
	
	return attributeDescriptions;
}

/*
* buffer memory allocation
* 
* @param bufferSize
*/
void Mesh::Buffer::allocate(size_t bufferSize) {
	this->bufferSize = bufferSize;
	buffer = (char*)malloc(bufferSize);
}

/*
* push data to the buffer
* 
* @param data
* @param dataSize
*/
void Mesh::Buffer::push(const void* data, size_t dataSize) {
	if (currentOffset + dataSize > bufferSize) {
		throw std::overflow_error("Mesh::Buffer::push(): buffer overrun");
	}
	memcpy(buffer + currentOffset, data, dataSize);
	currentOffset += dataSize;
}

/*
* deallocate buffer
*/
void Mesh::Buffer::cleanup() {
	free(buffer);
	buffer = nullptr;
	currentOffset = 0;
}

/*
* return buffer pointer
* 
* @return void* - pointer to the buffer
*/
void* Mesh::Buffer::data() const {
	return reinterpret_cast<void*>(buffer);
}
