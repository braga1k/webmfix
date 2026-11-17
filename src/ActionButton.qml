import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Material

Button {
    id: control
    property bool primary: false
    property bool compact: false
    implicitHeight: compact ? 34 : 42
    implicitWidth: contentItem.implicitWidth + (compact ? 24 : 32)
    leftPadding: compact ? 12 : 16
    rightPadding: leftPadding
    font.pixelSize: 13
    font.weight: primary ? Font.DemiBold : Font.Medium
    Material.roundedScale: theme.radius
    contentItem: Text {
        text: control.text
        font: control.font
        horizontalAlignment: Text.AlignHCenter
        verticalAlignment: Text.AlignVCenter
        color: !control.enabled ? "#646972" : control.primary ? theme.accentForeground : "#e2e4e9"
    }
    background: Rectangle {
        radius: theme.radius
        color: !control.enabled ? "#24272d" : control.primary ? (control.down ? Qt.darker(theme.accent, 1.15) : theme.accent)
            : control.hovered ? "#30343b" : "#24282f"
        border.width: control.activeFocus ? 2 : control.primary ? 0 : 1
        border.color: control.activeFocus ? theme.accent : "#383d45"
    }
}
