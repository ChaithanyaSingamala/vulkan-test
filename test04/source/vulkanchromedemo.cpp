/*
* Vulkan Example - Scene rendering
*
* Copyright (C) 2016 by Sascha Willems - www.saschawillems.de
*
* This code is licensed under the MIT license (MIT) (http://opensource.org/licenses/MIT)
*
* Summary:
* Renders a scene made of multiple parts with different materials and textures.
*
* The example loads a scene made up of multiple parts into one vertex and index buffer to only
* have one (big) memory allocation. In Vulkan it's advised to keep number of memory allocations
* down and try to allocate large blocks of memory at once instead of having many small allocations.
*
* Every part has a separate material and multiple descriptor sets (set = x layout qualifier in GLSL)
* are used to bind a uniform buffer with global matrices and the part's material's sampler at once.
*
* To demonstrate another way of passing data the example also uses push constants for passing
* material properties.
*
* Note that this example is just one way of rendering a scene made up of multiple parts in Vulkan.
*/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <vector>

#include <thread>
#include <random>

#define GLM_FORCE_RADIANS
#define GLM_FORCE_DEPTH_ZERO_TO_ONE
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>

#include <assimp/Importer.hpp> 
#include <assimp/scene.h>     
#include <assimp/postprocess.h>
#include <assimp/cimport.h>

#include <vulkan/vulkan.h>
#include "vulkanexamplebase.h"
#include "VulkanTexture.hpp"
#include "VulkanDevice.hpp"
#include "VulkanBuffer.hpp"

#include "threadpool.hpp"
#include "frustum.hpp"

#define VERTEX_BUFFER_BIND_ID 0
#define ENABLE_VALIDATION true
#define SAMPLE_COUNT VK_SAMPLE_COUNT_8_BIT

struct {
	struct {
		VkImage image;
		VkImageView view;
		VkDeviceMemory memory;
	} color;
	struct {
		VkImage image;
		VkImageView view;
		VkDeviceMemory memory;
	} depth;
} multisampleTarget;


// Vertex layout used in this example
struct Vertex {
	glm::vec3 pos;
	glm::vec3 normal;
	glm::vec2 uv;
	glm::vec3 color;
};

// Scene related structs

// Shader properites for a material
// Will be passed to the shaders using push constant
struct SceneMaterialProperites
{
	glm::vec4 ambient;
	glm::vec4 diffuse;
	glm::vec4 specular;
	float opacity;
};

// Stores info on the materials used in the scene
struct SceneMaterial
{
	std::string name;
	// Material properties
	SceneMaterialProperites properties;
	// The example only uses a diffuse channel
	vks::Texture2D diffuse;
	// The material's descriptor contains the material descriptors
	VkDescriptorSet descriptorSet;
	// Pointer to the pipeline used by this material
	VkPipeline *pipeline;
};

// Stores per-mesh Vulkan resources
struct ScenePart
{
	// Index of first index in the scene buffer
	uint32_t indexBase;
	uint32_t indexCount;

	// Pointer to the material used by this mesh
	SceneMaterial *material;
};

// Class for loading the scene and generating all Vulkan resources
class Scene
{
private:
	vks::VulkanDevice *vulkanDevice;
	VkQueue queue;

	VkDescriptorPool descriptorPool;

	// We will be using separate descriptor sets (and bindings)
	// for material and scene related uniforms
	struct
	{
		VkDescriptorSetLayout material;
		VkDescriptorSetLayout scene;
	} descriptorSetLayouts;

	// We will be using one single index and vertex buffer
	// containing vertices and indices for all meshes in the scene
	// This allows us to keep memory allocations down
	vks::Buffer vertexBuffer;
	vks::Buffer indexBuffer;

	VkDescriptorSet descriptorSetScene;

	const aiScene* aScene;

	// Get materials from the assimp scene and map to our scene structures
	void loadMaterials()
	{
		materials.resize(aScene->mNumMaterials);

		for (size_t i = 0; i < materials.size(); i++)
		{
			materials[i] = {};

			aiString name;
			aScene->mMaterials[i]->Get(AI_MATKEY_NAME, name);

			// Properties
			aiColor4D color;
			aScene->mMaterials[i]->Get(AI_MATKEY_COLOR_AMBIENT, color);
			materials[i].properties.ambient = glm::make_vec4(&color.r) + glm::vec4(0.1f);
			aScene->mMaterials[i]->Get(AI_MATKEY_COLOR_DIFFUSE, color);
			materials[i].properties.diffuse = glm::make_vec4(&color.r);
			aScene->mMaterials[i]->Get(AI_MATKEY_COLOR_SPECULAR, color);
			materials[i].properties.specular = glm::make_vec4(&color.r);
			aScene->mMaterials[i]->Get(AI_MATKEY_OPACITY, materials[i].properties.opacity);

			if ((materials[i].properties.opacity) > 0.0f)
				materials[i].properties.specular = glm::vec4(0.0f);

			materials[i].name = name.C_Str();
			std::cout << "Material \"" << materials[i].name << "\"" << std::endl;

			// Textures
			std::string texFormatSuffix;
			VkFormat texFormat;
			// Get supported compressed texture format
			if (vulkanDevice->features.textureCompressionBC) {
				texFormatSuffix = "_bc3_unorm";
				texFormat = VK_FORMAT_BC3_UNORM_BLOCK;
			}
			else if (vulkanDevice->features.textureCompressionASTC_LDR) {
				texFormatSuffix = "_astc_8x8_unorm";
				texFormat = VK_FORMAT_ASTC_8x8_UNORM_BLOCK;
			}
			else if (vulkanDevice->features.textureCompressionETC2) {
				texFormatSuffix = "_etc2_unorm";
				texFormat = VK_FORMAT_ETC2_R8G8B8_UNORM_BLOCK;
			}
			else {
				vks::tools::exitFatal("Device does not support any compressed texture format!", "Error");
			}

			//cks //not sure of compression
			texFormat = VK_FORMAT_R8G8B8A8_UNORM;
			texFormatSuffix = "";

			aiString texturefile;
			// Diffuse
			aScene->mMaterials[i]->GetTexture(aiTextureType_DIFFUSE, 0, &texturefile);
			if (aScene->mMaterials[i]->GetTextureCount(aiTextureType_DIFFUSE) > 0)
			{
				std::cout << "  Diffuse: \"" << texturefile.C_Str() << "\"" << std::endl;
				std::string fileName = std::string(texturefile.C_Str());
				std::replace(fileName.begin(), fileName.end(), '\\', '/');
				fileName.insert(fileName.find(".ktx"), texFormatSuffix);
				materials[i].diffuse.loadFromFile(assetPath + fileName, texFormat, vulkanDevice, queue);
			}
			else
			{
				std::cout << "  Material has no diffuse, using dummy texture!" << std::endl;
				// todo : separate pipeline and layout
				materials[i].diffuse.loadFromFile(assetPath + "dummy_rgba_unorm.ktx", VK_FORMAT_R8G8B8A8_UNORM, vulkanDevice, queue);
			}

			// For scenes with multiple textures per material we would need to check for additional texture types, e.g.:
			// aiTextureType_HEIGHT, aiTextureType_OPACITY, aiTextureType_SPECULAR, etc.

			// Assign pipeline
			int val = materials[i].name.find("alpha");
			if(val >= 0)
				materials[i].pipeline = &pipelines.blending;
			else
			{
				val = materials[i].name.find("mask");
				if (val >= 0)
					materials[i].pipeline = &pipelines.bg;
				else
					materials[i].pipeline = &pipelines.solid;
			}
			//materials[i].pipeline = (materials[i].properties.opacity == 0.0f) ? &pipelines.solid : &pipelines.blending;
		}

		// Generate descriptor sets for the materials

		// Descriptor pool
		std::vector<VkDescriptorPoolSize> poolSizes;
		poolSizes.push_back(vks::initializers::descriptorPoolSize(VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, static_cast<uint32_t>(materials.size())));
		poolSizes.push_back(vks::initializers::descriptorPoolSize(VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, static_cast<uint32_t>(materials.size())));

		VkDescriptorPoolCreateInfo descriptorPoolInfo =
			vks::initializers::descriptorPoolCreateInfo(
				static_cast<uint32_t>(poolSizes.size()),
				poolSizes.data(),
				static_cast<uint32_t>(materials.size()) + 1);

		VK_CHECK_RESULT(vkCreateDescriptorPool(vulkanDevice->logicalDevice, &descriptorPoolInfo, nullptr, &descriptorPool));

		// Descriptor set and pipeline layouts
		std::vector<VkDescriptorSetLayoutBinding> setLayoutBindings;
		VkDescriptorSetLayoutCreateInfo descriptorLayout;

		// Set 0: Scene matrices
		setLayoutBindings.push_back(vks::initializers::descriptorSetLayoutBinding(
			VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
			VK_SHADER_STAGE_VERTEX_BIT,
			0));
		descriptorLayout = vks::initializers::descriptorSetLayoutCreateInfo(
			setLayoutBindings.data(),
			static_cast<uint32_t>(setLayoutBindings.size()));
		VK_CHECK_RESULT(vkCreateDescriptorSetLayout(vulkanDevice->logicalDevice, &descriptorLayout, nullptr, &descriptorSetLayouts.scene));

		// Set 1: Material data
		setLayoutBindings.clear();
		setLayoutBindings.push_back(vks::initializers::descriptorSetLayoutBinding(
			VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
			VK_SHADER_STAGE_FRAGMENT_BIT,
			0));
		VK_CHECK_RESULT(vkCreateDescriptorSetLayout(vulkanDevice->logicalDevice, &descriptorLayout, nullptr, &descriptorSetLayouts.material));

		// Setup pipeline layout
		std::array<VkDescriptorSetLayout, 2> setLayouts = { descriptorSetLayouts.scene, descriptorSetLayouts.material };
		VkPipelineLayoutCreateInfo pipelineLayoutCreateInfo = vks::initializers::pipelineLayoutCreateInfo(setLayouts.data(), static_cast<uint32_t>(setLayouts.size()));

		// We will be using a push constant block to pass material properties to the fragment shaders
		VkPushConstantRange pushConstantRange = vks::initializers::pushConstantRange(
			VK_SHADER_STAGE_FRAGMENT_BIT,
			sizeof(SceneMaterialProperites),
			0);
		pipelineLayoutCreateInfo.pushConstantRangeCount = 1;
		pipelineLayoutCreateInfo.pPushConstantRanges = &pushConstantRange;

		VK_CHECK_RESULT(vkCreatePipelineLayout(vulkanDevice->logicalDevice, &pipelineLayoutCreateInfo, nullptr, &pipelineLayout));

		// Material descriptor sets
		for (size_t i = 0; i < materials.size(); i++)
		{
			// Descriptor set
			VkDescriptorSetAllocateInfo allocInfo =
				vks::initializers::descriptorSetAllocateInfo(
					descriptorPool,
					&descriptorSetLayouts.material,
					1);

			VK_CHECK_RESULT(vkAllocateDescriptorSets(vulkanDevice->logicalDevice, &allocInfo, &materials[i].descriptorSet));

			std::vector<VkWriteDescriptorSet> writeDescriptorSets;

			// todo : only use image sampler descriptor set and use one scene ubo for matrices

			// Binding 0: Diffuse texture
			writeDescriptorSets.push_back(vks::initializers::writeDescriptorSet(
				materials[i].descriptorSet,
				VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
				0,
				&materials[i].diffuse.descriptor));

			vkUpdateDescriptorSets(vulkanDevice->logicalDevice, static_cast<uint32_t>(writeDescriptorSets.size()), writeDescriptorSets.data(), 0, NULL);
		}

		// Scene descriptor set
		VkDescriptorSetAllocateInfo allocInfo =
			vks::initializers::descriptorSetAllocateInfo(
				descriptorPool,
				&descriptorSetLayouts.scene,
				1);
		VK_CHECK_RESULT(vkAllocateDescriptorSets(vulkanDevice->logicalDevice, &allocInfo, &descriptorSetScene));

		std::vector<VkWriteDescriptorSet> writeDescriptorSets;
		// Binding 0 : Vertex shader uniform buffer
		writeDescriptorSets.push_back(vks::initializers::writeDescriptorSet(
			descriptorSetScene,
			VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
			0,
			&uniformBuffer.descriptor));

		vkUpdateDescriptorSets(vulkanDevice->logicalDevice, static_cast<uint32_t>(writeDescriptorSets.size()), writeDescriptorSets.data(), 0, NULL);
	}

