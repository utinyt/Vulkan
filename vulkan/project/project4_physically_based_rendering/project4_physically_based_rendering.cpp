#include <array>
#include <chrono>
#include <string>
#include <include/imgui/imgui.h>
#include "core/vulkan_app_base.h"
#include "core/vulkan_mesh.h"
#define GLM_FORCE_RADIANS
#include "glm/glm.hpp"
#include "glm/gtc/matrix_transform.hpp"
#include "core/vulkan_imgui.h"
#include "core/vulkan_texture.h"
#include "core/vulkan_pipeline.h"
#include "core/vulkan_gltf.h"

class Imgui : public ImguiBase {
public:
	virtual void newFrame() override {
		ImGui::NewFrame();
		ImGui::Begin("Setting");

		static glm::vec3 lightPos = { 24.382f, 30.f, 0.1f };
		ImGui::Text("Light position");
		ImGui::SliderFloat("X [-30, 30]", &lightPos.x, -30.0f, 30.0f);
		ImGui::SliderFloat("Y [-30, 30]", &lightPos.y, -30.0f, 30.0f);
		ImGui::SliderFloat("Z [-30, 30]", &lightPos.z, -30.0f, 30.0f);
		if (lightPos != userInput.lightPos) {
			userInput.lightPos = lightPos;
		}

		ImGui::End();
		ImGui::Render();
	}

	/* user input collection */
	struct UserInput {
		glm::vec3 lightPos = { 24.382f, 30.f, 0.1f };
	} userInput;

	
};

class VulkanApp : public VulkanAppBase {
public:
	/** uniform buffer object */
	struct CameraMatrices {
		glm::mat4 view;
		glm::mat4 proj;
		glm::mat4 viewInverse;
		glm::mat4 projInverse;
	};

	/*
	* constructor - get window size & title
	*/
	VulkanApp(int width, int height, const std::string& appName)
		: VulkanAppBase(width, height, appName, VK_SAMPLE_COUNT_1_BIT) {
		imguiBase = new Imgui;
	}

