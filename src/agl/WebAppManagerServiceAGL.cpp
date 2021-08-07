#include "WebAppManagerServiceAGL.h"

#include <sys/file.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>

#include <cassert>
#include <cstdlib>
#include <climits>
#include <exception>
#include <iostream>
#include <sstream>
#include <pthread.h>

#include <json/value.h>
#include <libxml/parser.h>

#include "LogManager.h"
#include "JsonHelper.h"

#include "WebAppManager.h"
#include "WebAppBase.h"

#include "AglShellSurface.h"
#include "agl-shell.pb.h"

class WamSocketLockFile {
public:
  WamSocketLockFile() {
    const char* runtime_dir;
    if ((runtime_dir = getenv("XDG_RUNTIME_DIR")) == NULL) {
      LOG_DEBUG("Failed to retrieve XDG_RUNTIME_DIR, falling back to /tmp");
      runtime_dir = "/tmp";
    }
    lock_file_ = std::string(runtime_dir);
    lock_file_.append("/wamsocket.lock");
  }

  ~WamSocketLockFile() {
    if (lock_fd_ != -1)
      releaseLock(lock_fd_);
    if (lock_fd_ != -1)
      close(lock_fd_);
  }

  bool createAndLock() {
    lock_fd_ = openLockFile();
    if (!acquireLock(lock_fd_)) {
      LOG_DEBUG("Failed to lock file %d", lock_fd_);
      return false;
    }
    return true;
  }

  bool ownsLock() {
    return lock_fd_ != -1;
  }

  bool tryAcquireLock() {
    int fd = openLockFile();
    if (fd != -1) {
      if (acquireLock(fd)) {
        releaseLock(fd);
        return true;
      }
    }
    return false;
  }

private:

  int openLockFile() {
    int fd = open(lock_file_.c_str(), O_CREAT | O_TRUNC, S_IRWXU);
    if (fd == -1) {
      LOG_DEBUG("Failed to open lock file descriptor");
      return fd;
    }

    int flags = fcntl(fd, F_GETFD);
    if (flags == -1)
      LOG_DEBUG("Could not get flags for lock file %d", fd);

     flags |= FD_CLOEXEC;

     if (fcntl(fd, F_SETFD, flags) == -1)
       LOG_DEBUG("Could not set flags for lock file %d", fd);

     return fd;
  }

  bool acquireLock(int fd) {
    if (flock(fd, LOCK_EX | LOCK_NB) != 0)
      return false;
    return true;
  }

  void releaseLock(int fd) {
    flock(fd, LOCK_UN);
  }

  std::string lock_file_;
  int lock_fd_ = -1;
};

static ::panel_edge
to_panel_edge(panelEdge edge)
{
    switch (edge) {
    case TOP:
        return ::panel_edge::PANEL_TOP;
    case BOTTOM:
        return ::panel_edge::PANEL_BOTTOM;
    case LEFT:
        return ::panel_edge::PANEL_LEFT;
    case RIGHT:
        return ::panel_edge::PANEL_RIGHT;
    default:
        assert(!"Should not get here");
    }

    assert(!"Should not get here");
    return ::panel_edge::PANEL_TOP;
}

static panelEdge 
from_panel_edge(::panel_edge edge)
{

    switch (edge) {
    case ::panel_edge::PANEL_TOP:
        return TOP;
    case ::panel_edge::PANEL_BOTTOM:
        return BOTTOM;
    case ::panel_edge::PANEL_LEFT:
        return LEFT;
    case ::panel_edge::PANEL_RIGHT:
        return RIGHT;
    default:
        assert(!"Should not get here");
    }

    assert(!"Should not get here");
    return NONE;
}


static void
print_csurfaces(::CSurfaces surfaces)
{
    for (int i = 0; i < surfaces.surfaces_size(); i++) {
        const ::shell_surface &shsurf = surfaces.surfaces(i);
        ::surface_type s_type = shsurf.surface_type();

        if (s_type == ::surface_type::TYPE_BACKGROUND) {
            LOG_DEBUG("csurface type background");
        } else if (s_type == ::surface_type::TYPE_PANEL) {
            LOG_DEBUG("csurface type panel");
        }
    }
}

