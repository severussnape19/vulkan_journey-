#include <cstdint>
#include <fstream>
#include <ios>
#include <iostream>
#include <cstring>
#include <stdexcept>
#include <array>
#include <vector>
#include <cstdlib>
#include <cstddef>
#include <optional>

#include <vulkan/vulkan.h>
#include <vulkan/vulkan_core.h>
#define STB_IMAGE_WRITE_IMPLEMENTATION
#include <stb_image_write.h>

using i32 = int;
using f32 = float;
using f64 = double;
using u32 = uint32_t;
using usize = std::size_t;

std::array<char const*, 1> layers     { "VK_LAYER_KHRONOS_validation" };
std::array<char const*, 1> extensions { VK_EXT_DEBUG_UTILS_EXTENSION_NAME };

#define LOG(msg) std::clog << "[LOG] " << msg << '\n';
#define ERR(msg) \
    std::cerr << "[ERROR] at " << __FILE__ << ':' << __LINE__ << '\n'; \
    throw std::runtime_error(msg);

VkExtent3D extent {
    .width = 1920,
    .height = 1080,
    .depth = 1,
};

struct PushConstants {
    u32 width;
    u32 height;
};

static VKAPI_ATTR VkBool32 VKAPI_CALL debugCallback(
    VkDebugUtilsMessageSeverityFlagBitsEXT severity,
    VkDebugUtilsMessageTypeFlagsEXT type,
    VkDebugUtilsMessengerCallbackDataEXT const* pCallbackData,
    void* pUserData
)
{
    std::cerr << pCallbackData->pMessage << '\n';
    return VK_FALSE;
}

class HeadLessRenderer {
public:
    auto run() -> void
    {
        initVulkan();
        mainLoop();
        cleanUp();
    }
private:
    auto initVulkan() -> void
    {
        createInstance();
        setupDebugMessenger();
        pickPhysicalDevice();
        createLogicalDevice();
        createStorageimage();
        createImageView();

        createDescriptorSetLayout();
        createDescriptorPool();
        createDescriptorSet();
        createPipelineLayout();
        createShaderModule();
        createComputePipeline();
        createCommandPool();
        createCommandBuffer();
        createFence();
        createReadbackBuffer();
        render();
    }

    auto mainLoop() -> void {

        saveImage();
    }

    auto cleanUp() -> void
    {

        vkDestroyBuffer(logicalDevice, readBackBuffer, nullptr);
        vkFreeMemory(logicalDevice, readbackMemory, nullptr);
        
        vkDestroyFence(logicalDevice, fence, nullptr);
        
        vkFreeCommandBuffers(logicalDevice, commandPool, 1, &commandBuffer);
        vkDestroyCommandPool(logicalDevice, commandPool, nullptr);
        
        vkDestroyPipeline(logicalDevice, computePipeline, nullptr);
        vkDestroyShaderModule(logicalDevice, shaderModule, nullptr);
        vkDestroyPipelineLayout(logicalDevice, pipelineLayout, nullptr);
        
        //vkFreeDescriptorSets(logicalDevice, descriptorPool, 1, &descriptorSet);
        vkDestroyDescriptorPool(logicalDevice, descriptorPool, nullptr);
        vkDestroyDescriptorSetLayout(logicalDevice, descriptorSetLayout, nullptr);        

        vkDestroyImageView(logicalDevice, storageImageView, nullptr);
        vkDestroyImage(logicalDevice, storageImage, nullptr);
        vkFreeMemory(logicalDevice, storageImageMemory, nullptr);

        auto pfnDestroyDebugMessenger = (PFN_vkDestroyDebugUtilsMessengerEXT)vkGetInstanceProcAddr(instance, "vkDestroyDebugUtilsMessengerEXT");
        if (!pfnDestroyDebugMessenger) {
            ERR("COULD NOT RETRIVE POINTER TO FUNCTION - pfnDestroyDebugUtilsMessenger");  
        }
        pfnDestroyDebugMessenger(instance, debugMessenger, nullptr);
        
        vkDestroyDevice(logicalDevice, nullptr);
        vkDestroyInstance(instance, nullptr);
    }

