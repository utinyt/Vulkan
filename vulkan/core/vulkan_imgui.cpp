#include <array>
#include <imgui/imgui.h>
#include "vulkan_imgui.h"
#include <filesystem>

/*
* init context & style & resources
* 
* @param devices - abstracted vulkan device (physical / logical) pointer
*/
void Imgui::init(VulkanDevice* devices, int width, int height,
	VkRenderPass renderPass, uint32_t MAX_FRAMES_IN_FLIGHT) {
	this->devices = devices;
	ImGui::CreateContext();

	//color scheme
	ImGuiStyle& style = ImGui::GetStyle();
	style.Colors[ImGuiCol_TitleBg]			= ImVec4(1.f, 0.f, 0.f, 0.6f);
	style.Colors[ImGuiCol_TitleBgActive]	= ImVec4(1.f, 0.f, 0.f, 0.8f);
	style.Colors[ImGuiCol_MenuBarBg]		= ImVec4(1.f, 0.f, 0.f, 0.4f);
	style.Colors[ImGuiCol_Header]			= ImVec4(1.f, 0.f, 0.f, 0.4f);
	style.Colors[ImGuiCol_CheckMark]		= ImVec4(0.f, 1.f, 0.f, 1.0f);

	//dimensions
	ImGuiIO& io = ImGui::GetIO();
	io.DisplaySize = ImVec2(static_cast<float>(width), static_cast<float>(height));
	io.DisplayFramebufferScale = ImVec2(1.f, 1.f);

	//load font texture
	unsigned char* fontData;
	int texWidth, texHeight;
	io.Fonts->GetTexDataAsRGBA32(&fontData, &texWidth, &texHeight);
	fontImage.load(devices,
		fontData,
		texWidth,
		texHeight,
		texWidth * texHeight * 4 * sizeof(char),
		VK_FORMAT_R8G8B8A8_UNORM);
	
	//descriptor pool & layout & allocate
	bindings.addBinding(0, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_FRAGMENT_BIT);
	descriptorPool = bindings.createDescriptorPool(devices->device, MAX_FRAMES_IN_FLIGHT);
	descriptorSetLayout = bindings.createDescriptorSetLayout(devices->device);
	descriptorSets = vktools::allocateDescriptorSets(devices->device, descriptorSetLayout, descriptorPool, MAX_FRAMES_IN_FLIGHT);

	//update descriptor sets
	std::vector<VkWriteDescriptorSet> writes;
	for (uint32_t i = 0; i < MAX_FRAMES_IN_FLIGHT; ++i) {
		writes.emplace_back(bindings.makeWrite(descriptorSets[i], 0, &fontImage.descriptor));
	}
	vkUpdateDescriptorSets(devices->device, static_cast<uint32_t>(writes.size()), writes.data(), 0, nullptr);

	//pipeline 
	VkPipelineLayoutCreateInfo pipelineLayoutInfo = 
		vktools::initializers::pipelineLayoutCreateInfo(1, &descriptorSetLayout);
	
	VkPushConstantRange pushConstantRange{ VK_SHADER_STAGE_VERTEX_BIT, sizeof(PushConstBlock), 0 };
	pipelineLayoutInfo.pushConstantRangeCount = 1;
	pipelineLayoutInfo.pPushConstantRanges = &pushConstantRange;

	VK_CHECK_RESULT(vkCreatePipelineLayout(devices->device, &pipelineLayoutInfo, nullptr, &pipelineLayout));

	VkPipelineInputAssemblyStateCreateInfo inputAssemblyInfo =
		vktools::initializers::pipelineInputAssemblyStateCreateInfo();

	VkPipelineRasterizationStateCreateInfo rasterizationInfo =
		vktools::initializers::pipelineRasterizationStateCreateInfo(VK_POLYGON_MODE_FILL, VK_CULL_MODE_NONE);

	VkPipelineColorBlendAttachmentState blendAttachmentState =
		vktools::initializers::pipelineColorBlendAttachment(VK_TRUE);

	VkPipelineColorBlendStateCreateInfo colorBlendInfo =
		vktools::initializers::pipelineColorBlendStateCreateInfo(1, &blendAttachmentState);

	VkPipelineDepthStencilStateCreateInfo depthStencilStateInfo =
		vktools::initializers::pipelineDepthStencilStateCreateInfo(VK_FALSE, VK_FALSE, VK_COMPARE_OP_LESS_OR_EQUAL);

	VkPipelineViewportStateCreateInfo viewportStateInfo =
		vktools::initializers::pipelineViewportStateCreateInfo();

	VkPipelineMultisampleStateCreateInfo multisampleStateInfo =
		vktools::initializers::pipelineMultisampleStateCreateInfo();

	VkDynamicState dynamicStates[] = { VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR };
	VkPipelineDynamicStateCreateInfo dynamicStateInfo =
		vktools::initializers::pipelineDynamicStateCreateInfo(dynamicStates, 2);

	VkVertexInputBindingDescription vertexInputBinding = {
		vktools::initializers::vertexInputBindingDescription(0, sizeof(ImDrawVert))
	};

	std::array<VkVertexInputAttributeDescription, 3> vertexInputAttributes = {
		vktools::initializers::vertexInputAttributeDescription(0, 0, VK_FORMAT_R32G32_SFLOAT, offsetof(ImDrawVert, pos)),
		vktools::initializers::vertexInputAttributeDescription(0, 1, VK_FORMAT_R32G32_SFLOAT, offsetof(ImDrawVert, uv)),
		vktools::initializers::vertexInputAttributeDescription(0, 2, VK_FORMAT_R8G8B8A8_UNORM, offsetof(ImDrawVert, col))
	};

	VkPipelineVertexInputStateCreateInfo vertexInputStateInfo =
		vktools::initializers::pipelineVertexInputStateCreateInfo(&vertexInputBinding, 1,
			vertexInputAttributes.data(), static_cast<uint32_t>(vertexInputAttributes.size()));

	std::array<VkPipelineShaderStageCreateInfo, 2> shaderStages{};
	VkShaderModule vertexShader = vktools::createShaderModule(devices->device,
		vktools::readFile("../../core/shaders/imgui_vert.spv"));
	VkShaderModule fragmentShader = vktools::createShaderModule(devices->device,
		vktools::readFile("../../core/shaders/imgui_frag.spv"));
	shaderStages[0] = vktools::initializers::pipelineShaderStageCreateInfo(VK_SHADER_STAGE_VERTEX_BIT, vertexShader);
	shaderStages[1] = vktools::initializers::pipelineShaderStageCreateInfo(VK_SHADER_STAGE_FRAGMENT_BIT, fragmentShader);

	VkGraphicsPipelineCreateInfo pipelineInfo 
		= vktools::initializers::graphicsPipelineCreateInfo(pipelineLayout, renderPass);
	pipelineInfo.pVertexInputState		= &vertexInputStateInfo;
	pipelineInfo.pInputAssemblyState	= &inputAssemblyInfo;
	pipelineInfo.pRasterizationState	= &rasterizationInfo;
	pipelineInfo.pColorBlendState		= &colorBlendInfo;
	pipelineInfo.pMultisampleState		= &multisampleStateInfo;
	pipelineInfo.pViewportState			= &viewportStateInfo;
	pipelineInfo.pDepthStencilState		= &depthStencilStateInfo;
	pipelineInfo.pDynamicState			= &dynamicStateInfo;
	pipelineInfo.stageCount				= static_cast<uint32_t>(shaderStages.size());
	pipelineInfo.pStages				= shaderStages.data();
	VK_CHECK_RESULT(vkCreateGraphicsPipelines(devices->device, {}, 1, &pipelineInfo, nullptr, &pipeline));

	vkDestroyShaderModule(devices->device, vertexShader, nullptr);
	vkDestroyShaderModule(devices->device, fragmentShader, nullptr);
}