static void
print_surfaces(std::list<AglShellSurface> surfaces)
{
    for (AglShellSurface &s : surfaces) {
        Surface surface = s.getSurface();
        switch (surface.getSurfaceType()) {
        case BACKGROUND:
            LOG_DEBUG("shell surface is a background");
            break;
        case PANEL: {
            Panel panel = s.getPanel();
            LOG_DEBUG("shell surface is a panel");
            LOG_DEBUG("panel edge %d, width %d", panel.getPanelEdge(),
                                                 panel.getPanelWidth());
            break;
        }
        }
    }
}

static void
surfaces_to_csurfaces(::CSurfaces *surfaces_, std::list<AglShellSurface> surfaces)
{

    for (AglShellSurface s: surfaces) {
        Surface surface = s.getSurface();
        surfaceType sType = surface.getSurfaceType();
        ::shell_surface *sh_surf = surfaces_->add_surfaces();

        switch (sType) {
        case BACKGROUND:
            sh_surf->set_surface_type(::surface_type::TYPE_BACKGROUND);
            sh_surf->set_src(s.getSrc());
            sh_surf->set_entrypoint(s.getEntryPoint());
            LOG_DEBUG("Added background surface to CSurfaces");
            break;
        case PANEL: {
            Panel panel = s.getPanel();
            sh_surf->set_surface_type(::surface_type::TYPE_PANEL);

            ::shell_panel *sh_panel = sh_surf->mutable_panel();
            sh_panel->set_width(panel.getPanelWidth());
            /* convert from one to another */
            sh_panel->set_edge(to_panel_edge(panel.getPanelEdge()));

            sh_surf->set_src(s.getSrc());
            sh_surf->set_entrypoint(s.getEntryPoint());
            LOG_DEBUG("Added panel surface to CSurfaces");
            break;
        }

        }
    }
}

static void
csurfaces_to_surfaces(::CSurfaces surfaces_, std::list<AglShellSurface> *surfaces)
{
    int i;
    for (i = 0; i < surfaces_.surfaces_size(); i++) {
        const ::shell_surface &s = surfaces_.surfaces(i);
        AglShellSurface aglSurface;
        Surface surface;

        /* should be present in both cases */
        aglSurface.setSrc(s.src());
        aglSurface.setEntryPoint(s.entrypoint());

        switch (s.surface_type()) {
        case ::surface_type::TYPE_BACKGROUND:

            surface.setSurfaceType(BACKGROUND);
            aglSurface.setSurface(surface);

            LOG_DEBUG("Added csurface background");
            break;
        case ::surface_type::TYPE_PANEL: {
            Panel panel;

            surface.setSurfaceType(PANEL);
            panel.setPanelWidth(s.panel().width());
            panel.setPanelEdge(from_panel_edge(s.panel().edge()));

            aglSurface.setSurface(surface);
            aglSurface.setPanel(panel);
            LOG_DEBUG("Added csurface panel");
            break;
        }
        default:
            assert(!"Invalid surface type\n");
        }
        surfaces->push_back(aglSurface);
    }
}

class WamSocket {
public:
  WamSocket() {
    const char* runtime_dir;
    if ((runtime_dir = getenv("XDG_RUNTIME_DIR")) == NULL) {
      LOG_DEBUG("Failed to retrieve XDG_RUNTIME_DIR, falling back to /tmp");
      runtime_dir = "/tmp";
    }
    GOOGLE_PROTOBUF_VERIFY_VERSION;

    wam_socket_path_ = std::string(runtime_dir);
    wam_socket_path_.append("/wamsocket");
  }

  ~WamSocket() {
    if (socket_fd_ != -1)
      close(socket_fd_);

    google::protobuf::ShutdownProtobufLibrary();
  }

