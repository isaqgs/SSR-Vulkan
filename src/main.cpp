#include <vulkan/vulkan.h>
#include <GLFW/glfw3.h>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <array>
#include <cstddef>
#include <vector>
#include <optional>
#include <set>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <cstring>
#include <algorithm>
#include <cstdlib>

static constexpr uint32_t WIDTH = 1280;
static constexpr uint32_t HEIGHT = 720;
static constexpr int MAX_FRAMES_IN_FLIGHT = 2;

struct Vertex
{
    glm::vec3 pos;
    glm::vec3 normal;
    glm::vec3 color;
};

struct CameraData
{
    glm::mat4 model;
    glm::mat4 view;
    glm::mat4 projection;
    glm::mat4 inverseProjection;
    glm::mat4 inverseView;
};

static_assert(sizeof(CameraData) == 5 * sizeof(glm::mat4));
static_assert(offsetof(CameraData, view) == sizeof(glm::mat4));
static_assert(offsetof(CameraData, projection) == 2 * sizeof(glm::mat4));
static_assert(offsetof(CameraData, inverseProjection) == 3 * sizeof(glm::mat4));
static_assert(offsetof(CameraData, inverseView) == 4 * sizeof(glm::mat4));

struct SSRData
{
    int maxSteps = 64;
    float maxDistance = 8.0f;
    float stride = 0.015f;
    float thickness = 0.15f;
    float intensity = 1.5f;
};

struct QueueFamilyIndices
{
    std::optional<uint32_t> graphics;
    std::optional<uint32_t> present;
    bool complete() const { return graphics.has_value() && present.has_value(); }
};

struct SwapchainSupport
{
    VkSurfaceCapabilitiesKHR capabilities{};
    std::vector<VkSurfaceFormatKHR> formats;
    std::vector<VkPresentModeKHR> presentModes;
};

static std::vector<char> readFile(const std::string &filename)
{
    std::ifstream file(filename, std::ios::ate | std::ios::binary);
    if (!file)
        throw std::runtime_error("Cannot open SPIR-V file: " + filename);
    size_t size = static_cast<size_t>(file.tellg());
    std::vector<char> buffer(size);
    file.seekg(0);
    file.read(buffer.data(), static_cast<std::streamsize>(size));
    return buffer;
}

class VulkanSSRApp
{
public:
    void run()
    {
        initWindow();
        initVulkan();
        mainLoop();
        cleanup();
    }

private:
    GLFWwindow *window = nullptr;
    VkInstance instance = VK_NULL_HANDLE;
    VkDebugUtilsMessengerEXT debugMessenger = VK_NULL_HANDLE;
    VkSurfaceKHR surface = VK_NULL_HANDLE;
    VkPhysicalDevice physicalDevice = VK_NULL_HANDLE;
    VkDevice device = VK_NULL_HANDLE;
    VkQueue graphicsQueue = VK_NULL_HANDLE;
    VkQueue presentQueue = VK_NULL_HANDLE;

    VkSwapchainKHR swapchain = VK_NULL_HANDLE;
    std::vector<VkImage> swapchainImages;
    std::vector<VkImageView> swapchainImageViews;
    VkFormat swapchainFormat{};
    VkExtent2D swapchainExtent{};

    VkRenderPass sceneRenderPass = VK_NULL_HANDLE;
    VkRenderPass ssrRenderPass = VK_NULL_HANDLE;
    VkRenderPass compositeRenderPass = VK_NULL_HANDLE;

    VkPipeline scenePipeline = VK_NULL_HANDLE;
    VkPipeline ssrPipeline = VK_NULL_HANDLE;
    VkPipeline compositePipeline = VK_NULL_HANDLE;
    VkPipelineLayout scenePipelineLayout = VK_NULL_HANDLE;
    VkPipelineLayout ssrPipelineLayout = VK_NULL_HANDLE;
    VkPipelineLayout compositePipelineLayout = VK_NULL_HANDLE;

    VkDescriptorSetLayout sceneSetLayout = VK_NULL_HANDLE;
    VkDescriptorSetLayout ssrSetLayout = VK_NULL_HANDLE;
    VkDescriptorSetLayout compositeSetLayout = VK_NULL_HANDLE;
    VkDescriptorPool descriptorPool = VK_NULL_HANDLE;
    VkDescriptorSet sceneSet = VK_NULL_HANDLE;
    VkDescriptorSet ssrSet = VK_NULL_HANDLE;
    VkDescriptorSet compositeSet = VK_NULL_HANDLE;

    VkCommandPool commandPool = VK_NULL_HANDLE;
    std::vector<VkCommandBuffer> commandBuffers;

    VkBuffer vertexBuffer = VK_NULL_HANDLE;
    VkDeviceMemory vertexMemory = VK_NULL_HANDLE;
    uint32_t vertexCount = 0;
    VkBuffer cameraBuffer = VK_NULL_HANDLE;
    VkDeviceMemory cameraMemory = VK_NULL_HANDLE;
    VkBuffer ssrBuffer = VK_NULL_HANDLE;
    VkDeviceMemory ssrMemory = VK_NULL_HANDLE;

    VkImage sceneColor = VK_NULL_HANDLE;
    VkDeviceMemory sceneColorMemory = VK_NULL_HANDLE;
    VkImageView sceneColorView = VK_NULL_HANDLE;
    VkImage sceneNormal = VK_NULL_HANDLE;
    VkDeviceMemory sceneNormalMemory = VK_NULL_HANDLE;
    VkImageView sceneNormalView = VK_NULL_HANDLE;
    VkImage sceneDepth = VK_NULL_HANDLE;
    VkDeviceMemory sceneDepthMemory = VK_NULL_HANDLE;
    VkImageView sceneDepthView = VK_NULL_HANDLE;
    VkImage ssrResult = VK_NULL_HANDLE;
    VkDeviceMemory ssrResultMemory = VK_NULL_HANDLE;
    VkImageView ssrResultView = VK_NULL_HANDLE;

    VkSampler sampler = VK_NULL_HANDLE;
    VkFramebuffer sceneFramebuffer = VK_NULL_HANDLE;
    VkFramebuffer ssrFramebuffer = VK_NULL_HANDLE;
    std::vector<VkFramebuffer> compositeFramebuffers;

    VkFormat depthFormat{};
    VkFormat offscreenFormat = VK_FORMAT_R16G16B16A16_SFLOAT;

    std::array<VkSemaphore, MAX_FRAMES_IN_FLIGHT> imageAvailable{};
    std::vector<VkSemaphore> renderFinished;
    std::array<VkFence, MAX_FRAMES_IN_FLIGHT> inFlight{};
    size_t currentFrame = 0;
    bool framebufferResized = false;

    const std::vector<const char *> validationLayers = {"VK_LAYER_KHRONOS_validation"};

#ifdef NDEBUG
    const bool enableValidation = false;
#else
    const bool enableValidation = true;
#endif

    void initWindow()
    {
        if (!glfwInit())
            throw std::runtime_error("GLFW initialization failed");
        glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
        glfwWindowHint(GLFW_RESIZABLE, GLFW_TRUE);
        window = glfwCreateWindow(WIDTH, HEIGHT, "SSR - Entrega 4", nullptr, nullptr);
        if (!window)
            throw std::runtime_error("Window creation failed");
        glfwSetWindowUserPointer(window, this);
        glfwSetFramebufferSizeCallback(window, [](GLFWwindow *w, int, int)
                                       { static_cast<VulkanSSRApp *>(glfwGetWindowUserPointer(w))->framebufferResized = true; });
    }

    void initVulkan()
    {
        createInstance();
        setupDebugMessenger();
        createSurface();
        pickPhysicalDevice();
        createLogicalDevice();
        createSwapchain();
        createSwapchainImageViews();
        createCommandPool();
        createOffscreenResources();
        createRenderPasses();
        createDescriptorSetLayouts();
        createPipelines();
        createFramebuffers();
        createVertexBuffer();
        createUniformBuffers();
        createSampler();
        createDescriptorPool();
        createDescriptorSets();
        createCommandBuffers();
        createSyncObjects();
    }

    bool checkValidationLayerSupport()
    {
        uint32_t count = 0;
        vkEnumerateInstanceLayerProperties(&count, nullptr);
        std::vector<VkLayerProperties> available(count);
        vkEnumerateInstanceLayerProperties(&count, available.data());
        for (const char *requested : validationLayers)
        {
            bool found = false;
            for (const auto &layer : available)
                if (strcmp(requested, layer.layerName) == 0)
                    found = true;
            if (!found)
                return false;
        }
        return true;
    }

