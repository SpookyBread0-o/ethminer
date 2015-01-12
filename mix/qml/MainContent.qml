import QtQuick 2.2
import QtQuick.Controls 1.1
import QtQuick.Layouts 1.0
import QtQuick.Controls.Styles 1.1
import CodeEditorExtensionManager 1.0

Rectangle {

	objectName: "mainContent"
	signal keyPressed(variant event)
	focus: true
	Keys.enabled: true
	Keys.onPressed:
	{
		root.keyPressed(event.key);
	}
	anchors.fill: parent
	id: root

	function toggleRightView()
	{
		if (!rightView.visible)
		{
			rightView.show();
			debugModel.updateDebugPanel();
		}
		else
		{
			rightView.hide();
		}
	}

	function ensureRightView()
	{
		if (!rightView.visible)
		{
			rightView.show();
		}
	}

	function hideRightView()
	{
		if (rightView.visible)
		{
			rightView.hide();
		}
	}

	CodeEditorExtensionManager {
		headerView: headerPaneTabs;
		rightView: rightPaneTabs;
		editor: codeEditor
	}

	GridLayout
	{
		anchors.fill: parent
		rows: 2
		flow: GridLayout.TopToBottom
		columnSpacing: 0
		rowSpacing: 0
		Rectangle {
			width: parent.width
			height: 50
			Layout.row: 0
			Layout.fillWidth: true
			Layout.preferredHeight: 50
			id: headerView
			Rectangle
			{
				gradient: Gradient {
					GradientStop { position: 0.0; color: "#f1f1f1" }
					GradientStop { position: 1.0; color: "#d9d7da" }
				}
				id: headerPaneContainer
				anchors.fill: parent
				TabView {
					id: headerPaneTabs
					tabsVisible: false
					antialiasing: true
					anchors.fill: parent
					style: TabViewStyle {
						frameOverlap: 1
						tab: Rectangle {}
						frame: Rectangle { color: "transparent" }
					}
				}
			}

			Rectangle
			{
				color: "transparent"
				width: 100
				height: parent.height
				anchors.top: headerView.top
				anchors.right: headerView.right
				RowLayout
				{
					anchors.fill: parent
					Rectangle {
						color: "transparent"
						anchors.fill: parent
						Button
						{
							anchors.right: parent.right
							anchors.rightMargin: 15
							anchors.verticalCenter: parent.verticalCenter
							id: debugImg
							iconSource: "qrc:/qml/img/bugiconinactive.png"
							action: debugRunActionIcon
						}
						Action {
							id: debugRunActionIcon
							shortcut: "F5"
							onTriggered: {
								mainContent.ensureRightView();
								debugModel.debugDeployment();
							}
							enabled: codeModel.hasContract && !debugModel.running;
							onEnabledChanged: {
								if (enabled)
									debugImg.iconSource = "qrc:/qml/img/bugiconactive.png"
								else
									debugImg.iconSource = "qrc:/qml/img/bugiconinactive.png"
							}
						}
					}
				}
			}
		}

		SplitView {
			resizing: false
			Layout.row: 1
			orientation: Qt.Horizontal;
			Layout.fillWidth: true
			Layout.preferredHeight: root.height - headerView.height;
			Rectangle {

				anchors.top: parent.top
				id: contentView
				width: parent.width
				height: parent.height * 0.7

				Item {
					anchors.fill: parent
					Rectangle {
						id: lineColumn
						property int rowHeight: codeEditor.font.pixelSize + 3
						color: "#202020"
						width: 50
						height: parent.height
						Column {
							y: -codeEditor.flickableItem.contentY + 4
							width: parent.width
							Repeater {
								model: Math.max(codeEditor.lineCount + 2, (lineColumn.height/lineColumn.rowHeight))
								delegate: Text {
									id: text
									color: codeEditor.textColor
									font: codeEditor.font
									width: lineColumn.width - 4
									horizontalAlignment: Text.AlignRight
									verticalAlignment: Text.AlignVCenter
									height: lineColumn.rowHeight
									renderType: Text.NativeRendering
									text: index + 1
								}
							}
						}
					}

					TextArea {
						id: codeEditor
						textColor: "#EEE8D5"
						style: TextAreaStyle {
							backgroundColor: "#002B36"
						}

						anchors.left: lineColumn.right
						anchors.right: parent.right
						anchors.top: parent.top
						anchors.bottom: parent.bottom
						wrapMode: TextEdit.NoWrap
						frameVisible: false

						height: parent.height
						font.family: "Monospace"
						font.pointSize: 12
						width: parent.width
						//anchors.centerIn: parent
						tabChangesFocus: false
						Keys.onPressed: {
							if (event.key === Qt.Key_Tab) {
								codeEditor.insert(codeEditor.cursorPosition, "\t");
								event.accepted = true;
							}
						}
					}
				}
			}

			Rectangle {
				Keys.onEscapePressed:
				{
					hide();
				}
				visible: false;
				id: rightView;
				property real panelRelWidth: 0.38
				function show() {
					visible = true;
					contentView.width = parent.width * (1 - 0.38)
				}

				function hide() {
					visible = false;
					contentView.width = parent.width;
				}

				height: parent.height;
				width: Layout.minimumWidth
				Layout.minimumWidth: parent.width * 0.38
				Rectangle {
					anchors.fill: parent;
					id: rightPaneView
					TabView {
						id: rightPaneTabs
						tabsVisible: true
						antialiasing: true
						anchors.fill: parent
						style: TabViewStyle {
							frameOverlap: 1

							tabBar:
								Rectangle {
									color: "#ededed"
									id: background

								}
							tab: Rectangle {
								color: "#ededed"
								implicitWidth: 80
								implicitHeight: 20
								radius: 2
								Text {
									anchors.centerIn: parent
									text: styleData.title
									color: styleData.selected ? "#7da4cd" : "#202020"
								}
							}
							frame: Rectangle {
							}
						}
					}
				}
			}
		}
	}
}




