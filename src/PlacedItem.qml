import QtQuick

// One signature image or text box lying on a page until it is saved into the PDF.
// Position and size live in the `items` model as fractions of the page, so the
// item follows the page when the window is resized.
Item {
    id: root
    objectName: "placed"

    property int pageIndex
    property real pageW
    property real pageH
    property bool editing: false

    readonly property bool isText: model.kind === "text"
    readonly property bool selected: visible && win.selected === index
    // Poppler insets free text by 2pt inside its box; mirror that so saved text does not shift.
    readonly property real pad: isText ? pageW * 2 / 612 : 0

    visible: model.page === pageIndex
    x: model.fx * pageW
    y: model.fy * pageH
    width: isText ? label.implicitWidth + 2 * pad : model.fw * pageW
    height: isText ? label.implicitHeight + 2 * pad : model.fh * pageH

    Component.onCompleted: {
        if (isText && visible && win.selected === index) {
            editing = true;
            label.forceActiveFocus();
            label.selectAll();
        }
    }

    Image {
        anchors.fill: parent
        visible: !root.isText
        // Every page instantiates every item; only the one on its own page loads the file.
        source: root.visible && !root.isText ? "file://" + model.source : ""
        fillMode: Image.Stretch
        smooth: true
        mipmap: true
    }

    TextEdit {
        id: label
        visible: root.isText
        enabled: root.editing
        x: root.pad
        y: root.pad
        text: model.text
        font.family: "Helvetica"
        font.pixelSize: Math.max(4, model.fs * root.pageW)
        color: "black"
        selectByMouse: true
        selectionColor: win.accentColor
        onTextChanged: if (root.visible && text !== model.text) items.setProperty(index, "text", text)
        // Only text boxes size themselves from the editor; an image's hidden editor must not touch fw/fh.
        onImplicitWidthChanged: if (root.isText && root.visible) items.setProperty(index, "fw", (implicitWidth + 2 * root.pad) / root.pageW)
        onImplicitHeightChanged: if (root.isText && root.visible) items.setProperty(index, "fh", (implicitHeight + 2 * root.pad) / root.pageH)
        onActiveFocusChanged: {
            if (activeFocus)
                win.selected = index;
            else
                root.editing = false;
            win.editingText = activeFocus;
        }
        Keys.onEscapePressed: win.contentItem.forceActiveFocus()
    }

    Rectangle {
        anchors.fill: parent
        color: "transparent"
        border.width: root.selected || hover.hovered ? 1 : 0
        border.color: root.selected ? win.accentColor : Qt.rgba(0, 0, 0, 0.35)
    }

    HoverHandler {
        id: hover
        cursorShape: root.editing ? Qt.IBeamCursor : Qt.SizeAllCursor
    }

    TapHandler {
        onTapped: win.selected = index
        onDoubleTapped: {
            if (!root.isText)
                return;
            root.editing = true;
            label.forceActiveFocus();
            label.selectAll();
        }
    }

    DragHandler {
        target: null
        enabled: !root.editing
        grabPermissions: PointerHandler.CanTakeOverFromAnything
        property real startX
        property real startY
        onActiveChanged: {
            if (!active)
                return;
            startX = model.fx;
            startY = model.fy;
            win.selected = index;
        }
        onTranslationChanged: {
            if (!active)
                return;
            items.setProperty(index, "fx", startX + translation.x / root.pageW);
            items.setProperty(index, "fy", startY + translation.y / root.pageH);
        }
    }

    Rectangle {
        id: handle
        objectName: "handle"
        visible: root.selected
        width: win.scaledSize(10)
        height: width
        radius: 2
        x: root.width - width / 2
        y: root.height - height / 2
        color: win.accentColor

        HoverHandler { cursorShape: Qt.SizeFDiagCursor }

        DragHandler {
            target: null
            grabPermissions: PointerHandler.CanTakeOverFromAnything
            property real startFw
            property real startFs
            property real startPx
            onActiveChanged: {
                if (!active)
                    return;
                startFw = model.fw;
                startFs = model.fs;
                startPx = root.width;
            }
            onTranslationChanged: {
                if (!active)
                    return;
                var factor = Math.max(0.05, (startPx + translation.x) / startPx);
                if (root.isText) {
                    items.setProperty(index, "fs", startFs * factor);
                } else {
                    var fw = Math.max(0.02, startFw * factor);
                    items.setProperty(index, "fw", fw);
                    items.setProperty(index, "fh", fw * root.pageW * model.aspect / root.pageH);
                }
            }
        }
    }
}
