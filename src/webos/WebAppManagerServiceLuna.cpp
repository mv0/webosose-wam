// Copyright (c) 2014-2018 LG Electronics, Inc.
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
// http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.
//
// SPDX-License-Identifier: Apache-2.0


#include "WebAppManagerServiceLuna.h"

#include "LogManager.h"
#include <QByteArray>
#include <QJsonArray>
#include <QStringList>
#include "webos/public/runtime.h"
#include "webos/webview_base.h"
#include <string>
#include <vector>

// just to save some typing, the template filled out with the name of this class
#define QCB(FUNC) bus_callback_qjson<WebAppManagerServiceLuna, &WebAppManagerServiceLuna::FUNC>
#define QCB_subscription(FUNC) bus_subscription_callback_qjson<WebAppManagerServiceLuna, &WebAppManagerServiceLuna::FUNC>
#define LS2_METHOD_ENTRY(FUNC) {#FUNC, QCB(FUNC)}
#define LS2_SUBSCRIPTION_ENTRY(FUNC) {#FUNC, QCB_subscription(FUNC)}

#define GET_LS2_SERVER_STATUS(FUNC, PARAMS) call<WebAppManagerServiceLuna, &WebAppManagerServiceLuna::FUNC>("luna://com.palm.lunabus/signal/registerServerStatus", PARAMS, this)
#define LS2_CALL(FUNC, SERVICE, PARAMS) call<WebAppManagerServiceLuna, &WebAppManagerServiceLuna::FUNC>(SERVICE, PARAMS, this)

static void logJsonTruncated(const char* funcName, const QJsonObject& request)
{
    QJsonDocument doc(request);
    QByteArray requestBuffer = doc.toJson(QJsonDocument::Compact);
    const uint32_t chunkSize = 255;
    const uint32_t chunksCount = requestBuffer.size() % chunkSize ? (requestBuffer.size() / chunkSize) + 1 : requestBuffer.size() / chunkSize;
    for (int i = 0, part = 1; i < requestBuffer.size(); i += chunkSize, part++) {
        const auto& chunk = requestBuffer.mid(i, chunkSize);
        LOG_INFO(MSGID_WAM_DEBUG, 0, ">>>>>>> WebAppManagerServiceLuna::%s [%d/%d] request:\"%s\"", funcName, part, chunksCount, chunk.toStdString().c_str());
    }
}

LSMethod WebAppManagerServiceLuna::s_methods[] = {
    LS2_METHOD_ENTRY(launchApp),
    LS2_METHOD_ENTRY(killApp),
    LS2_METHOD_ENTRY(pauseApp),
    LS2_METHOD_ENTRY(closeAllApps),
    LS2_METHOD_ENTRY(setInspectorEnable),
    LS2_METHOD_ENTRY(logControl),
    LS2_METHOD_ENTRY(discardCodeCache),
    LS2_METHOD_ENTRY(getWebProcessSize),
    LS2_METHOD_ENTRY(clearBrowsingData),
    LS2_SUBSCRIPTION_ENTRY(listRunningApps),
    LS2_SUBSCRIPTION_ENTRY(webProcessCreated),
    { 0, 0 }
};

WebAppManagerServiceLuna::WebAppManagerServiceLuna()
    : m_clearedCache(false)
    , m_bootDone(false)
    , m_debugLevel("release")
{
}

WebAppManagerServiceLuna::~WebAppManagerServiceLuna()
{
}

bool WebAppManagerServiceLuna::startService()
{
    return PalmServiceBase::startService();
}