    auto createInstance() -> void
    {

        VkApplicationInfo appInfo
        {
            .sType = VK_STRUCTURE_TYPE_APPLICATION_INFO,
            .pNext = nullptr,
            .pApplicationName = "Headless Renderer",
            .applicationVersion = VK_MAKE_VERSION(0, 1, 0),
            .pEngineName = "HeadlessEngine",
            .engineVersion = VK_MAKE_VERSION(0, 1, 0),
            .apiVersion = VK_API_VERSION_1_3,
        };

        VkInstanceCreateInfo createInfo
        {
            .sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO,
            .pNext = nullptr,
            .flags = 0,
            .pApplicationInfo = &appInfo,
            .enabledLayerCount = layers.size(),
            .ppEnabledLayerNames = layers.data(),
            .enabledExtensionCount = extensions.size(),
            .ppEnabledExtensionNames = extensions.data(),
        };

        if (vkCreateInstance(&createInfo, nullptr, &instance) != VK_SUCCESS)
        {
            ERR("Failed to create vulkan instance!");
        }
        LOG("Instance created successfully!");
    }

    auto pickPhysicalDevice() -> void
    {
        u32 deviceCount{};
        if (vkEnumeratePhysicalDevices(instance, &deviceCount, nullptr) != VK_SUCCESS)
        {
            ERR("Failed to enumerate devices!");
        }

        std::vector<VkPhysicalDevice> devices(deviceCount);
        if (vkEnumeratePhysicalDevices(instance, &deviceCount, devices.data()) != VK_SUCCESS)
        {
            ERR("Failed to enumerate devices!");
        }

        for (auto const& device : devices)
        {
            VkPhysicalDeviceProperties deviceProperties{};
            vkGetPhysicalDeviceProperties(device, &deviceProperties);
            if (deviceProperties.deviceType == VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU || deviceProperties.deviceType == VK_PHYSICAL_DEVICE_TYPE_INTEGRATED_GPU)
            {
                auto const index = findQueueFamilies(device);
                if (index.has_value()) {
                    physicalDevice = device;
                    queueFamilyIndex = index.value();
                    std::clog << "Selected Device: " << deviceProperties.deviceName << '\n';
                    return;
                }
            }
        }

        throw std::runtime_error("Could not find a suitable GPU!");
    }

    auto findQueueFamilies(VkPhysicalDevice device) -> std::optional<u32>
    {
        u32 queueCount{};
        vkGetPhysicalDeviceQueueFamilyProperties(device, &queueCount, nullptr);

        std::vector<VkQueueFamilyProperties> queueFamilyProperties(queueCount);
        vkGetPhysicalDeviceQueueFamilyProperties(device, &queueCount, queueFamilyProperties.data());

        for (usize i{}; i < queueCount; ++i) {
            if (queueFamilyProperties[i].queueFlags & (VK_QUEUE_GRAPHICS_BIT | VK_QUEUE_COMPUTE_BIT)) { return i; }
        }
        return std::nullopt;
    }


    auto createLogicalDevice() -> void
    {
        VkPhysicalDeviceSynchronization2Features sync2features {
            .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SYNCHRONIZATION_2_FEATURES,
            .pNext = nullptr,
            .synchronization2 = VK_TRUE,
        };

        std::array<f32, 1> priority = { 1.f };
        VkDeviceQueueCreateInfo queueCreateInfo {
            .sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO,
            .pNext = nullptr,
            .flags = 0,
            .queueFamilyIndex = queueFamilyIndex,
            .queueCount = 1, // Since I need just the graphics queue
            .pQueuePriorities = priority.data(),
        };

        VkDeviceCreateInfo createInfo {
            .sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO,
            .pNext = &sync2features,
            .flags = 0,
            .queueCreateInfoCount = 1,
            .pQueueCreateInfos = &queueCreateInfo,
            .enabledExtensionCount = 0,
            .ppEnabledExtensionNames = nullptr, 
        };

        if (vkCreateDevice(physicalDevice, &createInfo, nullptr, &logicalDevice) != VK_SUCCESS) {
            ERR("Could not create a logical device!");       
        }

        vkGetDeviceQueue(logicalDevice, queueFamilyIndex, 0, &graphicsQueue);

        LOG("Created Logical device successfully!");
    }