  bool createSocket(bool server) {
        // Create the socket file descriptor

    socket_fd_ = socket(AF_UNIX, SOCK_DGRAM, 0);
    if (socket_fd_ == -1) {
      LOG_DEBUG("Failed to open socket file descriptor");
      return false;
    }

    sock_addr.sun_family = AF_UNIX;
    strncpy(sock_addr.sun_path, wam_socket_path_.c_str(), sizeof(sock_addr.sun_path));

    if (server) {
      LOG_DEBUG("service binding");
      unlink(wam_socket_path_.c_str());
      if (bind(socket_fd_, (struct sockaddr *) &sock_addr, sizeof(struct sockaddr_un)) != 0) {
        LOG_DEBUG("Failed to bind to named socket");
        return false;
      }
    } else {
      LOG_DEBUG("client connecting");
      if (connect(socket_fd_, (struct sockaddr *) &sock_addr, sizeof(struct sockaddr_un)) != 0) {
        LOG_DEBUG("Failed to connect to named socket");
        return false;
      }
    }
    return true;
  }

  void sendMsg(int argc, const char **argv, std::list<AglShellSurface> surfaces) {
    std::string cmd;
    for (int i = 0; i < argc; ++i)
        cmd.append(argv[i]).append(" ");
    LOG_DEBUG("Sending message=[%s]", cmd.c_str());

    if (!surfaces.empty()) {
        ::CSurfaces surfaces_;

        // convert surfaces to CSurfaces::surfaces then serialize it as a
        // string
        surfaces_to_csurfaces(&surfaces_, surfaces);
        print_csurfaces(surfaces_);

        cmd.append("|");
        std::string serialized_string = surfaces_.SerializeAsString();
        cmd.append(serialized_string);
    }

    ssize_t bytes = write(socket_fd_, (void *) cmd.c_str(), cmd.length());
    LOG_DEBUG("Wrote %zd bytes, cmd.length() %zd, buf=[%s]", bytes, cmd.length(), cmd.c_str());
  }

  int waitForMsg() {
      char buf[PATH_MAX] = {};
      ssize_t bytes;

      LOG_DEBUG("Waiting for data on socket_fd %d", socket_fd_);

      memset(buf, 0, sizeof(buf));

      bytes = recv(socket_fd_, (void *) buf, sizeof(buf), 0);

      int last = bytes - 1;
      // Remove the new line if there's one
      if (buf[last] == '\n') {
          LOG_DEBUG("Removing new line and adding NUL terminator");
          buf[last] = '\0';
      }

      char *str = buf;
      LOG_DEBUG("Got %zd bytes=[%s]", bytes, buf);

      std::list<std::string> event_args;

      std::list<AglShellSurface> surfaces;

      char *tokenize = strdup(str);
      char *token = strtok(tokenize, " ");
      while (token) {
          LOG_DEBUG("Looking at token %s", token);
          if (!strcmp(token, "|"))
              break;

          LOG_DEBUG("Pushing back token %s\n", token);
          event_args.push_back(std::string(token));
          token = strtok(NULL, " ");
      }

      // eat white-space until we get to '|'
      while (str && *str && *str != '|')
          str++;

      str++;
      std::string last_arg = std::string(str);
      ::CSurfaces surfaces_;

      bool parsed = surfaces_.ParseFromString(last_arg);
      if (parsed) {
          LOG_DEBUG("Serialized. Transfering to surfaces");
          csurfaces_to_surfaces(surfaces_, &surfaces);
          print_surfaces(surfaces);
      }

      std::string event = event_args.front();
      event_args.pop_front();

      if (event == kStartApp) {
          std::string arg1 = event_args.front();
          event_args.pop_front();
          std::string arg2 = event_args.front();
          event_args.pop_front();

          int arg3 = std::stoi(event_args.front());
          event_args.pop_front();
          int arg4 = std::stoi(event_args.front());
          event_args.pop_front();
          int arg5 = std::stoi(event_args.front());
          event_args.pop_front();

          LOG_DEBUG("kStartApp, event %s, arg1 %s, arg2 %s",
                  event.c_str(), arg1.c_str(), arg2.c_str());
          LOG_DEBUG("kStartApp, arg3 %d, arg4 %d, arg5 %d",
                  arg3, arg4, arg5);

          if (!surfaces.empty()) {
              LOG_DEBUG("Surfaces are not empty. Printing them");
              print_surfaces(surfaces);
          }

          WebAppManagerServiceAGL::instance()->setStartupApplication(
                  arg1, arg2, arg3, arg4, arg5, surfaces);
          WebAppManagerServiceAGL::instance()->triggerStartupApp();
      } else {
          WebAppManagerServiceAGL::instance()->setAppIdForEventTarget(event_args.front());
          WebAppManagerServiceAGL::instance()->triggetEventForApp(event);
      }
    return 1;
  }

private:

