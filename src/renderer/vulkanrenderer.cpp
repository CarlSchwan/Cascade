/*
 *  Cascade Image Editor
 *
 *  Copyright (C) 2022 Till Dechent and contributors
 *
 *  This program is free software: you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation, either version 3 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program.  If not, see <https://www.gnu.org/licenses/>.
*/

#include "vulkanrenderer.h"

#include <QVulkanFunctions>
#include <QCoreApplication>
#include <QFile>
#include <QMouseEvent>
#include <QVulkanWindowRenderer>

#include <OpenImageIO/imagebufalgo.h>
#include <OpenImageIO/color.h>

#include "../vulkanwindow.h"
#include "../uientities/fileboxentity.h"
#include "../benchmark.h"
#include "../multithreading.h"
#include "../log.h"
#include "renderutility.h"

namespace Cascade::Renderer {

// Use a triangle strip to get a quad.
static float vertexData[] = { // Y up, front = CW
    // x, y, z, u, v
    -1,  -1, 0, 0, 1,
    -1,   1, 0, 0, 0,
     1,  -1, 0, 1, 1,
     1,   1, 0, 1, 0
};

VulkanRenderer::VulkanRenderer(VulkanWindow *w)
    : window(w)
{
    concurrentFrameCount = window->concurrentFrameCount();
}

void VulkanRenderer::initResources()
{
    // Get device and functions
    device = window->device();
    physicalDevice = window->physicalDevice();

    // Init all the permanent parts of the renderer
    createVertexBuffer();
    createSampler();
    createDescriptorPool();
    createGraphicsDescriptors();
    createGraphicsPipelineCache();
    createGraphicsPipelineLayout();

    createGraphicsPipeline(graphicsPipelineRGB, ":/shaders/texture_frag.spv");
    createGraphicsPipeline(graphicsPipelineAlpha, ":/shaders/texture_alpha_frag.spv");

    createComputeDescriptors();
    createComputePipelineLayout();

    // Load all the shaders we need and create their pipelines
    loadShadersFromDisk();
    // Create Noop pipeline
    computePipelineNoop = createComputePipeline(
                createShaderFromFile(":/shaders/noop_comp.spv").get());
    // Create a pipeline for each shader
    createComputePipelines();

    computeCommandBuffer = std::unique_ptr<CsCommandBuffer>(
                new CsCommandBuffer(&device,
                                    &physicalDevice,
                                    &computePipelineLayout.get(),
                                    &computeDescriptorSet.get()));

    settingsBuffer = std::unique_ptr<CsSettingsBuffer>(new CsSettingsBuffer(
                &device,
                &physicalDevice));

    // Load OCIO config
    try
    {
        const char* file = "ocio/config.ocio";
        ocioConfig = OCIO::Config::CreateFromFile(file);
    }
    catch(OCIO::Exception& exception)
    {
        CS_LOG_WARNING("OpenColorIO Error: " + QString(exception.what()));
    }

    emit window->rendererHasBeenCreated();
}

QString VulkanRenderer::getGpuName()
{
    vk::PhysicalDeviceProperties deviceProps = physicalDevice.getProperties();
    auto deviceName = QString::fromLatin1(deviceProps.deviceName);

    return deviceName;
}

void VulkanRenderer::createVertexBuffer()
{
    // The current vertexBuffer will be destroyed,
    // so we have to wait here.
    auto result = device.waitIdle();

    const vk::PhysicalDeviceLimits pdevLimits(physicalDevice.getProperties().limits);
    const vk::DeviceSize uniAlign = pdevLimits.minUniformBufferOffsetAlignment;

    const vk::DeviceSize vertexAllocSize = aligned(sizeof(vertexData), uniAlign);
    const vk::DeviceSize uniformAllocSize = aligned(uniformDataSize, uniAlign);

    vk::BufferCreateInfo bufferInfo
            ({},
             vertexAllocSize + concurrentFrameCount * uniformAllocSize,
             vk::BufferUsageFlags(
                            vk::BufferUsageFlagBits::eVertexBuffer |
                            vk::BufferUsageFlagBits::eUniformBuffer));
    vertexBuffer = device.createBufferUnique(bufferInfo).value;

#ifdef QT_DEBUG
    {
        vk::DebugUtilsObjectNameInfoEXT debugUtilsObjectNameInfo(
                    vk::ObjectType::eBuffer,
                    NON_DISPATCHABLE_HANDLE_TO_UINT64_CAST(VkBuffer, *vertexBuffer),
                    "Vertex Buffer");
        result = device.setDebugUtilsObjectNameEXT(debugUtilsObjectNameInfo);
    }
#endif

    vk::MemoryRequirements memReq = device.getBufferMemoryRequirements(*vertexBuffer);

    vk::MemoryAllocateInfo memAllocInfo(memReq.size, window->hostVisibleMemoryIndex());

    vertexBufferMemory = device.allocateMemoryUnique(memAllocInfo).value;

#ifdef QT_DEBUG
    {
        vk::DebugUtilsObjectNameInfoEXT debugUtilsObjectNameInfo(
                    vk::ObjectType::eDeviceMemory,
                    NON_DISPATCHABLE_HANDLE_TO_UINT64_CAST(VkDeviceMemory, *vertexBufferMemory),
                    "Vertex Buffer Memory");
        result = device.setDebugUtilsObjectNameEXT(debugUtilsObjectNameInfo);
    }
#endif

    // copy the vertex and color data into device memory
    uint8_t* pData = static_cast<uint8_t *>(
                device.mapMemory(
                    vertexBufferMemory.get(), 0, memReq.size).value);
    memcpy(pData, vertexData, sizeof(vertexData));

    QMatrix4x4 ident;
    for (int i = 0; i < concurrentFrameCount; ++i)
    {
        const vk::DeviceSize offset = vertexAllocSize + i * uniformAllocSize;
        memcpy(pData + offset, ident.constData(), 16 * sizeof(float));
        uniformBufferInfo[i].setBuffer(*vertexBuffer);
        uniformBufferInfo[i].setOffset(offset);
        uniformBufferInfo[i].setRange(uniformAllocSize);
    }
    device.unmapMemory(vertexBufferMemory.get());

    result = device.bindBufferMemory(*vertexBuffer, *vertexBufferMemory, 0);
    Q_UNUSED(result);
}

void VulkanRenderer::createSampler()
{
    // Create sampler
    vk::SamplerCreateInfo samplerInfo(
                {},
                vk::Filter::eNearest,
                vk::Filter::eNearest,
                vk::SamplerMipmapMode::eNearest,
                vk::SamplerAddressMode::eClampToEdge,
                vk::SamplerAddressMode::eClampToEdge,
                vk::SamplerAddressMode::eClampToEdge,
                {},
                false);

    sampler = device.createSamplerUnique(samplerInfo).value;
}

void VulkanRenderer::createDescriptorPool()
{
    // Create descriptor pool
    std::vector<vk::DescriptorPoolSize> descPoolSizes = {
        { vk::DescriptorType::eUniformBuffer,         3 * uint32_t(concurrentFrameCount) },
        { vk::DescriptorType::eCombinedImageSampler,  1 * uint32_t(concurrentFrameCount) },
        { vk::DescriptorType::eCombinedImageSampler,  1 * uint32_t(concurrentFrameCount) },
        { vk::DescriptorType::eStorageImage,          6 * uint32_t(concurrentFrameCount) }
    };

    vk::DescriptorPoolCreateInfo descPoolInfo(
                {},
                6,
                4,
                descPoolSizes.data());

    descriptorPool = device.createDescriptorPoolUnique(descPoolInfo).value;
}

void VulkanRenderer::createGraphicsDescriptors()
{
    // Create DescriptorSetLayout
    std::vector<vk::DescriptorSetLayoutBinding> layoutBinding = {
        {
            0, // binding
            vk::DescriptorType::eUniformBuffer,
            1, // descriptorCount
            vk::ShaderStageFlagBits::eVertex
        },
        {
            1, // binding
            vk::DescriptorType::eCombinedImageSampler,
            1, // descriptorCount
            vk::ShaderStageFlagBits::eFragment
        },
        {
            2, // binding
            vk::DescriptorType::eCombinedImageSampler,
            1, // descriptorCount
            vk::ShaderStageFlagBits::eFragment
        }
    };

    vk::DescriptorSetLayoutCreateInfo descLayoutInfo(
                {},
                3, // bindingCount
                layoutBinding.data());

    graphicsDescriptorSetLayout = device.createDescriptorSetLayoutUnique(descLayoutInfo).value;
}

void VulkanRenderer::createGraphicsPipelineCache()
{
    // Pipeline cache
    vk::PipelineCacheCreateInfo pipelineCacheInfo;
    pipelineCache = device.createPipelineCacheUnique(pipelineCacheInfo).value;
}

void VulkanRenderer::createGraphicsPipelineLayout()
{
    vk::PushConstantRange pushConstantRange;
    pushConstantRange.stageFlags                = vk::ShaderStageFlagBits::eFragment;
    pushConstantRange.offset                    = 0;
    pushConstantRange.size                      = sizeof(viewerPushConstants);

    vk::PipelineLayoutCreateInfo pipelineLayoutInfo(
                {},
                1,
                &(*graphicsDescriptorSetLayout),
                1,
                &pushConstantRange);

    graphicsPipelineLayout = device.createPipelineLayoutUnique(pipelineLayoutInfo).value;
}

void VulkanRenderer::fillSettingsBuffer(const NodeBase* node)
{
    auto props = node->getAllPropertyValues();

    settingsBuffer->fillBuffer(props);
}

void VulkanRenderer::createGraphicsPipeline(
        vk::UniquePipeline& pl,
        const QString& fragShaderPath)
{
    // Vertex shader never changes
    vk::UniqueShaderModule vertShaderModule = createShaderFromFile(":/shaders/texture_vert.spv");

    vk::UniqueShaderModule fragShaderModule = createShaderFromFile(fragShaderPath);

    // Graphics pipeline
    vk::GraphicsPipelineCreateInfo pipelineInfo;

    std::vector<vk::PipelineShaderStageCreateInfo> shaderStages =
    {
        {
            {},
            vk::ShaderStageFlagBits::eVertex,
            *vertShaderModule,
            "main"
        },
        {
            {},
            vk::ShaderStageFlagBits::eFragment,
            *fragShaderModule,
            "main"
        }
    };
    pipelineInfo.stageCount = 2;
    pipelineInfo.pStages = shaderStages.data();

    // Vertex binding
    vk::VertexInputBindingDescription vertexBindingDesc(
                0,
                5 * sizeof(float),
                vk::VertexInputRate::eVertex);

    std::vector<vk::VertexInputAttributeDescription> vertexAttrDesc(
    {
        { // position
            0, // location
            0, // binding
            vk::Format::eR32G32B32Sfloat,
            0
        },
        { // texcoord
            1,
            0,
            vk::Format::eR32G32Sfloat,
            3 * sizeof(float)
        }
    });

    vk::PipelineVertexInputStateCreateInfo vertexInputInfo(
                {},
                1,
                &vertexBindingDesc,
                2,
                vertexAttrDesc.data());

    pipelineInfo.pVertexInputState = &vertexInputInfo;

    vk::PipelineInputAssemblyStateCreateInfo ia(
                {},
                vk::PrimitiveTopology::eTriangleStrip);
    pipelineInfo.pInputAssemblyState = &ia;

    // The viewport and scissor will be set dynamically via vkCmdSetViewport/Scissor.
    // This way the pipeline does not need to be touched when resizing the window.
    vk::PipelineViewportStateCreateInfo vp(
                {},
                1,
                {},
                1);
    pipelineInfo.pViewportState = &vp;

    vk::PipelineRasterizationStateCreateInfo rs(
                {},
                {},
                {},
                vk::PolygonMode::eFill,
                vk::CullModeFlagBits::eBack,
                vk::FrontFace::eClockwise,
                {},
                {},
                {},
                {},
                1.0f);
    pipelineInfo.pRasterizationState = &rs;

    vk::PipelineMultisampleStateCreateInfo ms(
                {},
                vk::SampleCountFlagBits::e1);
    pipelineInfo.pMultisampleState = &ms;

    vk::PipelineDepthStencilStateCreateInfo ds(
                {},
                true,
                true,
                vk::CompareOp::eLessOrEqual);
    pipelineInfo.pDepthStencilState = &ds;

    // assume pre-multiplied alpha, blend, write out all of rgba
    vk::PipelineColorBlendAttachmentState att(
                true,
                vk::BlendFactor::eOne,
                vk::BlendFactor::eOne,
                vk::BlendOp::eAdd,
                vk::BlendFactor::eOne,
                vk::BlendFactor::eOne,
                vk::BlendOp::eAdd,
                {
                    vk::ColorComponentFlags(
                        vk::ColorComponentFlagBits::eR |
                        vk::ColorComponentFlagBits::eG |
                        vk::ColorComponentFlagBits::eB |
                        vk::ColorComponentFlagBits::eA)
                });

    vk::PipelineColorBlendStateCreateInfo cb(
                {},
                {},
                {},
                1,
                &att);
    pipelineInfo.pColorBlendState = &cb;

    vk::DynamicState dynEnable[] =
    { vk::DynamicState::eViewport, vk::DynamicState::eScissor };

    vk::PipelineDynamicStateCreateInfo dyn(
                {},
                sizeof(dynEnable) / sizeof(vk::DynamicState),
                dynEnable);
    pipelineInfo.pDynamicState = &dyn;

    pipelineInfo.layout = *graphicsPipelineLayout;
    pipelineInfo.renderPass = window->defaultRenderPass();

    pl = std::move(device.createGraphicsPipelineUnique(*pipelineCache, pipelineInfo).value);
}

vk::UniqueShaderModule VulkanRenderer::createShaderFromFile(const QString &name)
{
    QFile file(name);
    if (!file.open(QIODevice::ReadOnly))
    {
        CS_LOG_WARNING("Failed to read shader:");
        CS_LOG_WARNING(qPrintable(name));
    }
    QByteArray blob = file.readAll();
    file.close();

    vk::ShaderModuleCreateInfo shaderInfo(
                {},
                blob.size(),
                reinterpret_cast<const uint32_t *>(blob.constData()));

    vk::UniqueShaderModule shaderModule = device.createShaderModuleUnique(shaderInfo).value;

    return shaderModule;
}

vk::UniqueShaderModule VulkanRenderer::createShaderFromCode(const std::vector<unsigned int> &code)
{
    auto codeChar = uintVecToCharVec(code);

    QByteArray codeArray = QByteArray(reinterpret_cast<const char*>(codeChar.data()), codeChar.size());

    vk::ShaderModuleCreateInfo shaderInfo(
                {},
                codeArray.size(),
                reinterpret_cast<const uint32_t *>(codeArray.constData()));

    vk::UniqueShaderModule shaderModule = device.createShaderModuleUnique(shaderInfo).value;

    return shaderModule;
}

void VulkanRenderer::loadShadersFromDisk()
{
    for (int i = 0; i != NODE_TYPE_MAX; i++)
    {
        NodeType nodeType = static_cast<NodeType>(i);

        auto props = getPropertiesForType(nodeType);

        shaders[nodeType] = createShaderFromFile(props.shaderPath);
    }
}

bool VulkanRenderer::createComputeRenderTarget(uint32_t width, uint32_t height)
{
    computeRenderTarget = std::unique_ptr<CsImage>(
                new CsImage(window,
                            &device,
                            &physicalDevice,
                            width,
                            height,
                            false,
                            "Compute Render Target"));

    emit window->renderTargetHasBeenCreated(width, height);

    currentRenderSize = QSize(width, height);

    return true;
}

bool VulkanRenderer::createImageFromFile(const QString &path, const int colorSpace)
{
    cpuImage = std::unique_ptr<ImageBuf>(new ImageBuf(path.toStdString()));
    bool ok = cpuImage->read(0, 0, 0, 4, true, OIIO::TypeDesc::FLOAT);
    if (!ok)
    {
        CS_LOG_WARNING("There was a problem reading the image from disk.");
        CS_LOG_WARNING(QString::fromStdString(cpuImage->geterror()));
    }
    // Add alpha channel if it doesn't exist
    if (cpuImage->nchannels() == 3)
    {
        int channelorder[] = { 0, 1, 2, -1 };
        float channelvalues[] = { 0 /*ignore*/, 0 /*ignore*/, 0 /*ignore*/, 1.0 };
        std::string channelnames[] = { "R", "G", "B", "A" };

        *cpuImage = OIIO::ImageBufAlgo::channels(*cpuImage, 4, channelorder, channelvalues, channelnames);
    }

    transformColorSpace(colorSpaces.at(colorSpace), "linear", *cpuImage);

    updateVertexData(cpuImage->xend(), cpuImage->yend());

    auto imageSize = QSize(cpuImage->xend(), cpuImage->yend());

    vk::FormatProperties props = physicalDevice.getFormatProperties(globalImageFormat);
    const bool canSampleLinear = (bool)(props.linearTilingFeatures & vk::FormatFeatureFlagBits::eSampledImage);
    const bool canSampleOptimal = (bool)(props.optimalTilingFeatures & vk::FormatFeatureFlagBits::eSampledImage);
    if (!canSampleLinear && !canSampleOptimal) {
        CS_LOG_WARNING("Neither linear nor optimal image sampling is supported for image");
        return false;
    }

    // The image that gets the data from the CPU
    loadImageStaging = std::unique_ptr<CsImage>(
                new CsImage(window,
                            &device,
                            &physicalDevice,
                            cpuImage->xend(),
                            cpuImage->yend(),
                            true,
                            "Load Image Staging"));

    if (!writeLinearImage(
                static_cast<float*>(cpuImage->localpixels()),
                QSize(cpuImage->xend(), cpuImage->yend()),
                loadImageStaging))
    {
        CS_LOG_WARNING("Failed to write linear image");
        return false;
    }
    return true;
}

void VulkanRenderer::transformColorSpace(const QString& from, const QString& to, ImageBuf& image)
{
    parallelApplyColorSpace(
                ocioConfig,
                from,
                to,
                static_cast<float*>(image.localpixels()),
                image.xend(),
                image.yend());
}

void VulkanRenderer::createComputeDescriptors()
{
    // TODO: Clean this up.

    if (!computeDescriptorSetLayout)
    {
        // Define the layout of the input of the shader.
        // 2 images to read, 1 image to write
        std::vector<vk::DescriptorSetLayoutBinding> bindings(4);

        bindings.at(0).binding         = 0;
        bindings.at(0).descriptorType  = vk::DescriptorType::eStorageImage;
        bindings.at(0).descriptorCount = 1;
        bindings.at(0).stageFlags      = vk::ShaderStageFlagBits::eCompute;

        bindings.at(1).binding         = 1;
        bindings.at(1).descriptorType  = vk::DescriptorType::eStorageImage;
        bindings.at(1).descriptorCount = 1;
        bindings.at(1).stageFlags      = vk::ShaderStageFlagBits::eCompute;

        bindings.at(2).binding         = 2;
        bindings.at(2).descriptorType  = vk::DescriptorType::eStorageImage;
        bindings.at(2).descriptorCount = 1;
        bindings.at(2).stageFlags      = vk::ShaderStageFlagBits::eCompute;

        bindings.at(3).binding         = 3;
        bindings.at(3).descriptorType  = vk::DescriptorType::eUniformBuffer;
        bindings.at(3).descriptorCount = 1;
        bindings.at(3).stageFlags      = vk::ShaderStageFlagBits::eCompute;

        vk::DescriptorSetLayoutCreateInfo descSetLayoutCreateInfo(
                    {},
                    4,
                    &bindings.at(0));

        computeDescriptorSetLayout = device.createDescriptorSetLayoutUnique(
                    descSetLayoutCreateInfo).value;
    }

    graphicsDescriptorSet.reserve(2);

    // Descriptor sets
    for (int i = 0; i < concurrentFrameCount; ++i)
    {
        {
            vk::DescriptorSetAllocateInfo descSetAllocInfo(
                        *descriptorPool,
                        1,
                        &(*graphicsDescriptorSetLayout));

            graphicsDescriptorSet.push_back(std::move(device.allocateDescriptorSetsUnique(descSetAllocInfo).value.front()));
        }
    }

    vk::DescriptorSetAllocateInfo descSetAllocInfoCompute(
                *descriptorPool,
                1,
                &(*computeDescriptorSetLayout));

    computeDescriptorSet = std::move(device.allocateDescriptorSetsUnique(descSetAllocInfoCompute).value.front());
}

void VulkanRenderer::updateGraphicsDescriptors(
        const CsImage* const outputImage,
        const CsImage* const upstreamImage)
{
    for (int i = 0; i < concurrentFrameCount; ++i)
    {
        std::vector<vk::WriteDescriptorSet> descWrite(3);
        descWrite.at(0).dstSet = *graphicsDescriptorSet.at(i);
        descWrite.at(0).dstBinding = 0;
        descWrite.at(0).descriptorCount = 1;
        descWrite.at(0).descriptorType = vk::DescriptorType::eUniformBuffer;
        descWrite.at(0).pBufferInfo = &uniformBufferInfo[i];

        vk::DescriptorImageInfo descImageInfo(
                    *sampler,
                    *outputImage->getImageView(),
                    vk::ImageLayout::eShaderReadOnlyOptimal);

        descWrite.at(1).dstSet = *graphicsDescriptorSet.at(i);
        descWrite.at(1).dstBinding = 1;
        descWrite.at(1).descriptorCount = 1;
        descWrite.at(1).descriptorType = vk::DescriptorType::eCombinedImageSampler;
        descWrite.at(1).pImageInfo = &descImageInfo;

        vk::DescriptorImageInfo descImageInfoUpstream(
                    *sampler,
                    *upstreamImage->getImageView(),
                    vk::ImageLayout::eShaderReadOnlyOptimal);

        descWrite.at(2).dstSet = *graphicsDescriptorSet.at(i);
        descWrite.at(2).dstBinding = 2;
        descWrite.at(2).descriptorCount = 1;
        descWrite.at(2).descriptorType = vk::DescriptorType::eCombinedImageSampler;
        descWrite.at(2).pImageInfo = &descImageInfoUpstream;

        device.updateDescriptorSets(descWrite, {});
    }
}

void VulkanRenderer::updateComputeDescriptors(
        const CsImage* const inputImageBack,
        const CsImage* const inputImageFront,
        const CsImage* const outputImage)
{
    auto result = device.waitIdle();
    Q_UNUSED(result);

    vk::DescriptorImageInfo sourceInfoBack(
                *sampler,
                *inputImageBack->getImageView(),
                vk::ImageLayout::eGeneral);

    vk::DescriptorImageInfo sourceInfoFront;
    sourceInfoFront.sampler = *sampler;
    if (inputImageFront)
        sourceInfoFront.imageView = *inputImageFront->getImageView();
    else
        sourceInfoFront.imageView = *inputImageBack->getImageView();
    sourceInfoFront.imageLayout = vk::ImageLayout::eGeneral;

    vk::DescriptorImageInfo destinationInfo(
                {},
                *outputImage->getImageView(),
                vk::ImageLayout::eGeneral);

    vk::DescriptorBufferInfo settingsBufferInfo(
                *settingsBuffer->getBuffer(),
                0,
                VK_WHOLE_SIZE);

    std::vector<vk::WriteDescriptorSet> descWrite(4);

    descWrite.at(0).dstSet                    = *computeDescriptorSet;
    descWrite.at(0).dstBinding                = 0;
    descWrite.at(0).descriptorCount           = 1;
    descWrite.at(0).descriptorType            = vk::DescriptorType::eStorageImage;
    descWrite.at(0).pImageInfo                = &sourceInfoBack;

    descWrite.at(1).dstSet                    = *computeDescriptorSet;
    descWrite.at(1).dstBinding                = 1;
    descWrite.at(1).descriptorCount           = 1;
    descWrite.at(1).descriptorType            = vk::DescriptorType::eStorageImage;
    descWrite.at(1).pImageInfo                = &sourceInfoFront;

    descWrite.at(2).dstSet                    = *computeDescriptorSet;
    descWrite.at(2).dstBinding                = 2;
    descWrite.at(2).descriptorCount           = 1;
    descWrite.at(2).descriptorType            = vk::DescriptorType::eStorageImage;
    descWrite.at(2).pImageInfo                = &destinationInfo;

    descWrite.at(3).dstSet                    = *computeDescriptorSet;
    descWrite.at(3).dstBinding                = 3;
    descWrite.at(3).descriptorCount           = 1;
    descWrite.at(3).descriptorType            = vk::DescriptorType::eUniformBuffer;
    descWrite.at(3).pBufferInfo               = &settingsBufferInfo;

    device.updateDescriptorSets(descWrite, {});
}

void VulkanRenderer::createComputePipelineLayout()
{
    vk::PipelineLayoutCreateInfo pipelineLayoutInfo(
                {},
                1,
                &(*computeDescriptorSetLayout));

    //Create the layout, store it to share between shaders
    computePipelineLayout = device.createPipelineLayoutUnique(pipelineLayoutInfo).value;
}


void VulkanRenderer::createComputePipelines()
{
    for (int i = 0; i != NODE_TYPE_MAX; i++)
    {
        NodeType nodeType = static_cast<NodeType>(i);

        pipelines[nodeType] = createComputePipeline(shaders[nodeType].get());
    }
}

vk::UniquePipeline VulkanRenderer::createComputePipeline(
        const vk::ShaderModule& shaderModule)
{
    vk::PipelineShaderStageCreateInfo computeStage(
                {},
                vk::ShaderStageFlagBits::eCompute,
                shaderModule,
                "main");

    vk::ComputePipelineCreateInfo pipelineInfo(
                {},
                computeStage,
                *computePipelineLayout);

    vk::UniquePipeline pl = device.createComputePipelineUnique(*pipelineCache, pipelineInfo).value;

    return pl;
}

void VulkanRenderer::createQueryPool()
{
    vk::QueryPoolCreateInfo queryPoolInfo(
                {},
                vk::QueryType::eTimestamp,
                2);

    queryPool = device.createQueryPoolUnique(queryPoolInfo).value;
}

bool VulkanRenderer::writeLinearImage(
        float* imgStart,
        QSize imgSize,
        std::unique_ptr<CsImage>& image)
{
    vk::ImageSubresource subres(
                vk::ImageAspectFlagBits::eColor,
                0, // mip level
                0);

    vk::SubresourceLayout layout = device.getImageSubresourceLayout(*image->getImage(), subres);

    float *p;
    vk::Result err = device.mapMemory(*image->getMemory(),
                                      layout.offset,
                                      layout.size,
                                      {},
                                      reinterpret_cast<void **>(&p));
    if (err != vk::Result::eSuccess)
    {
        CS_LOG_WARNING("Failed to map memory for linear image.");
        return false;
    }

    int pad = (layout.rowPitch - imgSize.width() * 16) / 4;

    // TODO: Parallelize this
    float* pixels = imgStart;
    int lineWidth = imgSize.width() * 16; // 4 channels * 4 bytes
    int w = imgSize.width();
    int h = imgSize.height();
    for (int y = 0; y < h; ++y)
    {
        memcpy(p, pixels, lineWidth);
        pixels += w * 4;
        p += w * 4 + pad;
    }

    device.unmapMemory(*image->getMemory());

    return true;
}

void VulkanRenderer::updateVertexData(const int w, const int h)
{
    vertexData[0]  = -0.002 * w;
    vertexData[5]  = -0.002 * w;
    vertexData[10] = 0.002 * w;
    vertexData[15] = 0.002 * w;
    vertexData[1]  = -0.002 * h;
    vertexData[6]  = 0.002 * h;
    vertexData[11] = -0.002 * h;
    vertexData[16] = 0.002 * h;
}

void VulkanRenderer::initSwapChainResources()
{
    // Projection matrix
    projection = window->clipCorrectionMatrix(); // adjust for Vulkan-OpenGL clip space differences
    const QSize sz = window->swapChainImageSize();
    projection.ortho( -sz.width() / scaleXY, sz.width() / scaleXY, -sz.height() / scaleXY, sz.height() / scaleXY, -1.0f, 100.0f);
    projection.scale(500);
}

void VulkanRenderer::setDisplayMode(const DisplayMode mode)
{
    displayMode = mode;
}

bool VulkanRenderer::saveImageToDisk(
        CsImage* const inputImage,
        const QString &path,
        const QMap<std::string, std::string>& attributes,
        const int colorSpace)
{
    bool success = true;

    auto mem = computeCommandBuffer->recordImageSave(
                inputImage);

    computeCommandBuffer->submitImageSave();

    auto result = device.waitIdle();

    float *pInput;
    result = device.mapMemory(
                *mem,
                0,
                VK_WHOLE_SIZE,
                {},
                reinterpret_cast<void **>(&pInput));
    if (result != vk::Result::eSuccess)
        CS_LOG_WARNING("Failed to map memory.");

    int width = inputImage->getWidth();
    int height = inputImage->getHeight();
    int numValues = width * height * 4;

    float* output = new float[numValues];
    float* pOutput = &output[0];

    parallelArrayCopy(pInput, pOutput, width, height);

    OIIO::ImageSpec spec(width, height, 4, OIIO::TypeDesc::FLOAT);
    QMap<std::string, std::string>::const_iterator it;
    for (it = attributes.begin(); it != attributes.end(); ++it)
    {
        spec.attribute(it.key(), it.value());
    }
    std::unique_ptr<ImageBuf> saveImage =
            std::unique_ptr<ImageBuf>(new ImageBuf(spec, output));

    transformColorSpace("linear", colorSpaces.at(colorSpace), *saveImage);

    success = saveImage->write(path.toStdString());

    if (!success)
    {
        CS_LOG_INFO("Problem saving image." + QString::fromStdString(saveImage->geterror()));
    }

    delete[] output;

    device.unmapMemory(*mem);

    return success;
}

void VulkanRenderer::createRenderPass()
{
    vk::CommandBuffer cb = window->currentCommandBuffer();

    const QSize sz = window->swapChainImageSize();

    // Clear background
    vk::ClearDepthStencilValue clearDS = { 1, 0 };
    vk::ClearValue clearValues[2];
    clearValues[0].color = clearColor;
    clearValues[1].depthStencil = clearDS;

    vk::RenderPassBeginInfo rpBeginInfo;
    rpBeginInfo.renderPass = window->defaultRenderPass();
    rpBeginInfo.framebuffer = window->currentFramebuffer();
    rpBeginInfo.renderArea.extent.width = sz.width();
    rpBeginInfo.renderArea.extent.height = sz.height();
    rpBeginInfo.clearValueCount = 2;
    rpBeginInfo.pClearValues = clearValues;

    cb.beginRenderPass(
                rpBeginInfo,
                vk::SubpassContents::eInline);

    // TODO: Can we do this once?
    quint8 *p;
    vk::Result err = device.mapMemory(
                *vertexBufferMemory,
                uniformBufferInfo[window->currentFrame()].offset,
                uniformDataSize,
                {},
                reinterpret_cast<void **>(&p));

    if (err != vk::Result::eSuccess)
    {
        CS_LOG_WARNING("Failed to map memory for vertex buffer.");
    }

    QMatrix4x4 m = projection;

    QMatrix4x4 rotation;
    rotation.setToIdentity();

    QMatrix4x4 translation;
    translation.setToIdentity();
    translation.translate(position_x, position_y, position_z);

    QMatrix4x4 scale;
    scale.setToIdentity();
    scale.scale(scaleXY, scaleXY, scaleXY);

    m = m * translation * scale;

    memcpy(p, m.constData(), 16 * sizeof(float));
    device.unmapMemory(*vertexBufferMemory);

    // Choose to either display RGB or Alpha
    vk::Pipeline* pl;
    if (displayMode == DISPLAY_MODE_ALPHA)
        pl = &graphicsPipelineAlpha.get();
    else
        pl = &graphicsPipelineRGB.get();

    cb.pushConstants(
                *graphicsPipelineLayout,
                vk::ShaderStageFlagBits::eFragment,
                0,
                sizeof(viewerPushConstants),
                viewerPushConstants.data());
    cb.bindPipeline(
                vk::PipelineBindPoint::eGraphics,
                *pl);
    cb.bindDescriptorSets(
                vk::PipelineBindPoint::eGraphics,
                *graphicsPipelineLayout,
                0,
                *graphicsDescriptorSet.at(window->currentFrame()),
                {});

    vk::DeviceSize vbOffset = 0;
    cb.bindVertexBuffers(
                0,
                1,
                &vertexBuffer.get(),
                &vbOffset);

    //negative viewport
    vk::Viewport viewport;
    viewport.x = 0;
    viewport.y = 0;
    viewport.width = sz.width();
    viewport.height = sz.height();
    viewport.minDepth = 0;
    viewport.maxDepth = 1;
    cb.setViewport(
                0,
                1,
                &viewport);

    vk::Rect2D scissor;
    scissor.offset.x = scissor.offset.y = 0;
    scissor.extent.width = viewport.width;
    scissor.extent.height = viewport.height;
    cb.setScissor(
                0,
                1,
                &scissor);

    cb.draw(4, 1, 0, 0);

    cb.endRenderPass();
}

void VulkanRenderer::setViewerPushConstants(const QString &s)
{
    viewerPushConstants = unpackPushConstants(s);
}

void VulkanRenderer::processReadNode(NodeBase *node)
{
    auto parts = node->getAllPropertyValues().split(",");
    int index = parts[parts.size() - 2].toInt();
    if ( index < 0 )
        index = 0;
    QString path = parts[index];
    int colorSpace = parts.last().toInt();

    QFileInfo checkFile(path);

    if(path != "" && checkFile.exists() && checkFile.isFile())
    {
        imagePath = path;

        // Create texture
        if (!createImageFromFile(imagePath, colorSpace))
            CS_LOG_WARNING("Failed to create texture");

        tmpCacheImage = std::unique_ptr<CsImage>(
                    new CsImage(window,
                                &device,
                                &physicalDevice,
                                cpuImage->xend(),
                                cpuImage->yend(),
                                false,
                                "Tmp Cache Image"));

        // Create render target
        if (!createComputeRenderTarget(cpuImage->xend(), cpuImage->yend()))
            CS_LOG_WARNING("Failed to create compute render target.");

        updateComputeDescriptors(tmpCacheImage.get(), nullptr, computeRenderTarget.get());

        computeCommandBuffer->recordImageLoad(
                    loadImageStaging.get(),
                    tmpCacheImage.get(),
                    computeRenderTarget.get(),
                    &pipelines[NODE_TYPE_READ].get());

        computeCommandBuffer->submitImageLoad();

        node->setCachedImage(std::move(computeRenderTarget));

        // Delete the staging image
        auto result = device.waitIdle();
        Q_UNUSED(result);

        loadImageStaging = nullptr;
    }
    else
    {
        node->flushCache();
    }
}

void VulkanRenderer::processNode(
        NodeBase* node,
        CsImage* inputImageBack,
        CsImage* inputImageFront,
        const QSize targetSize)
{
    auto result = device.waitIdle();

    fillSettingsBuffer(node);

    if (!createComputeRenderTarget(targetSize.width(), targetSize.height()))
        CS_LOG_WARNING("Failed to create compute render target.");

    // Tells the shader if we have a mask on the front input
    settingsBuffer->appendValue(0.0);
    if (inputImageFront)
    {
        settingsBuffer->incrementLastValue();
    }

    // TODO: This is a workaround for generative nodes without input
    // but needs to be fixed
    if (!inputImageBack)
    {
        tmpCacheImage = std::unique_ptr<CsImage>(
                    new CsImage(window,
                                &device,
                                &physicalDevice,
                                targetSize.width(),
                                targetSize.height(),
                                false,
                                "Tmp Cache Image"));
        inputImageBack = tmpCacheImage.get();
    }

    auto pipeline = pipelines[node->nodeType].get();

    if (node->nodeType == NODE_TYPE_SHADER || node->nodeType == NODE_TYPE_ISF)
    {
        if (node->getShaderCode().size() != 0)
        {
            shaderUser = createShaderFromCode(node->getShaderCode());

            computePipelineUser = createComputePipeline(shaderUser.get());

            pipeline = computePipelineUser.get();
        }
        else
        {
            pipeline = computePipelineNoop.get();
        }
    }

    int numShaderPasses = getPropertiesForType(node->nodeType).numShaderPasses;
    int currentShaderPass = 1;

    if (numShaderPasses == 1)
    {
        updateComputeDescriptors(inputImageBack, inputImageFront, computeRenderTarget.get());

        computeCommandBuffer->recordGeneric(
                    inputImageBack,
                    inputImageFront,
                    computeRenderTarget.get(),
                    pipeline,
                    numShaderPasses,
                    currentShaderPass);

        computeCommandBuffer->submitGeneric();

        window->requestUpdate();

        result = device.waitIdle();

        node->setCachedImage(std::move(computeRenderTarget));
    }
    else
    {
        for (int i = 1; i <= numShaderPasses; ++i)
        {
            // TODO: Shorten this
            if (currentShaderPass == 1)
            {
                // First pass of multipass shader
                settingsBuffer->appendValue(0.0);

                updateComputeDescriptors(inputImageBack, inputImageFront, computeRenderTarget.get());

                computeCommandBuffer->recordGeneric(
                            inputImageBack,
                            inputImageFront,
                            computeRenderTarget.get(),
                            pipeline,
                            numShaderPasses,
                            currentShaderPass);

                computeCommandBuffer->submitGeneric();
            }
            else if (currentShaderPass <= numShaderPasses)
            {
                // Subsequent passes
                settingsBuffer->incrementLastValue();

                if (!createComputeRenderTarget(targetSize.width(), targetSize.height()))
                    CS_LOG_WARNING("Failed to create compute render target.");

                updateComputeDescriptors(node->getCachedImage(), inputImageFront, computeRenderTarget.get());

                computeCommandBuffer->recordGeneric(
                            node->getCachedImage(),
                            inputImageFront,
                            computeRenderTarget.get(),
                            pipeline,
                            numShaderPasses,
                            currentShaderPass);

                computeCommandBuffer->submitGeneric();
            }
            currentShaderPass++;

            result = device.waitIdle();

            node->setCachedImage(std::move(computeRenderTarget));
        }

        window->requestUpdate();
    }
}

void VulkanRenderer::displayNode(const NodeBase *node)
{
    if(CsImage* image = node->getCachedImage())
    {
        // Execute a NoOp shader on the node
        clearScreen = false;

        updateVertexData(image->getWidth(), image->getHeight());
        createVertexBuffer();

        if (!createComputeRenderTarget(image->getWidth(), image->getHeight()))
            CS_LOG_WARNING("Failed to create compute render target.");

        CsImage* upstreamImage = nullptr;
        if (node->getUpstreamNodeBack())
            upstreamImage = node->getUpstreamNodeBack()->getCachedImage();
        if (!upstreamImage)
            upstreamImage = image;

        updateGraphicsDescriptors(image, upstreamImage);
        updateComputeDescriptors(image, nullptr, computeRenderTarget.get());

        computeCommandBuffer->recordGeneric(
                    image,
                    nullptr,
                    computeRenderTarget.get(),
                    *computePipelineNoop,
                    1,
                    1);

        computeCommandBuffer->submitGeneric();

        window->requestUpdate();
    }
    else
    {
        CS_LOG_INFO("Clearing screen");
        doClearScreen();
    }
}

void VulkanRenderer::doClearScreen()
{
    clearScreen = true;

    window->requestUpdate();
}

void VulkanRenderer::startNextFrame()
{
    if (clearScreen)
    {
        const QSize sz = window->swapChainImageSize();

        // Clear background
        vk::ClearDepthStencilValue clearDS = { 1, 0 };
        vk::ClearValue clearValues[2];
        clearValues[0].color = clearColor;
        clearValues[1].depthStencil = clearDS;

        vk::CommandBuffer cmdBuf = window->currentCommandBuffer();

        vk::RenderPassBeginInfo rpBeginInfo;
        rpBeginInfo.renderPass = window->defaultRenderPass();
        rpBeginInfo.framebuffer = window->currentFramebuffer();
        rpBeginInfo.renderArea.extent.width = sz.width();
        rpBeginInfo.renderArea.extent.height = sz.height();
        rpBeginInfo.clearValueCount = 2;
        rpBeginInfo.pClearValues = clearValues;

        cmdBuf.beginRenderPass(
                    &rpBeginInfo,
                    vk::SubpassContents::eInline);

        cmdBuf.endRenderPass();
    }
    else
    {
        createRenderPass();
    }

    window->frameReady();
}

void VulkanRenderer::logicalDeviceLost()
{
    emit window->deviceLost();
}

void VulkanRenderer::translate(float dx, float dy)
{
    const QSize sz = window->size();

    position_x += 6.0 * dx / sz.width();
    position_y += 2.0 * -dy / sz.height();

    window->requestUpdate();
}

void VulkanRenderer::scale(float s)
{
    scaleXY = s;
    window->requestUpdate();
    emit window->requestZoomTextUpdate(s);
}

void VulkanRenderer::releaseSwapChainResources()
{
    CS_LOG_INFO("Releasing swapchain resources.");
}

void VulkanRenderer::releaseResources()
{
    CS_LOG_INFO("Releasing resources.");
}

void VulkanRenderer::shutdown()
{
    CS_LOG_INFO("Destroying Renderer.");
    auto result = device.waitIdle();


    loadImageStaging = nullptr;
    tmpCacheImage = nullptr;
    computeRenderTarget = nullptr;
    settingsBuffer = nullptr;
    for(auto& pl : pipelines)
        device.destroy(*pl.second);
    device.destroy(*computePipelineNoop);
    device.destroy(*computePipelineUser);
    device.destroy(*graphicsPipelineRGB);
    device.destroy(*graphicsPipelineAlpha);
    device.destroy(*pipelineCache);
    device.destroy(*descriptorPool);
    for(auto& sh : shaders)
        device.destroy(*sh.second);
    device.destroy(*shaderUser);
    device.destroy(*graphicsPipelineLayout);
    device.destroy(*computePipelineLayout);
    device.destroy(*graphicsDescriptorSetLayout);
    device.destroy(*computeDescriptorSetLayout);
    computeCommandBuffer = nullptr;
    device.destroy(*sampler);
    device.free(*vertexBufferMemory);
    device.destroy(*vertexBuffer);

    result = device.waitIdle();
}

VulkanRenderer::~VulkanRenderer()
{

}

} // end namespace Cascade::Renderer
