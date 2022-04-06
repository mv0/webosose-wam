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

#ifndef AGL_WEB_APP_MANAGER_SERVICE_AGL_H
#define AGL_WEB_APP_MANAGER_SERVICE_AGL_H

#include <memory>

#include "agl_shell_types.h"
#include "timer.h"
#include "web_app_manager_service.h"

constexpr char kStartApp[] = "start-app";
constexpr char kKilledApp[] = "killed-app";
constexpr char kActivateEvent[] = "activate-event";
constexpr char kDeactivateEvent[] = "deactivate-event";

class WamSocket;
class WamSocketLockFile;

class WebAppManagerServiceAGL : public WebAppManagerService {
 public:
  static WebAppManagerServiceAGL* Instance();

  bool InitializeAsHostService();
  bool InitializeAsHostClient();

  bool IsHostServiceRunning();

  void SetStartupApplication(const std::string& startup_app_id,
                             const std::string& startup_app_uri,
                             int startup_app_surface_id,
                             AglShellSurfaceType surface_role,
                             AglShellPanelEdge panel_type,
                             int width,
                             int height);
  void SetAppIdForEventTarget(const std::string& app_id);

  void LaunchOnHost(int argc, const char** argv);
  void SendEvent(int argc, const char** argv);

  // WebAppManagerService
  bool StartService() override;
  Json::Value launchApp(const Json::Value& request) override;
  Json::Value killApp(const Json::Value& request) override;
  Json::Value pauseApp(const Json::Value& request) override;
  Json::Value logControl(const Json::Value& request) override;
  Json::Value setInspectorEnable(const Json::Value& request) override;
  Json::Value closeAllApps(const Json::Value& request) override;
  Json::Value discardCodeCache(const Json::Value& request) override;
  Json::Value listRunningApps(const Json::Value& request,
                              bool subscribed) override;
  Json::Value getWebProcessSize(const Json::Value& request) override;
  Json::Value clearBrowsingData(const Json::Value& request) override;
  Json::Value webProcessCreated(const Json::Value& request,
                                bool subscribed) override;

  void TriggerStartupApp();
  void TriggetEventForApp(const std::string& action);

 private:
  WebAppManagerServiceAGL();

  void LaunchStartupAppFromConfig();
  void LaunchStartupAppFromJsonConfig();
  void LaunchStartupAppFromURL();

  void OnActivateEvent();
  void OnDeactivateEvent();
  void OnKillEvent();

  std::string app_id_event_target_;

  std::string startup_app_id_;
  std::string startup_app_uri_;
  AglShellSurfaceType surface_role_;
  AglShellPanelEdge panel_type_;
  int width_;
  int height_;

  int startup_app_surface_id_;
  OneShotTimer<WebAppManagerServiceAGL> startup_app_timer_;

  std::unique_ptr<WamSocket> socket_;
  std::unique_ptr<WamSocketLockFile> lock_file_;
};

#endif  // AGL_WEB_APP_MANAGER_SERVICE_AGL_H
