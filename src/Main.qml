import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Material
import QtQuick.Layouts

ApplicationWindow {
    id: win
    width: 1040
    height: 860
    minimumWidth: 860
    minimumHeight: 700
    visible: true
    title: qsTr("AK820")
    color: "#0e0e10"

    Material.theme: Material.Dark
    Material.accent: theme.accent
    Material.roundedScale: theme.radius

    property bool helpVisible: false
    property bool colorLock: false
    property int red: 255
    property int green: 255
    property int blue: 255
    property date now: new Date()

    function hsvToRgb(hue, saturation, value) {
        const section = hue * 6
        const index = Math.floor(section)
        const fraction = section - index
        const p = value * (1 - saturation)
        const q = value * (1 - fraction * saturation)
        const t = value * (1 - (1 - fraction) * saturation)
        let r = 0
        let g = 0
        let b = 0
        switch (index % 6) {
        case 0: r = value; g = t; b = p; break
        case 1: r = q; g = value; b = p; break
        case 2: r = p; g = value; b = t; break
        case 3: r = p; g = q; b = value; break
        case 4: r = t; g = p; b = value; break
        default: r = value; g = p; b = q; break
        }
        return { r: Math.round(r * 255), g: Math.round(g * 255), b: Math.round(b * 255) }
    }

    function rgbToHsv(r, g, b) {
        r /= 255
        g /= 255
        b /= 255
        const max = Math.max(r, g, b)
        const min = Math.min(r, g, b)
        const delta = max - min
        let hue = 0
        if (delta !== 0) {
            if (max === r)
                hue = ((g - b) / delta) % 6
            else if (max === g)
                hue = (b - r) / delta + 2
            else
                hue = (r - g) / delta + 4
            hue /= 6
            if (hue < 0)
                hue += 1
        }
        return { hue: hue, saturation: max === 0 ? 0 : delta / max, value: max }
    }

    function hexColor() {
        function byte(value) {
            return Math.round(value).toString(16).padStart(2, "0")
        }
        return "#" + (byte(red) + byte(green) + byte(blue)).toUpperCase()
    }

    function commitFromWheel() {
        if (colorLock)
            return
        colorLock = true
        const color = hsvToRgb(colorWheel.hue, colorWheel.saturation, colorWheel.value)
        red = color.r
        green = color.g
        blue = color.b
        shadeSlider.value = Math.round(colorWheel.value * 255)
        if (!hexField.activeFocus)
            hexField.text = hexColor()
        colorLock = false
    }

    function commitFromRgb() {
        if (colorLock)
            return
        colorLock = true
        const hsv = rgbToHsv(red, green, blue)
        colorWheel.hue = hsv.hue
        colorWheel.saturation = hsv.saturation
        colorWheel.value = hsv.value
        shadeSlider.value = Math.round(hsv.value * 255)
        colorLock = false
    }

    function applyHex(entry, allowShort) {
        let raw = entry.trim()
        if (raw.charAt(0) === "#")
            raw = raw.slice(1)
        if (allowShort && /^[0-9a-fA-F]{3}$/.test(raw))
            raw = raw.charAt(0) + raw.charAt(0) + raw.charAt(1) + raw.charAt(1) + raw.charAt(2) + raw.charAt(2)
        if (!/^[0-9a-fA-F]{6}$/.test(raw))
            return false
        const color = parseInt(raw, 16)
        red = (color >> 16) & 255
        green = (color >> 8) & 255
        blue = color & 255
        commitFromRgb()
        return true
    }

    function stepName(level, names) {
        translator.language
        const index = Math.max(0, Math.min(names.length - 1, Math.round(level)))
        return names[index]
    }

    function saveToKeyboard() {
        backend.save(modeBox.currentIndex, red, green, blue, rainbowBox.checked,
                     Math.round(brightnessSlider.value), Math.round(speedSlider.value),
                     directionBox.currentIndex, sleepBox.currentIndex)
    }

    Shortcut { sequence: "?"; context: Qt.ApplicationShortcut; onActivated: win.helpVisible = !win.helpVisible }
    Shortcut { sequence: "Escape"; context: Qt.ApplicationShortcut; onActivated: win.helpVisible = false }
    Shortcut { sequence: "Q"; context: Qt.ApplicationShortcut; onActivated: Qt.quit() }
    Shortcut { sequence: "Space"; context: Qt.ApplicationShortcut; onActivated: win.saveToKeyboard() }
    Shortcut { sequence: "S"; context: Qt.ApplicationShortcut; onActivated: backend.sendScreen() }
    Shortcut { sequence: "C"; context: Qt.ApplicationShortcut; onActivated: backend.syncClock() }
    Shortcut { sequence: "I"; context: Qt.ApplicationShortcut; onActivated: backend.chooseImage() }

    Connections {
        target: translator
        function onChanged() {
            win.reloadBox(modeBox, backend.modeNames())
            win.reloadBox(directionBox, backend.directionNames())
            win.reloadBox(sleepBox, backend.sleepNames())
        }
    }

    function reloadBox(box, model) {
        const index = Math.max(0, box.currentIndex)
        box.model = model
        box.currentIndex = index
    }

    component ColorWheel: Item {
        id: wheel
        implicitWidth: 200
        implicitHeight: 200
        property real hue: 0
        property real saturation: 0
        property real value: 1

        function paint(ctx) {
            ctx.clearRect(0, 0, width, height)
            const radius = width / 2 - 8
            const center = width / 2
            for (let y = 0; y < height; y += 2) {
                for (let x = 0; x < width; x += 2) {
                    const dx = x + 1 - center
                    const dy = y + 1 - center
                    const dist = Math.sqrt(dx * dx + dy * dy) / radius
                    if (dist > 1)
                        continue
                    let hue = Math.atan2(dy, dx) / (Math.PI * 2)
                    if (hue < 0)
                        hue += 1
                    const color = win.hsvToRgb(hue, dist, 1)
                    ctx.fillStyle = Qt.rgba(color.r / 255, color.g / 255, color.b / 255, 1)
                    ctx.fillRect(x, y, 2, 2)
                }
            }
        }

        function pick(x, y) {
            const center = width / 2
            const radius = width / 2 - 8
            const dx = x - center
            const dy = y - center
            let hue = Math.atan2(dy, dx) / (Math.PI * 2)
            if (hue < 0)
                hue += 1
            wheel.hue = hue
            wheel.saturation = Math.min(1, Math.sqrt(dx * dx + dy * dy) / radius)
            win.commitFromWheel()
        }

        Canvas {
            id: disc
            anchors.fill: parent
            onPaint: wheel.paint(getContext("2d"))
        }

        Rectangle {
            anchors.centerIn: parent
            width: parent.width - 16
            height: parent.height - 16
            radius: width / 2
            color: "black"
            opacity: 1 - wheel.value
        }

        Rectangle {
            width: 16
            height: 16
            radius: 8
            border.color: "white"
            border.width: 2
            color: "#00000000"
            x: wheel.width / 2 + Math.cos(wheel.hue * Math.PI * 2) * wheel.saturation * (wheel.width / 2 - 8) - width / 2
            y: wheel.height / 2 + Math.sin(wheel.hue * Math.PI * 2) * wheel.saturation * (wheel.width / 2 - 8) - height / 2
        }

        MouseArea {
            anchors.fill: parent
            onPressed: mouse => wheel.pick(mouse.x, mouse.y)
            onPositionChanged: mouse => { if (pressed) wheel.pick(mouse.x, mouse.y) }
        }
    }

    Item {
        anchors.fill: parent
        LayoutMirroring.enabled: Qt.application.layoutDirection === Qt.RightToLeft
        LayoutMirroring.childrenInherit: true

        ColumnLayout {
            anchors.fill: parent
            anchors.margins: 20
            spacing: 12

            Label { text: qsTr("AK820"); font.pixelSize: 22 }
            Label {
                text: backend.status
                wrapMode: Text.WordWrap
                Layout.fillWidth: true
                color: backend.connected ? "#d7d7dc" : "#ffb020"
            }

            RowLayout {
                Layout.fillWidth: true
                Layout.fillHeight: true
                spacing: 28

                ColumnLayout {
                    Layout.fillWidth: true
                    Layout.alignment: Qt.AlignTop
                    spacing: 8
                    Label { text: qsTr("Lighting"); font.pixelSize: 16 }
                    Label { text: qsTr("Mode") }
                    ComboBox { id: modeBox; Layout.fillWidth: true }

                    RowLayout {
                        Layout.fillWidth: true
                        spacing: 16
                        ColorWheel { id: colorWheel }
                        ColumnLayout {
                            Layout.fillWidth: true
                            spacing: 8
                            Rectangle {
                                width: 56
                                height: 56
                                radius: Math.min(8, theme.radius)
                                color: Qt.rgba(win.red / 255, win.green / 255, win.blue / 255, 1)
                                border.color: "#3a3a3e"
                            }
                            TextField {
                                id: hexField
                                Layout.fillWidth: true
                                placeholderText: qsTr("Hex")
                                text: "#FFFFFF"
                                onTextEdited: win.applyHex(text, false)
                                onEditingFinished: {
                                    win.applyHex(text, true)
                                    text = win.hexColor()
                                }
                            }
                            Label { text: qsTr("Shade") }
                            Slider {
                                id: shadeSlider
                                from: 0
                                to: 255
                                value: 255
                                Layout.fillWidth: true
                                onMoved: {
                                    colorWheel.value = value / 255
                                    win.commitFromWheel()
                                }
                            }
                        }
                    }

                    RowLayout {
                        Label { text: qsTr("Brightness"); Layout.preferredWidth: 100 }
                        Slider {
                            id: brightnessSlider
                            from: 0
                            to: 5
                            stepSize: 1
                            snapMode: Slider.SnapAlways
                            value: 3
                            Layout.fillWidth: true
                        }
                        Label {
                            text: win.stepName(brightnessSlider.value, [qsTr("Darkest"), qsTr("Very low"), qsTr("Low"), qsTr("Medium"), qsTr("High"), qsTr("Brightest")])
                            Layout.preferredWidth: 110
                        }
                    }
                    RowLayout {
                        Label { text: qsTr("Speed"); Layout.preferredWidth: 100 }
                        Slider {
                            id: speedSlider
                            from: 0
                            to: 5
                            stepSize: 1
                            snapMode: Slider.SnapAlways
                            value: 2
                            Layout.fillWidth: true
                        }
                        Label {
                            text: win.stepName(speedSlider.value, [qsTr("Slowest"), qsTr("Slow"), qsTr("Medium"), qsTr("Fast"), qsTr("Very fast"), qsTr("Fastest")])
                            Layout.preferredWidth: 110
                        }
                    }
                    Label { text: qsTr("Direction") }
                    ComboBox { id: directionBox; Layout.fillWidth: true }
                    CheckBox { id: rainbowBox; text: qsTr("Rainbow") }
                    Label { text: qsTr("Sleep") }
                    ComboBox { id: sleepBox; Layout.fillWidth: true }
                    Button {
                        text: qsTr("Save to keyboard")
                        highlighted: true
                        enabled: !backend.busy
                        onClicked: win.saveToKeyboard()
                        Layout.fillWidth: true
                    }
                }

                ColumnLayout {
                    Layout.fillWidth: true
                    Layout.alignment: Qt.AlignTop
                    Layout.maximumWidth: 360
                    spacing: 8
                    Label { text: qsTr("Keyboard screen"); font.pixelSize: 16 }
                    Label { text: qsTr("128 × 128"); color: "#9a9aa2" }

                    Rectangle {
                        width: 200
                        height: 200
                        radius: Math.min(8, theme.radius)
                        color: "black"
                        border.color: "#3a3a3e"
                        border.width: 2

                        Image {
                            anchors.fill: parent
                            anchors.margins: 10
                            source: backend.imageUrl
                            fillMode: Image.PreserveAspectCrop
                            visible: backend.imageUrl.length > 0
                        }

                        Item {
                            anchors.fill: parent
                            anchors.margins: 16
                            visible: backend.imageUrl.length === 0

                            Canvas {
                                id: clockFace
                                anchors.fill: parent
                                onPaint: {
                                    const ctx = getContext("2d")
                                    ctx.clearRect(0, 0, width, height)
                                    const cx = width / 2
                                    const cy = height / 2
                                    const radius = Math.min(width, height) / 2 - 8
                                    ctx.strokeStyle = "#f2f2f2"
                                    ctx.lineWidth = 2
                                    ctx.beginPath()
                                    ctx.arc(cx, cy, radius, 0, Math.PI * 2)
                                    ctx.stroke()
                                    const moment = win.now
                                    const seconds = moment.getSeconds()
                                    const minutes = moment.getMinutes() + seconds / 60
                                    const hours = (moment.getHours() % 12) + minutes / 60
                                    function hand(turns, length, width) {
                                        const angle = (turns - 0.25) * Math.PI * 2
                                        ctx.strokeStyle = "#f2f2f2"
                                        ctx.lineWidth = width
                                        ctx.lineCap = "round"
                                        ctx.beginPath()
                                        ctx.moveTo(cx, cy)
                                        ctx.lineTo(cx + Math.cos(angle) * radius * length, cy + Math.sin(angle) * radius * length)
                                        ctx.stroke()
                                    }
                                    hand(hours / 12, 0.52, 3)
                                    hand(minutes / 60, 0.74, 2)
                                    hand(seconds / 60, 0.82, 1)
                                }
                            }

                        }
                    }

                    Label {
                        Layout.fillWidth: true
                        wrapMode: Text.WordWrap
                        text: backend.imageUrl.length === 0
                              ? qsTr("No picture loaded. The keyboard shows its clock.")
                              : (backend.imageOnKeyboard
                                 ? qsTr("This picture is on the keyboard.")
                                 : qsTr("Ready to send to the keyboard."))
                    }
                    TextField {
                        Layout.fillWidth: true
                        placeholderText: qsTr("Image path")
                        text: backend.imagePath
                        onEditingFinished: backend.setImagePath(text)
                    }
                    Button { text: qsTr("Choose image"); enabled: !backend.busy; onClicked: backend.chooseImage(); Layout.fillWidth: true }
                    Button { text: qsTr("Send to screen"); enabled: !backend.busy; onClicked: backend.sendScreen(); Layout.fillWidth: true }
                    Button { text: qsTr("Sync clock"); enabled: !backend.busy; onClicked: backend.syncClock(); Layout.fillWidth: true }
                    Item { Layout.fillHeight: true }
                }
            }
        }

        Rectangle {
            anchors.fill: parent
            visible: win.helpVisible
            color: "#dd0e0e10"
            ColumnLayout {
                anchors.centerIn: parent
                width: Math.min(520, parent.width - 48)
                spacing: 8
                Label { text: qsTr("Shortcuts"); font.pixelSize: 18 }
                Label { text: "Space  " + qsTr("Save lighting and sleep to the keyboard"); wrapMode: Text.WordWrap; Layout.fillWidth: true }
                Label { text: "S  " + qsTr("Send the chosen image to the screen"); wrapMode: Text.WordWrap; Layout.fillWidth: true }
                Label { text: "C  " + qsTr("Sync the clock"); wrapMode: Text.WordWrap; Layout.fillWidth: true }
                Label { text: "I  " + qsTr("Choose an image"); wrapMode: Text.WordWrap; Layout.fillWidth: true }
                Label { text: "?  " + qsTr("Show this list") }
                Label { text: "Q  " + qsTr("Quit") }
            }
            MouseArea { anchors.fill: parent; onClicked: win.helpVisible = false }
        }
    }

    Timer {
        interval: 1000
        running: backend.imageUrl.length === 0
        repeat: true
        onTriggered: {
            win.now = new Date()
            clockFace.requestPaint()
        }
    }

    Component.onCompleted: {
        reloadBox(modeBox, backend.modeNames())
        reloadBox(directionBox, backend.directionNames())
        reloadBox(sleepBox, backend.sleepNames())
        modeBox.currentIndex = 1
        commitFromRgb()
        clockFace.requestPaint()
        backend.refresh()
    }
}