	// Load all meshes from the scene and generate the buffers for rendering them
	void loadMeshes(VkCommandBuffer copyCmd)
	{
		std::vector<Vertex> vertices;
		std::vector<uint32_t> indices;
		uint32_t indexBase = 0;

		meshes.resize(aScene->mNumMeshes);
		for (uint32_t i = 0; i < meshes.size(); i++)
		{
			aiMesh *aMesh = aScene->mMeshes[i];

			std::cout << "Mesh \"" << aMesh->mName.C_Str() << "\"" << std::endl;
			std::cout << "	Material: \"" << materials[aMesh->mMaterialIndex].name << "\"" << std::endl;
			std::cout << "	Faces: " << aMesh->mNumFaces << std::endl;

			meshes[i].material = &materials[aMesh->mMaterialIndex];
			meshes[i].indexBase = indexBase;
			meshes[i].indexCount = aMesh->mNumFaces * 3;

			// Vertices
			bool hasUV = aMesh->HasTextureCoords(0);
			bool hasColor = aMesh->HasVertexColors(0);
			bool hasNormals = aMesh->HasNormals();

			for (uint32_t v = 0; v < aMesh->mNumVertices; v++)
			{
				Vertex vertex;
				vertex.pos = glm::make_vec3(&aMesh->mVertices[v].x);
				vertex.pos.y = -vertex.pos.y;
				vertex.uv = hasUV ? glm::make_vec2(&aMesh->mTextureCoords[0][v].x) : glm::vec2(0.0f);
				vertex.normal = hasNormals ? glm::make_vec3(&aMesh->mNormals[v].x) : glm::vec3(0.0f);
				vertex.normal.y = -vertex.normal.y;
				vertex.color = hasColor ? glm::make_vec3(&aMesh->mColors[0][v].r) : glm::vec3(1.0f);
				vertices.push_back(vertex);
			}

			// Indices
			for (uint32_t f = 0; f < aMesh->mNumFaces; f++)
			{
				for (uint32_t j = 0; j < 3; j++)
				{
					indices.push_back(aMesh->mFaces[f].mIndices[j]);
				}
			}

			indexBase += aMesh->mNumFaces * 3;
		}

		// Create buffers
		// For better performance we only create one index and vertex buffer to keep number of memory allocations down
		size_t vertexDataSize = vertices.size() * sizeof(Vertex);
		size_t indexDataSize = indices.size() * sizeof(uint32_t);

		vks::Buffer vertexStaging, indexStaging;

		// Vertex buffer
		// Staging buffer
		VK_CHECK_RESULT(vulkanDevice->createBuffer(
			VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
			VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
			&vertexStaging,
			static_cast<uint32_t>(vertexDataSize),
			vertices.data()));
		// Target
		VK_CHECK_RESULT(vulkanDevice->createBuffer(
			VK_BUFFER_USAGE_VERTEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
			VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
			&vertexBuffer,
			static_cast<uint32_t>(vertexDataSize)));

		// Index buffer
		VK_CHECK_RESULT(vulkanDevice->createBuffer(
			VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
			VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
			&indexStaging,
			static_cast<uint32_t>(indexDataSize),
			indices.data()));
		// Target
		VK_CHECK_RESULT(vulkanDevice->createBuffer(
			VK_BUFFER_USAGE_INDEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
			VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
			&indexBuffer,
			static_cast<uint32_t>(indexDataSize)));

		// Copy
		VkCommandBufferBeginInfo cmdBufInfo = vks::initializers::commandBufferBeginInfo();
		VK_CHECK_RESULT(vkBeginCommandBuffer(copyCmd, &cmdBufInfo));

		VkBufferCopy copyRegion = {};

		copyRegion.size = vertexDataSize;
		vkCmdCopyBuffer(
			copyCmd,
			vertexStaging.buffer,
			vertexBuffer.buffer,
			1,
			&copyRegion);

		copyRegion.size = indexDataSize;
		vkCmdCopyBuffer(
			copyCmd,
			indexStaging.buffer,
			indexBuffer.buffer,
			1,
			&copyRegion);

		VK_CHECK_RESULT(vkEndCommandBuffer(copyCmd));

		VkSubmitInfo submitInfo = {};
		submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
		submitInfo.commandBufferCount = 1;
		submitInfo.pCommandBuffers = &copyCmd;

		VK_CHECK_RESULT(vkQueueSubmit(queue, 1, &submitInfo, VK_NULL_HANDLE));
		VK_CHECK_RESULT(vkQueueWaitIdle(queue));

		//todo: fence
		vertexStaging.destroy();
		indexStaging.destroy();
	}

public:
#if defined(__ANDROID__)
	AAssetManager* assetManager = nullptr;
#endif

	std::string assetPath = "";

	std::vector<SceneMaterial> materials;
	std::vector<ScenePart> meshes;

	// Shared ubo containing matrices used by all
	// materials and meshes
	vks::Buffer uniformBuffer;
	struct UniformData {
		glm::mat4 projection;
		glm::mat4 view;
		glm::mat4 model;
		glm::vec4 lightPos = glm::vec4(0.0f, 0.0f, 1.0f, 0.0f);
	} uniformData;

	// Scene uses multiple pipelines
	struct {
		VkPipeline solid;
		VkPipeline blending;
		VkPipeline bg;
		VkPipeline wireframe;
	} pipelines;

	// Shared pipeline layout
	VkPipelineLayout pipelineLayout;

	// For displaying only a single part of the scene
	bool renderSingleScenePart = false;
	uint32_t scenePartIndex = 0;

