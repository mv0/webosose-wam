#ifndef WEBAPPMANAGERSERVICEAGL_H
#define WEBAPPMANAGERSERVICEAGL_H

#include <memory>

#include "WebRuntimeAGL.h"
#include "WebAppManagerService.h"
#include "Timer.h"
#include "AglShellSurface.h"

constexpr char kStartApp[] = "start-app";
constexpr char kKilledApp[] = "killed-app";
constexpr char kActivateEvent[] = "activate-event";
constexpr char kDeactivateEvent[] = "deactivate-event";
constexpr char kSendAglReady[] = "ready-event";

class WamSocket;
class WamSocketLockFile;

class WebAppManagerServiceAGL : public WebAppManagerService {
public:
    static WebAppManagerServiceAGL* instance();
    class StartupArgs {
    public:
       StartupArgs(std::string _app_id, std::string _app_uri,
                   int _surface_id, int _width, int _height,
                   std::list<AglShellSurface> _surfaces) :
                   app_id(_app_id), app_uri(_app_uri), surface_id(_surface_id),
                   width(_width), height(_height), surfaces(_surfaces)
        {
        }

       std::string app_id;
       std::string app_uri;
       int surface_id;

       int width;
       int height;

       std::list<AglShellSurface> surfaces;
    };

    bool initializeAsHostService();
    bool initializeAsHostClient();

    bool isHostServiceRunning();

    void setStartupApplication(const std::string& startup_app_id,
        const std::string& startup_app_uri, int startup_app_surface_id,
        int _width, int _height, std::list<AglShellSurface> surfaces);
    void setAppIdForEventTarget(const std::string& app_id);

    void launchOnHost(int argc, const char **argv, std::list<AglShellSurface> surfaces);
    void sendEvent(int argc, const char **argv);

    // WebAppManagerService
    bool startService() override;
    Json::Value launchApp(const Json::Value &request) override;
    Json::Value killApp(const Json::Value &request) override;
    Json::Value pauseApp(const Json::Value &request) override;
    Json::Value logControl(const Json::Value &request) override;
    Json::Value setInspectorEnable(const Json::Value &request) override;
    Json::Value closeAllApps(const Json::Value &request) override;
    Json::Value discardCodeCache(const Json::Value &request) override;
    Json::Value listRunningApps(const Json::Value &request, bool subscribed) override;
    Json::Value getWebProcessSize(const Json::Value &request) override;
    Json::Value clearBrowsingData(const Json::Value &request) override;
    Json::Value webProcessCreated(const Json::Value &request, bool subscribed) override;

    void triggerStartupApp(StartupArgs *sargs);
    void triggetEventForApp(const std::string& action);

private:

    WebAppManagerServiceAGL();

    void launchStartupAppFromConfig(void *data);
    void launchStartupAppFromURL(void *data);

    void onActivateEvent();
    void onDeactivateEvent();
    void onKillEvent();
    void onSendAglEvent();

    std::string app_id_event_target_;

    std::string startup_app_id_;
    std::string startup_app_uri_;

    std::list<AglShellSurface> surfaces_;
    int width;
    int height;

    int startup_app_surface_id_;
    OneShotTimer<WebAppManagerServiceAGL> startup_app_timer_;
    OneShotTimer<WebAppManagerServiceAGL> ready_app_timer_;

    std::unique_ptr<WamSocket> socket_;
    std::unique_ptr<WamSocketLockFile> lock_file_;
};

#endif // WEBAPPMANAGERSERVICEAGL_H