    auto setupDebugMessenger() -> void
    {
        VkDebugUtilsMessageSeverityFlagsEXT messageSeverityFlags = 
            VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT |
            //VK_DEBUG_UTILS_MESSAGE_SEVERITY_VERBOSE_BIT_EXT |
            VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT;

        VkDebugUtilsMessageTypeFlagsEXT messageTypeFlags =
            VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT |
            VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT |
            VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT;
        
        auto pfnDebugUtilsMessenger = (PFN_vkCreateDebugUtilsMessengerEXT)vkGetInstanceProcAddr(instance, "vkCreateDebugUtilsMessengerEXT");
        if (!pfnDebugUtilsMessenger) {
            ERR("COULD NOT RETRIVE POINTER TO FUNCTION - pfnDebugUtilsMessenger");  
        }
        VkDebugUtilsMessengerCreateInfoEXT createInfo {
            .sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT,
            .pNext = nullptr,
            .flags = 0,
            .messageSeverity = messageSeverityFlags,
            .messageType = messageTypeFlags,
            .pfnUserCallback = debugCallback,
            .pUserData = nullptr,
        };

        if (pfnDebugUtilsMessenger(instance, &createInfo, nullptr, &debugMessenger) != VK_SUCCESS) {
            ERR("Could not create user callback!");
        }
        LOG("CREATED DEBUG CALLBACK!")
    }

    auto findMemoryTypes(u32 typeFilter, VkMemoryPropertyFlags properties) -> u32
    {
        VkPhysicalDeviceMemoryProperties physicalDeviceMemProperties{};
        vkGetPhysicalDeviceMemoryProperties(physicalDevice, &physicalDeviceMemProperties);
        for (u32 i{}; i < physicalDeviceMemProperties.memoryTypeCount; ++i) {
            bool typeAcceptable = typeFilter & (1u << i);
            bool propsMatch = (physicalDeviceMemProperties.memoryTypes[i].propertyFlags & properties) == properties;
            if (typeAcceptable && propsMatch) {
                return i;    
            }
        }
        ERR("Missing Memory type!!");
    }

    auto createStorageimage() -> void
    {
        VkImageCreateInfo createInfo {
            .sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
            .pNext = nullptr,
            .flags = 0,
            .imageType = VK_IMAGE_TYPE_2D,
            .format = VK_FORMAT_R8G8B8A8_UNORM,
            .extent = extent,
            .mipLevels = 1,
            .arrayLayers = 1,
            .samples = VK_SAMPLE_COUNT_1_BIT,
            .tiling = VK_IMAGE_TILING_OPTIMAL,
            .usage = VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT,
            .initialLayout = VK_IMAGE_LAYOUT_UNDEFINED,
        };

        if (vkCreateImage(logicalDevice, &createInfo, nullptr, &storageImage) != VK_SUCCESS) {
            ERR("Could not create image storage!");            
        }

        VkMemoryRequirements memRequirement{};
        vkGetImageMemoryRequirements(logicalDevice, storageImage, &memRequirement);
        auto memIdx = findMemoryTypes(memRequirement.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);

        VkMemoryAllocateInfo allocateInfo {
            .sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
            .pNext = nullptr,
            .allocationSize = memRequirement.size,
            .memoryTypeIndex = memIdx,
        };

        if (vkAllocateMemory(logicalDevice, &allocateInfo, nullptr, &storageImageMemory) != VK_SUCCESS) {
            ERR("Could not allocate memory for resource!");
        }

        if (vkBindImageMemory(logicalDevice, storageImage, storageImageMemory, 0) != VK_SUCCESS) {
            ERR("Could not bind image memory!");   
        }

        LOG("Storage for image created successfully!");
    }

    auto createImageView() -> void
    {
        VkImageViewCreateInfo createInfo {
            .sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
            .pNext = nullptr,
            .flags = 0,
            .image = storageImage,
            .viewType = VK_IMAGE_VIEW_TYPE_2D,
            .format = VK_FORMAT_R8G8B8A8_UNORM,
            .components {
                .r = VK_COMPONENT_SWIZZLE_R,
                .g = VK_COMPONENT_SWIZZLE_G,
                .b = VK_COMPONENT_SWIZZLE_B,
                .a = VK_COMPONENT_SWIZZLE_A,
            },
            // Which part of the image does the view cover?
            .subresourceRange {
                .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
                .baseMipLevel = 0,
                .levelCount = 1,
                .baseArrayLayer = 0,
                .layerCount = 1,
            },
        };

        if (vkCreateImageView(logicalDevice, &createInfo, nullptr, &storageImageView) != VK_SUCCESS) {
            ERR("Could not create image view!");
        }

        LOG("Image view created successully!");
    }