QJsonObject WebAppManagerServiceLuna::launchApp(QJsonObject request)
{
    logJsonTruncated(__func__, request);
    int errCode;
    std::string errMsg;
    QJsonObject reply;

    if (  !request["appDesc"].isObject()
       || (request.contains("parameters") && !request["parameters"].isObject())
       || (request.contains("launchingAppId") && !request["launchingAppId"].isString())
       || (request.contains("launchingProcId") && !request["launchingProcId"].isString())
       || !request["instanceId"].isString()) {
        reply["returnValue"] = false;
        reply["errorCode"] = ERR_CODE_LAUNCHAPP_MISS_PARAM;
        reply["errorText"] = QString::fromStdString(err_missParam);
        return reply;
    }

    QJsonDocument doc(request["parameters"].toObject());
    QJsonObject jsonParams = doc.object();
    if(request["launchHidden"].toBool()) {
        jsonParams["launchedHidden"] = true;
    }

    // if "preload" parameter is not "full" or "partial" or "minimal", there is no preload parameter.
    if (request["preload"].isString()) {
        jsonParams["preload"] = request["preload"].toString();
    }

    if(request["keepAlive"].toBool()) {
        jsonParams["keepAlive"] = true;
    }

    QString instanceId = request["instanceId"].toString();
    if (!isValidInstanceId(instanceId)) {
        reply["returnValue"] = false;
        reply["errorCode"] = ERR_CODE_LAUNCHAPP_MISS_PARAM;
        reply["errorText"] = QString::fromStdString(err_missParam);
        return reply;
    }
    jsonParams["instanceId"] = instanceId;

    doc.setObject(jsonParams);
    QString params(doc.toJson());

    std::string appId = request["appDesc"].toObject()["id"].toString().toStdString();
    LOG_INFO_WITH_CLOCK(MSGID_APPLAUNCH_START, 4,
                        PMLOGKS("PerfType","AppLaunch"),
                        PMLOGKS("PerfGroup", appId.c_str()),
                        PMLOGKS("APP_ID", appId.c_str()),
                        PMLOGKS("INSTANCE_ID", instanceId.toStdString().c_str()),
                        "params : %s", qPrintable(params));

    instanceId = WebAppManagerService::onLaunch(
                    QJsonDocument(request["appDesc"].toObject()).toJson().data(),
                    params.toStdString(),
                    request["launchingAppId"].toString().toStdString(),
                    errCode, errMsg).c_str();

    if (instanceId.isEmpty()) {
        reply["returnValue"] = false;
        reply["errorCode"] = errCode;
        reply["errorText"] = QString::fromStdString(errMsg);
    }
    else {
        reply["returnValue"] = true;
        reply["appId"] = request["appDesc"].toObject()["id"];
        reply["instanceId"] = instanceId;
    }
    return reply;
}

bool WebAppManagerServiceLuna::isValidInstanceId(const QString& instanceId)
{
    return !instanceId.trimmed().isEmpty();
}

QJsonObject WebAppManagerServiceLuna::killApp(QJsonObject request)
{
    logJsonTruncated(__func__, request);
    bool instances;
    std::string instanceId = request["instanceId"].toString().toStdString();
    std::string appId = request["appId"].toString().toStdString();
    std::string reason;

    if (!request.isEmpty() && request.contains("reason"))
        reason = request["reason"].toString().toStdString();

    LOG_INFO(MSGID_LUNA_API, 3, PMLOGKS("APP_ID", appId.c_str()), PMLOGKS("INSTANCE_ID", instanceId.c_str()), PMLOGKS("API", "killApp"), "reason : %s", reason.c_str());

    if (reason == "memoryReclaim")
        instances = WebAppManagerService::onKillApp(request["appId"].toString().toStdString(), request["instanceId"].toString().toStdString(), true);
    else
        instances = WebAppManagerService::onKillApp(request["appId"].toString().toStdString(), request["instanceId"].toString().toStdString(), false);

    QJsonObject reply;
    if(instances)
    {
        reply["appId"] = request["appId"].toString();
        reply["instanceId"] = request["instanceId"].toString();
        reply["returnValue"] = true;
    }
    else
    {
        reply["returnValue"] = false;
        reply["errorCode"] = ERR_CODE_NO_RUNNING_APP;
        reply["errorText"] = QString::fromStdString(err_noRunningApp);
    }
    return reply;
}

QJsonObject WebAppManagerServiceLuna::pauseApp(QJsonObject request)
{
    logJsonTruncated(__func__, request);
    std::string id{request["instanceId"].toString().toStdString()};

    LOG_INFO(MSGID_LUNA_API, 2, PMLOGKS("INSTANCE_ID", id.c_str()), PMLOGKS("API", "pauseApp"), "");

    QJsonObject reply;
    if (WebAppManagerService::onPauseApp(id))
    {
        reply["returnValue"] = true;
        reply["appId"] = request["appId"].toString();
        reply["instanceId"] = request["instanceId"].toString();
    }
    else
    {
        reply["returnValue"] = false;
        reply["errorCode"] = ERR_CODE_NO_RUNNING_APP;
        reply["errorText"] = QString::fromStdString(err_noRunningApp);
    }
    return reply;
}

