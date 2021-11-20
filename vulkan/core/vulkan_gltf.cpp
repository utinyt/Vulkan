/*
* reference: 
https://github.com/SaschaWillems/Vulkan/blob/master/examples/gltfscenerendering/gltfscenerendering.cpp
https://github.com/nvpro-samples/nvpro_core/blob/master/nvh/gltfscene.cpp
*/
#include "vulkan_gltf.h"
#include "glm/gtc/type_ptr.hpp"

/*
* upload vertex / index data to device memory
*/
void uploadBufferToDeviceMemory(VulkanDevice* devices, VkBuffer& buffer, const void* data,
	VkDeviceSize bufferSize, VkBufferUsageFlags usage) {
	//staging buffers
	VkBuffer stagingBuffer;
	MemoryAllocator::HostVisibleMemory stagingMemory = devices->createBuffer(stagingBuffer, bufferSize,
		VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
		VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT
	);
	stagingMemory.mapData(devices->device, data);

	//create buffer
	devices->createBuffer(buffer, bufferSize,
		VK_BUFFER_USAGE_TRANSFER_DST_BIT | usage
	);

	//copy buffer
	VkCommandBuffer cmdBuf = devices->beginCommandBuffer();
	VkBufferCopy bufferCopyRegion{};
	bufferCopyRegion.srcOffset = 0;
	bufferCopyRegion.dstOffset = 0;
	bufferCopyRegion.size = bufferSize;
	vkCmdCopyBuffer(cmdBuf, stagingBuffer, buffer, 1, &bufferCopyRegion);
	devices->endCommandBuffer(cmdBuf);

	devices->memoryAllocator.freeBufferMemory(stagingBuffer,
		VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
	vkDestroyBuffer(devices->device, stagingBuffer, nullptr);
}

/*
* load gltf scene and assign resources 
*/
void VulkanGLTF::loadScene(VulkanDevice* devices, const std::string& path, VkBufferUsageFlags usage) {
	//init
	this->devices = devices;

	tinygltf::Model model;
	tinygltf::TinyGLTF loader;
	std::string err, warn;

	//load gltf file
	bool result = loader.LoadASCIIFromFile(&model, &err, &warn, path);
	std::string str = "Mesh::loadGltf(): ";
	if (!warn.empty()) {
		throw std::runtime_error(str + warn);
	}
	if (!err.empty()) {
		throw std::runtime_error(str + err);
	}
	if (!result) {
		throw std::runtime_error("failed to parse glTF\n");
	}

	//extract path to the model
	this->path = path.substr(0, path.find_last_of('/') + 1);
	
	loadImages(model);
	loadMaterials(model);
	loadTextures(model);
	
	//construct a map (mesh index -> primitive indices of that mesh)
	uint32_t primitiveCount = 0;
	uint32_t meshCount = 0;
	for (const tinygltf::Mesh& mesh : model.meshes) {
		std::vector<uint32_t> primitiveIndices{};
		for (const tinygltf::Primitive& primitive : mesh.primitives) {
			primitiveIndices.emplace_back(primitiveCount++);
		}
		meshToPrimitives[meshCount++] = std::move(primitiveIndices);
	}

	//get all primitives
	for (const tinygltf::Mesh& mesh : model.meshes) {
		for (const tinygltf::Primitive& primitive : mesh.primitives) {
			addPrimitive(primitive, model);
		}
	}

	//convert the scene hierarchy to a flat list
	const tinygltf::Scene& scene = model.scenes[0];
	for (int nodeIndex : scene.nodes) {
		loadNode(model, nodeIndex, glm::mat4(1.f));
	}
	
	//create vertex & index buffer
	size_t indexBufferSize			= bufferData.indices.size() * sizeof(uint32_t);
	size_t positionBufferSize		= bufferData.positions.size() * sizeof(glm::vec3);
	size_t normalBufferSize			= bufferData.normals.size() * sizeof(glm::vec3);
	size_t uvBufferSize				= bufferData.texCoord0s.size() * sizeof(glm::vec2);
	size_t colorBufferSize			= bufferData.colors.size() * sizeof(glm::vec3);
	size_t tangentBufferSize		= bufferData.tangents.size() * sizeof(glm::vec4);
	size_t materialIndicesSize		= bufferData.materialIndices.size() * sizeof(int32_t);
	uploadBufferToDeviceMemory(devices, indexBuffer, bufferData.indices.data(), indexBufferSize, VK_BUFFER_USAGE_INDEX_BUFFER_BIT | usage);
	uploadBufferToDeviceMemory(devices, vertexBuffer, bufferData.positions.data(), positionBufferSize, VK_BUFFER_USAGE_VERTEX_BUFFER_BIT | usage);
	uploadBufferToDeviceMemory(devices, normalBuffer, bufferData.normals.data(), normalBufferSize, VK_BUFFER_USAGE_VERTEX_BUFFER_BIT | usage);
	uploadBufferToDeviceMemory(devices, uvBuffer, bufferData.texCoord0s.data(), uvBufferSize, VK_BUFFER_USAGE_VERTEX_BUFFER_BIT | usage);
	uploadBufferToDeviceMemory(devices, colorBuffer, bufferData.colors.data(), colorBufferSize, VK_BUFFER_USAGE_VERTEX_BUFFER_BIT | usage);
	uploadBufferToDeviceMemory(devices, tangentBuffer, bufferData.tangents.data(), tangentBufferSize, VK_BUFFER_USAGE_VERTEX_BUFFER_BIT | usage);
	uploadBufferToDeviceMemory(devices, materialIndicesBuffer, bufferData.materialIndices.data(), materialIndicesSize,
		VK_BUFFER_USAGE_STORAGE_BUFFER_BIT |
		VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT);
	
	//create primitive buffer
	size_t primitiveBufferSize = primitives.size() * sizeof(Primitive);
	uploadBufferToDeviceMemory(devices, primitiveBuffer, primitives.data(), primitiveBufferSize,
		VK_BUFFER_USAGE_STORAGE_BUFFER_BIT |
		VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT);

	std::vector<ShadeMaterial> shadeMaterialsData{};
	//create material buffer
	for (Material material : materials) {
		ShadeMaterial shadeMaterial{};
		shadeMaterial.baseColorFactor = material.baseColorFactor;
		shadeMaterial.emissiveFactor = material.emissiveFactor;
		shadeMaterial.baseColorTextureIndex = material.baseColorTextureIndex;
		shadeMaterial.roughness = material.roughtness;
		shadeMaterial.metallic = material.metallic;
		shadeMaterialsData.push_back(shadeMaterial);
	}
	size_t shadeMaterialsSize = shadeMaterialsData.size() * sizeof(ShadeMaterial);
	uploadBufferToDeviceMemory(devices, materialBuffer, shadeMaterialsData.data(), shadeMaterialsSize,
		VK_BUFFER_USAGE_STORAGE_BUFFER_BIT |
		VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT);

	//free all temporary data
	bufferData.colors.clear();
	bufferData.indices.clear();
	bufferData.materialIndices.clear();
	bufferData.normals.clear();
	bufferData.positions.clear();
	bufferData.tangents.clear();
	bufferData.texCoord0s.clear();
}

/*
* clean up
*/
void VulkanGLTF::cleanup() {
	if (devices == nullptr) {
		return;
	}

	//images
	for (Texture2D& image : images) {
		image.cleanup();
	}

	//pipelines
	for (Material& material : materials) {
		vkDestroyPipeline(devices->device, material.pipeline, nullptr);
	}

	//buffers
	devices->memoryAllocator.freeBufferMemory(vertexBuffer, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
	vkDestroyBuffer(devices->device, vertexBuffer, nullptr);
	devices->memoryAllocator.freeBufferMemory(indexBuffer, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
	vkDestroyBuffer(devices->device, indexBuffer, nullptr);
	devices->memoryAllocator.freeBufferMemory(normalBuffer, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
	vkDestroyBuffer(devices->device, normalBuffer, nullptr);
	devices->memoryAllocator.freeBufferMemory(uvBuffer, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
	vkDestroyBuffer(devices->device, uvBuffer, nullptr);
	devices->memoryAllocator.freeBufferMemory(colorBuffer, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
	vkDestroyBuffer(devices->device, colorBuffer, nullptr);
	devices->memoryAllocator.freeBufferMemory(tangentBuffer, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
	vkDestroyBuffer(devices->device, tangentBuffer, nullptr);
	devices->memoryAllocator.freeBufferMemory(materialIndicesBuffer, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
	vkDestroyBuffer(devices->device, materialIndicesBuffer, nullptr);
	devices->memoryAllocator.freeBufferMemory(materialBuffer, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
	vkDestroyBuffer(devices->device, materialBuffer, nullptr);
	devices->memoryAllocator.freeBufferMemory(primitiveBuffer, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
	vkDestroyBuffer(devices->device, primitiveBuffer, nullptr);
}

/*
* load images from the model 
* 
* @param input - loaded gltf model
*/
void VulkanGLTF::loadImages(tinygltf::Model& input) {
	if (input.images.empty()) {
		//add dummy texture
		images.resize(1);
		unsigned char pixel = 255;
		images[0].load(devices, &pixel, 1, 1, 1, VK_FORMAT_R8_SRGB);
		return;
	}

	images.resize(input.images.size());
	for (int i = 0; i < input.images.size(); ++i) {
		tinygltf::Image& srcImage = input.images[i];
		images[i].load(devices, path + srcImage.uri, VK_SAMPLER_ADDRESS_MODE_REPEAT);
	}
}

/*
* parse image references from the model 
* 
* @param input - loaded gltf model
*/
void VulkanGLTF::loadTextures(tinygltf::Model& input) {
	textures.resize(input.textures.size());
	for (int i = 0; i < input.textures.size(); ++i) {
		textures[i] = input.textures[i].source;
	}
}

/*
* parse material info from the model
* 
* @param input - loaded gltf model
*/
void VulkanGLTF::loadMaterials(tinygltf::Model& input) {
	materials.resize(input.materials.size());
	for (size_t i = 0; i < materials.size(); ++i) {
		tinygltf::Material srcMaterial = input.materials[i];
	
		//base color
		if (srcMaterial.values.find("baseColorFactor") != srcMaterial.values.end()) {
			materials[i].baseColorFactor = glm::make_vec4(srcMaterial.values["baseColorFactor"].ColorFactor().data());
		}
		//base color texture index
		if (srcMaterial.values.find("baseColorTexture") != srcMaterial.values.end()) {
			materials[i].baseColorTextureIndex = srcMaterial.values["baseColorTexture"].TextureIndex();
		}
		//base color texture index
		if (srcMaterial.additionalValues.find("normalTexture") != srcMaterial.additionalValues.end()) {
			materials[i].normalTextureIndex = srcMaterial.additionalValues["normalTexture"].TextureIndex();
		}
		//additional parameters
		materials[i].alphaMode = srcMaterial.alphaMode;
		materials[i].alphaCutoff = static_cast<float>(srcMaterial.alphaCutoff);
		materials[i].doubleSided = srcMaterial.doubleSided;
		materials[i].emissiveFactor = glm::make_vec3(srcMaterial.emissiveFactor.data());
		materials[i].roughtness = srcMaterial.pbrMetallicRoughness.roughnessFactor;
		materials[i].metallic = srcMaterial.pbrMetallicRoughness.metallicFactor;
	}
}

/* 
* parse mesh data (vertex / index) 
* 
* @param inputNode
* @param input - the whole model
* @param parent
* @param indexBuffer - output index buffer
* @param vertexBuffer - output vertex buffer
*/
void VulkanGLTF::loadNode(const tinygltf::Model& model, int nodeIndex, const glm::mat4& parentMatrix) {
	const tinygltf::Node& srcNode = model.nodes[nodeIndex];
	glm::mat4 matrix = parentMatrix * getLocalMatrix(srcNode);

	//if the node contains mesh data, we load vertices and indices from the buffers
	if (srcNode.mesh > -1) {
		const std::vector<unsigned int>& primitiveIndices = meshToPrimitives[srcNode.mesh];
		for (unsigned int primitiveIndex : primitiveIndices) {
			nodes.push_back({ matrix, primitiveIndex});
		}
	}

	for (int childIndex : srcNode.children) {
		loadNode(model, childIndex, matrix);
	}
}

/*
* get local matrix from the node 
* 
* @param inputNode
* 
* @return localMatrix
*/
glm::mat4 VulkanGLTF::getLocalMatrix(const tinygltf::Node& inputNode) const {
	if (inputNode.matrix.size() == 16) {
		return glm::make_mat4(inputNode.matrix.data());
	}

	glm::mat4 localMatrix = glm::mat4(1.f);
	if (inputNode.translation.size() == 3) {
		localMatrix = glm::translate(localMatrix, glm::vec3(glm::make_vec3(inputNode.translation.data())));
	}
	if (inputNode.rotation.size() == 4) {
		glm::quat q = glm::make_quat(inputNode.rotation.data());
		localMatrix *= glm::mat4(q);
	}
	if (inputNode.scale.size() == 3) {
		localMatrix = glm::scale(localMatrix, glm::vec3(glm::make_vec3(inputNode.scale.data())));
	}
	return localMatrix;
}

/*
* get vertex / index info from the input primitive 
* 
* @param inputPrimitive - input primitive to parse data
* @param input - the whole model
* @param indexBuffer - buffer to store indices
* @param vertexBuffer - buffer to store vertices
*/
void VulkanGLTF::addPrimitive(const tinygltf::Primitive& inputPrimitive, const tinygltf::Model& model) {
	Primitive primitive{};
	primitive.vertexOffset = static_cast<uint32_t>(bufferData.positions.size());
	primitive.firstIndex = static_cast<uint32_t>(bufferData.indices.size());

	/*
	* vertices
	*/
	const float* positionBuffer = nullptr;
	const float* normalsBuffer = nullptr;
	const float* texCoordsBuffer = nullptr;
	const float* tangentsBuffer = nullptr;
	size_t vertexCount = 0;

	//get positions
	if (auto posIt = inputPrimitive.attributes.find("POSITION"); posIt != inputPrimitive.attributes.end()) {
		const tinygltf::Accessor& accessor = model.accessors[posIt->second];
		const tinygltf::BufferView& view = model.bufferViews[accessor.bufferView];
		positionBuffer = reinterpret_cast<const float*>(&(model.buffers[view.buffer].data[accessor.byteOffset + view.byteOffset]));
		vertexCount = accessor.count;
	}
	//get vertex normals
	if (auto normalIt = inputPrimitive.attributes.find("NORMAL"); normalIt != inputPrimitive.attributes.end()) {
		const tinygltf::Accessor& accessor = model.accessors[normalIt->second];
		const tinygltf::BufferView& view = model.bufferViews[accessor.bufferView];
		normalsBuffer = reinterpret_cast<const float*>(&(model.buffers[view.buffer].data[accessor.byteOffset + view.byteOffset]));
	}
	//get vertex texture coordinates
	if (auto uvIt = inputPrimitive.attributes.find("TEXCOORD_0"); uvIt != inputPrimitive.attributes.end()) {
		const tinygltf::Accessor& accessor = model.accessors[uvIt->second];
		const tinygltf::BufferView& view = model.bufferViews[accessor.bufferView];
		texCoordsBuffer = reinterpret_cast<const float*>(&(model.buffers[view.buffer].data[accessor.byteOffset + view.byteOffset]));
	}
	//get tengents
	if (auto tangentIt = inputPrimitive.attributes.find("TANGENT"); tangentIt != inputPrimitive.attributes.end()) {
		const tinygltf::Accessor& accessor = model.accessors[tangentIt->second];
		const tinygltf::BufferView& view = model.bufferViews[accessor.bufferView];
		tangentsBuffer = reinterpret_cast<const float*>(&(model.buffers[view.buffer].data[accessor.byteOffset + view.byteOffset]));
	}

	//append data to model's vertex buffer
	for (size_t i = 0; i < vertexCount; ++i) {
		bufferData.positions.push_back(glm::make_vec3(&positionBuffer[i * 3]));
		bufferData.normals.push_back(normalsBuffer ? glm::normalize(glm::make_vec3(&normalsBuffer[i * 3])) : glm::vec3(0.f));
		bufferData.texCoord0s.push_back(texCoordsBuffer ? glm::make_vec2(&texCoordsBuffer[i * 2]) : glm::vec2(0.f));
		bufferData.colors.push_back(glm::vec3(1.f));
		bufferData.tangents.push_back(tangentsBuffer ? glm::make_vec4(&tangentsBuffer[i * 4]) : glm::vec4(0.f));
	}
	
	/*
	* indices
	*/
	const tinygltf::Accessor& accessor = model.accessors[inputPrimitive.indices];
	const tinygltf::BufferView& bufferView = model.bufferViews[accessor.bufferView];
	const tinygltf::Buffer& buffer = model.buffers[bufferView.buffer];

	uint32_t indexCount = static_cast<uint32_t>(accessor.count);

	switch (accessor.componentType) {
	case TINYGLTF_PARAMETER_TYPE_UNSIGNED_INT: {
		const uint32_t* buf = reinterpret_cast<const uint32_t*>(&buffer.data[accessor.byteOffset + bufferView.byteOffset]);
		for (size_t i = 0; i < accessor.count; ++i) {
			bufferData.indices.push_back(buf[i] + primitive.vertexOffset);
		}
		break;
	}
	case TINYGLTF_PARAMETER_TYPE_UNSIGNED_SHORT: {
		const uint16_t* buf = reinterpret_cast<const uint16_t*>(&buffer.data[accessor.byteOffset + bufferView.byteOffset]);
		for (size_t i = 0; i < accessor.count; ++i) {
			bufferData.indices.push_back(buf[i] + primitive.vertexOffset);
		}
		break;
	}
	case TINYGLTF_PARAMETER_TYPE_UNSIGNED_BYTE: {
		const uint8_t* buf = reinterpret_cast<const uint8_t*>(&buffer.data[accessor.byteOffset + bufferView.byteOffset]);
		for (size_t i = 0; i < accessor.count; ++i) {
			bufferData.indices.push_back(buf[i] + primitive.vertexOffset);
		}
		break;
	}
	default:
		throw std::runtime_error("VulkanGLTF::addPrimitive(): index component type" + 
			std::to_string(accessor.componentType) + " not supported");
	}

	primitive.indexCount = indexCount;
	primitive.vertexCount = static_cast<uint32_t>(vertexCount);
	primitive.materialIndex = inputPrimitive.material;
	primitives.push_back(primitive);
	bufferData.materialIndices.push_back(inputPrimitive.material);
}
