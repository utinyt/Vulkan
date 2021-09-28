#include <array>
#include <chrono>
#include "core/vulkan_app_base.h"
#include "core/vulkan_mesh.h"
#define GLM_FORCE_RADIANS
#include "glm/glm.hpp"
#include "glm/gtc/matrix_transform.hpp"
#include "core/vulkan_imgui.h"
#include "core/vulkan_texture.h"
#include "core/vulkan_pipeline.h"

class VulkanApp : public VulkanAppBase {
public:
	/** uniform buffer object */
	struct UBO {
		glm::mat4 model;
		glm::mat4 view;
		glm::mat4 normalMatrix;
		glm::mat4 proj;
	};

	/*
	* constructor - get window size & title
	*/
	VulkanApp(int width, int height, const std::string& appName)
		: VulkanAppBase(width, height, appName) {}

	/*
	* destructor - destroy vulkan objects created in this level
	*/
	~VulkanApp() {
		if (devices.device == VK_NULL_HANDLE) {
			return;
		}

		imgui.cleanup();

		//descriptor releated resources
		vkDestroyDescriptorPool(devices.device, descriptorPool, nullptr);
		vkDestroyDescriptorSetLayout(devices.device, descriptorSetLayout, nullptr);

		//uniform buffers
		for (size_t i = 0; i < uniformBuffers.size(); ++i) {
			devices.memoryAllocator.freeBufferMemory(uniformBuffers[i],
				VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
			vkDestroyBuffer(devices.device, uniformBuffers[i], nullptr);
		}

		//skybox textures
		skyboxTexture.cleanup();
		
		//model & skybox buffers
		devices.memoryAllocator.freeBufferMemory(modelBuffer, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
		vkDestroyBuffer(devices.device, modelBuffer, nullptr);
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
		vkDestroyRenderPass(devices.device, renderPass, nullptr);
	}

	/*
	* application initialization - also contain base class initApp()
	*/
	virtual void initApp() override {
		VulkanAppBase::initApp();

		//mesh loading & buffer creation
		model.load("../../meshes/bunny.obj");
		modelBuffer = model.createModelBuffer(&devices);

		//skybox model loading & buffer creation
		skybox.load("../../meshes/cube.obj");
		skyboxBuffer = skybox.createModelBuffer(&devices);

		//skybox texture load
		skyboxTexture.load(&devices, "../../textures/skybox");

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
		imgui.init(&devices, swapchain.extent.width, swapchain.extent.height, renderPass, MAX_FRAMES_IN_FLIGHT);
		//record command buffer
		recordCommandBuffer();
	}

private:
	/** render pass */
	VkRenderPass renderPass = VK_NULL_HANDLE;
	/** graphics pipeline */
	VkPipeline pipeline = VK_NULL_HANDLE, skyboxPipeline = VK_NULL_HANDLE;
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
	std::vector<VkBuffer> uniformBuffers;
	/**  uniform buffer memory handle */
	std::vector<MemoryAllocator::HostVisibleMemory> uniformBufferMemories;
	/** abstracted 3d mesh */
	Mesh model, skybox;
	/** model vertex & index buffer */
	VkBuffer modelBuffer;
	/** skybox vertex & index buffer */
	VkBuffer skyboxBuffer;
	/** skybox texture */
	TextureCube skyboxTexture;

	/*
	* called every frame - submit queues
	*/
	virtual void draw() override {
		uint32_t imageIndex = prepareFrame();

		//imgui demo
		if (imgui.userInput.modelRotate) {
			updateUniformBuffer(currentFrame);
		}

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
		if (imgui.sampleCountChanged) {
			imgui.sampleCountChanged = false;
			changeMultisampleResources();
		}
	}

	/*
	* create render pass
	*/
	void createRenderPass() {
		if (renderPass != VK_NULL_HANDLE) {
			vkDestroyRenderPass(devices.device, renderPass, nullptr);
		}

		bool isCurrentSampleCount1 = imgui.userInput.currentSampleCount == VK_SAMPLE_COUNT_1_BIT;

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
		attachments[1].samples = imgui.userInput.currentSampleCount;
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
			attachments[2].samples = imgui.userInput.currentSampleCount;
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
		* pipeline for main model
		*/
		auto bindingDescription = model.getBindingDescription();
		auto attributeDescription = model.getAttributeDescriptions();

		PipelineGenerator gen(devices.device);
		gen.setColorBlendInfo(VK_FALSE);
		gen.setMultisampleInfo(imgui.userInput.currentSampleCount);
		gen.addVertexInputBindingDescription({ bindingDescription });
		gen.addVertexInputAttributeDescription(attributeDescription);
		gen.addDescriptorSetLayout({ descriptorSetLayout });
		gen.addShader(
			vktools::createShaderModule(devices.device, vktools::readFile("shaders/reflection_vert.spv")),
			VK_SHADER_STAGE_VERTEX_BIT);
		gen.addShader(
			vktools::createShaderModule(devices.device, vktools::readFile("shaders/reflection_frag.spv")),
			VK_SHADER_STAGE_FRAGMENT_BIT);

		//generate pipeline layout & pipeline
		gen.generate(renderPass, &pipeline, &pipelineLayout);
		//reset shader & vertex description settings to reuse pipeline generator
		gen.resetShaderVertexDescriptions();


		/*
		* pipeline for skybox
		*/
		bindingDescription = skybox.getBindingDescription();
		attributeDescription = skybox.getAttributeDescriptions();

		gen.setMultisampleInfo(imgui.userInput.currentSampleCount, VK_TRUE, 0.2f);
		gen.setDepthStencilInfo(VK_TRUE, VK_TRUE, VK_COMPARE_OP_LESS_OR_EQUAL);
		//gen.setRasterizerInfo(VK_POLYGON_MODE_FILL, VK_CULL_MODE_FRONT_BIT);
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

			if (imgui.userInput.currentSampleCount != VK_SAMPLE_COUNT_1_BIT) {
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
		if (imgui.userInput.currentSampleCount == VK_SAMPLE_COUNT_1_BIT) {
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
			vkCmdBindPipeline(commandBuffers[i], VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline);
			size_t descriptorSetIndex = i / framebuffers.size();
			vkCmdBindDescriptorSets(commandBuffers[i], VK_PIPELINE_BIND_POINT_GRAPHICS, pipelineLayout, 0, 1,
				&descriptorSets[descriptorSetIndex], 0, nullptr);

			VkDeviceSize offsets[] = { 0 };
			vkCmdBindVertexBuffers(commandBuffers[i], 0, 1, &modelBuffer, offsets);
			VkDeviceSize indexBufferOffset = model.vertices.bufferSize; // sizeof vertex buffer
			vkCmdBindIndexBuffer(commandBuffers[i], modelBuffer, indexBufferOffset, VK_INDEX_TYPE_UINT32);
			vkCmdDrawIndexed(commandBuffers[i], static_cast<uint32_t>(model.indices.size()), 1, 0, 0, 0);

			/*
			* draw skybox
			*/
			vkCmdBindPipeline(commandBuffers[i], VK_PIPELINE_BIND_POINT_GRAPHICS, skyboxPipeline);
			vkCmdBindDescriptorSets(commandBuffers[i], VK_PIPELINE_BIND_POINT_GRAPHICS, pipelineLayout, 0, 1,
				&descriptorSets[descriptorSetIndex], 0, nullptr);

			vkCmdBindVertexBuffers(commandBuffers[i], 0, 1, &skyboxBuffer, offsets);
			indexBufferOffset = skybox.vertices.bufferSize; // sizeof vertex buffer
			vkCmdBindIndexBuffer(commandBuffers[i], skyboxBuffer, indexBufferOffset, VK_INDEX_TYPE_UINT32);
			vkCmdDrawIndexed(commandBuffers[i], static_cast<uint32_t>(skybox.indices.size()), 1, 0, 0, 0);
			
			/*
			* imgui
			*/
			imgui.drawFrame(commandBuffers[i], descriptorSetIndex);

			vkCmdEndRenderPass(commandBuffers[i]);
			VK_CHECK_RESULT(vkEndCommandBuffer(commandBuffers[i]));
		}
		LOG("built:\t\tcommand buffers");
	}


	/*
	* create MAX_FRAMES_IN_FLIGHT of ubos
	*/
	void createUniformBuffers() {
		VkDeviceSize bufferSize = sizeof(UBO);
		uniformBuffers.resize(MAX_FRAMES_IN_FLIGHT);
		uniformBufferMemories.resize(MAX_FRAMES_IN_FLIGHT);

		VkBufferCreateInfo uniformBufferCreateInfo = vktools::initializers::bufferCreateInfo(
			bufferSize, VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT);

		for (size_t i = 0; i < MAX_FRAMES_IN_FLIGHT; ++i) {
			VK_CHECK_RESULT(vkCreateBuffer(devices.device, &uniformBufferCreateInfo, nullptr, &uniformBuffers[i]));
			uniformBufferMemories[i] = devices.memoryAllocator.allocateBufferMemory(
					uniformBuffers[i], VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
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
		
		UBO ubo{};
		ubo.model = glm::translate(glm::mat4(1.f), glm::vec3(0.f, -.5f, 0.f));
		glm::vec3 camPos = glm::vec3(2.5 * std::cos(time / 5), 0, 2.5 * std::sin(time / 5));
		ubo.view = glm::lookAt(camPos, glm::vec3(0.f, 0.0f, 0.f), glm::vec3(0.f, 1.f, 0.f));
		ubo.normalMatrix = glm::transpose(glm::inverse(ubo.view * ubo.model));
		ubo.proj = glm::perspective(glm::radians(45.f),
			swapchain.extent.width / (float)swapchain.extent.height, 0.1f, 10.f);
		ubo.proj[1][1] *= -1;

		uniformBufferMemories[currentFrame].mapData(devices.device, &ubo);
	}

	/*
	* set descriptor bindings & allocate destcriptor sets
	*/
	void createDescriptorSet() {
		//descriptor - 1 uniform buffer
		bindings.addBinding(0, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1, VK_SHADER_STAGE_VERTEX_BIT);
		//descriptor - 1 image sampler (skybox)
		bindings.addBinding(1, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_FRAGMENT_BIT);
		descriptorPool = bindings.createDescriptorPool(devices.device, MAX_FRAMES_IN_FLIGHT);
		descriptorSetLayout = bindings.createDescriptorSetLayout(devices.device);
		descriptorSets = vktools::allocateDescriptorSets(devices.device, descriptorSetLayout, descriptorPool, MAX_FRAMES_IN_FLIGHT);
	}

	/*
	* update descriptor set
	*/
	void updateDescriptorSets() {

		for (size_t i = 0; i < MAX_FRAMES_IN_FLIGHT; ++i) {
			VkDescriptorBufferInfo bufferInfo{ uniformBuffers[i], 0, sizeof(UBO)};
			std::vector<VkWriteDescriptorSet> writes = {
				bindings.makeWrite(descriptorSets[i], 0, &bufferInfo),
				bindings.makeWrite(descriptorSets[i], 1, &skyboxTexture.descriptor)
			};
			vkUpdateDescriptorSets(devices.device, static_cast<uint32_t>(writes.size()), writes.data(), 0, nullptr);
		}
	}

	/*
	* change all resource related to multusampling
	*/
	void changeMultisampleResources() {
		//finish all command before destroy vk resources
		vkDeviceWaitIdle(devices.device);

		//depth stencil image
		destroyDepthStencilImage();
		createDepthStencilImage(imgui.userInput.currentSampleCount);
		//multisample color buffer
		destroyMultisampleColorBuffer();
		createMultisampleColorBuffer(imgui.userInput.currentSampleCount);

		//render pass
		createRenderPass();
		//pipeline
		createPipeline();
		//framebuffer
		createFramebuffers();

		//imgui pipeline
		imgui.createPipeline(renderPass);

		//command buffer
		resetCommandBuffer();
		recordCommandBuffer();
	}
};

//entry point
RUN_APPLICATION_MAIN(VulkanApp, 800, 600, "project1");