/*
* destroy all resources
*/
void Imgui::cleanup() {
	ImGui::DestroyContext();
	//vertex & index buffer
	devices->memoryAllocator.freeBufferMemory(vertexBuffer, 
		VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
	vkDestroyBuffer(devices->device, vertexBuffer, nullptr);
	devices->memoryAllocator.freeBufferMemory(indexBuffer,
		VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
	vkDestroyBuffer(devices->device, indexBuffer, nullptr);
	//image
	fontImage.cleanup();
	//pipeline
	vkDestroyPipeline(devices->device, pipeline, nullptr);
	vkDestroyPipelineLayout(devices->device, pipelineLayout, nullptr);
	//descriptor
	vkDestroyDescriptorPool(devices->device, descriptorPool, nullptr);
	vkDestroyDescriptorSetLayout(devices->device, descriptorSetLayout, nullptr);
}

/*
* start  imgui frame
*/
void Imgui::newFrame() {
	ImGui::NewFrame();
	ImGui::ShowDemoWindow();
	ImGui::Render();
}

/*
* update vertex & index buffer
*/
void Imgui::updateBuffers() {
	ImDrawData* imDrawData = ImGui::GetDrawData();

	VkDeviceSize vertexBufferSize = imDrawData->TotalVtxCount * sizeof(ImDrawVert);
	VkDeviceSize indexBufferSize = imDrawData->TotalIdxCount* sizeof(ImDrawIdx);

	if (vertexBufferSize == 0 || indexBufferSize == 0) {
		return;
	}

	//update buffers only if vertex or index count has been changed
	//vertex buffer
	if (vertexBuffer == VK_NULL_HANDLE || vertexCount != imDrawData->TotalVtxCount) {
		devices->memoryAllocator.freeBufferMemory(vertexBuffer,
			VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
		vkDestroyBuffer(devices->device, vertexBuffer, nullptr);
		vertexMem = devices->createBuffer(vertexBuffer, vertexBufferSize, VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
			VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
		vertexCount = imDrawData->TotalVtxCount;
	}
	//index buffer
	if (indexBuffer == VK_NULL_HANDLE || indexCount != imDrawData->TotalIdxCount) {
		devices->memoryAllocator.freeBufferMemory(indexBuffer,
			VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
		vkDestroyBuffer(devices->device, indexBuffer, nullptr);
		indexMem = devices->createBuffer(indexBuffer, indexBufferSize, VK_BUFFER_USAGE_INDEX_BUFFER_BIT,
			VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
		indexCount = imDrawData->TotalIdxCount;
	}

	ImDrawVert* vtxDst = (ImDrawVert*)vertexMem.getHandle(devices->device);
	ImDrawIdx* idxDst = (ImDrawIdx*)indexMem.getHandle(devices->device);

	for (int n = 0; n < imDrawData->CmdListsCount; ++n) {
		const ImDrawList* cmd_list = imDrawData->CmdLists[n];
		memcpy(vtxDst, cmd_list->VtxBuffer.Data, cmd_list->VtxBuffer.Size * sizeof(ImDrawVert));
		memcpy(idxDst, cmd_list->IdxBuffer.Data, cmd_list->IdxBuffer.Size * sizeof(ImDrawIdx)); 
		vtxDst += cmd_list->VtxBuffer.Size;
		idxDst += cmd_list->IdxBuffer.Size;
	}

	vertexMem.unmap(devices->device);
	indexMem.unmap(devices->device);
}

/*
* record imgui draw commands
* 
* @param cmdBuf - command buffer to record
* @param currentFrame - index of descriptor set (0 <= currentFrame < MAX_FRAMES_IN_FLIGHT)
*/
void Imgui::drawFrame(VkCommandBuffer cmdBuf, size_t currentFrame) {
	vkCmdBindDescriptorSets(cmdBuf, VK_PIPELINE_BIND_POINT_GRAPHICS,
		pipelineLayout, 0, 1, &descriptorSets[currentFrame], 0, nullptr);
	vkCmdBindPipeline(cmdBuf, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline);

	ImGuiIO& io = ImGui::GetIO();
	VkViewport viewport = VkViewport{0, 0, io.DisplaySize.x, io.DisplaySize.y, 0.f, 1.f };
	vkCmdSetViewport(cmdBuf, 0, 1, &viewport);

	pushConstBlock.scale = glm::vec2(2.f / io.DisplaySize.x, 2.f / io.DisplaySize.y);
	pushConstBlock.translate = glm::vec2(-1.f);
	vkCmdPushConstants(cmdBuf, pipelineLayout, VK_SHADER_STAGE_VERTEX_BIT, 0,
		sizeof(PushConstBlock), &pushConstBlock);

	ImDrawData* imDrawData = ImGui::GetDrawData();
	int32_t vertexOffset = 0;
	int32_t indexOffset = 0;

	if (imDrawData->CmdListsCount > 0) {
		VkDeviceSize offsets[1] = { 0 };
		vkCmdBindVertexBuffers(cmdBuf, 0, 1, &vertexBuffer, offsets);
		vkCmdBindIndexBuffer(cmdBuf, indexBuffer, 0, VK_INDEX_TYPE_UINT16);

		for (int32_t i = 0; i < imDrawData->CmdListsCount; ++i) {
			const ImDrawList* cmd_list = imDrawData->CmdLists[i];
			for (int32_t j = 0; j < cmd_list->CmdBuffer.size(); ++j) {
				const ImDrawCmd* pcmd = &cmd_list->CmdBuffer[j];
				VkRect2D scissorRect;
				scissorRect.offset.x = std::max((int32_t)(pcmd->ClipRect.x), 0);
				scissorRect.offset.y = std::max((int32_t)(pcmd->ClipRect.y), 0);
				scissorRect.extent.width = (uint32_t)(pcmd->ClipRect.z - pcmd->ClipRect.x);
				scissorRect.extent.height = (uint32_t)(pcmd->ClipRect.w - pcmd->ClipRect.y);
				vkCmdSetScissor(cmdBuf, 0, 1, &scissorRect);
				vkCmdDrawIndexed(cmdBuf, pcmd->ElemCount, 1, indexOffset, vertexOffset, 0);
				indexOffset += pcmd->ElemCount;
			}
			vertexOffset += cmd_list->VtxBuffer.Size;
		}
	}
}
