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

#include "separatorentity.h"
#include "ui_separatorentity.h"

namespace Cascade {

SeparatorEntity::SeparatorEntity(UIElementType et, QWidget *parent) :
    UiEntity(et, parent),
    ui(new Ui::SeparatorEntity)
{
    ui->setupUi(this);
}

QString SeparatorEntity::getValuesAsString()
{
    return "";
}

void SeparatorEntity::loadPropertyValues(const QString &values)
{
    Q_UNUSED(values);
}

SeparatorEntity::~SeparatorEntity()
{
    delete ui;
}

} // namespace Cascade
