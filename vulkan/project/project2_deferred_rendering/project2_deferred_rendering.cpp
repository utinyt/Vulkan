#include <array>
#include <chrono>
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
		vkDestroyDescriptorPool(devices.device, offscreenDescriptorPool, nullptr);
		vkDestroyDescriptorSetLayout(devices.device, offscreenDescriptorSetLayout, nullptr);

		//uniform buffers
		for (size_t i = 0; i < uniformBuffers.size(); ++i) {
			devices.memoryAllocator.freeBufferMemory(uniformBuffers[i],
				VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
			vkDestroyBuffer(devices.device, uniformBuffers[i], nullptr);
		}

		//model & skybox buffers
		devices.memoryAllocator.freeBufferMemory(modelBuffer, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
		vkDestroyBuffer(devices.device, modelBuffer, nullptr);

		//framebuffers
		for (auto& framebuffer : framebuffers) {
			vkDestroyFramebuffer(devices.device, framebuffer, nullptr);
		}

		//pipelines & render pass
		vkDestroyPipeline(devices.device, pipeline, nullptr);
		vkDestroyPipelineLayout(devices.device, pipelineLayout, nullptr);
		vkDestroyPipeline(devices.device, offscreenPipeline, nullptr);
		vkDestroyPipelineLayout(devices.device, offscreenPipelineLayout, nullptr);
		vkDestroyRenderPass(devices.device, renderPass, nullptr);
		vkDestroyRenderPass(devices.device, offscreenRenderPass, nullptr);
		offscreenFramebuffer.cleanup();
		vkDestroySampler(devices.device, offscreenSampler, nullptr);

		for (int i = 0; i < MAX_FRAMES_IN_FLIGHT; ++i) {
			vkDestroySemaphore(devices.device, offscreenSemaphores[i], nullptr);
		}
	}

	/*
	* application initialization - also contain base class initApp()
	*/
	virtual void initApp() override {
		//sampleCount = static_cast<VkSampleCountFlagBits>(devices.maxSampleCount);
		VulkanAppBase::initApp();

		//mesh loading & buffer creation
		model.load("../../meshes/bunny.obj");
		modelBuffer = model.createModelBuffer(&devices);

		//offscreen resources
		createOffscreenRenderPassFramebuffer();
		//offscreen semaphore
		createOffscreenSemaphores();

		//render pass
		renderPass = vktools::createRenderPass(devices.device,
			{swapchain.surfaceFormat.format},
			depthFormat,
			VK_SAMPLE_COUNT_1_BIT,
			1,
			true, true,
			VK_IMAGE_LAYOUT_UNDEFINED,
			VK_IMAGE_LAYOUT_PRESENT_SRC_KHR,
			VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT | VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT,
			VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT);
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
		//create & record offscreen command buffer
		createOffscreenCommandBuffer();
	}

private:
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
	std::vector<VkBuffer> uniformBuffers;
	/**  uniform buffer memory handle */
	std::vector<MemoryAllocator::HostVisibleMemory> uniformBufferMemories;
	/** abstracted 3d mesh */
	Mesh model, skybox;
	/** model vertex & index buffer */
	VkBuffer modelBuffer;

	/** offscreen framebuffer */
	Framebuffer offscreenFramebuffer;
	/** offscreen render pass */
	VkRenderPass offscreenRenderPass = VK_NULL_HANDLE;
	/** offscreen sampler */
	VkSampler offscreenSampler = VK_NULL_HANDLE;
	/** offscreen render semaphores */
	std::vector<VkSemaphore> offscreenSemaphores;
	/** offscreen command buffer */
	std::vector<VkCommandBuffer> offscreenCmdBuf{};
	/** offscreen pipeline */
	VkPipeline offscreenPipeline = VK_NULL_HANDLE;
	/** offscreen pipeline layout */
	VkPipelineLayout offscreenPipelineLayout = VK_NULL_HANDLE;
	/** descriptor set bindings */
	DescriptorSetBindings offscreenBindings;
	/** descriptor layout */
	VkDescriptorSetLayout offscreenDescriptorSetLayout;
	/** descriptor pool */
	VkDescriptorPool offscreenDescriptorPool;
	/** descriptor sets */
	std::vector<VkDescriptorSet> offscreenDescriptorSets;

	/*
	* called every frame - submit queues
	*/
	virtual void draw() override {
		uint32_t imageIndex = prepareFrame();

		/*
		* offscreen rendering
		*/
		VkPipelineStageFlags waitStages[] = { VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT };
		VkSubmitInfo submitInfo{};
		submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
		submitInfo.waitSemaphoreCount = 1;
		submitInfo.pWaitSemaphores = &presentCompleteSemaphores[currentFrame];
		submitInfo.pWaitDstStageMask = waitStages;
		submitInfo.commandBufferCount = 1;
		submitInfo.pCommandBuffers = &offscreenCmdBuf[currentFrame];
		submitInfo.signalSemaphoreCount = 1;
		submitInfo.pSignalSemaphores = &offscreenSemaphores[currentFrame];
		VK_CHECK_RESULT(vkQueueSubmit(devices.graphicsQueue, 1, &submitInfo, VK_NULL_HANDLE));

		/*
		* post rendering
		*/
		submitInfo.pWaitSemaphores = &offscreenSemaphores[currentFrame];
		size_t commandBufferIndex = currentFrame * framebuffers.size() + imageIndex;
		submitInfo.pCommandBuffers = &commandBuffers[commandBufferIndex];
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
		createOffscreenRenderPassFramebuffer(true); //no need to recreate renderpass
		updateDescriptorSets();
		recordCommandBuffer();
		createOffscreenCommandBuffer();
	}

	/*
	* offscreen images & render pass & framebuffer
	*/
	void createOffscreenRenderPassFramebuffer(bool createFramebufferOnly = false) {
		offscreenFramebuffer.init(&devices);
		offscreenFramebuffer.cleanup();

		VkMemoryPropertyFlagBits memProperties = VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT;

		//position
		VkImageCreateInfo attachmentInfo = vktools::initializers::imageCreateInfo(
			{ swapchain.extent.width, swapchain.extent.height, 1 },
			VK_FORMAT_R16G16B16A16_SFLOAT,
			VK_IMAGE_TILING_OPTIMAL,
			VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
			sampleCount
		);
		offscreenFramebuffer.addAttachment(attachmentInfo, memProperties);

		//normal - use same info
		offscreenFramebuffer.addAttachment(attachmentInfo, memProperties);

		//depth
		attachmentInfo.format = depthFormat;
		attachmentInfo.usage = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT;
		offscreenFramebuffer.addAttachment(attachmentInfo, memProperties);

		if (createFramebufferOnly == false) {
			//sampler
			VkSamplerCreateInfo samplerInfo =
				vktools::initializers::samplerCreateInfo(devices.availableFeatures, devices.properties, VK_FILTER_NEAREST);
			VK_CHECK_RESULT(vkCreateSampler(devices.device, &samplerInfo, nullptr, &offscreenSampler));

			//render pass
			offscreenRenderPass = offscreenFramebuffer.createRenderPass();
		}
		
		offscreenFramebuffer.createFramebuffer(swapchain.extent, offscreenRenderPass);
	}

	/*
	* create offscreen semaphores
	*/
	void createOffscreenSemaphores() {
		offscreenSemaphores.resize(MAX_FRAMES_IN_FLIGHT);
		VkSemaphoreCreateInfo info{ VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO };
		for (int i = 0; i < MAX_FRAMES_IN_FLIGHT; ++i) {
			VK_CHECK_RESULT(vkCreateSemaphore(devices.device, &info, nullptr, &offscreenSemaphores[i]));
		}
	}

	/*
	* create & record offscreen command buffer
	*/
	void createOffscreenCommandBuffer() {
		if (!offscreenCmdBuf.empty()) {
			vkFreeCommandBuffers(devices.device, devices.commandPool,
				static_cast<uint32_t>(offscreenCmdBuf.size()), offscreenCmdBuf.data());
		}

		std::vector<VkClearValue> clearValues{};
		clearValues.resize(3);
		clearValues[0].color = clearColor;
		clearValues[1].color = clearColor;
		clearValues[2].depthStencil = {1.f, 0};
		clearValues.shrink_to_fit();

		VkRenderPassBeginInfo offscreenRenderPassBeginInfo{};
		offscreenRenderPassBeginInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
		offscreenRenderPassBeginInfo.renderPass = offscreenRenderPass;
		offscreenRenderPassBeginInfo.renderArea.offset = { 0, 0 };
		offscreenRenderPassBeginInfo.renderArea.extent = swapchain.extent;
		offscreenRenderPassBeginInfo.clearValueCount = static_cast<uint32_t>(clearValues.size());
		offscreenRenderPassBeginInfo.pClearValues = clearValues.data();
		offscreenRenderPassBeginInfo.framebuffer = offscreenFramebuffer.framebuffer;

		VkCommandBufferAllocateInfo allocInfo{};
		allocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
		allocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
		allocInfo.commandPool = devices.commandPool;
		allocInfo.commandBufferCount = 2;
		offscreenCmdBuf.resize(MAX_FRAMES_IN_FLIGHT);

		VK_CHECK_RESULT(vkAllocateCommandBuffers(devices.device, &allocInfo, offscreenCmdBuf.data()));

		VkCommandBufferBeginInfo beginInfo{};
		beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;

		for (int i = 0; i < MAX_FRAMES_IN_FLIGHT; ++i) {
			VK_CHECK_RESULT(vkBeginCommandBuffer(offscreenCmdBuf[i], &beginInfo));

			vkCmdBeginRenderPass(offscreenCmdBuf[i], &offscreenRenderPassBeginInfo, VK_SUBPASS_CONTENTS_INLINE);
			vktools::setViewportScissorDynamicStates(offscreenCmdBuf[i], swapchain.extent);

			vkCmdBindPipeline(offscreenCmdBuf[i], VK_PIPELINE_BIND_POINT_GRAPHICS, offscreenPipeline);
			vkCmdBindDescriptorSets(offscreenCmdBuf[i], VK_PIPELINE_BIND_POINT_GRAPHICS, offscreenPipelineLayout, 
				0, 1, &offscreenDescriptorSets[i], 0, nullptr);

			VkDeviceSize offsets[] = { 0 };
			vkCmdBindVertexBuffers(offscreenCmdBuf[i], 0, 1, &modelBuffer, offsets);
			VkDeviceSize indexBufferOffset = model.vertices.bufferSize; // sizeof vertex buffer
			vkCmdBindIndexBuffer(offscreenCmdBuf[i], modelBuffer, indexBufferOffset, VK_INDEX_TYPE_UINT32);
			vkCmdDrawIndexed(offscreenCmdBuf[i], static_cast<uint32_t>(model.indices.size()), 1, 0, 0, 0);

			vkCmdEndRenderPass(offscreenCmdBuf[i]);
			VK_CHECK_RESULT(vkEndCommandBuffer(offscreenCmdBuf[i]));
		}
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

		/*
		* offscreen pipeline (g-buffer)
		*/
		auto bindingDescription = model.getBindingDescription();
		auto attributeDescription = model.getAttributeDescriptions();

		PipelineGenerator gen(devices.device);
		gen.setColorBlendInfo(VK_FALSE, 2);
		gen.setMultisampleInfo(sampleCount);
		gen.addVertexInputBindingDescription({ bindingDescription });
		gen.addVertexInputAttributeDescription(attributeDescription);
		gen.addDescriptorSetLayout({ offscreenDescriptorSetLayout });
		gen.addShader(
			vktools::createShaderModule(devices.device, vktools::readFile("shaders/gbuffer_vert.spv")),
			VK_SHADER_STAGE_VERTEX_BIT);
		gen.addShader(
			vktools::createShaderModule(devices.device, vktools::readFile("shaders/gbuffer_frag.spv")),
			VK_SHADER_STAGE_FRAGMENT_BIT);

		//generate pipeline layout & pipeline
		gen.generate(offscreenRenderPass, &offscreenPipeline, &offscreenPipelineLayout);
		//reset generator
		gen.resetAll();

		/*
		* full screen quad pipeline
		*/
		gen.setColorBlendInfo(VK_FALSE, 1);
		gen.setMultisampleInfo(VK_SAMPLE_COUNT_1_BIT);
		gen.setRasterizerInfo(VK_POLYGON_MODE_FILL, VK_CULL_MODE_FRONT_BIT);
		gen.addDescriptorSetLayout({ descriptorSetLayout });
		gen.addShader(
			vktools::createShaderModule(devices.device, vktools::readFile("shaders/full_quad_vert.spv")),
			VK_SHADER_STAGE_VERTEX_BIT);
		gen.addShader(
			vktools::createShaderModule(devices.device, vktools::readFile("shaders/full_quad_frag.spv")),
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
		clearValues.resize(2);
		clearValues[0].color = clearColor;
		clearValues[1].depthStencil = { 1.f, 0 };

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
			* draw full screen quad
			*/
			vkCmdBindPipeline(commandBuffers[i], VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline);
			size_t descriptorSetIndex = i / framebuffers.size();
			vkCmdBindDescriptorSets(commandBuffers[i], VK_PIPELINE_BIND_POINT_GRAPHICS, pipelineLayout, 0, 1,
				&descriptorSets[descriptorSetIndex], 0, nullptr);

			vkCmdDraw(commandBuffers[i], 3, 1, 0, 0);
			
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
		glm::vec3 camPos = glm::vec3(2.5 * std::cos(time), 0, 2.5 * std::sin(time));
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
		/*
		* offscreen descriptor
		*/
		//descriptor - camera matrices
		offscreenBindings.addBinding(0, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1, VK_SHADER_STAGE_VERTEX_BIT);
		offscreenDescriptorPool = offscreenBindings.createDescriptorPool(devices.device, MAX_FRAMES_IN_FLIGHT);
		offscreenDescriptorSetLayout = offscreenBindings.createDescriptorSetLayout(devices.device);
		offscreenDescriptorSets = vktools::allocateDescriptorSets(devices.device, offscreenDescriptorSetLayout, offscreenDescriptorPool, MAX_FRAMES_IN_FLIGHT);

		/*
		* full-screen quad descriptor
		*/
		//descriptor - 2 image samplers
		bindings.addBinding(0, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_FRAGMENT_BIT);
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
			VkDescriptorImageInfo posAttachmentInfo{offscreenSampler, 
				offscreenFramebuffer.attachments[0].imageView, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
			VkDescriptorImageInfo normalAttachmentInfo{ offscreenSampler, 
				offscreenFramebuffer.attachments[1].imageView, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL };

			std::vector<VkWriteDescriptorSet> writes = {
				offscreenBindings.makeWrite(offscreenDescriptorSets[i], 0, &bufferInfo),
				bindings.makeWrite(descriptorSets[i], 0, &posAttachmentInfo),
				bindings.makeWrite(descriptorSets[i], 1, &normalAttachmentInfo),
			};
			vkUpdateDescriptorSets(devices.device, static_cast<uint32_t>(writes.size()), writes.data(), 0, nullptr);
		}
	}
};

//entry point
RUN_APPLICATION_MAIN(VulkanApp, 1200, 800, "project2");
