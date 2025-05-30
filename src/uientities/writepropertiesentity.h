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

#ifndef WRITEPROPERTIESENTITY_H
#define WRITEPROPERTIESENTITY_H

#include <QWidget>

#include "uientity.h"
#include "cssliderboxentity.h"

namespace Ui {
class WritePropertiesEntity;
}

namespace Cascade {

class NodeProperties;

class WritePropertiesEntity : public UiEntity
{
    Q_OBJECT

public:
    explicit WritePropertiesEntity(UIElementType et, QWidget *parent = nullptr);

    void selfConnectToRequestFileSave(NodeProperties* p);

    QString getValuesAsString() override;

    void loadPropertyValues(const QString& values) override;

    ~WritePropertiesEntity();

private:
    void setFileName(const QString& f);
    void setFolder(const QString& f);
    void updateFileNameLabel();
    void updateAttributes();
    void hideAllAttributeElements();

    Ui::WritePropertiesEntity *ui;

    CsSliderBoxEntity* jpegCompressionSlider;

    QString fileName = "unnamed";
    QString folder = "";
    QStringList filetypes = { "jpg", "jp2", "png", "tga", "tif", "exr" };
    QMap<std::string, std::string> attributes;

signals:
    void requestFileSave(
            const QString& path,
            const QString& fileType,
            const QMap<std::string, std::string>& attributes,
            const bool batchRender);

public slots:
    void handleFileNametextChanged();
    void handleSetFolderButtonClicked();
    void handleSaveFileButtonClicked();
    void handleFileTypeChanged();
};

} // namespace Cascade

#endif // WRITEPROPERTIESENTITY_H
