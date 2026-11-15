import QtQuick
import QtQuick.Window

Window {
    id: root
    required property var node
    title: "Cloakd Render SDK"
    width: node.spec.width
    height: node.spec.height
    x: node.spec.x
    y: node.spec.y
    visible: node.spec.visible
    color: "transparent"
    RenderItem {
        anchors.fill: parent
        node: root.node
    }
}