	// Default constructor
	Scene(vks::VulkanDevice *vulkanDevice, VkQueue queue)
	{
		this->vulkanDevice = vulkanDevice;
		this->queue = queue;

		// Prepare uniform buffer for global matrices
		VkMemoryRequirements memReqs;
		VkMemoryAllocateInfo memAlloc = vks::initializers::memoryAllocateInfo();
		VkBufferCreateInfo bufferCreateInfo = vks::initializers::bufferCreateInfo(VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT, sizeof(uniformData));
		VK_CHECK_RESULT(vkCreateBuffer(vulkanDevice->logicalDevice, &bufferCreateInfo, nullptr, &uniformBuffer.buffer));
		vkGetBufferMemoryRequirements(vulkanDevice->logicalDevice, uniformBuffer.buffer, &memReqs);
		memAlloc.allocationSize = memReqs.size;
		memAlloc.memoryTypeIndex = vulkanDevice->getMemoryType(memReqs.memoryTypeBits, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
		VK_CHECK_RESULT(vkAllocateMemory(vulkanDevice->logicalDevice, &memAlloc, nullptr, &uniformBuffer.memory));
		VK_CHECK_RESULT(vkBindBufferMemory(vulkanDevice->logicalDevice, uniformBuffer.buffer, uniformBuffer.memory, 0));
		VK_CHECK_RESULT(vkMapMemory(vulkanDevice->logicalDevice, uniformBuffer.memory, 0, sizeof(uniformData), 0, (void **)&uniformBuffer.mapped));
		uniformBuffer.descriptor.offset = 0;
		uniformBuffer.descriptor.buffer = uniformBuffer.buffer;
		uniformBuffer.descriptor.range = sizeof(uniformData);
		uniformBuffer.device = vulkanDevice->logicalDevice;
	}

	// Default destructor
	~Scene()
	{
		vertexBuffer.destroy();
		indexBuffer.destroy();
		for (auto material : materials)
		{
			material.diffuse.destroy();
		}
		vkDestroyPipelineLayout(vulkanDevice->logicalDevice, pipelineLayout, nullptr);
		vkDestroyDescriptorSetLayout(vulkanDevice->logicalDevice, descriptorSetLayouts.material, nullptr);
		vkDestroyDescriptorSetLayout(vulkanDevice->logicalDevice, descriptorSetLayouts.scene, nullptr);
		vkDestroyDescriptorPool(vulkanDevice->logicalDevice, descriptorPool, nullptr);
		vkDestroyPipeline(vulkanDevice->logicalDevice, pipelines.solid, nullptr);
		vkDestroyPipeline(vulkanDevice->logicalDevice, pipelines.bg, nullptr);
		vkDestroyPipeline(vulkanDevice->logicalDevice, pipelines.blending, nullptr);
		vkDestroyPipeline(vulkanDevice->logicalDevice, pipelines.wireframe, nullptr);
		uniformBuffer.destroy();
	}

	void load(std::string filename, VkCommandBuffer copyCmd)
	{
		Assimp::Importer Importer;

		int flags = aiProcess_PreTransformVertices | aiProcess_Triangulate | aiProcess_GenNormals;

#if defined(__ANDROID__)
		AAsset* asset = AAssetManager_open(assetManager, filename.c_str(), AASSET_MODE_STREAMING);
		assert(asset);
		size_t size = AAsset_getLength(asset);
		assert(size > 0);
		void *meshData = malloc(size);
		AAsset_read(asset, meshData, size);
		AAsset_close(asset);
		aScene = Importer.ReadFileFromMemory(meshData, size, flags);
		free(meshData);
#else
		aScene = Importer.ReadFile(filename.c_str(), flags);
#endif
		if (aScene)
		{
			loadMaterials();
			loadMeshes(copyCmd);
		}
		else
		{
			printf("Error parsing '%s': '%s'\n", filename.c_str(), Importer.GetErrorString());
#if defined(__ANDROID__)
			LOGE("Error parsing '%s': '%s'", filename.c_str(), Importer.GetErrorString());
#endif
		}

	}

	void RenderMesh(int id, VkCommandBuffer cmdBuffer, bool wireframe)
	{
		// todo : per material pipelines
		// vkCmdBindPipeline(cmdBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, *mesh.material->pipeline);

		// We will be using multiple descriptor sets for rendering
		// In GLSL the selection is done via the set and binding keywords
		// VS: layout (set = 0, binding = 0) uniform UBO;
		// FS: layout (set = 1, binding = 0) uniform sampler2D samplerColorMap;

		std::array<VkDescriptorSet, 2> descriptorSets;
		// Set 0: Scene descriptor set containing global matrices
		descriptorSets[0] = descriptorSetScene;
		// Set 1: Per-Material descriptor set containing bound images
		descriptorSets[1] = meshes[id].material->descriptorSet;

		vkCmdBindPipeline(cmdBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, wireframe ? pipelines.wireframe : *meshes[id].material->pipeline);
		vkCmdBindDescriptorSets(cmdBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, pipelineLayout, 0, static_cast<uint32_t>(descriptorSets.size()), descriptorSets.data(), 0, NULL);

		// Pass material properies via push constants
		vkCmdPushConstants(
			cmdBuffer,
			pipelineLayout,
			VK_SHADER_STAGE_FRAGMENT_BIT,
			0,
			sizeof(SceneMaterialProperites),
			&meshes[id].material->properties);

		// Render from the global scene vertex buffer using the mesh index offset
		vkCmdDrawIndexed(cmdBuffer, meshes[id].indexCount, 1, 0, meshes[id].indexBase, 0);
	}

	// Renders the scene into an active command buffer
	// In a real world application we would do some visibility culling in here
	void render(VkCommandBuffer cmdBuffer, bool wireframe)
	{
		VkDeviceSize offsets[1] = { 0 };


		// Bind scene vertex and index buffers
		vkCmdBindVertexBuffers(cmdBuffer, 0, 1, &vertexBuffer.buffer, offsets);
		vkCmdBindIndexBuffer(cmdBuffer, indexBuffer.buffer, 0, VK_INDEX_TYPE_UINT32);

		//cks 
		std::vector<int> bgMeshIds;
		std::vector<int> alphaMeshIds;
		std::vector<int> nonAlphaMeshIds;

		for (size_t i = 0; i < meshes.size(); i++)
		{
			if ((renderSingleScenePart) && (i != scenePartIndex))
				continue;

			//cks //check for alpha materials
			int testForAlpha = meshes[i].material->name.find("alpha");
			if (testForAlpha > 0)
			{
				alphaMeshIds.push_back(i);
				continue;
			}
			int testForBG = meshes[i].material->name.find("mask");
			if (testForBG > 0)
			{
				bgMeshIds.push_back(i);
				continue;
			}

			nonAlphaMeshIds.push_back(i);
		}

		for (size_t i = 0; i < bgMeshIds.size(); i++)
		{
			RenderMesh(bgMeshIds[i], cmdBuffer, wireframe);
		}

		for (size_t i = 0; i < nonAlphaMeshIds.size(); i++)
		{
			RenderMesh(nonAlphaMeshIds[i], cmdBuffer, wireframe);
		}

		for (size_t i = 0; i < alphaMeshIds.size(); i++)
		{
			RenderMesh(alphaMeshIds[i], cmdBuffer, wireframe);
		}
		
	}
};

#include "VulkanModel.hpp"

class VulkanExample : public VulkanExampleBase
{
	// Vertex layout for the models
	vks::VertexLayout vertexLayout = vks::VertexLayout({
		vks::VERTEX_COMPONENT_POSITION,
		vks::VERTEX_COMPONENT_NORMAL,
		vks::VERTEX_COMPONENT_UV,
		vks::VERTEX_COMPONENT_COLOR,
	});

	struct {
		vks::Model ufo;
		vks::Model skysphere;
	} models;

	// Max. dimension of the ufo mesh for use as the sphere
	// radius for frustum culling
	float objectSphereDim;

	struct {
		VkPipeline phong;
		VkPipeline starsphere;
	} pipelines;

	VkPipelineLayout pipelineLayout;


public:
	bool wireframe = false;
	bool attachLight = false;

	Scene *scene = nullptr;

	struct {
		VkPipelineVertexInputStateCreateInfo inputState;
		std::vector<VkVertexInputBindingDescription> bindingDescriptions;
		std::vector<VkVertexInputAttributeDescription> attributeDescriptions;
	} vertices;

	VulkanExample() : VulkanExampleBase(ENABLE_VALIDATION)
	{
		title = "Vulkan Chrome Demo";
		rotationSpeed = 0.5f;
		enableTextOverlay = false;
		camera.type = Camera::CameraType::firstperson;
		camera.movementSpeed = 7.5f;
		camera.position = { 0.0f, 0.0f, -0.39f };
		camera.setRotation(glm::vec3(-0.0f, 0.0f, 0.0f));
		camera.setPerspective(19.157f, (float)width / (float)height, 0.01f, 256.0f);

		// Get number of max. concurrrent threads
		numThreads = std::thread::hardware_concurrency();
		assert(numThreads > 0);
#if defined(__ANDROID__)
		LOGD("numThreads = %d", numThreads);
#else
		std::cout << "numThreads = " << numThreads << std::endl;
#endif
		srand(time(NULL));

		

		threadPool.setThreadCount(numThreads);

		numObjectsPerThread = 1024 / numThreads;

	}

	~VulkanExample()
	{
		vkDestroyPipeline(device, pipelines.phong, nullptr);
		vkDestroyPipeline(device, pipelines.starsphere, nullptr);

		vkDestroyPipelineLayout(device, pipelineLayout, nullptr);

		vkFreeCommandBuffers(device, cmdPool, 1, &primaryCommandBuffer);
		vkFreeCommandBuffers(device, cmdPool, 1, &secondaryCommandBuffer);

		models.ufo.destroy();
		models.skysphere.destroy();

		for (auto& thread : threadData)
		{
			vkFreeCommandBuffers(device, thread.commandPool, thread.commandBuffer.size(), thread.commandBuffer.data());
			vkDestroyCommandPool(device, thread.commandPool, nullptr);
		}

		vkDestroyFence(device, renderFence, nullptr);
		delete(scene);
	}

	// Enable physical device features required for this example				
	virtual void getEnabledFeatures()
	{
		// Fill mode non solid is required for wireframe display
		if (deviceFeatures.fillModeNonSolid) {
			enabledFeatures.fillModeNonSolid = VK_TRUE;
		};
	}

	void reBuildCommandBuffers()
	{
		if (!checkCommandBuffers())
		{
			destroyCommandBuffers();
			createCommandBuffers();
		}
		buildCommandBuffers();
	}

