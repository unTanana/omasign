import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Material
import QtQuick.Dialogs as Dialogs
import QtQuick.Layouts
import QtQuick.Window

ApplicationWindow {
    id: win
    width: 1100
    height: 900
    minimumWidth: 640
    minimumHeight: 480
    visible: true
    title: (items.count > 0 ? "* " : "") + backend.fileName + (backend.pageCount > 0 ? " - Omasign" : "")

    readonly property bool darkMode: backend.darkMode
    readonly property color pageColor: backend.themeBackground
    readonly property color textColor: backend.themeForeground
    readonly property color accentColor: backend.themeAccent
    readonly property color mutedColor: darkMode ? "#909191" : "#aeb1b5"
    readonly property real textScale: backend.textScale
    readonly property int toolbarHeight: scaledSize(44)
    readonly property int pageGap: scaledSize(24)
    readonly property real pageWidth: Math.max(200, Math.min(pages.width - 2 * pageGap, scaledSize(900)))
    property int selected: -1
    property bool editingText: false
    property string statusText: ""
    property string pendingAction: ""
    property url pendingUrl
    property bool closeConfirmed: false

    Material.theme: darkMode ? Material.Dark : Material.Light
    Material.accent: accentColor
    Material.foreground: textColor
    Material.background: pageColor
    color: pageColor

    ListModel { id: items }
    readonly property alias itemModel: items

    function scaledSize(pixels) {
        return Math.max(1, Math.round(pixels * win.textScale));
    }

    // `revision` is unused on purpose: naming it makes the binding re-evaluate after a save.
    function pageAspect(index, revision) {
        var size = backend.pageSize(index);
        return size.height / size.width;
    }

    function visiblePage() {
        if (backend.pageCount === 0)
            return 0;
        var index = pages.indexAt(pages.width / 2, pages.contentY + pages.height / 2);
        return index < 0 ? Math.max(0, pages.indexAt(pages.width / 2, pages.contentY + 1)) : index;
    }

    // Vertical centre of the part of `page` that is on screen, as a page fraction,
    // so a new item lands in view rather than at the page's middle.
    function visibleCenterY(page, itemHeight) {
        var delegate = pages.itemAtIndex(page);
        var center = 0.5;
        if (delegate && delegate.pageH > 0)
            center = (pages.contentY + pages.height / 2 - delegate.y) / delegate.pageH;
        return Math.max(0, Math.min(1 - itemHeight, center - itemHeight / 2));
    }

    function addSignature(path) {
        if (backend.pageCount === 0)
            return;
        var page = visiblePage();
        var aspect = backend.imageAspect(path);
        var fw = 0.3;
        var fh = fw * aspect / pageAspect(page, backend.revision);
        selected = items.count; // before append, so the new item sees itself selected on creation
        items.append({ page: page, kind: "image", source: path, text: "", aspect: aspect,
                       fx: 0.5 - fw / 2, fy: visibleCenterY(page, fh), fw: fw, fh: fh, fs: 0 });
    }

    function addText() {
        if (backend.pageCount === 0)
            return;
        var page = visiblePage();
        selected = items.count;
        items.append({ page: page, kind: "text", source: "", text: "Text", aspect: 0,
                       fx: 0.4, fy: visibleCenterY(page, 0.03), fw: 0.2, fh: 0.03, fs: 0.02 });
    }

    function removeSelected() {
        if (selected < 0)
            return;
        items.remove(selected);
        selected = -1;
    }

    function itemList() {
        var list = [];
        for (var i = 0; i < items.count; ++i) {
            var o = items.get(i);
            list.push({ page: o.page, kind: o.kind, source: o.source, text: o.text,
                        fx: o.fx, fy: o.fy, fw: o.fw, fh: o.fh, fs: o.fs });
        }
        return list;
    }

    function save() {
        if (backend.pageCount === 0)
            return false;
        if (!backend.save(itemList()))
            return false;
        items.clear();
        selected = -1;
        return true;
    }

    function openFile(url) {
        if (!backend.open(url))
            return;
        items.clear();
        selected = -1;
        pages.contentY = -pages.topMargin;
    }

    function requestOpen(url) {
        if (items.count === 0) {
            openFile(url);
            return;
        }
        pendingUrl = url;
        pendingAction = "open";
        unsavedDialog.open();
    }

    function completePending() {
        var action = pendingAction;
        pendingAction = "";
        if (action === "open") {
            openFile(pendingUrl);
        } else if (action === "close") {
            closeConfirmed = true;
            close();
        }
    }

    onClosing: function(close) {
        if (closeConfirmed || items.count === 0)
            return;
        close.accepted = false;
        pendingAction = "close";
        unsavedDialog.open();
    }

    Connections {
        target: backend
        function onStatusChanged() {
            win.statusText = backend.status;
            statusTimer.restart();
        }
    }

    Timer {
        id: statusTimer
        interval: 4000
        onTriggered: win.statusText = ""
    }

    Shortcut { sequence: "Ctrl+O"; onActivated: openDialog.open() }
    Shortcut { sequence: "Ctrl+S"; onActivated: win.save() }
    Shortcut { sequence: "Ctrl+Shift+S"; onActivated: saveDialog.open() }
    Shortcut { sequence: "Ctrl+T"; onActivated: win.addText() }
    Shortcut { sequence: "Ctrl+Q"; onActivated: win.close() }
    Shortcut {
        sequences: ["Delete", "Backspace"]
        enabled: win.selected >= 0 && !win.editingText
        onActivated: win.removeSelected()
    }
    Shortcut {
        sequence: "Escape"
        enabled: !win.editingText
        onActivated: {
            win.selected = -1;
            win.contentItem.forceActiveFocus();
        }
    }
    Shortcut { sequence: "PgDown"; enabled: !win.editingText; onActivated: pages.scrollBy(pages.height * 0.9) }
    Shortcut { sequence: "PgUp"; enabled: !win.editingText; onActivated: pages.scrollBy(-pages.height * 0.9) }
    Shortcut { sequence: "Home"; enabled: !win.editingText; onActivated: pages.contentY = -pages.topMargin }
    Shortcut { sequence: "End"; enabled: !win.editingText; onActivated: pages.scrollBy(pages.contentHeight) }

    header: Rectangle {
        height: win.toolbarHeight
        color: win.pageColor

        Rectangle {
            anchors.bottom: parent.bottom
            width: parent.width
            height: 1
            color: Qt.rgba(win.textColor.r, win.textColor.g, win.textColor.b, 0.12)
        }

        RowLayout {
            anchors.fill: parent
            anchors.leftMargin: win.scaledSize(8)
            anchors.rightMargin: win.scaledSize(8)
            spacing: win.scaledSize(4)

            ToolButton {
                focusPolicy: Qt.NoFocus
                text: "Open"
                onClicked: openDialog.open()
            }
            Label {
                Layout.fillWidth: true
                horizontalAlignment: Text.AlignHCenter
                elide: Text.ElideMiddle
                text: win.statusText.length > 0 ? win.statusText : (backend.pageCount > 0 ? backend.fileName : "")
                color: win.mutedColor
            }
            ToolButton {
                objectName: "textButton"
                focusPolicy: Qt.NoFocus
                text: "Text"
                enabled: backend.pageCount > 0
                onClicked: win.addText()
            }
            ToolButton {
                id: signButton
                objectName: "signButton"
                focusPolicy: Qt.NoFocus
                text: "Sign ▾"
                enabled: backend.pageCount > 0
                onClicked: signMenu.open()
            }
            ToolButton {
                objectName: "saveButton"
                focusPolicy: Qt.NoFocus
                text: "Save"
                enabled: items.count > 0
                onClicked: win.save()
            }
        }
    }

    ListView {
        id: pages
        objectName: "pages"
        anchors.fill: parent
        model: backend.pageCount
        clip: true
        topMargin: win.pageGap
        bottomMargin: win.pageGap
        cacheBuffer: height
        boundsBehavior: Flickable.StopAtBounds
        ScrollBar.vertical: ScrollBar {}

        function scrollBy(delta) {
            var maxY = Math.max(-topMargin, contentHeight - height + bottomMargin);
            contentY = Math.max(-topMargin, Math.min(maxY, contentY + delta));
        }

        delegate: Item {
            id: pageItem
            required property int index
            readonly property real pageW: win.pageWidth
            readonly property real pageH: pageW * win.pageAspect(index, backend.revision)
            width: pages.width
            height: pageH + win.pageGap

            Rectangle {
                id: paper
                x: Math.round((parent.width - pageItem.pageW) / 2)
                width: pageItem.pageW
                height: pageItem.pageH
                color: "white"

                Image {
                    anchors.fill: parent
                    // Render width is rounded up to 64px steps so a window resize does not
                    // re-render the page on every frame.
                    readonly property int renderWidth: Math.ceil(paper.width * Screen.devicePixelRatio / 64) * 64
                    source: "image://pdf/" + pageItem.index + "/" + backend.revision
                    sourceSize: Qt.size(renderWidth, paper.width > 0 ? Math.round(renderWidth * paper.height / paper.width) : renderWidth)
                    asynchronous: true
                    smooth: true
                }

                TapHandler {
                    // Taps on a placed item reach this handler too; only bare paper deselects.
                    onTapped: function(eventPoint) {
                        var hit = paper.childAt(eventPoint.position.x, eventPoint.position.y);
                        if (hit && hit.objectName === "placed")
                            return;
                        win.selected = -1;
                        win.contentItem.forceActiveFocus();
                    }
                }

                Repeater {
                    model: items
                    delegate: PlacedItem {
                        pageIndex: pageItem.index
                        pageW: paper.width
                        pageH: paper.height
                    }
                }
            }
        }
    }

    Column {
        anchors.centerIn: parent
        visible: backend.pageCount === 0
        spacing: win.scaledSize(16)

        Label {
            anchors.horizontalCenter: parent.horizontalCenter
            text: "Open a PDF to sign it"
            color: win.mutedColor
            font.pixelSize: win.scaledSize(18)
        }
        Button {
            anchors.horizontalCenter: parent.horizontalCenter
            text: "Open…"
            onClicked: openDialog.open()
        }
        Label {
            anchors.horizontalCenter: parent.horizontalCenter
            text: "or drop it here"
            color: win.mutedColor
        }
    }

    DropArea {
        anchors.fill: parent
        onDropped: function(drop) {
            if (drop.hasUrls)
                win.requestOpen(drop.urls[0]);
        }
    }

    Popup {
        id: signMenu
        objectName: "signMenu"
        parent: signButton
        x: signButton.width - width
        y: signButton.height
        padding: win.scaledSize(6)
        background: Rectangle {
            color: win.pageColor
            radius: win.scaledSize(6)
            border.width: 1
            border.color: Qt.rgba(win.textColor.r, win.textColor.g, win.textColor.b, 0.2)
        }

        contentItem: Column {
            width: win.scaledSize(268)
            spacing: win.scaledSize(2)

            Repeater {
                model: backend.signatures
                delegate: Row {
                    required property string modelData
                    spacing: win.scaledSize(4)

                    Rectangle {
                        objectName: "signatureCard"
                        width: win.scaledSize(220)
                        height: win.scaledSize(64)
                        radius: 4
                        color: "white"
                        border.width: 1
                        border.color: cardHover.hovered ? win.accentColor : Qt.rgba(0, 0, 0, 0.15)

                        Image {
                            anchors.fill: parent
                            anchors.margins: win.scaledSize(6)
                            source: "file://" + modelData
                            fillMode: Image.PreserveAspectFit
                            sourceSize.height: 128
                            smooth: true
                        }
                        HoverHandler { id: cardHover; cursorShape: Qt.PointingHandCursor }
                        TapHandler {
                            onTapped: {
                                signMenu.close();
                                win.addSignature(modelData);
                            }
                        }
                    }
                    ToolButton {
                        text: "×"
                        focusPolicy: Qt.NoFocus
                        width: win.scaledSize(44)
                        height: win.scaledSize(64)
                        onClicked: backend.removeSignature(modelData)
                    }
                }
            }
            MenuSeparator { width: parent.width; visible: backend.signatures.length > 0 }
            ItemDelegate {
                objectName: "drawEntry"
                width: parent.width
                text: "Draw signature…"
                onClicked: {
                    signMenu.close();
                    padDialog.open();
                }
            }
            ItemDelegate {
                width: parent.width
                text: "Import image…"
                onClicked: {
                    signMenu.close();
                    importDialog.open();
                }
            }
        }
    }

    Dialog {
        id: padDialog
        objectName: "padDialog"
        modal: true
        anchors.centerIn: parent
        title: "Draw your signature"
        padding: win.scaledSize(16)
        background: Rectangle {
            color: win.pageColor
            radius: win.scaledSize(6)
            border.width: 1
            border.color: Qt.rgba(win.textColor.r, win.textColor.g, win.textColor.b, 0.2)
        }
        onOpened: pad.clear()

        contentItem: Column {
            spacing: win.scaledSize(12)

            Rectangle {
                id: paperPad
                width: win.scaledSize(640)
                height: win.scaledSize(220)
                color: "white"
                radius: 6

                Rectangle {
                    x: win.scaledSize(24)
                    width: parent.width - 2 * x
                    y: Math.round(parent.height * 0.72)
                    height: 1
                    color: "#cccccc"
                }

                Canvas {
                    id: pad
                    objectName: "pad"
                    // Draw at physical resolution so the stored PNG is crisp on hidpi screens.
                    readonly property real dpr: Screen.devicePixelRatio
                    property var strokes: []
                    width: paperPad.width * dpr
                    height: paperPad.height * dpr
                    transformOrigin: Item.TopLeft
                    scale: 1 / dpr

                    function clear() {
                        strokes = [];
                        requestPaint();
                    }

                    onPaint: {
                        var ctx = getContext("2d");
                        ctx.setTransform(dpr, 0, 0, dpr, 0, 0);
                        ctx.clearRect(0, 0, width, height);
                        ctx.strokeStyle = "#111111";
                        ctx.fillStyle = "#111111";
                        ctx.lineWidth = 2.6;
                        ctx.lineCap = "round";
                        ctx.lineJoin = "round";
                        for (var s = 0; s < strokes.length; ++s) {
                            var stroke = strokes[s];
                            if (stroke.length === 1) {
                                ctx.beginPath();
                                ctx.arc(stroke[0].x, stroke[0].y, 1.3, 0, 2 * Math.PI);
                                ctx.fill();
                                continue;
                            }
                            ctx.beginPath();
                            ctx.moveTo(stroke[0].x, stroke[0].y);
                            for (var i = 1; i < stroke.length; ++i)
                                ctx.lineTo(stroke[i].x, stroke[i].y);
                            ctx.stroke();
                        }
                    }

                    MouseArea {
                        anchors.fill: parent
                        onPressed: function(mouse) {
                            // Reassign rather than push so bindings on `strokes` re-evaluate.
                            pad.strokes = pad.strokes.concat([[Qt.point(mouse.x / pad.dpr, mouse.y / pad.dpr)]]);
                            pad.requestPaint();
                        }
                        onPositionChanged: function(mouse) {
                            if (!pressed)
                                return;
                            pad.strokes[pad.strokes.length - 1].push(Qt.point(mouse.x / pad.dpr, mouse.y / pad.dpr));
                            pad.requestPaint();
                        }
                    }
                }
            }

            Row {
                anchors.right: parent.right
                spacing: win.scaledSize(8)

                Button {
                    text: "Clear"
                    flat: true
                    onClicked: pad.clear()
                }
                Button {
                    text: "Cancel"
                    flat: true
                    onClicked: padDialog.close()
                }
                Button {
                    objectName: "padDone"
                    text: "Done"
                    highlighted: true
                    enabled: pad.strokes.length > 0
                    onClicked: pad.grabToImage(function(result) {
                        if (!backend.addSignatureImage(result.image))
                            return;
                        padDialog.close();
                        win.addSignature(backend.signatures[backend.signatures.length - 1]);
                    })
                }
            }
        }
    }

    Dialog {
        id: unsavedDialog
        modal: true
        anchors.centerIn: parent
        title: "Save changes to " + backend.fileName + "?"
        background: Rectangle {
            color: win.pageColor
            radius: win.scaledSize(6)
            border.width: 1
            border.color: Qt.rgba(win.textColor.r, win.textColor.g, win.textColor.b, 0.2)
        }
        onAccepted: if (win.save()) win.completePending()
        onDiscarded: {
            items.clear();
            win.selected = -1;
            close();
            win.completePending();
        }
        onRejected: win.pendingAction = ""

        footer: DialogButtonBox {
            Button {
                text: "Don't Save"
                flat: true
                DialogButtonBox.buttonRole: DialogButtonBox.DestructiveRole
            }
            Button {
                text: "Cancel"
                flat: true
                DialogButtonBox.buttonRole: DialogButtonBox.RejectRole
            }
            Button {
                text: "Save"
                highlighted: true
                DialogButtonBox.buttonRole: DialogButtonBox.AcceptRole
            }
        }
    }

    Dialogs.FileDialog {
        id: openDialog
        title: "Open PDF"
        nameFilters: ["PDF documents (*.pdf)"]
        onAccepted: win.requestOpen(selectedFile)
    }

    Dialogs.FileDialog {
        id: importDialog
        title: "Import signature image"
        nameFilters: ["Images (*.png *.jpg *.jpeg *.webp *.bmp)"]
        onAccepted: {
            if (backend.addSignature(selectedFile))
                win.addSignature(backend.signatures[backend.signatures.length - 1]);
        }
    }

    Dialogs.FileDialog {
        id: saveDialog
        title: "Save PDF as"
        fileMode: Dialogs.FileDialog.SaveFile
        defaultSuffix: "pdf"
        nameFilters: ["PDF documents (*.pdf)"]
        currentFile: backend.fileUrl
        onAccepted: {
            if (!backend.saveAs(selectedFile, win.itemList()))
                return;
            items.clear();
            win.selected = -1;
        }
    }
}
