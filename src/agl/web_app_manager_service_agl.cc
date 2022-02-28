// Copyright (c) 2018-2022 LG Electronics, Inc.
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

#include "web_app_manager_service_agl.h"

#include <pthread.h>
#include <sys/file.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>
#include <algorithm>
#include <cassert>
#include <climits>
#include <cstdlib>
#include <exception>
#include <iostream>
#include <set>
#include <sstream>

#include <json/value.h>
#include <libxml/parser.h>

#include "log_manager.h"
#include "utils.h"
#include "web_app_base.h"
#include "web_app_manager.h"

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
      ReleaseLock(lock_fd_);
    if (lock_fd_ != -1)
      close(lock_fd_);
  }

  bool CreateAndLock() {
    lock_fd_ = OpenLockFile();
    if (!AcquireLock(lock_fd_)) {
      LOG_DEBUG("Failed to lock file %d", lock_fd_);
      return false;
    }
    return true;
  }

  bool OwnsLock() const { return lock_fd_ != -1; }

  bool TryAcquireLock() {
    int fd = OpenLockFile();
    if (fd != -1) {
      if (AcquireLock(fd)) {
        ReleaseLock(fd);
        return true;
      }
    }
    return false;
  }

 private:
  int OpenLockFile() {
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

  bool AcquireLock(int fd) {
    if (flock(fd, LOCK_EX | LOCK_NB) != 0)
      return false;
    return true;
  }

  void ReleaseLock(int fd) { flock(fd, LOCK_UN); }

  std::string lock_file_;
  int lock_fd_ = -1;
};

class WamSocket {
 public:
  WamSocket() {
    const char* runtime_dir;
    if ((runtime_dir = getenv("XDG_RUNTIME_DIR")) == NULL) {
      LOG_DEBUG("Failed to retrieve XDG_RUNTIME_DIR, falling back to /tmp");
      runtime_dir = "/tmp";
    }
    wam_socket_path_ = std::string(runtime_dir);
    wam_socket_path_.append("/wamsocket");
  }

  ~WamSocket() {
    if (socket_fd_ != -1)
      close(socket_fd_);
  }

  bool CreateSocket(bool server) {
    // Create the socket file descriptor
    socket_fd_ = socket(AF_UNIX, SOCK_DGRAM, 0);
    if (socket_fd_ == -1) {
      LOG_DEBUG("Failed to open socket file descriptor");
      return false;
    }

    sock_addr.sun_family = AF_UNIX;
    strncpy(sock_addr.sun_path, wam_socket_path_.c_str(),
            sizeof(sock_addr.sun_path));

    if (server) {
      LOG_DEBUG("service binding");
      unlink(wam_socket_path_.c_str());
      if (bind(socket_fd_, (struct sockaddr*)&sock_addr,
               sizeof(struct sockaddr_un)) != 0) {
        LOG_DEBUG("Failed to bind to named socket");
        return false;
      }
    } else {
      LOG_DEBUG("client connecting");
      if (connect(socket_fd_, (struct sockaddr*)&sock_addr,
                  sizeof(struct sockaddr_un)) != 0) {
        LOG_DEBUG("Failed to connect to named socket");
        return false;
      }
    }
    return true;
  }

  void SendMsg(int argc, const char** argv) {
    std::string cmd;
    for (int i = 0; i < argc; ++i)
      cmd.append(argv[i]).append(" ");
    // Remove the last appended space if any
    if (argc > 1)
      cmd.pop_back();
    LOG_DEBUG("Sending message=[%s]", cmd.c_str());
    ssize_t bytes = write(socket_fd_, cmd.c_str(), cmd.length());
    LOG_DEBUG("Wrote %zd bytes.", bytes);
  }