    auto createDescriptorSetLayout() -> void
    {
        VkDescriptorSetLayoutBinding layoutBinding {
            .binding = 0,
            .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,
            .descriptorCount = 1,
            .stageFlags = VK_SHADER_STAGE_COMPUTE_BIT,
        };

        VkDescriptorSetLayoutCreateInfo createInfo {
            .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,
            .pNext = nullptr,
            .flags = 0,
            .bindingCount = 1,
            .pBindings = &layoutBinding
        };

        if (vkCreateDescriptorSetLayout(logicalDevice, &createInfo, nullptr, &descriptorSetLayout) != VK_SUCCESS) {
            ERR("Could not create a descriptor set layout!");
        }

        LOG("Successfully created a descriptor set layout!");
    }

    auto createDescriptorPool() -> void
    {
        VkDescriptorPoolSize poolSize {
            .type = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,
            .descriptorCount = 1,
        };

        VkDescriptorPoolCreateInfo createInfo {
            .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO,
            .pNext = nullptr,
            .flags = 0,
            .maxSets = 1,
            .poolSizeCount = 1,
            .pPoolSizes = &poolSize, 
        };
        
        if (vkCreateDescriptorPool(logicalDevice, &createInfo, nullptr, &descriptorPool) != VK_SUCCESS) {
            ERR("Failed to create a descriptor pool!");
        }
        
        LOG("Descriptor pool successfully created!");
    }

    auto createDescriptorSet() -> void
    {
        VkDescriptorSetAllocateInfo allocInfo {
            .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,
            .pNext = nullptr,
            .descriptorPool = descriptorPool,
            .descriptorSetCount = 1,
            .pSetLayouts = &descriptorSetLayout
        };
        
        if (vkAllocateDescriptorSets(logicalDevice, &allocInfo, &descriptorSet) != VK_SUCCESS) {
            ERR("Failed to allocate descriptor sets!");
        }
        
        VkDescriptorImageInfo imageInfo {
            .sampler = VK_NULL_HANDLE,
            .imageView = storageImageView,
            .imageLayout = VK_IMAGE_LAYOUT_GENERAL,
        };

        VkWriteDescriptorSet write {
            .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
            .pNext = nullptr,
            .dstSet = descriptorSet,
            .dstBinding = 0,
            .dstArrayElement = 0,
            .descriptorCount = 1,
            .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,
            .pImageInfo = &imageInfo,
        };

        vkUpdateDescriptorSets(logicalDevice, 1, &write, 0, nullptr);
        
        LOG("Updated descriptor sets!");
    }

    auto createPipelineLayout() -> void {

        VkPushConstantRange pushConstantRange {
            .stageFlags = VK_SHADER_STAGE_COMPUTE_BIT,
            .offset = 0,
            .size = sizeof(PushConstants),
        };

        VkPipelineLayoutCreateInfo createInfo {
            .sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
            .pNext = nullptr,
            .flags = 0,
            .setLayoutCount = 1,
            .pSetLayouts = &descriptorSetLayout,
            .pushConstantRangeCount = 1,
            .pPushConstantRanges = &pushConstantRange, // Can be used as constants in a shader. Requires no synchronization
        };

        if (vkCreatePipelineLayout(logicalDevice, &createInfo, nullptr, &pipelineLayout) != VK_SUCCESS) {
            ERR("Could not create a pipeline layout");
        }

        LOG("Created pipeline layout!");
    }

    auto loadShader(std::string const& path) -> std::vector<char> {

        std::ifstream file(path, std::ios::ate | std::ios::binary); // open in binary and get to the end of the file.
        if (!file.is_open()) {
            ERR("Could not open file!" + path);
        }
        usize filesize = file.tellg();
        std::vector<char> buffer(filesize);
        file.seekg(0);
        file.read(buffer.data(), filesize);

        return buffer;
    }

