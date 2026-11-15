import QtQuick
import LocalOverlay 1.0
Item {
    required property var node
    FrameCanvas {
        anchors.fill: parent
        frame: parent.node.spec.customProperties.frame || ({})
    }
}
