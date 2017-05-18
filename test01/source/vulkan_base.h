#pragma once
#include <iostream>
#include <vulkan/vulkan.h>

using namespace std;

class VulkanBase
{
	VkInstance instance;

	void InitInstance();
	void DeInitInstance();
public:
	VulkanBase();
	virtual ~VulkanBase();
};

