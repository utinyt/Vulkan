#include <array>
#include <chrono>
#include <random>
#include <include/imgui/imgui.h>
#include "core/vulkan_app_base.h"
#include "core/vulkan_mesh.h"
#define GLM_FORCE_RADIANS
#include "glm/glm.hpp"
#include "glm/gtc/matrix_transform.hpp"
#include "core/vulkan_imgui.h"
#include "core/vulkan_texture.h"
#include "core/vulkan_pipeline.h"
#include "core/vulkan_framebuffer.h"

namespace {
	std::random_device device;
	std::mt19937_64 RNGen(device());
	std::uniform_real_distribution<> rdFloat(0.0, 1.0);
	const int LIGHT_NUM = 20;
	const int INSTANCE_NUM_SQRT = 32;
}

class Imgui : public ImguiBase {
public:
	virtual void newFrame() override {
		ImGui::NewFrame();
		ImGui::Begin("Setting");

		ImGui::End();
		ImGui::Render();
	}

	/* user input collection */
	struct UserInput {
	} userInput;
};

class VulkanApp : public VulkanAppBase {
public:
	/*
	* constructor - get window size & title
	*/
	VulkanApp(int width, int height, const std::string& appName)
		: VulkanAppBase(width, height, appName) {
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
			devices.memoryAllocator.freeBufferMemory(lightUBO[i],
				VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
			vkDestroyBuffer(devices.device, lightUBO[i], nullptr);
		}

		//model & floor buffers
		devices.memoryAllocator.freeBufferMemory(modelBuffer, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
		vkDestroyBuffer(devices.device, modelBuffer, nullptr);
		devices.memoryAllocator.freeBufferMemory(floorBuffer, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
		vkDestroyBuffer(devices.device, floorBuffer, nullptr);

		//instanced position buffer
		devices.memoryAllocator.freeBufferMemory(instancedTransformationBuffer, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
		vkDestroyBuffer(devices.device, instancedTransformationBuffer, nullptr);

		//framebuffers
		for (auto& framebuffer : framebuffers) {
			vkDestroyFramebuffer(devices.device, framebuffer, nullptr);
		}

		//pipelines & render pass
		vkDestroyPipeline(devices.device, pipeline, nullptr);
		vkDestroyPipelineLayout(devices.device, pipelineLayout, nullptr);
		vkDestroyRenderPass(devices.device, renderPass, nullptr);
	}

	/*
	* application initialization - also contain base class initApp()
	*/
	virtual void initApp() override {
		sampleCount = static_cast<VkSampleCountFlagBits>(devices.maxSampleCount);
		VulkanAppBase::initApp();

		//init cam setting
		camera.camPos = glm::vec3(5.f, 5.f, 20.f);
		camera.camFront = -camera.camPos;
		camera.camUp = glm::vec3(0.f, 1.f, 0.f);

		//mesh loading & buffer creation
		model.load("../../meshes/bunny.obj");
		modelBuffer = model.createModelBuffer(&devices);
		floor.load("../../meshes/cube.obj");
		floorBuffer = floor.createModelBuffer(&devices);

		//instance possition buffer
		createInstancePositionBuffer();

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
	/** uniform buffer object */
	struct CameraMatrices {
		glm::mat4 view;
		glm::mat4 normalMatrix;
		glm::mat4 proj;
	} ubo;

	/** Light struct */
	struct Light {
		glm::vec4 pos;
		glm::vec3 color;
		float radius;
	};

	/** ubo for deferred rendering */
	struct LightInfo {
		Light lights[LIGHT_NUM];
		int sampleCount = 1;
	} lightInfo;

	/** random float generator */
	std::random_device rd;
	std::mt19937 mt;

	/** render pass */
	VkRenderPass renderPass = VK_NULL_HANDLE;
	/** graphics pipeline */
	VkPipeline pipeline = VK_NULL_HANDLE;
	/** pipeline layout */
	VkPipelineLayout pipelineLayout = VK_NULL_HANDLE;
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
	/** uniform buffer handle */
	std::vector<VkBuffer> lightUBO;
	/** uniform buffer memory handle */
	std::vector<MemoryAllocator::HostVisibleMemory> lightUBOMemories;

	/** abstracted 3d mesh */
	Mesh model, floor;
	/** model vertex & index buffer */
	VkBuffer modelBuffer, floorBuffer;

	/** instanced model positions */
	struct Transformation {
		glm::vec3 pos;
		glm::vec3 scale;
	};
	std::vector<Transformation> instancedTransformation;
	/** buffer for instancePos */
	VkBuffer instancedTransformationBuffer = VK_NULL_HANDLE;

	/*
	* called every frame - submit queues
	*/
	virtual void draw() override {
		uint32_t imageIndex = prepareFrame();

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
		updateUniformBuffer(currentFrame);
	}

	/*
	* override resize function - update offscreen resources
	*/
	void resizeWindow(bool /*recordCommandBuffer*/) override {
		VulkanAppBase::resizeWindow(false);
		updateDescriptorSets();
		recordCommandBuffer();
	}

	/*
	* create a buffer for instanced model positions
	*/
	void createInstancePositionBuffer() {
		instancedTransformation.push_back({ glm::vec3(0.f, 0.f, 0.f), glm::vec3(50.f, 1.f, 50.f) }); //floor
		if (INSTANCE_NUM_SQRT != 1) {
			int start = -INSTANCE_NUM_SQRT / 2;
			for (int col = start; col < -start; ++col) {
				for (int row = start; row < -start; ++row) {
					instancedTransformation.push_back({
						glm::vec3(col * 1.5, 0.5f, row * 1.5),
						glm::vec3(1.f, 1.f, 1.f)
						});
				}
			}
		}
		else {
			instancedTransformation.push_back({
						glm::vec3(0, 0.5f, 0.f),
						glm::vec3(1.f, 1.f, 1.f)
				});
		}
		

		instancedTransformation.shrink_to_fit();

		VkDeviceSize bufferSize = sizeof(instancedTransformation[0]) * instancedTransformation.size();

		//create staging buffer
		VkBuffer stagingBuffer;
		VkBufferCreateInfo stagingBufferCreateInfo = vktools::initializers::bufferCreateInfo(
			bufferSize, VK_BUFFER_USAGE_TRANSFER_SRC_BIT);
		VK_CHECK_RESULT(vkCreateBuffer(devices.device, &stagingBufferCreateInfo, nullptr, &stagingBuffer));

		//suballocate
		VkMemoryPropertyFlags properties = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;
		MemoryAllocator::HostVisibleMemory hostVisibleMemory = devices.memoryAllocator.allocateBufferMemory(
			stagingBuffer, properties);

		hostVisibleMemory.mapData(devices.device, instancedTransformation.data());

		//create vertex & index buffer
		VkBufferCreateInfo bufferCreateInfo = vktools::initializers::bufferCreateInfo(
			bufferSize,
			VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_VERTEX_BUFFER_BIT);
		VK_CHECK_RESULT(vkCreateBuffer(devices.device, &bufferCreateInfo, nullptr, &instancedTransformationBuffer));

		//suballocation
		devices.memoryAllocator.allocateBufferMemory(instancedTransformationBuffer, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);

		//host visible -> device local
		devices.copyBuffer(devices.commandPool, stagingBuffer, instancedTransformationBuffer, bufferSize);

		devices.memoryAllocator.freeBufferMemory(stagingBuffer, properties);
		vkDestroyBuffer(devices.device, stagingBuffer, nullptr);
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
			vkDestroyPipelineLayout(devices.device, pipelineLayout, nullptr);
			pipeline = VK_NULL_HANDLE;
			pipelineLayout = VK_NULL_HANDLE;
		}

		//model descriptions
		auto bindingDescription = model.getBindingDescription();
		auto attributeDescription = model.getAttributeDescriptions();
		//instanced position descriptions
		VkVertexInputBindingDescription instancedPosBindingDesc{1, sizeof(Transformation), VK_VERTEX_INPUT_RATE_INSTANCE};
		attributeDescription.push_back({ 2, 1, VK_FORMAT_R32G32B32_SFLOAT, 0 });
		attributeDescription.push_back({ 3, 1, VK_FORMAT_R32G32B32_SFLOAT, sizeof(glm::vec3) });

		/*
		* full screen quad pipeline
		*/
		PipelineGenerator gen(devices.device);
		gen.setColorBlendInfo(VK_FALSE, 1);
		gen.setMultisampleInfo(sampleCount);
		gen.addVertexInputBindingDescription({ bindingDescription, instancedPosBindingDesc });
		gen.addVertexInputAttributeDescription(attributeDescription);
		gen.addDescriptorSetLayout({ descriptorSetLayout });
		gen.addShader(
			vktools::createShaderModule(devices.device, vktools::readFile("shaders/forward_vert.spv")),
			VK_SHADER_STAGE_VERTEX_BIT);
		gen.addShader(
			vktools::createShaderModule(devices.device, vktools::readFile("shaders/forward_frag.spv")),
			VK_SHADER_STAGE_FRAGMENT_BIT);

		//generate pipeline layout & pipeline
		gen.generate(renderPass, &pipeline, &pipelineLayout);

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
			attachments.shrink_to_fit();

			if (sampleCount != VK_SAMPLE_COUNT_1_BIT) {
				attachments.push_back(multisampleColorImageView);
			}

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

			vkCmdBindPipeline(commandBuffers[i], VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline);
			size_t descriptorSetIndex = i / framebuffers.size();
			vkCmdBindDescriptorSets(commandBuffers[i], VK_PIPELINE_BIND_POINT_GRAPHICS, pipelineLayout, 0, 1,
				&descriptorSets[descriptorSetIndex], 0, nullptr);

			VkDeviceSize offsets[] = { 0 };
			vkCmdBindVertexBuffers(commandBuffers[i], 0, 1, &floorBuffer, offsets);
			vkCmdBindVertexBuffers(commandBuffers[i], 1, 1, &instancedTransformationBuffer, offsets);
			VkDeviceSize indexBufferOffset = floor.vertices.bufferSize; // sizeof vertex buffer
			vkCmdBindIndexBuffer(commandBuffers[i], floorBuffer, indexBufferOffset, VK_INDEX_TYPE_UINT32);
			vkCmdDrawIndexed(commandBuffers[i], static_cast<uint32_t>(floor.indices.size()), 1, 0, 0, 0);

			vkCmdBindVertexBuffers(commandBuffers[i], 0, 1, &modelBuffer, offsets);
			offsets[0] = sizeof(Transformation);
			vkCmdBindVertexBuffers(commandBuffers[i], 1, 1, &instancedTransformationBuffer, offsets);
			indexBufferOffset = model.vertices.bufferSize; // sizeof vertex buffer
			vkCmdBindIndexBuffer(commandBuffers[i], modelBuffer, indexBufferOffset, VK_INDEX_TYPE_UINT32);
			vkCmdDrawIndexed(commandBuffers[i], static_cast<uint32_t>(model.indices.size()), INSTANCE_NUM_SQRT * INSTANCE_NUM_SQRT, 0, 0, 0);
			

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
		VkDeviceSize cameraUBOSize = sizeof(CameraMatrices);
		cameraUBO.resize(MAX_FRAMES_IN_FLIGHT);
		cameraUBOMemories.resize(MAX_FRAMES_IN_FLIGHT);
		VkDeviceSize lightUBOSize = sizeof(LightInfo);
		lightUBO.resize(MAX_FRAMES_IN_FLIGHT);
		lightUBOMemories.resize(MAX_FRAMES_IN_FLIGHT);

		VkBufferCreateInfo cameraUBOCreateInfo = vktools::initializers::bufferCreateInfo(
			cameraUBOSize, VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT);
		VkBufferCreateInfo lightUBOCreateInfo = vktools::initializers::bufferCreateInfo(
			lightUBOSize, VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT);

		for (size_t i = 0; i < MAX_FRAMES_IN_FLIGHT; ++i) {
			VK_CHECK_RESULT(vkCreateBuffer(devices.device, &cameraUBOCreateInfo, nullptr, &cameraUBO[i]));
			cameraUBOMemories[i] = devices.memoryAllocator.allocateBufferMemory(
					cameraUBO[i], VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
			VK_CHECK_RESULT(vkCreateBuffer(devices.device, &lightUBOCreateInfo, nullptr, &lightUBO[i]));
			lightUBOMemories[i] = devices.memoryAllocator.allocateBufferMemory(
				lightUBO[i], VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
		}

		//assign color & radius
		for (int i = 0; i < LIGHT_NUM; ++i) {
			lightInfo.lights[i].color = glm::vec3(rdFloat(RNGen), rdFloat(RNGen), rdFloat(RNGen));
			float constant = 1.f;
			float linear = 0.7f;
			float quadratic = 1.8f;
			float lightMax = std::fmaxf(std::fmaxf(lightInfo.lights[i].color.x,
				lightInfo.lights[i].color.y), lightInfo.lights[i].color.z);
			lightInfo.lights[i].radius = (-linear + std::sqrtf(
				linear * linear - 4 * quadratic * (constant - (256 / 5.f) * lightMax)) / (2 * quadratic));
		}
	}

	/*
	* update matrices in ubo - rotates 90 degrees per second
	* 
	* @param currentFrame - index of uniform buffer vector
	*/
	void updateUniformBuffer(size_t currentFrame) {
		static auto startTime = std::chrono::high_resolution_clock::now();
		auto currentTime = std::chrono::high_resolution_clock::now();
		float time = std::chrono::duration<float, std::chrono::seconds::period>(currentTime - startTime).count();
		
		/*
		* update camera
		*/
		ubo.view = glm::lookAt(camera.camPos, camera.camPos + camera.camFront, camera.camUp);
		ubo.normalMatrix = glm::transpose(glm::inverse(ubo.view /** ubo.model*/));
		ubo.proj = glm::perspective(glm::radians(45.f),
			swapchain.extent.width / (float)swapchain.extent.height, 0.1f, 100.f);
		ubo.proj[1][1] *= -1;

		cameraUBOMemories[currentFrame].mapData(devices.device, &ubo);

		/*
		* update lights & renderMode
		*/
		lightInfo.sampleCount = sampleCount;
		float PI = 3.141592f;
		float angleInc = 2 * PI / LIGHT_NUM;
		for (int i = 0; i < LIGHT_NUM; ++i) {
			lightInfo.lights[i].pos =
				glm::vec4(12 * std::cos(time / 3 + i * angleInc), 3.f, 12 * std::sin(time / 3 + i * angleInc), 1.f);
			lightInfo.lights[i].pos = ubo.view * lightInfo.lights[i].pos; // light position in view space
		}
		lightUBOMemories[currentFrame].mapData(devices.device, &lightInfo);
	}

	/*
	* set descriptor bindings & allocate destcriptor sets
	*/
	void createDescriptorSet() {
		//descriptor - 2 uniform buffers
		bindings.addBinding(0, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1, VK_SHADER_STAGE_VERTEX_BIT);
		bindings.addBinding(1, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1, VK_SHADER_STAGE_FRAGMENT_BIT);
		descriptorPool = bindings.createDescriptorPool(devices.device, MAX_FRAMES_IN_FLIGHT);
		descriptorSetLayout = bindings.createDescriptorSetLayout(devices.device);
		descriptorSets = vktools::allocateDescriptorSets(devices.device, descriptorSetLayout, descriptorPool, MAX_FRAMES_IN_FLIGHT);
	}

	/*
	* update descriptor set
	*/
	void updateDescriptorSets() {
		for (size_t i = 0; i < MAX_FRAMES_IN_FLIGHT; ++i) {
			//camera matrices
			VkDescriptorBufferInfo cameraUBObufferInfo{ cameraUBO[i], 0, sizeof(CameraMatrices)};
			//light info
			VkDescriptorBufferInfo lightInfoBufferInfo{ lightUBO[i], 0, sizeof(LightInfo) };

			std::vector<VkWriteDescriptorSet> writes;
			writes.push_back(bindings.makeWrite(descriptorSets[i], 0, &cameraUBObufferInfo));
			writes.push_back(bindings.makeWrite(descriptorSets[i], 1, &lightInfoBufferInfo));
			vkUpdateDescriptorSets(devices.device, static_cast<uint32_t>(writes.size()), writes.data(), 0, nullptr);
		}
	}
};

//entry point
RUN_APPLICATION_MAIN(VulkanApp, 1200, 800, "project2");
