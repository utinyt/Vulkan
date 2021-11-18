#include <array>
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

		ImGui::Text("Render Mode");
		ImGui::RadioButton("Lighting", &userInput.renderMode, 0); ImGui::SameLine();
		ImGui::RadioButton("Position", &userInput.renderMode, 1); ImGui::SameLine();
		ImGui::RadioButton("Normal", &userInput.renderMode, 2); ImGui::SameLine();
		ImGui::RadioButton("SSAO", &userInput.renderMode, 3); ImGui::SameLine();
		ImGui::RadioButton("Edge", &userInput.renderMode, 4);

		if (userInput.renderMode == 0) {
			ImGui::NewLine();
			ImGui::Checkbox("Enable SSAO", &userInput.enableSSAO);
		}

		//edge detection threshold
		if (userInput.renderMode == 0 || userInput.renderMode == 4) {
			ImGui::Text("Edge detection threshold");
			ImGui::SliderFloat("Threshold", &userInput.threshold, 0.0f, 1.0f);
		}

		ImGui::End();
		ImGui::Render();
	}

	/* user input collection */
	struct UserInput {
		int renderMode = 0;
		float threshold = 0.5f;
		bool enableSSAO = false;
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
		MAX_FRAMES_IN_FLIGHT = 2;
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
		vkDestroyDescriptorPool(devices.device, ssaoDescriptorPool, nullptr);
		vkDestroyDescriptorSetLayout(devices.device, ssaoDescriptorSetLayout, nullptr);
		vkDestroyDescriptorPool(devices.device, ssaoBlurDescriptorPool, nullptr);
		vkDestroyDescriptorSetLayout(devices.device, ssaoBlurDescriptorSetLayout, nullptr);

		//uniform buffers
		for (size_t i = 0; i < cameraUBO.size(); ++i) {
			devices.memoryAllocator.freeBufferMemory(cameraUBO[i],
				VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
			vkDestroyBuffer(devices.device, cameraUBO[i], nullptr);
			devices.memoryAllocator.freeBufferMemory(deferredUBO[i],
				VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
			vkDestroyBuffer(devices.device, deferredUBO[i], nullptr);
		}

		//ssao resources
		devices.memoryAllocator.freeBufferMemory(ssaoKernelUBO,
			VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
		vkDestroyBuffer(devices.device, ssaoKernelUBO, nullptr);
		ssaoNoiseTex.cleanup();

		//skybox textures
		skyboxTexture.cleanup();

		//model & floor buffer & skybox buffers
		devices.memoryAllocator.freeBufferMemory(modelBuffer, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
		vkDestroyBuffer(devices.device, modelBuffer, nullptr);
		devices.memoryAllocator.freeBufferMemory(floorBuffer, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
		vkDestroyBuffer(devices.device, floorBuffer, nullptr);
		devices.memoryAllocator.freeBufferMemory(skyboxBuffer, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
		vkDestroyBuffer(devices.device, skyboxBuffer, nullptr);

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
		vkDestroyPipeline(devices.device, offscreenPipeline, nullptr);
		vkDestroyPipelineLayout(devices.device, offscreenPipelineLayout, nullptr);
		vkDestroyPipeline(devices.device, ssaoPipeline, nullptr);
		vkDestroyPipelineLayout(devices.device, ssaoPipelineLayout, nullptr);
		vkDestroyPipeline(devices.device, ssaoBlurPipeline, nullptr);
		vkDestroyPipelineLayout(devices.device, ssaoBlurPipelineLayout, nullptr);
		vkDestroyPipeline(devices.device, skyboxPipeline, nullptr);
		vkDestroyPipeline(devices.device, msaaPipeline, nullptr);
		vkDestroyRenderPass(devices.device, renderPass, nullptr);
		vkDestroyRenderPass(devices.device, offscreenRenderPass, nullptr);
		vkDestroySampler(devices.device, offscreenSampler, nullptr);
		vkDestroyRenderPass(devices.device, ssaoRenderPass, nullptr);
		vkDestroyRenderPass(devices.device, ssaoBlurRenderPass, nullptr);

		for (int i = 0; i < MAX_FRAMES_IN_FLIGHT; ++i) {
			offscreenFramebuffers[i].cleanup();
			ssaoFramebuffers[i].cleanup();
			ssaoBlurFramebuffers[i].cleanup();
		}
	}

	/*
	* application initialization - also contain base class initApp()
	*/
	virtual void initApp() override {
		sampleCount = VK_SAMPLE_COUNT_1_BIT;
		VulkanAppBase::initApp();
		sampleCount = static_cast<VkSampleCountFlagBits>(devices.maxSampleCount);

		//init cap setting
		camera.camPos = glm::vec3(5.f, 5.f, 20.f);
		camera.camFront = -camera.camPos;
		camera.camUp = glm::vec3(0.f, 1.f, 0.f);

		//mesh loading & buffer creation
		model.load("../../meshes/bunny.obj");
		modelBuffer = model.createModelBuffer(&devices);
		floor.load("../../meshes/cube.obj");
		floorBuffer = floor.createModelBuffer(&devices);

		//skybox model loading & buffer creation
		skybox.load("../../meshes/cube.obj");
		skyboxBuffer = skybox.createModelBuffer(&devices);

		//skybox texture load
		skyboxTexture.load(&devices, "../../textures/skybox", VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE);

		//ssao sample kernel uniform & noise images
		createSSAOResources();
		//ssao render pass & framebuffer
		createSSAORenderPassFramebuffer();

		//instance possition buffer
		createInstancePositionBuffer();

		//offscreen resources
		createOffscreenRenderPassFramebuffer();

		//render pass
		renderPass = vktools::createRenderPass(devices.device,
			{ swapchain.surfaceFormat.format },
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
			renderPass, MAX_FRAMES_IN_FLIGHT, VK_SAMPLE_COUNT_1_BIT);
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
	struct UBODeferredRending {
		Light lights[LIGHT_NUM];
		int renderMode = 0; // 0 - deferred lighting, 1 - position, 2 - normal
		int sampleCount = 1;
		float threshold = 0.5f;
		bool enableSSAO = false;
	} uboDeferredRendering;

	/** random float generator */
	std::random_device rd;
	std::mt19937 mt;

	/** render pass */
	VkRenderPass renderPass = VK_NULL_HANDLE;
	/** graphics pipeline */
	VkPipeline pipeline = VK_NULL_HANDLE, skyboxPipeline = VK_NULL_HANDLE, msaaPipeline = VK_NULL_HANDLE;
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
	VkClearColorValue clearColor{ 0.f, 0.2f, 0.f, 1.f };

	/** uniform buffer handle */
	std::vector<VkBuffer> cameraUBO;
	/**  uniform buffer memory handle */
	std::vector<MemoryAllocator::HostVisibleMemory> cameraUBOMemories;
	/** uniform buffer handle */
	std::vector<VkBuffer> deferredUBO;
	/** uniform buffer memory handle */
	std::vector<MemoryAllocator::HostVisibleMemory> deferredUBOMemories;
	/** ssao sample kernel */
	VkBuffer ssaoKernelUBO;
	/** ssao sample kernel memory handle */
	MemoryAllocator::HostVisibleMemory ssaoKernelUBOMemory;
	/** ssao noise texture */
	Texture2D ssaoNoiseTex;

	/** abstracted 3d mesh */
	Mesh model, floor, skybox;
	/** model vertex & index buffer */
	VkBuffer modelBuffer, floorBuffer, skyboxBuffer;
	/** skybox texture */
	TextureCube skyboxTexture;

	/*
	* offscreen resources
	*/
	/** offscreen framebuffer */
	std::vector<Framebuffer> offscreenFramebuffers;
	/** offscreen render pass */
	VkRenderPass offscreenRenderPass = VK_NULL_HANDLE;
	/** offscreen sampler */
	VkSampler offscreenSampler = VK_NULL_HANDLE;
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
	* ssao resources
	*/
	/** ssao framebuffer - one channel attachment */
	std::vector<Framebuffer> ssaoFramebuffers, ssaoBlurFramebuffers;
	/** ssao render pass */
	VkRenderPass ssaoRenderPass = VK_NULL_HANDLE, ssaoBlurRenderPass = VK_NULL_HANDLE;
	/** ssao pipeline - reference gbuffer */
	VkPipeline ssaoPipeline = VK_NULL_HANDLE, ssaoBlurPipeline = VK_NULL_HANDLE;
	/** ssao pipeline layout */
	VkPipelineLayout ssaoPipelineLayout = VK_NULL_HANDLE, ssaoBlurPipelineLayout = VK_NULL_HANDLE;
	/** ssao descriptor bindings */
	DescriptorSetBindings ssaoBindings, ssaoBlurBindings;
	/** ssao descriptor set layout */
	VkDescriptorSetLayout ssaoDescriptorSetLayout = VK_NULL_HANDLE, ssaoBlurDescriptorSetLayout = VK_NULL_HANDLE;
	/** ssao descriptor pool */
	VkDescriptorPool ssaoDescriptorPool = VK_NULL_HANDLE, ssaoBlurDescriptorPool = VK_NULL_HANDLE;
	/** ssao descriptor sets */
	std::vector<VkDescriptorSet> ssaoDescriptorSets, ssaoBlurDescriptorSets;

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
		sampleCount = VK_SAMPLE_COUNT_1_BIT;
		VulkanAppBase::resizeWindow(false);
		sampleCount = static_cast<VkSampleCountFlagBits>(devices.maxSampleCount);

		createSSAORenderPassFramebuffer(true);
		createOffscreenRenderPassFramebuffer(true); //no need to recreate renderpass
		updateDescriptorSets();
		recordCommandBuffer();
	}

	/*
	* create ssao related resources
	*/
	void createSSAOResources() {
		//sample kernel
		std::vector<glm::vec4> sampleKernel;
		for (int i = 0; i < 64; ++i) {
			glm::vec3 sample(
				rdFloat(RNGen) * 2.f - 1.f,
				rdFloat(RNGen) * 2.f - 1.f,
				rdFloat(RNGen)); // hemisphere
			sample = glm::normalize(sample);
			sample *= rdFloat(RNGen);

			//distribute kernel samples closer to the origin
			float scale = (float)i / 64.f;
			scale = 0.1f + scale * scale * (0.9f); //lerp(0.1f, 1.f, scale * scale)
			sample *= scale;
			sampleKernel.push_back(glm::vec4(sample, 0.f));
		}

		//create sample kernel uniform buffer
		VkDeviceSize kernelSize = static_cast<VkDeviceSize>(sizeof(sampleKernel[0]) * sampleKernel.size());
		ssaoKernelUBOMemory = devices.createBuffer(ssaoKernelUBO, kernelSize, VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
			VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
		ssaoKernelUBOMemory.mapData(devices.device, sampleKernel.data());

		//ssao noise
		std::vector<glm::vec4> ssaoNoise;
		for (size_t i = 0; i < 16; ++i) {
			ssaoNoise.push_back(glm::vec4(rdFloat(RNGen) * 2.f - 1.f, rdFloat(RNGen) * 2.f - 1.f, 0.f, 0.f));
		}

		VkDeviceSize noiseTexSize = static_cast<VkDeviceSize>(sizeof(ssaoNoise[0]) * ssaoNoise.size());
		ssaoNoiseTex.load(&devices, reinterpret_cast<unsigned char*>(ssaoNoise.data()),
			4, 4, noiseTexSize, VK_FORMAT_R32G32B32A32_SFLOAT, VK_FILTER_NEAREST, VK_SAMPLER_ADDRESS_MODE_REPEAT);
	}

	/*
	* create ssao framebuffer & renderpass
	*/
	void createSSAORenderPassFramebuffer(bool createFramebufferOnly = false) {
		ssaoFramebuffers.resize(MAX_FRAMES_IN_FLIGHT);
		ssaoBlurFramebuffers.resize(MAX_FRAMES_IN_FLIGHT);
		for (int i = 0; i < MAX_FRAMES_IN_FLIGHT; ++i) {
			ssaoFramebuffers[i].init(&devices);
			ssaoFramebuffers[i].cleanup();
			ssaoBlurFramebuffers[i].init(&devices);
			ssaoBlurFramebuffers[i].cleanup();

			//create ssao framebuffer
			VkImageCreateInfo ssaoBufferInfo =
				vktools::initializers::imageCreateInfo(
					{ swapchain.extent.width, swapchain.extent.height, 1 },
					VK_FORMAT_R8_UNORM,
					VK_IMAGE_TILING_OPTIMAL,
					VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
					sampleCount);

			ssaoFramebuffers[i].addAttachment(ssaoBufferInfo, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
			ssaoBlurFramebuffers[i].addAttachment(ssaoBufferInfo, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
		}
		
		VkSubpassDependency ssaoStart{};
		ssaoStart.srcSubpass = VK_SUBPASS_EXTERNAL;
		ssaoStart.dstSubpass = 0;
		ssaoStart.srcStageMask = VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT;
		ssaoStart.srcAccessMask = VK_ACCESS_MEMORY_READ_BIT;
		ssaoStart.dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
		ssaoStart.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
		ssaoStart.dependencyFlags = VK_DEPENDENCY_BY_REGION_BIT;

		VkSubpassDependency ssaoEnd{};
		ssaoEnd.srcSubpass = 0;
		ssaoEnd.dstSubpass = VK_SUBPASS_EXTERNAL;
		ssaoEnd.srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
		ssaoEnd.srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
		ssaoEnd.dstStageMask = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
		ssaoEnd.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
		ssaoEnd.dependencyFlags = VK_DEPENDENCY_BY_REGION_BIT;

		VkSubpassDependency ssaoBlurStart{};
		ssaoBlurStart.srcSubpass = VK_SUBPASS_EXTERNAL;
		ssaoBlurStart.dstSubpass = 0;
		ssaoBlurStart.srcStageMask = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
		ssaoBlurStart.srcAccessMask = VK_ACCESS_SHADER_READ_BIT;
		ssaoBlurStart.dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
		ssaoBlurStart.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;

		//render pass
		if (createFramebufferOnly == false) {
			ssaoRenderPass = ssaoFramebuffers[0].createRenderPass({ssaoStart, ssaoEnd});
			ssaoBlurRenderPass = ssaoBlurFramebuffers[0].createRenderPass({ssaoBlurStart, ssaoEnd});
		}

		for (int i = 0; i < MAX_FRAMES_IN_FLIGHT; ++i) {
			ssaoFramebuffers[i].createFramebuffer(swapchain.extent, ssaoRenderPass);
			ssaoBlurFramebuffers[i].createFramebuffer(swapchain.extent, ssaoRenderPass);
		}
	}

	/*
	* create a buffer for instanced model positions
	*/
	void createInstancePositionBuffer() {
		instancedTransformation.push_back({ glm::vec3(0.f, 0.f, 0.f), glm::vec3(50.f, 1.f, 50.f) }); //floor
		int start = -INSTANCE_NUM_SQRT / 2;
		for (int col = start; col < -start; ++col) {
			for (int row = start; row < -start; ++row) {
				instancedTransformation.push_back({
					glm::vec3(col * 1.5, 0.5f, row * 1.5),
					glm::vec3(1.f, 1.f, 1.f)
					});
			}
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
	* offscreen images & render pass & framebuffer
	*/
	void createOffscreenRenderPassFramebuffer(bool createFramebufferOnly = false) {
		offscreenFramebuffers.resize(MAX_FRAMES_IN_FLIGHT);
		for (int i = 0; i < MAX_FRAMES_IN_FLIGHT; ++i) {
			offscreenFramebuffers[i].init(&devices);
			offscreenFramebuffers[i].cleanup();

			VkMemoryPropertyFlagBits memProperties = VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT;

			//position
			VkImageCreateInfo attachmentInfo = vktools::initializers::imageCreateInfo(
				{ swapchain.extent.width, swapchain.extent.height, 1 },
				VK_FORMAT_R16G16B16A16_SFLOAT,
				VK_IMAGE_TILING_OPTIMAL,
				VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
				sampleCount
			);
			//TODO: reconstruct position data from depth value
			offscreenFramebuffers[i].addAttachment(attachmentInfo, memProperties);

			//normal - use same info
			offscreenFramebuffers[i].addAttachment(attachmentInfo, memProperties);

			//depth
			attachmentInfo.format = depthFormat;
			attachmentInfo.usage = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT;
			offscreenFramebuffers[i].addAttachment(attachmentInfo, memProperties);
		}

		std::vector<VkSubpassDependency> dependencies{};
		dependencies.resize(2);
		dependencies[0].srcSubpass = VK_SUBPASS_EXTERNAL;
		dependencies[0].dstSubpass = 0;
		dependencies[0].srcStageMask = VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT;
		dependencies[0].srcAccessMask = VK_ACCESS_MEMORY_READ_BIT;
		dependencies[0].dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT | VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT;
		dependencies[0].dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
		dependencies[0].dependencyFlags = VK_DEPENDENCY_BY_REGION_BIT;

		dependencies[1].srcSubpass = 0;
		dependencies[1].dstSubpass = VK_SUBPASS_EXTERNAL;
		dependencies[1].srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT | VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT;
		dependencies[1].srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
		dependencies[1].dstStageMask = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
		dependencies[1].dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
		dependencies[1].dependencyFlags = VK_DEPENDENCY_BY_REGION_BIT;

		if (createFramebufferOnly == false) {
			//sampler
			VkSamplerCreateInfo samplerInfo =
				vktools::initializers::samplerCreateInfo(devices.availableFeatures, devices.properties, VK_FILTER_NEAREST);
			VK_CHECK_RESULT(vkCreateSampler(devices.device, &samplerInfo, nullptr, &offscreenSampler));

			//render pass
			offscreenRenderPass = offscreenFramebuffers[0].createRenderPass(dependencies);
		}

		for (int i = 0; i < MAX_FRAMES_IN_FLIGHT; ++i) {
			offscreenFramebuffers[i].createFramebuffer(swapchain.extent, offscreenRenderPass);
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
		//model descriptions
		auto bindingDescription = model.getBindingDescription();
		auto attributeDescription = model.getAttributeDescriptions();
		//instanced position descriptions
		VkVertexInputBindingDescription instancedPosBindingDesc{ 1, sizeof(Transformation), VK_VERTEX_INPUT_RATE_INSTANCE };
		attributeDescription.push_back({ 2, 1, VK_FORMAT_R32G32B32_SFLOAT, 0 });
		attributeDescription.push_back({ 3, 1, VK_FORMAT_R32G32B32_SFLOAT, sizeof(glm::vec3) });

		PipelineGenerator gen(devices.device);
		gen.setColorBlendInfo(VK_FALSE, 2);
		gen.setMultisampleInfo(sampleCount);
		gen.addVertexInputBindingDescription({ bindingDescription, instancedPosBindingDesc });
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
		* ssao pipeline
		*/
		gen.setColorBlendInfo(VK_FALSE, 1);
		gen.setMultisampleInfo(sampleCount);
		gen.setRasterizerInfo(VK_POLYGON_MODE_FILL, VK_CULL_MODE_FRONT_BIT);
		gen.addDescriptorSetLayout({ ssaoDescriptorSetLayout });
		gen.addShader(
			vktools::createShaderModule(devices.device, vktools::readFile("shaders/full_quad_vert.spv")),
			VK_SHADER_STAGE_VERTEX_BIT);
		gen.addShader(
			vktools::createShaderModule(devices.device, vktools::readFile("shaders/ssao_frag.spv")),
			VK_SHADER_STAGE_FRAGMENT_BIT);
		//generate pipeline layout & pipeline
		gen.generate(ssaoRenderPass, &ssaoPipeline, &ssaoPipelineLayout);
		//reset generator
		gen.resetAll();

		/*
		* ssao blur pipeline
		*/
		gen.setColorBlendInfo(VK_FALSE, 1);
		gen.setMultisampleInfo(sampleCount);
		gen.setRasterizerInfo(VK_POLYGON_MODE_FILL, VK_CULL_MODE_FRONT_BIT);
		gen.addDescriptorSetLayout({ ssaoBlurDescriptorSetLayout });
		gen.addShader(
			vktools::createShaderModule(devices.device, vktools::readFile("shaders/full_quad_vert.spv")),
			VK_SHADER_STAGE_VERTEX_BIT);
		gen.addShader(
			vktools::createShaderModule(devices.device, vktools::readFile("shaders/ssao_blur_frag.spv")),
			VK_SHADER_STAGE_FRAGMENT_BIT);
		//generate pipeline layout & pipeline
		gen.generate(ssaoBlurRenderPass, &ssaoBlurPipeline, &ssaoBlurPipelineLayout); //reuse ssaoRenderPass
		gen.resetAll();

		/*
		* full screen quad pipeline for normal pixel & fill stencil buffer
		*/
		gen.setColorBlendInfo(VK_TRUE, 1);
		gen.setMultisampleInfo(VK_SAMPLE_COUNT_1_BIT);
		gen.setRasterizerInfo(VK_POLYGON_MODE_FILL, VK_CULL_MODE_NONE);
		VkPipelineDepthStencilStateCreateInfo& depthStencilInfo = gen.getPipelineDepthStencilStateCreateInfo();
		depthStencilInfo.stencilTestEnable = VK_TRUE;
		depthStencilInfo.back.compareOp = VK_COMPARE_OP_ALWAYS;
		depthStencilInfo.back.failOp = VK_STENCIL_OP_REPLACE;
		depthStencilInfo.back.depthFailOp = VK_STENCIL_OP_REPLACE;
		depthStencilInfo.back.passOp = VK_STENCIL_OP_REPLACE;
		depthStencilInfo.back.compareMask = 0xFF;
		depthStencilInfo.back.writeMask = 0xFF;
		depthStencilInfo.back.reference = 1;
		depthStencilInfo.front = depthStencilInfo.back;
		gen.addDescriptorSetLayout({ descriptorSetLayout });
		gen.addShader(
			vktools::createShaderModule(devices.device, vktools::readFile("shaders/full_quad_vert.spv")),
			VK_SHADER_STAGE_VERTEX_BIT);
		gen.addShader(
			vktools::createShaderModule(devices.device, vktools::readFile("shaders/full_quad_normal_frag.spv")),
			VK_SHADER_STAGE_FRAGMENT_BIT);

		//generate pipeline layout & pipeline
		gen.generate(renderPass, &pipeline, &pipelineLayout);
		gen.resetShaderVertexDescriptions();

		/*
		* use stencil buffer to only draw complex pixels
		*/
		depthStencilInfo = gen.getPipelineDepthStencilStateCreateInfo();
		depthStencilInfo.back.compareOp = VK_COMPARE_OP_NOT_EQUAL;
		depthStencilInfo.back.failOp = VK_STENCIL_OP_KEEP;
		depthStencilInfo.back.depthFailOp = VK_STENCIL_OP_KEEP;
		depthStencilInfo.back.passOp = VK_STENCIL_OP_REPLACE;
		depthStencilInfo.front = depthStencilInfo.back;
		depthStencilInfo.depthTestEnable = VK_FALSE;
		gen.addShader(
			vktools::createShaderModule(devices.device, vktools::readFile("shaders/full_quad_vert.spv")),
			VK_SHADER_STAGE_VERTEX_BIT);
		gen.addShader(
			vktools::createShaderModule(devices.device, vktools::readFile("shaders/full_quad_complex_frag.spv")),
			VK_SHADER_STAGE_FRAGMENT_BIT);
		gen.generate(renderPass, &msaaPipeline, &pipelineLayout);

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

		//ssao clear value
		std::vector<VkClearValue> ssaoClearValues{};
		ssaoClearValues.resize(1);
		ssaoClearValues[0].color = clearColor;

		VkRenderPassBeginInfo renderPassBeginInfo{};
		renderPassBeginInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
		renderPassBeginInfo.renderPass = renderPass;
		renderPassBeginInfo.renderArea.offset = { 0, 0 };
		renderPassBeginInfo.renderArea.extent = swapchain.extent;
		renderPassBeginInfo.clearValueCount = static_cast<uint32_t>(clearValues.size());
		renderPassBeginInfo.pClearValues = clearValues.data();

		std::vector<VkClearValue> gbufferClearValues{};
		gbufferClearValues.resize(3);
		gbufferClearValues[0].color = VkClearColorValue{ 0.f, 0.0f, 0.f, 0.f };
		gbufferClearValues[1].color = VkClearColorValue{ 0.f, 0.0f, 0.f, 0.f };
		gbufferClearValues[2].depthStencil = { 1.f, 0 };
		gbufferClearValues.shrink_to_fit();

		VkRenderPassBeginInfo gbufferRenderPassBeginInfo{};
		gbufferRenderPassBeginInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
		gbufferRenderPassBeginInfo.renderPass = offscreenRenderPass;
		gbufferRenderPassBeginInfo.renderArea.offset = { 0, 0 };
		gbufferRenderPassBeginInfo.renderArea.extent = swapchain.extent;
		gbufferRenderPassBeginInfo.clearValueCount = static_cast<uint32_t>(gbufferClearValues.size());
		gbufferRenderPassBeginInfo.pClearValues = gbufferClearValues.data();

		for (size_t i = 0; i < framebuffers.size() * MAX_FRAMES_IN_FLIGHT; ++i) {
			VK_CHECK_RESULT(vkBeginCommandBuffer(commandBuffers[i], &cmdBufBeginInfo));
			size_t resourceIndex = i / framebuffers.size();
			/*
			* gbuffer
			*/
			gbufferRenderPassBeginInfo.framebuffer = offscreenFramebuffers[resourceIndex].framebuffer;
			vkCmdBeginRenderPass(commandBuffers[i], &gbufferRenderPassBeginInfo, VK_SUBPASS_CONTENTS_INLINE);
			vktools::setViewportScissorDynamicStates(commandBuffers[i], swapchain.extent);

			vkCmdBindPipeline(commandBuffers[i], VK_PIPELINE_BIND_POINT_GRAPHICS, offscreenPipeline);
			vkCmdBindDescriptorSets(commandBuffers[i], VK_PIPELINE_BIND_POINT_GRAPHICS, offscreenPipelineLayout,
				0, 1, &offscreenDescriptorSets[resourceIndex], 0, nullptr);

			//floor
			VkDeviceSize offsets[] = { 0 };
			vkCmdBindVertexBuffers(commandBuffers[i], 0, 1, &floorBuffer, offsets);
			vkCmdBindVertexBuffers(commandBuffers[i], 1, 1, &instancedTransformationBuffer, offsets);
			VkDeviceSize indexBufferOffset = floor.vertices.bufferSize; // sizeof vertex buffer
			vkCmdBindIndexBuffer(commandBuffers[i], floorBuffer, indexBufferOffset, VK_INDEX_TYPE_UINT32);
			vkCmdDrawIndexed(commandBuffers[i], static_cast<uint32_t>(floor.indices.size()), 1, 0, 0, 0);

			//model
			vkCmdBindVertexBuffers(commandBuffers[i], 0, 1, &modelBuffer, offsets);
			offsets[0] = sizeof(Transformation);
			vkCmdBindVertexBuffers(commandBuffers[i], 1, 1, &instancedTransformationBuffer, offsets);
			indexBufferOffset = model.vertices.bufferSize; // sizeof vertex buffer
			vkCmdBindIndexBuffer(commandBuffers[i], modelBuffer, indexBufferOffset, VK_INDEX_TYPE_UINT32);
			vkCmdDrawIndexed(commandBuffers[i], static_cast<uint32_t>(model.indices.size()), INSTANCE_NUM_SQRT * INSTANCE_NUM_SQRT, 0, 0, 0);

			vkCmdEndRenderPass(commandBuffers[i]);

			/*
			* ssao occlusion render - full screem quad
			*/
			renderPassBeginInfo.renderPass = ssaoRenderPass;
			renderPassBeginInfo.framebuffer = ssaoFramebuffers[resourceIndex].framebuffer;
			renderPassBeginInfo.clearValueCount = 1;
			renderPassBeginInfo.pClearValues = ssaoClearValues.data();
			vkCmdBeginRenderPass(commandBuffers[i], &renderPassBeginInfo, VK_SUBPASS_CONTENTS_INLINE);

			//dynamic states
			vktools::setViewportScissorDynamicStates(commandBuffers[i], swapchain.extent);

			vkCmdBindPipeline(commandBuffers[i], VK_PIPELINE_BIND_POINT_GRAPHICS, ssaoPipeline);
			vkCmdBindDescriptorSets(commandBuffers[i], VK_PIPELINE_BIND_POINT_GRAPHICS, ssaoPipelineLayout, 0, 1,
				&ssaoDescriptorSets[resourceIndex], 0, nullptr);
			vkCmdDraw(commandBuffers[i], 3, 1, 0, 0);
			vkCmdEndRenderPass(commandBuffers[i]);

			/*
			* ssao blur - full screen quad
			*/
			renderPassBeginInfo.renderPass = ssaoBlurRenderPass;
			renderPassBeginInfo.framebuffer = ssaoBlurFramebuffers[resourceIndex].framebuffer;
			vkCmdBeginRenderPass(commandBuffers[i], &renderPassBeginInfo, VK_SUBPASS_CONTENTS_INLINE);

			//dynamic states
			vktools::setViewportScissorDynamicStates(commandBuffers[i], swapchain.extent);

			vkCmdBindPipeline(commandBuffers[i], VK_PIPELINE_BIND_POINT_GRAPHICS, ssaoBlurPipeline);
			vkCmdBindDescriptorSets(commandBuffers[i], VK_PIPELINE_BIND_POINT_GRAPHICS, ssaoBlurPipelineLayout, 0, 1,
				&ssaoBlurDescriptorSets[resourceIndex], 0, nullptr);
			vkCmdDraw(commandBuffers[i], 3, 1, 0, 0);
			vkCmdEndRenderPass(commandBuffers[i]);

			/*
			* lighting calculation for normal pixels
			*/
			renderPassBeginInfo.renderPass = renderPass;
			size_t framebufferIndex = i % framebuffers.size();
			renderPassBeginInfo.framebuffer = framebuffers[framebufferIndex];
			renderPassBeginInfo.clearValueCount = static_cast<uint32_t>(clearValues.size());
			renderPassBeginInfo.pClearValues = clearValues.data();
			vkCmdBeginRenderPass(commandBuffers[i], &renderPassBeginInfo, VK_SUBPASS_CONTENTS_INLINE);

			//dynamic states
			vktools::setViewportScissorDynamicStates(commandBuffers[i], swapchain.extent);

			vkCmdBindPipeline(commandBuffers[i], VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline);
			vkCmdBindDescriptorSets(commandBuffers[i], VK_PIPELINE_BIND_POINT_GRAPHICS, pipelineLayout, 0, 1,
				&descriptorSets[resourceIndex], 0, nullptr);

			vkCmdDraw(commandBuffers[i], 3, 1, 0, 0);

			/*
			* final - lighting calculation for complex pixels
			*/
			vkCmdBindPipeline(commandBuffers[i], VK_PIPELINE_BIND_POINT_GRAPHICS, msaaPipeline);
			vkCmdBindDescriptorSets(commandBuffers[i], VK_PIPELINE_BIND_POINT_GRAPHICS, pipelineLayout, 0, 1,
				&descriptorSets[resourceIndex], 0, nullptr);

			vkCmdDraw(commandBuffers[i], 3, 1, 0, 0);

			/*
			* imgui
			*/
			imguiBase->drawFrame(commandBuffers[i], resourceIndex);

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
		VkDeviceSize deferredUBOSize = sizeof(UBODeferredRending);
		deferredUBO.resize(MAX_FRAMES_IN_FLIGHT);
		deferredUBOMemories.resize(MAX_FRAMES_IN_FLIGHT);

		VkBufferCreateInfo cameraUBOCreateInfo = vktools::initializers::bufferCreateInfo(
			cameraUBOSize, VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT);
		VkBufferCreateInfo deferredUBOCreateInfo = vktools::initializers::bufferCreateInfo(
			deferredUBOSize, VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT);

		for (size_t i = 0; i < MAX_FRAMES_IN_FLIGHT; ++i) {
			VK_CHECK_RESULT(vkCreateBuffer(devices.device, &cameraUBOCreateInfo, nullptr, &cameraUBO[i]));
			cameraUBOMemories[i] = devices.memoryAllocator.allocateBufferMemory(
				cameraUBO[i], VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);

			VK_CHECK_RESULT(vkCreateBuffer(devices.device, &deferredUBOCreateInfo, nullptr, &deferredUBO[i]));
			deferredUBOMemories[i] = devices.memoryAllocator.allocateBufferMemory(
				deferredUBO[i], VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
		}

		//assign color & radius
		for (int i = 0; i < LIGHT_NUM; ++i) {
			uboDeferredRendering.lights[i].color = glm::vec3(rdFloat(RNGen), rdFloat(RNGen), rdFloat(RNGen));
			float constant = 1.f;
			float linear = 0.7f;
			float quadratic = 1.8f;
			float lightMax = std::fmaxf(std::fmaxf(uboDeferredRendering.lights[i].color.x,
				uboDeferredRendering.lights[i].color.y), uboDeferredRendering.lights[i].color.z);
			uboDeferredRendering.lights[i].radius = (-linear + std::sqrtf(
				linear * linear - 4 * quadratic * (constant - (256 / 5.f) * lightMax)) / (2 * quadratic));
		}
	}

	/*
	* update matrices in ubo - rotates 90 degrees per second
	*
	* @param currentFrame - index of uniform buffer vector
	*/
	void updateUniformBuffer(size_t currentFrame) {
		/*
		* update camera
		*/
		ubo.view = cameraMatrices.view;
		ubo.normalMatrix = glm::transpose(glm::inverse(ubo.view /** ubo.model*/));
		ubo.proj = cameraMatrices.proj;

		cameraUBOMemories[currentFrame].mapData(devices.device, &ubo);

		/*
		* update lights & renderMode
		*/
		uboDeferredRendering.sampleCount = sampleCount;
		Imgui* imgui = static_cast<Imgui*>(imguiBase);
		uboDeferredRendering.renderMode = imgui->userInput.renderMode;
		uboDeferredRendering.threshold = imgui->userInput.threshold;
		uboDeferredRendering.enableSSAO = imgui->userInput.enableSSAO;
		float PI = 3.141592f;
		float angleInc = 2 * PI / LIGHT_NUM;
		for (int i = 0; i < LIGHT_NUM; ++i) {
			uboDeferredRendering.lights[i].pos =
				glm::vec4(12 * std::cos(oldTime / 3 + i * angleInc), 3.f, 12 * std::sin(oldTime / 3 + i * angleInc), 1.f);
			uboDeferredRendering.lights[i].pos = ubo.view * uboDeferredRendering.lights[i].pos; // light position in view space
		}
		deferredUBOMemories[currentFrame].mapData(devices.device, &uboDeferredRendering);
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
		* ssao descriptor
		*/
		ssaoBindings.addBinding(0, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_FRAGMENT_BIT); //gbuffer pos
		ssaoBindings.addBinding(1, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_FRAGMENT_BIT); //gbuffer normal
		ssaoBindings.addBinding(2, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_FRAGMENT_BIT); //ssao noise
		ssaoBindings.addBinding(3, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1, VK_SHADER_STAGE_FRAGMENT_BIT); //sample kernal
		ssaoBindings.addBinding(4, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1, VK_SHADER_STAGE_FRAGMENT_BIT); //camera matrices
		ssaoDescriptorPool = ssaoBindings.createDescriptorPool(devices.device, MAX_FRAMES_IN_FLIGHT);
		ssaoDescriptorSetLayout = ssaoBindings.createDescriptorSetLayout(devices.device);
		ssaoDescriptorSets = vktools::allocateDescriptorSets(devices.device, ssaoDescriptorSetLayout, ssaoDescriptorPool, MAX_FRAMES_IN_FLIGHT);

		/*
		* ssao blur descriptor
		*/
		ssaoBlurBindings.addBinding(0, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_FRAGMENT_BIT); //ssao attchment from privous pass
		ssaoBlurDescriptorPool = ssaoBlurBindings.createDescriptorPool(devices.device, MAX_FRAMES_IN_FLIGHT);
		ssaoBlurDescriptorSetLayout = ssaoBlurBindings.createDescriptorSetLayout(devices.device);
		ssaoBlurDescriptorSets = vktools::allocateDescriptorSets(devices.device, ssaoBlurDescriptorSetLayout, ssaoBlurDescriptorPool, MAX_FRAMES_IN_FLIGHT);

		/*
		* full-screen quad descriptor
		*/
		//descriptor - 3 image samplers
		bindings.addBinding(0, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_FRAGMENT_BIT);
		bindings.addBinding(1, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_FRAGMENT_BIT);
		bindings.addBinding(2, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_FRAGMENT_BIT);
		//descriptor - 1 uniform buffer
		bindings.addBinding(3, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1, VK_SHADER_STAGE_FRAGMENT_BIT);
		descriptorPool = bindings.createDescriptorPool(devices.device, MAX_FRAMES_IN_FLIGHT);
		descriptorSetLayout = bindings.createDescriptorSetLayout(devices.device);
		descriptorSets = vktools::allocateDescriptorSets(devices.device, descriptorSetLayout, descriptorPool, MAX_FRAMES_IN_FLIGHT);
	}

	/*
	* update descriptor set
	*/
	void updateDescriptorSets() {
		for (size_t i = 0; i < MAX_FRAMES_IN_FLIGHT; ++i) {
			std::vector<VkWriteDescriptorSet> writes;

			//gbuffer attachments
			VkDescriptorImageInfo posAttachmentInfo{ offscreenSampler,
				offscreenFramebuffers[i].attachments[0].imageView, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL };
			VkDescriptorImageInfo normalAttachmentInfo{ offscreenSampler,
				offscreenFramebuffers[i].attachments[1].imageView, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL };
			VkDescriptorImageInfo ssaoBlurAttachmentInfo{ offscreenSampler,
				ssaoBlurFramebuffers[i].attachments[0].imageView, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL };

			//offscreen rendering
			VkDescriptorBufferInfo cameraUBObufferInfo{ cameraUBO[i], 0, sizeof(CameraMatrices) };
			//full quad rendering
			VkDescriptorBufferInfo deferredUBObufferInfo{ deferredUBO[i], 0, sizeof(UBODeferredRending) };
			
			//ssao
			VkDescriptorBufferInfo sampleKernelUBObufferInfo{ ssaoKernelUBO, 0, ssaoKernelUBOMemory.size };
			writes.push_back(ssaoBindings.makeWrite(ssaoDescriptorSets[i], 0, &posAttachmentInfo));
			writes.push_back(ssaoBindings.makeWrite(ssaoDescriptorSets[i], 1, &normalAttachmentInfo));
			writes.push_back(ssaoBindings.makeWrite(ssaoDescriptorSets[i], 2, &ssaoNoiseTex.descriptor));
			writes.push_back(ssaoBindings.makeWrite(ssaoDescriptorSets[i], 3, &sampleKernelUBObufferInfo));
			writes.push_back(ssaoBindings.makeWrite(ssaoDescriptorSets[i], 4, &cameraUBObufferInfo)); //need proj matrix only
			
			//ssao blur
			VkDescriptorImageInfo ssaoAttachmentInfo{ offscreenSampler,
				ssaoFramebuffers[i].attachments[0].imageView, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL };
			writes.push_back(ssaoBlurBindings.makeWrite(ssaoBlurDescriptorSets[i], 0, &ssaoAttachmentInfo));

			writes.push_back(offscreenBindings.makeWrite(offscreenDescriptorSets[i], 0, &cameraUBObufferInfo));
			writes.push_back(bindings.makeWrite(descriptorSets[i], 0, &posAttachmentInfo));
			writes.push_back(bindings.makeWrite(descriptorSets[i], 1, &normalAttachmentInfo));
			writes.push_back(bindings.makeWrite(descriptorSets[i], 2, &ssaoBlurAttachmentInfo));
			writes.push_back(bindings.makeWrite(descriptorSets[i], 3, &deferredUBObufferInfo));
			vkUpdateDescriptorSets(devices.device, static_cast<uint32_t>(writes.size()), writes.data(), 0, nullptr);
		}
	}
};

//entry point
RUN_APPLICATION_MAIN(VulkanApp, 1200, 800, "project2_deferred_rendering");