    std::vector<const char *> requiredExtensions()
    {
        uint32_t count = 0;
        const char **glfwExt = glfwGetRequiredInstanceExtensions(&count);
        if (!glfwExt)
            throw std::runtime_error("GLFW did not provide Vulkan extensions");
        std::vector<const char *> extensions(glfwExt, glfwExt + count);
        if (enableValidation)
            extensions.push_back(VK_EXT_DEBUG_UTILS_EXTENSION_NAME);
        return extensions;
    }

    void createInstance()
    {
        if (enableValidation && !checkValidationLayerSupport())
            throw std::runtime_error("Validation layer VK_LAYER_KHRONOS_validation not available");

        VkApplicationInfo app{VK_STRUCTURE_TYPE_APPLICATION_INFO};
        app.pApplicationName = "SSR Entrega 4";
        app.applicationVersion = VK_MAKE_VERSION(1, 0, 0);
        app.pEngineName = "SSR Boilerplate";
        app.engineVersion = VK_MAKE_VERSION(1, 0, 0);
        app.apiVersion = VK_API_VERSION_1_2;

        auto extensions = requiredExtensions();
        VkInstanceCreateInfo info{VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO};
        info.pApplicationInfo = &app;
        info.enabledExtensionCount = static_cast<uint32_t>(extensions.size());
        info.ppEnabledExtensionNames = extensions.data();
        if (enableValidation)
        {
            info.enabledLayerCount = static_cast<uint32_t>(validationLayers.size());
            info.ppEnabledLayerNames = validationLayers.data();
            VkDebugUtilsMessengerCreateInfoEXT debugInfo{};
            populateDebugCreateInfo(debugInfo);
            info.pNext = &debugInfo;
        }
        if (vkCreateInstance(&info, nullptr, &instance) != VK_SUCCESS)
            throw std::runtime_error("Failed to create Vulkan instance");
    }

    static void populateDebugCreateInfo(VkDebugUtilsMessengerCreateInfoEXT &info)
    {
        info = {VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT};
        info.messageSeverity = VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT |
                               VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT;
        info.messageType = VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT |
                           VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT |
                           VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT;
        info.pfnUserCallback = debugCallback;
    }

    static VKAPI_ATTR VkBool32 VKAPI_CALL debugCallback(
        VkDebugUtilsMessageSeverityFlagBitsEXT severity,
        VkDebugUtilsMessageTypeFlagsEXT,
        const VkDebugUtilsMessengerCallbackDataEXT *data,
        void *)
    {
        if (severity >= VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT)
            std::cerr << "[Vulkan] " << data->pMessage << '\n';
        return VK_FALSE;
    }

    void setupDebugMessenger()
    {
        if (!enableValidation)
            return;
        VkDebugUtilsMessengerCreateInfoEXT info{};
        populateDebugCreateInfo(info);
        auto fn = reinterpret_cast<PFN_vkCreateDebugUtilsMessengerEXT>(vkGetInstanceProcAddr(instance, "vkCreateDebugUtilsMessengerEXT"));
        if (fn && fn(instance, &info, nullptr, &debugMessenger) != VK_SUCCESS)
            throw std::runtime_error("Failed to create debug messenger");
    }

    void createSurface()
    {
        if (glfwCreateWindowSurface(instance, window, nullptr, &surface) != VK_SUCCESS)
            throw std::runtime_error("Failed to create window surface");
    }

    QueueFamilyIndices findQueueFamilies(VkPhysicalDevice gpu)
    {
        QueueFamilyIndices indices;
        uint32_t count = 0;
        vkGetPhysicalDeviceQueueFamilyProperties(gpu, &count, nullptr);
        std::vector<VkQueueFamilyProperties> families(count);
        vkGetPhysicalDeviceQueueFamilyProperties(gpu, &count, families.data());
        for (uint32_t i = 0; i < count; ++i)
        {
            if (families[i].queueFlags & VK_QUEUE_GRAPHICS_BIT)
                indices.graphics = i;
            VkBool32 present = VK_FALSE;
            vkGetPhysicalDeviceSurfaceSupportKHR(gpu, i, surface, &present);
            if (present)
                indices.present = i;
            if (indices.complete())
                break;
        }
        return indices;
    }

    SwapchainSupport querySwapchainSupport(VkPhysicalDevice gpu)
    {
        SwapchainSupport s;
        vkGetPhysicalDeviceSurfaceCapabilitiesKHR(gpu, surface, &s.capabilities);
        uint32_t count = 0;
        vkGetPhysicalDeviceSurfaceFormatsKHR(gpu, surface, &count, nullptr);
        s.formats.resize(count);
        vkGetPhysicalDeviceSurfaceFormatsKHR(gpu, surface, &count, s.formats.data());
        vkGetPhysicalDeviceSurfacePresentModesKHR(gpu, surface, &count, nullptr);
        s.presentModes.resize(count);
        vkGetPhysicalDeviceSurfacePresentModesKHR(gpu, surface, &count, s.presentModes.data());
        return s;
    }

    bool suitable(VkPhysicalDevice gpu)
    {
        QueueFamilyIndices q = findQueueFamilies(gpu);
        auto support = querySwapchainSupport(gpu);
        uint32_t extCount = 0;
        vkEnumerateDeviceExtensionProperties(gpu, nullptr, &extCount, nullptr);
        std::vector<VkExtensionProperties> exts(extCount);
        vkEnumerateDeviceExtensionProperties(gpu, nullptr, &extCount, exts.data());
        bool swapchain = false;
        for (const auto &e : exts)
            if (strcmp(e.extensionName, VK_KHR_SWAPCHAIN_EXTENSION_NAME) == 0)
                swapchain = true;
        VkPhysicalDeviceFeatures features{};
        vkGetPhysicalDeviceFeatures(gpu, &features);
        return q.complete() && swapchain && !support.formats.empty() && !support.presentModes.empty() && features.samplerAnisotropy;
    }

    void pickPhysicalDevice()
    {
        uint32_t count = 0;
        vkEnumeratePhysicalDevices(instance, &count, nullptr);
        if (!count)
            throw std::runtime_error("No Vulkan GPU found");
        std::vector<VkPhysicalDevice> gpus(count);
        vkEnumeratePhysicalDevices(instance, &count, gpus.data());
        for (auto gpu : gpus)
            if (suitable(gpu))
            {
                physicalDevice = gpu;
                break;
            }
        if (physicalDevice == VK_NULL_HANDLE)
            throw std::runtime_error("No suitable Vulkan GPU found");
        VkPhysicalDeviceProperties props{};
        vkGetPhysicalDeviceProperties(physicalDevice, &props);
        std::cout << "GPU: " << props.deviceName << "\n";
    }

    void createLogicalDevice()
    {
        QueueFamilyIndices indices = findQueueFamilies(physicalDevice);
        std::set<uint32_t> unique = {*indices.graphics, *indices.present};
        float priority = 1.0f;
        std::vector<VkDeviceQueueCreateInfo> queues;
        for (uint32_t family : unique)
        {
            VkDeviceQueueCreateInfo q{VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO};
            q.queueFamilyIndex = family;
            q.queueCount = 1;
            q.pQueuePriorities = &priority;
            queues.push_back(q);
        }
        VkPhysicalDeviceFeatures features{};
        features.samplerAnisotropy = VK_TRUE;
        VkDeviceCreateInfo info{VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO};
        info.queueCreateInfoCount = static_cast<uint32_t>(queues.size());
        info.pQueueCreateInfos = queues.data();
        info.pEnabledFeatures = &features;
        const char *ext = VK_KHR_SWAPCHAIN_EXTENSION_NAME;
        info.enabledExtensionCount = 1;
        info.ppEnabledExtensionNames = &ext;
        if (vkCreateDevice(physicalDevice, &info, nullptr, &device) != VK_SUCCESS)
            throw std::runtime_error("Failed to create logical device");
        vkGetDeviceQueue(device, *indices.graphics, 0, &graphicsQueue);
        vkGetDeviceQueue(device, *indices.present, 0, &presentQueue);
    }

    VkSurfaceFormatKHR chooseSurfaceFormat(const std::vector<VkSurfaceFormatKHR> &formats)
    {
        for (const auto &f : formats)
            if (f.format == VK_FORMAT_B8G8R8A8_SRGB && f.colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR)
                return f;
        return formats[0];
    }