	/*
	* destructor - destroy vulkan objects created in this level
	*/
	~VulkanApp() {
		if (devices.device == VK_NULL_HANDLE) {
			return;
		}

		imguiBase->cleanup();
		delete imguiBase;

		//descriptor releated resources
		vkDestroyDescriptorPool(devices.device, descriptorPool, nullptr);
		vkDestroyDescriptorSetLayout(devices.device, descriptorSetLayout, nullptr);

		//uniform buffers
		for (size_t i = 0; i < cameraUBO.size(); ++i) {
			devices.memoryAllocator.freeBufferMemory(cameraUBO[i],
				VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
			vkDestroyBuffer(devices.device, cameraUBO[i], nullptr);
		}

		//skybox textures
		skyboxTexture.cleanup();
		
		//model & skybox buffers
		gltfModel.cleanup();
		devices.memoryAllocator.freeBufferMemory(skyboxBuffer, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
		vkDestroyBuffer(devices.device, skyboxBuffer, nullptr);

		//framebuffers
		for (auto& framebuffer : framebuffers) {
			vkDestroyFramebuffer(devices.device, framebuffer, nullptr);
		}

		//pipelines & render pass
		vkDestroyPipeline(devices.device, skyboxPipeline, nullptr);
		vkDestroyPipeline(devices.device, pipeline, nullptr);
		vkDestroyPipelineLayout(devices.device, pipelineLayout, nullptr);
		vkDestroyPipeline(devices.device, gltfPipeline, nullptr);
		vkDestroyPipelineLayout(devices.device, gltfPipelineLayout, nullptr);
		vkDestroyRenderPass(devices.device, renderPass, nullptr);
	}

	/*
	* application initialization - also contain base class initApp()
	*/
	virtual void initApp() override {
		VulkanAppBase::initApp();

		//init cam setting
		camera.camPos = glm::vec3(1.f, 1.f, 2.f);
		camera.camFront = -camera.camPos;
		camera.camUp = glm::vec3(0.f, 1.f, 0.f);

		//mesh loading & buffer creation
		gltfModel.loadScene(&devices, "../../meshes/pica_pica_mini_diorama/scene.gltf", 0);

		//skybox model loading & buffer creation
		skybox.load("../../meshes/cube.obj");
		skyboxBuffer = skybox.createModelBuffer(&devices);

		//skybox texture load
		skyboxTexture.load(&devices, "../../textures/skybox/MarriottMadisonWest", VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE);

		//render pass
		createRenderPass();
		//descriptor sets
		createDescriptorSet();
		//pipeline
		createPipeline();
		//framebuffer
		createFramebuffers();
		//uniform buffers
		createUniformBuffers();
		//update descriptor set
		updateDescriptorSets();
		//imgui
		imguiBase->init(&devices, swapchain.extent.width, swapchain.extent.height,
			renderPass, MAX_FRAMES_IN_FLIGHT, sampleCount);
		//record command buffer
		recordCommandBuffer();
	}

private:
	/** render pass */
	VkRenderPass renderPass = VK_NULL_HANDLE;
	/** graphics pipeline */
	VkPipeline pipeline = VK_NULL_HANDLE, skyboxPipeline = VK_NULL_HANDLE, gltfPipeline = VK_NULL_HANDLE;
	/** pipeline layout */
	VkPipelineLayout pipelineLayout = VK_NULL_HANDLE, gltfPipelineLayout = VK_NULL_HANDLE;
	/** framebuffers */
	std::vector<VkFramebuffer> framebuffers;
	/** descriptor set bindings */
	DescriptorSetBindings bindings;
	/** descriptor layout */
	VkDescriptorSetLayout descriptorSetLayout;
	/** descriptor pool */
	VkDescriptorPool descriptorPool;
	/** descriptor sets */
	std::vector<VkDescriptorSet> descriptorSets;
	/** clear color */
	VkClearColorValue clearColor{0.f, 0.2f, 0.f, 1.f};

	/** uniform buffer handle */
	std::vector<VkBuffer> cameraUBO;
	/**  uniform buffer memory handle */
	std::vector<MemoryAllocator::HostVisibleMemory> cameraUBOMemories;
	/** gltf mesh */
	VulkanGLTF gltfModel;
	/** floor mesh */
	Mesh skybox;
	/** skybox vertex & index buffer */
	VkBuffer skyboxBuffer;
	/** skybox texture */
	TextureCube skyboxTexture;
	/** gltf material & transformation info */
	struct PushConstant {
		glm::mat4 modelMatrix = glm::mat4(1.f);
		glm::mat4 normalMatrix = glm::mat4(1.f);
		glm::vec3 lightPos = glm::vec3(0.f);
		uint32_t materialId = 0;
	} pushConstant;

	/*
	* called every frame - submit queues
	*/
	virtual void draw() override {
		uint32_t imageIndex = prepareFrame();

		//render
		VkPipelineStageFlags waitStages[] = { VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT };
		VkSubmitInfo submitInfo{};
		submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
		submitInfo.waitSemaphoreCount = 1;
		submitInfo.pWaitSemaphores = &presentCompleteSemaphores[currentFrame];
		submitInfo.pWaitDstStageMask = waitStages;
		submitInfo.commandBufferCount = 1;
		size_t commandBufferIndex = currentFrame * framebuffers.size() + imageIndex;
		submitInfo.pCommandBuffers = &commandBuffers[commandBufferIndex];
		submitInfo.signalSemaphoreCount = 1;
		submitInfo.pSignalSemaphores = &renderCompleteSemaphores[currentFrame];
		VK_CHECK_RESULT(vkQueueSubmit(devices.graphicsQueue, 1, &submitInfo, frameLimitFences[currentFrame]));

		submitFrame(imageIndex);
	}

	/*
	* called every frame - udpate glfw & imgui
	*/
	void update() override {
		VulkanAppBase::update();

		//uniform buffer
		updateUniformBuffer(currentFrame);
	}

	/*
	* create render pass
	*/
	void createRenderPass() {
		if (renderPass != VK_NULL_HANDLE) {
			vkDestroyRenderPass(devices.device, renderPass, nullptr);
		}

		bool isCurrentSampleCount1 = sampleCount == VK_SAMPLE_COUNT_1_BIT;

		std::vector<VkAttachmentDescription> attachments{};
		if (isCurrentSampleCount1) {
			attachments.resize(2); //present + depth
		}
		else {
			attachments.resize(3); //present + depth + multisample color buffer
		}

		//swapchain present attachment
		attachments[0].format = swapchain.surfaceFormat.format;
		attachments[0].samples = VK_SAMPLE_COUNT_1_BIT;
		attachments[0].loadOp = isCurrentSampleCount1 ? 
			VK_ATTACHMENT_LOAD_OP_CLEAR : VK_ATTACHMENT_LOAD_OP_DONT_CARE;
		attachments[0].storeOp = VK_ATTACHMENT_STORE_OP_STORE;
		attachments[0].stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
		attachments[0].stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
		attachments[0].initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
		attachments[0].finalLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
		VkAttachmentReference resolveRef{};
		resolveRef.attachment = 0;
		resolveRef.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

		//depth attachment
		attachments[1].format = depthFormat;
		attachments[1].samples = sampleCount;
		attachments[1].loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
		attachments[1].storeOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
		attachments[1].stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
		attachments[1].stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
		attachments[1].initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
		attachments[1].finalLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
		VkAttachmentReference depthRef{};
		depthRef.attachment = 1;
		depthRef.layout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

		VkAttachmentReference colorRef{};
		if (!isCurrentSampleCount1) {
			//multisample color attachment
			attachments[2].format = swapchain.surfaceFormat.format;
			attachments[2].samples = sampleCount;
			attachments[2].loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
			attachments[2].storeOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
			attachments[2].stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
			attachments[2].stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
			attachments[2].initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
			attachments[2].finalLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
			colorRef.attachment = 2;
			colorRef.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
		}
		
		//subpass
		VkSubpassDescription subpass{};
		subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
		subpass.colorAttachmentCount = 1;
		subpass.pColorAttachments = isCurrentSampleCount1 ? &resolveRef : &colorRef;
		subpass.pDepthStencilAttachment = &depthRef;
		subpass.pResolveAttachments = isCurrentSampleCount1 ? nullptr : &resolveRef;

		//subpass dependency
		VkSubpassDependency dependency{};
		dependency.srcSubpass = VK_SUBPASS_EXTERNAL;
		dependency.dstSubpass = 0;
		dependency.srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT | VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT;
		dependency.dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT | VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT;
		dependency.srcAccessMask = 0;
		dependency.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;

		//create renderpass
		VkRenderPassCreateInfo renderPassInfo{};
		renderPassInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
		renderPassInfo.attachmentCount = static_cast<uint32_t>(attachments.size());
		renderPassInfo.pAttachments = attachments.data();
		renderPassInfo.subpassCount = 1;
		renderPassInfo.pSubpasses = &subpass;
		renderPassInfo.dependencyCount = 1;
		renderPassInfo.pDependencies = &dependency;

		VK_CHECK_RESULT(vkCreateRenderPass(devices.device, &renderPassInfo, nullptr, &renderPass));
	}

	/*
	* create graphics pipeline
	*/
	void createPipeline() {
		if (pipeline != VK_NULL_HANDLE) {
			vkDestroyPipeline(devices.device, pipeline, nullptr);
			vkDestroyPipeline(devices.device, skyboxPipeline, nullptr);
			vkDestroyPipelineLayout(devices.device, pipelineLayout, nullptr);
			pipeline = VK_NULL_HANDLE;
			skyboxPipeline = VK_NULL_HANDLE;
			pipelineLayout = VK_NULL_HANDLE;
		}

		/*
		* gltf pipeline
		*/
		PipelineGenerator gen(devices.device);
		gen.addVertexInputBindingDescription({
			{0, sizeof(glm::vec3)}, //pos
			{1, sizeof(glm::vec3)}, //normal
			{2, sizeof(glm::vec2)} //texcoord0
		});
		gen.addVertexInputAttributeDescription({
			{0, 0, VK_FORMAT_R32G32B32_SFLOAT, 0}, //pos
			{1, 1, VK_FORMAT_R32G32B32_SFLOAT, 0}, //normal
			{2, 2, VK_FORMAT_R32G32_SFLOAT, 0} //texcoord0
		});
		gen.addDescriptorSetLayout({ descriptorSetLayout });
		gen.addPushConstantRange({
			{VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(PushConstant)}
		});
		gen.addShader(vktools::createShaderModule(devices.device, vktools::readFile("shaders/gltf_vert.spv")),
			VK_SHADER_STAGE_VERTEX_BIT);
		gen.addShader(vktools::createShaderModule(devices.device, vktools::readFile("shaders/gltf_frag.spv")),
			VK_SHADER_STAGE_FRAGMENT_BIT);
		gen.generate(renderPass, &gltfPipeline, &gltfPipelineLayout);
		gen.resetAll();
		/*
		* pipeline for main model
		*/
		//auto bindingDescription = floor.getBindingDescription();
		//auto attributeDescription = model.getAttributeDescriptions();

		//PipelineGenerator gen(devices.device);
		//gen.setColorBlendInfo(VK_FALSE);
		//gen.setMultisampleInfo(sampleCount);
		//gen.addVertexInputBindingDescription({ bindingDescription });
		//gen.addVertexInputAttributeDescription(attributeDescription);
		//gen.addDescriptorSetLayout({ descriptorSetLayout });
		//gen.addShader(
		//	vktools::createShaderModule(devices.device, vktools::readFile("shaders/reflection_vert.spv")),
		//	VK_SHADER_STAGE_VERTEX_BIT);
		//gen.addShader(
		//	vktools::createShaderModule(devices.device, vktools::readFile("shaders/reflection_frag.spv")),
		//	VK_SHADER_STAGE_FRAGMENT_BIT);

		////generate pipeline layout & pipeline
		//gen.generate(renderPass, &pipeline, &pipelineLayout);
		////reset shader & vertex description settings to reuse pipeline generator
		//gen.resetShaderVertexDescriptions();


		/*
		* pipeline for skybox
		*/
		auto bindingDescription = skybox.getBindingDescription();
		auto attributeDescription = skybox.getAttributeDescriptions();

		gen.setDepthStencilInfo(VK_TRUE, VK_TRUE, VK_COMPARE_OP_LESS_OR_EQUAL);
		gen.setRasterizerInfo(VK_POLYGON_MODE_FILL, VK_CULL_MODE_FRONT_BIT);
		gen.addDescriptorSetLayout({ descriptorSetLayout });
		gen.addVertexInputBindingDescription({ bindingDescription });
		gen.addVertexInputAttributeDescription(attributeDescription);
		gen.addShader(
			vktools::createShaderModule(devices.device, vktools::readFile("shaders/skybox_vert.spv")),
			VK_SHADER_STAGE_VERTEX_BIT);
		gen.addShader(
			vktools::createShaderModule(devices.device, vktools::readFile("shaders/skybox_frag.spv")),
			VK_SHADER_STAGE_FRAGMENT_BIT);

		//generate skybox pipeline
		gen.generate(renderPass, &skyboxPipeline, &pipelineLayout);

		LOG("created:\tgraphics pipelines");
	}

	/*
	* create framebuffer - use swapchain images
	*/
	virtual void createFramebuffers() override {
		for (auto& framebuffer : framebuffers) {
			vkDestroyFramebuffer(devices.device, framebuffer, nullptr);
		}
		framebuffers.resize(swapchain.imageCount);

		for (size_t i = 0; i < swapchain.imageCount; ++i) {
			std::vector<VkImageView> attachments;
			attachments.push_back(swapchain.imageViews[i]);
			attachments.push_back(depthImageView);

			if (sampleCount != VK_SAMPLE_COUNT_1_BIT) {
				attachments.push_back(multisampleColorImageView);
			}

			attachments.shrink_to_fit();

			VkFramebufferCreateInfo framebufferInfo{};
			framebufferInfo.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
			framebufferInfo.renderPass = renderPass;
			framebufferInfo.attachmentCount = static_cast<uint32_t>(attachments.size());
			framebufferInfo.pAttachments = attachments.data();
			framebufferInfo.width = swapchain.extent.width;
			framebufferInfo.height = swapchain.extent.height;
			framebufferInfo.layers = 1;
			VK_CHECK_RESULT(vkCreateFramebuffer(devices.device, &framebufferInfo, nullptr, &framebuffers[i]));
		}
		LOG("created:\tframebuffers");
	}

	/*
	* record drawing commands to command buffers
	*/
	virtual void recordCommandBuffer() override {
		VkCommandBufferBeginInfo cmdBufBeginInfo{};
		cmdBufBeginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;

		std::vector<VkClearValue> clearValues{};
		if (sampleCount == VK_SAMPLE_COUNT_1_BIT) {
			clearValues.resize(2);
			clearValues[0].color = clearColor;
			clearValues[1].depthStencil = { 1.f, 0 };
		}
		else {
			clearValues.resize(3);
			clearValues[0].color = clearColor;
			clearValues[1].depthStencil = { 1.f, 0 };
			clearValues[2].color = clearColor;
		}

		clearValues.shrink_to_fit();

		VkRenderPassBeginInfo renderPassBeginInfo{};
		renderPassBeginInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
		renderPassBeginInfo.renderPass = renderPass;
		renderPassBeginInfo.renderArea.offset = { 0, 0 };
		renderPassBeginInfo.renderArea.extent = swapchain.extent;
		renderPassBeginInfo.clearValueCount = static_cast<uint32_t>(clearValues.size());
		renderPassBeginInfo.pClearValues = clearValues.data();
		
		for (size_t i = 0; i < framebuffers.size() * MAX_FRAMES_IN_FLIGHT; ++i) {
			size_t framebufferIndex = i % framebuffers.size();
			renderPassBeginInfo.framebuffer = framebuffers[framebufferIndex];

			VK_CHECK_RESULT(vkBeginCommandBuffer(commandBuffers[i], &cmdBufBeginInfo));
			vkCmdBeginRenderPass(commandBuffers[i], &renderPassBeginInfo, VK_SUBPASS_CONTENTS_INLINE);

			//dynamic states
			vktools::setViewportScissorDynamicStates(commandBuffers[i], swapchain.extent);

			/*
			* draw model 
			*/
			vkCmdBindPipeline(commandBuffers[i], VK_PIPELINE_BIND_POINT_GRAPHICS, gltfPipeline);
			size_t descriptorSetIndex = i / framebuffers.size();
			vkCmdBindDescriptorSets(commandBuffers[i], VK_PIPELINE_BIND_POINT_GRAPHICS, gltfPipelineLayout, 0, 1,
				&descriptorSets[descriptorSetIndex], 0, nullptr);

			//binding vertex & index buffers
			VkDeviceSize offsets[] = { 0, 0, 0 };
			VkBuffer vertexBuffers[] = { gltfModel.vertexBuffer, gltfModel.normalBuffer, gltfModel.uvBuffer };
			vkCmdBindVertexBuffers(commandBuffers[i], 0, 3, vertexBuffers, offsets);
			vkCmdBindIndexBuffer(commandBuffers[i], gltfModel.indexBuffer, 0, VK_INDEX_TYPE_UINT32);

			//draw all node
			for (VulkanGLTF::Node& node : gltfModel.nodes) {
				VulkanGLTF::Primitive& primitive = gltfModel.primitives[node.primitiveIndex];
				pushConstant.modelMatrix = node.matrix;
				pushConstant.materialId = primitive.materialIndex;
				pushConstant.normalMatrix = glm::transpose(glm::inverse(cameraMatrices.view * node.matrix));
				pushConstant.lightPos = static_cast<Imgui*>(imguiBase)->userInput.lightPos;
				vkCmdPushConstants(commandBuffers[i], gltfPipelineLayout, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
					0, sizeof(PushConstant), &pushConstant);
				vkCmdDrawIndexed(commandBuffers[i], primitive.indexCount, 1, primitive.firstIndex, 0, 0);
			}
			
			/*
			* draw skybox
			*/
			vkCmdBindPipeline(commandBuffers[i], VK_PIPELINE_BIND_POINT_GRAPHICS, skyboxPipeline);
			vkCmdBindDescriptorSets(commandBuffers[i], VK_PIPELINE_BIND_POINT_GRAPHICS, pipelineLayout, 0, 1,
				&descriptorSets[descriptorSetIndex], 0, nullptr);

			vkCmdBindVertexBuffers(commandBuffers[i], 0, 1, &skyboxBuffer, offsets);
			uint32_t indexBufferOffset = skybox.vertices.bufferSize; // sizeof vertex buffer
			vkCmdBindIndexBuffer(commandBuffers[i], skyboxBuffer, indexBufferOffset, VK_INDEX_TYPE_UINT32);
			vkCmdDrawIndexed(commandBuffers[i], static_cast<uint32_t>(skybox.indices.size()), 1, 0, 0, 0);
			
			/*
			* imgui
			*/
			imguiBase->drawFrame(commandBuffers[i], descriptorSetIndex);

			vkCmdEndRenderPass(commandBuffers[i]);
			VK_CHECK_RESULT(vkEndCommandBuffer(commandBuffers[i]));
		}
		LOG("built:\t\tcommand buffers");
	}


	/*
	* create MAX_FRAMES_IN_FLIGHT of ubos
	*/
	void createUniformBuffers() {
		VkDeviceSize bufferSize = sizeof(CameraMatrices);
		cameraUBO.resize(MAX_FRAMES_IN_FLIGHT);
		cameraUBOMemories.resize(MAX_FRAMES_IN_FLIGHT);

		VkBufferCreateInfo uniformBufferCreateInfo = vktools::initializers::bufferCreateInfo(
			bufferSize, VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT);

		for (size_t i = 0; i < MAX_FRAMES_IN_FLIGHT; ++i) {
			VK_CHECK_RESULT(vkCreateBuffer(devices.device, &uniformBufferCreateInfo, nullptr, &cameraUBO[i]));
			cameraUBOMemories[i] = devices.memoryAllocator.allocateBufferMemory(
					cameraUBO[i], VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
		}
	}

	/*
	* update matrices in ubo - rotates 90 degrees per second
	* 
	* @param currentFrame - index of uniform buffer vector
	*/
	void updateUniformBuffer(size_t currentFrame) {
		CameraMatrices ubo{};
		ubo.view = cameraMatrices.view;
		ubo.proj = cameraMatrices.proj;
		ubo.viewInverse = glm::inverse(cameraMatrices.view);
		ubo.projInverse = glm::inverse(cameraMatrices.proj);

		cameraUBOMemories[currentFrame].mapData(devices.device, &ubo);
	}

	/*
	* set descriptor bindings & allocate destcriptor sets
	*/
	void createDescriptorSet() {
		//descriptor - 1 uniform buffer
		bindings.addBinding(0, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1, VK_SHADER_STAGE_VERTEX_BIT);
		//descriptor - 1 image sampler (skybox)
		bindings.addBinding(1, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_FRAGMENT_BIT);
		//descriptor - n image sampler (gltf textures)
		bindings.addBinding(2, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
			static_cast<uint32_t>(gltfModel.images.size()), VK_SHADER_STAGE_FRAGMENT_BIT);
		//descriptor - 1 storage buffer (gltf materials)
		bindings.addBinding(3, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_FRAGMENT_BIT);

		descriptorPool = bindings.createDescriptorPool(devices.device, MAX_FRAMES_IN_FLIGHT);
		descriptorSetLayout = bindings.createDescriptorSetLayout(devices.device);
		descriptorSets = vktools::allocateDescriptorSets(devices.device, descriptorSetLayout, descriptorPool, MAX_FRAMES_IN_FLIGHT);
	}

	/*
	* update descriptor set
	*/
	void updateDescriptorSets() {
		//gltf images
		std::vector<VkDescriptorImageInfo> imageInfos{};
		for (auto& image : gltfModel.images) {
			imageInfos.push_back(image.descriptor);
		}

		//gltf materials
		VkDescriptorBufferInfo materialBufferInfo{gltfModel.materialBuffer, 0, VK_WHOLE_SIZE};

		for (size_t i = 0; i < MAX_FRAMES_IN_FLIGHT; ++i) {
			VkDescriptorBufferInfo bufferInfo{ cameraUBO[i], 0, sizeof(CameraMatrices)};
			std::vector<VkWriteDescriptorSet> writes = {
				bindings.makeWrite(descriptorSets[i], 0, &bufferInfo),
				bindings.makeWrite(descriptorSets[i], 1, &skyboxTexture.descriptor),
				bindings.makeWriteArray(descriptorSets[i], 2, imageInfos.data()),
				bindings.makeWrite(descriptorSets[i], 3, &materialBufferInfo)
			};
			vkUpdateDescriptorSets(devices.device, static_cast<uint32_t>(writes.size()), writes.data(), 0, nullptr);
		}
	}
};

//entry point
RUN_APPLICATION_MAIN(VulkanApp, 1200, 800, "project4_physically_based_rendering");