    auto createShaderModule() -> void 
    {
        auto buffer = loadShader("./ray.spv");
        VkShaderModuleCreateInfo createInfo {
            .sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO,
            .flags = 0,
            .codeSize = buffer.size(),
            .pCode = reinterpret_cast<const uint32_t*>(buffer.data()),
        };

        if (vkCreateShaderModule(logicalDevice, &createInfo, nullptr, &shaderModule) != VK_SUCCESS) {
            ERR("Could not create a shader module!");
        }

        LOG("Created shader module!");
    }

    auto createComputePipeline() -> void
    {
        VkPipelineShaderStageCreateInfo stageCreateInfo {
            .sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
            .pNext = nullptr,
            .flags = 0,
            .stage = VK_SHADER_STAGE_COMPUTE_BIT,
            .module = shaderModule,
            .pName = "main", 
        };

        VkComputePipelineCreateInfo createInfo {
            .sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO,
            .pNext = nullptr,
            .flags = 0,
            .stage = stageCreateInfo,
            .layout = pipelineLayout,
            .basePipelineHandle = VK_NULL_HANDLE,
            .basePipelineIndex = -1,
        };

        if (vkCreateComputePipelines(logicalDevice, VK_NULL_HANDLE, 1, &createInfo, nullptr, &computePipeline) != VK_SUCCESS) {
            ERR("Could not create compute pipeline!");
        }

        LOG("Created compute pipeline!");
    }

    auto createCommandPool() -> void {

        VkCommandPoolCreateInfo createInfo {
            .sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO,
            .pNext = nullptr,
            .flags = 0,
            .queueFamilyIndex = queueFamilyIndex,
        };

        if (vkCreateCommandPool(logicalDevice, &createInfo, nullptr, &commandPool) != VK_SUCCESS) {
            ERR("Could not create a command pool!");
        }

        LOG("Command pool created successfully!");
    }

    auto createCommandBuffer() -> void {
        
        VkCommandBufferAllocateInfo allocInfo {
            .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
            .pNext = nullptr,
            .commandPool = commandPool,
            .level = VK_COMMAND_BUFFER_LEVEL_PRIMARY,
            .commandBufferCount = 1,
        };

        if (vkAllocateCommandBuffers(logicalDevice,  &allocInfo,  &commandBuffer) != VK_SUCCESS) {
            ERR("Could not allocate a command buffer!");
        }

        LOG("Command buffer created successfully!");
    }

    auto createFence() -> void {

        VkFenceCreateInfo createInfo {
            .sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO,
            .pNext = nullptr,
            .flags = 0,
        };

        if (vkCreateFence(logicalDevice, &createInfo, nullptr, &fence) != VK_SUCCESS) {
            ERR("Could not create fence!");
        }

        LOG("Fence created successfully!");
    }

    auto transitionImageLayout(VkImageLayout oldLayout, VkImageLayout newLayout,
        VkPipelineStageFlags2 srcStage, VkPipelineStageFlags2 dstStage,
        VkAccessFlags2 srcAccessMask, VkAccessFlags2 dstAccessMask
    ) -> void {
        
        VkImageMemoryBarrier2 barrier {
            .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2,
            .pNext = nullptr,
            .srcStageMask = srcStage,
            .srcAccessMask = srcAccessMask,
            .dstStageMask = dstStage,
            .dstAccessMask = dstAccessMask,
            .oldLayout = oldLayout,
            .newLayout = newLayout,
            .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
            .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
            .image = storageImage,
            .subresourceRange = {
                .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
                .baseMipLevel = 0,
                .levelCount = 1,
                .baseArrayLayer = 0,
                .layerCount = 1,
            },
        };

        VkDependencyInfo depInfo {
            .sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO,
            .pNext = nullptr,
            .imageMemoryBarrierCount = 1,
            .pImageMemoryBarriers = &barrier,
        };

        vkCmdPipelineBarrier2(commandBuffer, &depInfo);

        LOG("Transitioned successfully!");
    }

