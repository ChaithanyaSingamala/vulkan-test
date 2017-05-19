/*
* Vulkan Samples
*
* Copyright (C) 2015-2016 Valve Corporation
* Copyright (C) 2015-2016 LunarG, Inc.
*
* Licensed under the Apache License, Version 2.0 (the "License");
* you may not use this file except in compliance with the License.
* You may obtain a copy of the License at
*
*     http://www.apache.org/licenses/LICENSE-2.0
*
* Unless required by applicable law or agreed to in writing, software
* distributed under the License is distributed on an "AS IS" BASIS,
* WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
* See the License for the specific language governing permissions and
* limitations under the License.
*/

/*
VULKAN_SAMPLE_SHORT_DESCRIPTION
Inititalize Swapchain
*/

/* This is part of the draw cube progression */

#include <util_init.hpp>
#include <assert.h>
#include <cstdlib>

inline char const* vkErrorToStr(VkResult errorCode)
{
	switch (errorCode)
	{
	case VK_SUCCESS: return "VK_SUCCESS";
	case VK_NOT_READY: return "VK_NOT_READY";
	case VK_TIMEOUT: return "VK_TIMEOUT";
	case VK_EVENT_SET: return "VK_EVENT_SET";
	case VK_EVENT_RESET: return "VK_EVENT_RESET";
	case VK_INCOMPLETE: return "VK_INCOMPLETE";
	case VK_ERROR_OUT_OF_HOST_MEMORY: return "VK_ERROR_OUT_OF_HOST_MEMORY";
	case VK_ERROR_OUT_OF_DEVICE_MEMORY: return "VK_ERROR_OUT_OF_DEVICE_MEMORY";
	case VK_ERROR_INITIALIZATION_FAILED: return "VK_ERROR_INITIALIZATION_FAILED";
	case VK_ERROR_DEVICE_LOST: return "VK_ERROR_DEVICE_LOST";
	case VK_ERROR_MEMORY_MAP_FAILED: return "VK_ERROR_MEMORY_MAP_FAILED";
	case VK_ERROR_LAYER_NOT_PRESENT: return "VK_ERROR_LAYER_NOT_PRESENT";
	case VK_ERROR_EXTENSION_NOT_PRESENT: return "VK_ERROR_EXTENSION_NOT_PRESENT";
	case VK_ERROR_FEATURE_NOT_PRESENT: return "VK_ERROR_FEATURE_NOT_PRESENT";
	case VK_ERROR_INCOMPATIBLE_DRIVER: return "VK_ERROR_INCOMPATIBLE_DRIVER";
	case VK_ERROR_TOO_MANY_OBJECTS: return "VK_ERROR_TOO_MANY_OBJECTS";
	case VK_ERROR_FORMAT_NOT_SUPPORTED: return "VK_ERROR_FORMAT_NOT_SUPPORTED";
	case VK_ERROR_SURFACE_LOST_KHR: return "VK_ERROR_SURFACE_LOST_KHR";
	case VK_SUBOPTIMAL_KHR: return "VK_SUBOPTIMAL_KHR";
	case VK_ERROR_OUT_OF_DATE_KHR: return "VK_ERROR_OUT_OF_DATE_KHR";
	case VK_ERROR_INCOMPATIBLE_DISPLAY_KHR: return "VK_ERROR_INCOMPATIBLE_DISPLAY_KHR";
	case VK_ERROR_NATIVE_WINDOW_IN_USE_KHR: return "VK_ERROR_NATIVE_WINDOW_IN_USE_KHR";
	case VK_ERROR_VALIDATION_FAILED_EXT: return "VK_ERROR_VALIDATION_FAILED_EXT";
	//case VK_ERROR_FRAGMENTED_POOL: return "VK_ERROR_FRAGMENTED_POOL";
	case VK_ERROR_INVALID_SHADER_NV: return "VK_ERROR_INVALID_SHADER_NV";
		// Declared here to Hide the Warnings.
	default: return "";
	}
}

inline bool vkIsSuccessful(VkResult result, const char* msg)
{
	if (result != VK_SUCCESS)
	{
		printf( "Failed: %s. Vulkan has raised an error: %s", msg, vkErrorToStr(result));
		return false;
	}
	return true;
}

