import QtQuick
import QtQuick.Controls

Item {
    id: root
    required property var node
    readonly property var spec: root.node.spec
    x: spec.x
    y: spec.y
    width: spec.width
    height: spec.height
    visible: spec.visible
    opacity: spec.opacity
    rotation: spec.rotation
    property real phase: 0
    FrameAnimation {
        running: root.visible && root.opacity > 0 && root.spec.speed > 0 && (root.spec.kind === "graph" || root.spec.shader !== "none")
        onTriggered: root.phase += frameTime * root.spec.speed
    }
    Rectangle {
        anchors.fill: root.spec.kind === "circle" ? undefined : parent
        anchors.centerIn: root.spec.kind === "circle" ? parent : undefined
        width: Math.min(root.width,root.height)
        height: width
        radius: root.spec.kind === "circle" ? Math.min(width,height)/2 : root.spec.radius
        color: root.spec.kind === "line" ? root.spec.color : root.spec.background
        border.color: root.spec.borderColor
        border.width: root.spec.borderWidth
        clip: true
        ShaderEffect {
            anchors.fill: parent
            visible: root.spec.shader !== "none"
            property real time: root.phase
            property real strength: root.spec.strength
            property color tint: root.spec.accent
            fragmentShader: root.node.shaderUrl
            onStatusChanged: root.node.renderError("shader", status === ShaderEffect.Error ? log : "")
        }
        Text {
            anchors.fill: parent
            anchors.margins: Math.min(16, Math.max(0, (root.height-root.spec.fontSize*1.5)/2))
            visible: root.spec.kind === "text"
            text: root.spec.text
            textFormat: Text.PlainText
            color: root.spec.color
            font.pixelSize: root.spec.fontSize
            wrapMode: Text.Wrap
            clip: true
        }
        Loader {
            anchors.fill: parent
            anchors.margins: 16
            active: root.spec.kind === "graph"
            source: root.spec.renderer === "native" ? "NativeGraphView.qml" : "PythonGraphView.qml"
            onLoaded: {
                item.phase = Qt.binding(function() { return root.phase })
                item.amplitude = Qt.binding(function() { return root.spec.amplitude })
                item.samples = Qt.binding(function() { return root.spec.samples })
                item.tint = Qt.binding(function() { return root.spec.accent })
                root.node.renderError("renderer", "")
            }
            onStatusChanged: {
                if (status === Loader.Error) root.node.renderError("renderer", "Renderer plugin could not be loaded")
            }
        }
        BasicElement {
            anchors.fill: parent
            visible: ["button","checkbox","slider","input","progress","image"].indexOf(root.spec.kind) >= 0
            node: root.node
        }
        Loader {
            id: custom
            anchors.fill: parent
            active: root.spec.kind === "custom"
            source: ""
            function reload() {
                if (active) setSource(root.node.customUrl, {node: root.node})
                else source = ""
            }
            onActiveChanged: reload()
            Component.onCompleted: reload()
            Connections {
                target: root.node
                function onChanged() {
                    if (custom.active && custom.source.toString() !== root.node.customUrl) custom.reload()
                }
            }
            onStatusChanged: root.node.renderError("custom", status === Loader.Error ? "Custom QML could not be loaded" : "")
        }
    }
}