  std::string wam_socket_path_;
  int socket_fd_;
  struct sockaddr_un sock_addr;
};

WebAppManagerServiceAGL::WebAppManagerServiceAGL()
  : socket_(std::make_unique<WamSocket>()),
  lock_file_(std::make_unique<WamSocketLockFile>())
{
}

WebAppManagerServiceAGL* WebAppManagerServiceAGL::instance() {
  static WebAppManagerServiceAGL *srv = new WebAppManagerServiceAGL();
  return srv;
}

bool WebAppManagerServiceAGL::initializeAsHostService() {
  if (lock_file_->createAndLock())
    return socket_->createSocket(true);
  return false;
}

bool WebAppManagerServiceAGL::initializeAsHostClient() {
  return socket_->createSocket(false);
}

bool WebAppManagerServiceAGL::isHostServiceRunning()
{
    return !lock_file_->tryAcquireLock();
}

void WebAppManagerServiceAGL::launchOnHost(int argc, const char **argv,
                                           std::list<AglShellSurface> surfaces)
{
    LOG_DEBUG("Dispatching launchOnHost");
    socket_->sendMsg(argc, argv, surfaces);
}

void WebAppManagerServiceAGL::sendEvent(int argc, const char **argv)
{
    LOG_DEBUG("Sending event");

    std::list<AglShellSurface> surfaces;
    socket_->sendMsg(argc, argv, surfaces);
}

void WebAppManagerServiceAGL::setStartupApplication(
    const std::string& startup_app_id,
    const std::string& startup_app_uri, int startup_app_surface_id,
    int _width, int _height, std::list<AglShellSurface> surfaces)
{
	startup_app_id_ = startup_app_id;
	startup_app_uri_ = startup_app_uri;
	startup_app_surface_id_ = startup_app_surface_id;
	surfaces_ = surfaces;

	width = _width;
	height = _height;
}

void WebAppManagerServiceAGL::setAppIdForEventTarget(const std::string& app_id) {
  // This might be a subject to races. But it works ok as a temp solution.
  if (app_id_event_target_.empty())
    app_id_event_target_ = app_id;
}

void *run_socket(void *socket) {
  WamSocket *s = (WamSocket*)socket;
  while(s->waitForMsg());
  return 0;
}

bool WebAppManagerServiceAGL::startService()
{
    if (lock_file_->ownsLock()) {
      pthread_t thread_id;
      if( pthread_create( &thread_id , nullptr,  run_socket, socket_.get()) < 0) {
          perror("could not create thread");
          LOG_DEBUG("Couldnt create thread...");
          return false;
      }
    }

    triggerStartupApp();

    return true;
}

void WebAppManagerServiceAGL::triggerStartupApp()
{
    LOG_DEBUG("Triggering app start: %s", startup_app_uri_.c_str());
    if (!startup_app_uri_.empty()) {
      if (startup_app_uri_.find("http://") == 0) {
        startup_app_timer_.start(10, this,
              &WebAppManagerServiceAGL::launchStartupAppFromURL);
      } else {
        startup_app_timer_.start(10, this,
              &WebAppManagerServiceAGL::launchStartupAppFromConfig);
      }
    }
}