QJsonObject WebAppManagerServiceLuna::setInspectorEnable(QJsonObject request)
{
    logJsonTruncated(__func__, request);
    LOG_DEBUG("WebAppManagerService::setInspectorEnable");
    QString appId = request["appId"].toString();
    QJsonObject reply;
    QString errorMessage("Not supported on this platform");

    LOG_DEBUG("errorMessage : %s", qPrintable(errorMessage));
    reply["errorMessage"] = errorMessage;
    reply["returnValue"] = false;
    return reply;
}


QJsonObject WebAppManagerServiceLuna::closeAllApps(QJsonObject request)
{
    logJsonTruncated(__func__, request);
    bool val = WebAppManagerService::onCloseAllApps();

    QJsonObject reply;
    reply["returnValue"] = val;
    return reply;
}

QJsonObject WebAppManagerServiceLuna::logControl(QJsonObject request)
{
    logJsonTruncated(__func__, request);
    QJsonObject reply;

    if (!request.contains("keys") || !request.contains("value")) {
        reply["returnValue"] = false;
        return reply;
    }

    return WebAppManagerService::onLogControl(
                request["keys"].toString().toStdString(),
                request["value"].toString().toStdString()
             );
}

QJsonObject WebAppManagerServiceLuna::discardCodeCache(QJsonObject request)
{
    logJsonTruncated(__func__, request);
    bool forcedClearCache = false;
    uint32_t pid = 0;
    std::list<const WebAppBase*> running;
    QJsonObject reply;

    if (!WebAppManagerService::isDiscardCodeCacheRequired()) {
        reply["returnValue"] = true;
        return reply;
    }

    if (!request.isEmpty() && request.contains("force"))
        forcedClearCache = request["force"].toBool();

    if (!request.isEmpty() && request.contains("pid"))
        pid = request["pid"].toString().toUInt();

    if (!pid)
        running = WebAppManagerService::runningApps();
    else
        running = WebAppManagerService::runningApps(pid);

    if(running.size() != 0 && !forcedClearCache) {
        reply["returnValue"] = false;
        return reply;
    }

    if (!WebAppManagerService::onCloseAllApps(pid)) {
        reply["returnValue"] = false;
        return reply;
    }

    m_clearedCache = true;
    WebAppManagerService::onDiscardCodeCache(pid);
    if (forcedClearCache)
        WebAppManagerService::onPurgeSurfacePool(pid);
    reply["returnValue"] = true;
    return reply;
}

QJsonObject WebAppManagerServiceLuna::getWebProcessSize(QJsonObject request)
{
    logJsonTruncated(__func__, request);
    QJsonObject reply = WebAppManagerService::getWebProcessProfiling();
    return reply;
}

QJsonObject WebAppManagerServiceLuna::listRunningApps(QJsonObject request, bool subscribed)
{
    logJsonTruncated(__func__, request);
    bool includeSysApps = request["includeSysApps"].toBool();

    std::vector<ApplicationInfo> apps = WebAppManagerService::list(includeSysApps);

    QJsonObject reply;
    QJsonArray runningApps;
    for (auto it = apps.begin(); it != apps.end(); ++it) {
        QJsonObject app;
        app["id"] = it->appId;
        app["instanceId"] = it->instanceId;
        app["webprocessid"] = QString::number(it->pid);
        runningApps.append(app);
    }
    reply["running"] = runningApps;
    reply["returnValue"] = true;
    return reply;
}

