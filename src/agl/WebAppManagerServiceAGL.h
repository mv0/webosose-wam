#ifndef WEBAPPMANAGERSERVICEAGL_H
#define WEBAPPMANAGERSERVICEAGL_H

#include <memory>

#include "AglShellSurface.h"
#include "WebRuntimeAGL.h"
#include "WebAppManagerService.h"
#include "Timer.h"

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
       StartupArgs(const std::string &app_id, const std::string &app_uri,
                   int surface_id, int width, int height,
                   std::list<AglShellSurface> surfaces) :
                   m_app_id(app_id), m_app_uri(app_uri), m_surface_id(surface_id),
                   m_width(width), m_height(height), m_surfaces(surfaces)
        {
        }

       std::string GetAppId()   { return m_app_id; }
       std::string GetAppUri()  { return m_app_uri; }
       int GetSurfaceId()             { return m_surface_id; }
       int GetWidth()                 { return m_width; }
       int GetHeight()                { return m_height;}
       std::list<AglShellSurface> GetSurfaces() { return m_surfaces; }

    private:
       std::string m_app_id;
       std::string m_app_uri;
       int m_surface_id;

       int m_width;
       int m_height;

       std::list<AglShellSurface> m_surfaces;
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

    void launchStartupAppFromConfig();
    void launchStartupAppFromURL();

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
