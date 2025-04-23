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

#include "vulkanview.h"

#include <QApplication>
#include <QHBoxLayout>
#include <QLoggingCategory>

#include "viewerstatusbar.h"
#include "renderer/vulkanrenderer.h"
#include "log.h"
#include "popupmessages.h"
#include "renderer/renderconfig.h"

namespace Cascade {

VulkanView::VulkanView(ViewerStatusBar* statusBar, QWidget *parent)
    : QWidget(parent)
{
    CS_LOG_INFO("Creating Vulkan instance");

    // Set up validation layers
    mInstance.setLayers(Renderer::instanceLayers);
    mInstance.setExtensions(Renderer::instanceExtensions);


    if (!mInstance.create())
    {
        executeMessageBox(MESSAGEBOX_FAILED_INITIALIZATION);

        CS_LOG_FATAL("Failed to create Vulkan instance. Error code: ");
        CS_LOG_FATAL(QString::number(mInstance.errorCode()));
    }

    // Set up Dynamic Dispatch Loader to use with vulkan.hpp
    vk::detail::DynamicLoader dl;
    PFN_vkGetInstanceProcAddr vkGetInstanceProcAddr =
        dl.getProcAddress<PFN_vkGetInstanceProcAddr>("vkGetInstanceProcAddr");
    VULKAN_HPP_ASSERT(mInstance.vkInstance());
    VULKAN_HPP_ASSERT(vkGetInstanceProcAddr);
    VULKAN_HPP_DEFAULT_DISPATCHER.init(mInstance.vkInstance(), vkGetInstanceProcAddr);

    // Create a VulkanWindow
    vulkanWindow = new VulkanWindow();
    vulkanWindow->setVulkanInstance(&mInstance);

    vulkanWindow->setPreferredColorFormats(QVector<VkFormat>() << VK_FORMAT_R32G32B32A32_SFLOAT);

    // Create Vulkan window container and put in layout
    vulkanWrapper =  QWidget::createWindowContainer(vulkanWindow);
    QVBoxLayout* layout = new QVBoxLayout();
    layout->addWidget(vulkanWrapper);
    layout->addWidget(statusBar);
    layout->setContentsMargins(QMargins(0, 0, 0, 0));
    layout->setSpacing(0);
    this->setLayout(layout);
}

VulkanWindow* VulkanView::getVulkanWindow()
{
    return vulkanWindow;
}

VulkanView::~VulkanView()
{
    delete vulkanWindow;
}

} // namespace Cascade
