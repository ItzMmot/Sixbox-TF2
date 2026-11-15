pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls

Item {
    id: root
    required property var node
    readonly property var spec: root.node.spec
    Loader {
        anchors.fill: parent
        sourceComponent: {
            switch(root.spec.kind) {
            case "button": return button
            case "checkbox": return checkbox
            case "slider": return slider
            case "input": return input
            case "progress": return progress
            case "image": return image
            default: return empty
            }
        }
    }
    Component { id: empty; Item {} }
    Component {
        id: button
        Button {
            id: control
            objectName: "basicButton"
            text: root.spec.text
            onClicked: root.node.dispatch("click", null)
            palette.buttonText: root.spec.color
            palette.button: root.spec.background
            padding: 4
            contentItem: Text {
                text: control.text
                color: root.spec.color
                font.pixelSize: root.spec.fontSize
                horizontalAlignment: Text.AlignHCenter
                verticalAlignment: Text.AlignVCenter
                elide: Text.ElideRight
            }
            background: Rectangle {
                color: control.down ? "#466755" : control.hovered ? "#314e3e" : root.spec.background
                radius: 8
                border.color: root.spec.accent
                border.width: control.visualFocus ? 2 : 1
            }
        }
    }
    Component {
        id: checkbox
        CheckBox {
            objectName: "basicCheckbox"
            text: root.spec.text
            checked: root.spec.checked
            palette.windowText: root.spec.color
            palette.highlight: root.spec.accent
            font.pixelSize: root.spec.fontSize
            onClicked: root.node.dispatch("change", checked)
        }
    }
    Component {
        id: slider
        Slider {
            objectName: "basicSlider"
            from: root.spec.minimum
            to: root.spec.maximum
            value: root.spec.value
            palette.highlight: root.spec.accent
            onMoved: root.node.dispatch("change", value)
        }
    }
    Component {
        id: input
        TextField {
            objectName: "basicInput"
            text: root.spec.text
            placeholderText: root.spec.placeholder
            color: root.spec.color
            font.pixelSize: root.spec.fontSize
            onTextEdited: root.node.dispatch("change", text)
            onAccepted: root.node.dispatch("submit", text)
            background: Rectangle {
                color: root.spec.background
                border.color: root.spec.accent
                border.width: 1
                radius: 8
            }
        }
    }
    Component {
        id: progress
        ProgressBar {
            objectName: "basicProgress"
            from: root.spec.minimum
            to: root.spec.maximum
            value: root.spec.value
            palette.highlight: root.spec.accent
        }
    }
    Component {
        id: image
        Image {
            objectName: "basicImage"
            source: root.node.imageUrl
            fillMode: Image.PreserveAspectFit
            asynchronous: true
            onStatusChanged: root.node.renderError("image", status === Image.Error ? "Image could not be loaded" : "")
        }
    }
}