QJsonObject WebAppManagerServiceLuna::clearBrowsingData(QJsonObject request)
{
    logJsonTruncated(__func__, request);
    QJsonObject reply;
    QJsonValue value = request["types"];
    bool returnValue = true;
    int removeBrowsingDataMask = 0;


    switch (value.type()) {
        case QJsonValue::Null:
        case QJsonValue::Undefined:
            removeBrowsingDataMask = WebAppManagerService::maskForBrowsingDataType("all");
            break;
        case QJsonValue::Array: {
            QJsonArray array = value.toArray();

            if (array.size() < 1) {
                reply["errorCode"] = ERR_CODE_CLEAR_DATA_BRAWSING_EMPTY_ARRAY;
                reply["errorText"] = QString::fromStdString(err_emptyArray);
                returnValue = false;
                break;
            }

            for (int i = 0; i < array.size(); ++i) {
                if (!array[i].isString()) {
                    reply["errorCode"] = ERR_CODE_CLEAR_DATA_BRAWSING_INVALID_VALUE;
                    reply["errorText"] = QString::fromStdString(err_invalidValue)
                        .append(" (%1)").arg(QString::fromStdString(err_onlyAllowedForString));
                    returnValue = false;
                    break;
                }

                int mask = WebAppManagerService::maskForBrowsingDataType(
                        array[i].toString().toUtf8().data());
                if (mask == 0) {
                    reply["errorCode"] = ERR_CODE_CLEAR_DATA_BRAWSING_UNKNOWN_DATA;
                    reply["errorText"] = QString::fromStdString(err_unknownData).append(": %1").
                        arg(array[i].toString());
                    returnValue = false;
                    break;
                }

                removeBrowsingDataMask |= mask;
            }
            break;
        }
        default:
            reply["errorCode"] = ERR_CODE_CLEAR_DATA_BRAWSING_INVALID_VALUE;
            reply["errorText"] = QString::fromStdString(err_invalidValue);
            returnValue = false;
    }

    LOG_DEBUG("removeBrowsingDataMask: %d", removeBrowsingDataMask);

    if (returnValue)
        WebAppManagerService::onClearBrowsingData(removeBrowsingDataMask);

    reply["returnValue"] = returnValue;
    return reply;
}

void WebAppManagerServiceLuna::didConnect()
{
    QJsonObject params;
    params["subscribe"] = true;

    params["serviceName"] = QStringLiteral("com.webos.settingsservice");
    if (!GET_LS2_SERVER_STATUS(systemServiceConnectCallback, params)) {
        LOG_WARNING(MSGID_SERVICE_CONNECT_FAIL, 0, "Failed to connect to settingsservice");
    }

    params["serviceName"] = QStringLiteral("com.webos.memorymanager");
    if (!GET_LS2_SERVER_STATUS(memoryManagerConnectCallback, params)) {
        LOG_WARNING(MSGID_MEMORY_CONNECT_FAIL, 0, "Failed to connect to memory manager");
    }

    params["serviceName"] = QStringLiteral("com.webos.applicationManager");
    if (!GET_LS2_SERVER_STATUS(applicationManagerConnectCallback, params)) {
        LOG_WARNING(MSGID_APPMANAGER_CONNECT_FAIL, 0, "Failed to connect to application manager");
    }

    params["serviceName"] = QStringLiteral("com.webos.bootManager");
    if (!GET_LS2_SERVER_STATUS(bootdConnectCallback, params)) {
        LOG_WARNING(MSGID_BOOTD_CONNECT_FAIL, 0, "Failed to connect to bootd");
    }

    params["serviceName"] = QStringLiteral("com.webos.service.connectionmanager");
    if (!GET_LS2_SERVER_STATUS(networkConnectionStatusCallback, params)) {
        LOG_WARNING(MSGID_NETWORK_CONNECT_FAIL, 0, "Failed to connect to connectionmanager");
    }
}

void WebAppManagerServiceLuna::systemServiceConnectCallback(QJsonObject reply)
{
    if (reply["connected"] == true) {
        QJsonObject localeParams;
        localeParams["subscribe"] = true;
        QStringList localeList;
        localeList << "localeInfo";
        localeParams["keys"] = QJsonArray::fromStringList(localeList);
        LS2_CALL(getSystemLocalePreferencesCallback,
            "luna://com.webos.settingsservice/getSystemSettings", localeParams);
    }
}

void WebAppManagerServiceLuna::getSystemLocalePreferencesCallback(QJsonObject reply)
{
    QJsonObject localeInfo = reply.value("settings").toObject().value("localeInfo").toObject();

    //LocaleInfo(language, etc) is empty when service is crashed
    //The right value will be notified again when service is restarted
    if(localeInfo.isEmpty()){
        QJsonDocument doc(reply);
        LOG_WARNING(MSGID_RECEIVED_INVALID_SETTINGS, 1,
            PMLOGKFV("MSG", "%s", qPrintable(QString(doc.toJson()))),
            "");
        return;

    }

    QString language(localeInfo.value("locales").toObject().value("UI").toString());

    LOG_INFO(MSGID_SETTING_SERVICE, 1,
        PMLOGKS("LANGUAGE", language.isEmpty() ? "None" : qPrintable(language)),
        "");

    if(language.isEmpty())
        return;

    if(language == WebAppManagerService::getSystemLanguage())
        return;

    WebAppManagerService::setSystemLanguage(language);
}

