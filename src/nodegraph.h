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

#ifndef NODEGRAPH_H
#define NODEGRAPH_H

#include <set>

#include <QObject>
#include <QGraphicsView>

#include "nodedefinitions.h"
#include "nodebase.h"
#include "nodegraphcontextmenu.h"
#include "connection.h"
#include "windowmanager.h"
#include "rendermanager.h"

namespace Cascade {

struct NodePersistentProperties
{
    NodeType nodeType;
    QPoint pos;
    QString uuid;
    QMap<int, QString> inputs;
    QMap<int, QString> properties;
    QString customName;
};

class NodeGraph : public QGraphicsView
{
    Q_OBJECT

friend class NodeBaseTest;

public:
    NodeGraph(QWidget* parent = nullptr);

    void createNode(
            const NodeType type,
            const QPoint pos,
            const bool view = true,
            const QString& customName = "");
    void viewNode(NodeBase* node);

    NodeBase* getViewedNode();
    NodeBase* getSelectedNode();

    float getViewScale() const;

    void getNodeGraphAsJson(QJsonArray& jsonNodeGraph);

    void flushCacheAllNodes();

    QPoint lastMousePos;
    QPoint lastCreatedNodePos = QPoint(29700, 29920);

protected:
    void mousePressEvent(QMouseEvent*) override;
    void mouseMoveEvent(QMouseEvent*) override;
    void mouseReleaseEvent(QMouseEvent*) override;
    void wheelEvent(QWheelEvent*) override;

private:
    void showContextMenu();

    void deleteNode(NodeBase* node);
    void createProject();
    void loadProject(const QJsonArray& jsonNodeGraph);
    void clear();

    QGraphicsItem* getObjectUnderCursor();
    QWidget* getWidgetFromGraphicsItem(QGraphicsItem* item);

    Connection* createOpenConnection(NodeOutput* nodeOut);
    void establishConnection(NodeInput* nodeIn);
    void destroyOpenConnection();
    void deleteConnection(Connection* c);
    void loadConnection(NodeOutput* src, NodeInput* dst);

    void selectNode(NodeBase* node);
    void activateNode(NodeBase* node);
    NodeBase* findNode(const QString& id);

    NodeBase* loadNode(const NodePersistentProperties& p);
    void connectNodeSignals(NodeBase* n);

    QGraphicsScene* scene;
    WindowManager* wManager;
    RenderManager* rManager;
    NodeGraphContextMenu* contextMenu;

    std::vector<NodeBase*> nodes;
    std::vector<Connection*> connections;

    bool leftMouseIsDragging = false;
    bool middleMouseIsDragging = false;

    float viewScale = 1.0f;

    // The selected node
    NodeBase* selectedNode = nullptr;
    // The node with active properties
    NodeBase* activeNode = nullptr;
    // The node that is being displayed
    NodeBase* viewedNode = nullptr;

    Connection* openConnection = nullptr;

    const int viewWidth = 60000;
    const int viewHeight = 60000;

signals:
    void requestNodeDisplay(Cascade::NodeBase* node);
    void requestNodeFileSave(
            Cascade::NodeBase* node,
            const QString& path,
            const QMap<std::string, std::string>& attributes,
            const bool isBatch = false,
            const bool isLast = false);
    void requestClearScreen();
    void requestClearProperties();
    void projectIsDirty();

public slots:
    void handleNodeLeftClicked(Cascade::NodeBase* node);
    void handleNodeDoubleClicked(Cascade::NodeBase* node);
    void handleNodeOutputLeftClicked(Cascade::NodeOutput* nodeOut);
    void handleNodeUpdateRequest(Cascade::NodeBase* node);
    void handleFileSaveRequest(
            Cascade::NodeBase* node,
            const QString& path,
            const QString& fileType,
            const QMap<std::string, std::string>& attributes,
            const bool batchRender);
    void handleConnectedNodeInputClicked(Cascade::Connection* c);

    void handleDeleteKeyPressed();
    void handleCreateStartupProject();
    void handleCreateNewProject();
    void handleLoadProject(const QJsonArray& jsonNodeGraph);
};

} // namespace Cascade

#endif // NODEGRAPH_H