    auto createReadbackBuffer() -> void {

        VkBufferCreateInfo createInfo {
            .sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
            .pNext = nullptr,
            .flags = 0,
            .size = extent.width * extent.height * 4,
            .usage = VK_BUFFER_USAGE_TRANSFER_DST_BIT,
            .sharingMode = VK_SHARING_MODE_EXCLUSIVE,
            //.queueFamilyIndexCount = 1,
            //.pQueueFamilyIndices = &queueFamilyIndex,
        };

        if (vkCreateBuffer(logicalDevice, &createInfo, nullptr, &readBackBuffer) != VK_SUCCESS) {
            ERR("Could not create a readback buffer!");
        }

        LOG("Created a readback buffer!");

        VkMemoryRequirements memRequirements{};
        vkGetBufferMemoryRequirements(logicalDevice, readBackBuffer, &memRequirements);
        auto memIdx = findMemoryTypes(memRequirements.memoryTypeBits, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);

        VkMemoryAllocateInfo allocInfo {
            .sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
            .pNext = nullptr,
            .allocationSize = memRequirements.size,
            .memoryTypeIndex = memIdx,
        };

        if (vkAllocateMemory(logicalDevice, &allocInfo, nullptr, &readbackMemory) != VK_SUCCESS) {
            ERR("Could not allocate memory!");
        }

        if (vkBindBufferMemory(logicalDevice, readBackBuffer, readbackMemory, 0) != VK_SUCCESS) {
            ERR("Could not bind buffer memory!");
        }

        LOG("Readback buffer created successfully!");
    }