void WebAppManagerServiceLuna::memoryManagerConnectCallback(QJsonObject reply)
{
    if (reply["connected"] == true) {
        QJsonObject closeAppObj;
        closeAppObj["subscribe"] = true;
        closeAppObj["appType"] = "web";

        if (!call<WebAppManagerServiceLuna, &WebAppManagerServiceLuna::getCloseAppIdCallback>(
                "luna://com.webos.memorymanager/getCloseAppId",
                closeAppObj, this)) {
            LOG_WARNING(MSGID_MEM_MGR_API_CALL_FAIL, 0, "Failed to get close application identifier");
        }

        QJsonObject thresholdChanged;
        thresholdChanged["subscribe"] = true;
        thresholdChanged["category"] = "/com/webos/memory";
        thresholdChanged["method"] = "thresholdChanged";
        if (!call<WebAppManagerServiceLuna, &WebAppManagerServiceLuna::thresholdChangedCallback>(
                "luna://com.palm.bus/signal/addmatch",
                thresholdChanged, this)) {
            LOG_WARNING(MSGID_SIGNAL_REGISTRATION_FAIL, 0, "Failed to register a client for thresholdChanged");
        }
    }
}

void WebAppManagerServiceLuna::getCloseAppIdCallback(QJsonObject reply)
{
    QString pid = reply["pid"].toString();
    if (!pid.isEmpty()) {
        WebAppManagerService::requestKillWebProcess(pid.toUInt());
        return;
    }

    QString appId = reply["id"].toString();
    QString instanceId = reply["instanceId"].toString();

   if(!appId.isEmpty() && !instanceId.isEmpty())
        WebAppManagerService::setForceCloseApp(appId, instanceId);
}

void WebAppManagerServiceLuna::thresholdChangedCallback(QJsonObject reply)
{
    QString currentLevel = reply["current"].toString();
    if (currentLevel.isEmpty()) {
        LOG_DEBUG("thresholdChanged without level");
        return;
    }
    LOG_INFO(MSGID_NOTIFY_MEMORY_STATE, 1, PMLOGKS("State", qPrintable(currentLevel)), "");

    webos::WebViewBase::MemoryPressureLevel level;
    if (currentLevel == "medium")
        level = webos::WebViewBase::MEMORY_PRESSURE_LOW;
    else if (currentLevel == "critical" || currentLevel == "low")
        level = webos::WebViewBase::MEMORY_PRESSURE_CRITICAL;
    else
        level = webos::WebViewBase::MEMORY_PRESSURE_NONE;
    WebAppManagerService::notifyMemoryPressure(level);
}

void WebAppManagerServiceLuna::applicationManagerConnectCallback(QJsonObject reply)
{
    if (reply["connected"] == true) {
        QJsonObject params;
        params["subscribe"] = true;

        if (!call<WebAppManagerServiceLuna, &WebAppManagerServiceLuna::getAppStatusCallback>(
                "luna://com.webos.applicationManager/listApps",
                params, this)) {
            LOG_WARNING(MSGID_APP_MGR_API_CALL_FAIL, 0, "Failed to get an application list");
        }

        params["extraInfo"] = true;
        if (!call<WebAppManagerServiceLuna, &WebAppManagerServiceLuna::getForegroundAppInfoCallback>(
                "luna://com.webos.applicationManager/getForegroundAppInfo",
                params, this)) {
            LOG_WARNING(MSGID_APP_MGR_API_CALL_FAIL, 0, "Failed to get foreground application Information");
        }
    }
}

