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
}

class Imgui : public ImguiBase {
public:
	virtual void newFrame() override {
		ImGui::NewFrame();
		ImGui::Begin("Setting");

		ImGui::Checkbox("HDR", &userInput.enableHDR);
		if(userInput.enableHDR == true)
			ImGui::Checkbox("Bloom", &userInput.enableBloom);

		ImGui::End();
		ImGui::Render();
	}

	/* user input collection */
	struct UserInput {
		bool enableHDR= true;
		bool enableBloom = true;
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
		vkDestroyDescriptorPool(devices.device, computeDescriptorPool, nullptr);
		vkDestroyDescriptorSetLayout(devices.device, computeDescriptorSetLayout, nullptr);
		vkDestroyDescriptorPool(devices.device, hdrDescriptorPool, nullptr);
		vkDestroyDescriptorSetLayout(devices.device, hdrDescriptorSetLayout, nullptr);
		vkDestroyDescriptorPool(devices.device, brightDescriptorPool, nullptr);
		vkDestroyDescriptorSetLayout(devices.device, brightDescriptorSetLayout, nullptr);
		vkDestroyDescriptorPool(devices.device, bloomDescriptorVertPool, nullptr);
		vkDestroyDescriptorSetLayout(devices.device, bloomDescriptorSetVertLayout, nullptr);
		vkDestroyDescriptorPool(devices.device, bloomDescriptorHorzPool, nullptr);
		vkDestroyDescriptorSetLayout(devices.device, bloomDescriptorSetHorzLayout, nullptr);

