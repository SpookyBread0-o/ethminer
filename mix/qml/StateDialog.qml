import QtQuick 2.2
import QtQuick.Controls 1.1
import QtQuick.Dialogs 1.2
import QtQuick.Layouts 1.1
import QtQuick.Window 2.0
import QtQuick.Controls.Styles 1.3
import org.ethereum.qml.QEther 1.0
import "js/QEtherHelper.js" as QEtherHelper
import "js/TransactionHelper.js" as TransactionHelper
import "."

Dialog {
	id: modalStateDialog
	modality: Qt.ApplicationModal

	width: 630
	height: 480
	title: qsTr("Edit State")
	visible: false

	property alias stateTitle: titleField.text
	property alias isDefault: defaultCheckBox.checked
	property alias model: transactionsModel
	property alias transactionDialog: transactionDialog
	property int stateIndex
	property var stateTransactions: []
	property var stateAccounts: []
	signal accepted

	StateDialogStyle {
		id: stateDialogStyle
	}

	function open(index, item, setDefault) {
		stateIndex = index;
		stateTitle = item.title;
		transactionsModel.clear();

		stateTransactions = [];
		var transactions = item.transactions;
		for (var t = 0; t < transactions.length; t++) {
			transactionsModel.append(item.transactions[t]);
			stateTransactions.push(item.transactions[t]);
		}

		accountsModel.clear();
		stateAccounts = [];
		for (var k = 0; k < item.accounts.length; k++)
		{
			accountsModel.append(item.accounts[k]);
			stateAccounts.push(item.accounts[k]);
		}

		visible = true;
		isDefault = setDefault;
		titleField.focus = true;
		defaultCheckBox.enabled = !isDefault;
		forceActiveFocus();
	}

	function acceptAndClose() {
		close();
		accepted();
	}

	function close() {
		visible = false;
	}

	function getItem() {
		var item = {
			title: stateDialog.stateTitle,
			transactions: [],
			accounts: []
		}
		item.transactions = stateTransactions;
		item.accounts = stateAccounts;
		return item;
	}
	contentItem: Rectangle {
		color: stateDialogStyle.generic.backgroundColor
		Rectangle {
			color: stateDialogStyle.generic.backgroundColor
			anchors.top: parent.top
			anchors.margins: 10
			anchors.fill: parent
			ColumnLayout {
				anchors.fill: parent
				anchors.margins: 10
				ColumnLayout {
					id: dialogContent
					anchors.top: parent.top
					RowLayout
					{
						Layout.fillWidth: true
						DefaultLabel {
							Layout.preferredWidth: 85
							text: qsTr("Title")
						}
						DefaultTextField
						{
							id: titleField
							Layout.fillWidth: true
						}
					}

					CommonSeparator
					{
						Layout.fillWidth: true
					}

					RowLayout
					{
						Layout.fillWidth: true

						Rectangle
						{
							Layout.preferredWidth: 85
							DefaultLabel {
								id: accountsLabel
								Layout.preferredWidth: 85
								text: qsTr("Accounts")
							}

							Button
							{
								anchors.top: accountsLabel.bottom
								anchors.topMargin: 10
								iconSource: "qrc:/qml/img/plus.png"
								action: newAccountAction
							}

							Action {
								id: newAccountAction
								tooltip: qsTr("Add new Account")
								onTriggered:
								{
									var account = stateListModel.newAccount("1000000", QEther.Ether);
									stateAccounts.push(account);
									accountsModel.append(account);
								}
							}
						}

						MessageDialog
						{
							id: alertAlreadyUsed
							text: qsTr("This account is in use. You cannot remove it. The first account is used to deploy config contract and cannot be removed.")
							icon: StandardIcon.Warning
							standardButtons: StandardButton.Ok
						}

						TableView
						{
							id: accountsView
							Layout.fillWidth: true
							model: accountsModel
							headerVisible: false
							TableViewColumn {
								role: "name"
								title: qsTr("Name")
								width: 150
								delegate: Item {
									RowLayout
									{
										height: 25
										width: parent.width
										Button
										{
											iconSource: "qrc:/qml/img/delete_sign.png"
											action: deleteAccountAction
										}

										Action {
											id: deleteAccountAction
											tooltip: qsTr("Delete Account")
											onTriggered:
											{
												if (transactionsModel.isUsed(stateAccounts[styleData.row].secret))
													alertAlreadyUsed.open();
												else
												{
													stateAccounts.splice(styleData.row, 1);
													accountsModel.remove(styleData.row);
												}
											}
										}

										DefaultTextField {
											anchors.verticalCenter: parent.verticalCenter
											onTextChanged: {
												if (styleData.row > -1)
													stateAccounts[styleData.row].name = text;
											}
											text:  {
												return styleData.value
											}
										}
									}
								}
							}

							TableViewColumn {
								role: "balance"
								title: qsTr("Balance")
								width: 200
								delegate: Item {
									Ether {
										id: balanceField
										edit: true
										displayFormattedValue: false
										value: styleData.value
									}
								}
							}
							rowDelegate:
								Rectangle {
								color: styleData.alternate ? "transparent" : "#f0f0f0"
								height: 30;
							}
						}
					}

					CommonSeparator
					{
						Layout.fillWidth: true
					}

					RowLayout
					{
						Layout.fillWidth: true
						DefaultLabel {
							Layout.preferredWidth: 85
							text: qsTr("Default")
						}
						CheckBox {
							id: defaultCheckBox
							Layout.fillWidth: true
						}
					}

					CommonSeparator
					{
						Layout.fillWidth: true
					}

					RowLayout
					{
						Layout.fillWidth: true

						Rectangle
						{
							Layout.preferredWidth: 85
							DefaultLabel {
								id: transactionsLabel
								Layout.preferredWidth: 85
								text: qsTr("Transactions")
							}

							Button
							{
								anchors.top: transactionsLabel.bottom
								anchors.topMargin: 10
								iconSource: "qrc:/qml/img/plus.png"
								action: newTrAction
							}

							Action {
								id: newTrAction
								tooltip: qsTr("Create a new transaction")
								onTriggered: transactionsModel.addTransaction()
							}
						}

						TableView
						{
							id: transactionsView
							Layout.fillWidth: true
							model: transactionsModel
							headerVisible: false
							TableViewColumn {
								role: "name"
								title: qsTr("Name")
								width: 150
								delegate: Item {
									RowLayout
									{
										height: 30
										width: parent.width
										Button
										{
											iconSource: "qrc:/qml/img/delete_sign.png"
											action: deleteTransactionAction
										}

										Action {
											id: deleteTransactionAction
											tooltip: qsTr("Delete")
											onTriggered: transactionsModel.deleteTransaction(styleData.row)
										}

										Button
										{
											iconSource: "qrc:/qml/img/edit.png"
											action: editAction
											visible: styleData.row >= 0 ? !transactionsModel.get(styleData.row).stdContract : false
											width: 10
											height: 10
											Action {
												id: editAction
												tooltip: qsTr("Edit")
												onTriggered: transactionsModel.editTransaction(styleData.row)
											}
										}

										DefaultLabel {
											Layout.preferredWidth: 150
											text: styleData.row >= 0 ? transactionsModel.get(styleData.row).functionId : ""
										}
									}
								}
							}
							rowDelegate:
								Rectangle {
								color: styleData.alternate ? "transparent" : "#f0f0f0"
								height: 30;
							}
						}
					}

				}

				RowLayout
				{
					anchors.bottom: parent.bottom
					anchors.right: parent.right;

					Button {
						text: qsTr("OK");
						onClicked: {
							close();
							accepted();
						}
					}
					Button {
						text: qsTr("Cancel");
						onClicked: close();
					}
					Button {
						text: qsTr("Delete");
						onClicked: {
							projectModel.stateListModel.deleteState(stateIndex);
							close();
						}
					}
				}

				ListModel {
					id: accountsModel

					function removeAccount(_i)
					{
						accountsModel.remove(_i);
						stateAccounts.splice(_i, 1);
					}
				}

				ListModel {
					id: transactionsModel

					function editTransaction(index) {
						transactionDialog.stateAccounts = stateAccounts;
						transactionDialog.open(index, transactionsModel.get(index));
					}

					function addTransaction() {
						// Set next id here to work around Qt bug
						// https://bugreports.qt-project.org/browse/QTBUG-41327
						// Second call to signal handler would just edit the item that was just created, no harm done
						var item = TransactionHelper.defaultTransaction();
						transactionDialog.stateAccounts = stateAccounts;
						transactionDialog.open(transactionsModel.count, item);
					}

					function deleteTransaction(index) {
						stateTransactions.splice(index, 1);
						transactionsModel.remove(index);
					}

					function isUsed(secret)
					{
						for (var i in stateTransactions)
						{
							if (stateTransactions[i].sender === secret)
								return true;
						}
						return false;
					}
				}

				TransactionDialog
				{
					id: transactionDialog
					onAccepted:
					{
						var item = transactionDialog.getItem();

						if (transactionDialog.transactionIndex < transactionsModel.count) {
							transactionsModel.set(transactionDialog.transactionIndex, item);
							stateTransactions[transactionDialog.transactionIndex] = item;
						} else {
							transactionsModel.append(item);
							stateTransactions.push(item);
						}
					}
				}
			}
		}
	}
}