int sample_main(int argc, char *argv[]) {
	VkResult U_ASSERT_ONLY res;
	struct sample_info info = {};
	char sample_title[] = "Swapchain Initialization Sample";

	init_global_layer_properties(info);
	init_instance_extension_names(info);
	init_device_extension_names(info);
	init_instance(info, sample_title);
	init_enumerate_device(info);
	init_window_size(info, 50, 50);
	init_connection(info);
	init_window(info);

	/* VULKAN_KEY_START */
	// Construct the surface description:
#ifdef _WIN32
	VkWin32SurfaceCreateInfoKHR createInfo = {};
	createInfo.sType = VK_STRUCTURE_TYPE_WIN32_SURFACE_CREATE_INFO_KHR;
	createInfo.pNext = NULL;
	createInfo.hinstance = info.connection;
	createInfo.hwnd = info.window;
	res = vkCreateWin32SurfaceKHR(info.inst, &createInfo,
		NULL, &info.surface);
#elif defined(__ANDROID__)
	GET_INSTANCE_PROC_ADDR(info.inst, CreateAndroidSurfaceKHR);

	VkAndroidSurfaceCreateInfoKHR createInfo;
	createInfo.sType = VK_STRUCTURE_TYPE_ANDROID_SURFACE_CREATE_INFO_KHR;
	createInfo.pNext = nullptr;
	createInfo.flags = 0;
	createInfo.window = AndroidGetApplicationWindow();
	res = info.fpCreateAndroidSurfaceKHR(info.inst, &createInfo, nullptr, &info.surface);
#else  // !__ANDROID__ && !_WIN32
#if 0
	VkXcbSurfaceCreateInfoKHR createInfo = {};
	createInfo.sType = VK_STRUCTURE_TYPE_XCB_SURFACE_CREATE_INFO_KHR;
	createInfo.pNext = NULL;
	createInfo.connection = info.connection;
	createInfo.window = info.window;
	res = vkCreateXcbSurfaceKHR(info.inst, &createInfo, NULL, &info.surface);
#endif
#if 1
	VkDisplayPropertiesKHR properties;
	uint32_t propertiesCount = 1;
	if (vkGetPhysicalDeviceDisplayPropertiesKHR)
	{
		vkGetPhysicalDeviceDisplayPropertiesKHR(info.gpus[0], &propertiesCount, &properties);
	}

	std::string supportedTransforms;
	if (properties.supportedTransforms & VK_SURFACE_TRANSFORM_IDENTITY_BIT_KHR) { supportedTransforms.append("none "); }
	if (properties.supportedTransforms & VK_SURFACE_TRANSFORM_ROTATE_90_BIT_KHR) { supportedTransforms.append("rot90 "); }
	if (properties.supportedTransforms & VK_SURFACE_TRANSFORM_ROTATE_180_BIT_KHR) { supportedTransforms.append("rot180 "); }
	if (properties.supportedTransforms & VK_SURFACE_TRANSFORM_ROTATE_270_BIT_KHR) { supportedTransforms.append("rot270 "); }
	if (properties.supportedTransforms & VK_SURFACE_TRANSFORM_HORIZONTAL_MIRROR_BIT_KHR) { supportedTransforms.append("h_mirror "); }
	if (properties.supportedTransforms & VK_SURFACE_TRANSFORM_HORIZONTAL_MIRROR_ROTATE_90_BIT_KHR) { supportedTransforms.append("h_mirror+rot90 "); }
	if (properties.supportedTransforms & VK_SURFACE_TRANSFORM_HORIZONTAL_MIRROR_ROTATE_180_BIT_KHR) { supportedTransforms.append("hmirror+rot180 "); }
	if (properties.supportedTransforms & VK_SURFACE_TRANSFORM_HORIZONTAL_MIRROR_ROTATE_270_BIT_KHR) { supportedTransforms.append("hmirror+rot270 "); }
	if (properties.supportedTransforms & VK_SURFACE_TRANSFORM_INHERIT_BIT_KHR) { supportedTransforms.append("inherit "); }
	printf("**** Display Properties: ****\n");
	printf("name: %s\n", properties.displayName);
	printf("size: %dx%d\n", properties.physicalDimensions.width, properties.physicalDimensions.height);
	printf("resolution: %dx%d\n", properties.physicalResolution.width, properties.physicalResolution.height);
	printf("transforms: %s\n", supportedTransforms.c_str());
	printf("plane reordering?: %s\n", properties.planeReorderPossible ? "yes" : "no");
	printf("persistent conents?: %s\n", properties.persistentContent ? "yes" : "no");

	//displayHandle.nativeDisplay = properties.display;

	glm::uint32 modeCount = 0;
	vkGetDisplayModePropertiesKHR(info.gpus[0], properties.display, &modeCount, NULL);
	std::vector<VkDisplayModePropertiesKHR> modeProperties; modeProperties.resize(modeCount);
	vkGetDisplayModePropertiesKHR(info.gpus[0], properties.display, &modeCount, modeProperties.data());

	printf("Display Modes:\n");
	for (uint32_t i = 0; i < modeCount; ++i)
	{
		printf("\t[%u] %ux%u @%uHz\n", i, modeProperties[i].parameters.visibleRegion.width,
			modeProperties[i].parameters.visibleRegion.height, modeProperties[i].parameters.refreshRate);
	}

	VkDisplaySurfaceCreateInfoKHR surfaceCreateInfo;

	surfaceCreateInfo.sType = VK_STRUCTURE_TYPE_DISPLAY_SURFACE_CREATE_INFO_KHR;
	surfaceCreateInfo.pNext = NULL;

	surfaceCreateInfo.displayMode = modeProperties[0].displayMode;
	surfaceCreateInfo.planeIndex = 0;
	surfaceCreateInfo.planeStackIndex = 0;
	surfaceCreateInfo.transform = VK_SURFACE_TRANSFORM_IDENTITY_BIT_KHR;
	surfaceCreateInfo.globalAlpha = 0.0f;
	surfaceCreateInfo.alphaMode = VK_DISPLAY_PLANE_ALPHA_PER_PIXEL_BIT_KHR;
	surfaceCreateInfo.imageExtent = modeProperties[0].parameters.visibleRegion;

	if (!vkIsSuccessful(vkCreateDisplayPlaneSurfaceKHR(info.inst, &surfaceCreateInfo,
		NULL, &info.surface), "Could not create DisplayPlane Surface\n"))
	{
		return false;
	}
#endif
#endif // _WIN32


	//assert(res == VK_SUCCESS);

	// Iterate over each queue to learn whether it supports presenting:
	VkBool32 *pSupportsPresent =
		(VkBool32 *)malloc(info.queue_family_count * sizeof(VkBool32));
	for (uint32_t i = 0; i < info.queue_family_count; i++) {
		vkGetPhysicalDeviceSurfaceSupportKHR(info.gpus[0], i, info.surface,
			&pSupportsPresent[i]);
	}

	// Search for a graphics and a present queue in the array of queue
	// families, try to find one that supports both
	info.graphics_queue_family_index = UINT32_MAX;
	info.present_queue_family_index = UINT32_MAX;
	for (uint32_t i = 0; i < info.queue_family_count; ++i) {
		if ((info.queue_props[i].queueFlags & VK_QUEUE_GRAPHICS_BIT) != 0) {
			if (info.graphics_queue_family_index == UINT32_MAX)
				info.graphics_queue_family_index = i;

			if (pSupportsPresent[i] == VK_TRUE) {
				info.graphics_queue_family_index = i;
				info.present_queue_family_index = i;
				break;
			}
		}
	}

	if (info.present_queue_family_index == UINT32_MAX) {
		// If didn't find a queue that supports both graphics and present, then
		// find a separate present queue.
		for (size_t i = 0; i < info.queue_family_count; ++i)
			if (pSupportsPresent[i] == VK_TRUE) {
				info.present_queue_family_index = i;
				break;
			}
	}
	free(pSupportsPresent);

	// Generate error if could not find queues that support graphics
	// and present
	if (info.graphics_queue_family_index == UINT32_MAX ||
		info.present_queue_family_index == UINT32_MAX) {
		std::cout << "Could not find a queues for graphics and "
			"present\n";
		exit(-1);
	}

	init_device(info);

	// Get the list of VkFormats that are supported:
	uint32_t formatCount;
	res = vkGetPhysicalDeviceSurfaceFormatsKHR(info.gpus[0], info.surface,
		&formatCount, NULL);
	assert(res == VK_SUCCESS);
	VkSurfaceFormatKHR *surfFormats =
		(VkSurfaceFormatKHR *)malloc(formatCount * sizeof(VkSurfaceFormatKHR));
	res = vkGetPhysicalDeviceSurfaceFormatsKHR(info.gpus[0], info.surface,
		&formatCount, surfFormats);
	assert(res == VK_SUCCESS);
	// If the format list includes just one entry of VK_FORMAT_UNDEFINED,
	// the surface has no preferred format.  Otherwise, at least one
	// supported format will be returned.
	if (formatCount == 1 && surfFormats[0].format == VK_FORMAT_UNDEFINED) {
		info.format = VK_FORMAT_B8G8R8A8_UNORM;
	}
	else {
		assert(formatCount >= 1);
		info.format = surfFormats[0].format;
	}
	free(surfFormats);

	VkSurfaceCapabilitiesKHR surfCapabilities;

	res = vkGetPhysicalDeviceSurfaceCapabilitiesKHR(info.gpus[0], info.surface,
		&surfCapabilities);
	assert(res == VK_SUCCESS);

	uint32_t presentModeCount;
	res = vkGetPhysicalDeviceSurfacePresentModesKHR(info.gpus[0], info.surface,
		&presentModeCount, NULL);
	assert(res == VK_SUCCESS);
	VkPresentModeKHR *presentModes =
		(VkPresentModeKHR *)malloc(presentModeCount * sizeof(VkPresentModeKHR));

	res = vkGetPhysicalDeviceSurfacePresentModesKHR(
		info.gpus[0], info.surface, &presentModeCount, presentModes);
	assert(res == VK_SUCCESS);

	VkExtent2D swapchainExtent;
	// width and height are either both 0xFFFFFFFF, or both not 0xFFFFFFFF.
	if (surfCapabilities.currentExtent.width == 0xFFFFFFFF) {
		// If the surface size is undefined, the size is set to
		// the size of the images requested.
		swapchainExtent.width = info.width;
		swapchainExtent.height = info.height;
		if (swapchainExtent.width < surfCapabilities.minImageExtent.width) {
			swapchainExtent.width = surfCapabilities.minImageExtent.width;
		}
		else if (swapchainExtent.width >
			surfCapabilities.maxImageExtent.width) {
			swapchainExtent.width = surfCapabilities.maxImageExtent.width;
		}

		if (swapchainExtent.height < surfCapabilities.minImageExtent.height) {
			swapchainExtent.height = surfCapabilities.minImageExtent.height;
		}
		else if (swapchainExtent.height >
			surfCapabilities.maxImageExtent.height) {
			swapchainExtent.height = surfCapabilities.maxImageExtent.height;
		}
	}
	else {
		// If the surface size is defined, the swap chain size must match
		swapchainExtent = surfCapabilities.currentExtent;
	}

	// The FIFO present mode is guaranteed by the spec to be supported
	VkPresentModeKHR swapchainPresentMode = VK_PRESENT_MODE_FIFO_KHR;

	// Determine the number of VkImage's to use in the swap chain.
	// We need to acquire only 1 presentable image at at time.
	// Asking for minImageCount images ensures that we can acquire
	// 1 presentable image as long as we present it before attempting
	// to acquire another.
	uint32_t desiredNumberOfSwapChainImages = surfCapabilities.minImageCount;

	VkSurfaceTransformFlagBitsKHR preTransform;
	if (surfCapabilities.supportedTransforms &
		VK_SURFACE_TRANSFORM_IDENTITY_BIT_KHR) {
		preTransform = VK_SURFACE_TRANSFORM_IDENTITY_BIT_KHR;
	}
	else {
		preTransform = surfCapabilities.currentTransform;
	}

	VkSwapchainCreateInfoKHR swapchain_ci = {};
	swapchain_ci.sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR;
	swapchain_ci.pNext = NULL;
	swapchain_ci.surface = info.surface;
	swapchain_ci.minImageCount = desiredNumberOfSwapChainImages;
	swapchain_ci.imageFormat = info.format;
	swapchain_ci.imageExtent.width = swapchainExtent.width;
	swapchain_ci.imageExtent.height = swapchainExtent.height;
	swapchain_ci.preTransform = preTransform;
	swapchain_ci.compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
	swapchain_ci.imageArrayLayers = 1;
	swapchain_ci.presentMode = swapchainPresentMode;
	swapchain_ci.oldSwapchain = VK_NULL_HANDLE;
	swapchain_ci.clipped = true;
	swapchain_ci.imageColorSpace = VK_COLORSPACE_SRGB_NONLINEAR_KHR;
	swapchain_ci.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
	swapchain_ci.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
	swapchain_ci.queueFamilyIndexCount = 0;
	swapchain_ci.pQueueFamilyIndices = NULL;
	uint32_t queueFamilyIndices[2] = {
		(uint32_t)info.graphics_queue_family_index,
		(uint32_t)info.present_queue_family_index };
	if (info.graphics_queue_family_index != info.present_queue_family_index) {
		// If the graphics and present queues are from different queue families,
		// we either have to explicitly transfer ownership of images between
		// the queues, or we have to create the swapchain with imageSharingMode
		// as VK_SHARING_MODE_CONCURRENT
		swapchain_ci.imageSharingMode = VK_SHARING_MODE_CONCURRENT;
		swapchain_ci.queueFamilyIndexCount = 2;
		swapchain_ci.pQueueFamilyIndices = queueFamilyIndices;
	}

	res = vkCreateSwapchainKHR(info.device, &swapchain_ci, NULL,
		&info.swap_chain);
	assert(res == VK_SUCCESS);

	res = vkGetSwapchainImagesKHR(info.device, info.swap_chain,
		&info.swapchainImageCount, NULL);
	assert(res == VK_SUCCESS);

	VkImage *swapchainImages =
		(VkImage *)malloc(info.swapchainImageCount * sizeof(VkImage));
	assert(swapchainImages);
	res = vkGetSwapchainImagesKHR(info.device, info.swap_chain,
		&info.swapchainImageCount, swapchainImages);
	assert(res == VK_SUCCESS);

	info.buffers.resize(info.swapchainImageCount);
	for (uint32_t i = 0; i < info.swapchainImageCount; i++) {
		info.buffers[i].image = swapchainImages[i];
	}
	free(swapchainImages);

	for (uint32_t i = 0; i < info.swapchainImageCount; i++) {
		VkImageViewCreateInfo color_image_view = {};
		color_image_view.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
		color_image_view.pNext = NULL;
		color_image_view.flags = 0;
		color_image_view.image = info.buffers[i].image;
		color_image_view.viewType = VK_IMAGE_VIEW_TYPE_2D;
		color_image_view.format = info.format;
		color_image_view.components.r = VK_COMPONENT_SWIZZLE_R;
		color_image_view.components.g = VK_COMPONENT_SWIZZLE_G;
		color_image_view.components.b = VK_COMPONENT_SWIZZLE_B;
		color_image_view.components.a = VK_COMPONENT_SWIZZLE_A;
		color_image_view.subresourceRange.aspectMask =
			VK_IMAGE_ASPECT_COLOR_BIT;
		color_image_view.subresourceRange.baseMipLevel = 0;
		color_image_view.subresourceRange.levelCount = 1;
		color_image_view.subresourceRange.baseArrayLayer = 0;
		color_image_view.subresourceRange.layerCount = 1;

		res = vkCreateImageView(info.device, &color_image_view, NULL,
			&info.buffers[i].view);
		assert(res == VK_SUCCESS);
	}

	/* VULKAN_KEY_END */

	/* Clean Up */
	for (uint32_t i = 0; i < info.swapchainImageCount; i++) {
		vkDestroyImageView(info.device, info.buffers[i].view, NULL);
	}
	vkDestroySwapchainKHR(info.device, info.swap_chain, NULL);
	destroy_device(info);
	destroy_window(info);
	destroy_instance(info);

	return 0;
}
