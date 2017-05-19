/*
* Class wrapping access to the swap chain
* 
* A swap chain is a collection of framebuffers used for rendering and presentation to the windowing system
*
* Copyright (C) 2016 by Sascha Willems - www.saschawillems.de
*
* This code is licensed under the MIT license (MIT) (http://opensource.org/licenses/MIT)
*/

#pragma once

#include <stdlib.h>
#include <string>
#include <fstream>
#include <assert.h>
#include <stdio.h>
#include <vector>
#ifdef _WIN32
#include <windows.h>
#include <fcntl.h>
#include <io.h>
#else
#endif

#include <vulkan/vulkan.h>
#include "VulkanTools.h"

#ifdef __ANDROID__
#include "vulkanandroid.h"
#endif

// Macro to get a procedure address based on a vulkan instance
#define GET_INSTANCE_PROC_ADDR(inst, entrypoint)                        \
{                                                                       \
	fp##entrypoint = reinterpret_cast<PFN_vk##entrypoint>(vkGetInstanceProcAddr(inst, "vk"#entrypoint)); \
	if (fp##entrypoint == NULL)                                         \
	{																    \
		exit(1);                                                        \
	}                                                                   \
}

// Macro to get a procedure address based on a vulkan device
#define GET_DEVICE_PROC_ADDR(dev, entrypoint)                           \
{                                                                       \
	fp##entrypoint = reinterpret_cast<PFN_vk##entrypoint>(vkGetDeviceProcAddr(dev, "vk"#entrypoint));   \
	if (fp##entrypoint == NULL)                                         \
	{																    \
		exit(1);                                                        \
	}                                                                   \
}

typedef struct _SwapChainBuffers {
	VkImage image;
	VkImageView view;
} SwapChainBuffer;

class VulkanSwapChain
{
private: 
	VkInstance instance;
	VkDevice device;
	VkPhysicalDevice physicalDevice;
	VkSurfaceKHR surface;
	// Function pointers
	PFN_vkGetPhysicalDeviceSurfaceSupportKHR fpGetPhysicalDeviceSurfaceSupportKHR;
	PFN_vkGetPhysicalDeviceSurfaceCapabilitiesKHR fpGetPhysicalDeviceSurfaceCapabilitiesKHR; 
	PFN_vkGetPhysicalDeviceSurfaceFormatsKHR fpGetPhysicalDeviceSurfaceFormatsKHR;
	PFN_vkGetPhysicalDeviceSurfacePresentModesKHR fpGetPhysicalDeviceSurfacePresentModesKHR;
	PFN_vkCreateSwapchainKHR fpCreateSwapchainKHR;
	PFN_vkDestroySwapchainKHR fpDestroySwapchainKHR;
	PFN_vkGetSwapchainImagesKHR fpGetSwapchainImagesKHR;
	PFN_vkAcquireNextImageKHR fpAcquireNextImageKHR;
	PFN_vkQueuePresentKHR fpQueuePresentKHR;
public:
	VkFormat colorFormat;
	VkColorSpaceKHR colorSpace;
	/** @brief Handle to the current swap chain, required for recreation */
	VkSwapchainKHR swapChain = VK_NULL_HANDLE;	
	uint32_t imageCount;
	std::vector<VkImage> images;
	std::vector<SwapChainBuffer> buffers;
	// Index of the deteced graphics and presenting device queue
	/** @brief Queue family index of the detected graphics and presenting device queue */
	uint32_t queueNodeIndex = UINT32_MAX;

	// Creates an os specific surface
	/**
	* Create the surface object, an abstraction for the native platform window
	*
	* @pre Windows
	* @param platformHandle HINSTANCE of the window to create the surface for
	* @param platformWindow HWND of the window to create the surface for
	*
	* @pre Android 
	* @param window A native platform window
	*
	* @pre Linux (XCB)
	* @param connection xcb connection to the X Server
	* @param window The xcb window to create the surface for
	* @note Targets other than XCB ar not yet supported
	*/
	void initSurface(
#ifdef _WIN32
		void* platformHandle, void* platformWindow
#else
#ifdef __ANDROID__
		ANativeWindow* window
#else
#ifdef _DIRECT2DISPLAY
	uint32_t width, uint32_t height
#else
#ifdef VK_USE_PLATFORM_WAYLAND_KHR
	wl_display *display, wl_surface *window
#else
	xcb_connection_t* connection, xcb_window_t window
#endif
#endif
#endif
#endif
	)
	{
		VkResult err;

		// Create the os-specific surface
#ifdef _WIN32
		VkWin32SurfaceCreateInfoKHR surfaceCreateInfo = {};
		surfaceCreateInfo.sType = VK_STRUCTURE_TYPE_WIN32_SURFACE_CREATE_INFO_KHR;
		surfaceCreateInfo.hinstance = (HINSTANCE)platformHandle;
		surfaceCreateInfo.hwnd = (HWND)platformWindow;
		err = vkCreateWin32SurfaceKHR(instance, &surfaceCreateInfo, nullptr, &surface);
#else
#ifdef __ANDROID__
		VkAndroidSurfaceCreateInfoKHR surfaceCreateInfo = {};
		surfaceCreateInfo.sType = VK_STRUCTURE_TYPE_ANDROID_SURFACE_CREATE_INFO_KHR;
		surfaceCreateInfo.window = window;
		err = vkCreateAndroidSurfaceKHR(instance, &surfaceCreateInfo, NULL, &surface);
#else
#if defined(_DIRECT2DISPLAY)
		printf("\n swapchain 11111111111111111111111\n");
		createDirect2DisplaySurface(width, height);
		printf("\n swapchain 999999999999999999999999\n");
#else
#ifdef VK_USE_PLATFORM_WAYLAND_KHR
		VkWaylandSurfaceCreateInfoKHR surfaceCreateInfo = {};
		surfaceCreateInfo.sType = VK_STRUCTURE_TYPE_WAYLAND_SURFACE_CREATE_INFO_KHR;
		surfaceCreateInfo.display = display;
		surfaceCreateInfo.surface = window;
		err = vkCreateWaylandSurfaceKHR(instance, &surfaceCreateInfo, nullptr, &surface);
#else
		VkXcbSurfaceCreateInfoKHR surfaceCreateInfo = {};
		surfaceCreateInfo.sType = VK_STRUCTURE_TYPE_XCB_SURFACE_CREATE_INFO_KHR;
		surfaceCreateInfo.connection = connection;
		surfaceCreateInfo.window = window;
		err = vkCreateXcbSurfaceKHR(instance, &surfaceCreateInfo, nullptr, &surface);
#endif
#endif
#endif
#endif

		// Get available queue family properties
		uint32_t queueCount;
		vkGetPhysicalDeviceQueueFamilyProperties(physicalDevice, &queueCount, NULL);
		assert(queueCount >= 1);

		std::vector<VkQueueFamilyProperties> queueProps(queueCount);
		vkGetPhysicalDeviceQueueFamilyProperties(physicalDevice, &queueCount, queueProps.data());

		// Iterate over each queue to learn whether it supports presenting:
		// Find a queue with present support
		// Will be used to present the swap chain images to the windowing system
		std::vector<VkBool32> supportsPresent(queueCount);
		for (uint32_t i = 0; i < queueCount; i++) 
		{
			fpGetPhysicalDeviceSurfaceSupportKHR(physicalDevice, i, surface, &supportsPresent[i]);
		}

		// Search for a graphics and a present queue in the array of queue
		// families, try to find one that supports both
		uint32_t graphicsQueueNodeIndex = UINT32_MAX;
		uint32_t presentQueueNodeIndex = UINT32_MAX;
		for (uint32_t i = 0; i < queueCount; i++) 
		{
			if ((queueProps[i].queueFlags & VK_QUEUE_GRAPHICS_BIT) != 0) 
			{
				if (graphicsQueueNodeIndex == UINT32_MAX) 
				{
					graphicsQueueNodeIndex = i;
				}

				if (supportsPresent[i] == VK_TRUE) 
				{
					graphicsQueueNodeIndex = i;
					presentQueueNodeIndex = i;
					break;
				}
			}
		}
		if (presentQueueNodeIndex == UINT32_MAX) 
		{	
			// If there's no queue that supports both present and graphics
			// try to find a separate present queue
			for (uint32_t i = 0; i < queueCount; ++i) 
			{
				if (supportsPresent[i] == VK_TRUE) 
				{
					presentQueueNodeIndex = i;
					break;
				}
			}
		}

		// Exit if either a graphics or a presenting queue hasn't been found
		if (graphicsQueueNodeIndex == UINT32_MAX || presentQueueNodeIndex == UINT32_MAX) 
		{
			vks::tools::exitFatal("Could not find a graphics and/or presenting queue!", "Fatal error");
		}

		// todo : Add support for separate graphics and presenting queue
		if (graphicsQueueNodeIndex != presentQueueNodeIndex) 
		{
			vks::tools::exitFatal("Separate graphics and presenting queues are not supported yet!", "Fatal error");
		}

		queueNodeIndex = graphicsQueueNodeIndex;

		// Get list of supported surface formats
		uint32_t formatCount;
		err = fpGetPhysicalDeviceSurfaceFormatsKHR(physicalDevice, surface, &formatCount, NULL);
		assert(!err);
		assert(formatCount > 0);

		std::vector<VkSurfaceFormatKHR> surfaceFormats(formatCount);
		err = fpGetPhysicalDeviceSurfaceFormatsKHR(physicalDevice, surface, &formatCount, surfaceFormats.data());
		assert(!err);

		// If the surface format list only includes one entry with VK_FORMAT_UNDEFINED,
		// there is no preferered format, so we assume VK_FORMAT_B8G8R8A8_UNORM
		if ((formatCount == 1) && (surfaceFormats[0].format == VK_FORMAT_UNDEFINED))
		{
			colorFormat = VK_FORMAT_B8G8R8A8_UNORM;
			colorSpace = surfaceFormats[0].colorSpace;
		}
		else
		{
			// iterate over the list of available surface format and
			// check for the presence of VK_FORMAT_B8G8R8A8_UNORM
			bool found_B8G8R8A8_UNORM = false;
			for (auto&& surfaceFormat : surfaceFormats)
			{
				if (surfaceFormat.format == VK_FORMAT_B8G8R8A8_UNORM)
				{
					colorFormat = surfaceFormat.format;
					colorSpace = surfaceFormat.colorSpace;
					found_B8G8R8A8_UNORM = true;
					break;
				}
			}

			// in case VK_FORMAT_B8G8R8A8_UNORM is not available
			// select the first available color format
			if (!found_B8G8R8A8_UNORM)
			{
				colorFormat = surfaceFormats[0].format;
				colorSpace = surfaceFormats[0].colorSpace;
			}
		}

	}

	/**
	* Set instance, physical and logical device to use for the swapchain and get all required function pointers
	* 
	* @param instance Vulkan instance to use
	* @param physicalDevice Physical device used to query properties and formats relevant to the swapchain
	* @param device Logical representation of the device to create the swapchain for
	*
	*/
	void connect(VkInstance instance, VkPhysicalDevice physicalDevice, VkDevice device)
	{
		this->instance = instance;
		this->physicalDevice = physicalDevice;
		this->device = device;
		GET_INSTANCE_PROC_ADDR(instance, GetPhysicalDeviceSurfaceSupportKHR);
		GET_INSTANCE_PROC_ADDR(instance, GetPhysicalDeviceSurfaceCapabilitiesKHR);
		GET_INSTANCE_PROC_ADDR(instance, GetPhysicalDeviceSurfaceFormatsKHR);
		GET_INSTANCE_PROC_ADDR(instance, GetPhysicalDeviceSurfacePresentModesKHR);
		GET_DEVICE_PROC_ADDR(device, CreateSwapchainKHR);
		GET_DEVICE_PROC_ADDR(device, DestroySwapchainKHR);
		GET_DEVICE_PROC_ADDR(device, GetSwapchainImagesKHR);
		GET_DEVICE_PROC_ADDR(device, AcquireNextImageKHR);
		GET_DEVICE_PROC_ADDR(device, QueuePresentKHR);
	}

	/** 
	* Create the swapchain and get it's images with given width and height
	* 
	* @param width Pointer to the width of the swapchain (may be adjusted to fit the requirements of the swapchain)
	* @param height Pointer to the height of the swapchain (may be adjusted to fit the requirements of the swapchain)
	* @param vsync (Optional) Can be used to force vsync'd rendering (by using VK_PRESENT_MODE_FIFO_KHR as presentation mode)
	*/
	void create(uint32_t *width, uint32_t *height, bool vsync = false)
	{
		VkResult err;
		VkSwapchainKHR oldSwapchain = swapChain;

		// Get physical device surface properties and formats
		VkSurfaceCapabilitiesKHR surfCaps;
		err = fpGetPhysicalDeviceSurfaceCapabilitiesKHR(physicalDevice, surface, &surfCaps);
		assert(!err);

		// Get available present modes
		uint32_t presentModeCount;
		err = fpGetPhysicalDeviceSurfacePresentModesKHR(physicalDevice, surface, &presentModeCount, NULL);
		assert(!err);
		assert(presentModeCount > 0);

		std::vector<VkPresentModeKHR> presentModes(presentModeCount);

		err = fpGetPhysicalDeviceSurfacePresentModesKHR(physicalDevice, surface, &presentModeCount, presentModes.data());
		assert(!err);

		VkExtent2D swapchainExtent = {};
		// If width (and height) equals the special value 0xFFFFFFFF, the size of the surface will be set by the swapchain
		if (surfCaps.currentExtent.width == (uint32_t)-1)
		{
			// If the surface size is undefined, the size is set to
			// the size of the images requested.
			swapchainExtent.width = *width;
			swapchainExtent.height = *height;
		}
		else
		{
			// If the surface size is defined, the swap chain size must match
			swapchainExtent = surfCaps.currentExtent;
			*width = surfCaps.currentExtent.width;
			*height = surfCaps.currentExtent.height;
		}


		// Select a present mode for the swapchain

		// The VK_PRESENT_MODE_FIFO_KHR mode must always be present as per spec
		// This mode waits for the vertical blank ("v-sync")
		VkPresentModeKHR swapchainPresentMode = VK_PRESENT_MODE_FIFO_KHR;

		// If v-sync is not requested, try to find a mailbox mode
		// It's the lowest latency non-tearing present mode available
		if (!vsync)
		{
			for (size_t i = 0; i < presentModeCount; i++)
			{
				if (presentModes[i] == VK_PRESENT_MODE_MAILBOX_KHR)
				{
					swapchainPresentMode = VK_PRESENT_MODE_MAILBOX_KHR;
					break;
				}
				if ((swapchainPresentMode != VK_PRESENT_MODE_MAILBOX_KHR) && (presentModes[i] == VK_PRESENT_MODE_IMMEDIATE_KHR))
				{
					swapchainPresentMode = VK_PRESENT_MODE_IMMEDIATE_KHR;
				}
			}
		}

		// Determine the number of images
		uint32_t desiredNumberOfSwapchainImages = surfCaps.minImageCount + 1;
		if ((surfCaps.maxImageCount > 0) && (desiredNumberOfSwapchainImages > surfCaps.maxImageCount))
		{
			desiredNumberOfSwapchainImages = surfCaps.maxImageCount;
		}

		// Find the transformation of the surface
		VkSurfaceTransformFlagsKHR preTransform;
		if (surfCaps.supportedTransforms & VK_SURFACE_TRANSFORM_IDENTITY_BIT_KHR)
		{
			// We prefer a non-rotated transform
			preTransform = VK_SURFACE_TRANSFORM_IDENTITY_BIT_KHR;
		}
		else
		{
			preTransform = surfCaps.currentTransform;
		}

		// Find a supported composite alpha format (not all devices support alpha opaque)
		VkCompositeAlphaFlagBitsKHR compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
		// Simply select the first composite alpha format available
		std::vector<VkCompositeAlphaFlagBitsKHR> compositeAlphaFlags = {
			VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR,
			VK_COMPOSITE_ALPHA_PRE_MULTIPLIED_BIT_KHR,
			VK_COMPOSITE_ALPHA_POST_MULTIPLIED_BIT_KHR,
			VK_COMPOSITE_ALPHA_INHERIT_BIT_KHR,
		};
		for (auto& compositeAlphaFlag : compositeAlphaFlags) {
			if (surfCaps.supportedCompositeAlpha & compositeAlphaFlag) {
				compositeAlpha = compositeAlphaFlag;
				break;
			};
		}

		VkSwapchainCreateInfoKHR swapchainCI = {};
		swapchainCI.sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR;
		swapchainCI.pNext = NULL;
		swapchainCI.surface = surface;
		swapchainCI.minImageCount = desiredNumberOfSwapchainImages;
		swapchainCI.imageFormat = colorFormat;
		swapchainCI.imageColorSpace = colorSpace;
		swapchainCI.imageExtent = { swapchainExtent.width, swapchainExtent.height };
		swapchainCI.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
		swapchainCI.preTransform = (VkSurfaceTransformFlagBitsKHR)preTransform;
		swapchainCI.imageArrayLayers = 1;
		swapchainCI.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
		swapchainCI.queueFamilyIndexCount = 0;
		swapchainCI.pQueueFamilyIndices = NULL;
		swapchainCI.presentMode = swapchainPresentMode;
		swapchainCI.oldSwapchain = oldSwapchain;
		// Setting clipped to VK_TRUE allows the implementation to discard rendering outside of the surface area
		swapchainCI.clipped = VK_TRUE;
		swapchainCI.compositeAlpha = compositeAlpha;

		// Set additional usage flag for blitting from the swapchain images if supported
		VkFormatProperties formatProps;
		vkGetPhysicalDeviceFormatProperties(physicalDevice, colorFormat, &formatProps);
		if (formatProps.optimalTilingFeatures & VK_FORMAT_FEATURE_BLIT_DST_BIT) {
			swapchainCI.imageUsage |= VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
		}

		err = fpCreateSwapchainKHR(device, &swapchainCI, nullptr, &swapChain);
		assert(!err);

		// If an existing swap chain is re-created, destroy the old swap chain
		// This also cleans up all the presentable images
		if (oldSwapchain != VK_NULL_HANDLE) 
		{ 
			for (uint32_t i = 0; i < imageCount; i++)
			{
				vkDestroyImageView(device, buffers[i].view, nullptr);
			}
			fpDestroySwapchainKHR(device, oldSwapchain, nullptr);
		}

		err = fpGetSwapchainImagesKHR(device, swapChain, &imageCount, NULL);
		assert(!err);

		// Get the swap chain images
		images.resize(imageCount);
		err = fpGetSwapchainImagesKHR(device, swapChain, &imageCount, images.data());
		assert(!err);

		// Get the swap chain buffers containing the image and imageview
		buffers.resize(imageCount);
		for (uint32_t i = 0; i < imageCount; i++)
		{
			VkImageViewCreateInfo colorAttachmentView = {};
			colorAttachmentView.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
			colorAttachmentView.pNext = NULL;
			colorAttachmentView.format = colorFormat;
			colorAttachmentView.components = {
				VK_COMPONENT_SWIZZLE_R,
				VK_COMPONENT_SWIZZLE_G,
				VK_COMPONENT_SWIZZLE_B,
				VK_COMPONENT_SWIZZLE_A
			};
			colorAttachmentView.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
			colorAttachmentView.subresourceRange.baseMipLevel = 0;
			colorAttachmentView.subresourceRange.levelCount = 1;
			colorAttachmentView.subresourceRange.baseArrayLayer = 0;
			colorAttachmentView.subresourceRange.layerCount = 1;
			colorAttachmentView.viewType = VK_IMAGE_VIEW_TYPE_2D;
			colorAttachmentView.flags = 0;

			buffers[i].image = images[i];

			colorAttachmentView.image = buffers[i].image;

			err = vkCreateImageView(device, &colorAttachmentView, nullptr, &buffers[i].view);
			assert(!err);
		}
	}

	/** 
	* Acquires the next image in the swap chain
	*
	* @param presentCompleteSemaphore (Optional) Semaphore that is signaled when the image is ready for use
	* @param imageIndex Pointer to the image index that will be increased if the next image could be acquired
	*
	* @note The function will always wait until the next image has been acquired by setting timeout to UINT64_MAX
	*
	* @return VkResult of the image acquisition
	*/
	VkResult acquireNextImage(VkSemaphore presentCompleteSemaphore, uint32_t *imageIndex)
	{
		// By setting timeout to UINT64_MAX we will always wait until the next image has been acquired or an actual error is thrown
		// With that we don't have to handle VK_NOT_READY
		return fpAcquireNextImageKHR(device, swapChain, UINT64_MAX, presentCompleteSemaphore, (VkFence)nullptr, imageIndex);
	}

	/**
	* Queue an image for presentation
	*
	* @param queue Presentation queue for presenting the image
	* @param imageIndex Index of the swapchain image to queue for presentation
	* @param waitSemaphore (Optional) Semaphore that is waited on before the image is presented (only used if != VK_NULL_HANDLE)
	*
	* @return VkResult of the queue presentation
	*/
	VkResult queuePresent(VkQueue queue, uint32_t imageIndex, VkSemaphore waitSemaphore = VK_NULL_HANDLE)
	{
		VkPresentInfoKHR presentInfo = {};
		presentInfo.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
		presentInfo.pNext = NULL;
		presentInfo.swapchainCount = 1;
		presentInfo.pSwapchains = &swapChain;
		presentInfo.pImageIndices = &imageIndex;
		// Check if a wait semaphore has been specified to wait for before presenting the image
		if (waitSemaphore != VK_NULL_HANDLE)
		{
			presentInfo.pWaitSemaphores = &waitSemaphore;
			presentInfo.waitSemaphoreCount = 1;
		}
		return fpQueuePresentKHR(queue, &presentInfo);
	}


	/**
	* Destroy and free Vulkan resources used for the swapchain
	*/
	void cleanup()
	{
		if (swapChain != VK_NULL_HANDLE)
		{
			for (uint32_t i = 0; i < imageCount; i++)
			{
				vkDestroyImageView(device, buffers[i].view, nullptr);
			}
		}
		if (surface != VK_NULL_HANDLE)
		{
			fpDestroySwapchainKHR(device, swapChain, nullptr);
			vkDestroySurfaceKHR(instance, surface, nullptr);
		}
		surface = VK_NULL_HANDLE;
		swapChain = VK_NULL_HANDLE;
	}

#if defined(_DIRECT2DISPLAY)

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
			printf("Failed: %s. Vulkan has raised an error: %s", msg, vkErrorToStr(result));
			return false;
		}
		return true;
	}


	/**
	* Create direct to display surface
	*/	
	void createDirect2DisplaySurface(uint32_t width, uint32_t height)
	{

		VkDisplayPropertiesKHR properties;
		uint32_t propertiesCount = 1;
		if (vkGetPhysicalDeviceDisplayPropertiesKHR)
		{
			vkGetPhysicalDeviceDisplayPropertiesKHR(physicalDevice, &propertiesCount, &properties);
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
		vkGetDisplayModePropertiesKHR(physicalDevice, properties.display, &modeCount, NULL);
		std::vector<VkDisplayModePropertiesKHR> modeProperties; modeProperties.resize(modeCount);
		vkGetDisplayModePropertiesKHR(physicalDevice, properties.display, &modeCount, modeProperties.data());

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

		if (!vkIsSuccessful(vkCreateDisplayPlaneSurfaceKHR(instance, &surfaceCreateInfo,
			NULL, &surface), "Could not create DisplayPlane Surface\n"))
		{
			return;
		}

	//	res = VK_SUCCESS;

		/*

		uint32_t displayPropertyCount;
		
		printf("\n swapchain 22222222222222222\n");

		if (vkGetPhysicalDeviceDisplayPropertiesKHR == NULL)
			printf("EEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEE");

		// Get display property
		vkGetPhysicalDeviceDisplayPropertiesKHR(physicalDevice, &displayPropertyCount, NULL);
		printf("\n swapchain 22222222222222222 aaaaaaaaa\n");
		VkDisplayPropertiesKHR* pDisplayProperties = new VkDisplayPropertiesKHR[displayPropertyCount];
		printf("\n swapchain 22222222222222222 bbbbbbbbbbbbbb\n");
		vkGetPhysicalDeviceDisplayPropertiesKHR(physicalDevice, &displayPropertyCount, pDisplayProperties);

		printf("\n swapchain 333333333333333333333\n");
		// Get plane property
		uint32_t planePropertyCount;
		printf("\n\nswapchain !!!!!!!!!!!!!!!!!!!%d!!!!!!!!!!!!!!!!\n\n", planePropertyCount);
		vkGetPhysicalDeviceDisplayPlanePropertiesKHR(physicalDevice, &planePropertyCount, NULL);
		VkDisplayPlanePropertiesKHR* pPlaneProperties = new VkDisplayPlanePropertiesKHR[planePropertyCount];
		vkGetPhysicalDeviceDisplayPlanePropertiesKHR(physicalDevice, &planePropertyCount, pPlaneProperties);

		printf("\n swapchain 44444444444444444444444444\n");
		VkDisplayKHR display = VK_NULL_HANDLE;
		VkDisplayModeKHR displayMode;
		VkDisplayModePropertiesKHR* pModeProperties;
		bool foundMode = false;

		printf("\n swapchain 555555555555555555555555555\n");
		for(uint32_t i = 0; i < displayPropertyCount;++i)
	   	{
			printf("\n swapchain 666666666666666666666\n");
			display = pDisplayProperties[i].display;
			uint32_t modeCount;
			vkGetDisplayModePropertiesKHR(physicalDevice, display, &modeCount, NULL);
			pModeProperties = new VkDisplayModePropertiesKHR[modeCount];
			vkGetDisplayModePropertiesKHR(physicalDevice, display, &modeCount, pModeProperties);

			for (uint32_t j = 0; j < modeCount; ++j)
			{
				printf("\n swapchain 77777777777777777\n");
				const VkDisplayModePropertiesKHR* mode = &pModeProperties[j];

				if (mode->parameters.visibleRegion.width == width && mode->parameters.visibleRegion.height == height)
				{
					displayMode = mode->displayMode;
					foundMode = true;
					break;
				}
			}
			if (foundMode)
			{
				break;
			}
			delete [] pModeProperties;
		}
		printf("\n swapchain 22266669999999\n");

		if(!foundMode)
		{
			vks::tools::exitFatal("Can't find a display and a display mode!", "Fatal error");
			return;
		}

		// Search for a best plane we can use
		uint32_t bestPlaneIndex = UINT32_MAX;
		VkDisplayKHR* pDisplays = NULL;
		for(uint32_t i = 0; i < planePropertyCount; i++)
		{
			uint32_t planeIndex=i;
			uint32_t displayCount;
			vkGetDisplayPlaneSupportedDisplaysKHR(physicalDevice, planeIndex, &displayCount, NULL);
			if (pDisplays)
			{
				delete [] pDisplays;
			}
			pDisplays = new VkDisplayKHR[displayCount];
			vkGetDisplayPlaneSupportedDisplaysKHR(physicalDevice, planeIndex, &displayCount, pDisplays);

			// Find a display that matches the current plane
			bestPlaneIndex = UINT32_MAX;
			for(uint32_t j = 0; j < displayCount; j++)
			{
				if(display == pDisplays[j])
				{
					bestPlaneIndex = i;
					break;
				}
			}
			if(bestPlaneIndex != UINT32_MAX)
			{
				break;
			}
		}

		if(bestPlaneIndex == UINT32_MAX)
		{
			vks::tools::exitFatal("Can't find a plane for displaying!", "Fatal error");
			return;
		}

		VkDisplayPlaneCapabilitiesKHR planeCap;
		vkGetDisplayPlaneCapabilitiesKHR(physicalDevice, displayMode, bestPlaneIndex, &planeCap);
		VkDisplayPlaneAlphaFlagBitsKHR alphaMode;

		if (planeCap.supportedAlpha & VK_DISPLAY_PLANE_ALPHA_PER_PIXEL_PREMULTIPLIED_BIT_KHR)
		{
			alphaMode = VK_DISPLAY_PLANE_ALPHA_PER_PIXEL_PREMULTIPLIED_BIT_KHR;
		}
		else if (planeCap.supportedAlpha & VK_DISPLAY_PLANE_ALPHA_PER_PIXEL_BIT_KHR)
		{

			alphaMode = VK_DISPLAY_PLANE_ALPHA_PER_PIXEL_BIT_KHR;
		}
		else
		{
			alphaMode = VK_DISPLAY_PLANE_ALPHA_GLOBAL_BIT_KHR;
		}

		VkDisplaySurfaceCreateInfoKHR surfaceInfo{};
		surfaceInfo.sType = VK_STRUCTURE_TYPE_DISPLAY_SURFACE_CREATE_INFO_KHR;
		surfaceInfo.pNext = NULL;
		surfaceInfo.flags = 0;
		surfaceInfo.displayMode = displayMode;
		surfaceInfo.planeIndex = bestPlaneIndex;
		surfaceInfo.planeStackIndex = pPlaneProperties[bestPlaneIndex].currentStackIndex;
		surfaceInfo.transform = VK_SURFACE_TRANSFORM_IDENTITY_BIT_KHR;
		surfaceInfo.globalAlpha = 1.0;
		surfaceInfo.alphaMode = alphaMode;
		surfaceInfo.imageExtent.width = width;
		surfaceInfo.imageExtent.height = height;

		VkResult result = vkCreateDisplayPlaneSurfaceKHR(instance, &surfaceInfo, NULL, &surface);
		if(result !=VK_SUCCESS)
		{
			vks::tools::exitFatal("Failed to create surface!", "Fatal error");
		}

		delete[] pDisplays;
		delete[] pModeProperties;
		delete[] pDisplayProperties;
		delete[] pPlaneProperties;

		*/
	}
#endif 
};
