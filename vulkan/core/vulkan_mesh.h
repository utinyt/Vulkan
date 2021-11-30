#pragma once
#include <array>
#include "vulkan_utils.h"
#include "glm/glm.hpp"

struct VulkanDevice;
struct Mesh {
	struct Buffer {
		Buffer() {};
		~Buffer() {
			cleanup();
		}
		void allocate(size_t bufferSize);
		void push(const void* data, size_t dataSize);
		void cleanup();
		void* data() const;

		char* buffer = nullptr;
		size_t bufferSize = 0;
		size_t currentOffset = 0;
	} vertices;

	Mesh(){}
	Mesh(const std::string& path);
	/** @brief load obj model from a file */
	void load(const std::string& path);
	/** @brief build model from vertex data */
	void load(const std::vector<glm::vec3>& position,
		const std::vector<glm::vec3>& normal,
		const std::vector<glm::vec2>& uv,
		const std::vector<uint32_t>& indices,
		uint32_t vertexCount, bool hasNormal, bool hasUV);
	/** @brief create vertex+index buffer */
	VkBuffer createModelBuffer(VulkanDevice* devices);

	/** @brief return vertex binding description for current model */
	VkVertexInputBindingDescription getBindingDescription() const;
	/** @brief return vertex attribute description for current model */
	std::vector<VkVertexInputAttributeDescription> getAttributeDescriptions() const;

	/** vector of index - uint32_t by default */
	std::vector<uint32_t> indices;
	/** vertex size (or stride) */
	size_t vertexSize = 0;
	/** vertex count */
	size_t vertexCount = 0;

private:
	bool hasNormalAttribute = false;
	bool hasTexcoordAttribute = false;
};