  int WaitForMsg() {
    char buf[PATH_MAX] = {};
    ssize_t bytes;

    LOG_DEBUG("Waiting for data...");
    while (TEMP_FAILURE_RETRY(
        (bytes = recv(socket_fd_, (void*)buf, sizeof(buf), 0)) != -1)) {
      int last = bytes - 1;
      // Remove the new line if there's one
      if (buf[last] == '\n')
        buf[last] = '\0';
      LOG_DEBUG("Got %zd bytes=[%s].", bytes, buf);

      std::string data(buf);
      std::istringstream iss(data);
      std::vector<const char*> res;
      for (std::string s; iss >> s;) {
        res.push_back(strdup(s.c_str()));
      }

      if (std::string(res[0]) == kStartApp) {
        WebAppManagerServiceAGL::Instance()->SetStartupApplication(
            std::string(res[1]), std::string(res[2]), atoi(res[3]),
            static_cast<AglShellSurfaceType>(atoi(res[4])),
            static_cast<AglShellPanelEdge>(atoi(res[5])), atoi(res[6]),
            atoi(res[7]));

        WebAppManagerServiceAGL::Instance()->TriggerStartupApp();
      } else {
        WebAppManagerServiceAGL::Instance()->SetAppIdForEventTarget(
            std::string(res[1]));

        WebAppManagerServiceAGL::Instance()->TriggetEventForApp(
            std::string(res[0]));
      }
      return 1;
    }
    return 0;
  }

 private:
  std::string wam_socket_path_;
  int socket_fd_;
  struct sockaddr_un sock_addr;
};

WebAppManagerServiceAGL::WebAppManagerServiceAGL()
    : socket_(std::make_unique<WamSocket>()),
      lock_file_(std::make_unique<WamSocketLockFile>()) {}

WebAppManagerServiceAGL* WebAppManagerServiceAGL::Instance() {
  static WebAppManagerServiceAGL* srv = new WebAppManagerServiceAGL();
  return srv;
}

bool WebAppManagerServiceAGL::InitializeAsHostService() {
  if (lock_file_->CreateAndLock())
    return socket_->CreateSocket(true);
  return false;
}

bool WebAppManagerServiceAGL::InitializeAsHostClient() {
  return socket_->CreateSocket(false);
}

bool WebAppManagerServiceAGL::IsHostServiceRunning() {
  return !lock_file_->TryAcquireLock();
}

void WebAppManagerServiceAGL::LaunchOnHost(int argc, const char** argv) {
  LOG_DEBUG("Dispatching launchOnHost");
  socket_->SendMsg(argc, argv);
}

void WebAppManagerServiceAGL::SendEvent(int argc, const char** argv) {
  LOG_DEBUG("Sending event");
  socket_->SendMsg(argc, argv);
}

void WebAppManagerServiceAGL::SetStartupApplication(
    const std::string& startup_app_id,
    const std::string& startup_app_uri,
    int startup_app_surface_id,
    AglShellSurfaceType surface_role,
    AglShellPanelEdge panel_type,
    int width,
    int height) {
  startup_app_id_ = startup_app_id;
  startup_app_uri_ = startup_app_uri;
  startup_app_surface_id_ = startup_app_surface_id;
  surface_role_ = surface_role;
  panel_type_ = panel_type;

  width_ = width_;
  height_ = height;
}

void WebAppManagerServiceAGL::SetAppIdForEventTarget(
    const std::string& app_id) {
  // This might be a subject to races. But it works ok as a temp solution.
  if (app_id_event_target_.empty())
    app_id_event_target_ = app_id;
}

void* RunSocket(void* socket) {
  WamSocket* s = static_cast<WamSocket*>(socket);
  while (s->WaitForMsg())
    ;
  return 0;
}

bool WebAppManagerServiceAGL::StartService() {
  if (lock_file_->OwnsLock()) {
    pthread_t thread_id;
    if (pthread_create(&thread_id, nullptr, RunSocket, socket_.get()) < 0) {
      perror("could not create thread");
      LOG_DEBUG("Couldnt create thread...");
      return false;
    }
  }

  TriggerStartupApp();

  return true;
}

void WebAppManagerServiceAGL::TriggerStartupApp() {
  LOG_DEBUG("Triggering app start: %s", startup_app_uri_.c_str());
  if (!startup_app_uri_.empty()) {
    if (startup_app_uri_.find("http://") == 0) {
      startup_app_timer_.Start(
          10, this, &WebAppManagerServiceAGL::LaunchStartupAppFromURL);
    } else {
      startup_app_timer_.Start(
          10, this, &WebAppManagerServiceAGL::LaunchStartupAppFromConfig);
    }
  }
}

