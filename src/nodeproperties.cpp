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

#include "nodeproperties.h"

#include <QLabel>

#include "nodebase.h"
#include "projectmanager.h"
#include "uientities/cssliderboxentity.h"
#include "uientities/fileboxentity.h"
#include "propertiesheading.h"
#include "uientities/colorbuttonentity.h"
#include "uientities/writepropertiesentity.h"
#include "uientities/spinboxentity.h"
#include "uientities/comboboxentity.h"
#include "uientities/channelselectentity.h"
#include "uientities/colorpropertiesentity.h"
#include "uientities/sizeboxentity.h"
#include "uientities/textboxentity.h"
#include "uientities/checkboxentity.h"
#include "uientities/textbrowserentity.h"
#include "uientities/separatorentity.h"
#include "uientities/lineeditentity.h"
#include "uientities/folderboxentity.h"
#include "uientities/uientity.h"
#include "uientities/resizepropertiesentity.h"
#include "uientities/codeeditorentity.h"

namespace Cascade {

NodeProperties::NodeProperties(
        const NodeType t,
        NodeBase* parentNode,
        const NodeInitProperties& initProps,
        QWidget *parent)
        : QWidget(parent),
          nodeType(t),
          parentNode(parentNode)
{
    layout = new QVBoxLayout();
    layout->setAlignment(Qt::AlignTop);
    layout->setContentsMargins(QMargins(0,0,0,0));
    this->setLayout(layout);

    ProjectManager* pm = &ProjectManager::getInstance();
    connect(this, &NodeProperties::projectIsDirty,
            pm, &ProjectManager::handleProjectIsDirty);

    foreach (auto& elem, initProps.uiElements)
    {
        if(elem.first == UI_ELEMENT_TYPE_PROPERTIES_HEADING)
        {
            PropertiesHeading* item = new PropertiesHeading(
                        elem.second.toUpper(),
                        this);
            layout->addWidget(item);
        }
        else if (elem.first == UI_ELEMENT_TYPE_SPINBOX)
        {
            SpinBoxEntity* item =
                    new SpinBoxEntity(
                        UI_ELEMENT_TYPE_SPINBOX,
                        this);
            auto parts = elem.second.split(",");
            item->setName(parts.at(0));
            item->setMinMaxStepValue(
                        parts.at(1).toInt(),
                        parts.at(2).toInt(),
                        parts.at(3).toInt(),
                        parts.at(4).toInt());
            item->selfConnectToValueChanged(this);
            layout->addWidget(item);
            widgets.push_back(item);
        }
        else if (elem.first == UI_ELEMENT_TYPE_FILEBOX)
        {
            FileBoxEntity* item = new FileBoxEntity(
                        UI_ELEMENT_TYPE_FILEBOX,
                        this);
            item->selfConnectToValueChanged(this);
            layout->addWidget(item);
            widgets.push_back(item);
        }
        else if (elem.first == UI_ELEMENT_TYPE_COLOR_BUTTON)
        {
            ColorButtonEntity* item = new ColorButtonEntity(
                        UI_ELEMENT_TYPE_COLOR_BUTTON,
                        this);
            item->selfConnectToValueChanged(this);
            auto parts = elem.second.split(",");
            item->setName(parts.at(0));
            QColor color = QColor(
                        parts.at(1).toFloat() * 255.0,
                        parts.at(2).toFloat() * 255.0,
                        parts.at(3).toFloat() * 255.0,
                        parts.at(4).toFloat() * 255.0);
            item->setColor(color);
            layout->addWidget(item);
            widgets.push_back(item);
        }
        else if (elem.first == UI_ELEMENT_TYPE_WRITE_PROPERTIES)
        {
            WritePropertiesEntity* item = new WritePropertiesEntity(
                        UI_ELEMENT_TYPE_WRITE_PROPERTIES,
                        this);
            item->selfConnectToRequestFileSave(this);
            layout->addWidget(item);
            widgets.push_back(item);
        }
        else if (elem.first == UI_ELEMENT_TYPE_COMBOBOX)
        {
            ComboBoxEntity* item = new ComboBoxEntity(
                        UI_ELEMENT_TYPE_COMBOBOX,
                        this);
            auto parts = elem.second.split(",");
            item->setName(parts[0]);
            std::vector<QString> options;
            for (int i = 1; i < parts.size() - 1; ++i)
            {
                options.push_back(parts[i]);
            }
            item->setOptions(options, parts.last().toInt());
            item->selfConnectToValueChanged(this);
            layout->addWidget(item);
            widgets.push_back(item);
        }
        else if (elem.first == UI_ELEMENT_TYPE_CHANNEL_SELECT)
        {
            ChannelSelectEntity* item = new ChannelSelectEntity(
                        UI_ELEMENT_TYPE_CHANNEL_SELECT,
                        this);
            auto parts = elem.second.split(",");
            if (parts.at(0).toInt() == 1)
            {
                item->hideAlphaChannel();
            }
            item->selfConnectToValueChanged(this);
            layout->addWidget(item);
            widgets.push_back(item);
        }
        else if (elem.first == UI_ELEMENT_TYPE_SLIDER_BOX_DOUBLE)
        {
            CsSliderBoxEntity* item =
                    new CsSliderBoxEntity(
                        UI_ELEMENT_TYPE_SLIDER_BOX_DOUBLE,
                        this);
            auto parts = elem.second.split(",");
            item->setName(parts.at(0));
            item->setMinMaxStepValue(
                        parts.at(1).toDouble(),
                        parts.at(2).toDouble(),
                        parts.at(3).toDouble(),
                        parts.at(4).toDouble());
            item->selfConnectToValueChanged(this);
            layout->addWidget(item);
            widgets.push_back(item);
        }
        else if (elem.first == UI_ELEMENT_TYPE_SLIDER_BOX_INT)
        {
            CsSliderBoxEntity* item =
                    new CsSliderBoxEntity(
                        UI_ELEMENT_TYPE_SLIDER_BOX_INT,
                        this);
            auto parts = elem.second.split(",");
            item->setName(parts.at(0));
            item->setMinMaxStepValue(
                        parts.at(1).toInt(),
                        parts.at(2).toInt(),
                        parts.at(3).toInt(),
                        parts.at(4).toInt());
            item->selfConnectToValueChanged(this);
            layout->addWidget(item);
            widgets.push_back(item);
        }
        else if (elem.first == UI_ELEMENT_TYPE_COLOR_PROPERTIES)
        {
            ColorPropertiesEntity* item = new ColorPropertiesEntity(
                        UI_ELEMENT_TYPE_COLOR_PROPERTIES,
                        this);
            item->selfConnectToValueChanged(this);
            layout->addWidget(item);
            widgets.push_back(item);
        }
        else if (elem.first == UI_ELEMENT_TYPE_SIZEBOX)
        {
            SizeBoxEntity* item = new SizeBoxEntity(
                        UI_ELEMENT_TYPE_SIZEBOX,
                        this);
            item->selfConnectToValueChanged(this);
            layout->addWidget(item);
            widgets.push_back(item);
        }
        else if (elem.first == UI_ELEMENT_TYPE_TEXTBOX)
        {
            TextBoxEntity* item = new TextBoxEntity(
                        UI_ELEMENT_TYPE_TEXTBOX,
                        this);
            item->setText(elem.second);
            layout->addWidget(item);
            widgets.push_back(item);
        }
        else if (elem.first == UI_ELEMENT_TYPE_CHECKBOX)
        {
            CheckBoxEntity* item = new CheckBoxEntity(
                        UI_ELEMENT_TYPE_CHECKBOX,
                        this);
            item->selfConnectToValueChanged(this);
            auto parts = elem.second.split(",");
            item->setName(parts.at(0));
            item->setChecked(parts.at(1).toInt());
            layout->addWidget(item);
            widgets.push_back(item);
        }
        else if (elem.first == UI_ELEMENT_TYPE_TEXTBROWSER)
        {
            TextBrowserEntity* item = new TextBrowserEntity(
                        UI_ELEMENT_TYPE_TEXTBOX,
                        this);
            item->setText(elem.second);
            layout->addWidget(item);
            widgets.push_back(item);
        }
        else if (elem.first == UI_ELEMENT_TYPE_SEPARATOR)
        {
            SeparatorEntity* item = new SeparatorEntity(
                        UI_ELEMENT_TYPE_SEPARATOR,
                        this);
            layout->addWidget(item);
            widgets.push_back(item);
        }
        else if (elem.first == UI_ELEMENT_TYPE_LINEEDIT)
        {
            LineEditEntity* item = new LineEditEntity(
                        UI_ELEMENT_TYPE_LINEEDIT,
                        this);
            auto parts = elem.second.split(",");
            item->setName(parts[0]);
            item->setText(parts[1]);
            layout->addWidget(item);
            widgets.push_back(item);
        }
        else if (elem.first == UI_ELEMENT_TYPE_FOLDERBOX)
        {
            FolderBoxEntity* item = new FolderBoxEntity(
                        UI_ELEMENT_TYPE_FOLDERBOX,
                        this);
            item->setName(elem.second);
            item->selfConnectToValueChanged(this);
            layout->addWidget(item);
            widgets.push_back(item);
        }
        else if (elem.first == UI_ELEMENT_TYPE_RESIZE_PROPERTIES)
        {
            ResizePropertiesEntity* item = new ResizePropertiesEntity(
                        UI_ELEMENT_TYPE_SIZEBOX,
                        this);
            item->selfConnectToValueChanged(this);
            item->setParentNode(parentNode);
            layout->addWidget(item);
            widgets.push_back(item);

            connect(parentNode, &NodeBase::nodeRequestUpdate,
                    item, &ResizePropertiesEntity::handleNodeRequestUpdate);
        }
        else if (elem.first == UI_ELEMENT_TYPE_CODE_EDITOR)
        {
            CodeEditorEntity* item = new CodeEditorEntity(
                        UI_ELEMENT_TYPE_CODE_EDITOR,
                        this);
            item->selfConnectToValueChanged(this);
            item->setParentNode(parentNode);
            layout->addWidget(item);
            widgets.push_back(item);
        }
    }
}

void NodeProperties::loadNodePropertyValues(const QMap<int, QString> &values)
{
    for (int i = 0; i < values.size(); i++)
    {
        widgets[i]->loadPropertyValues(values[i]);
    }
}

void NodeProperties::handleSomeValueChanged()
{
    parentNode->requestUpdate();

    emit projectIsDirty();
}

void NodeProperties::handleFileSaveRequest(
        const QString& path,
        const QString& fileType,
        const QMap<std::string, std::string>& attributes,
        const bool batchRender)
{
    emit parentNode->nodeRequestFileSave(parentNode, path, fileType, attributes, batchRender);
}

int NodeProperties::getNumImages()
{
    auto entity = static_cast<FileBoxEntity*>(widgets.at(0));

    return entity->getNumImages();
}

void NodeProperties::switchToFirstImage()
{
    auto entity = static_cast<FileBoxEntity*>(widgets.at(0));

    entity->switchToFirstImage();
}

void NodeProperties::switchToNextImage()
{
    auto entity = static_cast<FileBoxEntity*>(widgets.at(0));

    entity->switchToNextImage();
}

} // namespace Cascade
