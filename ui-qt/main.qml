import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Qt.labs.folderlistmodel

ApplicationWindow {
    id: root
    width: 1120
    height: 760
    visible: true
    title: controller.title
    color: uiTheme.background
    font.family: uiTheme.monoFont
    font.pixelSize: 11

    palette.window: uiTheme.background
    palette.base: uiTheme.background
    palette.alternateBase: uiTheme.surface
    palette.button: uiTheme.surface
    palette.light: uiTheme.surface
    palette.midlight: uiTheme.surface
    palette.mid: uiTheme.surfaceStrong
    palette.dark: uiTheme.surfaceStrong
    palette.shadow: uiTheme.foreground
    palette.text: uiTheme.foreground
    palette.windowText: uiTheme.foreground
    palette.buttonText: uiTheme.foreground
    palette.brightText: uiTheme.foreground
    palette.placeholderText: uiTheme.muted
    palette.highlight: uiTheme.selection
    palette.highlightedText: uiTheme.foreground
    palette.link: uiTheme.accent
    palette.linkVisited: uiTheme.accent
    palette.toolTipBase: uiTheme.surface
    palette.toolTipText: uiTheme.foreground
    palette.disabled.text: uiTheme.muted
    palette.disabled.windowText: uiTheme.muted
    palette.disabled.buttonText: uiTheme.muted


    property bool sourceMode: false
    // A file opened without a project (for example from the file manager) starts with the
    // sidebar collapsed.
    property bool sidebarOpen: !startWithSidebarCollapsed
    readonly property string currentDocumentPath: {
        if (controller.unsupportedFilePath) return controller.unsupportedFilePath
        const tab = controller.tabs.find(t => t.active)
        return tab ? tab.path : ""
    }
    readonly property bool canInsertMarkup: !controller.unsupportedFileMessage && controller.hasDocument && (controller.markdown
        || (controller.blocks.length > 0 && controller.blocks[0].previewFormat !== "rich"))
    // Keyboard shortcuts must not act on the document behind an open dialog.
    readonly property bool dialogOpen: saveDialog.visible || overwriteDialog.visible || imageFileDialog.visible
        || newFileDialog.visible || folderDialog.visible || richInsertDialog.visible || newTableDialog.visible
        || discardDialog.visible || quitDialog.visible || reloadDialog.visible || errorDialog.visible
    property bool quitAllowed: false
    property int documentFocusAttempts: 0
    Component.onCompleted: if (controller.projectFolder && !controller.hasDocument) focusFileList()
    // Table highlighted for deletion (dragged over or Shift+arrowed onto), or -1.
    property int selectedTable: -1
    property int pendingCloseTab: -1
    property var activeEditor: null
    property int insertBlock: -1
    property int insertCursor: 0
    property bool insertVisualCursor: false
    property bool insertSourceCursor: false
    property int insertAfterTableIndex: -1
    property string insertKind: "link"
    property int insertTargetBlock: -1
    property int insertStart: 0
    property int insertEnd: 0
    property string hoveredLink: ""
    property int pendingTableIndex: -1
    property int pendingTableRow: -1
    property int pendingTableCol: -1
    property int tableFocusAttempts: 0
    property int pendingTextTableIndex: -1
    property int pendingTextDirection: 0
    property int textFocusAttempts: 0
    function offerLink(item, link) {
        if (!link) {
            linkCloseTimer.restart()
            return
        }
        linkCloseTimer.stop()
        hoveredLink = link
        const point = root.contentItem.mapFromGlobal(controller.pointerPosition())
        linkPopup.x = Math.max(8, Math.min(root.width - linkPopup.implicitWidth - 8, point.x + 12))
        linkPopup.y = Math.max(8, Math.min(root.height - linkPopup.implicitHeight - 8, point.y + 18))
        linkPopup.open()
    }

    function focusFileList() {
        sidebarOpen = true
        Qt.callLater(() => {
            fileList.forceActiveFocus()
            if (fileList.currentIndex < 0 && controller.projectFiles.length > 0) {
                fileList.currentIndex = 0
                fileList.rememberCurrent()
            }
        })
    }

    function tableBlockAt(index) {
        const block = controller.blocks[index]
        return block && block.kind === "table" ? block : null
    }

    function onFirstLine(editor) {
        return Math.abs(editor.cursorRectangle.y - editor.positionToRectangle(0).y) < 2
    }

    function onLastLine(editor) {
        return Math.abs(editor.cursorRectangle.y - editor.positionToRectangle(editor.length).y) < 2
    }

    // Arrow keys step into a neighbouring table, and Shift+arrow selects it as a whole.
    function handleTableKeys(editor, event) {
        const shift = event.modifiers & Qt.ShiftModifier
        const key = event.key
        const vertical = key === Qt.Key_Up || key === Qt.Key_Down
        const index = editor.sourceBlockIndex
        const below = tableBlockAt(index + 1)
        const above = tableBlockAt(index - 1)
        if (selectedTable >= 0) {
            if (key === Qt.Key_Delete || key === Qt.Key_Backspace) {
                deleteSelectedTable()
                event.accepted = true
                return
            }
            if (key === Qt.Key_Shift || key === Qt.Key_Control || key === Qt.Key_Alt || key === Qt.Key_Meta) return
            if (shift && key === Qt.Key_Up && below && below.tableIndex === selectedTable) {
                selectedTable = -1
                event.accepted = true
                return
            }
            if (shift && key === Qt.Key_Down && above && above.tableIndex === selectedTable) {
                selectedTable = -1
                event.accepted = true
                return
            }
            if (!(shift && vertical)) selectedTable = -1
        }
        if (!vertical || (event.modifiers & (Qt.ControlModifier | Qt.AltModifier | Qt.MetaModifier))) return
        if (key === Qt.Key_Down && below && onLastLine(editor)) {
            if (shift) selectedTable = below.tableIndex
            else queueTableCellFocus(below.tableIndex, 0, 0)
            event.accepted = true
        } else if (key === Qt.Key_Up && above && onFirstLine(editor)) {
            if (shift) selectedTable = above.tableIndex
            else queueTableCellFocus(above.tableIndex, above.rows.length - 1, 0)
            event.accepted = true
        }
    }

    // A mouse drag that starts outside a table and passes over it selects the table.
    function selectTablesInDrag(startY, currentY) {
        if (Math.abs(currentY - startY) < 6) return
        const top = Math.min(startY, currentY)
        const bottom = Math.max(startY, currentY)
        let found = -1
        for (let i = 0; i < blockRepeater.count; ++i) {
            const loader = blockRepeater.itemAt(i)
            const data = loader && loader.item ? loader.item.blockData : null
            if (!data || data.kind !== "table") continue
            if (startY >= loader.y && startY <= loader.y + loader.height) {
                selectedTable = -1
                return
            }
            if (bottom >= loader.y && top <= loader.y + loader.height && found < 0) found = data.tableIndex
        }
        selectedTable = found
    }

    function deleteSelectedTable() {
        const table = selectedTable
        selectedTable = -1
        if (table < 0) return
        const blocks = controller.blocks
        const tableBlockIndex = blocks.findIndex(block => block.kind === "table" && block.tableIndex === table)
        let caretBlock = 0
        let caretPosition = 0
        if (tableBlockIndex > 0) {
            const loader = blockRepeater.itemAt(tableBlockIndex - 1)
            if (loader && loader.item && loader.item.editor) {
                caretBlock = tableBlockIndex - 1
                caretPosition = loader.item.editor.length
            }
        }
        commitActive()
        if (!controller.deleteTable(table)) return
        // The text above and below the table is now one block; put the caret where the table was.
        Qt.callLater(() => {
            const loader = blockRepeater.itemAt(caretBlock)
            const editor = loader && loader.item ? loader.item.editor : null
            if (!editor) return
            editor.cursorPosition = Math.min(caretPosition, editor.length)
            editor.forceActiveFocus()
        })
    }

    function commitActive() {
        if (activeEditor && activeEditor.commit)
            activeEditor.commit()
        activeEditor = null
        if (sourceMode && sourceArea.text !== controller.sourceText)
            controller.setSourceText(sourceArea.text)
    }

    function formatSelection(style) {
        const editor = activeEditor
        if (!editor || !editor.richMarkdown) return
        if (editor.selectionEnd > editor.selectionStart) {
            controller.formatSelection(editor.textDocument, editor.selectionStart, editor.selectionEnd, style)
            editor.contentDirty = true
            return
        }
        if (!editor.explicitTypingStyle) {
            editor.typingBold = controller.styleAtCursor(editor.textDocument, editor.cursorPosition, "bold")
            editor.typingItalic = controller.styleAtCursor(editor.textDocument, editor.cursorPosition, "italic")
            editor.explicitTypingStyle = true
        }
        if (style === "bold") editor.typingBold = !editor.typingBold
        else if (style === "italic") editor.typingItalic = !editor.typingItalic
    }

    function handleStyledKey(editor, event) {
        if (!editor.explicitTypingStyle || !event.text || /[\u0000-\u001f\u007f]/.test(event.text)
                || (event.modifiers & (Qt.ControlModifier | Qt.AltModifier | Qt.MetaModifier))) return
        const start = Math.min(editor.selectionStart, editor.selectionEnd)
        const end = Math.max(editor.selectionStart, editor.selectionEnd)
        editor.cursorPosition = controller.insertStyledText(editor.textDocument, start, end, event.text,
                                                             editor.typingBold, editor.typingItalic)
        editor.contentDirty = true
        event.accepted = true
    }

    // Markdown-style shortcuts while typing in the visual editor.
    function handleMarkdownKey(editor, event) {
        if (!editor.richMarkdown || editor.readOnly || (event.modifiers & (Qt.ControlModifier | Qt.AltModifier | Qt.MetaModifier)))
            return
        let position = -1
        if (event.key === Qt.Key_Space)
            position = controller.applyHeadingShortcut(editor.textDocument, editor.cursorPosition)
        else if ((event.key === Qt.Key_Return || event.key === Qt.Key_Enter) && !(event.modifiers & Qt.ShiftModifier))
            position = controller.endHeading(editor.textDocument, editor.cursorPosition)
        if (position < 0) return
        editor.cursorPosition = position
        editor.contentDirty = true
        event.accepted = true
    }

    function toggleList(numbered) {
        if (!controller.hasDocument || !controller.markdown) return
        if (sourceMode) {
            const marker = numbered ? "1. " : "- "
            sourceArea.insert(sourceArea.cursorPosition, marker)
            controller.setSourceText(sourceArea.text)
        } else if (activeEditor && activeEditor.sourceBlockIndex !== undefined) {
            controller.toggleList(activeEditor.textDocument, activeEditor.selectionStart,
                                  activeEditor.selectionEnd, numbered)
            activeEditor.contentDirty = true
        }
    }

    function openInsertDialog(kind) {
        if (!canInsertMarkup) return
        insertKind = kind
        if (sourceMode) {
            insertTargetBlock = -1
            insertStart = sourceArea.selectionStart
            insertEnd = sourceArea.selectionEnd
            insertLabelField.text = sourceArea.selectedText
        } else if (activeEditor && activeEditor.sourceBlockIndex !== undefined) {
            insertTargetBlock = activeEditor.sourceBlockIndex
            insertStart = activeEditor.selectionStart
            insertEnd = activeEditor.selectionEnd
            insertLabelField.text = activeEditor.selectedText
        } else return
        insertUrlField.text = ""
        commitActive()
        richInsertDialog.open()
    }

    function insertionMarkup(label, url) {
        const target = url.replace(/ /g, "%20").replace(/\(/g, "%28").replace(/\)/g, "%29")
        const text = (label || (insertKind === "image" ? "" : url)).replace(/([\[\]])/g, "\\$1")
        return (insertKind === "image" ? "![" : "[") + text + "](" + target + ")"
    }

    function applyRichInsertion() {
        root.requestActivate()
        const url = insertUrlField.text.trim()
        if (!url) return
        const label = insertLabelField.text.trim()
        if (insertTargetBlock < 0) {
            const markup = insertionMarkup(label, url)
            sourceArea.remove(insertStart, insertEnd)
            sourceArea.insert(insertStart, markup)
            sourceArea.cursorPosition = insertStart + markup.length
            controller.setSourceText(sourceArea.text)
            sourceArea.forceActiveFocus()
            return
        }
        const loader = blockRepeater.itemAt(insertTargetBlock)
        const editor = loader && loader.item ? loader.item.editor : null
        if (!editor) return
        if (!controller.markdown) {
            const markup = insertionMarkup(label, url)
            editor.remove(insertStart, insertEnd)
            editor.insert(insertStart, markup)
            editor.contentDirty = true
            editor.cursorPosition = insertStart + markup.length
            editor.forceActiveFocus()
            return
        }
        const position = insertKind === "image"
            ? controller.insertImage(editor.textDocument, insertStart, insertEnd, url, label)
            : controller.insertLink(editor.textDocument, insertStart, insertEnd, url, label, uiTheme.accent)
        editor.contentDirty = true
        editor.cursorPosition = position
        editor.forceActiveFocus()
    }

    function saveCurrent() {
        commitActive()
        if (!controller.hasDocument) return
        if (controller.currentHasPath) controller.save()
        else saveDialog.open()
    }

    function newDocument() {
        commitActive()
        controller.newDocument()
        Qt.callLater(root.focusDocument)
    }

    // Puts the caret at the start of the document, once the editors for it exist. A document that
    // begins with a table gets the caret in that table's first cell.
    function focusDocument() {
        if (controller.unsupportedFileMessage) return
        root.requestActivate()
        documentFocusAttempts = 0
        documentFocusTimer.interval = 0
        documentFocusTimer.restart()
    }

    function focusDocumentStart() {
        if (sourceMode) {
            sourceArea.cursorPosition = 0
            sourceArea.forceActiveFocus()
            return sourceArea.activeFocus
        }
        const loader = blockRepeater.itemAt(0)
        if (!loader || !loader.item) return false
        if (loader.item.editor) {
            loader.item.editor.cursorPosition = 0
            loader.item.editor.forceActiveFocus()
            return loader.item.editor.activeFocus
        }
        return loader.item.focusCell ? loader.item.focusCell(0, 0) : false
    }

    function focusDocumentEnd() {
        if (!controller.hasDocument || controller.unsupportedFileMessage) return
        root.commitActive()
        // Committing another block may rebuild the visual editors.
        Qt.callLater(() => {
            if (root.sourceMode) {
                sourceArea.cursorPosition = sourceArea.length
                sourceArea.forceActiveFocus()
                return
            }
            const loader = blockRepeater.itemAt(blockRepeater.count - 1)
            const editor = loader && loader.item ? loader.item.editor : null
            if (editor && !editor.readOnly) {
                editor.cursorPosition = editor.length
                editor.forceActiveFocus()
            }
        })
    }

    function closeTab(index) {
        commitActive()
        if (!controller.closeTab(index, false)) {
            pendingCloseTab = index
            discardDialog.open()
        }
    }

    function closeCurrent() {
        if (!controller.hasDocument)
            return
        const current = controller.tabs.findIndex(t => t.active)
        if (current >= 0) {
            closeTab(current)
        }
    }

    function reloadCurrent() {
        commitActive()
        const current = controller.tabs.find(t => t.active)
        if (current && current.dirty) reloadDialog.open()
        else controller.reloadCurrent(false)
    }

    function newTable() {
        if (!canInsertMarkup) return
        let index = -1
        let cursor = 0
        let visualCursor = false
        let afterTable = -1
        if (sourceMode) {
            cursor = sourceArea.cursorPosition
        } else if (activeEditor && activeEditor.tableIndex !== undefined) {
            afterTable = activeEditor.tableIndex
        } else if (activeEditor && activeEditor.sourceBlockIndex !== undefined) {
            index = activeEditor.sourceBlockIndex
            cursor = activeEditor.cursorPosition
            visualCursor = controller.markdown
        }
        commitActive()
        const blocks = controller.blocks
        if (!sourceMode && afterTable < 0 && (index < 0 || index >= blocks.length || blocks[index].kind !== "text")) {
            for (let i = blocks.length - 1; i >= 0; --i) {
                if (blocks[i].kind === "text") {
                    index = i
                    cursor = blocks[i].text.length
                    visualCursor = false
                    break
                }
            }
        }
        insertBlock = index
        insertCursor = cursor
        insertVisualCursor = visualCursor
        insertSourceCursor = sourceMode
        insertAfterTableIndex = afterTable
        newTableDialog.open()
    }

    function focusTableCell(tableIndex, row, col) {
        for (let i = 0; i < blockRepeater.count; ++i) {
            const loader = blockRepeater.itemAt(i)
            if (loader && loader.item && loader.item.blockData
                    && loader.item.blockData.kind === "table"
                    && loader.item.blockData.tableIndex === tableIndex) {
                return loader.item.focusCell(row, col)
            }
        }
        return false
    }

    function queueTableCellFocus(tableIndex, row, col) {
        pendingTableIndex = tableIndex
        pendingTableRow = row
        pendingTableCol = col
        tableFocusAttempts = 0
        tableFocusTimer.interval = 0
        tableFocusTimer.restart()
    }

    function adjacentTextBlock(tableIndex, direction) {
        const blocks = controller.blocks
        const tableBlockIndex = blocks.findIndex(block => block.kind === "table" && block.tableIndex === tableIndex)
        if (tableBlockIndex < 0) return -1
        for (let i = tableBlockIndex + direction; i >= 0 && i < blocks.length; i += direction)
            if (blocks[i].kind === "text") return i
        return -1
    }

    function focusAdjacentTextBlock(tableIndex, direction) {
        const index = adjacentTextBlock(tableIndex, direction)
        if (index < 0) return false
        const loader = blockRepeater.itemAt(index)
        const editor = loader && loader.item ? loader.item.editor : null
        if (!editor) return false
        editor.cursorPosition = direction < 0 ? editor.length : 0
        editor.forceActiveFocus()
        return editor.activeFocus
    }

    function queueAdjacentTextFocus(tableIndex, direction) {
        pendingTextTableIndex = tableIndex
        pendingTextDirection = direction
        textFocusAttempts = 0
        textFocusTimer.interval = 0
        textFocusTimer.restart()
    }

    function runTableAction(action) {
        const editor = root.activeFocusItem
        if (!editor || editor.tableIndex === undefined) return
        root.activeEditor = editor
        for (let i = 0; i < blockRepeater.count; ++i) {
            const loader = blockRepeater.itemAt(i)
            if (loader && loader.item && loader.item.blockData
                    && loader.item.blockData.kind === "table"
                    && loader.item.blockData.tableIndex === editor.tableIndex) {
                root.executeTableAction(editor.tableIndex, action, editor.rowIndex,
                                        editor.columnIndex, loader.item.columns)
                return
            }
        }
    }

    function executeTableAction(tableIndex, action, row, col, columns) {
        if ((action === "row-above" || action === "row-delete") && row === 0) return
        if (action === "col-delete" && columns <= 1) return
        let targetRow = row
        let targetCol = col
        if (action === "row-below") targetRow++
        else if (action === "row-delete") targetRow = Math.max(0, row - 1)
        else if (action === "col-after") targetCol++
        else if (action === "col-delete") targetCol = Math.max(0, col - 1)
        root.queueTableCellFocus(tableIndex, targetRow, targetCol)
        root.commitActive()
        controller.tableCommand(tableIndex, action, row, col)
    }

    Timer {
        id: documentFocusTimer
        interval: 0
        onTriggered: {
            if (root.focusDocumentStart()) return
            if (++root.documentFocusAttempts < 15) {
                interval = 10
                restart()
            }
        }
    }

    Timer {
        id: tableFocusTimer
        interval: 0
        onTriggered: {
            if (root.focusTableCell(root.pendingTableIndex, root.pendingTableRow, root.pendingTableCol)) {
                root.pendingTableIndex = -1
                return
            }
            if (++root.tableFocusAttempts < 10) {
                interval = 10
                restart()
            } else root.pendingTableIndex = -1
        }
    }

    Timer {
        id: textFocusTimer
        interval: 0
        onTriggered: {
            if (root.focusAdjacentTextBlock(root.pendingTextTableIndex, root.pendingTextDirection)) {
                root.pendingTextTableIndex = -1
                return
            }
            if (++root.textFocusAttempts < 10) {
                interval = 10
                restart()
            } else root.pendingTextTableIndex = -1
        }
    }

    onClosing: function(close) {
        commitActive()
        if (!quitAllowed && controller.hasDirtyDocuments) {
            close.accepted = false
            quitDialog.open()
        }
    }

    Shortcut { sequence: "Ctrl+N"; enabled: !root.dialogOpen; onActivated: root.newDocument() }
    Shortcut { sequence: "Ctrl+O"; enabled: !root.dialogOpen; onActivated: folderDialog.open() }
    Shortcut { sequence: "Ctrl+K, Ctrl+O"; enabled: !root.dialogOpen; onActivated: folderDialog.open() }
    Shortcut { sequence: "Ctrl+Shift+O"; enabled: !root.dialogOpen; onActivated: folderDialog.open() }
    Shortcut { sequence: "Ctrl+S"; enabled: !root.dialogOpen; onActivated: root.saveCurrent() }
    Shortcut { sequence: "Ctrl+Shift+S"; enabled: !root.dialogOpen; onActivated: { root.commitActive(); if (controller.hasDocument) saveDialog.open() } }
    Shortcut { sequence: "Ctrl+R"; enabled: !root.dialogOpen && (controller.currentHasPath); onActivated: root.reloadCurrent() }
    Shortcut { sequence: "Ctrl+W"; enabled: !root.dialogOpen; onActivated: root.closeCurrent() }
    Shortcut { sequence: "Ctrl+Z"; enabled: !root.dialogOpen && (!controller.unsupportedFileMessage); onActivated: { root.commitActive(); controller.undo() } }
    Shortcut { sequence: "Ctrl+Y"; enabled: !root.dialogOpen && (!controller.unsupportedFileMessage); onActivated: { root.commitActive(); controller.redo() } }
    Shortcut { sequence: "Ctrl+Shift+Z"; enabled: !root.dialogOpen && (!controller.unsupportedFileMessage); onActivated: { root.commitActive(); controller.redo() } }
    Shortcut { sequence: "Ctrl+Q"; enabled: !root.dialogOpen; onActivated: root.close() }
    Shortcut { sequence: "Ctrl+Shift+E"; enabled: !root.dialogOpen; onActivated: root.focusFileList() }
    Shortcut { sequence: "Ctrl+Shift+B"; enabled: !root.dialogOpen; onActivated: root.sidebarOpen = !root.sidebarOpen }
    Shortcut { sequence: "Ctrl+B"; enabled: !root.dialogOpen; onActivated: root.formatSelection("bold") }
    Shortcut { sequence: "Ctrl+I"; enabled: !root.dialogOpen; onActivated: root.formatSelection("italic") }
    Shortcut { sequence: "Ctrl+Shift+8"; enabled: !root.dialogOpen && (controller.hasDocument && controller.markdown); onActivated: root.toggleList(false) }
    Shortcut { sequence: "Ctrl+Shift+7"; enabled: !root.dialogOpen && (controller.hasDocument && controller.markdown); onActivated: root.toggleList(true) }
    Shortcut { sequence: "Ctrl+Alt+K"; enabled: !root.dialogOpen && (root.canInsertMarkup); onActivated: root.openInsertDialog("link") }
    Shortcut { sequence: "Ctrl+Alt+I"; enabled: !root.dialogOpen && (root.canInsertMarkup); onActivated: root.openInsertDialog("image") }
    Shortcut { sequence: "Ctrl+Alt+T"; enabled: !root.dialogOpen && (root.canInsertMarkup); onActivated: root.newTable() }
    Shortcut { sequence: "Ctrl+Alt+Up"; enabled: !root.dialogOpen && (root.activeFocusItem && root.activeFocusItem.tableIndex !== undefined)
        onActivated: root.runTableAction("row-above") }
    Shortcut { sequence: "Ctrl+Alt+Down"; enabled: !root.dialogOpen && (root.activeFocusItem && root.activeFocusItem.tableIndex !== undefined)
        onActivated: root.runTableAction("row-below") }
    Shortcut { sequence: "Ctrl+Alt+Left"; enabled: !root.dialogOpen && (root.activeFocusItem && root.activeFocusItem.tableIndex !== undefined)
        onActivated: root.runTableAction("col-before") }
    Shortcut { sequence: "Ctrl+Alt+Right"; enabled: !root.dialogOpen && (root.activeFocusItem && root.activeFocusItem.tableIndex !== undefined)
        onActivated: root.runTableAction("col-after") }
    Shortcut { sequence: "Ctrl+Alt+Shift+Up"; enabled: !root.dialogOpen && (root.activeFocusItem && root.activeFocusItem.tableIndex !== undefined)
        onActivated: root.runTableAction("row-delete") }
    Shortcut { sequence: "Ctrl+Alt+Shift+Left"; enabled: !root.dialogOpen && (root.activeFocusItem && root.activeFocusItem.tableIndex !== undefined)
        onActivated: root.runTableAction("col-delete") }
    Shortcut { sequence: "Ctrl++"; onActivated: uiTheme.zoomIn() }
    Shortcut { sequence: "Ctrl+="; onActivated: uiTheme.zoomIn() }
    Shortcut { sequence: "Ctrl+-"; onActivated: uiTheme.zoomOut() }
    Shortcut { sequence: "Ctrl+0"; onActivated: uiTheme.resetZoom() }

    // Number input for dialogs: type a value directly, Tab moves on, Enter confirms the dialog.
    component NumberField: SpinBox {
        id: numberField
        signal confirmed()
        editable: true
        onActiveFocusChanged: if (activeFocus) Qt.callLater(() => contentItem.selectAll())
        Connections {
            target: numberField.contentItem
            function onAccepted() {
                numberField.value = numberField.valueFromText(numberField.contentItem.text, numberField.locale)
                numberField.confirmed()
            }
        }
    }

    component KernButton: Button {
        id: kernButton
        property bool primary: false
        implicitHeight: 26
        implicitWidth: Math.max(72, contentItem.implicitWidth + 24)
        topPadding: 4
        bottomPadding: 4
        leftPadding: 12
        rightPadding: 12
        font.pixelSize: 11
        Keys.onReturnPressed: kernButton.clicked()
        Keys.onEnterPressed: kernButton.clicked()
        contentItem: Label {
            text: kernButton.text
            font: kernButton.font
            horizontalAlignment: Text.AlignHCenter
            verticalAlignment: Text.AlignVCenter
            elide: Text.ElideRight
            color: !kernButton.enabled ? uiTheme.muted
                : kernButton.primary ? uiTheme.background : uiTheme.foreground
        }
        background: Rectangle {
            radius: 4
            opacity: kernButton.enabled ? 1 : 0.5
            color: kernButton.primary
                ? (kernButton.down ? Qt.darker(uiTheme.accent, 1.25)
                    : kernButton.hovered || kernButton.activeFocus ? Qt.lighter(uiTheme.accent, 1.12) : uiTheme.accent)
                : Qt.alpha(uiTheme.accent, kernButton.down ? 0.34 : kernButton.activeFocus ? 0.26 : kernButton.hovered ? 0.2 : 0.1)
            border.width: kernButton.activeFocus ? 2 : 1
            border.color: kernButton.activeFocus ? (kernButton.primary ? uiTheme.foreground : uiTheme.accent)
                : kernButton.primary ? Qt.darker(uiTheme.accent, 1.3) : Qt.alpha(uiTheme.accent, 0.4)
        }
    }
    // Dialog with a flat accent-tinted surface and an explicit button row. The accept button
    // comes first and takes focus on open; Left/Right move to Cancel, Enter activates, Esc cancels.
    component KernDialog: Dialog {
        id: kernDialog
        property string acceptText: "OK"
        property string rejectText: "Cancel"
        property bool focusAccept: true
        property bool autoAccept: true
        property alias acceptButton: dialogAccept
        property alias rejectButton: dialogReject
        signal acceptRequested()
        function confirm() {
            if (autoAccept) accept()
            else acceptRequested()
        }
        padding: 14
        topPadding: 8
        background: Rectangle {
            color: Qt.darker(uiTheme.background, 1.06)
            radius: 6
            border.color: Qt.alpha(uiTheme.accent, 0.5)
        }
        header: Label {
            text: kernDialog.title
            visible: text.length > 0
            height: visible ? implicitHeight : 0
            font.bold: true
            font.pixelSize: 13
            color: uiTheme.foreground
            padding: 14
            bottomPadding: 0
        }
        footer: Item {
            implicitWidth: dialogButtons.implicitWidth + 28
            implicitHeight: dialogButtons.implicitHeight + 24
            RowLayout {
                id: dialogButtons
                spacing: 8
                anchors { right: parent.right; bottom: parent.bottom; rightMargin: 14; bottomMargin: 12 }
                KernButton {
                    id: dialogAccept
                    objectName: "dialog-accept"
                    primary: true
                    text: kernDialog.acceptText
                    visible: text.length > 0
                    KeyNavigation.right: dialogReject
                    KeyNavigation.tab: dialogReject
                    onClicked: kernDialog.confirm()
                }
                KernButton {
                    id: dialogReject
                    objectName: "dialog-reject"
                    text: kernDialog.rejectText
                    visible: text.length > 0
                    KeyNavigation.left: dialogAccept
                    KeyNavigation.backtab: dialogAccept
                    onClicked: kernDialog.reject()
                }
            }
        }
        onOpened: if (focusAccept) dialogAccept.forceActiveFocus()
        Overlay.modal: Rectangle { color: Qt.alpha(uiTheme.background, 0.6) }
    }

    component RibbonButton: ToolButton {
        id: ribbonButton
        property string glyph: ""
        property string tip: ""
        focusPolicy: Qt.NoFocus
        topPadding: 4
        bottomPadding: 4
        leftPadding: 5
        rightPadding: 5
        ToolTip.text: tip
        ToolTip.visible: hovered && tip.length > 0
        ToolTip.delay: 400
        contentItem: RowLayout {
            spacing: 3
            Label { text: ribbonButton.glyph; visible: text.length > 0; font.family: uiTheme.monoFont; font.pixelSize: 15; color: ribbonButton.enabled ? uiTheme.accent : uiTheme.muted; opacity: ribbonButton.enabled ? 1 : 0.45 }
            Label { text: ribbonButton.text; visible: text.length > 0; font.pixelSize: 11; color: ribbonButton.enabled ? uiTheme.foreground : uiTheme.muted }
        }
        background: Rectangle {
            radius: 3
            color: ribbonButton.down ? Qt.alpha(uiTheme.accent, 0.18)
                : ribbonButton.hovered ? Qt.alpha(uiTheme.foreground, 0.06) : "transparent"
        }
    }
    component RibbonGroup: ColumnLayout {
        id: group
        property string title
        default property alias content: buttonRow.data
        spacing: 0
        Layout.fillHeight: true
        RowLayout {
            id: buttonRow
            spacing: 1
            Layout.alignment: Qt.AlignVCenter
        }
        Label {
            text: group.title
            Layout.alignment: Qt.AlignHCenter
            font.pixelSize: 9
            opacity: 0.55
        }
    }
    component TableButton: ToolButton {
        id: tableButton
        property string tip: ""
        focusPolicy: Qt.NoFocus
        topPadding: 1
        bottomPadding: 1
        leftPadding: 4
        rightPadding: 4
        implicitWidth: 58
        implicitHeight: 20
        font.family: uiTheme.monoFont
        font.pixelSize: 10
        opacity: enabled ? 1 : 0.5
        ToolTip.text: tip
        ToolTip.visible: hovered && tip.length > 0
        ToolTip.delay: 400
        background: Rectangle {
            radius: 2
            color: tableButton.down ? Qt.alpha(uiTheme.accent, 0.28)
                : tableButton.hovered ? uiTheme.surfaceStrong : uiTheme.surface
            border.color: Qt.alpha(uiTheme.foreground, 0.24)
        }
    }

    header: Rectangle {
        implicitHeight: 58
        color: Qt.alpha(uiTheme.foreground, 0.035)
        Flickable {
            anchors { fill: parent; leftMargin: 8; rightMargin: 8; topMargin: 3; bottomMargin: 3 }
            clip: true
            contentWidth: ribbonRow.implicitWidth
            contentHeight: height
            flickableDirection: Flickable.HorizontalFlick
            boundsBehavior: Flickable.StopAtBounds
            ScrollBar.horizontal: ScrollBar { policy: ScrollBar.AsNeeded }
            RowLayout {
                id: ribbonRow
                height: parent.height
                spacing: 3
                RibbonGroup {
                    title: "File"
                    RibbonButton { glyph: "\uebdf"; tip: "Open project folder  Ctrl+O, Ctrl+Shift+O, or Ctrl+K Ctrl+O"; onClicked: folderDialog.open() }
                    RibbonButton { glyph: "\ueb4b"; tip: "Save  Ctrl+S"; enabled: controller.hasDocument; onClicked: root.saveCurrent() }
                    RibbonButton { glyph: "\ueb4a"; tip: "Save as  Ctrl+Shift+S"; enabled: controller.hasDocument; onClicked: { root.commitActive(); saveDialog.open() } }
                    RibbonButton { glyph: "\uf021"; tip: "Reload from disk  Ctrl+R; confirm before discarding unsaved edits"
                        enabled: controller.currentHasPath; onClicked: root.reloadCurrent() }
                }
                ToolSeparator { Layout.fillHeight: true }
                RibbonGroup {
                    title: "Format"
                    RibbonButton { glyph: "\uf032"; tip: "Bold  Ctrl+B"; onClicked: root.formatSelection("bold") }
                    RibbonButton { glyph: "\uf033"; tip: "Italic  Ctrl+I"; onClicked: root.formatSelection("italic") }
                }
                ToolSeparator { Layout.fillHeight: true }
                RibbonGroup {
                    title: "Insert"
                    RibbonButton { glyph: "\uf0ca"; tip: "Bulleted list  Ctrl+Shift+8"; enabled: controller.hasDocument && controller.markdown
                        onClicked: root.toggleList(false) }
                    RibbonButton { glyph: "\uf0cb"; tip: "Numbered list  Ctrl+Shift+7"; enabled: controller.hasDocument && controller.markdown
                        onClicked: root.toggleList(true) }
                    RibbonButton { glyph: "\uf0c1"; tip: root.canInsertMarkup ? "Insert link  Ctrl+Alt+K" : "Insert link requires an editable text or Markdown document"; enabled: root.canInsertMarkup
                        onClicked: root.openInsertDialog("link") }
                    RibbonButton { glyph: "\uf03e"; tip: root.canInsertMarkup ? "Insert image  Ctrl+Alt+I" : "Insert image requires an editable text or Markdown document"; enabled: root.canInsertMarkup
                        onClicked: root.openInsertDialog("image") }
                    RibbonButton { glyph: "\uf0ce"; enabled: root.canInsertMarkup
                        tip: "Insert Markdown table  Ctrl+Alt+T"; onClicked: root.newTable() }
                }
                ToolSeparator { Layout.fillHeight: true }
                RibbonGroup {
                    title: "Zoom"
                    RibbonButton { glyph: "\ueb82"; tip: "Zoom out  Ctrl+- or Ctrl+wheel down"; onClicked: uiTheme.zoomOut() }
                    RibbonButton { text: uiTheme.zoomPercent + "%"; tip: "Reset zoom  Ctrl+0"; onClicked: uiTheme.resetZoom() }
                    RibbonButton { glyph: "\ueb81"; tip: "Zoom in  Ctrl++ or Ctrl+wheel up"; onClicked: uiTheme.zoomIn() }
                }
            }
        }
    }

    // Keyboard-first file and folder chooser used for opening a folder, creating and saving files,
    // and choosing an image. Up/Down select, Enter opens a folder (or picks a file), Left or
    // Backspace goes up a level, typing jumps to a name, Tab moves to the buttons, and Ctrl+L
    // edits the path. The accept button acts on the folder shown (folder mode), on the typed
    // name (askName), or on the highlighted file.
    component FilePickerDialog: KernDialog {
        id: picker
        property string prefix: "folder"
        property string instruction: ""
        property bool showFiles: false
        property bool askName: false
        property var suffixes: []
        property url currentFolder: controller.projectFolderUrl
        property url selectedFile
        property string pathError: ""
        property string pendingSelect: ""
        property alias nameText: nameField.text
        readonly property alias folderModel: listModel

        function focusName() { nameField.forceActiveFocus(); nameField.selectAll() }
        function navigate(folderUrl, selectName) {
            currentFolder = folderUrl
            pathField.text = controller.localFolderPath(currentFolder)
            pathError = ""
            pendingSelect = selectName || ""
            list.currentIndex = -1
        }
        function navigateEnteredPath() {
            const folderUrl = controller.existingFolderUrl(pathField.text.trim())
            if (folderUrl.toString()) navigate(folderUrl)
            else pathError = "Select an existing directory."
        }
        function goUp() {
            const name = controller.localFolderPath(currentFolder).split("/").filter(part => part).pop()
            navigate(listModel.parentFolder, name)
        }
        function restoreSelection() {
            let index = listModel.count > 0 ? 0 : -1
            if (pendingSelect) {
                for (let i = 0; i < listModel.count; ++i)
                    if (listModel.get(i, "fileName") === pendingSelect) { index = i; break }
            }
            pendingSelect = ""
            list.currentIndex = index
        }
        function currentFileUrl() {
            const item = list.currentItem
            return item && !item.fileIsDir ? item.fileUrl : ""
        }
        function activateCurrent() {
            const item = list.currentItem
            if (!item) return
            if (item.fileIsDir) navigate(item.fileUrl)
            else if (askName) { nameField.text = item.fileName; focusName() }
            else { selectedFile = item.fileUrl; confirm() }
        }
        function typeAhead(text) {
            const start = list.currentIndex + 1
            for (let step = 0; step < listModel.count; ++step) {
                const i = (start + step) % listModel.count
                if (listModel.get(i, "fileName").toLowerCase().startsWith(text.toLowerCase())) {
                    list.currentIndex = i
                    return
                }
            }
        }

        modal: true
        anchors.centerIn: parent
        width: Math.min(root.width - 40, 640)
        height: Math.min(root.height - 40, 500)
        padding: 14
        header: null
        focusAccept: false
        onOpened: Qt.callLater(() => {
            restoreSelection()
            if (askName) focusName()
            else list.forceActiveFocus()
        })
        Component.onCompleted: {
            // Tab cycle: [name] list, accept, cancel, path field. Shift+Tab goes the other way.
            list.KeyNavigation.tab = askName ? nameField : acceptButton
            list.KeyNavigation.backtab = pathField
            nameField.KeyNavigation.tab = acceptButton
            nameField.KeyNavigation.backtab = list
            acceptButton.KeyNavigation.backtab = askName ? nameField : list
            rejectButton.KeyNavigation.tab = pathField
            pathField.KeyNavigation.tab = list
            pathField.KeyNavigation.backtab = rejectButton
        }

        Shortcut {
            sequence: "Ctrl+L"
            enabled: picker.visible
            onActivated: { pathField.forceActiveFocus(); pathField.selectAll() }
        }

        FolderListModel {
            id: listModel
            folder: picker.currentFolder
            showFiles: picker.showFiles
            showDirsFirst: true
            sortField: FolderListModel.Name
            nameFilters: {
                const filters = []
                for (const suffix of picker.suffixes)
                    filters.push("*." + suffix, "*." + suffix.toUpperCase())
                return filters.length ? filters : ["*"]
            }
            onStatusChanged: if (status === FolderListModel.Ready) Qt.callLater(picker.restoreSelection)
        }

        contentItem: ColumnLayout {
            spacing: 9
            Label {
                objectName: picker.prefix + "-instruction"
                text: picker.instruction
                color: uiTheme.foreground
                font.pixelSize: 13
                font.bold: true
                Layout.fillWidth: true
            }
            RowLayout {
                Layout.fillWidth: true
                spacing: 6
                ToolButton {
                    text: "\uf062"
                    font.family: uiTheme.monoFont
                    focusPolicy: Qt.NoFocus
                    ToolTip.text: "Parent directory  Left or Backspace in the list"
                    ToolTip.visible: hovered
                    enabled: listModel.parentFolder.toString() !== picker.currentFolder.toString()
                    onClicked: picker.goUp()
                }
                TextField {
                    id: pathField
                    objectName: picker.prefix + "-path-field"
                    Layout.fillWidth: true
                    placeholderText: "Directory path  (Ctrl+L)"
                    onAccepted: {
                        const entered = controller.existingFolderUrl(text.trim())
                        if (!picker.askName && !picker.showFiles && entered.toString()
                                && entered.toString() === picker.currentFolder.toString())
                            picker.confirm()
                        else {
                            picker.navigateEnteredPath()
                            if (picker.askName) picker.focusName()
                            else list.forceActiveFocus()
                        }
                    }
                }
                KernButton { text: "Go"; focusPolicy: Qt.NoFocus; onClicked: picker.navigateEnteredPath() }
            }
            Label {
                text: picker.pathError
                color: uiTheme.accent
                visible: text.length > 0
                wrapMode: Text.Wrap
                Layout.fillWidth: true
            }
            Label { text: "Filename"; visible: picker.askName }
            TextField {
                id: nameField
                objectName: picker.prefix + "-name"
                visible: picker.askName
                Layout.fillWidth: true
                placeholderText: "Filename, including extension"
                onAccepted: picker.confirm()
                onTextEdited: picker.pathError = ""
                Keys.onDownPressed: { list.forceActiveFocus(); if (list.currentIndex < 0 && listModel.count > 0) list.currentIndex = 0 }
            }
            ListView {
                id: list
                objectName: picker.prefix + "-list"
                Layout.fillWidth: true
                Layout.fillHeight: true
                clip: true
                model: listModel
                keyNavigationEnabled: true
                currentIndex: -1
                highlightMoveDuration: 0
                highlight: Rectangle {
                    color: Qt.alpha(uiTheme.accent, list.activeFocus ? 0.26 : 0.12)
                    radius: 3
                }
                Keys.onPressed: event => {
                    if (event.modifiers & (Qt.ControlModifier | Qt.AltModifier | Qt.MetaModifier)) return
                    if (event.key === Qt.Key_Return || event.key === Qt.Key_Enter) picker.activateCurrent()
                    else if (event.key === Qt.Key_Left || event.key === Qt.Key_Backspace) picker.goUp()
                    else if (event.key === Qt.Key_Right) {
                        if (currentItem && currentItem.fileIsDir) picker.activateCurrent()
                    } else if (event.text.length === 1 && event.text.trim().length === 1) picker.typeAhead(event.text)
                    else return
                    event.accepted = true
                }
                ScrollBar.vertical: ScrollBar { policy: ScrollBar.AsNeeded }
                delegate: ItemDelegate {
                    required property int index
                    required property string fileName
                    required property url fileUrl
                    required property bool fileIsDir
                    objectName: picker.prefix + "-entry-" + fileName
                    width: list.width
                    height: 22
                    topPadding: 0
                    bottomPadding: 0
                    focusPolicy: Qt.NoFocus
                    text: (fileIsDir ? "\uf07b  " : "\uf15b  ") + fileName
                    font.family: uiTheme.monoFont
                    background: Item {}
                    onClicked: {
                        list.currentIndex = index
                        if (fileIsDir) picker.navigate(fileUrl)
                        else if (picker.askName) { picker.nameText = fileName; picker.focusName() }
                    }
                    onDoubleClicked: if (!fileIsDir && !picker.askName) picker.activateCurrent()
                }
            }
        }
    }

    KernDialog {
        id: overwriteDialog
        objectName: "overwrite-dialog"
        property url target
        title: "Replace existing file?"
        modal: true
        anchors.centerIn: parent
        acceptText: "Replace"
        onAccepted: { controller.saveAsUrl(target); saveDialog.close() }
    }

    FilePickerDialog {
        id: saveDialog
        objectName: "save-file-dialog"
        prefix: "save-file"
        instruction: "Save as"
        showFiles: true
        askName: true
        acceptText: "Save"
        autoAccept: false
        onOpened: {
            navigate(controller.folderOf(controller.suggestedSaveUrl))
            nameText = controller.fileNameOf(controller.suggestedSaveUrl)
        }
        onAcceptRequested: {
            const problem = controller.fileNameProblem(nameText)
            if (problem) { pathError = problem; focusName(); return }
            const target = controller.childUrl(currentFolder, nameText)
            if (controller.pathExists(target) && !controller.isFile(target)) {
                pathError = "A directory with this name already exists."
                focusName()
            } else if (controller.pathExists(target)) {
                overwriteDialog.target = target
                overwriteDialog.open()
            } else {
                controller.saveAsUrl(target)
                close()
            }
        }
    }

    FilePickerDialog {
        id: newFileDialog
        objectName: "new-file-dialog"
        prefix: "new-file"
        instruction: "Create a new file"
        showFiles: true
        askName: true
        acceptText: "Create"
        autoAccept: false
        function createFile() {
            root.commitActive()
            pathError = controller.createNamedProjectFile(currentFolder, nameText)
            if (pathError) {
                focusName()
                return
            }
            close()
            Qt.callLater(root.focusDocument)
        }
        onOpened: { navigate(controller.projectFolderUrl); nameText = "" }
        onAcceptRequested: createFile()
    }

    FilePickerDialog {
        id: folderDialog
        objectName: "folder-dialog"
        prefix: "folder"
        instruction: "Select a directory to open as a project."
        acceptText: "Open folder"
        onOpened: navigate(controller.projectFolderUrl)
        onAccepted: {
            controller.setProjectFolderUrl(currentFolder)
            root.focusFileList()
        }
    }

    KernDialog {
        id: richInsertDialog
        objectName: "rich-insert-dialog"
        title: root.insertKind === "image" ? "Insert image" : "Insert link"
        modal: true
        anchors.centerIn: parent
        width: Math.min(root.width - 40, 520)
        focusAccept: false
        onOpened: insertUrlField.forceActiveFocus()
        onAccepted: Qt.callLater(root.applyRichInsertion)
        contentItem: ColumnLayout {
            spacing: 8
            Label { text: "URL or file path" }
            RowLayout {
                Layout.fillWidth: true
                TextField {
                    id: insertUrlField
                    objectName: "rich-insert-url"
                    Layout.fillWidth: true
                    Layout.minimumWidth: 0
                    placeholderText: "https://example.org or relative path"
                    onAccepted: richInsertDialog.accept()
                }
                KernButton {
                    objectName: "browse-image-button"
                    text: "Browse…"
                    visible: root.insertKind === "image"
                    onClicked: imageFileDialog.open()
                }
            }
            Label { text: root.insertKind === "image" ? "Alt text" : "Link text" }
            TextField {
                id: insertLabelField
                objectName: "rich-insert-label"
                Layout.fillWidth: true
                placeholderText: root.insertKind === "image" ? "Describe the image" : "Text shown in the document"
                onAccepted: richInsertDialog.accept()
            }
        }
    }

    FilePickerDialog {
        id: imageFileDialog
        objectName: "image-file-dialog"
        prefix: "image-file"
        instruction: "Choose an image"
        showFiles: true
        suffixes: ["png", "jpg", "jpeg", "gif", "webp", "svg", "bmp", "avif"]
        acceptText: "Choose"
        autoAccept: false
        onOpened: navigate(controller.currentHasPath ? controller.folderOf(controller.documentUrl) : controller.projectFolderUrl)
        onAcceptRequested: {
            const file = currentFileUrl()
            if (file.toString()) { selectedFile = file; accept() }
            else pathError = "Select an image file."
        }
        onAccepted: {
            insertUrlField.text = selectedFile.toString().replace(/ /g, "%20").replace(/\(/g, "%28").replace(/\)/g, "%29")
            insertLabelField.forceActiveFocus()
        }
    }

    Timer {
        id: linkCloseTimer
        interval: 450
        onTriggered: if (!linkPopup.hovered) linkPopup.close()
    }
    Popup {
        id: linkPopup
        objectName: "link-actions"
        property bool hovered: hoverArea.hovered
        modal: false
        focus: false
        padding: 8
        closePolicy: Popup.CloseOnEscape
        background: Rectangle {
            color: uiTheme.surface
            border.color: Qt.alpha(uiTheme.foreground, 0.28)
            radius: 5
        }
        HoverHandler {
            id: hoverArea
            onHoveredChanged: if (!hovered) linkCloseTimer.restart()
        }
        ColumnLayout {
            spacing: 5
            Label {
                text: root.hoveredLink
                color: uiTheme.muted
                font.pixelSize: 10
                elide: Text.ElideMiddle
                Layout.maximumWidth: 330
            }
            RowLayout {
                spacing: 4
                KernButton {
                    text: "\uf0ac Open in browser"
                    focusPolicy: Qt.NoFocus
                    onClicked: { controller.openLink(root.hoveredLink); linkPopup.close() }
                }
                KernButton {
                    text: "\uf0c5 Copy URL"
                    focusPolicy: Qt.NoFocus
                    onClicked: { controller.copyLink(root.hoveredLink); linkPopup.close() }
                }
            }
        }
    }

    KernDialog {
        id: newTableDialog
        objectName: "new-table-dialog"
        title: "New Table"
        modal: true
        anchors.centerIn: parent
        acceptText: "Insert"
        focusAccept: false
        onOpened: rowCount.forceActiveFocus()
        onAccepted: {
            let table = -1
            if (root.insertSourceCursor)
                table = controller.insertTableAtSourceCursor(root.insertCursor, rowCount.value, columnCount.value)
            else if (root.insertAfterTableIndex >= 0)
                table = controller.insertTableAfterTable(root.insertAfterTableIndex, rowCount.value, columnCount.value)
            else if (root.insertVisualCursor)
                table = controller.insertTableAtVisualCursor(root.insertBlock, root.insertCursor, rowCount.value, columnCount.value)
            else
                table = controller.insertTable(root.insertBlock, root.insertCursor, rowCount.value, columnCount.value)
            // Put the caret in the new table's first cell so typing can start at once.
            if (table >= 0 && !root.sourceMode)
                root.queueTableCellFocus(table, 0, 0)
        }
        GridLayout {
            columns: 2
            Label { text: "Rows" }
            NumberField { id: rowCount; from: 1; to: 100; value: 3; onConfirmed: if (newTableDialog.visible) newTableDialog.accept() }
            Label { text: "Columns" }
            NumberField { id: columnCount; from: 1; to: 30; value: 3; onConfirmed: if (newTableDialog.visible) newTableDialog.accept() }
        }
    }

    KernDialog {
        id: discardDialog
        objectName: "discard-dialog"
        title: "Discard unsaved changes?"
        modal: true
        anchors.centerIn: parent
        acceptText: "Discard"
        onAccepted: controller.closeTab(root.pendingCloseTab, true)
    }
    KernDialog {
        id: quitDialog
        objectName: "quit-dialog"
        title: "Quit with unsaved changes?"
        modal: true
        anchors.centerIn: parent
        acceptText: "Discard"
        onAccepted: { root.quitAllowed = true; root.close() }
    }
    KernDialog {
        id: reloadDialog
        objectName: "reload-dialog"
        title: "Discard changes and reload?"
        modal: true
        anchors.centerIn: parent
        acceptText: "Discard"
        onAccepted: controller.reloadCurrent(true)
    }
    KernDialog {
        id: errorDialog
        objectName: "error-dialog"
        title: "kern"
        modal: true
        anchors.centerIn: parent
        rejectText: ""
        Label { text: controller.error; wrapMode: Text.Wrap; width: 440 }
    }
    Connections {
        target: controller
        function onErrorChanged() { if (controller.error) errorDialog.open() }
        function onSourceChanged() { sourceArea.text = controller.sourceText; root.selectedTable = -1 }
        function onUnsupportedFileChanged() {
            if (controller.unsupportedFileMessage) {
                root.commitActive()
                unsupportedFileOverlay.forceActiveFocus()
            } else Qt.callLater(root.focusDocument)
        }
    }

    SplitView {
        anchors.fill: parent
        anchors.topMargin: 4
        orientation: Qt.Horizontal

        Rectangle {
            visible: root.sidebarOpen
            SplitView.preferredWidth: 205
            SplitView.minimumWidth: 130
            color: Qt.alpha(uiTheme.foreground, 0.03)
            ColumnLayout {
                anchors.fill: parent
                spacing: 0
                RowLayout {
                    Layout.fillWidth: true
                    Layout.leftMargin: 8
                    Label {
                        text: controller.projectFolder ? controller.projectFolder.split("/").pop() : "No project"
                        elide: Text.ElideMiddle
                        Layout.fillWidth: true
                        font.pixelSize: 10
                        font.bold: true
                    }
                    ToolButton {
                        text: "\uebdf"
                        implicitWidth: 24
                        implicitHeight: 24
                        focusPolicy: Qt.NoFocus
                        ToolTip.text: "Open project folder"
                        ToolTip.visible: hovered
                        onClicked: folderDialog.open()
                    }
                    ToolButton {
                        text: "\uf15b"
                        visible: !!controller.projectFolder
                        implicitWidth: 24
                        implicitHeight: 24
                        focusPolicy: Qt.NoFocus
                        ToolTip.text: "New file"
                        ToolTip.visible: hovered
                        onClicked: newFileDialog.open()
                    }
                    ToolButton {
                        text: "\uf053"
                        implicitWidth: 24
                        implicitHeight: 24
                        focusPolicy: Qt.NoFocus
                        ToolTip.text: "Collapse files  Ctrl+Shift+B"
                        ToolTip.visible: hovered
                        onClicked: root.sidebarOpen = false
                    }
                }
                ListView {
                    id: fileList
                    objectName: "project-list"
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    clip: true
                    model: controller.projectFiles
                    activeFocusOnTab: true
                    currentIndex: -1
                    highlightMoveDuration: 0
                    // Shift+Up/Down extends a range from the anchor; Enter opens every file in it.
                    property int anchorIndex: -1
                    property string currentPath: ""
                    function rememberCurrent() {
                        const entry = controller.projectFiles[currentIndex]
                        currentPath = entry ? entry.path : ""
                    }
                    function inSelection(i) {
                        return anchorIndex >= 0 && i >= Math.min(anchorIndex, currentIndex)
                            && i <= Math.max(anchorIndex, currentIndex)
                    }
                    function move(target, extend) {
                        if (count === 0) return
                        if (extend && anchorIndex < 0) anchorIndex = Math.max(currentIndex, 0)
                        if (!extend) anchorIndex = -1
                        currentIndex = Math.max(0, Math.min(count - 1, target))
                        rememberCurrent()
                    }
                    function openSelection() {
                        const entries = controller.projectFiles
                        root.commitActive()
                        let openedFile = false
                        if (anchorIndex >= 0 && anchorIndex !== currentIndex) {
                            const last = Math.max(anchorIndex, currentIndex)
                            for (let i = Math.min(anchorIndex, currentIndex); i <= last; ++i)
                                if (!entries[i].directory && !entries[i].info) {
                                    controller.activateProjectEntry(i)
                                    openedFile = true
                                }
                        } else if (entries[currentIndex] && !entries[currentIndex].info) {
                            openedFile = !entries[currentIndex].directory
                            controller.activateProjectEntry(currentIndex)
                        }
                        anchorIndex = -1
                        if (openedFile) Qt.callLater(root.focusDocument)
                    }
                    function restoreCurrent() {
                        const entries = controller.projectFiles
                        let index = entries.findIndex(entry => entry.path === currentPath)
                        if (index < 0) index = entries.length > 0 && currentIndex >= 0 ? 0 : -1
                        currentIndex = index
                        anchorIndex = -1
                    }
                    Connections {
                        target: controller
                        function onProjectFilesChanged() { Qt.callLater(fileList.restoreCurrent) }
                    }
                    highlight: Rectangle {
                        visible: fileList.activeFocus && fileList.anchorIndex < 0
                        color: Qt.alpha(uiTheme.accent, 0.28)
                    }
                    Keys.onPressed: event => {
                        if (event.modifiers & (Qt.ControlModifier | Qt.AltModifier | Qt.MetaModifier)) return
                        const extend = event.modifiers & Qt.ShiftModifier
                        const entries = controller.projectFiles
                        const entry = entries[currentIndex]
                        if (event.key === Qt.Key_Down) move(currentIndex + 1, extend)
                        else if (event.key === Qt.Key_Up) move(currentIndex - 1, extend)
                        else if (event.key === Qt.Key_PageDown) move(currentIndex + 10, extend)
                        else if (event.key === Qt.Key_PageUp) move(currentIndex - 10, extend)
                        else if (event.key === Qt.Key_Home) move(0, extend)
                        else if (event.key === Qt.Key_End) move(count - 1, extend)
                        else if (event.key === Qt.Key_Return || event.key === Qt.Key_Enter) openSelection()
                        else if (event.key === Qt.Key_Right) {
                            if (entry && entry.directory && !entry.expanded) controller.activateProjectEntry(currentIndex)
                        } else if (event.key === Qt.Key_Left) {
                            if (entry && entry.directory && entry.expanded) controller.activateProjectEntry(currentIndex)
                            else if (entry)
                                for (let i = currentIndex - 1; i >= 0; --i)
                                    if (entries[i].depth < entry.depth) { move(i, false); break }
                        } else if (event.key === Qt.Key_Escape) root.focusDocument()
                        else return
                        event.accepted = true
                    }
                    delegate: ItemDelegate {
                        width: ListView.view.width
                        height: 21
                        leftPadding: 8 + modelData.depth * 13
                        topPadding: 0
                        bottomPadding: 0
                        font.pixelSize: 10
                        focusPolicy: Qt.NoFocus
                        enabled: !modelData.info
                        highlighted: !modelData.directory && modelData.path === root.currentDocumentPath
                        text: (modelData.directory ? (modelData.expanded ? "\uf07c " : "\uf07b ") : "\uf15b ") + modelData.name
                        background: Rectangle {
                            color: fileList.inSelection(index) ? Qt.alpha(uiTheme.accent, 0.28)
                                : parent.highlighted ? Qt.alpha(uiTheme.accent, 0.14)
                                : parent.hovered ? Qt.alpha(uiTheme.foreground, 0.05) : "transparent"
                        }
                        objectName: "project-entry-" + modelData.name
                        onClicked: {
                            fileList.currentIndex = index
                            fileList.anchorIndex = -1
                            fileList.rememberCurrent()
                            root.commitActive()
                            controller.activateProjectEntry(index)
                        }
                    }
                }
            }
        }

        Rectangle {
            visible: !root.sidebarOpen
            SplitView.preferredWidth: 26
            SplitView.minimumWidth: 26
            color: Qt.alpha(uiTheme.foreground, 0.03)
            ToolButton {
                anchors.top: parent.top
                anchors.horizontalCenter: parent.horizontalCenter
                text: "\uf054"
                implicitWidth: 24
                implicitHeight: 28
                focusPolicy: Qt.NoFocus
                ToolTip.text: "Expand files  Ctrl+Shift+B"
                ToolTip.visible: hovered
                onClicked: root.sidebarOpen = true
            }
        }

        Item {
            SplitView.fillWidth: true
            SplitView.fillHeight: true
            ColumnLayout {
                anchors.fill: parent
                spacing: 0
                RowLayout {
                    Layout.fillWidth: true
                    implicitHeight: 30
                    spacing: 0
                    Repeater {
                        model: controller.tabs
                        delegate: RowLayout {
                            spacing: 0
                            ToolButton {
                                id: fileTab
                                objectName: "document-tab-" + index
                                text: (modelData.dirty ? "• " : "") + modelData.name
                                focusPolicy: Qt.NoFocus
                                topPadding: 3
                                bottomPadding: 3
                                leftPadding: 10
                                rightPadding: 4
                                background: Rectangle {
                                    color: modelData.active ? Qt.alpha(uiTheme.foreground, 0.07)
                                        : fileTab.hovered ? Qt.alpha(uiTheme.foreground, 0.04) : "transparent"
                                    Rectangle {
                                        visible: modelData.active
                                        anchors { left: parent.left; right: parent.right; bottom: parent.bottom }
                                        height: 2
                                        color: uiTheme.accent
                                    }
                                }
                                onClicked: { root.commitActive(); controller.selectTab(index) }
                            }
                            ToolButton {
                                text: "×"
                                focusPolicy: Qt.NoFocus
                                implicitWidth: 22
                                implicitHeight: 24
                                onClicked: root.closeTab(index)
                            }
                        }
                    }
                    Item { Layout.fillWidth: true }
                    Label {
                        text: "VISUAL"
                        color: !root.sourceMode ? uiTheme.accent : uiTheme.muted
                        font.pixelSize: 10
                        Layout.alignment: Qt.AlignVCenter
                        MouseArea {
                            anchors.fill: parent
                            enabled: controller.hasDocument
                            cursorShape: enabled ? Qt.PointingHandCursor : Qt.ArrowCursor
                            onClicked: modeSwitch.checked = false
                        }
                    }
                    Switch {
                        id: modeSwitch
                        objectName: "mode-switch"
                        enabled: controller.hasDocument
                        checked: root.sourceMode
                        focusPolicy: Qt.NoFocus
                        padding: 0
                        spacing: 0
                        Layout.preferredWidth: 27
                        Layout.preferredHeight: 16
                        Layout.alignment: Qt.AlignVCenter
                        Layout.leftMargin: 7
                        contentItem: Item {}
                        onCheckedChanged: {
                            if (root.sourceMode !== checked) {
                                root.commitActive()
                                root.sourceMode = checked
                            }
                        }
                        indicator: Rectangle {
                            implicitWidth: 27
                            implicitHeight: 13
                            y: (modeSwitch.height - height) / 2
                            radius: 7
                            color: Qt.alpha(uiTheme.foreground, 0.10)
                            border.color: Qt.alpha(uiTheme.foreground, 0.25)
                            Rectangle {
                                width: 9
                                height: 9
                                radius: 5
                                y: 2
                                x: modeSwitch.checked ? parent.width - width - 2 : 2
                                color: uiTheme.accent
                            }
                        }
                    }
                    Label {
                        text: "SOURCE"
                        color: root.sourceMode ? uiTheme.accent : uiTheme.muted
                        font.pixelSize: 10
                        rightPadding: 10
                        Layout.leftMargin: 7
                        Layout.alignment: Qt.AlignVCenter
                        MouseArea {
                            anchors.fill: parent
                            enabled: controller.hasDocument
                            cursorShape: enabled ? Qt.PointingHandCursor : Qt.ArrowCursor
                            onClicked: modeSwitch.checked = true
                        }
                    }
                }

                Rectangle { Layout.fillWidth: true; height: 1; color: Qt.alpha(uiTheme.foreground, 0.13) }

                Item {
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    Label {
                        anchors.centerIn: parent
                        visible: !controller.hasDocument
                        text: "Open a file or create a new document"
                        color: uiTheme.muted
                    }
                    ScrollView {
                        id: blockScroll
                        enabled: !controller.unsupportedFileMessage
                        anchors.fill: parent
                        visible: controller.hasDocument && root.sourceMode
                        TextArea {
                            id: sourceArea
                            objectName: "source-editor"
                            width: blockScroll.availableWidth
                            height: Math.max(implicitHeight, blockScroll.availableHeight)
                            text: controller.sourceText
                            wrapMode: TextEdit.Wrap
                            selectByMouse: true
                            font.family: uiTheme.monoFont
                            font.pixelSize: Math.round(15 * uiTheme.zoomPercent / 100)
                            color: uiTheme.foreground
                            selectionColor: uiTheme.selection
                            selectedTextColor: uiTheme.foreground
                            leftPadding: 24
                            rightPadding: 24
                            onActiveFocusChanged: if (!activeFocus && root.sourceMode && text !== controller.sourceText)
                                controller.setSourceText(text)
                        }
                    }
                    ScrollView {
                        id: previewScroll
                        enabled: !controller.unsupportedFileMessage
                        anchors.fill: parent
                        visible: controller.hasDocument && !root.sourceMode
                        clip: true
                        Column {
                            id: previewBlocks
                            width: Math.max(200, previewScroll.width - 16)
                            leftPadding: 24
                            rightPadding: 24
                            topPadding: 16
                            bottomPadding: 16
                            spacing: 0
                            Repeater {
                                id: blockRepeater
                                model: controller.blocks
                                delegate: Loader {
                                    width: parent.width - 48
                                    sourceComponent: modelData.kind === "table" ? tableComponent : textComponent
                                    onLoaded: {
                                        item.blockData = modelData
                                        item.blockIndex = index
                                    }
                                }
                            }
                        }
                    }
                    // Watches mouse drags without taking them from the text editors beneath. It sits
                    // above them because a handler on an ancestor never sees a press that a child accepts.
                    Item {
                        id: dragOverlay
                        anchors.fill: previewScroll
                        visible: previewScroll.visible
                        z: 10
                        PointHandler {
                            acceptedButtons: Qt.LeftButton
                            acceptedDevices: PointerDevice.Mouse
                            onActiveChanged: if (active) root.selectedTable = -1
                            onPointChanged: {
                                if (!active) return
                                const start = dragOverlay.mapToItem(previewBlocks, point.pressPosition)
                                const current = dragOverlay.mapToItem(previewBlocks, point.position)
                                root.selectTablesInDrag(start.y, current.y)
                            }
                        }
                    }
                    MouseArea {
                        objectName: "document-end-click-area"
                        enabled: controller.hasDocument && !root.sourceMode && !controller.unsupportedFileMessage
                        x: 0
                        y: Math.max(0, previewBlocks.height - previewBlocks.bottomPadding
                                    - (previewScroll.contentItem.contentY || 0))
                        width: Math.max(0, previewScroll.availableWidth - 16)
                        height: Math.max(0, parent.height - y)
                        cursorShape: Qt.IBeamCursor
                        onClicked: root.focusDocumentEnd()
                    }
                    Rectangle {
                        id: unsupportedFileOverlay
                        objectName: "unsupported-file-overlay"
                        anchors.fill: parent
                        visible: controller.unsupportedFileMessage.length > 0
                        focus: visible
                        color: Qt.alpha(uiTheme.background, 0.88)
                        // Keep keyboard input away from the document behind this message.
                        Keys.onPressed: event => event.accepted = true
                        MouseArea { anchors.fill: parent }
                        ColumnLayout {
                            anchors.centerIn: parent
                            width: Math.max(0, Math.min(parent.width - 48, 640))
                            spacing: 12
                            Label {
                                objectName: "unsupported-file-message"
                                Layout.fillWidth: true
                                text: controller.unsupportedFileMessage
                                wrapMode: Text.Wrap
                                horizontalAlignment: Text.AlignHCenter
                                color: uiTheme.foreground
                            }
                            Label {
                                Layout.fillWidth: true
                                text: "Select another file or a document tab to continue."
                                wrapMode: Text.Wrap
                                horizontalAlignment: Text.AlignHCenter
                                color: uiTheme.muted
                            }
                        }
                    }
                }
            }
        }
    }

    Component {
        id: textComponent
        Item {
            id: textBlock
            property var blockData
            property int blockIndex
            readonly property var editor: visualEditor
            width: parent.width
            implicitHeight: Math.max(24, visualEditor.contentHeight + 8)
            TextEdit {
                id: visualEditor
                objectName: "formatted-editor-" + textBlock.blockIndex
                property int sourceBlockIndex: textBlock.blockIndex
                property bool richMarkdown: controller.markdown
                property bool typingBold: false
                property bool typingItalic: false
                property bool explicitTypingStyle: false
                property bool contentDirty: false
                property string originalText: textBlock.blockData
                    ? controller.markdown ? uiTheme.markdownHtml(textBlock.blockData.preview, uiTheme.accent,
                                                                 uiTheme.monoFont, uiTheme.zoomPercent)
                        : textBlock.blockData.preview : ""
                width: parent.width
                text: originalText
                textFormat: textBlock.blockData && textBlock.blockData.previewFormat === "rich"
                    ? TextEdit.RichText : controller.markdown ? TextEdit.RichText : TextEdit.PlainText
                readOnly: textBlock.blockData && textBlock.blockData.previewFormat === "rich" && !controller.markdown
                baseUrl: controller.documentUrl
                wrapMode: TextEdit.Wrap
                selectByMouse: true
                font.family: uiTheme.monoFont
                font.pixelSize: Math.round(15 * uiTheme.zoomPercent / 100)
                color: uiTheme.foreground
                selectionColor: uiTheme.selection
                selectedTextColor: uiTheme.foreground
                activeFocusOnTab: true
                onLinkHovered: link => root.offerLink(visualEditor, link)
                onLinkActivated: link => root.offerLink(visualEditor, link)
                Keys.onPressed: event => {
                    root.handleTableKeys(visualEditor, event)
                    if (!event.accepted) root.handleMarkdownKey(visualEditor, event)
                    if (!event.accepted) root.handleStyledKey(visualEditor, event)
                }
                onTextEdited: contentDirty = true
                function commit() {
                    if (!textBlock.blockData || !contentDirty) return
                    contentDirty = false
                    const replacement = text
                    const index = textBlock.blockIndex
                    if (root.activeEditor === this) root.activeEditor = null
                    if (controller.markdown) controller.editFormattedBlock(index, replacement)
                    else controller.editTextBlock(index, replacement)
                }
                onActiveFocusChanged: {
                    if (activeFocus) root.activeEditor = this
                    else {
                        explicitTypingStyle = false
                        typingBold = false
                        typingItalic = false
                        if (root.activeEditor === this) commit()
                    }
                }
            }
        }
    }

    Component {
        id: tableComponent
        Item {
            id: tableBlock
            property var blockData
            property int blockIndex
            property int selectedRow: 0
            property int selectedCol: 0
            readonly property int columns: blockData ? blockData.columns : 0
            readonly property var rows: blockData ? blockData.rows : []

            function focusCell(row, col) {
                if (row < 0 || row >= rows.length || col < 0 || col >= columns)
                    return false
                const cell = cellRepeater.itemAt(row * columns + col)
                if (cell && cell.cellRow === row && cell.cellCol === col) {
                    cell.focusEditor()
                    return true
                }
                return false
            }

            function runCommand(action) {
                const editor = root.activeFocusItem
                const row = editor && editor.tableIndex === blockData.tableIndex ? editor.rowIndex : selectedRow
                const col = editor && editor.tableIndex === blockData.tableIndex ? editor.columnIndex : selectedCol
                if (editor && editor.tableIndex === blockData.tableIndex) root.activeEditor = editor
                root.executeTableAction(blockData.tableIndex, action, row, col, columns)
            }

            width: parent.width
            implicitHeight: tableColumn.implicitHeight

            Rectangle {
                objectName: "table-selection"
                visible: tableBlock.blockData && root.selectedTable === tableBlock.blockData.tableIndex
                x: tableGrid.x
                y: tableGrid.y
                width: tableGrid.width
                height: tableGrid.height
                z: 5
                color: Qt.alpha(uiTheme.accent, 0.16)
                border.color: uiTheme.accent
                border.width: 2
                radius: 3
            }

            ColumnLayout {
                id: tableColumn
                anchors.left: parent.left
                anchors.right: parent.right
                anchors.top: parent.top
                spacing: 3

                GridLayout {
                    id: tableGrid
                    Layout.fillWidth: true
                    columns: Math.max(1, tableBlock.columns)
                    rowSpacing: 0
                    columnSpacing: 0
                    Repeater {
                        id: cellRepeater
                        model: tableBlock.rows.length * tableBlock.columns
                        delegate: Rectangle {
                            id: cellFrame
                            readonly property int cellRow: Math.floor(index / tableBlock.columns)
                            readonly property int cellCol: index % tableBlock.columns
                            objectName: "table-frame-" + tableBlock.blockData.tableIndex + "-" + cellRow + "-" + cellCol
                            Layout.fillWidth: true
                            Layout.fillHeight: true
                            Layout.preferredWidth: Math.max(75, tableBlock.width / tableBlock.columns)
                            implicitHeight: Math.max(32, cellEditor.contentHeight + 12)
                            color: cellEditor.activeFocus ? Qt.alpha(uiTheme.accent, 0.08)
                                : cellRow === 0 ? Qt.alpha(uiTheme.foreground, 0.035) : "transparent"
                            Rectangle {
                                anchors { top: parent.top; bottom: parent.bottom; right: parent.right }
                                width: 1
                                visible: cellFrame.cellCol < tableBlock.columns - 1
                                color: Qt.alpha(uiTheme.foreground, 0.24)
                            }
                            Rectangle {
                                anchors { left: parent.left; right: parent.right; bottom: parent.bottom }
                                height: 1
                                visible: cellFrame.cellRow < tableBlock.rows.length - 1
                                color: Qt.alpha(uiTheme.foreground, 0.24)
                            }
                            function focusEditor() {
                cellEditor.cursorPosition = 0
                cellEditor.forceActiveFocus()
            }

                            TextEdit {
                                id: cellEditor
                                objectName: "table-cell-" + tableBlock.blockData.tableIndex + "-" + cellRow + "-" + cellCol
                                property bool richMarkdown: true
                                property bool typingBold: false
                                property bool typingItalic: false
                                property bool explicitTypingStyle: false
                                property bool contentDirty: false
                                property int tableIndex: tableBlock.blockData.tableIndex
                                property int rowIndex: cellRow
                                property int columnIndex: cellCol
                                property string originalHtml: uiTheme.markdownHtml(tableBlock.rows[cellRow][cellCol],
                                    uiTheme.accent, uiTheme.monoFont, uiTheme.zoomPercent)
                                anchors.fill: parent
                                anchors.margins: 6
                                text: originalHtml
                                textFormat: TextEdit.RichText
                                baseUrl: controller.documentUrl
                                wrapMode: TextEdit.WrapAnywhere
                                font.family: uiTheme.monoFont
                                font.pixelSize: Math.round(13 * uiTheme.zoomPercent / 100)
                                font.bold: cellRow === 0
                                color: uiTheme.foreground
                                selectionColor: uiTheme.selection
                                selectedTextColor: uiTheme.foreground
                                selectByMouse: true
                                activeFocusOnTab: true
                                horizontalAlignment: tableBlock.blockData && tableBlock.blockData.align[cellCol] === "right"
                                    ? TextEdit.AlignRight
                                    : tableBlock.blockData && tableBlock.blockData.align[cellCol] === "center"
                                      ? TextEdit.AlignHCenter : TextEdit.AlignLeft
                                onLinkHovered: link => root.offerLink(cellEditor, link)
                                onLinkActivated: link => root.offerLink(cellEditor, link)
                                onTextEdited: contentDirty = true

                                function commit() {
                                    if (root.activeEditor === this) root.activeEditor = null
                                    if (contentDirty) {
                                        contentDirty = false
                                        controller.editFormattedCell(tableIndex, cellRow, cellCol, text)
                                    }
                                }

                                function moveTo(row, col, appendRow) {
                                    const currentTable = tableIndex
                                    root.queueTableCellFocus(currentTable, row, col)
                                    commit()
                                    if (appendRow)
                                        controller.tableCommand(currentTable, "row-below", cellRow, cellCol)
                                }

                                function moveOut(direction) {
                                    const currentTable = tableIndex
                                    root.queueAdjacentTextFocus(currentTable, direction)
                                    commit()
                                }

                                onActiveFocusChanged: {
                                    if (activeFocus) {
                                        root.activeEditor = this
                                        tableBlock.selectedRow = cellRow
                                        tableBlock.selectedCol = cellCol
                                    } else {
                                        explicitTypingStyle = false
                                        typingBold = false
                                        typingItalic = false
                                        if (root.activeEditor === this) commit()
                                    }
                                }
                                Keys.onTabPressed: event => {
                                    event.accepted = true
                                    if (cellCol + 1 < tableBlock.columns)
                                        moveTo(cellRow, cellCol + 1, false)
                                    else if (cellRow + 1 < tableBlock.rows.length)
                                        moveTo(cellRow + 1, 0, false)
                                    else
                                        moveTo(cellRow + 1, 0, true)
                                }
                                Keys.onBacktabPressed: event => {
                                    event.accepted = true
                                    if (cellCol > 0) moveTo(cellRow, cellCol - 1, false)
                                    else if (cellRow > 0) moveTo(cellRow - 1, tableBlock.columns - 1, false)
                                }
                                Keys.onPressed: event => {
                                    root.handleStyledKey(cellEditor, event)
                                    if (event.accepted) return
                                    if (event.modifiers & (Qt.ControlModifier | Qt.AltModifier | Qt.MetaModifier)) return
                                    if (event.key === Qt.Key_Up) {
                                        if (cellRow > 0) {
                                            event.accepted = true
                                            moveTo(cellRow - 1, cellCol, false)
                                        } else if (root.adjacentTextBlock(tableIndex, -1) >= 0) {
                                            event.accepted = true
                                            moveOut(-1)
                                        }
                                    } else if (event.key === Qt.Key_Down) {
                                        if (cellRow + 1 < tableBlock.rows.length) {
                                            event.accepted = true
                                            moveTo(cellRow + 1, cellCol, false)
                                        } else if (root.adjacentTextBlock(tableIndex, 1) >= 0) {
                                            event.accepted = true
                                            moveOut(1)
                                        }
                                    } else if ((event.key === Qt.Key_Return || event.key === Qt.Key_Enter)
                                               && cellRow + 1 < tableBlock.rows.length) {
                                        event.accepted = true
                                        moveTo(cellRow + 1, cellCol, false)
                                    } else if ((event.key === Qt.Key_Return || event.key === Qt.Key_Enter)
                                               && cellRow + 1 === tableBlock.rows.length) {
                                        event.accepted = true
                                        moveTo(cellRow + 1, cellCol, true)
                                    }
                                }
                            }
                        }
                    }
                }

                RowLayout {
                    Layout.fillWidth: true
                    spacing: 3
                    Item { Layout.fillWidth: true }
                    TableButton { objectName: "table-insert-row-above"; text: "\uf067 row \uf062"
                        tip: "Insert row above  Ctrl+Alt+Up"; enabled: tableBlock.selectedRow > 0
                        onClicked: tableBlock.runCommand("row-above") }
                    TableButton { objectName: "table-insert-row-below"; text: "\uf067 row \uf063"
                        tip: "Insert row below  Ctrl+Alt+Down"; onClicked: tableBlock.runCommand("row-below") }
                    TableButton { objectName: "table-insert-col-left"; text: "\uf067 col \uf060"
                        tip: "Insert column left  Ctrl+Alt+Left"; onClicked: tableBlock.runCommand("col-before") }
                    TableButton { objectName: "table-insert-col-right"; text: "\uf067 col \uf061"
                        tip: "Insert column right  Ctrl+Alt+Right"; onClicked: tableBlock.runCommand("col-after") }
                    TableButton { objectName: "table-delete-row"; text: "\uf068 row"
                        tip: "Delete row  Ctrl+Alt+Shift+Up"; enabled: tableBlock.selectedRow > 0
                        onClicked: tableBlock.runCommand("row-delete") }
                    TableButton { objectName: "table-delete-col"; text: "\uf068 col"
                        tip: "Delete column  Ctrl+Alt+Shift+Left"; enabled: tableBlock.columns > 1
                        onClicked: tableBlock.runCommand("col-delete") }
                }
            }
        }
    }
}
