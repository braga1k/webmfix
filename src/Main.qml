import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Material
import QtQuick.Layouts

ApplicationWindow {
    id: win
    width: 860
    height: 650
    minimumWidth: 760
    minimumHeight: 580
    visible: true
    title: qsTr("WebM Audio Fix")
    color: "#14171c"
    font.family: "Inter"
    Material.theme: Material.Dark
    Material.accent: theme.accent
    Material.roundedScale: theme.radius
    property bool helpVisible: false
    function requestQuit() { if (backend.busy) closingDialog.open(); else Qt.quit(); }
    onClosing: function(close) { if (backend.busy) { close.accepted = false; closingDialog.open(); } }
    Connections { target: backend; function onMayClose() { Qt.quit(); } }
    Shortcut { sequence: "?"; context: Qt.ApplicationShortcut; onActivated: win.helpVisible = !win.helpVisible }
    Shortcut { sequence: "Q"; context: Qt.ApplicationShortcut; onActivated: win.requestQuit() }
    Shortcut { sequence: "Ctrl+O"; context: Qt.ApplicationShortcut; onActivated: backend.chooseFiles() }
    Shortcut { sequence: "Space"; context: Qt.ApplicationShortcut; onActivated: backend.repairAll() }

    Item {
        anchors.fill: parent
        LayoutMirroring.enabled: Qt.application.layoutDirection === Qt.RightToLeft
        LayoutMirroring.childrenInherit: true
        ColumnLayout {
            anchors.fill: parent
            anchors.margins: 28
            spacing: 18
            RowLayout {
                spacing: 14
                Image { source: "qrc:/webmfix.svg"; Layout.preferredWidth: 44; Layout.preferredHeight: 44 }
                ColumnLayout {
                    spacing: 4
                    Label { text: qsTr("Repair WebM audio"); color: "#f1f2f5"; font.pixelSize: 24; font.weight: Font.DemiBold }
                    Label { text: qsTr("Get your videos ready for the editor."); color: "#a0a6b0"; font.pixelSize: 13 }
                }
                Item { Layout.fillWidth: true }
                ToolButton {
                    text: "?"; font.pixelSize: 18; Accessible.name: qsTr("Keyboard shortcuts")
                    onClicked: win.helpVisible = !win.helpVisible
                    ToolTip.visible: hovered; ToolTip.text: qsTr("Keyboard shortcuts")
                }
            }
            RowLayout {
                spacing: 10
                ActionButton { objectName: "addFiles"; text: qsTr("Add videos"); onClicked: backend.chooseFiles(); enabled: !backend.busy }
                ActionButton { objectName: "addFolder"; text: qsTr("Add folder"); onClicked: backend.chooseFolder(); enabled: !backend.busy }
                CheckBox {
                    text: qsTr("Include subfolders"); checked: backend.recursive; enabled: !backend.busy
                    onToggled: backend.recursive = checked
                    font.pixelSize: 12
                }
                Item { Layout.fillWidth: true }
            }
            Rectangle {
                Layout.fillWidth: true
                Layout.fillHeight: true
                color: "#1b1f26"
                radius: theme.radius
                border.color: dropArea.containsDrag ? theme.accent : "#31363f"
                border.width: dropArea.containsDrag ? 2 : 1
                ColumnLayout {
                    anchors.fill: parent
                    anchors.margins: 16
                    spacing: 10
                    RowLayout {
                        Label {
                            text: qsTr("%n video(s)", "", backend.count)
                            color: "#adb4bf"; font.pixelSize: 12; font.weight: Font.Medium
                        }
                        Item { Layout.fillWidth: true }
                        ToolButton {
                            text: qsTr("Clear list"); font.pixelSize: 12
                            visible: backend.count > 0; enabled: !backend.busy
                            onClicked: backend.clear()
                        }
                    }
                    Item {
                        Layout.fillWidth: true; Layout.fillHeight: true
                        Column {
                            anchors.centerIn: parent
                            width: Math.min(parent.width - 32, 450)
                            spacing: 10
                            visible: backend.count === 0
                            Label { text: "↓"; anchors.horizontalCenter: parent.horizontalCenter; font.pixelSize: 38; color: theme.accent }
                            Label {
                                text: qsTr("Drop your WebM videos here")
                                width: parent.width; horizontalAlignment: Text.AlignHCenter
                                color: "#e3e6ec"; font.pixelSize: 18; font.weight: Font.Medium
                            }
                            Label {
                                text: qsTr("Or add videos above. Each file is checked automatically.")
                                width: parent.width; wrapMode: Text.WordWrap; horizontalAlignment: Text.AlignHCenter
                                color: "#a0a6b0"; font.pixelSize: 13
                            }
                        }
                        ListView {
                            id: fileList
                            objectName: "fileList"
                            anchors.fill: parent
                            visible: backend.count > 0
                            model: backend
                            spacing: 8
                            clip: true
                            ScrollBar.vertical: ScrollBar {}
                            delegate: Rectangle {
                                id: entry
                                required property int index
                                required property string fileName
                                required property string filePath
                                required property int statusCode
                                required property string detail
                                required property string outputPath
                                width: ListView.view.width - 12
                                height: Math.max(68, detailLabel.implicitHeight + 40)
                                radius: theme.radius
                                color: "#222730"
                                property color statusColor: statusCode === 5 ? "#88cba9" : statusCode === 6 ? "#ed9a98" : statusCode === 2 ? theme.accent : "#b7c0ce"
                                RowLayout {
                                    anchors.fill: parent; anchors.margins: 12; spacing: 12
                                    Rectangle { Layout.preferredWidth: 3; Layout.fillHeight: true; color: entry.statusColor; radius: Math.min(4, theme.radius) }
                                    ColumnLayout {
                                        Layout.fillWidth: true; spacing: 3
                                        Label {
                                            text: entry.fileName; Layout.fillWidth: true; elide: Text.ElideMiddle
                                            font.pixelSize: 13; font.weight: Font.Medium; color: "#e9ecf1"
                                            HoverHandler { id: fileHover }
                                            ToolTip.visible: fileHover.hovered; ToolTip.text: entry.filePath
                                        }
                                        Label {
                                            id: detailLabel
                                            text: entry.detail || entry.filePath
                                            Layout.fillWidth: true; color: "#a4adbb"; font.pixelSize: 11
                                            wrapMode: Text.WrapAnywhere; maximumLineCount: 2; elide: Text.ElideRight
                                            HoverHandler { id: detailHover }
                                            ToolTip.visible: detailHover.hovered && entry.detail.length > 0
                                            ToolTip.text: entry.detail
                                        }
                                    }
                                    BusyIndicator { running: entry.statusCode === 1 || entry.statusCode === 4; visible: running; Layout.preferredWidth: 24; Layout.preferredHeight: 24 }
                                    Label {
                                        text: { translator.language; return backend.statusText(entry.statusCode) }
                                        color: entry.statusColor; font.pixelSize: 11; font.weight: Font.Medium
                                    }
                                    ToolButton {
                                        text: entry.statusCode === 5 ? "↗" : "×"
                                        font.pixelSize: 19; enabled: !backend.busy
                                        Accessible.name: entry.statusCode === 5 ? qsTr("Open output folder") : qsTr("Remove video")
                                        onClicked: entry.statusCode === 5 ? backend.openOutput(entry.index) : backend.remove(entry.index)
                                        ToolTip.visible: hovered; ToolTip.text: entry.statusCode === 5 ? qsTr("Open output folder") : qsTr("Remove video")
                                    }
                                }
                            }
                        }
                    }
                }
                DropArea {
                    id: dropArea
                    anchors.fill: parent
                    enabled: !backend.busy
                    onDropped: function(drop) { if (drop.hasUrls) { backend.addUrls(drop.urls); drop.acceptProposedAction(); } }
                }
            }
            RowLayout {
                spacing: 8
                Label { text: qsTr("Save copies:"); color: "#a0a6b0"; font.pixelSize: 12 }
                Label {
                    text: backend.destination || qsTr("Next to the original videos")
                    Layout.fillWidth: true; elide: Text.ElideMiddle; font.pixelSize: 12; color: "#e3e6ec"
                }
                ActionButton { text: qsTr("Change…"); compact: true; enabled: !backend.busy; onClicked: backend.chooseDestination() }
                ToolButton {
                    text: "×"; visible: backend.destination.length > 0; enabled: !backend.busy
                    Accessible.name: qsTr("Reset output folder"); onClicked: backend.destination = ""
                }
            }
            ColumnLayout {
                spacing: 8
                Layout.fillWidth: true
                ProgressBar { from: 0; to: 1; value: backend.progress; visible: backend.busy; Layout.fillWidth: true }
                Label {
                    text: backend.message || qsTr("Your originals stay intact. Copies keep the same quality.")
                    Layout.fillWidth: true; wrapMode: Text.WordWrap; font.pixelSize: 12; color: "#adb6c4"
                }
            }
            RowLayout {
                spacing: 10
                ActionButton {
                    text: backend.busy ? qsTr("Stop after this video") : qsTr("Check again")
                    enabled: backend.count > 0
                    onClicked: backend.busy ? backend.stopAfterCurrent() : backend.checkAll()
                }
                ActionButton { text: qsTr("Open copies"); visible: backend.doneCount > 0; enabled: !backend.busy; onClicked: backend.openOutput() }
                Item { Layout.fillWidth: true }
                ActionButton {
                    objectName: "repairButton"
                    primary: true
                    text: qsTr("Repair %n video(s)", "", backend.readyCount)
                    enabled: backend.readyCount > 0 && !backend.busy
                    onClicked: backend.repairAll()
                }
            }
        }
    }
    Popup {
        id: help
        visible: win.helpVisible
        onClosed: win.helpVisible = false
        anchors.centerIn: Overlay.overlay
        width: 390; padding: 24; modal: true
        Column {
            width: parent.width; spacing: 16
            Label { text: qsTr("Keyboard shortcuts"); font.pixelSize: 19; font.weight: Font.DemiBold }
            Label { text: qsTr("Ctrl+O     Add videos\nSpace      Repair marked videos\n?               Show shortcuts\nQ              Close"); lineHeight: 1.6 }
            Label { text: qsTr("Only the missing audio header problem is repaired."); width: parent.width; wrapMode: Text.WordWrap; color: "#a0a6b0" }
            ActionButton { text: qsTr("Close"); onClicked: win.helpVisible = false }
        }
    }
    Dialog {
        id: closingDialog
        anchors.centerIn: Overlay.overlay
        width: 430
        title: qsTr("A video is being processed")
        modal: true
        ColumnLayout {
            anchors.fill: parent; spacing: 18
            Label { text: qsTr("Finish the current video before closing?"); Layout.fillWidth: true; wrapMode: Text.WordWrap }
            RowLayout {
                Layout.alignment: Qt.AlignRight
                ActionButton { text: qsTr("Keep open"); onClicked: closingDialog.close() }
                ActionButton { text: qsTr("Finish and close"); primary: true; onClicked: { closingDialog.close(); backend.closeAfterCurrent(); } }
            }
        }
    }
}