void WebAppManagerServiceAGL::triggetEventForApp(const std::string& action) {
  if (app_id_event_target_.empty())
    return;

  if (action == kActivateEvent) {
     startup_app_timer_.start(10, this,
           &WebAppManagerServiceAGL::onActivateEvent);
  } else if (action == kDeactivateEvent) {
     startup_app_timer_.start(10, this,
           &WebAppManagerServiceAGL::onDeactivateEvent);
  } else if (action == kKilledApp) {
    startup_app_timer_.start(1000, this,
          &WebAppManagerServiceAGL::onKillEvent);
  }
}

void WebAppManagerServiceAGL::launchStartupAppFromConfig()
{
    std::string configfile;
    configfile.append(startup_app_uri_);
    configfile.append("/config.xml");

    xmlDoc *doc = xmlReadFile(configfile.c_str(), nullptr, 0);
    xmlNode *root = xmlDocGetRootElement(doc);

    xmlChar *id = nullptr;
    xmlChar *version = nullptr;
    xmlChar *name = nullptr;
    xmlChar *content = nullptr;
    xmlChar *description = nullptr;
    xmlChar *author = nullptr;
    xmlChar *icon = nullptr;

    id = xmlGetProp(root, (const xmlChar*)"id");
    version = xmlGetProp(root, (const xmlChar*)"version");
    for (xmlNode *node = root->children; node; node = node->next) {
      if (!xmlStrcmp(node->name, (const xmlChar*)"name"))
        name = xmlNodeListGetString(doc, node->xmlChildrenNode, 1);
      if (!xmlStrcmp(node->name, (const xmlChar*)"icon"))
        icon = xmlGetProp(node, (const xmlChar*)"src");
      if (!xmlStrcmp(node->name, (const xmlChar*)"content"))
        content = xmlGetProp(node, (const xmlChar*)"src");
      if (!xmlStrcmp(node->name, (const xmlChar*)"description"))
        description = xmlNodeListGetString(doc, node->xmlChildrenNode, 1);
      if (!xmlStrcmp(node->name, (const xmlChar*)"author"))
        author = xmlNodeListGetString(doc, node->xmlChildrenNode, 1);
    }
    fprintf(stdout, "...\n");
    LOG_DEBUG("id: %s", id);
    LOG_DEBUG("version: %s", version);
    LOG_DEBUG("name: %s", name);
    LOG_DEBUG("content: %s", content);
    LOG_DEBUG("description: %s", description);
    LOG_DEBUG("author: %s", author);
    LOG_DEBUG("icon: %s", icon);


    Json::Value obj(Json::objectValue);
    obj["id"] = (const char*)id;
    obj["version"] = (const char*)version;
    obj["vendor"] = (const char*)author;
    obj["type"] = "web";
    obj["main"] = (const char*)content;
    obj["title"] = (const char*)name;
    obj["uiRevision"] = "2";
    obj["icon"] = (const char*)icon;
    obj["folderPath"] = startup_app_uri_.c_str();
    obj["surfaceId"] = startup_app_surface_id_;

    xmlFree(id);
    xmlFree(version);
    xmlFree(name);
    xmlFree(content);
    xmlFree(description);
    xmlFree(author);
    xmlFree(icon);
    xmlFreeDoc(doc);

    std::string appDesc;
    dumpJsonToString(obj, appDesc);
    std::string params;
    std::string app_id = obj["id"].asString();
    int errCode = 0;
    std::string errMsg;
    WebAppManagerService::onLaunch(appDesc, params, app_id, errCode, errMsg);
}