    VkPresentModeKHR choosePresentMode(const std::vector<VkPresentModeKHR> &modes)
    {
        for (auto m : modes)
            if (m == VK_PRESENT_MODE_MAILBOX_KHR)
                return m;
        return VK_PRESENT_MODE_FIFO_KHR;
    }

    VkExtent2D chooseExtent(const VkSurfaceCapabilitiesKHR &caps)
    {
        if (caps.currentExtent.width != UINT32_MAX)
            return caps.currentExtent;
        int w, h;
        glfwGetFramebufferSize(window, &w, &h);
        VkExtent2D extent{static_cast<uint32_t>(w), static_cast<uint32_t>(h)};
        extent.width = std::clamp(extent.width, caps.minImageExtent.width, caps.maxImageExtent.width);
        extent.height = std::clamp(extent.height, caps.minImageExtent.height, caps.maxImageExtent.height);
        return extent;
    }

    void createSwapchain()
    {
        auto support = querySwapchainSupport(physicalDevice);
        auto format = chooseSurfaceFormat(support.formats);
        auto present = choosePresentMode(support.presentModes);
        auto extent = chooseExtent(support.capabilities);
        uint32_t imageCount = support.capabilities.minImageCount + 1;
        if (support.capabilities.maxImageCount && imageCount > support.capabilities.maxImageCount)
            imageCount = support.capabilities.maxImageCount;

        QueueFamilyIndices indices = findQueueFamilies(physicalDevice);
        uint32_t families[] = {*indices.graphics, *indices.present};
        VkSwapchainCreateInfoKHR info{VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR};
        info.surface = surface;
        info.minImageCount = imageCount;
        info.imageFormat = format.format;
        info.imageColorSpace = format.colorSpace;
        info.imageExtent = extent;
        info.imageArrayLayers = 1;
        info.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
        if (*indices.graphics != *indices.present)
        {
            info.imageSharingMode = VK_SHARING_MODE_CONCURRENT;
            info.queueFamilyIndexCount = 2;
            info.pQueueFamilyIndices = families;
        }
        else
            info.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
        info.preTransform = support.capabilities.currentTransform;
        info.compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
        info.presentMode = present;
        info.clipped = VK_TRUE;
        if (vkCreateSwapchainKHR(device, &info, nullptr, &swapchain) != VK_SUCCESS)
            throw std::runtime_error("Failed to create swapchain");
        vkGetSwapchainImagesKHR(device, swapchain, &imageCount, nullptr);
        swapchainImages.resize(imageCount);
        vkGetSwapchainImagesKHR(device, swapchain, &imageCount, swapchainImages.data());
        // Presentation can outlive a frame fence, so its semaphore follows the acquired image.
        renderFinished.resize(imageCount);
        VkSemaphoreCreateInfo semaphoreInfo{VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO};
        for (auto &semaphore : renderFinished)
            if (vkCreateSemaphore(device, &semaphoreInfo, nullptr, &semaphore) != VK_SUCCESS)
                throw std::runtime_error("Failed to create presentation semaphore");
        swapchainFormat = format.format;
        swapchainExtent = extent;
    }

    void createSwapchainImageViews()
    {
        swapchainImageViews.resize(swapchainImages.size());
        for (size_t i = 0; i < swapchainImages.size(); ++i)
            swapchainImageViews[i] = createImageView(swapchainImages[i], swapchainFormat, VK_IMAGE_ASPECT_COLOR_BIT);
    }

    uint32_t findMemoryType(uint32_t filter, VkMemoryPropertyFlags properties)
    {
        VkPhysicalDeviceMemoryProperties mem{};
        vkGetPhysicalDeviceMemoryProperties(physicalDevice, &mem);
        for (uint32_t i = 0; i < mem.memoryTypeCount; ++i)
            if ((filter & (1u << i)) && (mem.memoryTypes[i].propertyFlags & properties) == properties)
                return i;
        throw std::runtime_error("No suitable memory type");
    }

    void createCommandPool()
    {
        QueueFamilyIndices q = findQueueFamilies(physicalDevice);
        VkCommandPoolCreateInfo info{VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO};
        info.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
        info.queueFamilyIndex = *q.graphics;
        if (vkCreateCommandPool(device, &info, nullptr, &commandPool) != VK_SUCCESS)
            throw std::runtime_error("Failed to create command pool");
    }

    VkFormat findDepthFormat()
    {
        for (VkFormat f : {VK_FORMAT_D32_SFLOAT, VK_FORMAT_D32_SFLOAT_S8_UINT, VK_FORMAT_D24_UNORM_S8_UINT})
        {
            VkFormatProperties props{};
            vkGetPhysicalDeviceFormatProperties(physicalDevice, f, &props);
            if (props.optimalTilingFeatures & VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT)
                return f;
        }
        throw std::runtime_error("No depth format available");
    }