		//uniform buffers
		for (size_t i = 0; i < cameraUBO.size(); ++i) {
			devices.memoryAllocator.freeBufferMemory(cameraUBO[i],
				VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
			vkDestroyBuffer(devices.device, cameraUBO[i], nullptr);
		}
		devices.memoryAllocator.freeBufferMemory(computeUBO,
			VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
		vkDestroyBuffer(devices.device, computeUBO, nullptr);

		for (auto& hdrUBOBuffer : hdrUBO) {
			devices.memoryAllocator.freeBufferMemory(hdrUBOBuffer,
				VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
			vkDestroyBuffer(devices.device, hdrUBOBuffer, nullptr);
		}

		//model & floor buffer & skybox buffers
		devices.memoryAllocator.freeBufferMemory(particleBuffer, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
		vkDestroyBuffer(devices.device, particleBuffer, nullptr);
		particleTex.cleanup();

		//framebuffers
		for (auto& framebuffer : framebuffers) {
			vkDestroyFramebuffer(devices.device, framebuffer, nullptr);
		}

		//pipelines & render pass
		vkDestroyPipeline(devices.device, pipeline, nullptr);
		vkDestroyPipelineLayout(devices.device, pipelineLayout, nullptr);
		vkDestroyRenderPass(devices.device, renderPass, nullptr);
		vkDestroyPipeline(devices.device, computePipelineCompute, nullptr);
		vkDestroyPipeline(devices.device, computePipelineUpdate, nullptr);
		vkDestroyPipelineLayout(devices.device, computePipelineLayout, nullptr);
		vkDestroyPipeline(devices.device, hdrPipeline, nullptr);
		vkDestroyPipelineLayout(devices.device, hdrPipelineLayout, nullptr);
		vkDestroyRenderPass(devices.device, hdrRenderPass, nullptr);
		vkDestroyPipeline(devices.device, brightPipeline, nullptr);
		vkDestroyPipelineLayout(devices.device, brightPipelineLayout, nullptr);
		vkDestroyRenderPass(devices.device, brightRenderPass, nullptr);
		vkDestroyPipeline(devices.device, bloomPipelineVert, nullptr);
		vkDestroyPipeline(devices.device, bloomPipelineHorz, nullptr);
		vkDestroyRenderPass(devices.device, bloomRenderPass, nullptr);
		vkDestroyPipelineLayout(devices.device, bloomPipelineLayout, nullptr);
		vkDestroySampler(devices.device, offscreenSampler, nullptr);

		for (int i = 0; i < MAX_FRAMES_IN_FLIGHT; ++i) {
			hdrFramebuffers[i].cleanup();
			brightFramebuffers[i].cleanup();
			bloomFramebufferVerts[i].cleanup();
			bloomFramebufferHorzs[i].cleanup();
		}
		

		//semaphore
		for (auto& semaphore : particleComputeCompleteSemaphores) {
			vkDestroySemaphore(devices.device, semaphore, nullptr);
		}
		for (auto& semaphore : renderCompleteComputeSemaphores) {
			vkDestroySemaphore(devices.device, semaphore, nullptr);
		}
	}

	/*
	* application initialization - also contain base class initApp()
	*/
	virtual void initApp() override {
		VulkanAppBase::initApp();

		//init cap setting
		camera.camPos = glm::vec3(0.f, 0.f, 150.f);
		camera.camFront = glm::normalize(-camera.camPos);
		camera.camUp = glm::vec3(0.f, 1.f, 0.f);

		//create particle vertex buffer
		createParticles();
		//load particle texture
		particleTex.load(&devices, "../../textures/particle.png");
		
		createHDRBloomResources();
		createRenderpass();
		createFramebuffers();
		createUniformBuffers();
		createDescriptorSet();
		updateDescriptorSets();
		createPipeline();
		imguiBase->init(&devices, swapchain.extent.width, swapchain.extent.height,
			renderPass, MAX_FRAMES_IN_FLIGHT, VK_SAMPLE_COUNT_1_BIT);
		recordCommandBuffer();
		createComputeSemaphore();
		createComputeCommandBuffers();
		recordComputeCommandBuffers();
	}

private:
	/** compute shader ubo */
	struct ComputeUBO {
		float dt;
		int particleNum;
	} ubo;

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
	VkClearColorValue clearColor{0.0f, 0.0f, 0.0f, 1.f};
	/** uniform buffer handle */
	std::vector<VkBuffer> cameraUBO;
	/**  uniform buffer memory handle */
	std::vector<MemoryAllocator::HostVisibleMemory> cameraUBOMemories;
	/** semaphore for synchronizing compute & graphics pipeline */
	std::vector<VkSemaphore> renderCompleteComputeSemaphores;

	/** particle info */
	struct Particle {
		glm::vec4 posm; //xyz = position, w = mass
		glm::vec4 vel;
	};
	/** number of particle */
	uint32_t particleNum = 0;
	/** particle texture */
	Texture2D particleTex;

	/*
	* compute resources
	*/
	/** partice buffer */
	VkBuffer particleBuffer = VK_NULL_HANDLE;
	/** particle buffer size */
	VkDeviceSize particleBufferSize;
	/** uniform buffer handle */
	VkBuffer computeUBO;
	/**  uniform buffer memory handle */
	MemoryAllocator::HostVisibleMemory computeUBOMemories;
	/** descriptor set bindings */
	DescriptorSetBindings computeBindings;
	/** descriptor layout */
	VkDescriptorSetLayout computeDescriptorSetLayout;
	/** descriptor pool */
	VkDescriptorPool computeDescriptorPool;
	/** descriptor sets */
	VkDescriptorSet computeDescriptorSets;
	/** graphics pipeline */
	VkPipeline computePipelineCompute = VK_NULL_HANDLE, computePipelineUpdate = VK_NULL_HANDLE;
	/** pipeline layout */
	VkPipelineLayout computePipelineLayout = VK_NULL_HANDLE;
	/** semaphore for synchronizing compute & graphics pipeline */
	std::vector<VkSemaphore> particleComputeCompleteSemaphores;
	/** compute command buffers */
	std::vector<VkCommandBuffer> computeCommandBuffers;

	/*
	* hdr & bloom resources
	*/
	/** offscreen (hdr) framebuffer */
	std::vector<Framebuffer> hdrFramebuffers, brightFramebuffers, bloomFramebufferVerts, bloomFramebufferHorzs;
	/** render passes */
	VkRenderPass hdrRenderPass = VK_NULL_HANDLE,
		brightRenderPass = VK_NULL_HANDLE,
		bloomRenderPass = VK_NULL_HANDLE;
	/** sampler */
	VkSampler offscreenSampler = VK_NULL_HANDLE;
	/** pipelines */
	VkPipeline hdrPipeline = VK_NULL_HANDLE,
		brightPipeline = VK_NULL_HANDLE,
		bloomPipelineVert = VK_NULL_HANDLE,
		bloomPipelineHorz = VK_NULL_HANDLE;
	/** pipelines */
	VkPipelineLayout hdrPipelineLayout = VK_NULL_HANDLE,
		brightPipelineLayout = VK_NULL_HANDLE,
		bloomPipelineLayout = VK_NULL_HANDLE;
	/** descriptor set bindings */
	DescriptorSetBindings hdrBindings, brightBindings, bloomBindingsVert, bloomBindingsHorz;
	/** descriptor pools */
	VkDescriptorPool hdrDescriptorPool = VK_NULL_HANDLE,
		brightDescriptorPool = VK_NULL_HANDLE,
		bloomDescriptorVertPool = VK_NULL_HANDLE,
		bloomDescriptorHorzPool = VK_NULL_HANDLE;
	/** descriptor set layouts */
	VkDescriptorSetLayout hdrDescriptorSetLayout = VK_NULL_HANDLE,
		brightDescriptorSetLayout = VK_NULL_HANDLE,
		bloomDescriptorSetVertLayout = VK_NULL_HANDLE,
		bloomDescriptorSetHorzLayout = VK_NULL_HANDLE;
	/** descriptor sets */
	std::vector<VkDescriptorSet> hdrDescriptorSets,
		brightDescriptorSets,
		bloomDescriptorSetsVert,
		bloomDescriptorSetsHorz;

	struct HDRUBO {
		uint32_t enableHDR = 1;
		uint32_t enableBloom = 1;
	} hdrubo;

	std::vector<VkBuffer> hdrUBO;
	std::vector<MemoryAllocator::HostVisibleMemory> hdrUBOMemories;

	/*
	* called every frame - submit queues
	*/
	virtual void draw() override {
		uint32_t imageIndex = prepareFrame();

		/*
		* graphics command
		*/
		VkPipelineStageFlags waitStages[] = { 
			VK_PIPELINE_STAGE_VERTEX_INPUT_BIT, 
			VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT 
		};
		VkSemaphore graphicsWaitSemaphores[] = { 
			particleComputeCompleteSemaphores[currentFrame],
			presentCompleteSemaphores[currentFrame]
		};
		VkSemaphore graphicsSignalSemaphores[] = { 
			renderCompleteComputeSemaphores[currentFrame],
			renderCompleteSemaphores[currentFrame] 
		};

		VkSubmitInfo submitInfo{ VK_STRUCTURE_TYPE_SUBMIT_INFO };
		submitInfo.waitSemaphoreCount = 2;
		submitInfo.pWaitSemaphores = graphicsWaitSemaphores;
		submitInfo.pWaitDstStageMask = waitStages;
		submitInfo.commandBufferCount = 1;
		size_t commandBufferIndex = currentFrame * framebuffers.size() + imageIndex;
		submitInfo.pCommandBuffers = &commandBuffers[commandBufferIndex];
		submitInfo.signalSemaphoreCount = 2;
		submitInfo.pSignalSemaphores = graphicsSignalSemaphores;
		VK_CHECK_RESULT(vkQueueSubmit(devices.graphicsQueue, 1, &submitInfo, frameLimitFences[currentFrame]));

		submitFrame(imageIndex);

		/*
		* compute command
		*/
		VkPipelineStageFlags waitStageCompute = VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT;

		VkSubmitInfo computeSubmitInfo{ VK_STRUCTURE_TYPE_SUBMIT_INFO };
		computeSubmitInfo.waitSemaphoreCount = 1;
		computeSubmitInfo.pWaitSemaphores = &renderCompleteComputeSemaphores[currentFrame];
		computeSubmitInfo.pWaitDstStageMask = &waitStageCompute;
		computeSubmitInfo.commandBufferCount = 1;
		computeSubmitInfo.pCommandBuffers = &computeCommandBuffers[currentFrame];
		computeSubmitInfo.signalSemaphoreCount = 1;
		computeSubmitInfo.pSignalSemaphores = &particleComputeCompleteSemaphores[currentFrame];
		VK_CHECK_RESULT(vkQueueSubmit(devices.computeQueue, 1, &computeSubmitInfo, VK_NULL_HANDLE));
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

		createHDRBloomResources(true);
		updateDescriptorSets();

		destroyComputeCommandBuffers();
		createComputeCommandBuffers();
		recordComputeCommandBuffers();

		recordCommandBuffer();
	}

	/*
	* create renderpass for framebuffer (containing swapchain images)
	*/
	void createRenderpass() {
		//attachment 0 : swapchain image
		VkAttachmentDescription swapchainIamge{};
		swapchainIamge.format = swapchain.surfaceFormat.format;
		swapchainIamge.samples = sampleCount;
		swapchainIamge.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
		swapchainIamge.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
		swapchainIamge.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
		swapchainIamge.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
		swapchainIamge.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
		swapchainIamge.finalLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;

		VkAttachmentReference swapchainImageRef{};
		swapchainImageRef.attachment = 0;
		swapchainImageRef.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

		//attachment 1 : depth image
		VkAttachmentDescription depthImage{};
		depthImage.format = depthFormat;
		depthImage.samples = VK_SAMPLE_COUNT_1_BIT;
		depthImage.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
		depthImage.storeOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
		depthImage.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
		depthImage.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
		//layout of depth image already translated
		depthImage.initialLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
		depthImage.finalLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

		VkAttachmentReference depthImageRef{};
		depthImageRef.attachment = 1;
		depthImageRef.layout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

		//subpass
		VkSubpassDescription subpassDescription{};
		subpassDescription.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
		subpassDescription.inputAttachmentCount = 0; //optional
		subpassDescription.pInputAttachments = nullptr; //optional
		subpassDescription.colorAttachmentCount = 1;
		subpassDescription.pColorAttachments = &swapchainImageRef;
		subpassDescription.pResolveAttachments = nullptr;
		subpassDescription.pDepthStencilAttachment = &depthImageRef;

		//subpass dependencies
		std::array<VkSubpassDependency, 2> dependencies{};
		dependencies[0].srcSubpass = VK_SUBPASS_EXTERNAL;
		dependencies[0].dstSubpass = 0;
		dependencies[0].srcStageMask = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
		dependencies[0].srcAccessMask = VK_ACCESS_SHADER_READ_BIT;
		dependencies[0].dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
		dependencies[0].dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT | VK_ACCESS_COLOR_ATTACHMENT_READ_BIT;
		dependencies[0].dependencyFlags = VK_DEPENDENCY_BY_REGION_BIT;

		dependencies[1].srcSubpass = 0;
		dependencies[1].dstSubpass = VK_SUBPASS_EXTERNAL;
		dependencies[1].srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
		dependencies[1].srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT | VK_ACCESS_COLOR_ATTACHMENT_READ_BIT;
		dependencies[1].dstStageMask = VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT;
		dependencies[1].dstAccessMask = VK_ACCESS_MEMORY_READ_BIT;
		dependencies[1].dependencyFlags = VK_DEPENDENCY_BY_REGION_BIT;

		std::array<VkAttachmentDescription, 2> attachments{ swapchainIamge, depthImage };

		VkRenderPassCreateInfo renderpassInfo{ VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO };
		renderpassInfo.attachmentCount = static_cast<uint32_t>(attachments.size());
		renderpassInfo.pAttachments = attachments.data();
		renderpassInfo.subpassCount = 1;
		renderpassInfo.pSubpasses = &subpassDescription;
		renderpassInfo.dependencyCount = static_cast<uint32_t>(dependencies.size());
		renderpassInfo.pDependencies = dependencies.data();
		VK_CHECK_RESULT(vkCreateRenderPass(devices.device, &renderpassInfo, nullptr, &renderPass));
	}

	/*
	* 
	*/
	void transferOwnership() {

	}

	/*
	* create framebuffers & renderpasses for hdr / bloom passes
	*/
	void createHDRBloomResources(bool createFramebufferOnly = false) {
		hdrFramebuffers.resize(MAX_FRAMES_IN_FLIGHT);
		brightFramebuffers.resize(MAX_FRAMES_IN_FLIGHT);
		bloomFramebufferVerts.resize(MAX_FRAMES_IN_FLIGHT);
		bloomFramebufferHorzs.resize(MAX_FRAMES_IN_FLIGHT);

		for (int i = 0; i < MAX_FRAMES_IN_FLIGHT; ++i) {
			hdrFramebuffers[i].init(&devices);
			hdrFramebuffers[i].cleanup();
			brightFramebuffers[i].init(&devices);
			brightFramebuffers[i].cleanup();
			bloomFramebufferVerts[i].init(&devices);
			bloomFramebufferVerts[i].cleanup();
			bloomFramebufferHorzs[i].init(&devices);
			bloomFramebufferHorzs[i].cleanup();

			VkImageCreateInfo hdrImageInfo =
				vktools::initializers::imageCreateInfo({ swapchain.extent.width, swapchain.extent.height, 1 },
					VK_FORMAT_R16G16B16A16_SFLOAT,
					VK_IMAGE_TILING_OPTIMAL,
					VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT
				);

			//hdr image
			hdrFramebuffers[i].addAttachment(hdrImageInfo, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
			//contain only bright color (brightness > 1.f)
			brightFramebuffers[i].addAttachment(hdrImageInfo, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
			//bloom vertical blur
			bloomFramebufferVerts[i].addAttachment(hdrImageInfo, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
			//bloom horizontal blur
			bloomFramebufferHorzs[i].addAttachment(hdrImageInfo, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
			//depth
			hdrImageInfo.format = depthFormat;
			hdrImageInfo.usage = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT;
			hdrFramebuffers[i].addAttachment(hdrImageInfo, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
		}

		VkSubpassDependency initialDependency{};
		initialDependency.srcSubpass = VK_SUBPASS_EXTERNAL;
		initialDependency.dstSubpass = 0;
		initialDependency.srcStageMask = VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT;
		initialDependency.srcAccessMask = VK_ACCESS_MEMORY_READ_BIT;
		initialDependency.dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT | VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT;
		initialDependency.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
		initialDependency.dependencyFlags = VK_DEPENDENCY_BY_REGION_BIT;

		std::vector<VkSubpassDependency> dependencies{};
		dependencies.resize(2);
		dependencies[0].srcSubpass = VK_SUBPASS_EXTERNAL;
		dependencies[0].dstSubpass = 0;
		dependencies[0].srcStageMask = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
		dependencies[0].srcAccessMask = VK_ACCESS_SHADER_READ_BIT;
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

		//renderpass
		if (createFramebufferOnly == false) {
			hdrRenderPass = hdrFramebuffers[0].createRenderPass({initialDependency, dependencies[1]});
			brightRenderPass = brightFramebuffers[0].createRenderPass(dependencies);
			dependencies[0].dependencyFlags = 0;
			dependencies[1].dependencyFlags = 0;
			bloomRenderPass = bloomFramebufferVerts[0].createRenderPass(dependencies);

			VkSamplerCreateInfo samplerInfo =
				vktools::initializers::samplerCreateInfo(devices.availableFeatures, devices.properties);
			VK_CHECK_RESULT(vkCreateSampler(devices.device, &samplerInfo, nullptr, &offscreenSampler));
		}

		//create framebuffer
		for (int i = 0; i < MAX_FRAMES_IN_FLIGHT; ++i) {
			hdrFramebuffers[i].createFramebuffer(swapchain.extent, hdrRenderPass);
			brightFramebuffers[i].createFramebuffer(swapchain.extent, brightRenderPass);
			bloomFramebufferVerts[i].createFramebuffer(swapchain.extent, bloomRenderPass);
			bloomFramebufferHorzs[i].createFramebuffer(swapchain.extent, bloomRenderPass);
		}
	}

	/*
	* return a random point on s surface of sphere - naive
	*/
	glm::vec3 getRandomPointOnSphere(const glm::vec3& center, float radius) {
		float PI = 3.141592f;
		float theta = 2 * PI * static_cast<float>(rdFloat(RNGen));
		float phi = std::acos(1.f - 2 * static_cast<float>(rdFloat(RNGen)));
		float x = std::sin(phi) * std::cos(theta);
		float y = std::sin(phi) * std::sin(theta);
		float z = std::cos(phi);
		return radius * glm::vec3( x, y, z ) + center;
	}

	/*
	* create particle info
	*/
	void createParticles() {
		std::vector<glm::vec3> attractors{
			glm::vec3(0.f, 0.f, 0.f)
		};

		const uint32_t particlePerAttractor = 65536;
		particleNum = static_cast<uint32_t>(attractors.size()) * particlePerAttractor;
		ubo.particleNum = particleNum;
		std::vector<Particle> particles(particleNum);

		for (size_t i = 0; i < attractors.size(); ++i) {
			for (uint32_t j = 0; j < particlePerAttractor; ++j) {
				Particle& particle = particles[i * particlePerAttractor + j];
				glm::vec3 pos = getRandomPointOnSphere(attractors[i], 30.f);
				float mass = (static_cast<float>(rdFloat(RNGen)) * 0.5f + 0.5f) * 75.f;
				particle.posm = glm::vec4(pos, mass);
				particle.vel = glm::vec4(0.f);
				
			}
		}

		/* create vertex buffer */
		particleBufferSize = particles.size() * sizeof(Particle);

		VkBuffer stagingBuffer;
		MemoryAllocator::HostVisibleMemory hostVisibleMemory = 
			devices.createBuffer(stagingBuffer, particleBufferSize, VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
				VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
		hostVisibleMemory.mapData(devices.device, particles.data());
	
		devices.createBuffer(particleBuffer, particleBufferSize,
			VK_BUFFER_USAGE_VERTEX_BUFFER_BIT |
			VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | 
			VK_BUFFER_USAGE_TRANSFER_DST_BIT);

		VkCommandBuffer oneTimeCmdBuf = devices.beginCommandBuffer();
		VkBufferCopy copy{};
		copy.size = particleBufferSize;
		vkCmdCopyBuffer(oneTimeCmdBuf, stagingBuffer, particleBuffer, 1, &copy);
		devices.endCommandBuffer(oneTimeCmdBuf);

		devices.memoryAllocator.freeBufferMemory(stagingBuffer,
			VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
		vkDestroyBuffer(devices.device, stagingBuffer, nullptr);
	}

	/*
	* create semaphore to sync compute & graphics pipeline
	*/
	void createComputeSemaphore() {
		//create semaphore
		particleComputeCompleteSemaphores.resize(MAX_FRAMES_IN_FLIGHT);
		renderCompleteComputeSemaphores.resize(MAX_FRAMES_IN_FLIGHT);
		VkSemaphoreCreateInfo semaphoreCreateInfo{ VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO };
		for (int i = 0; i < MAX_FRAMES_IN_FLIGHT; ++i) {
			VK_CHECK_RESULT(vkCreateSemaphore(devices.device, &semaphoreCreateInfo, nullptr, &particleComputeCompleteSemaphores[i]));
			VK_CHECK_RESULT(vkCreateSemaphore(devices.device, &semaphoreCreateInfo, nullptr, &renderCompleteComputeSemaphores[i]));
		}

		//signal semaphore since graphics pipeline is used before compute pipeline
		VkSubmitInfo submitInfo{};
		submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
		submitInfo.pSignalSemaphores = particleComputeCompleteSemaphores.data();
		submitInfo.signalSemaphoreCount = static_cast<uint32_t>(particleComputeCompleteSemaphores.size());
		VK_CHECK_RESULT(vkQueueSubmit(devices.graphicsQueue, 1, &submitInfo, VK_NULL_HANDLE));
		VK_CHECK_RESULT(vkQueueWaitIdle(devices.graphicsQueue));
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
		* hdr pass
		*/
		PipelineGenerator gen(devices.device);
		gen.addVertexInputBindingDescription({ {0, sizeof(Particle), VK_VERTEX_INPUT_RATE_VERTEX} });
		gen.addVertexInputAttributeDescription({ 
			{0, 0, VK_FORMAT_R32G32B32A32_SFLOAT, offsetof(Particle, posm)}, 
			{1, 0, VK_FORMAT_R32G32B32A32_SFLOAT, offsetof(Particle, vel)} 
		});
		gen.setInputTopology(VK_PRIMITIVE_TOPOLOGY_POINT_LIST);
		gen.setRasterizerInfo(VK_POLYGON_MODE_FILL, VK_CULL_MODE_NONE);
		gen.setDepthStencilInfo(VK_FALSE, VK_FALSE, VK_COMPARE_OP_ALWAYS);

		VkPipelineColorBlendAttachmentState state{};
		state.blendEnable = VK_TRUE;
		state.colorWriteMask = 0xF;
		state.colorBlendOp = VK_BLEND_OP_ADD;
		state.srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
		state.dstColorBlendFactor = VK_BLEND_FACTOR_ONE;
		state.alphaBlendOp = VK_BLEND_OP_ADD;
		state.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
		state.dstAlphaBlendFactor = VK_BLEND_FACTOR_ZERO;
		gen.setColorBlendAttachmentState(state);
		gen.addDescriptorSetLayout({ hdrDescriptorSetLayout });
		gen.addShader(
			vktools::createShaderModule(devices.device, vktools::readFile("shaders/particle_vert.spv")),
			VK_SHADER_STAGE_VERTEX_BIT);
		gen.addShader(
			vktools::createShaderModule(devices.device, vktools::readFile("shaders/particle_frag.spv")),
			VK_SHADER_STAGE_FRAGMENT_BIT);

		//generate pipeline layout & pipeline
		gen.generate(hdrRenderPass, &hdrPipeline, &hdrPipelineLayout);

		/*
		* extract bright color
		*/
		gen.resetAll();
		gen.setRasterizerInfo(VK_POLYGON_MODE_FILL, VK_CULL_MODE_NONE);
		gen.addDescriptorSetLayout({ brightDescriptorSetLayout });
		gen.addShader(
			vktools::createShaderModule(devices.device, vktools::readFile("shaders/full_quad_vert.spv")),
			VK_SHADER_STAGE_VERTEX_BIT);
		gen.addShader(
			vktools::createShaderModule(devices.device, vktools::readFile("shaders/full_quad_extract_bright_color_frag.spv")),
			VK_SHADER_STAGE_FRAGMENT_BIT);

		//generate pipeline layout & pipeline
		gen.generate(brightRenderPass, &brightPipeline, &brightPipelineLayout);

		/*
		* bloom pass
		*/
		gen.resetAll();
		gen.setRasterizerInfo(VK_POLYGON_MODE_FILL, VK_CULL_MODE_NONE);
		gen.addDescriptorSetLayout({ bloomDescriptorSetVertLayout });
		gen.addShader(
			vktools::createShaderModule(devices.device, vktools::readFile("shaders/full_quad_vert.spv")),
			VK_SHADER_STAGE_VERTEX_BIT);
		gen.addShader(
			vktools::createShaderModule(devices.device, vktools::readFile("shaders/full_quad_bloom_frag.spv")),
			VK_SHADER_STAGE_FRAGMENT_BIT);
		
		uint32_t horizontalBlur = 0;
		VkSpecializationMapEntry specializationMapEntry{ 0, 0, sizeof(uint32_t) };
		VkSpecializationInfo specializationInfo{ 1, &specializationMapEntry, sizeof(uint32_t), &horizontalBlur };
		auto stages = gen.getShaderStageCreateInfo();
		stages[1].pSpecializationInfo = &specializationInfo;

		//generate pipeline layout & pipeline
		gen.generate(bloomRenderPass, &bloomPipelineVert, &bloomPipelineLayout);
		horizontalBlur = 1;
		gen.generate(bloomRenderPass, &bloomPipelineHorz, &bloomPipelineLayout);

		/*
		* final pass - full screen quad
		*/
		gen.resetAll();
		gen.setRasterizerInfo(VK_POLYGON_MODE_FILL, VK_CULL_MODE_NONE);
		gen.addDescriptorSetLayout({ descriptorSetLayout });
		gen.addShader(
			vktools::createShaderModule(devices.device, vktools::readFile("shaders/full_quad_vert.spv")),
			VK_SHADER_STAGE_VERTEX_BIT);
		gen.addShader(
			vktools::createShaderModule(devices.device, vktools::readFile("shaders/full_quad_frag.spv")),
			VK_SHADER_STAGE_FRAGMENT_BIT);

		//generate pipeline layout & pipeline
		gen.generate(renderPass, &pipeline, &pipelineLayout);


		/*
		* compute pipeline
		*/
		//compute pipeline layout
		VkPipelineLayoutCreateInfo computePipelineLayoutCreateInfo = 
			vktools::initializers::pipelineLayoutCreateInfo(&computeDescriptorSetLayout, 1);
		VK_CHECK_RESULT(vkCreatePipelineLayout(devices.device, &computePipelineLayoutCreateInfo, nullptr, &computePipelineLayout));

		//compute pipeline
		VkComputePipelineCreateInfo computePipelineCreateInfo{};
		computePipelineCreateInfo.sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO;
		computePipelineCreateInfo.layout = computePipelineLayout;
		VkShaderModule csCompute = vktools::createShaderModule(devices.device, vktools::readFile("shaders/particle_compute_comp.spv"));
		computePipelineCreateInfo.stage = vktools::initializers::pipelineShaderStageCreateInfo(VK_SHADER_STAGE_COMPUTE_BIT, csCompute);

		//specialization data for the compute shader
		struct SpecializationData {
			uint32_t sharedDataSize;
			float gravity;
			float power;
			float soften;
		} specializationData;

		specializationData.sharedDataSize = std::min((uint32_t)1024, (uint32_t)(devices.properties.limits.maxComputeSharedMemorySize / sizeof(glm::vec4)));
		specializationData.gravity = 0.0002f;
		specializationData.power = .75f;
		specializationData.soften = 0.05f;

		std::vector<VkSpecializationMapEntry> entry = {
			{0, offsetof(SpecializationData, sharedDataSize), sizeof(uint32_t)},
			{1, offsetof(SpecializationData, gravity), sizeof(float)},
			{2, offsetof(SpecializationData, power), sizeof(float)},
			{3, offsetof(SpecializationData, soften), sizeof(float)}
		};

		specializationInfo.mapEntryCount = static_cast<uint32_t>(entry.size());
		specializationInfo.pMapEntries = entry.data();
		specializationInfo.dataSize = sizeof(SpecializationData);
		specializationInfo.pData = &specializationData;

		computePipelineCreateInfo.stage.pSpecializationInfo = &specializationInfo;
		//create compute pipeline - 1st pass
		VK_CHECK_RESULT(vkCreateComputePipelines(devices.device, VK_NULL_HANDLE, 1, &computePipelineCreateInfo, nullptr, &computePipelineCompute));

		//create compute pipeline - 2nd pass
		VkShaderModule csUpdate = vktools::createShaderModule(devices.device, vktools::readFile("shaders/particle_update_comp.spv"));
		computePipelineCreateInfo.stage = vktools::initializers::pipelineShaderStageCreateInfo(VK_SHADER_STAGE_COMPUTE_BIT, csUpdate);
		VK_CHECK_RESULT(vkCreateComputePipelines(devices.device, VK_NULL_HANDLE, 1, &computePipelineCreateInfo, nullptr, &computePipelineUpdate));

		vkDestroyShaderModule(devices.device, csCompute, nullptr);
		vkDestroyShaderModule(devices.device, csUpdate, nullptr);
		LOG("created:\tpipelines");
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
		VkCommandBufferBeginInfo cmdBufBeginInfo{ VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO };

		std::vector<VkClearValue> hdrClearValues{};
		hdrClearValues.resize(3);
		hdrClearValues[0].color = clearColor;
		hdrClearValues[1].depthStencil = { 1.f, 0 };
		hdrClearValues.shrink_to_fit();

		VkClearValue brightClearValue{};
		brightClearValue.color = clearColor;

		std::vector<VkClearValue> clearValues{};
		clearValues.resize(2);
		clearValues[0].color = clearColor;
		clearValues[1].depthStencil = { 1.f, 0 };
		clearValues.shrink_to_fit();

		VkRenderPassBeginInfo hdrRenderPassBeginInfo{};
		hdrRenderPassBeginInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
		hdrRenderPassBeginInfo.renderPass = hdrRenderPass;
		hdrRenderPassBeginInfo.renderArea.offset = { 0, 0 };
		hdrRenderPassBeginInfo.renderArea.extent = swapchain.extent;
		hdrRenderPassBeginInfo.clearValueCount = static_cast<uint32_t>(hdrClearValues.size());
		hdrRenderPassBeginInfo.pClearValues = hdrClearValues.data();
		//hdrRenderPassBeginInfo.framebuffer = hdrFramebuffer.framebuffer;

		VkRenderPassBeginInfo brightRenderPassBeginInfo{};
		brightRenderPassBeginInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
		brightRenderPassBeginInfo.renderPass = brightRenderPass;
		brightRenderPassBeginInfo.renderArea.offset = { 0, 0 };
		brightRenderPassBeginInfo.renderArea.extent = swapchain.extent;
		brightRenderPassBeginInfo.clearValueCount = 1;
		brightRenderPassBeginInfo.pClearValues = &brightClearValue;
		//brightRenderPassBeginInfo.framebuffer = brightFramebuffer.framebuffer;

		VkRenderPassBeginInfo bloomVertRenderPassBeginInfo{};
		bloomVertRenderPassBeginInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
		bloomVertRenderPassBeginInfo.renderPass = bloomRenderPass;
		bloomVertRenderPassBeginInfo.renderArea.offset = { 0, 0 };
		bloomVertRenderPassBeginInfo.renderArea.extent = swapchain.extent;
		bloomVertRenderPassBeginInfo.clearValueCount = 1;
		bloomVertRenderPassBeginInfo.pClearValues = &brightClearValue;
		//bloomVertRenderPassBeginInfo.framebuffer = bloomFramebufferVert.framebuffer;

		VkRenderPassBeginInfo bloomHorzRenderPassBeginInfo{};
		bloomHorzRenderPassBeginInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
		bloomHorzRenderPassBeginInfo.renderPass = bloomRenderPass;
		bloomHorzRenderPassBeginInfo.renderArea.offset = { 0, 0 };
		bloomHorzRenderPassBeginInfo.renderArea.extent = swapchain.extent;
		bloomHorzRenderPassBeginInfo.clearValueCount = 1;
		bloomHorzRenderPassBeginInfo.pClearValues = &brightClearValue;
		//bloomHorzRenderPassBeginInfo.framebuffer = bloomFramebufferHorz.framebuffer;

		VkRenderPassBeginInfo renderPassBeginInfo{};
		renderPassBeginInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
		renderPassBeginInfo.renderPass = renderPass;
		renderPassBeginInfo.renderArea.offset = { 0, 0 };
		renderPassBeginInfo.renderArea.extent = swapchain.extent;
		renderPassBeginInfo.clearValueCount = static_cast<uint32_t>(clearValues.size());
		renderPassBeginInfo.pClearValues = clearValues.data();
		
		for (size_t i = 0; i < framebuffers.size() * MAX_FRAMES_IN_FLIGHT; ++i) {
			VK_CHECK_RESULT(vkBeginCommandBuffer(commandBuffers[i], &cmdBufBeginInfo));
			const size_t resourceIndex = i / framebuffers.size();
			/*
			* hdr pass
			*/
			hdrRenderPassBeginInfo.framebuffer = hdrFramebuffers[resourceIndex].framebuffer;
			vkCmdBeginRenderPass(commandBuffers[i], &hdrRenderPassBeginInfo, VK_SUBPASS_CONTENTS_INLINE);
			vktools::setViewportScissorDynamicStates(commandBuffers[i], swapchain.extent);

			vkCmdBindPipeline(commandBuffers[i], VK_PIPELINE_BIND_POINT_GRAPHICS, hdrPipeline);
			vkCmdBindDescriptorSets(commandBuffers[i], VK_PIPELINE_BIND_POINT_GRAPHICS, hdrPipelineLayout, 0, 1,
				&hdrDescriptorSets[resourceIndex], 0, nullptr);

			VkDeviceSize offsets = { 0 };
			vkCmdBindVertexBuffers(commandBuffers[i], 0, 1, &particleBuffer, &offsets);

			vkCmdDraw(commandBuffers[i], particleNum, 1, 0, 0);
			vkCmdEndRenderPass(commandBuffers[i]);

			//TODO: merge this pass to the previous renderpass (subpass)
			/*
			* extract bright color
			*/
			brightRenderPassBeginInfo.framebuffer = brightFramebuffers[resourceIndex].framebuffer;
			vkCmdBeginRenderPass(commandBuffers[i], &brightRenderPassBeginInfo, VK_SUBPASS_CONTENTS_INLINE);
			vktools::setViewportScissorDynamicStates(commandBuffers[i], swapchain.extent);

			vkCmdBindPipeline(commandBuffers[i], VK_PIPELINE_BIND_POINT_GRAPHICS, brightPipeline);
			vkCmdBindDescriptorSets(commandBuffers[i], VK_PIPELINE_BIND_POINT_GRAPHICS, brightPipelineLayout, 0, 1,
				&brightDescriptorSets[resourceIndex], 0, nullptr);

			vkCmdDraw(commandBuffers[i], 3, 1, 0, 0);
			vkCmdEndRenderPass(commandBuffers[i]);

			/*
			* bloom pass vert
			*/
			bloomVertRenderPassBeginInfo.framebuffer = bloomFramebufferVerts[resourceIndex].framebuffer;
			vkCmdBeginRenderPass(commandBuffers[i], &bloomVertRenderPassBeginInfo, VK_SUBPASS_CONTENTS_INLINE);
			vktools::setViewportScissorDynamicStates(commandBuffers[i], swapchain.extent);

			vkCmdBindPipeline(commandBuffers[i], VK_PIPELINE_BIND_POINT_GRAPHICS, bloomPipelineVert);
			vkCmdBindDescriptorSets(commandBuffers[i], VK_PIPELINE_BIND_POINT_GRAPHICS, bloomPipelineLayout, 0, 1,
				&bloomDescriptorSetsVert[resourceIndex], 0, nullptr);

			vkCmdDraw(commandBuffers[i], 3, 1, 0, 0);
			vkCmdEndRenderPass(commandBuffers[i]);

			/*
			* bloom pass horz
			*/
			bloomHorzRenderPassBeginInfo.framebuffer = bloomFramebufferHorzs[resourceIndex].framebuffer;
			vkCmdBeginRenderPass(commandBuffers[i], &bloomHorzRenderPassBeginInfo, VK_SUBPASS_CONTENTS_INLINE);
			vktools::setViewportScissorDynamicStates(commandBuffers[i], swapchain.extent);

			vkCmdBindPipeline(commandBuffers[i], VK_PIPELINE_BIND_POINT_GRAPHICS, bloomPipelineHorz);
			vkCmdBindDescriptorSets(commandBuffers[i], VK_PIPELINE_BIND_POINT_GRAPHICS, bloomPipelineLayout, 0, 1,
				&bloomDescriptorSetsHorz[resourceIndex], 0, nullptr);

			vkCmdDraw(commandBuffers[i], 3, 1, 0, 0);
			vkCmdEndRenderPass(commandBuffers[i]);

			/*
			* final pass - full screen quad
			*/
			size_t framebufferIndex = i % framebuffers.size();
			renderPassBeginInfo.framebuffer = framebuffers[framebufferIndex];
			vkCmdBeginRenderPass(commandBuffers[i], &renderPassBeginInfo, VK_SUBPASS_CONTENTS_INLINE);

			//dynamic states
			vktools::setViewportScissorDynamicStates(commandBuffers[i], swapchain.extent);

			vkCmdBindPipeline(commandBuffers[i], VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline);
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
	* create command buffers used in compute pipeline
	*/
	void createComputeCommandBuffers() {
		//create command buffers
		computeCommandBuffers.resize(MAX_FRAMES_IN_FLIGHT);
		VkCommandBufferAllocateInfo compCmdBufInfo{ VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO };
		compCmdBufInfo.commandPool = devices.commandPool;
		compCmdBufInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
		compCmdBufInfo.commandBufferCount = static_cast<uint32_t>(computeCommandBuffers.size());
		VK_CHECK_RESULT(vkAllocateCommandBuffers(devices.device, &compCmdBufInfo, computeCommandBuffers.data()));
		LOG("created:\t compute command buffers");
	}

	/*
	* destroy compute command buffer
	*/
	void destroyComputeCommandBuffers() {
		if (!computeCommandBuffers.empty()) {
			vkFreeCommandBuffers(devices.device, devices.commandPool,
				static_cast<uint32_t>(computeCommandBuffers.size()), computeCommandBuffers.data());
		}
	}

	/*
	* record compute command buffer
	*/
	void recordComputeCommandBuffers() {
		//record command buffers
		VkCommandBufferBeginInfo cmdBufBeginInfo{ VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO };
		for (int i = 0; i < MAX_FRAMES_IN_FLIGHT; ++i) {
			VK_CHECK_RESULT(vkBeginCommandBuffer(computeCommandBuffers[i], &cmdBufBeginInfo));
			//acquire barrrier...

			//first pass - compute particle gravity
			vkCmdBindPipeline(computeCommandBuffers[i], VK_PIPELINE_BIND_POINT_COMPUTE, computePipelineCompute);
			vkCmdBindDescriptorSets(computeCommandBuffers[i], VK_PIPELINE_BIND_POINT_COMPUTE, computePipelineLayout,
				0, 1, &computeDescriptorSets, 0, 0);
			uint32_t localGroupSize = 256;
			vkCmdDispatch(computeCommandBuffers[i], particleNum / localGroupSize, 1, 1); //local_group_x = 256

			//memory barrier
			VkBufferMemoryBarrier bufferBarrier{VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER};
			bufferBarrier.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
			bufferBarrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
			bufferBarrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
			bufferBarrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
			bufferBarrier.buffer = particleBuffer;
			bufferBarrier.size = particleBufferSize;

			vkCmdPipelineBarrier(computeCommandBuffers[i],
				VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
				VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
				0,
				0, nullptr,
				1, &bufferBarrier,
				0, nullptr);

			//second pass - update particle position
			vkCmdBindPipeline(computeCommandBuffers[i], VK_PIPELINE_BIND_POINT_COMPUTE, computePipelineUpdate);
			vkCmdDispatch(computeCommandBuffers[i], particleNum / localGroupSize, 1, 1);

			//release barrrier ...
			vkEndCommandBuffer(computeCommandBuffers[i]);
		}
	}

	/*
	* create MAX_FRAMES_IN_FLIGHT of ubos
	*/
	void createUniformBuffers() {
		//camera ubo
		VkDeviceSize cameraUBOSize = sizeof(CameraMatrices);
		cameraUBO.resize(MAX_FRAMES_IN_FLIGHT);
		cameraUBOMemories.resize(MAX_FRAMES_IN_FLIGHT);
		hdrUBO.resize(MAX_FRAMES_IN_FLIGHT);
		hdrUBOMemories.resize(MAX_FRAMES_IN_FLIGHT);

		VkBufferCreateInfo cameraUBOCreateInfo = 
			vktools::initializers::bufferCreateInfo(cameraUBOSize, VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT);
		VkBufferCreateInfo bloomUBOCreateInfo =
			vktools::initializers::bufferCreateInfo(VkDeviceSize(sizeof(HDRUBO)), VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT);

		for (size_t i = 0; i < MAX_FRAMES_IN_FLIGHT; ++i) {
			VK_CHECK_RESULT(vkCreateBuffer(devices.device, &cameraUBOCreateInfo, nullptr, &cameraUBO[i]));
			cameraUBOMemories[i] = devices.memoryAllocator.allocateBufferMemory(
					cameraUBO[i], VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);

			VK_CHECK_RESULT(vkCreateBuffer(devices.device, &bloomUBOCreateInfo, nullptr, &hdrUBO[i]));
			hdrUBOMemories[i] = devices.memoryAllocator.allocateBufferMemory(
				hdrUBO[i], VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
		}

		//compute shader ubo
		VkDeviceSize computeUBOSize = sizeof(ComputeUBO);
		VkBufferCreateInfo computeUBOCreateInfo = 
			vktools::initializers::bufferCreateInfo(computeUBOSize, VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT);
		VK_CHECK_RESULT(vkCreateBuffer(devices.device, &computeUBOCreateInfo, nullptr, &computeUBO));
		computeUBOMemories = devices.memoryAllocator.allocateBufferMemory(computeUBO,
			VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
	}

	/*
	* update matrices in ubo - rotates 90 degrees per second
	* 
	* @param currentFrame - index of uniform buffer vector
	*/
	void updateUniformBuffer(size_t currentFrame) {
		//graphics
		cameraUBOMemories[currentFrame].mapData(devices.device, &cameraMatrices);
		
		Imgui* imgui = static_cast<Imgui*>(imguiBase);
		hdrubo.enableHDR = imgui->userInput.enableHDR;
		hdrubo.enableBloom = imgui->userInput.enableBloom;
		hdrUBOMemories[currentFrame].mapData(devices.device, &hdrubo);
		//compute
		ubo.dt = dt;
		computeUBOMemories.mapData(devices.device, &ubo);
	}

	/*
	* set descriptor bindings & allocate destcriptor sets
	*/
	void createDescriptorSet() {
		//hdr pass
		hdrBindings.addBinding(0, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1, VK_SHADER_STAGE_VERTEX_BIT); //particle (vertex) buffer
		hdrBindings.addBinding(1, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_FRAGMENT_BIT); // camera ubo
		hdrDescriptorPool = hdrBindings.createDescriptorPool(devices.device, MAX_FRAMES_IN_FLIGHT);
		hdrDescriptorSetLayout = hdrBindings.createDescriptorSetLayout(devices.device);
		hdrDescriptorSets = vktools::allocateDescriptorSets(devices.device, hdrDescriptorSetLayout, hdrDescriptorPool, MAX_FRAMES_IN_FLIGHT);

		//bright color pass
		brightBindings.addBinding(0, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_FRAGMENT_BIT); //image from hdr pass
		brightDescriptorPool = brightBindings.createDescriptorPool(devices.device, MAX_FRAMES_IN_FLIGHT);
		brightDescriptorSetLayout = brightBindings.createDescriptorSetLayout(devices.device);
		brightDescriptorSets = vktools::allocateDescriptorSets(devices.device, brightDescriptorSetLayout, brightDescriptorPool, MAX_FRAMES_IN_FLIGHT);

		//bloom pass
		bloomBindingsVert.addBinding(0, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_FRAGMENT_BIT); //image from bright color pass
		bloomDescriptorVertPool = bloomBindingsVert.createDescriptorPool(devices.device, MAX_FRAMES_IN_FLIGHT);
		bloomDescriptorSetVertLayout = bloomBindingsVert.createDescriptorSetLayout(devices.device);
		bloomDescriptorSetsVert = vktools::allocateDescriptorSets(devices.device, bloomDescriptorSetVertLayout, bloomDescriptorVertPool, MAX_FRAMES_IN_FLIGHT);
		
		bloomBindingsHorz.addBinding(0, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_FRAGMENT_BIT); //image from bright color pass
		bloomDescriptorHorzPool = bloomBindingsHorz.createDescriptorPool(devices.device, MAX_FRAMES_IN_FLIGHT);
		bloomDescriptorSetHorzLayout = bloomBindingsHorz.createDescriptorSetLayout(devices.device);
		bloomDescriptorSetsHorz = vktools::allocateDescriptorSets(devices.device, bloomDescriptorSetHorzLayout, bloomDescriptorHorzPool, MAX_FRAMES_IN_FLIGHT);

		//graphics
		bindings.addBinding(0, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_FRAGMENT_BIT); //image from hdr pass
		bindings.addBinding(1, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_FRAGMENT_BIT); //image bloom pass
		bindings.addBinding(2, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1, VK_SHADER_STAGE_FRAGMENT_BIT); //image bloom pass
		descriptorPool = bindings.createDescriptorPool(devices.device, MAX_FRAMES_IN_FLIGHT);
		descriptorSetLayout = bindings.createDescriptorSetLayout(devices.device);
		descriptorSets = vktools::allocateDescriptorSets(devices.device, descriptorSetLayout, descriptorPool, MAX_FRAMES_IN_FLIGHT);

		//compute
		computeBindings.addBinding(0, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_COMPUTE_BIT);
		computeBindings.addBinding(1, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1, VK_SHADER_STAGE_COMPUTE_BIT);
		computeDescriptorPool = computeBindings.createDescriptorPool(devices.device, 1);
		computeDescriptorSetLayout = computeBindings.createDescriptorSetLayout(devices.device);
		computeDescriptorSets = vktools::allocateDescriptorSets(devices.device, computeDescriptorSetLayout, computeDescriptorPool, 1).front();
	}

	/*
	* update descriptor set
	*/
	void updateDescriptorSets() {
		for (size_t i = 0; i < MAX_FRAMES_IN_FLIGHT; ++i) {
			std::vector<VkWriteDescriptorSet> writes;
			//hdr pass
			VkDescriptorBufferInfo cameraUBObufferInfo{ cameraUBO[i], 0, sizeof(CameraMatrices) };
			writes.push_back(hdrBindings.makeWrite(hdrDescriptorSets[i], 0, &cameraUBObufferInfo));
			writes.push_back(hdrBindings.makeWrite(hdrDescriptorSets[i], 1, &particleTex.descriptor));

			//bright color pass
			VkDescriptorImageInfo hdrImageInfo = { offscreenSampler,
				hdrFramebuffers[i].attachments[0].imageView,
				VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL };
			writes.push_back(brightBindings.makeWrite(brightDescriptorSets[i], 0, &hdrImageInfo));

			//bloom pass vertical blur
			VkDescriptorImageInfo brightImageInfo = { offscreenSampler,
				brightFramebuffers[i].attachments[0].imageView,
				VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL };
			writes.push_back(bloomBindingsVert.makeWrite(bloomDescriptorSetsVert[i], 0, &brightImageInfo));

			//bloom pass vertical blur
			VkDescriptorImageInfo bloomVertImageInfo = { offscreenSampler,
				bloomFramebufferVerts[i].attachments[0].imageView,
				VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL };
			writes.push_back(bloomBindingsHorz.makeWrite(bloomDescriptorSetsHorz[i], 0, &bloomVertImageInfo));

			//graphics
			VkDescriptorImageInfo bloomHorzImageInfo = { offscreenSampler,
				bloomFramebufferHorzs[i].attachments[0].imageView,
				VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL };

			writes.push_back(bindings.makeWrite(descriptorSets[i], 0, &hdrImageInfo));
			writes.push_back(bindings.makeWrite(descriptorSets[i], 1, &bloomHorzImageInfo));

			VkDescriptorBufferInfo bloomUBObufferInfo{ hdrUBO[i], 0, sizeof(HDRUBO) };
			writes.push_back(bindings.makeWrite(descriptorSets[i], 2, &bloomUBObufferInfo));
			vkUpdateDescriptorSets(devices.device, static_cast<uint32_t>(writes.size()), writes.data(), 0, nullptr);
		}

		//compute
		std::vector<VkWriteDescriptorSet> writes;
		VkDescriptorBufferInfo vertexBufferInfo{ particleBuffer, 0, particleBufferSize };
		VkDescriptorBufferInfo computeUBOInfo{ computeUBO, 0, sizeof(ComputeUBO)};
		writes.push_back(computeBindings.makeWrite(computeDescriptorSets, 0, &vertexBufferInfo));
		writes.push_back(computeBindings.makeWrite(computeDescriptorSets, 1, &computeUBOInfo));

		//update all of descriptor sets
		vkUpdateDescriptorSets(devices.device, static_cast<uint32_t>(writes.size()), writes.data(), 0, nullptr);
	}
};

//entry point
RUN_APPLICATION_MAIN(VulkanApp, 1200, 800, "project3_n_body_simulation");
