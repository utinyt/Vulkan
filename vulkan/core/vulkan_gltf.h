/*
* reference: 
https://github.com/SaschaWillems/Vulkan/blob/master/examples/gltfscenerendering/gltfscenerendering.h
https://github.com/nvpro-samples/nvpro_core/blob/master/nvh/gltfscene.h
*/
#pragma once
#include <unordered_map>
#include "vulkan_utils.h"
#include "vulkan_texture.h"
#include "../../include/tiny_gltf.h"

/*
* load gltf file and parse node / images / textures / materials
*/
struct VulkanDevice;
class VulkanGLTF {
public:
	/** handle to the vulkan devices */
	VulkanDevice* devices = nullptr;
	/** path to the model */
	std::string path;

	/** @breif load gltf scene and assign resources */
	void loadScene(VulkanDevice* devices, const std::string& path, VkBufferUsageFlags usage);
	/** @brief release all resources */
	void cleanup();

	/*
	* image
	*/
	std::vector<Texture2D> images;
	/** @brief load images from the model */
	void loadImages(tinygltf::Model& input);

	/*
	* texture (reference image via image index)
	*/
	std::vector<uint32_t> textures;
	/** @brief parse image references from the model */
	void loadTextures(tinygltf::Model& input);

	/*
	* material
	*/
	struct Material {
		glm::vec4 baseColorFactor = glm::vec4(1.f);
		int32_t baseColorTextureIndex = -1;
		uint32_t normalTextureIndex = 0;
		glm::vec3 emissiveFactor = glm::vec4(0.f);
		std::string alphaMode = "OPAQUE";
		float alphaCutoff = 0;
		bool doubleSided = false;
		VkPipeline pipeline = VK_NULL_HANDLE;
		VkDescriptorSet descriptorSet = VK_NULL_HANDLE;
		float roughtness = 0;
		float metallic = 0;
	};
	std::vector<Material> materials;
	/** @brief parse material info from the model */
	void loadMaterials(tinygltf::Model& input);

	/*
	* mesh
	*/
	/** @brief iterate all meshes in the model and get primitive info */
	std::unordered_map<int, std::vector<unsigned int>> meshToPrimitives{};

	/*
	* vertex data
	*/
	struct Vertex {
		glm::vec3 pos;
		glm::vec3 normal;
		glm::vec2 uv;
		glm::vec3 color;
		glm::vec4 tangent;
	};
	VkDeviceSize vertexSize = sizeof(Vertex);

	/*
	* node
	*/
	/** flat list of scene nodes */
	struct Node {
		glm::mat4 matrix;
		uint32_t primitiveIndex = 0;
	};
	std::vector<Node> nodes;
	/** @brief parse mesh data (vertex / index) */
	void loadNode(const tinygltf::Model& model, int nodeIndex, const glm::mat4& parentMatrix);

	/*
	* buffers
	*/
	struct BufferData {
		std::vector<uint32_t> indices;
		std::vector<glm::vec3> positions;
		std::vector<glm::vec3> normals;
		std::vector<glm::vec2> texCoord0s;
		std::vector<glm::vec3> colors;
		std::vector<glm::vec4> tangents;
		std::vector<int32_t> materialIndices;
	} bufferData;

	VkBuffer indexBuffer = VK_NULL_HANDLE;
	VkBuffer vertexBuffer = VK_NULL_HANDLE;
	VkBuffer normalBuffer = VK_NULL_HANDLE;
	VkBuffer uvBuffer = VK_NULL_HANDLE;
	VkBuffer colorBuffer = VK_NULL_HANDLE;
	VkBuffer tangentBuffer = VK_NULL_HANDLE;
	VkBuffer materialIndicesBuffer = VK_NULL_HANDLE;
	VkBuffer materialBuffer = VK_NULL_HANDLE;
	
	struct ShadeMaterial {
		glm::vec4 baseColorFactor = glm::vec4(1.f);
		glm::vec3 emissiveFactor = glm::vec4(0.f);
		int32_t baseColorTextureIndex = -1;
		float roughness = 0;
		float metallic = 0;
		float padding1, padding2;
	};
	struct Primitive {
		uint32_t firstIndex;
		uint32_t indexCount;
		uint32_t vertexOffset;
		uint32_t vertexCount;
		int32_t materialIndex;
	};
	std::vector<Primitive> primitives;
	VkBuffer primitiveBuffer = VK_NULL_HANDLE;

private:
	/** @brief get local matrix from the node */
	glm::mat4 getLocalMatrix(const tinygltf::Node& inputNode) const;
	/** @brief get vertex / index info from the input primitive */
	void addPrimitive(const tinygltf::Primitive& inputPrimitive, const tinygltf::Model& model);
};