void WebAppManagerServiceAGL::TriggetEventForApp(const std::string& action) {
  if (app_id_event_target_.empty())
    return;

  if (action == kActivateEvent) {
    startup_app_timer_.Start(10, this,
                             &WebAppManagerServiceAGL::OnActivateEvent);
  } else if (action == kDeactivateEvent) {
    startup_app_timer_.Start(10, this,
                             &WebAppManagerServiceAGL::OnDeactivateEvent);
  } else if (action == kKilledApp) {
    startup_app_timer_.Start(1000, this, &WebAppManagerServiceAGL::OnKillEvent);
  }
}

void WebAppManagerServiceAGL::LaunchStartupAppFromConfig() {
  std::string configfile;
  configfile.append(startup_app_uri_);
  configfile.append("/config.xml");

  xmlDoc* doc = xmlReadFile(configfile.c_str(), nullptr, 0);
  xmlNode* root = xmlDocGetRootElement(doc);

  xmlChar* id = nullptr;
  xmlChar* version = nullptr;
  xmlChar* name = nullptr;
  xmlChar* content = nullptr;
  xmlChar* description = nullptr;
  xmlChar* author = nullptr;
  xmlChar* icon = nullptr;
  xmlChar* width = nullptr;
  xmlChar* height = nullptr;

  std::set<std::string> extensions_list;

  id = xmlGetProp(root, (const xmlChar*)"id");
  version = xmlGetProp(root, (const xmlChar*)"version");
  for (xmlNode* node = root->children; node; node = node->next) {
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
    if (!xmlStrcmp(node->name, (const xmlChar*)"window")) {
      width = xmlGetProp(node, (const xmlChar*)"width");
      height = xmlGetProp(node, (const xmlChar*)"height");
    }

    if (!xmlStrcmp(node->name, (const xmlChar*)"feature")) {
      xmlChar* feature_name = xmlGetProp(node, (const xmlChar*)"name");
      if (!xmlStrcmp(feature_name,
                     (const xmlChar*)"urn:AGL:widget:required-api")) {
        for (xmlNode* feature_child = node->children; feature_child;
             feature_child = feature_child->next) {
          if (!xmlStrcmp(feature_child->name, (const xmlChar*)"param")) {
            xmlChar* param_name =
                xmlGetProp(feature_child, (const xmlChar*)"name");
            xmlChar* param_value =
                xmlGetProp(feature_child, (const xmlChar*)"value");
            if (!xmlStrcmp(param_value, (const xmlChar*)"injection"))
              extensions_list.emplace((const char*)param_name);
            xmlFree(param_name);
            xmlFree(param_value);
          }
        }
      }
      xmlFree(feature_name);
    }
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
  obj["surface_role"] = static_cast<int>(surface_role_);
  obj["panel_type"] = static_cast<int>(panel_type_);
  Json::Value extensions_obj(Json::arrayValue);
  std::for_each(
      extensions_list.cbegin(), extensions_list.cend(),
      [&](const auto& extension) { extensions_obj.append(extension); });
  obj["extensions"] = extensions_obj;

  if (width)
    width_ = atoi((const char*)width);
  if (height)
    height_ = atoi((const char*)height);

  if (width_)
    obj["widthOverride"] = width_;
  if (height_)
    obj["heightOverride"] = height_;

  xmlFree(id);
  xmlFree(version);
  xmlFree(name);
  xmlFree(content);
  xmlFree(description);
  xmlFree(author);
  xmlFree(icon);
  xmlFree(width);
  xmlFree(height);
  xmlFreeDoc(doc);

  std::string app_desc = util::JsonToString(obj);
  std::string params = "{}";
  std::string app_id = obj["id"].asString();
  int err_code = 0;
  std::string err_msg;
  WebAppManagerService::OnLaunch(app_desc, params, app_id, err_code, err_msg);
}

void WebAppManagerServiceAGL::LaunchStartupAppFromURL() {
  LOG_DEBUG("WebAppManagerServiceAGL::LaunchStartupAppFromURL");
  LOG_DEBUG("    url: %s", startup_app_uri_.c_str());
  Json::Value obj(Json::objectValue);
  obj["id"] = startup_app_id_;
  obj["version"] = "1.0";
  obj["vendor"] = "some vendor";
  obj["type"] = "web";
  obj["main"] = startup_app_uri_;
  obj["title"] = "webapp";
  obj["uiRevision"] = "2";
  // obj["icon"] = (const char*)icon;
  // obj["folderPath"] = startup_app_.c_str();
  obj["surfaceId"] = startup_app_surface_id_;
  obj["surface_role"] = static_cast<int>(surface_role_);
  obj["panel_type"] = static_cast<int>(panel_type_);

  obj["widthOverride"] = width_;
  obj["heightOverride"] = height_;

  std::string app_desc = util::JsonToString(obj);
  std::string app_id = startup_app_id_;
  int err_code = 0;
  std::string params = "{}";
  std::string err_msg;

  LOG_DEBUG("Launching with appDesc=[%s]", app_desc.c_str());

  WebAppManagerService::OnLaunch(app_desc, params, app_id, err_code, err_msg);
  LOG_DEBUG("onLaunch: Done.");
}

Json::Value WebAppManagerServiceAGL::launchApp(const Json::Value& request) {
  return Json::Value(Json::objectValue);
}

Json::Value WebAppManagerServiceAGL::killApp(const Json::Value& request) {
  return Json::Value(Json::objectValue);
}

Json::Value WebAppManagerServiceAGL::pauseApp(const Json::Value& request) {
  return Json::Value(Json::objectValue);
}

Json::Value WebAppManagerServiceAGL::logControl(const Json::Value& request) {
  return Json::Value(Json::objectValue);
}

Json::Value WebAppManagerServiceAGL::setInspectorEnable(
    const Json::Value& request) {
  return Json::Value(Json::objectValue);
}

Json::Value WebAppManagerServiceAGL::closeAllApps(const Json::Value& request) {
  return Json::Value(Json::objectValue);
}

Json::Value WebAppManagerServiceAGL::discardCodeCache(
    const Json::Value& request) {
  return Json::Value(Json::objectValue);
}

Json::Value WebAppManagerServiceAGL::listRunningApps(const Json::Value& request,
                                                     bool subscribed) {
  return Json::Value(Json::objectValue);
}

Json::Value WebAppManagerServiceAGL::getWebProcessSize(
    const Json::Value& request) {
  return Json::Value(Json::objectValue);
}

Json::Value WebAppManagerServiceAGL::clearBrowsingData(
    const Json::Value& request) {
  return Json::Value(Json::objectValue);
}

Json::Value WebAppManagerServiceAGL::webProcessCreated(
    const Json::Value& request,
    bool subscribed) {
  return Json::Value(Json::objectValue);
}

void WebAppManagerServiceAGL::OnActivateEvent() {
  LOG_DEBUG("Activate app=%s", app_id_event_target_.c_str());
  WebAppBase* web_app =
      WebAppManager::Instance()->FindAppById(app_id_event_target_);
  if (web_app) {
    web_app->OnStageActivated();
    web_app->SendAglActivate(app_id_event_target_.c_str());
  } else {
    LOG_DEBUG("Not found app=%s running", app_id_event_target_.c_str());
  }
  app_id_event_target_.clear();
}

void WebAppManagerServiceAGL::OnDeactivateEvent() {
  LOG_DEBUG("Dectivate app=%s", app_id_event_target_.c_str());
  WebAppBase* web_app =
      WebAppManager::Instance()->FindAppById(app_id_event_target_);
  if (web_app)
    web_app->OnStageDeactivated();
  app_id_event_target_.clear();
}

void WebAppManagerServiceAGL::OnKillEvent() {
  LOG_DEBUG("Kill app=%s", app_id_event_target_.c_str());
  WebAppManager::Instance()->OnKillApp(app_id_event_target_,
                                       app_id_event_target_);
  app_id_event_target_.clear();
}
