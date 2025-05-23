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

#ifndef UIENTITY_H
#define UIENTITY_H

#include <QObject>
#include <QWidget>

#include "../nodedefinitions.h"
#include "../nodeproperties.h"
#include "../log.h"

namespace Cascade {

class UiEntity : public QWidget
{
    Q_OBJECT

public:
    explicit UiEntity(UIElementType et, QWidget *parent = nullptr);

    virtual QString getValuesAsString() = 0;

    virtual void loadPropertyValues(const QString& values) = 0;

    virtual const QString name();

    const UIElementType elementType;
};

} // namespace Cascade

#endif // UIENTITY_H