void WebAppManagerServiceLuna::getAppStatusCallback(QJsonObject reply)
{
    if (reply["change"].toString() == "removed") {
        QJsonObject appObject = reply["app"].toObject();
        QString appId = appObject["id"].toString();

        LOG_INFO(MSGID_WAM_DEBUG, 0, "Application removed %s", qPrintable(appId));
        WebAppManagerService::onAppRemoved(appId.toStdString());
    }
    if (reply["change"].toString() == "added") {
        QJsonObject appObject = reply["app"].toObject();
        QString appId = appObject["id"].toString();
        LOG_INFO(MSGID_WAM_DEBUG, 0, "Application installed %s", qPrintable(appId));
        WebAppManagerService::onAppInstalled(appId.toStdString());
    }
    if (reply["change"].toString() == "removed" ||
        reply["change"].toString() == "updated") {

        QJsonObject appObject = reply["app"].toObject();
        QString appBasePath = appObject["folderPath"].toString();
        bool isCustomPlugin = appObject["customPlugin"].toBool();

        if(isCustomPlugin) {
            WebAppManagerService::killCustomPluginProcess(appBasePath);
        }
    }
}


void WebAppManagerServiceLuna::getForegroundAppInfoCallback(QJsonObject reply)
{
    if (m_clearedCache)
        m_clearedCache = false;

    if(reply["returnValue"] == true) {
        if(!reply.value("appId").isUndefined()) {
            QString appId = reply["appId"].toString();
            webos::Runtime::GetInstance()->SetIsForegroundAppEnyo(
                WebAppManagerService::isEnyoApp(appId));
        }
    }
}

void WebAppManagerServiceLuna::bootdConnectCallback(QJsonObject reply)
{
    if (reply["connected"].toBool() == true) {
        QJsonObject subscribe;
        subscribe["subscribe"] = true;
        if (!LS2_CALL(getBootStatusCallback, "luna://com.webos.bootManager/getBootStatus", subscribe)) {
            LOG_WARNING(MSGID_BOOTD_SUBSCRIBE_FAIL, 0, "Failed to subscribe to bootManager");
        }
    }
}

void WebAppManagerServiceLuna::getBootStatusCallback(QJsonObject reply)
{
    QJsonObject bootd_signals = reply["signals"].toObject();
    m_bootDone = bootd_signals["boot-done"].toBool();
}

void WebAppManagerServiceLuna::closeApp(const std::string& id)
{
    QJsonObject json;
    json["instanceId"] = QString::fromStdString(id);

    if (!LS2_CALL(closeAppCallback, "luna://com.webos.applicationManager/close", json))
        LOG_WARNING(MSGID_CLOSE_CALL_FAIL, 0, "Failed to send closeByAppId command to SAM");
}

void WebAppManagerServiceLuna::closeAppCallback(QJsonObject reply)
{
    // TODO: check reply and close app again.
}

QJsonObject WebAppManagerServiceLuna::webProcessCreated(QJsonObject request, bool subscribed)
{
     QString appId = request["appId"].toString();
     QJsonObject reply;

     if (!appId.isEmpty())
     {
        int pid = WebAppManagerService::getWebProcessId(appId, request["instanceId"].toString());
        reply["id"] = appId;
        reply["instanceId"] = request["instanceId"].toString();

        if (pid) {
            reply["webprocessid"] = pid;
            reply["returnValue"] = true;
        }
        else {
            reply["returnValue"] = false;
            reply["errorText"] = QStringLiteral("process is not running");
        }
    } else if (subscribed) {
       reply["returnValue"] = true;
    } else {
       reply["returnValue"] = false;
       reply["errorText"] = QStringLiteral("parameter error");
    }

    return reply;
}

void WebAppManagerServiceLuna::networkConnectionStatusCallback(QJsonObject reply)
{
    if (reply["connected"] == true) {
        LOG_DEBUG("connectionmanager is connected");
        QJsonObject subscribe;
        subscribe["subscribe"] = true;
        if (!LS2_CALL(getNetworkConnectionStatusCallback, "luna://com.palm.connectionmanager/getStatus", subscribe)) {
            LOG_WARNING(MSGID_LS2_CALL_FAIL, 0, "Fail to subscribe to connection manager");
        }
    }
}

void WebAppManagerServiceLuna::getNetworkConnectionStatusCallback(QJsonObject reply)
{
    // luna-send -f -n 1 luna://com.webos.service.connectionmanager/getstatus '{"subscribe": true}'
    WebAppManagerService::updateNetworkStatus(reply);
}