void WebAppManagerServiceAGL::launchStartupAppFromURL()
{
    LOG_DEBUG("WebAppManagerServiceAGL::launchStartupAppFromURL");
    LOG_DEBUG("    url: %s", startup_app_uri_.c_str());
    Json::Value obj(Json::objectValue);
    obj["id"] = startup_app_id_;
    obj["version"] = "1.0";
    obj["vendor"] = "some vendor";
    obj["type"] = "web";
    obj["main"] = startup_app_uri_;
    obj["title"] = "webapp";
    obj["uiRevision"] = "2";
    //obj["icon"] = (const char*)icon;
    //obj["folderPath"] = startup_app_.c_str();
    obj["surfaceId"] = startup_app_surface_id_;
    //obj["surface_role"] = surface_role;
    //obj["panel_type"] = panel_type;

    obj["widthOverride"] = width;
    obj["heightOverride"] = height;

    std::string appDesc;
    dumpJsonToString(obj, appDesc);
    std::string app_id = startup_app_id_;
    int errCode = 0;
    std::string params, errMsg;

    LOG_DEBUG("Launching with appDesc=[%s]", appDesc.c_str());

    WebAppManagerService::onLaunch(appDesc, params, app_id, errCode, errMsg);
    LOG_DEBUG("onLaunch: Done.");
}

Json::Value WebAppManagerServiceAGL::launchApp(const Json::Value &request)
{
    return Json::Value(Json::objectValue);
}

Json::Value WebAppManagerServiceAGL::killApp(const Json::Value &request)
{
    return Json::Value(Json::objectValue);
}

Json::Value WebAppManagerServiceAGL::pauseApp(const Json::Value &request)
{
    return Json::Value(Json::objectValue);
}

Json::Value WebAppManagerServiceAGL::logControl(const Json::Value &request)
{
    return Json::Value(Json::objectValue);
}

Json::Value WebAppManagerServiceAGL::setInspectorEnable(const Json::Value &request)
{
    return Json::Value(Json::objectValue);
}

Json::Value WebAppManagerServiceAGL::closeAllApps(const Json::Value &request)
{
    return Json::Value(Json::objectValue);
}

Json::Value WebAppManagerServiceAGL::discardCodeCache(const Json::Value &request)
{
    return Json::Value(Json::objectValue);
}

Json::Value WebAppManagerServiceAGL::listRunningApps(const Json::Value &request, bool subscribed)
{
    return Json::Value(Json::objectValue);
}

Json::Value WebAppManagerServiceAGL::getWebProcessSize(const Json::Value &request)
{
    return Json::Value(Json::objectValue);
}

Json::Value WebAppManagerServiceAGL::clearBrowsingData(const Json::Value &request)
{
    return Json::Value(Json::objectValue);
}

Json::Value WebAppManagerServiceAGL::webProcessCreated(const Json::Value &request, bool subscribed)
{
    return Json::Value(Json::objectValue);
}

void WebAppManagerServiceAGL::onActivateEvent() {
  LOG_DEBUG("Activate app=%s", app_id_event_target_.c_str());
  WebAppBase* web_app = WebAppManager::instance()->findAppById(app_id_event_target_);
  if (web_app) {
    web_app->onStageActivated();
    web_app->sendAglActivate(app_id_event_target_.c_str());
  } else {
	  LOG_DEBUG("Not found app=%s running", app_id_event_target_.c_str());
  }
  app_id_event_target_.clear();
}

void WebAppManagerServiceAGL::onDeactivateEvent() {
  LOG_DEBUG("Dectivate app=%s", app_id_event_target_.c_str());
  WebAppBase* web_app = WebAppManager::instance()->findAppById(app_id_event_target_);
  if (web_app)
    web_app->onStageDeactivated();
  app_id_event_target_.clear();
}

void WebAppManagerServiceAGL::onKillEvent() {
  LOG_DEBUG("Kill app=%s", app_id_event_target_.c_str());
  WebAppManager::instance()->onKillApp(app_id_event_target_, app_id_event_target_);
  app_id_event_target_.clear();
}
