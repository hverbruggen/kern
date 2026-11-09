import QtQuick
import QtQuick.Controls

Item {
    id: decorationsRoot
    required property var editor
    required property var presentation
    property int revision: 0
    property int savedAnchor: 0
    property int savedCaret: 0
    signal linkHovered(string link)
    anchors.fill: parent
    HoverHandler {
        onPointChanged: {
            if (decorationsRoot.presentation)
                decorationsRoot.linkHovered(decorationsRoot.presentation.linkAt(decorationsRoot.editor.positionAt(point.position.x, point.position.y)))
        }
        onHoveredChanged: if (!hovered) decorationsRoot.linkHovered("")
    }
    TapHandler {
        acceptedModifiers: Qt.ControlModifier
        onTapped: if (decorationsRoot.presentation) {
            const link = decorationsRoot.presentation.linkAt(decorationsRoot.editor.positionAt(point.position.x, point.position.y))
            if (link) controller.openLink(link)
        }
    }
    Connections {
        target: decorationsRoot.presentation
        function onUpdated() { Qt.callLater(() => decorationsRoot.revision++) }
        function onProjectionStarting() {
            const e = decorationsRoot.editor
            decorationsRoot.savedCaret = e.cursorPosition
            decorationsRoot.savedAnchor = e.cursorPosition === e.selectionStart ? e.selectionEnd : e.selectionStart
        }
        function onProjectionFinished() {
            decorationsRoot.editor.select(decorationsRoot.savedAnchor, decorationsRoot.savedCaret)
        }
    }
    Connections {
        target: decorationsRoot.editor
        function onContentHeightChanged() { decorationsRoot.revision++ }
        function onWidthChanged() { decorationsRoot.revision++ }
    }
    Repeater {
        model: decorationsRoot.presentation ? decorationsRoot.presentation.decorations : []
        delegate: Item {
            id: decoration
            objectName: "source-decoration-" + modelData.kind + "-" + modelData.from
            required property var modelData
            readonly property bool panel: modelData.kind === "background"
            readonly property rect first: { decorationsRoot.revision; return decorationsRoot.editor.positionToRectangle(modelData.from) }
            readonly property rect last: { decorationsRoot.revision; return decorationsRoot.editor.positionToRectangle(modelData.to) }
            x: panel ? 0 : modelData.kind === "displaymath" ? Math.max(0,(decorationsRoot.editor.width-width)/2) : first.x
            y: first.y
            width: panel ? decorationsRoot.editor.width : modelData.width || Math.max(12,last.x-first.x)
            height: panel ? Math.max(first.height,last.y+last.height-first.y) : Math.max(first.height,modelData.height || 0)
            z: panel ? -1 : 1
            Rectangle {
                anchors.fill: parent
                visible: decoration.panel
                color: uiTheme.foreground
                opacity: modelData.quote ? 0.035 : 0.065
                radius: 4
            }
            Rectangle {
                visible: decoration.panel && modelData.quote === true
                width: 3; height: parent.height; color: uiTheme.muted; opacity: 0.5
            }
            Image {
                visible: modelData.image !== undefined
                source: modelData.image || ""
                width: modelData.width || 0; height: modelData.height || 0
                anchors.verticalCenter: parent.verticalCenter
                fillMode: Image.PreserveAspectFit
            }
            Text {
                visible: modelData.kind === "bullet" || modelData.kind === "task"
                anchors.centerIn: parent
                text: modelData.kind === "bullet" ? "•" : modelData.checked ? "☑" : "☐"
                color: uiTheme.foreground
                font.pixelSize: decorationsRoot.editor.font.pixelSize
            }
            MouseArea {
                anchors.fill: parent
                enabled: !decoration.panel
                hoverEnabled: true
                cursorShape: Qt.IBeamCursor
                onClicked: {
                    if (modelData.kind === "task") {
                        decorationsRoot.presentation.activate(modelData.from, modelData.to, "task")
                        decorationsRoot.editor.contentDirty = true
                    } else {
                        decorationsRoot.editor.forceActiveFocus()
                        decorationsRoot.editor.select(modelData.from, modelData.to)
                    }
                }
                ToolTip.visible: containsMouse && modelData.kind === "error"
                ToolTip.text: modelData.message || ""
            }
        }
    }
}
