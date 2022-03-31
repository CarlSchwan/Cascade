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

#include "channelselectentity.h"
#include "ui_channelselectentity.h"

namespace Cascade {

ChannelSelectEntity::ChannelSelectEntity(UIElementType et, QWidget *parent)
    : UiEntity(et, parent),
    ui(new Ui::ChannelSelectEntity)
{
    ui->setupUi(this);

    connect(ui->redBox, &QCheckBox::toggled,
            this, &ChannelSelectEntity::valueChanged);
    connect(ui->greenBox, &QCheckBox::toggled,
            this, &ChannelSelectEntity::valueChanged);
    connect(ui->blueBox, &QCheckBox::toggled,
            this, &ChannelSelectEntity::valueChanged);
    connect(ui->alphaBox, &QCheckBox::toggled,
            this, &ChannelSelectEntity::valueChanged);
}

void ChannelSelectEntity::hideAlphaChannel()
{
    ui->alphaBox->setHidden(true);
}

void ChannelSelectEntity::selfConnectToValueChanged(NodeProperties *p)
{
    connect(this, &ChannelSelectEntity::valueChanged,
            p, [p]{p->handleSomeValueChanged();});
}

QString ChannelSelectEntity::getValuesAsString()
{
    auto s = QString::number(ui->redBox->isChecked()) + "," +
             QString::number(ui->greenBox->isChecked()) + "," +
             QString::number(ui->blueBox->isChecked()) + "," +
             QString::number(ui->alphaBox->isChecked());
    return s;
}

void ChannelSelectEntity::loadPropertyValues(const QString &values)
{
    auto split = values.split(",");
    std::vector<QCheckBox*> boxes = {
        ui->redBox, ui->greenBox, ui->blueBox, ui->alphaBox
    };
    for (int i = 0; i < split.size(); i++)
    {
        split[i].toInt() == 1 ?
                    boxes[i]->setCheckState(Qt::CheckState::Checked) :
                    boxes[i]->setCheckState(Qt::CheckState::Unchecked);
    }
}

ChannelSelectEntity::~ChannelSelectEntity()
{
    delete ui;
}

} // namespace Cascade
