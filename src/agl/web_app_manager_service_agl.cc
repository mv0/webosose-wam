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
#include <fstream>
#include <iostream>
#include <set>
#include <sstream>

#include <json/value.h>

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

  width_ = width;
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
          10, this, &WebAppManagerServiceAGL::LaunchStartupAppFromJsonConfig);
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

void WebAppManagerServiceAGL::LaunchStartupAppFromJsonConfig() {
  std::string configfile;
  configfile.append(startup_app_uri_);
  configfile.append("/appinfo.json");

  Json::Value root;
  Json::CharReaderBuilder builder;
  JSONCPP_STRING errs;

  std::ifstream ifs;
  ifs.open(configfile.c_str());

  if (!parseFromStream(builder, ifs, &root, &errs)) {
    LOG_DEBUG("Failed to parse %s configuration file", configfile.c_str());
  }

  if (width_)
    root["widthOverride"] = width_;
  if (height_)
    root["heightOverride"] = height_;

  root["surface_role"] = static_cast<int>(surface_role_);
  root["panel_type"] = static_cast<int>(panel_type_);

  std::string app_desc = util::JsonToString(root);
  std::string params = "{}";
  std::string app_id = root["id"].asString();
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