	void buildCommandBuffers()
	{
		VkCommandBufferBeginInfo cmdBufInfo = vks::initializers::commandBufferBeginInfo();

		VkClearValue clearValues[3];
		clearValues[0].color = { { 1.0f, 0.0f, 1.0f, 1.0f } };
		clearValues[1].color = { { 1.0f, 0.0f, 1.0f, 1.0f } };
		clearValues[2].depthStencil = { 1.0f, 0 };

		VkRenderPassBeginInfo renderPassBeginInfo = vks::initializers::renderPassBeginInfo();
		renderPassBeginInfo.renderPass = renderPass;
		renderPassBeginInfo.renderArea.offset.x = 0;
		renderPassBeginInfo.renderArea.offset.y = 0;
		renderPassBeginInfo.renderArea.extent.width = width;
		renderPassBeginInfo.renderArea.extent.height = height;
		renderPassBeginInfo.clearValueCount = 3;
		renderPassBeginInfo.pClearValues = clearValues;

		for (int32_t i = 0; i < drawCmdBuffers.size(); ++i)
		{
			renderPassBeginInfo.framebuffer = frameBuffers[i];

			VK_CHECK_RESULT(vkBeginCommandBuffer(drawCmdBuffers[i], &cmdBufInfo));

			vkCmdBeginRenderPass(drawCmdBuffers[i], &renderPassBeginInfo, VK_SUBPASS_CONTENTS_INLINE);

			VkViewport viewport = vks::initializers::viewport((float)width, (float)height, 0.0f, 1.0f);
			vkCmdSetViewport(drawCmdBuffers[i], 0, 1, &viewport);

			VkRect2D scissor = vks::initializers::rect2D(width, height, 0, 0);
			vkCmdSetScissor(drawCmdBuffers[i], 0, 1, &scissor);

			scene->render(drawCmdBuffers[i], wireframe);

			vkCmdEndRenderPass(drawCmdBuffers[i]);

			VK_CHECK_RESULT(vkEndCommandBuffer(drawCmdBuffers[i]));
		}
	}

	void setupVertexDescriptions()
	{
		// Binding description
		vertices.bindingDescriptions.resize(1);
		vertices.bindingDescriptions[0] =
			vks::initializers::vertexInputBindingDescription(
				VERTEX_BUFFER_BIND_ID,
				sizeof(Vertex),
				VK_VERTEX_INPUT_RATE_VERTEX);

		// Attribute descriptions
		// Describes memory layout and shader positions
		vertices.attributeDescriptions.resize(4);
		// Location 0 : Position
		vertices.attributeDescriptions[0] =
			vks::initializers::vertexInputAttributeDescription(
				VERTEX_BUFFER_BIND_ID,
				0,
				VK_FORMAT_R32G32B32_SFLOAT,
				0);
		// Location 1 : Normal
		vertices.attributeDescriptions[1] =
			vks::initializers::vertexInputAttributeDescription(
				VERTEX_BUFFER_BIND_ID,
				1,
				VK_FORMAT_R32G32B32_SFLOAT,
				sizeof(float) * 3);
		// Location 2 : Texture coordinates
		vertices.attributeDescriptions[2] =
			vks::initializers::vertexInputAttributeDescription(
				VERTEX_BUFFER_BIND_ID,
				2,
				VK_FORMAT_R32G32_SFLOAT,
				sizeof(float) * 6);
		// Location 3 : Color
		vertices.attributeDescriptions[3] =
			vks::initializers::vertexInputAttributeDescription(
				VERTEX_BUFFER_BIND_ID,
				3,
				VK_FORMAT_R32G32B32_SFLOAT,
				sizeof(float) * 8);

		vertices.inputState = vks::initializers::pipelineVertexInputStateCreateInfo();
		vertices.inputState.vertexBindingDescriptionCount = static_cast<uint32_t>(vertices.bindingDescriptions.size());
		vertices.inputState.pVertexBindingDescriptions = vertices.bindingDescriptions.data();
		vertices.inputState.vertexAttributeDescriptionCount = static_cast<uint32_t>(vertices.attributeDescriptions.size());
		vertices.inputState.pVertexAttributeDescriptions = vertices.attributeDescriptions.data();
	}
	
	void preparePipelines()
	{
		VkPipelineInputAssemblyStateCreateInfo inputAssemblyState =
			vks::initializers::pipelineInputAssemblyStateCreateInfo(
				VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST,
				0,
				VK_FALSE);

		VkPipelineRasterizationStateCreateInfo rasterizationState =
			vks::initializers::pipelineRasterizationStateCreateInfo(
				VK_POLYGON_MODE_FILL,
				VK_CULL_MODE_BACK_BIT,
				VK_FRONT_FACE_COUNTER_CLOCKWISE,
				0);

		VkPipelineColorBlendAttachmentState blendAttachmentState =
			vks::initializers::pipelineColorBlendAttachmentState(
				0xf,
				VK_FALSE);

		VkPipelineColorBlendStateCreateInfo colorBlendState =
			vks::initializers::pipelineColorBlendStateCreateInfo(
				1,
				&blendAttachmentState);

		VkPipelineDepthStencilStateCreateInfo depthStencilState =
			vks::initializers::pipelineDepthStencilStateCreateInfo(
				VK_TRUE,
				VK_TRUE,
				VK_COMPARE_OP_LESS_OR_EQUAL);

		VkPipelineViewportStateCreateInfo viewportState =
			vks::initializers::pipelineViewportStateCreateInfo(1, 1, 0);

		VkPipelineMultisampleStateCreateInfo multisampleState =
			vks::initializers::pipelineMultisampleStateCreateInfo(
				SAMPLE_COUNT,
				0);

		std::vector<VkDynamicState> dynamicStateEnables = {
			VK_DYNAMIC_STATE_VIEWPORT,
			VK_DYNAMIC_STATE_SCISSOR
		};
		VkPipelineDynamicStateCreateInfo dynamicState =
			vks::initializers::pipelineDynamicStateCreateInfo(
				dynamicStateEnables.data(),
				static_cast<uint32_t>(dynamicStateEnables.size()),
				0);

		std::array<VkPipelineShaderStageCreateInfo, 2> shaderSolidStages;
		std::array<VkPipelineShaderStageCreateInfo, 2> shaderAlphaStages;

		// Solid rendering pipeline
		shaderSolidStages[0] = loadShader(scene->assetPath + "shaders/scene_solid.vert.spv", VK_SHADER_STAGE_VERTEX_BIT);
		shaderSolidStages[1] = loadShader(scene->assetPath + "shaders/scene_solid.frag.spv", VK_SHADER_STAGE_FRAGMENT_BIT);

		VkGraphicsPipelineCreateInfo pipelineCreateInfo =
			vks::initializers::pipelineCreateInfo(
				scene->pipelineLayout,
				renderPass,
				0);

		pipelineCreateInfo.pVertexInputState = &vertices.inputState;
		pipelineCreateInfo.pInputAssemblyState = &inputAssemblyState;
		pipelineCreateInfo.pRasterizationState = &rasterizationState;
		pipelineCreateInfo.pColorBlendState = &colorBlendState;
		pipelineCreateInfo.pMultisampleState = &multisampleState;
		pipelineCreateInfo.pViewportState = &viewportState;
		pipelineCreateInfo.pDepthStencilState = &depthStencilState;
		pipelineCreateInfo.pDynamicState = &dynamicState;
		pipelineCreateInfo.stageCount = static_cast<uint32_t>(shaderSolidStages.size());
		pipelineCreateInfo.pStages = shaderSolidStages.data();

		VK_CHECK_RESULT(vkCreateGraphicsPipelines(device, pipelineCache, 1, &pipelineCreateInfo, nullptr, &scene->pipelines.solid));

		// Alpha blended pipeline
		shaderAlphaStages[0] = loadShader(scene->assetPath + "shaders/scene_alpha.vert.spv", VK_SHADER_STAGE_VERTEX_BIT);
		shaderAlphaStages[1] = loadShader(scene->assetPath + "shaders/scene_alpha.frag.spv", VK_SHADER_STAGE_FRAGMENT_BIT);

		//depthStencilState.depthTestEnable = VK_FALSE;
		//depthStencilState.depthWriteEnable = VK_FALSE;

		rasterizationState.cullMode = VK_CULL_MODE_BACK_BIT;
		blendAttachmentState.blendEnable = VK_TRUE;
		blendAttachmentState.colorBlendOp = VK_BLEND_OP_ADD;
		blendAttachmentState.srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
		blendAttachmentState.dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
		pipelineCreateInfo.stageCount = static_cast<uint32_t>(shaderAlphaStages.size());
		pipelineCreateInfo.pStages = shaderAlphaStages.data();

		VK_CHECK_RESULT(vkCreateGraphicsPipelines(device, pipelineCache, 1, &pipelineCreateInfo, nullptr, &scene->pipelines.blending));
		
		VK_CHECK_RESULT(vkCreateGraphicsPipelines(device, pipelineCache, 1, &pipelineCreateInfo, nullptr, &scene->pipelines.bg));

		// Wire frame rendering pipeline
		if (deviceFeatures.fillModeNonSolid) {
			rasterizationState.cullMode = VK_CULL_MODE_BACK_BIT;
			blendAttachmentState.blendEnable = VK_FALSE;
			rasterizationState.polygonMode = VK_POLYGON_MODE_LINE;
			rasterizationState.lineWidth = 1.0f;
			VK_CHECK_RESULT(vkCreateGraphicsPipelines(device, pipelineCache, 1, &pipelineCreateInfo, nullptr, &scene->pipelines.wireframe));
		}
	}

