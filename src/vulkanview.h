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

#ifndef VULKANVIEW_H
#define VULKANVIEW_H

#include <QWidget>

#include "vulkanwindow.h"

namespace Cascade {

class ViewerStatusBar;

class VulkanView : public QWidget
{
    Q_OBJECT

public:
    explicit VulkanView(ViewerStatusBar* statusBar, QWidget *parent = nullptr);

    VulkanWindow* getVulkanWindow();

    ~VulkanView();

private:
    QWidget* vulkanWrapper;
    VulkanWindow* vulkanWindow;
    QVulkanInstance mInstance;
};

} // namespace Cascade

#endif // VULKANVIEW_H
