#include <array>
#include <chrono>
#include "core/vulkan_app_base.h"
#include "core/vulkan_mesh.h"
#define GLM_FORCE_RADIANS
#include "glm/glm.hpp"
#include "glm/gtc/matrix_transform.hpp"
#include "core/vulkan_imgui.h"

class VulkanApp : public VulkanAppBase {
public:
	/** uniform buffer object */
	struct UBO {
		glm::mat4 model;
		glm::mat4 view;
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
		imgui.cleanup();
		vkDestroyDescriptorPool(devices.device, descriptorPool, nullptr);

		for (size_t i = 0; i < uniformBuffers.size(); ++i) {
			devices.memoryAllocator.freeBufferMemory(uniformBuffers[i],
				VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
			vkDestroyBuffer(devices.device, uniformBuffers[i], nullptr);
		}

		vkDestroyDescriptorSetLayout(devices.device, descriptorSetLayout, nullptr);
		devices.memoryAllocator.freeBufferMemory(vertexIndexBuffer, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
		vkDestroyBuffer(devices.device, vertexIndexBuffer, nullptr);

		for (auto& framebuffer : framebuffers) {
			vkDestroyFramebuffer(devices.device, framebuffer, nullptr);
		}
		vkDestroyPipeline(devices.device, pipeline, nullptr);
		vkDestroyPipelineLayout(devices.device, pipelineLayout, nullptr);
		vkDestroyRenderPass(devices.device, renderPass, nullptr);
	}

	/*
	* application initialization - also contain base class initApp()
	*/
	virtual void initApp() override {
		VulkanAppBase::initApp();

		//descriptor - 1 uniform buffer
		bindings.addBinding(0, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1, VK_SHADER_STAGE_VERTEX_BIT);
		descriptorPool = bindings.createDescriptorPool(devices.device, MAX_FRAMES_IN_FLIGHT);
		descriptorSetLayout = bindings.createDescriptorSetLayout(devices.device);
		descriptorSets = vktools::allocateDescriptorSets(devices.device, descriptorSetLayout, descriptorPool, MAX_FRAMES_IN_FLIGHT);

		//mesh loading & buffer creation
		mesh.load("../meshes/bunny.obj");
		VkDeviceSize vertexBufferSize = mesh.vertices.bufferSize;
		VkDeviceSize indexBufferSize = sizeof(mesh.indices[0]) * mesh.indices.size();
		Mesh::Buffer buffer;
		buffer.allocate(vertexBufferSize + indexBufferSize);
		buffer.push(mesh.vertices.data(), vertexBufferSize);
		buffer.push(mesh.indices.data(), indexBufferSize);
		createVertexIndexBuffer(buffer.data(), vertexBufferSize + indexBufferSize,
			VK_BUFFER_USAGE_VERTEX_BUFFER_BIT | VK_BUFFER_USAGE_INDEX_BUFFER_BIT, vertexIndexBuffer);
		
		//render pass
		renderPass = vktools::createRenderPass(devices.device, { swapchain.surfaceFormat.format }, depthFormat);
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
	VkRenderPass renderPass;
	/** graphics pipeline */
	VkPipeline pipeline;
	/** pipeline layout */
	VkPipelineLayout pipelineLayout;
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

	/** vertex buffer handle */
	VkBuffer vertexIndexBuffer;
	/** uniform buffer handle */
	std::vector<VkBuffer> uniformBuffers;
	/**  uniform buffer memory handle */
	std::vector<MemoryAllocator::HostVisibleMemory> uniformBufferMemories;
	/** abstracted 3d mesh */
	Mesh mesh;

	/*
	* called every frame - submit queues
	*/
	virtual void draw() override {
		uint32_t imageIndex = prepareFrame();

		updateUniformBuffer(currentFrame);

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
	* create graphics pipeline
	*/
	void createPipeline() {
		auto bindingDescription = mesh.getBindingDescription();
		auto attributeDescription = mesh.getAttributeDescriptions();
		
		VkPipelineVertexInputStateCreateInfo vertexInputInfo
			= vktools::initializers::pipelineVertexInputStateCreateInfo(&bindingDescription, 1,
				attributeDescription.data(), static_cast<uint32_t>(attributeDescription.size()));

		VkPipelineInputAssemblyStateCreateInfo inputAssemblyInfo =
			vktools::initializers::pipelineInputAssemblyStateCreateInfo();

		VkPipelineViewportStateCreateInfo viewportStateInfo =
			vktools::initializers::pipelineViewportStateCreateInfo();

		VkDynamicState dynamicStates[] = { VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR };
		VkPipelineDynamicStateCreateInfo dynamicStateInfo =
			vktools::initializers::pipelineDynamicStateCreateInfo(dynamicStates, 2);

		VkPipelineRasterizationStateCreateInfo rasterizationInfo =
			vktools::initializers::pipelineRasterizationStateCreateInfo(VK_POLYGON_MODE_FILL, VK_CULL_MODE_BACK_BIT);

		VkPipelineMultisampleStateCreateInfo multisampleStateInfo =
			vktools::initializers::pipelineMultisampleStateCreateInfo();

		VkPipelineDepthStencilStateCreateInfo depthStencilStateInfo =
			vktools::initializers::pipelineDepthStencilStateCreateInfo(VK_TRUE, VK_TRUE, VK_COMPARE_OP_LESS);

		VkPipelineColorBlendAttachmentState blendAttachmentState =
			vktools::initializers::pipelineColorBlendAttachment(VK_FALSE);

		VkPipelineColorBlendStateCreateInfo colorBlendInfo =
			vktools::initializers::pipelineColorBlendStateCreateInfo(1, &blendAttachmentState);

		VkPipelineLayoutCreateInfo pipelineLayoutInfo =
			vktools::initializers::pipelineLayoutCreateInfo(1, &descriptorSetLayout);
		VK_CHECK_RESULT(vkCreatePipelineLayout(devices.device, &pipelineLayoutInfo, nullptr, &pipelineLayout));

		//shader
		VkShaderModule vertexModule = vktools::createShaderModule(devices.device, vktools::readFile("shaders/phong_vert.spv"));
		VkShaderModule fragmentModule = vktools::createShaderModule(devices.device, vktools::readFile("shaders/phong_frag.spv"));

		VkPipelineShaderStageCreateInfo vertShaderStageInfo 
			= vktools::initializers::pipelineShaderStageCreateInfo(VK_SHADER_STAGE_VERTEX_BIT, vertexModule);

		VkPipelineShaderStageCreateInfo fragShaderStageInfo = 
			vktools::initializers::pipelineShaderStageCreateInfo(VK_SHADER_STAGE_FRAGMENT_BIT, fragmentModule);

		VkPipelineShaderStageCreateInfo shaderStages[] = { vertShaderStageInfo , fragShaderStageInfo };

		//pipeline
		VkGraphicsPipelineCreateInfo pipelineInfo{};
		pipelineInfo.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
		pipelineInfo.stageCount = 2;
		pipelineInfo.pStages = shaderStages;
		pipelineInfo.pVertexInputState = &vertexInputInfo;
		pipelineInfo.pInputAssemblyState = &inputAssemblyInfo;
		pipelineInfo.pViewportState = &viewportStateInfo;
		pipelineInfo.pRasterizationState = &rasterizationInfo;
		pipelineInfo.pMultisampleState = &multisampleStateInfo;
		pipelineInfo.pDepthStencilState = &depthStencilStateInfo;
		pipelineInfo.pColorBlendState = &colorBlendInfo;
		pipelineInfo.pDynamicState = &dynamicStateInfo;
		pipelineInfo.layout = pipelineLayout;
		pipelineInfo.renderPass = renderPass;
		pipelineInfo.subpass = 0;
		VK_CHECK_RESULT(vkCreateGraphicsPipelines(devices.device, pipelineCache, 1, &pipelineInfo, nullptr, &pipeline));
		LOG("created:\tgraphics pipeline");
		
		vkDestroyShaderModule(devices.device, vertexModule, nullptr);
		vkDestroyShaderModule(devices.device, fragmentModule, nullptr);
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
			std::array<VkImageView, 2> attachments = {
				swapchain.imageViews[i],
				depthImageView
			};

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

	/** @brief build buffer - used to create vertex / index buffer */
	/*
	* build buffer - used to create vertex / index buffer
	*
	* @param bufferData - data to transfer
	* @param bufferSize
	* @param usage - buffer usage (vertex / index bit)
	* @param buffer - buffer handle
	* @param bufferMemory - buffer memory handle
	*/
	void createVertexIndexBuffer(const void* bufferData, VkDeviceSize bufferSize, VkBufferUsageFlags usage,
		VkBuffer& buffer) {
		//create staging buffer
		VkBuffer stagingBuffer;
		VkBufferCreateInfo stagingBufferCreateInfo = vktools::initializers::bufferCreateInfo(
			bufferSize, VK_BUFFER_USAGE_TRANSFER_SRC_BIT);
		VK_CHECK_RESULT(vkCreateBuffer(devices.device, &stagingBufferCreateInfo, nullptr, &stagingBuffer));

		//suballocate
		VkMemoryPropertyFlags properties = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;
		MemoryAllocator::HostVisibleMemory hostVisibleMemory = devices.memoryAllocator.allocateBufferMemory(
			stagingBuffer, properties);

		hostVisibleMemory.mapData(devices.device, bufferData);

		//create vertex & index buffer
		VkBufferCreateInfo bufferCreateInfo = vktools::initializers::bufferCreateInfo(
			bufferSize, VK_BUFFER_USAGE_TRANSFER_DST_BIT | usage);
		VK_CHECK_RESULT(vkCreateBuffer(devices.device, &bufferCreateInfo, nullptr, &buffer));

		//suballocation
		devices.memoryAllocator.allocateBufferMemory(buffer, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);

		//host visible -> device local
		devices.copyBuffer(devices.commandPool, stagingBuffer, buffer, bufferSize);

		devices.memoryAllocator.freeBufferMemory(stagingBuffer, properties);
		vkDestroyBuffer(devices.device, stagingBuffer, nullptr);
	}

	/*
	* record drawing commands to command buffers
	*/
	virtual void recordCommandBuffer() override {
		VkCommandBufferBeginInfo cmdBufBeginInfo{};
		cmdBufBeginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;

		std::array<VkClearValue, 2> clearValues{};
		clearValues[0].color = { 0.f, 0.2f, 0.f, 1.f };
		clearValues[1].depthStencil = { 1.f, 0 };

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
			VkDeviceSize offsets[] = { 0 };
			vkCmdBindVertexBuffers(commandBuffers[i], 0, 1, &vertexIndexBuffer, offsets);
			VkDeviceSize indexBufferOffset = mesh.vertices.bufferSize; // sizeof vertex buffer
			vkCmdBindIndexBuffer(commandBuffers[i], vertexIndexBuffer, indexBufferOffset, VK_INDEX_TYPE_UINT32);
			size_t descriptorSetIndex = i / framebuffers.size();
			vkCmdBindDescriptorSets(commandBuffers[i], VK_PIPELINE_BIND_POINT_GRAPHICS, pipelineLayout, 0, 1,
				&descriptorSets[descriptorSetIndex], 0, nullptr);

			vkCmdDrawIndexed(commandBuffers[i], static_cast<uint32_t>(mesh.indices.size()), 1, 0, 0, 0);
			
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
		ubo.model = glm::rotate(glm::mat4(1.f), time * glm::radians(90.f), glm::vec3(0.f, 1.f, 0.f));
		ubo.view = glm::lookAt(glm::vec3(0.f, 0.f, 4.f), glm::vec3(0.f, 0.5f, 0.f), glm::vec3(0.f, 1.f, 0.f));
		ubo.proj = glm::perspective(glm::radians(45.f),
			swapchain.extent.width / (float)swapchain.extent.height, 0.1f, 10.f);
		ubo.proj[1][1] *= -1;

		uniformBufferMemories[currentFrame].mapData(devices.device, &ubo);
	}

	/*
	* update descriptor set
	*/
	void updateDescriptorSets() {
		for (size_t i = 0; i < MAX_FRAMES_IN_FLIGHT; ++i) {
			VkDescriptorBufferInfo bufferInfo{ uniformBuffers[i], 0, sizeof(UBO)};
			VkWriteDescriptorSet write = bindings.makeWrite(descriptorSets[i], 0, &bufferInfo);
			vkUpdateDescriptorSets(devices.device, 1, &write, 0, nullptr);
		}
	}
};

//entry point
RUN_APPLICATION_MAIN(VulkanApp, 800, 600, "Project");