	void preparePipelines2()
	{
		VkPipelineInputAssemblyStateCreateInfo inputAssemblyState =
			vks::initializers::pipelineInputAssemblyStateCreateInfo(
				VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST,
				0,
				VK_FALSE);

		VkPipelineRasterizationStateCreateInfo rasterizationState =
			vks::initializers::pipelineRasterizationStateCreateInfo(
				VK_POLYGON_MODE_FILL,
				VK_CULL_MODE_BACK_BIT,
				VK_FRONT_FACE_CLOCKWISE,
				0);

		VkPipelineColorBlendAttachmentState blendAttachmentState =
			vks::initializers::pipelineColorBlendAttachmentState(
				0xf,
				VK_TRUE);
		blendAttachmentState.colorBlendOp = VK_BLEND_OP_ADD;
		blendAttachmentState.srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
		blendAttachmentState.dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;

		VkPipelineColorBlendStateCreateInfo colorBlendState =
			vks::initializers::pipelineColorBlendStateCreateInfo(
				1,
				&blendAttachmentState);

		VkPipelineDepthStencilStateCreateInfo depthStencilState =
			vks::initializers::pipelineDepthStencilStateCreateInfo(
				VK_TRUE,
				VK_TRUE,
				VK_COMPARE_OP_LESS_OR_EQUAL);

		VkPipelineViewportStateCreateInfo viewportState =
			vks::initializers::pipelineViewportStateCreateInfo(1, 1, 0);

		VkPipelineMultisampleStateCreateInfo multisampleState =
			vks::initializers::pipelineMultisampleStateCreateInfo(
				SAMPLE_COUNT,
				0);

		std::vector<VkDynamicState> dynamicStateEnables = {
			VK_DYNAMIC_STATE_VIEWPORT,
			VK_DYNAMIC_STATE_SCISSOR
		};
		VkPipelineDynamicStateCreateInfo dynamicState =
			vks::initializers::pipelineDynamicStateCreateInfo(
				dynamicStateEnables.data(),
				dynamicStateEnables.size(),
				0);

		// Solid rendering pipeline
		// Load shaders
		std::array<VkPipelineShaderStageCreateInfo, 2> shaderStages;

		shaderStages[0] = loadShader(getAssetPath() + "shaders/multithreading/phong.vert.spv", VK_SHADER_STAGE_VERTEX_BIT);
		shaderStages[1] = loadShader(getAssetPath() + "shaders/multithreading/phong.frag.spv", VK_SHADER_STAGE_FRAGMENT_BIT);

		VkGraphicsPipelineCreateInfo pipelineCreateInfo =
			vks::initializers::pipelineCreateInfo(
				pipelineLayout,
				renderPass,
				0);

		pipelineCreateInfo.pVertexInputState = &vertices.inputState;
		pipelineCreateInfo.pInputAssemblyState = &inputAssemblyState;
		pipelineCreateInfo.pRasterizationState = &rasterizationState;
		pipelineCreateInfo.pColorBlendState = &colorBlendState;
		pipelineCreateInfo.pMultisampleState = &multisampleState;
		pipelineCreateInfo.pViewportState = &viewportState;
		pipelineCreateInfo.pDepthStencilState = &depthStencilState;
		pipelineCreateInfo.pDynamicState = &dynamicState;
		pipelineCreateInfo.stageCount = shaderStages.size();
		pipelineCreateInfo.pStages = shaderStages.data();

		VK_CHECK_RESULT(vkCreateGraphicsPipelines(device, pipelineCache, 1, &pipelineCreateInfo, nullptr, &pipelines.phong));

		// Star sphere rendering pipeline
		rasterizationState.cullMode = VK_CULL_MODE_FRONT_BIT;
		depthStencilState.depthWriteEnable = VK_FALSE;
		shaderStages[0] = loadShader(getAssetPath() + "shaders/multithreading/starsphere.vert.spv", VK_SHADER_STAGE_VERTEX_BIT);
		shaderStages[1] = loadShader(getAssetPath() + "shaders/multithreading/starsphere.frag.spv", VK_SHADER_STAGE_FRAGMENT_BIT);
		VK_CHECK_RESULT(vkCreateGraphicsPipelines(device, pipelineCache, 1, &pipelineCreateInfo, nullptr, &pipelines.starsphere));
	}


	void updateUniformBuffers()
	{
		if (attachLight)
		{
			scene->uniformData.lightPos = glm::vec4(-camera.position, 1.0f);
		}

		scene->uniformData.projection = camera.matrices.perspective;
		scene->uniformData.view = camera.matrices.view;
		scene->uniformData.model = glm::mat4();

		memcpy(scene->uniformBuffer.mapped, &scene->uniformData, sizeof(scene->uniformData));
	}

	void draw()
	{
		VulkanExampleBase::prepareFrame();

		updateCommandBuffers(frameBuffers[currentBuffer]);

		submitInfo.commandBufferCount = 1;
		submitInfo.pCommandBuffers = &primaryCommandBuffer;

		VK_CHECK_RESULT(vkQueueSubmit(queue, 1, &submitInfo, renderFence));

		// Wait for fence to signal that all command buffers are ready
		VkResult fenceRes;
		do
		{
			fenceRes = vkWaitForFences(device, 1, &renderFence, VK_TRUE, 100000000);
		} while (fenceRes == VK_TIMEOUT);
		VK_CHECK_RESULT(fenceRes);
		vkResetFences(device, 1, &renderFence);

#if 1
		// Command buffer to be sumitted to the queue
		// Wait for color attachment output to finish before rendering the text overlay
		VkSubmitInfo submitInfo1 = {};
		submitInfo1.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;

		VkPipelineStageFlags stageFlags = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
		submitInfo1.pWaitDstStageMask = &stageFlags;

		submitInfo1.commandBufferCount = 1;
		submitInfo1.pCommandBuffers = &drawCmdBuffers[currentBuffer];

		// Submit to queue
		VK_CHECK_RESULT(vkQueueSubmit(queue, 1, &submitInfo1, VK_NULL_HANDLE));
#endif

		VulkanExampleBase::submitFrame();
	}

	void loadScene()
	{
		VkCommandBuffer copyCmd = VulkanExampleBase::createCommandBuffer(VK_COMMAND_BUFFER_LEVEL_PRIMARY, false);
		scene = new Scene(vulkanDevice, queue);

#if defined(__ANDROID__)
		scene->assetManager = androidApp->activity->assetManager;
#endif
		scene->assetPath = getAssetPath() + "chrome/";
		scene->load(getAssetPath() + "chrome/chrome.dae", copyCmd);
		vkFreeCommandBuffers(device, cmdPool, 1, &copyCmd);

		models.ufo.loadFromFile(getAssetPath() + "chrome/map.dae", vertexLayout, 0.7f, vulkanDevice, queue);
		models.skysphere.loadFromFile(getAssetPath() + "models/sphere.obj", vertexLayout, 1.0f, vulkanDevice, queue);
		objectSphereDim = std::max(std::max(models.ufo.dim.size.x, models.ufo.dim.size.y), models.ufo.dim.size.z);


		updateUniformBuffers();
	}