    void createImage(uint32_t width, uint32_t height, VkFormat format, VkImageUsageFlags usage,
                     VkImage &image, VkDeviceMemory &memory)
    {
        VkImageCreateInfo info{VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO};
        info.imageType = VK_IMAGE_TYPE_2D;
        info.extent = {width, height, 1};
        info.mipLevels = 1;
        info.arrayLayers = 1;
        info.format = format;
        info.tiling = VK_IMAGE_TILING_OPTIMAL;
        info.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        info.usage = usage;
        info.samples = VK_SAMPLE_COUNT_1_BIT;
        info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
        if (vkCreateImage(device, &info, nullptr, &image) != VK_SUCCESS)
            throw std::runtime_error("Failed to create image");
        VkMemoryRequirements req{};
        vkGetImageMemoryRequirements(device, image, &req);
        VkMemoryAllocateInfo alloc{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
        alloc.allocationSize = req.size;
        alloc.memoryTypeIndex = findMemoryType(req.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
        if (vkAllocateMemory(device, &alloc, nullptr, &memory) != VK_SUCCESS)
            throw std::runtime_error("Failed to allocate image memory");
        vkBindImageMemory(device, image, memory, 0);
    }

    VkImageView createImageView(VkImage image, VkFormat format, VkImageAspectFlags aspect)
    {
        VkImageViewCreateInfo info{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
        info.image = image;
        info.viewType = VK_IMAGE_VIEW_TYPE_2D;
        info.format = format;
        info.subresourceRange.aspectMask = aspect;
        info.subresourceRange.baseMipLevel = 0;
        info.subresourceRange.levelCount = 1;
        info.subresourceRange.baseArrayLayer = 0;
        info.subresourceRange.layerCount = 1;
        VkImageView view;
        if (vkCreateImageView(device, &info, nullptr, &view) != VK_SUCCESS)
            throw std::runtime_error("Failed to create image view");
        return view;
    }

    void createOffscreenResources()
    {
        depthFormat = findDepthFormat();
        createImage(swapchainExtent.width, swapchainExtent.height, offscreenFormat,
                    VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
                    sceneColor, sceneColorMemory);
        sceneColorView = createImageView(sceneColor, offscreenFormat, VK_IMAGE_ASPECT_COLOR_BIT);

        createImage(swapchainExtent.width, swapchainExtent.height, offscreenFormat,
                    VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
                    sceneNormal, sceneNormalMemory);
        sceneNormalView = createImageView(sceneNormal, offscreenFormat, VK_IMAGE_ASPECT_COLOR_BIT);

        createImage(swapchainExtent.width, swapchainExtent.height, depthFormat,
                    VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
                    sceneDepth, sceneDepthMemory);
        sceneDepthView = createImageView(sceneDepth, depthFormat, VK_IMAGE_ASPECT_DEPTH_BIT);

        createImage(swapchainExtent.width, swapchainExtent.height, offscreenFormat,
                    VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
                    ssrResult, ssrResultMemory);
        ssrResultView = createImageView(ssrResult, offscreenFormat, VK_IMAGE_ASPECT_COLOR_BIT);
    }

    void createRenderPasses()
    {
        VkAttachmentDescription sceneAttachments[3]{};
        sceneAttachments[0].format = offscreenFormat;
        sceneAttachments[0].samples = VK_SAMPLE_COUNT_1_BIT;
        sceneAttachments[0].loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
        sceneAttachments[0].storeOp = VK_ATTACHMENT_STORE_OP_STORE;
        sceneAttachments[0].initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        sceneAttachments[0].finalLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        sceneAttachments[1] = sceneAttachments[0];
        sceneAttachments[2].format = depthFormat;
        sceneAttachments[2].samples = VK_SAMPLE_COUNT_1_BIT;
        sceneAttachments[2].loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
        sceneAttachments[2].storeOp = VK_ATTACHMENT_STORE_OP_STORE;
        sceneAttachments[2].stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
        sceneAttachments[2].stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
        sceneAttachments[2].initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        sceneAttachments[2].finalLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

        VkAttachmentReference color0{0, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL};
        VkAttachmentReference color1{1, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL};
        VkAttachmentReference depthRef{2, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL};
        std::array<VkAttachmentReference, 2> colors{color0, color1};
        VkSubpassDescription sceneSub{};
        sceneSub.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
        sceneSub.colorAttachmentCount = static_cast<uint32_t>(colors.size());
        sceneSub.pColorAttachments = colors.data();
        sceneSub.pDepthStencilAttachment = &depthRef;
        std::array<VkSubpassDependency, 2> sceneDeps{};
        sceneDeps[0].srcSubpass = VK_SUBPASS_EXTERNAL;
        sceneDeps[0].dstSubpass = 0;
        sceneDeps[0].srcStageMask = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
        sceneDeps[0].dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT | VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT;
        sceneDeps[0].dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
        sceneDeps[1].srcSubpass = 0;
        sceneDeps[1].dstSubpass = VK_SUBPASS_EXTERNAL;
        sceneDeps[1].srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT | VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT;
        sceneDeps[1].srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
        sceneDeps[1].dstStageMask = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
        sceneDeps[1].dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
        VkRenderPassCreateInfo sceneInfo{VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO};
        sceneInfo.attachmentCount = 3;
        sceneInfo.pAttachments = sceneAttachments;
        sceneInfo.subpassCount = 1;
        sceneInfo.pSubpasses = &sceneSub;
        sceneInfo.dependencyCount = static_cast<uint32_t>(sceneDeps.size());
        sceneInfo.pDependencies = sceneDeps.data();
        if (vkCreateRenderPass(device, &sceneInfo, nullptr, &sceneRenderPass) != VK_SUCCESS)
            throw std::runtime_error("Failed to create scene render pass");

        VkAttachmentDescription ssrAttachment{};
        ssrAttachment.format = offscreenFormat;
        ssrAttachment.samples = VK_SAMPLE_COUNT_1_BIT;
        ssrAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
        ssrAttachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
        ssrAttachment.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        ssrAttachment.finalLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        VkAttachmentReference ssrColor{0, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL};
        VkSubpassDescription ssrSub{};
        ssrSub.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
        ssrSub.colorAttachmentCount = 1;
        ssrSub.pColorAttachments = &ssrColor;
        std::array<VkSubpassDependency, 2> ssrDeps{};
        ssrDeps[0].srcSubpass = VK_SUBPASS_EXTERNAL;
        ssrDeps[0].dstSubpass = 0;
        ssrDeps[0].srcStageMask = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
        ssrDeps[0].dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
        ssrDeps[0].srcAccessMask = VK_ACCESS_SHADER_READ_BIT;
        ssrDeps[0].dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
        ssrDeps[1].srcSubpass = 0;
        ssrDeps[1].dstSubpass = VK_SUBPASS_EXTERNAL;
        ssrDeps[1].srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
        ssrDeps[1].srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
        ssrDeps[1].dstStageMask = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
        ssrDeps[1].dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
        VkRenderPassCreateInfo ssrInfo{VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO};
        ssrInfo.attachmentCount = 1;
        ssrInfo.pAttachments = &ssrAttachment;
        ssrInfo.subpassCount = 1;
        ssrInfo.pSubpasses = &ssrSub;
        ssrInfo.dependencyCount = static_cast<uint32_t>(ssrDeps.size());
        ssrInfo.pDependencies = ssrDeps.data();
        if (vkCreateRenderPass(device, &ssrInfo, nullptr, &ssrRenderPass) != VK_SUCCESS)
            throw std::runtime_error("Failed to create SSR render pass");

        VkAttachmentDescription swapAttachment{};
        swapAttachment.format = swapchainFormat;
        swapAttachment.samples = VK_SAMPLE_COUNT_1_BIT;
        swapAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
        swapAttachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
        swapAttachment.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        swapAttachment.finalLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
        VkAttachmentReference swapColor{0, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL};
        VkSubpassDescription compositeSub{};
        compositeSub.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
        compositeSub.colorAttachmentCount = 1;
        compositeSub.pColorAttachments = &swapColor;
        VkSubpassDependency compDep{};
        compDep.srcSubpass = VK_SUBPASS_EXTERNAL;
        compDep.dstSubpass = 0;
        compDep.srcStageMask = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
        compDep.dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
        compDep.srcAccessMask = VK_ACCESS_SHADER_READ_BIT;
        compDep.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
        VkRenderPassCreateInfo compInfo{VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO};
        compInfo.attachmentCount = 1;
        compInfo.pAttachments = &swapAttachment;
        compInfo.subpassCount = 1;
        compInfo.pSubpasses = &compositeSub;
        compInfo.dependencyCount = 1;
        compInfo.pDependencies = &compDep;
        if (vkCreateRenderPass(device, &compInfo, nullptr, &compositeRenderPass) != VK_SUCCESS)
            throw std::runtime_error("Failed to create composite render pass");
    }

    void createDescriptorSetLayouts()
    {
        VkDescriptorSetLayoutBinding sceneBinding{0, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1, VK_SHADER_STAGE_VERTEX_BIT, nullptr};
        VkDescriptorSetLayoutCreateInfo sceneInfo{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};
        sceneInfo.bindingCount = 1;
        sceneInfo.pBindings = &sceneBinding;
        if (vkCreateDescriptorSetLayout(device, &sceneInfo, nullptr, &sceneSetLayout) != VK_SUCCESS)
            throw std::runtime_error("Scene descriptor layout failed");

        std::array<VkDescriptorSetLayoutBinding, 5> ssrBindings{};
        for (int i = 0; i < 3; ++i)
            ssrBindings[i] = {static_cast<uint32_t>(i), VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_FRAGMENT_BIT, nullptr};
        ssrBindings[3] = {3, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1, VK_SHADER_STAGE_FRAGMENT_BIT, nullptr};
        ssrBindings[4] = {4, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1, VK_SHADER_STAGE_FRAGMENT_BIT, nullptr};
        VkDescriptorSetLayoutCreateInfo ssrInfo{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};
        ssrInfo.bindingCount = 5;
        ssrInfo.pBindings = ssrBindings.data();
        if (vkCreateDescriptorSetLayout(device, &ssrInfo, nullptr, &ssrSetLayout) != VK_SUCCESS)
            throw std::runtime_error("SSR descriptor layout failed");

        std::array<VkDescriptorSetLayoutBinding, 2> compBindings{
            VkDescriptorSetLayoutBinding{0, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_FRAGMENT_BIT, nullptr},
            VkDescriptorSetLayoutBinding{1, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_FRAGMENT_BIT, nullptr}};
        VkDescriptorSetLayoutCreateInfo compInfo{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};
        compInfo.bindingCount = 2;
        compInfo.pBindings = compBindings.data();
        if (vkCreateDescriptorSetLayout(device, &compInfo, nullptr, &compositeSetLayout) != VK_SUCCESS)
            throw std::runtime_error("Composite descriptor layout failed");
    }

    VkShaderModule createShaderModule(const std::string &path)
    {
        auto code = readFile(path);
        VkShaderModuleCreateInfo info{VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO};
        info.codeSize = code.size();
        info.pCode = reinterpret_cast<const uint32_t *>(code.data());
        VkShaderModule module;
        if (vkCreateShaderModule(device, &info, nullptr, &module) != VK_SUCCESS)
            throw std::runtime_error("Failed to create shader module: " + path);
        return module;
    }

    VkPipelineShaderStageCreateInfo shaderStage(VkShaderStageFlagBits stage, VkShaderModule module)
    {
        VkPipelineShaderStageCreateInfo info{VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO};
        info.stage = stage;
        info.module = module;
        info.pName = "main";
        return info;
    }

    void createPipelines()
    {
        const std::string dir = SHADER_DIR;
        VkShaderModule sv = createShaderModule(dir + "/scene.vert.spv");
        VkShaderModule sf = createShaderModule(dir + "/scene.frag.spv");
        VkShaderModule qv = createShaderModule(dir + "/ssr.vert.spv");
        VkShaderModule qf = createShaderModule(dir + "/ssr.frag.spv");
        VkShaderModule cv = createShaderModule(dir + "/composite.vert.spv");
        VkShaderModule cf = createShaderModule(dir + "/composite.frag.spv");

        VkPipelineLayoutCreateInfo sl{VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};
        sl.setLayoutCount = 1;
        sl.pSetLayouts = &sceneSetLayout;
        vkCreatePipelineLayout(device, &sl, nullptr, &scenePipelineLayout);
        VkPipelineLayoutCreateInfo ql{VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};
        ql.setLayoutCount = 1;
        ql.pSetLayouts = &ssrSetLayout;
        vkCreatePipelineLayout(device, &ql, nullptr, &ssrPipelineLayout);
        VkPipelineLayoutCreateInfo cl{VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};
        cl.setLayoutCount = 1;
        cl.pSetLayouts = &compositeSetLayout;
        vkCreatePipelineLayout(device, &cl, nullptr, &compositePipelineLayout);

        VkPipelineVertexInputStateCreateInfo vertexInput{VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO};
        VkVertexInputBindingDescription binding{0, sizeof(Vertex), VK_VERTEX_INPUT_RATE_VERTEX};
        std::array<VkVertexInputAttributeDescription, 3> attrs{
            VkVertexInputAttributeDescription{0, 0, VK_FORMAT_R32G32B32_SFLOAT, offsetof(Vertex, pos)},
            VkVertexInputAttributeDescription{1, 0, VK_FORMAT_R32G32B32_SFLOAT, offsetof(Vertex, normal)},
            VkVertexInputAttributeDescription{2, 0, VK_FORMAT_R32G32B32_SFLOAT, offsetof(Vertex, color)}};
        vertexInput.vertexBindingDescriptionCount = 1;
        vertexInput.pVertexBindingDescriptions = &binding;
        vertexInput.vertexAttributeDescriptionCount = static_cast<uint32_t>(attrs.size());
        vertexInput.pVertexAttributeDescriptions = attrs.data();

        VkPipelineInputAssemblyStateCreateInfo assembly{VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO};
        assembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
        VkViewport viewport{0, 0, float(swapchainExtent.width), float(swapchainExtent.height), 0, 1};
        VkRect2D scissor{{0, 0}, swapchainExtent};
        VkPipelineViewportStateCreateInfo vp{VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO};
        vp.viewportCount = 1;
        vp.pViewports = &viewport;
        vp.scissorCount = 1;
        vp.pScissors = &scissor;
        VkPipelineRasterizationStateCreateInfo rast{VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO};
        rast.polygonMode = VK_POLYGON_MODE_FILL;
        rast.cullMode = VK_CULL_MODE_NONE;
        rast.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
        rast.lineWidth = 1.0f;
        VkPipelineMultisampleStateCreateInfo ms{VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO};
        ms.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;
        VkPipelineDepthStencilStateCreateInfo ds{VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO};
        ds.depthTestEnable = VK_TRUE;
        ds.depthWriteEnable = VK_TRUE;
        ds.depthCompareOp = VK_COMPARE_OP_LESS;
        std::array<VkPipelineColorBlendAttachmentState, 2> blend{};
        for (auto &b : blend)
        {
            b.blendEnable = VK_FALSE;
            b.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
        }
        VkPipelineColorBlendStateCreateInfo cb{VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO};
        cb.attachmentCount = 2;
        cb.pAttachments = blend.data();
        VkDynamicState dynamicStates[] = {VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR};
        VkPipelineDynamicStateCreateInfo dyn{VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO};
        dyn.dynamicStateCount = 2;
        dyn.pDynamicStates = dynamicStates;

        auto ss = shaderStage(VK_SHADER_STAGE_VERTEX_BIT, sv);
        auto fs = shaderStage(VK_SHADER_STAGE_FRAGMENT_BIT, sf);
        std::array<VkPipelineShaderStageCreateInfo, 2> stages{ss, fs};
        VkGraphicsPipelineCreateInfo pi{VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO};
        pi.stageCount = 2;
        pi.pStages = stages.data();
        pi.pVertexInputState = &vertexInput;
        pi.pInputAssemblyState = &assembly;
        pi.pViewportState = &vp;
        pi.pRasterizationState = &rast;
        pi.pMultisampleState = &ms;
        pi.pDepthStencilState = &ds;
        pi.pColorBlendState = &cb;
        pi.pDynamicState = &dyn;
        pi.layout = scenePipelineLayout;
        pi.renderPass = sceneRenderPass;
        if (vkCreateGraphicsPipelines(device, VK_NULL_HANDLE, 1, &pi, nullptr, &scenePipeline) != VK_SUCCESS)
            throw std::runtime_error("Scene pipeline failed");

        VkPipelineColorBlendAttachmentState singleBlend{};
        singleBlend.blendEnable = VK_FALSE;
        singleBlend.colorWriteMask = 0xF;
        VkPipelineColorBlendStateCreateInfo qcb{VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO};
        qcb.attachmentCount = 1;
        qcb.pAttachments = &singleBlend;
        VkPipelineVertexInputStateCreateInfo emptyVI{VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO};
        auto qvs = shaderStage(VK_SHADER_STAGE_VERTEX_BIT, qv);
        auto qfs = shaderStage(VK_SHADER_STAGE_FRAGMENT_BIT, qf);
        std::array<VkPipelineShaderStageCreateInfo, 2> qst{qvs, qfs};
        VkGraphicsPipelineCreateInfo qi{VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO};
        qi.stageCount = 2;
        qi.pStages = qst.data();
        qi.pVertexInputState = &emptyVI;
        qi.pInputAssemblyState = &assembly;
        qi.pViewportState = &vp;
        qi.pRasterizationState = &rast;
        qi.pMultisampleState = &ms;
        qi.pColorBlendState = &qcb;
        qi.pDynamicState = &dyn;
        qi.layout = ssrPipelineLayout;
        qi.renderPass = ssrRenderPass;
        if (vkCreateGraphicsPipelines(device, VK_NULL_HANDLE, 1, &qi, nullptr, &ssrPipeline) != VK_SUCCESS)
            throw std::runtime_error("SSR pipeline failed");

        auto cvs = shaderStage(VK_SHADER_STAGE_VERTEX_BIT, cv);
        auto cfs = shaderStage(VK_SHADER_STAGE_FRAGMENT_BIT, cf);
        std::array<VkPipelineShaderStageCreateInfo, 2> cst{cvs, cfs};
        VkGraphicsPipelineCreateInfo ci{VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO};
        ci.stageCount = 2;
        ci.pStages = cst.data();
        ci.pVertexInputState = &emptyVI;
        ci.pInputAssemblyState = &assembly;
        ci.pViewportState = &vp;
        ci.pRasterizationState = &rast;
        ci.pMultisampleState = &ms;
        ci.pColorBlendState = &qcb;
        ci.pDynamicState = &dyn;
        ci.layout = compositePipelineLayout;
        ci.renderPass = compositeRenderPass;
        if (vkCreateGraphicsPipelines(device, VK_NULL_HANDLE, 1, &ci, nullptr, &compositePipeline) != VK_SUCCESS)
            throw std::runtime_error("Composite pipeline failed");

        vkDestroyShaderModule(device, sv, nullptr);
        vkDestroyShaderModule(device, sf, nullptr);
        vkDestroyShaderModule(device, qv, nullptr);
        vkDestroyShaderModule(device, qf, nullptr);
        vkDestroyShaderModule(device, cv, nullptr);
        vkDestroyShaderModule(device, cf, nullptr);
    }

    void createFramebuffers()
    {
        std::array<VkImageView, 3> sceneViews{sceneColorView, sceneNormalView, sceneDepthView};
        VkFramebufferCreateInfo s{VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO};
        s.renderPass = sceneRenderPass;
        s.attachmentCount = 3;
        s.pAttachments = sceneViews.data();
        s.width = swapchainExtent.width;
        s.height = swapchainExtent.height;
        s.layers = 1;
        if (vkCreateFramebuffer(device, &s, nullptr, &sceneFramebuffer) != VK_SUCCESS)
            throw std::runtime_error("Scene framebuffer failed");
        VkFramebufferCreateInfo q{VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO};
        q.renderPass = ssrRenderPass;
        q.attachmentCount = 1;
        q.pAttachments = &ssrResultView;
        q.width = swapchainExtent.width;
        q.height = swapchainExtent.height;
        q.layers = 1;
        if (vkCreateFramebuffer(device, &q, nullptr, &ssrFramebuffer) != VK_SUCCESS)
            throw std::runtime_error("SSR framebuffer failed");
        compositeFramebuffers.resize(swapchainImageViews.size());
        for (size_t i = 0; i < swapchainImageViews.size(); ++i)
        {
            VkFramebufferCreateInfo c{VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO};
            c.renderPass = compositeRenderPass;
            c.attachmentCount = 1;
            c.pAttachments = &swapchainImageViews[i];
            c.width = swapchainExtent.width;
            c.height = swapchainExtent.height;
            c.layers = 1;
            if (vkCreateFramebuffer(device, &c, nullptr, &compositeFramebuffers[i]) != VK_SUCCESS)
                throw std::runtime_error("Composite framebuffer failed");
        }
    }

    void createBuffer(VkDeviceSize size, VkBufferUsageFlags usage, VkMemoryPropertyFlags properties, VkBuffer &buffer, VkDeviceMemory &memory)
    {
        VkBufferCreateInfo info{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};
        info.size = size;
        info.usage = usage;
        info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
        if (vkCreateBuffer(device, &info, nullptr, &buffer) != VK_SUCCESS)
            throw std::runtime_error("Buffer creation failed");
        VkMemoryRequirements req{};
        vkGetBufferMemoryRequirements(device, buffer, &req);
        VkMemoryAllocateInfo alloc{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
        alloc.allocationSize = req.size;
        alloc.memoryTypeIndex = findMemoryType(req.memoryTypeBits, properties);
        if (vkAllocateMemory(device, &alloc, nullptr, &memory) != VK_SUCCESS)
            throw std::runtime_error("Buffer memory allocation failed");
        vkBindBufferMemory(device, buffer, memory, 0);
    }

    void createVertexBuffer()
    {
        std::vector<Vertex> vertices;
        auto addVertex = [&](const glm::vec3 &position, const glm::vec3 &normal, const glm::vec3 &color)
        {
            vertices.push_back({position, normal, color});
        };
        auto addTriangle = [&](const glm::vec3 &a, const glm::vec3 &b, const glm::vec3 &c,
                               const glm::vec3 &normal, const glm::vec3 &color)
        {
            addVertex(a, normal, color);
            addVertex(b, normal, color);
            addVertex(c, normal, color);
        };
        auto addQuad = [&](const glm::vec3 &a, const glm::vec3 &b, const glm::vec3 &c, const glm::vec3 &d,
                           const glm::vec3 &normal, const glm::vec3 &color)
        {
            addTriangle(a, b, c, normal, color);
            addTriangle(a, c, d, normal, color);
        };

        const float floorY = -1.0f;
        addQuad({-5.0f, floorY, 3.0f}, {5.0f, floorY, 3.0f}, {5.0f, floorY, -12.0f}, {-5.0f, floorY, -12.0f},
                {0.0f, 1.0f, 0.0f}, {0.22f, 0.25f, 0.31f});

        auto addBox = [&](const glm::vec3 &center, const glm::vec3 &halfSize, const glm::vec3 &color)
        {
            const float x0 = center.x - halfSize.x, x1 = center.x + halfSize.x;
            const float y0 = center.y - halfSize.y, y1 = center.y + halfSize.y;
            const float z0 = center.z - halfSize.z, z1 = center.z + halfSize.z;
            addQuad({x0, y1, z0}, {x0, y1, z1}, {x1, y1, z1}, {x1, y1, z0}, {0, 1, 0}, color);
            addQuad({x0, y0, z1}, {x0, y0, z0}, {x1, y0, z0}, {x1, y0, z1}, {0, -1, 0}, color * 0.55f);
            addQuad({x1, y0, z0}, {x1, y1, z0}, {x1, y1, z1}, {x1, y0, z1}, {1, 0, 0}, color * 0.82f);
            addQuad({x0, y0, z1}, {x0, y1, z1}, {x0, y1, z0}, {x0, y0, z0}, {-1, 0, 0}, color * 0.72f);
            addQuad({x0, y0, z1}, {x1, y0, z1}, {x1, y1, z1}, {x0, y1, z1}, {0, 0, 1}, color * 0.92f);
            addQuad({x1, y0, z0}, {x0, y0, z0}, {x0, y1, z0}, {x1, y1, z0}, {0, 0, -1}, color * 0.65f);
        };

        addBox({-1.35f, -0.35f, -2.1f}, {0.65f, 0.65f, 0.65f}, {0.95f, 0.12f, 0.055f});
        addBox({1.2f, -0.6f, -3.45f}, {0.4f, 0.4f, 0.4f}, {0.05f, 0.55f, 0.95f});

        const glm::vec3 sphereCenter{0.0f, -0.42f, -4.8f};
        constexpr float sphereRadius = 0.58f;
        constexpr int sphereRings = 12;
        constexpr int sphereSegments = 24;
        constexpr float pi = 3.14159265358979323846f;
        const glm::vec3 sphereColor{0.95f, 0.62f, 0.04f};
        auto sphereNormal = [&](int ring, int segment)
        {
            const float theta = pi * float(ring) / float(sphereRings);
            const float phi = 2.0f * pi * float(segment) / float(sphereSegments);
            return glm::vec3(std::sin(theta) * std::cos(phi), std::cos(theta), std::sin(theta) * std::sin(phi));
        };
        auto addSphereTriangle = [&](const glm::vec3 &normalA, const glm::vec3 &normalB, const glm::vec3 &normalC)
        {
            addVertex(sphereCenter + normalA * sphereRadius, normalA, sphereColor);
            addVertex(sphereCenter + normalB * sphereRadius, normalB, sphereColor);
            addVertex(sphereCenter + normalC * sphereRadius, normalC, sphereColor);
        };
        for (int ring = 0; ring < sphereRings; ++ring)
        {
            for (int segment = 0; segment < sphereSegments; ++segment)
            {
                const glm::vec3 a = sphereNormal(ring, segment);
                const glm::vec3 b = sphereNormal(ring, segment + 1);
                const glm::vec3 c = sphereNormal(ring + 1, segment + 1);
                const glm::vec3 d = sphereNormal(ring + 1, segment);
                if (ring > 0)
                    addSphereTriangle(a, b, d);
                if (ring + 1 < sphereRings)
                    addSphereTriangle(b, c, d);
            }
        }

        vertexCount = static_cast<uint32_t>(vertices.size());
        createBuffer(sizeof(Vertex) * vertices.size(), VK_BUFFER_USAGE_VERTEX_BUFFER_BIT, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, vertexBuffer, vertexMemory);
        void *data;
        vkMapMemory(device, vertexMemory, 0, VK_WHOLE_SIZE, 0, &data);
        memcpy(data, vertices.data(), sizeof(Vertex) * vertices.size());
        vkUnmapMemory(device, vertexMemory);
    }

    void createUniformBuffers()
    {
        createBuffer(sizeof(CameraData), VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, cameraBuffer, cameraMemory);
        createBuffer(sizeof(SSRData), VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, ssrBuffer, ssrMemory);
        updateUniforms();
    }

    void updateUniforms()
    {
        CameraData camera{};
        camera.model = glm::mat4(1.0f);
        camera.view = glm::lookAt(glm::vec3(0.0f, 4.0f, 7.5f), glm::vec3(0.0f, -0.65f, -2.5f), glm::vec3(0, 1, 0));
        camera.projection = glm::perspective(glm::radians(55.0f), float(swapchainExtent.width) / float(swapchainExtent.height), 0.1f, 40.0f);
        camera.projection[1][1] *= -1.0f;
        camera.inverseProjection = glm::inverse(camera.projection);
        camera.inverseView = glm::inverse(camera.view);
        void *data;
        vkMapMemory(device, cameraMemory, 0, sizeof(camera), 0, &data);
        memcpy(data, &camera, sizeof(camera));
        vkUnmapMemory(device, cameraMemory);
        SSRData params{};
        vkMapMemory(device, ssrMemory, 0, sizeof(params), 0, &data);
        memcpy(data, &params, sizeof(params));
        vkUnmapMemory(device, ssrMemory);
    }

    void createSampler()
    {
        VkSamplerCreateInfo info{VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO};
        info.magFilter = VK_FILTER_LINEAR;
        info.minFilter = VK_FILTER_LINEAR;
        info.addressModeU = info.addressModeV = info.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
        info.anisotropyEnable = VK_TRUE;
        info.maxAnisotropy = 1.0f;
        info.borderColor = VK_BORDER_COLOR_FLOAT_OPAQUE_BLACK;
        info.unnormalizedCoordinates = VK_FALSE;
        info.compareEnable = VK_FALSE;
        info.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
        if (vkCreateSampler(device, &info, nullptr, &sampler) != VK_SUCCESS)
            throw std::runtime_error("Sampler creation failed");
    }

    void createDescriptorPool()
    {
        std::array<VkDescriptorPoolSize, 2> sizes{
            VkDescriptorPoolSize{VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 3},
            VkDescriptorPoolSize{VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 5}};
        VkDescriptorPoolCreateInfo info{VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO};
        info.poolSizeCount = 2;
        info.pPoolSizes = sizes.data();
        info.maxSets = 3;
        if (vkCreateDescriptorPool(device, &info, nullptr, &descriptorPool) != VK_SUCCESS)
            throw std::runtime_error("Descriptor pool failed");
    }

    void createDescriptorSets()
    {
        std::array<VkDescriptorSetLayout, 3> layouts{sceneSetLayout, ssrSetLayout, compositeSetLayout};
        std::array<VkDescriptorSet, 3> sets{};
        VkDescriptorSetAllocateInfo alloc{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO};
        alloc.descriptorPool = descriptorPool;
        alloc.descriptorSetCount = 3;
        alloc.pSetLayouts = layouts.data();
        if (vkAllocateDescriptorSets(device, &alloc, sets.data()) != VK_SUCCESS)
            throw std::runtime_error("Descriptor allocation failed");
        sceneSet = sets[0];
        ssrSet = sets[1];
        compositeSet = sets[2];

        VkDescriptorBufferInfo cam{cameraBuffer, 0, sizeof(CameraData)};
        VkWriteDescriptorSet sw{VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};
        sw.dstSet = sceneSet;
        sw.dstBinding = 0;
        sw.descriptorCount = 1;
        sw.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
        sw.pBufferInfo = &cam;
        vkUpdateDescriptorSets(device, 1, &sw, 0, nullptr);

        VkDescriptorBufferInfo ssrCam{cameraBuffer, 0, sizeof(CameraData)};
        VkDescriptorBufferInfo params{ssrBuffer, 0, sizeof(SSRData)};
        VkDescriptorImageInfo color{sampler, sceneColorView, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
        VkDescriptorImageInfo depth{sampler, sceneDepthView, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
        VkDescriptorImageInfo normal{sampler, sceneNormalView, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
        std::array<VkWriteDescriptorSet, 5> qw{};
        qw[0] = {VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, nullptr, ssrSet, 0, 0, 1, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, nullptr, nullptr, nullptr};
        qw[0].pImageInfo = &color;
        qw[1] = {VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, nullptr, ssrSet, 1, 0, 1, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, nullptr, nullptr, nullptr};
        qw[1].pImageInfo = &depth;
        qw[2] = {VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, nullptr, ssrSet, 2, 0, 1, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, nullptr, nullptr, nullptr};
        qw[2].pImageInfo = &normal;
        qw[3] = {VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, nullptr, ssrSet, 3, 0, 1, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, nullptr, nullptr, nullptr};
        qw[3].pBufferInfo = &ssrCam;
        qw[4] = {VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, nullptr, ssrSet, 4, 0, 1, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, nullptr, nullptr, nullptr};
        qw[4].pBufferInfo = &params;
        vkUpdateDescriptorSets(device, 5, qw.data(), 0, nullptr);

        VkDescriptorImageInfo compScene{sampler, sceneColorView, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
        VkDescriptorImageInfo compSSR{sampler, ssrResultView, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
        std::array<VkWriteDescriptorSet, 2> cw{};
        cw[0] = {VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, nullptr, compositeSet, 0, 0, 1, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, nullptr, nullptr, nullptr};
        cw[0].pImageInfo = &compScene;
        cw[1] = {VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, nullptr, compositeSet, 1, 0, 1, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, nullptr, nullptr, nullptr};
        cw[1].pImageInfo = &compSSR;
        vkUpdateDescriptorSets(device, 2, cw.data(), 0, nullptr);
    }

    void createCommandBuffers()
    {
        commandBuffers.resize(MAX_FRAMES_IN_FLIGHT);
        VkCommandBufferAllocateInfo info{VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};
        info.commandPool = commandPool;
        info.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
        info.commandBufferCount = MAX_FRAMES_IN_FLIGHT;
        if (vkAllocateCommandBuffers(device, &info, commandBuffers.data()) != VK_SUCCESS)
            throw std::runtime_error("Command buffer allocation failed");
    }

    void createSyncObjects()
    {
        VkSemaphoreCreateInfo sem{VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO};
        VkFenceCreateInfo fence{VK_STRUCTURE_TYPE_FENCE_CREATE_INFO};
        fence.flags = VK_FENCE_CREATE_SIGNALED_BIT;
        for (int i = 0; i < MAX_FRAMES_IN_FLIGHT; ++i)
        {
            if (vkCreateSemaphore(device, &sem, nullptr, &imageAvailable[i]) != VK_SUCCESS || vkCreateFence(device, &fence, nullptr, &inFlight[i]) != VK_SUCCESS)
                throw std::runtime_error("Synchronization object creation failed");
        }
    }

    void recordCommandBuffer(VkCommandBuffer cmd, uint32_t imageIndex)
    {
        VkCommandBufferBeginInfo begin{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
        if (vkBeginCommandBuffer(cmd, &begin) != VK_SUCCESS)
            throw std::runtime_error("Failed to begin command buffer");

        std::array<VkClearValue, 3> sceneClear{};
        sceneClear[0].color = {{0.015f, 0.02f, 0.04f, 1.0f}};
        sceneClear[1].color = {{0.5f, 0.5f, 1.0f, 1.0f}};
        sceneClear[2].depthStencil = {1.0f, 0};
        VkRenderPassBeginInfo scene{VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO};
        scene.renderPass = sceneRenderPass;
        scene.framebuffer = sceneFramebuffer;
        scene.renderArea = {{0, 0}, swapchainExtent};
        scene.clearValueCount = 3;
        scene.pClearValues = sceneClear.data();
        vkCmdBeginRenderPass(cmd, &scene, VK_SUBPASS_CONTENTS_INLINE);
        vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, scenePipeline);
        VkViewport dv{0, 0, float(swapchainExtent.width), float(swapchainExtent.height), 0, 1};
        VkRect2D ds{{0, 0}, swapchainExtent};
        vkCmdSetViewport(cmd, 0, 1, &dv);
        vkCmdSetScissor(cmd, 0, 1, &ds);
        VkDeviceSize offset = 0;
        vkCmdBindVertexBuffers(cmd, 0, 1, &vertexBuffer, &offset);
        vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, scenePipelineLayout, 0, 1, &sceneSet, 0, nullptr);
        vkCmdDraw(cmd, vertexCount, 1, 0, 0);
        vkCmdEndRenderPass(cmd);

        std::array<VkClearValue, 1> ssrClear{};
        ssrClear[0].color = {{0, 0, 0, 0}};
        VkRenderPassBeginInfo ssr{VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO};
        ssr.renderPass = ssrRenderPass;
        ssr.framebuffer = ssrFramebuffer;
        ssr.renderArea = {{0, 0}, swapchainExtent};
        ssr.clearValueCount = 1;
        ssr.pClearValues = ssrClear.data();
        vkCmdBeginRenderPass(cmd, &ssr, VK_SUBPASS_CONTENTS_INLINE);
        vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, ssrPipeline);
        VkViewport qv{0, 0, float(swapchainExtent.width), float(swapchainExtent.height), 0, 1};
        VkRect2D qs{{0, 0}, swapchainExtent};
        vkCmdSetViewport(cmd, 0, 1, &qv);
        vkCmdSetScissor(cmd, 0, 1, &qs);
        vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, ssrPipelineLayout, 0, 1, &ssrSet, 0, nullptr);
        vkCmdDraw(cmd, 3, 1, 0, 0);
        vkCmdEndRenderPass(cmd);

        VkClearValue finalClear{};
        finalClear.color = {{0.0f, 0.0f, 0.0f, 1.0f}};
        VkRenderPassBeginInfo comp{VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO};
        comp.renderPass = compositeRenderPass;
        comp.framebuffer = compositeFramebuffers[imageIndex];
        comp.renderArea = {{0, 0}, swapchainExtent};
        comp.clearValueCount = 1;
        comp.pClearValues = &finalClear;
        vkCmdBeginRenderPass(cmd, &comp, VK_SUBPASS_CONTENTS_INLINE);
        vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, compositePipeline);
        VkViewport cvp{0, 0, float(swapchainExtent.width), float(swapchainExtent.height), 0, 1};
        VkRect2D csc{{0, 0}, swapchainExtent};
        vkCmdSetViewport(cmd, 0, 1, &cvp);
        vkCmdSetScissor(cmd, 0, 1, &csc);
        vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, compositePipelineLayout, 0, 1, &compositeSet, 0, nullptr);
        vkCmdDraw(cmd, 3, 1, 0, 0);
        vkCmdEndRenderPass(cmd);

        if (vkEndCommandBuffer(cmd) != VK_SUCCESS)
            throw std::runtime_error("Failed to record command buffer");
    }

    void drawFrame()
    {
        vkWaitForFences(device, 1, &inFlight[currentFrame], VK_TRUE, UINT64_MAX);
        uint32_t imageIndex = 0;
        VkResult result = vkAcquireNextImageKHR(device, swapchain, UINT64_MAX, imageAvailable[currentFrame], VK_NULL_HANDLE, &imageIndex);
        if (result == VK_ERROR_OUT_OF_DATE_KHR)
        {
            recreateSwapchain();
            return;
        }
        if (result != VK_SUCCESS && result != VK_SUBOPTIMAL_KHR)
            throw std::runtime_error("Failed to acquire swapchain image");
        vkResetFences(device, 1, &inFlight[currentFrame]);
        vkResetCommandBuffer(commandBuffers[currentFrame], 0);
        recordCommandBuffer(commandBuffers[currentFrame], imageIndex);
        // Wait before render-pass layout transitions of the acquired image.
        VkPipelineStageFlags waitStage = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
        VkSubmitInfo submit{VK_STRUCTURE_TYPE_SUBMIT_INFO};
        submit.waitSemaphoreCount = 1;
        submit.pWaitSemaphores = &imageAvailable[currentFrame];
        submit.pWaitDstStageMask = &waitStage;
        submit.commandBufferCount = 1;
        submit.pCommandBuffers = &commandBuffers[currentFrame];
        submit.signalSemaphoreCount = 1;
        submit.pSignalSemaphores = &renderFinished[imageIndex];
        if (vkQueueSubmit(graphicsQueue, 1, &submit, inFlight[currentFrame]) != VK_SUCCESS)
            throw std::runtime_error("Failed to submit command buffer");
        VkPresentInfoKHR present{VK_STRUCTURE_TYPE_PRESENT_INFO_KHR};
        present.waitSemaphoreCount = 1;
        present.pWaitSemaphores = &renderFinished[imageIndex];
        present.swapchainCount = 1;
        present.pSwapchains = &swapchain;
        present.pImageIndices = &imageIndex;
        result = vkQueuePresentKHR(presentQueue, &present);
        if (result == VK_ERROR_OUT_OF_DATE_KHR || result == VK_SUBOPTIMAL_KHR || framebufferResized)
        {
            framebufferResized = false;
            recreateSwapchain();
        }
        else if (result != VK_SUCCESS)
            throw std::runtime_error("Failed to present swapchain image");
        currentFrame = (currentFrame + 1) % MAX_FRAMES_IN_FLIGHT;
    }

    void mainLoop()
    {
        while (!glfwWindowShouldClose(window))
        {
            glfwPollEvents();
            drawFrame();
        }
        vkDeviceWaitIdle(device);
    }

    void cleanupSwapchain()
    {
        for (auto fb : compositeFramebuffers)
            vkDestroyFramebuffer(device, fb, nullptr);
        compositeFramebuffers.clear();
        for (auto semaphore : renderFinished)
            vkDestroySemaphore(device, semaphore, nullptr);
        renderFinished.clear();
        vkDestroyFramebuffer(device, ssrFramebuffer, nullptr);
        ssrFramebuffer = VK_NULL_HANDLE;
        vkDestroyFramebuffer(device, sceneFramebuffer, nullptr);
        sceneFramebuffer = VK_NULL_HANDLE;
        vkDestroyImageView(device, ssrResultView, nullptr);
        vkDestroyImage(device, ssrResult, nullptr);
        vkFreeMemory(device, ssrResultMemory, nullptr);
        vkDestroyImageView(device, sceneDepthView, nullptr);
        vkDestroyImage(device, sceneDepth, nullptr);
        vkFreeMemory(device, sceneDepthMemory, nullptr);
        vkDestroyImageView(device, sceneNormalView, nullptr);
        vkDestroyImage(device, sceneNormal, nullptr);
        vkFreeMemory(device, sceneNormalMemory, nullptr);
        vkDestroyImageView(device, sceneColorView, nullptr);
        vkDestroyImage(device, sceneColor, nullptr);
        vkFreeMemory(device, sceneColorMemory, nullptr);
        for (auto v : swapchainImageViews)
            vkDestroyImageView(device, v, nullptr);
        swapchainImageViews.clear();
        vkDestroySwapchainKHR(device, swapchain, nullptr);
        swapchain = VK_NULL_HANDLE;
    }

    void recreateSwapchain()
    {
        int w = 0, h = 0;
        glfwGetFramebufferSize(window, &w, &h);
        while (w == 0 || h == 0)
        {
            glfwGetFramebufferSize(window, &w, &h);
            glfwWaitEvents();
        }
        vkDeviceWaitIdle(device);
        // Descriptor sets must not retain views destroyed with the old swapchain.
        vkDestroyDescriptorPool(device, descriptorPool, nullptr);
        descriptorPool = VK_NULL_HANDLE;
        cleanupSwapchain();
        createSwapchain();
        createSwapchainImageViews();
        createOffscreenResources();
        createFramebuffers();
        updateUniforms();
        createDescriptorPool();
        createDescriptorSets();
    }

    void cleanup()
    {
        vkDeviceWaitIdle(device);
        for (int i = 0; i < MAX_FRAMES_IN_FLIGHT; ++i)
        {
            vkDestroySemaphore(device, imageAvailable[i], nullptr);
            vkDestroyFence(device, inFlight[i], nullptr);
        }
        vkDestroyDescriptorPool(device, descriptorPool, nullptr);
        vkDestroyDescriptorSetLayout(device, sceneSetLayout, nullptr);
        vkDestroyDescriptorSetLayout(device, ssrSetLayout, nullptr);
        vkDestroyDescriptorSetLayout(device, compositeSetLayout, nullptr);
        vkDestroySampler(device, sampler, nullptr);
        vkDestroyBuffer(device, ssrBuffer, nullptr);
        vkFreeMemory(device, ssrMemory, nullptr);
        vkDestroyBuffer(device, cameraBuffer, nullptr);
        vkFreeMemory(device, cameraMemory, nullptr);
        vkDestroyBuffer(device, vertexBuffer, nullptr);
        vkFreeMemory(device, vertexMemory, nullptr);
        cleanupSwapchain();
        vkDestroyPipeline(device, scenePipeline, nullptr);
        vkDestroyPipeline(device, ssrPipeline, nullptr);
        vkDestroyPipeline(device, compositePipeline, nullptr);
        vkDestroyPipelineLayout(device, scenePipelineLayout, nullptr);
        vkDestroyPipelineLayout(device, ssrPipelineLayout, nullptr);
        vkDestroyPipelineLayout(device, compositePipelineLayout, nullptr);
        vkDestroyRenderPass(device, sceneRenderPass, nullptr);
        vkDestroyRenderPass(device, ssrRenderPass, nullptr);
        vkDestroyRenderPass(device, compositeRenderPass, nullptr);
        vkDestroyCommandPool(device, commandPool, nullptr);
        vkDestroyDevice(device, nullptr);
        if (enableValidation && debugMessenger)
        {
            auto fn = reinterpret_cast<PFN_vkDestroyDebugUtilsMessengerEXT>(vkGetInstanceProcAddr(instance, "vkDestroyDebugUtilsMessengerEXT"));
            if (fn)
                fn(instance, debugMessenger, nullptr);
        }
        vkDestroySurfaceKHR(instance, surface, nullptr);
        vkDestroyInstance(instance, nullptr);
        glfwDestroyWindow(window);
        glfwTerminate();
    }
};

int main()
{
    try
    {
        VulkanSSRApp app;
        app.run();
        return EXIT_SUCCESS;
    }
    catch (const std::exception &e)
    {
        std::cerr << "Fatal error: " << e.what() << '\n';
        return EXIT_FAILURE;
    }
}
