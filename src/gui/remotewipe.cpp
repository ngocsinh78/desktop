/*
 * SPDX-FileCopyrightText: 2019 Nextcloud GmbH and Nextcloud contributors
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "remotewipe.h"
#include "folderman.h"

#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkRequest>
#include <QBuffer>

namespace OCC {

Q_LOGGING_CATEGORY(lcRemoteWipe, "nextcloud.gui.remotewipe", QtInfoMsg)

RemoteWipe::RemoteWipe(AccountPtr account, QObject *parent)
    : QObject(parent),
      _account(account),
      _appPassword(QString()),
      _networkManager(nullptr)
{
    QObject::connect(AccountManager::instance(), &AccountManager::accountRemoved,
                     this, [=, this](AccountState *) {
        _accountRemoved = true;
    });
    if (FolderMan::instance()) {
        QObject::connect(this, &RemoteWipe::authorized, FolderMan::instance(),
                         &FolderMan::slotWipeFolderForAccount);
        QObject::connect(FolderMan::instance(), &FolderMan::wipeDone, this,
                         &RemoteWipe::notifyServerSuccessJob);
    }

    QObject::connect(_account.data(), &Account::appPasswordRetrieved, this,
                     &RemoteWipe::startCheckJobWithAppPassword);
}

void RemoteWipe::startCheckJobWithAppPassword(QString pwd){
    if(pwd.isEmpty())
        return;

    _appPassword = pwd;
    QUrl requestUrl = Utility::concatUrlPath(_account->url().toString(),
                                             QLatin1String("/index.php/core/wipe/check"));
    QNetworkRequest request;
    request.setHeader(QNetworkRequest::ContentTypeHeader,
                      "application/x-www-form-urlencoded");
    request.setUrl(requestUrl);
    request.setSslConfiguration(_account->getOrCreateSslConfig());
    auto requestBody = new QBuffer;
    QUrlQuery arguments(QStringLiteral("token=%1").arg(_appPassword));
    requestBody->setData(arguments.query(QUrl::FullyEncoded).toLatin1());
    _networkReplyCheck = _networkManager.post(request, requestBody);
    QObject::connect(&_networkManager, &QNetworkAccessManager::sslErrors,
        _account.data(), &Account::slotHandleSslErrors);
    QObject::connect(_networkReplyCheck, &QNetworkReply::finished, this,
                     &RemoteWipe::checkJobSlot);
}

void RemoteWipe::checkJobSlot()
{
    auto jsonData = _networkReplyCheck->readAll();
    QJsonParseError jsonParseError{};
    QJsonObject json = QJsonDocument::fromJson(jsonData, &jsonParseError).object();
    bool wipe = false;

    //check for errors
    if (_networkReplyCheck->error() != QNetworkReply::NoError ||
            jsonParseError.error != QJsonParseError::NoError) {
        QString errorFromJson = json["error"].toString();
        if (!errorFromJson.isEmpty()) {
            qCWarning(lcRemoteWipe) << QStringLiteral("Error returned from the server: <em>%1<em>")
                                       .arg(errorFromJson.toHtmlEscaped());
        } else if (_networkReplyCheck->error() != QNetworkReply::NoError) {
            qCWarning(lcRemoteWipe) << QStringLiteral("There was an error accessing the 'token' endpoint: <br><em>%1</em>")
                              .arg(_networkReplyCheck->errorString().toHtmlEscaped());
        } else if (jsonParseError.error != QJsonParseError::NoError) {
            qCWarning(lcRemoteWipe) << QStringLiteral("Could not parse the JSON returned from the server: <br><em>%1</em>")
                              .arg(jsonParseError.errorString());
        } else {
            qCWarning(lcRemoteWipe) <<  QStringLiteral("The reply from the server did not contain all expected fields");
        }

    // check for wipe request
    } else if(!json.value("wipe").isUndefined()){
        wipe = json["wipe"].toBool();
    }

    auto manager = AccountManager::instance();
    auto accountState = manager->account(_account->displayName()).data();

    if(wipe){
        /* IMPORTANT - remove later - FIXME MS@2019-12-07 -->
         * TODO: For "Log out" & "Remove account": Remove client CA certs and KEY!
         *
         *       Disabled as long as selecting another cert is not supported by the UI.
         *
         *       Being able to specify a new certificate is important anyway: expiry etc.
         *
         *       We introduce this dirty hack here, to allow deleting them upon Remote Wipe.
         */
        _account->setRemoteWipeRequested_HACK();
        // <-- FIXME MS@2019-12-07

        // delete data
        emit authorized(accountState);

        // delete account
        manager->deleteAccount(accountState);
        manager->save();
    } else {
        // ask user for his credentials again
        accountState->handleInvalidCredentials();
    }

    _networkReplyCheck->deleteLater();
}

void RemoteWipe::notifyServerSuccessJob(AccountState *accountState, bool dataWiped){
    if(_accountRemoved && dataWiped && _account == accountState->account()){
        QUrl requestUrl = Utility::concatUrlPath(_account->url().toString(),
                                                 QLatin1String("/index.php/core/wipe/success"));
        QNetworkRequest request;
        request.setHeader(QNetworkRequest::ContentTypeHeader,
                          "application/x-www-form-urlencoded");
        request.setUrl(requestUrl);
        request.setSslConfiguration(_account->getOrCreateSslConfig());
        auto requestBody = new QBuffer;
        QUrlQuery arguments(QStringLiteral("token=%1").arg(_appPassword));
        requestBody->setData(arguments.query(QUrl::FullyEncoded).toLatin1());
        _networkReplySuccess = _networkManager.post(request, requestBody);
        QObject::connect(_networkReplySuccess, &QNetworkReply::finished, this,
                         &RemoteWipe::notifyServerSuccessJobSlot);
    }
}

void RemoteWipe::notifyServerSuccessJobSlot()
{
    auto jsonData = _networkReplySuccess->readAll();
    QJsonParseError jsonParseError{};
    QJsonObject json = QJsonDocument::fromJson(jsonData, &jsonParseError).object();
    if (_networkReplySuccess->error() != QNetworkReply::NoError ||
            jsonParseError.error != QJsonParseError::NoError) {
        QString errorFromJson = json["error"].toString();
        if (!errorFromJson.isEmpty()) {
            qCWarning(lcRemoteWipe) << QStringLiteral("Error returned from the server: <em>%1</em>")
                              .arg(errorFromJson.toHtmlEscaped());
        } else if (_networkReplySuccess->error() != QNetworkReply::NoError) {
            qCWarning(lcRemoteWipe) << QStringLiteral("There was an error accessing the 'success' endpoint: <br><em>%1</em>")
                              .arg(_networkReplySuccess->errorString().toHtmlEscaped());
        } else if (jsonParseError.error != QJsonParseError::NoError) {
            qCWarning(lcRemoteWipe) << QStringLiteral("Could not parse the JSON returned from the server: <br><em>%1</em>")
                              .arg(jsonParseError.errorString());
        } else {
            qCWarning(lcRemoteWipe) << QStringLiteral("The reply from the server did not contain all expected fields.");
        }
    }

    _networkReplySuccess->deleteLater();
}
}