	// Creates a multi sample render target (image and view) that is used to resolve 
	// into the visible frame buffer target in the render pass
	void setupMultisampleTarget()
	{
		// Check if device supports requested sample count for color and depth frame buffer
		assert((deviceProperties.limits.framebufferColorSampleCounts >= SAMPLE_COUNT) && (deviceProperties.limits.framebufferDepthSampleCounts >= SAMPLE_COUNT));

		// Color target
		VkImageCreateInfo info = vks::initializers::imageCreateInfo();
		info.imageType = VK_IMAGE_TYPE_2D;
		info.format = swapChain.colorFormat;
		info.extent.width = width;
		info.extent.height = height;
		info.extent.depth = 1;
		info.mipLevels = 1;
		info.arrayLayers = 1;
		info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
		info.tiling = VK_IMAGE_TILING_OPTIMAL;
		info.samples = SAMPLE_COUNT;
		// Image will only be used as a transient target
		info.usage = VK_IMAGE_USAGE_TRANSIENT_ATTACHMENT_BIT | VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
		info.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;

		VK_CHECK_RESULT(vkCreateImage(device, &info, nullptr, &multisampleTarget.color.image));

		VkMemoryRequirements memReqs;
		vkGetImageMemoryRequirements(device, multisampleTarget.color.image, &memReqs);
		VkMemoryAllocateInfo memAlloc = vks::initializers::memoryAllocateInfo();
		memAlloc.allocationSize = memReqs.size;
		// We prefer a lazily allocated memory type
		// This means that the memory gets allocated when the implementation sees fit, e.g. when first using the images
		VkBool32 lazyMemTypePresent;
		memAlloc.memoryTypeIndex = vulkanDevice->getMemoryType(memReqs.memoryTypeBits, VK_MEMORY_PROPERTY_LAZILY_ALLOCATED_BIT, &lazyMemTypePresent);
		if (!lazyMemTypePresent)
		{
			// If this is not available, fall back to device local memory
			memAlloc.memoryTypeIndex = vulkanDevice->getMemoryType(memReqs.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
		}
		VK_CHECK_RESULT(vkAllocateMemory(device, &memAlloc, nullptr, &multisampleTarget.color.memory));
		vkBindImageMemory(device, multisampleTarget.color.image, multisampleTarget.color.memory, 0);

		// Create image view for the MSAA target
		VkImageViewCreateInfo viewInfo = vks::initializers::imageViewCreateInfo();
		viewInfo.image = multisampleTarget.color.image;
		viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
		viewInfo.format = swapChain.colorFormat;
		viewInfo.components.r = VK_COMPONENT_SWIZZLE_R;
		viewInfo.components.g = VK_COMPONENT_SWIZZLE_G;
		viewInfo.components.b = VK_COMPONENT_SWIZZLE_B;
		viewInfo.components.a = VK_COMPONENT_SWIZZLE_A;
		viewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
		viewInfo.subresourceRange.levelCount = 1;
		viewInfo.subresourceRange.layerCount = 1;

		VK_CHECK_RESULT(vkCreateImageView(device, &viewInfo, nullptr, &multisampleTarget.color.view));

		// Depth target
		info.imageType = VK_IMAGE_TYPE_2D;
		info.format = depthFormat;
		info.extent.width = width;
		info.extent.height = height;
		info.extent.depth = 1;
		info.mipLevels = 1;
		info.arrayLayers = 1;
		info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
		info.tiling = VK_IMAGE_TILING_OPTIMAL;
		info.samples = SAMPLE_COUNT;
		// Image will only be used as a transient target
		info.usage = VK_IMAGE_USAGE_TRANSIENT_ATTACHMENT_BIT | VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT;
		info.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;

		VK_CHECK_RESULT(vkCreateImage(device, &info, nullptr, &multisampleTarget.depth.image));

		vkGetImageMemoryRequirements(device, multisampleTarget.depth.image, &memReqs);
		memAlloc = vks::initializers::memoryAllocateInfo();
		memAlloc.allocationSize = memReqs.size;

		memAlloc.memoryTypeIndex = vulkanDevice->getMemoryType(memReqs.memoryTypeBits, VK_MEMORY_PROPERTY_LAZILY_ALLOCATED_BIT, &lazyMemTypePresent);
		if (!lazyMemTypePresent)
		{
			memAlloc.memoryTypeIndex = vulkanDevice->getMemoryType(memReqs.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
		}

		VK_CHECK_RESULT(vkAllocateMemory(device, &memAlloc, nullptr, &multisampleTarget.depth.memory));
		vkBindImageMemory(device, multisampleTarget.depth.image, multisampleTarget.depth.memory, 0);

		// Create image view for the MSAA target
		viewInfo.image = multisampleTarget.depth.image;
		viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
		viewInfo.format = depthFormat;
		viewInfo.components.r = VK_COMPONENT_SWIZZLE_R;
		viewInfo.components.g = VK_COMPONENT_SWIZZLE_G;
		viewInfo.components.b = VK_COMPONENT_SWIZZLE_B;
		viewInfo.components.a = VK_COMPONENT_SWIZZLE_A;
		viewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT | VK_IMAGE_ASPECT_STENCIL_BIT;
		viewInfo.subresourceRange.levelCount = 1;
		viewInfo.subresourceRange.layerCount = 1;

		VK_CHECK_RESULT(vkCreateImageView(device, &viewInfo, nullptr, &multisampleTarget.depth.view));
	}

	// Setup a render pass for using a multi sampled attachment 
	// and a resolve attachment that the msaa image is resolved 
	// to at the end of the render pass
	void setupRenderPass()
	{
		// Overrides the virtual function of the base class

		std::array<VkAttachmentDescription, 4> attachments = {};

		// Multisampled attachment that we render to
		attachments[0].format = swapChain.colorFormat;
		attachments[0].samples = SAMPLE_COUNT;
		attachments[0].loadOp = VK_ATTACHMENT_LOAD_OP_LOAD;
		// No longer required after resolve, this may save some bandwidth on certain GPUs
		attachments[0].storeOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
		attachments[0].stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
		attachments[0].stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
		attachments[0].initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
		attachments[0].finalLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

		// This is the frame buffer attachment to where the multisampled image
		// will be resolved to and which will be presented to the swapchain
		attachments[1].format = swapChain.colorFormat;
		attachments[1].samples = VK_SAMPLE_COUNT_1_BIT;
		attachments[1].loadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
		attachments[1].storeOp = VK_ATTACHMENT_STORE_OP_STORE;
		attachments[1].stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
		attachments[1].stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
		attachments[1].initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
		attachments[1].finalLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;

		// Multisampled depth attachment we render to
		attachments[2].format = depthFormat;
		attachments[2].samples = SAMPLE_COUNT;
		attachments[2].loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
		attachments[2].storeOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
		attachments[2].stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
		attachments[2].stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
		attachments[2].initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
		attachments[2].finalLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

		// Depth resolve attachment
		attachments[3].format = depthFormat;
		attachments[3].samples = VK_SAMPLE_COUNT_1_BIT;
		attachments[3].loadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
		attachments[3].storeOp = VK_ATTACHMENT_STORE_OP_STORE;
		attachments[3].stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
		attachments[3].stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
		attachments[3].initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
		attachments[3].finalLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

		VkAttachmentReference colorReference = {};
		colorReference.attachment = 0;
		colorReference.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

		VkAttachmentReference depthReference = {};
		depthReference.attachment = 2;
		depthReference.layout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

		// Two resolve attachment references for color and depth
		std::array<VkAttachmentReference, 2> resolveReferences = {};
		resolveReferences[0].attachment = 1;
		resolveReferences[0].layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
		resolveReferences[1].attachment = 3;
		resolveReferences[1].layout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

		VkSubpassDescription subpass = {};
		subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
		subpass.colorAttachmentCount = 1;
		subpass.pColorAttachments = &colorReference;
		// Pass our resolve attachments to the sub pass
		subpass.pResolveAttachments = resolveReferences.data();
		subpass.pDepthStencilAttachment = &depthReference;

		std::array<VkSubpassDependency, 2> dependencies;

		dependencies[0].srcSubpass = VK_SUBPASS_EXTERNAL;
		dependencies[0].dstSubpass = 0;
		dependencies[0].srcStageMask = VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT;
		dependencies[0].dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
		dependencies[0].srcAccessMask = VK_ACCESS_MEMORY_READ_BIT;
		dependencies[0].dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
		dependencies[0].dependencyFlags = VK_DEPENDENCY_BY_REGION_BIT;

		dependencies[1].srcSubpass = 0;
		dependencies[1].dstSubpass = VK_SUBPASS_EXTERNAL;
		dependencies[1].srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
		dependencies[1].dstStageMask = VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT;
		dependencies[1].srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
		dependencies[1].dstAccessMask = VK_ACCESS_MEMORY_READ_BIT;
		dependencies[1].dependencyFlags = VK_DEPENDENCY_BY_REGION_BIT;

		VkRenderPassCreateInfo renderPassInfo = vks::initializers::renderPassCreateInfo();
		renderPassInfo.attachmentCount = attachments.size();
		renderPassInfo.pAttachments = attachments.data();
		renderPassInfo.subpassCount = 1;
		renderPassInfo.pSubpasses = &subpass;
		renderPassInfo.dependencyCount = 2;
		renderPassInfo.pDependencies = dependencies.data();

		VK_CHECK_RESULT(vkCreateRenderPass(device, &renderPassInfo, nullptr, &renderPass));
	}

	// Frame buffer attachments must match with render pass setup, 
	// so we need to adjust frame buffer creation to cover our 
	// multisample target
	void setupFrameBuffer()
	{
		// Overrides the virtual function of the base class

		std::array<VkImageView, 4> attachments;

		setupMultisampleTarget();

		attachments[0] = multisampleTarget.color.view;
		// attachment[1] = swapchain image
		attachments[2] = multisampleTarget.depth.view;
		attachments[3] = depthStencil.view;

		VkFramebufferCreateInfo frameBufferCreateInfo = {};
		frameBufferCreateInfo.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
		frameBufferCreateInfo.pNext = NULL;
		frameBufferCreateInfo.renderPass = renderPass;
		frameBufferCreateInfo.attachmentCount = attachments.size();
		frameBufferCreateInfo.pAttachments = attachments.data();
		frameBufferCreateInfo.width = width;
		frameBufferCreateInfo.height = height;
		frameBufferCreateInfo.layers = 1;

		// Create frame buffers for every swap chain image
		frameBuffers.resize(swapChain.imageCount);
		for (uint32_t i = 0; i < frameBuffers.size(); i++)
		{
			attachments[1] = swapChain.buffers[i].view;
			VK_CHECK_RESULT(vkCreateFramebuffer(device, &frameBufferCreateInfo, nullptr, &frameBuffers[i]));
		}
	}

	void updateMatrices()
	{
		static int stage = 0;
		static float stage_count = 0.0;
		static float new_zoom = 0.0;
		static float farClip = 10.0f;
		stage_count += 0.001;
		stage = (int)stage_count;
		if (stage_count > 4.0f)
			stage_count = 0;
		static float rotY = 0.0f, posY = 0.0f;
		if (stage == 0)
		{
			new_zoom = 0;
			farClip = 10.0f;
		}
		else if (stage == 1)
		{
			new_zoom = -1;
			farClip = 15.0f;
		}
		else if (stage == 2)
		{
			new_zoom = -2;
			farClip = 20.0f;
		}
		else if (stage == 3)
		{
			new_zoom = -3;
			farClip = 25.0f;
		}
		rotY += 0.01f;
		
		if (abs(new_zoom - zoom) > 0.01f)
		{
			if (new_zoom < zoom)
				zoom -= 0.01f;
			else
				zoom += 0.01f;
		}
		//zoom -= 0.001f;
		rotation = glm::vec3(75.0 + zoom * 5.0f, 0.0, rotY);
		matrices.projection = glm::perspective(glm::radians(60.0f), (float)width / (float)height, 0.1f, farClip);
		matrices.view = glm::translate(glm::mat4(), glm::vec3(0.0f, -10.0f - zoom, zoom));
		matrices.view = glm::rotate(matrices.view, glm::radians(rotation.x), glm::vec3(1.0f, 0.0f, 0.0f));
		matrices.view = glm::rotate(matrices.view, glm::radians(rotation.y), glm::vec3(0.0f, 1.0f, 0.0f));
		matrices.view = glm::rotate(matrices.view, glm::radians(rotation.z), glm::vec3(0.0f, 0.0f, 1.0f));

		frustum.update(matrices.projection * matrices.view);
	}

	void setupPipelineLayout2()
	{
		VkPipelineLayoutCreateInfo pPipelineLayoutCreateInfo =
			vks::initializers::pipelineLayoutCreateInfo(nullptr, 0);

		// Push constants for model matrices
		VkPushConstantRange pushConstantRange =
			vks::initializers::pushConstantRange(
				VK_SHADER_STAGE_VERTEX_BIT,
				sizeof(ThreadPushConstantBlock),
				0);

		// Push constant ranges are part of the pipeline layout
		pPipelineLayoutCreateInfo.pushConstantRangeCount = 1;
		pPipelineLayoutCreateInfo.pPushConstantRanges = &pushConstantRange;

		VK_CHECK_RESULT(vkCreatePipelineLayout(device, &pPipelineLayoutCreateInfo, nullptr, &pipelineLayout));
	}

	void prepare()
	{
		VulkanExampleBase::prepare();
		// Create a fence for synchronization
		VkFenceCreateInfo fenceCreateInfo = vks::initializers::fenceCreateInfo(VK_FLAGS_NONE);
		vkCreateFence(device, &fenceCreateInfo, NULL, &renderFence);
		
		setupVertexDescriptions();
		loadScene();
		
		preparePipelines();
		setupPipelineLayout2();
		preparePipelines2();
		prepareMultiThreadedRenderer();
		updateMatrices();
		buildCommandBuffers();
		prepared = true;
	}

	virtual void render()
	{
		if (!prepared)
			return;
		draw();

		updateMatrices();
	}

	virtual void viewChanged()
	{
		//updateUniformBuffers();
		updateMatrices();
	}


	VkCommandBuffer primaryCommandBuffer;
	VkCommandBuffer secondaryCommandBuffer;

	// Fence to wait for all command buffers to finish before
	// presenting to the swap chain
	VkFence renderFence = {};

	// Number of animated objects to be renderer
	// by using threads and secondary command buffers
	uint32_t numObjectsPerThread;

	// Multi threaded stuff
	// Max. number of concurrent threads
	uint32_t numThreads;

	// Use push constants to update shader
	// parameters on a per-thread base
	struct ThreadPushConstantBlock {
		glm::mat4 mvp;
		glm::vec3 color;
	};

	struct ObjectData {
		glm::mat4 model;
		glm::vec3 pos;
		glm::vec3 rotation;
		float rotationDir;
		float rotationSpeed;
		float scale;
		float deltaT;
		float stateT = 0;
		bool visible = true;
	};

	struct ThreadData {
		VkCommandPool commandPool;
		// One command buffer per render object
		std::vector<VkCommandBuffer> commandBuffer;
		// One push constant block per render object
		std::vector<ThreadPushConstantBlock> pushConstBlock;
		// Per object information (position, rotation, etc.)
		std::vector<ObjectData> objectData;
	};
	std::vector<ThreadData> threadData;

	vks::ThreadPool threadPool;

	// View frustum for culling invisible objects
	vks::Frustum frustum;

	// Shared matrices used for thread push constant blocks
	struct {
		glm::mat4 projection;
		glm::mat4 view;
	} matrices;

	float rnd(float range)
	{
		return range * (rand() / double(RAND_MAX));
	}

	// Create all threads and initialize shader push constants
	void prepareMultiThreadedRenderer()
	{
		// Since this demo updates the command buffers on each frame
		// we don't use the per-framebuffer command buffers from the
		// base class, and create a single primary command buffer instead
		VkCommandBufferAllocateInfo cmdBufAllocateInfo =
			vks::initializers::commandBufferAllocateInfo(
				cmdPool,
				VK_COMMAND_BUFFER_LEVEL_PRIMARY,
				1);
		VK_CHECK_RESULT(vkAllocateCommandBuffers(device, &cmdBufAllocateInfo, &primaryCommandBuffer));

		// Create a secondary command buffer for rendering the star sphere
		cmdBufAllocateInfo.level = VK_COMMAND_BUFFER_LEVEL_SECONDARY;
		VK_CHECK_RESULT(vkAllocateCommandBuffers(device, &cmdBufAllocateInfo, &secondaryCommandBuffer));

		threadData.resize(numThreads);

		float maxX = std::floor(std::sqrt(numThreads * numObjectsPerThread));
		uint32_t posX = 0;
		uint32_t posZ = 0;

		std::mt19937 rndGenerator((unsigned)time(NULL));
		std::uniform_real_distribution<float> uniformDist(0.0f, 1.0f);

		int count = 0;
		int total = 1024;
		int x1 = 0;
		int y1 = 0;
		float x1_init_pos = -16.0f;
		float y1_init_pos = -16.0f;
		float x1_offset = 2.0f;
		float y1_offset = 2.0f;
		for (uint32_t i = 0; i < numThreads; i++)
		{

			ThreadData *thread = &threadData[i];

			// Create one command pool for each thread
			VkCommandPoolCreateInfo cmdPoolInfo = vks::initializers::commandPoolCreateInfo();
			cmdPoolInfo.queueFamilyIndex = swapChain.queueNodeIndex;
			cmdPoolInfo.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
			VK_CHECK_RESULT(vkCreateCommandPool(device, &cmdPoolInfo, nullptr, &thread->commandPool));

			// One secondary command buffer per object that is updated by this thread
			thread->commandBuffer.resize(numObjectsPerThread);
			// Generate secondary command buffers for each thread
			VkCommandBufferAllocateInfo secondaryCmdBufAllocateInfo =
				vks::initializers::commandBufferAllocateInfo(
					thread->commandPool,
					VK_COMMAND_BUFFER_LEVEL_SECONDARY,
					thread->commandBuffer.size());
			VK_CHECK_RESULT(vkAllocateCommandBuffers(device, &secondaryCmdBufAllocateInfo, thread->commandBuffer.data()));

			thread->pushConstBlock.resize(numObjectsPerThread);
			thread->objectData.resize(numObjectsPerThread);

			for (uint32_t j = 0; j < numObjectsPerThread; j++)
			{
				//y1 = 8 - count % 512;
				//x1 = 8 - count / 16;
				//int left = (count % 2)? -1: 1;
				if (x1 >= 32)
				{
					x1 -= 32;
					y1++;
				}
				//printf("\n%d %d %d\n", count, x1, y1);

				thread->objectData[j].pos = glm::vec3((x1_init_pos + x1) * x1_offset, (y1_init_pos + y1) *y1_offset, -10.0);

				float theta = 2.0f * float(M_PI) * uniformDist(rndGenerator);
				float phi = acos(1.0f - 2.0f * uniformDist(rndGenerator));
				//thread->objectData[j].pos = glm::vec3(sin(phi) * cos(theta), 0.0f, cos(phi)) * 35.0f;

				thread->objectData[j].rotation = glm::vec3(-90.0f, 0.0f, 0.0f);
				thread->objectData[j].deltaT = 0.0; // rnd(1.0f);
				thread->objectData[j].rotationDir = 1.0f; // (rnd(100.0f) < 50.0f) ? 1.0f : -1.0f;
				thread->objectData[j].rotationSpeed = 0.0; // (2.0f + rnd(4.0f)) * thread->objectData[j].rotationDir;
				thread->objectData[j].scale = 1.0f; // 0.75f + rnd(0.5f);
/**/
				thread->pushConstBlock[j].color = glm::vec3(rnd(1.0f), rnd(1.0f), rnd(1.0f));

				count++;
				x1++;

			//	if (count > 64)
			//		break;

			}


			//if (count > 64)
			//	break;
		}

	}

	// Builds the secondary command buffer for each thread
	void threadRenderCode(uint32_t threadIndex, uint32_t cmdBufferIndex, VkCommandBufferInheritanceInfo inheritanceInfo)
	{
		ThreadData *thread = &threadData[threadIndex];
		if (thread->objectData.size() <= 0)
			return;

		ObjectData *objectData = &thread->objectData[cmdBufferIndex];

		// Check visibility against view frustum
		objectData->visible = frustum.checkSphere(objectData->pos, objectSphereDim * 0.5f);

		if (!objectData->visible)
		{
			return;
		}

		VkCommandBufferBeginInfo commandBufferBeginInfo = vks::initializers::commandBufferBeginInfo();
		commandBufferBeginInfo.flags = VK_COMMAND_BUFFER_USAGE_RENDER_PASS_CONTINUE_BIT;
		commandBufferBeginInfo.pInheritanceInfo = &inheritanceInfo;

		VkCommandBuffer cmdBuffer = thread->commandBuffer[cmdBufferIndex];

		VK_CHECK_RESULT(vkBeginCommandBuffer(cmdBuffer, &commandBufferBeginInfo));

		VkViewport viewport = vks::initializers::viewport((float)width, (float)height, 0.0f, 1.0f);
		vkCmdSetViewport(cmdBuffer, 0, 1, &viewport);

		VkRect2D scissor = vks::initializers::rect2D(width, height, 0, 0);
		vkCmdSetScissor(cmdBuffer, 0, 1, &scissor);

		vkCmdBindPipeline(cmdBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, pipelines.phong);

		// Update
		/*objectData->rotation.y += 2.5f * objectData->rotationSpeed * frameTimer;
		if (objectData->rotation.y > 360.0f)
		{
			objectData->rotation.y -= 360.0f;
		}
		objectData->deltaT += 0.15f * frameTimer;
		if (objectData->deltaT > 1.0f)
			objectData->deltaT -= 1.0f;
		objectData->pos.y = sin(glm::radians(objectData->deltaT * 360.0f)) * 2.5f;*/
		objectData->model = glm::translate(glm::mat4(), objectData->pos);
		//objectData->model = glm::rotate(objectData->model, -sinf(glm::radians(objectData->deltaT * 360.0f)) * 0.25f, glm::vec3(objectData->rotationDir, 0.0f, 0.0f));
		objectData->model = glm::rotate(objectData->model, glm::radians(objectData->rotation.x), glm::vec3(1.0f, 0.0f, 0.0f));
		//objectData->model = glm::rotate(objectData->model, glm::radians(objectData->deltaT * 360.0f), glm::vec3(0.0f, objectData->rotationDir, 0.0f));
		//objectData->model = glm::scale(objectData->model, glm::vec3(objectData->scale));

		thread->pushConstBlock[cmdBufferIndex].mvp = matrices.projection * matrices.view * objectData->model;

		// Update shader push constant block
		// Contains model view matrix
		vkCmdPushConstants(
			cmdBuffer,
			pipelineLayout,
			VK_SHADER_STAGE_VERTEX_BIT,
			0,
			sizeof(ThreadPushConstantBlock),
			&thread->pushConstBlock[cmdBufferIndex]);

		VkDeviceSize offsets[1] = { 0 };
		vkCmdBindVertexBuffers(cmdBuffer, 0, 1, &models.ufo.vertices.buffer, offsets);
		vkCmdBindIndexBuffer(cmdBuffer, models.ufo.indices.buffer, 0, VK_INDEX_TYPE_UINT32);
		vkCmdDrawIndexed(cmdBuffer, models.ufo.indexCount, 1, 0, 0, 0);

		VK_CHECK_RESULT(vkEndCommandBuffer(cmdBuffer));
	}

	void updateSecondaryCommandBuffer(VkCommandBufferInheritanceInfo inheritanceInfo)
	{
		
		// Secondary command buffer for the sky sphere
		VkCommandBufferBeginInfo commandBufferBeginInfo = vks::initializers::commandBufferBeginInfo();
		commandBufferBeginInfo.flags = VK_COMMAND_BUFFER_USAGE_RENDER_PASS_CONTINUE_BIT;
		commandBufferBeginInfo.pInheritanceInfo = &inheritanceInfo;

		VK_CHECK_RESULT(vkBeginCommandBuffer(secondaryCommandBuffer, &commandBufferBeginInfo));

		VkViewport viewport = vks::initializers::viewport((float)width, (float)height, 0.0f, 1.0f);
		vkCmdSetViewport(secondaryCommandBuffer, 0, 1, &viewport);

		VkRect2D scissor = vks::initializers::rect2D(width, height, 0, 0);
		vkCmdSetScissor(secondaryCommandBuffer, 0, 1, &scissor);

		vkCmdBindPipeline(secondaryCommandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, pipelines.starsphere);

		glm::mat4 projection = glm::perspective(glm::radians(60.0f), (float)width / (float)height, 0.1f, 256.0f);
		glm::mat4 view = glm::mat4();
		view = glm::rotate(view, glm::radians(rotation.x), glm::vec3(1.0f, 0.0f, 0.0f));
		view = glm::rotate(view, glm::radians(rotation.y), glm::vec3(0.0f, 1.0f, 0.0f));
		view = glm::rotate(view, glm::radians(rotation.z), glm::vec3(0.0f, 0.0f, 1.0f));

		glm::mat4 mvp = projection * view;

		vkCmdPushConstants(
			secondaryCommandBuffer,
			pipelineLayout,
			VK_SHADER_STAGE_VERTEX_BIT,
			0,
			sizeof(mvp),
			&mvp);

		VkDeviceSize offsets[1] = { 0 };
		vkCmdBindVertexBuffers(secondaryCommandBuffer, 0, 1, &models.skysphere.vertices.buffer, offsets);
		vkCmdBindIndexBuffer(secondaryCommandBuffer, models.skysphere.indices.buffer, 0, VK_INDEX_TYPE_UINT32);
		vkCmdDrawIndexed(secondaryCommandBuffer, models.skysphere.indexCount, 1, 0, 0, 0);

		//scene->render(secondaryCommandBuffer, wireframe);
		
		VK_CHECK_RESULT(vkEndCommandBuffer(secondaryCommandBuffer));

	}

	// Updates the secondary command buffers using a thread pool 
	// and puts them into the primary command buffer that's 
	// lat submitted to the queue for rendering
	void updateCommandBuffers(VkFramebuffer frameBuffer)
	{
		VkCommandBufferBeginInfo cmdBufInfo = vks::initializers::commandBufferBeginInfo();

		VkClearValue clearValues[3];
		clearValues[0].color = defaultClearColor;
		clearValues[0].color = { { 0.0f, 0.0f, 0.2f, 0.0f } };
		clearValues[1].color = { { 0.0f, 0.0f, 0.2f, 0.0f } };
		clearValues[2].depthStencil = { 1.0f, 0 };

		VkRenderPassBeginInfo renderPassBeginInfo = vks::initializers::renderPassBeginInfo();
		renderPassBeginInfo.renderPass = renderPass;
		renderPassBeginInfo.renderArea.offset.x = 0;
		renderPassBeginInfo.renderArea.offset.y = 0;
		renderPassBeginInfo.renderArea.extent.width = width;
		renderPassBeginInfo.renderArea.extent.height = height;
		renderPassBeginInfo.clearValueCount = 3;
		renderPassBeginInfo.pClearValues = clearValues;
		renderPassBeginInfo.framebuffer = frameBuffer;

		// Set target frame buffer

		VK_CHECK_RESULT(vkBeginCommandBuffer(primaryCommandBuffer, &cmdBufInfo));

		// The primary command buffer does not contain any rendering commands
		// These are stored (and retrieved) from the secondary command buffers
		vkCmdBeginRenderPass(primaryCommandBuffer, &renderPassBeginInfo, VK_SUBPASS_CONTENTS_SECONDARY_COMMAND_BUFFERS);

		// Inheritance info for the secondary command buffers
		VkCommandBufferInheritanceInfo inheritanceInfo = vks::initializers::commandBufferInheritanceInfo();
		inheritanceInfo.renderPass = renderPass;
		// Secondary command buffer also use the currently active framebuffer
		inheritanceInfo.framebuffer = frameBuffer;

		// Contains the list of secondary command buffers to be executed
		std::vector<VkCommandBuffer> commandBuffers;


		// Add a job to the thread's queue for each object to be rendered
		for (uint32_t t = 0; t < numThreads; t++)
		{
			for (uint32_t i = 0; i < numObjectsPerThread; i++)
			{
				threadPool.threads[t]->addJob([=] { threadRenderCode(t, i, inheritanceInfo); });
			}
		}

		threadPool.wait();

		// Only submit if object is within the current view frustum
		for (uint32_t t = 0; t < numThreads; t++)
		{
			for (uint32_t i = 0; i < numObjectsPerThread; i++)
			{
				if(threadData[t].objectData.size() > 0)
				{
					if (threadData[t].objectData[i].visible)
					{
						commandBuffers.push_back(threadData[t].commandBuffer[i]);
					}
				}
			}
		}

		// Secondary command buffer with star background sphere
		//updateSecondaryCommandBuffer(inheritanceInfo);
		//commandBuffers.push_back(secondaryCommandBuffer);

		// Execute render commands from the secondary command buffer
		if(commandBuffers.size() > 0)
			vkCmdExecuteCommands(primaryCommandBuffer, commandBuffers.size(), commandBuffers.data());

		vkCmdEndRenderPass(primaryCommandBuffer);

		VK_CHECK_RESULT(vkEndCommandBuffer(primaryCommandBuffer));
	}

	virtual void keyPressed(uint32_t keyCode)
	{
		switch (keyCode)
		{
		case KEY_SPACE:
		case GAMEPAD_BUTTON_A:
			if (deviceFeatures.fillModeNonSolid) {
				wireframe = !wireframe;
				reBuildCommandBuffers();
			}
			break;
		case KEY_P:
			scene->renderSingleScenePart = !scene->renderSingleScenePart;
			reBuildCommandBuffers();
			updateTextOverlay();
			break;
		case KEY_KPADD:
			scene->scenePartIndex = (scene->scenePartIndex < static_cast<uint32_t>(scene->meshes.size())) ? scene->scenePartIndex + 1 : 0;
			reBuildCommandBuffers();
			updateTextOverlay();
			break;
		case KEY_KPSUB:
			scene->scenePartIndex = (scene->scenePartIndex > 0) ? scene->scenePartIndex - 1 : static_cast<uint32_t>(scene->meshes.size()) - 1;
			updateTextOverlay();
			reBuildCommandBuffers();
			break;
		case KEY_L:
			attachLight = !attachLight;
			updateUniformBuffers();
			break;
		}
	}

	virtual void getOverlayText(VulkanTextOverlay *textOverlay)
	{
		if (deviceFeatures.fillModeNonSolid) {
#if defined(__ANDROID__)
			textOverlay->addText("Press \"Button A\" to toggle wireframe", 5.0f, 85.0f, VulkanTextOverlay::alignLeft);
#else
			textOverlay->addText("Press \"space\" to toggle wireframe", 5.0f, 85.0f, VulkanTextOverlay::alignLeft);
			if ((scene) && (scene->renderSingleScenePart))
			{
				textOverlay->addText("Rendering mesh " + std::to_string(scene->scenePartIndex + 1) + " of " + std::to_string(static_cast<uint32_t>(scene->meshes.size())) + "(\"p\" to toggle)", 5.0f, 100.0f, VulkanTextOverlay::alignLeft);
			}
			else
			{
				textOverlay->addText("Rendering whole scene (\"p\" to toggle)", 5.0f, 100.0f, VulkanTextOverlay::alignLeft);
			}
#endif
		}
	}
};

VULKAN_EXAMPLE_MAIN()