    auto render() -> void {

        VkCommandBufferBeginInfo beginInfo {
            .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
            .pNext = nullptr,
            .flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT,
        };

        if (vkBeginCommandBuffer(commandBuffer, &beginInfo) != VK_SUCCESS) {
            ERR("Could not begin command buffer!");
        }

        LOG("Command buffer began!");

        transitionImageLayout(
            VK_IMAGE_LAYOUT_UNDEFINED, 
            VK_IMAGE_LAYOUT_GENERAL, 
            VK_PIPELINE_STAGE_2_TOP_OF_PIPE_BIT, 
            VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
            VK_ACCESS_2_NONE, 
            VK_ACCESS_2_SHADER_WRITE_BIT
        );

        PushConstants pVals { .width = extent.width, .height = extent.height };
        vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, computePipeline);
        vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, pipelineLayout,  0, 1, &descriptorSet, 0, nullptr);
        vkCmdPushConstants(commandBuffer, pipelineLayout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(PushConstants), &pVals);
        
        u32 groupsX = (pVals.width + 15) / 16;
        u32 groupsY = (pVals.height + 15) / 16;

        vkCmdDispatch(commandBuffer, groupsX, groupsY, 1);
        
        transitionImageLayout(
            VK_IMAGE_LAYOUT_GENERAL, 
            VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, 
            VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, 
            VK_PIPELINE_STAGE_2_TRANSFER_BIT,
            VK_ACCESS_2_SHADER_WRITE_BIT, 
            VK_ACCESS_2_TRANSFER_READ_BIT
        );
       
        VkBufferImageCopy region {
            .bufferOffset = 0,
            .bufferRowLength = 0,
            .bufferImageHeight = 0,
            .imageSubresource {
                .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
                .mipLevel = 0,
                .baseArrayLayer = 0,
                .layerCount = 1,
            },
            .imageOffset = { 0, 0, 0 },
            .imageExtent = { extent.width, extent.height, 1 },
        };

        vkCmdCopyImageToBuffer(commandBuffer, storageImage, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, readBackBuffer, 1, &region);

        
        if (vkEndCommandBuffer(commandBuffer) != VK_SUCCESS) {
            ERR("Could not end command buffer!");
        }

        LOG("ENDED COMMAND BUFFER!");

        VkCommandBufferSubmitInfo submitInfo {
            .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_SUBMIT_INFO,
            .pNext = nullptr,
            .commandBuffer = commandBuffer,
            .deviceMask = 0,
        };

        VkSubmitInfo2 info {
            .sType = VK_STRUCTURE_TYPE_SUBMIT_INFO_2,
            .pNext = nullptr,
            .flags = 0,
            .waitSemaphoreInfoCount = 0,
            .pWaitSemaphoreInfos = nullptr,
            .commandBufferInfoCount = 1,
            .pCommandBufferInfos = &submitInfo,
        };

        LOG("ABOUT TO SUBMIT!!!");
        if (vkQueueSubmit2(graphicsQueue, 1, &info, fence) != VK_SUCCESS) {
            ERR("Could not submit to queue!");
        }

        LOG("Submitted command buffer to queue!");

        if (vkWaitForFences(logicalDevice, 1, &fence, VK_TRUE, UINT64_MAX) != VK_SUCCESS) {
            ERR("Could not wait for fences!");
        }

        LOG("FENCE SIGNALED!");
    }

    auto saveImage() -> void {
    
        void* data;
        if (vkMapMemory(logicalDevice, readbackMemory, 0, extent.width * extent.height * 4, 0, &data) != VK_SUCCESS) {
            ERR("Could not map memory!");
        }
        LOG("Data pointer: " + std::to_string(reinterpret_cast<uintptr_t>(data)));
        LOG("Width: " + std::to_string(extent.width) + " Height: " + std::to_string(extent.height));
        stbi_write_png("output.png", extent.width, extent.height, 4, data, extent.width * 4);
        vkUnmapMemory(logicalDevice, readbackMemory);
        LOG("Image saved successfully!");
    }


    // Instance gives us access to the physical devices and the vulkan loader
    VkInstance               instance            { VK_NULL_HANDLE };
    // We enumerate physical devices through the instance
    VkPhysicalDevice         physicalDevice      { VK_NULL_HANDLE };
    // We create logical devices out of a phsyical device with selected functionality
    VkDevice                 logicalDevice       { VK_NULL_HANDLE };
    // We pull out queues from the logical device
    VkQueue                  graphicsQueue       { VK_NULL_HANDLE };
    // An extension that lets us filter out errors, logs and messages. Retrive through vkGetInstanceProcAddress
    VkDebugUtilsMessengerEXT debugMessenger      { VK_NULL_HANDLE };
    // Data similar to a buffer.
    // Buffer - a linear chunk of data that can be used for literally anything.
    // Images - structured and have type and format information. Support advanced operations for reading oand writing data from them.
    VkImage                  storageImage        { VK_NULL_HANDLE };
    // Allocate a chunk of memory for the image after checking if the MEMORY TYPE and PROPERTY are supported.
    // Fill in a struct about the image type and format
    // Check if the current setup supports the type, format and the properties required
    // Then allocate
    VkDeviceMemory           storageImageMemory  { VK_NULL_HANDLE };
    // An Image object cannot directly be used by the GPU in a shader or pipeline. 
    // We use ImageView(an object) that tells vulkan how to interpret the image data.
    // ImageView in the CPU side points at image
    VkImageView              storageImageView    { VK_NULL_HANDLE }; 

    // Shape of the discriptor set (blueprint)
    VkDescriptorSetLayout    descriptorSetLayout { VK_NULL_HANDLE };
    // Pool of descriptors. Descriptor sets are created from this pool
    VkDescriptorPool         descriptorPool      { VK_NULL_HANDLE };
    // Descriptor sets bind GPU resourcses to the shaders 
    VkDescriptorSet          descriptorSet       { VK_NULL_HANDLE };

    // Pipeline created from the descriptor sets
    VkPipelineLayout         pipelineLayout      { VK_NULL_HANDLE };
    VkShaderModule           shaderModule        { VK_NULL_HANDLE };
    // Pipeline object created with the pipeline layout and the shader.
    VkPipeline               computePipeline     { VK_NULL_HANDLE };
    VkCommandPool            commandPool         { VK_NULL_HANDLE };
    VkCommandBuffer          commandBuffer       { VK_NULL_HANDLE };
    
    VkFence                  fence               { VK_NULL_HANDLE };

    VkBuffer       readBackBuffer { VK_NULL_HANDLE };

    VkDeviceMemory readbackMemory { VK_NULL_HANDLE };
    
    u32 queueFamilyIndex{};
};

auto main() -> i32
{
    try {
        HeadLessRenderer app;
        app.run();
    } catch (std::exception const& e) {
        std::cerr << e.what() << '\n';
        return EXIT_FAILURE;
    }

    return EXIT_SUCCESS;
}