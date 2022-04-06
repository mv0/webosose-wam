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

#include "web_runtime_agl.h"

#include <getopt.h>
#include <unistd.h>
#include <cassert>
#include <fstream>
#include <regex>

#include <glib.h>
#include <json/value.h>
#include <webos/app/webos_main.h>

#include "log_manager.h"
#include "platform_module_factory_impl.h"
#include "utils.h"
#include "web_app_manager.h"
#include "web_app_manager_service_agl.h"

const char kWebAppConfig_appinfo[] = "appinfo.json";

volatile sig_atomic_t e_flag = 1;

/*
 *   std::vector<const char*> data;
 *   data.push_back(kDeactivateEvent);
 *   data.push_back(this->id_.c_str());
 *   WebAppManagerServiceAGL::Instance()->SendEvent(data.size(), data.data());
 *
 *   used to send data
 */
static std::string GetAppId(Args* args, const char* app_afm_id) {
  if (args->is_set_flag(Args::FLAG_APP_ID))
    return args->app_id_;
  return std::string(app_afm_id);
}

static std::string GetAppUrl(Args* args) {
  if (args->is_set_flag(Args::FLAG_HTTP_LINK))
    return args->http_link_;
  return args->app_dir_;
}

static bool IsBrowserProcess(Args* args) {
  if (args->is_set_flag(Args::FLAG_APP_TYPE))
    return false;
  return true;
}

static std::string IsActivateApp(Args* args) {
  if (args->is_set_flag(Args::FLAG_ACTIVATE_APP))
    return args->activate_app_;
  return std::string();
}

static AglShellSurfaceType GetSurfaceType(const char* surface_type) {
  if (!strcmp(surface_type, "background"))
    return AglShellSurfaceType::kBackground;
  if (!strcmp(surface_type, "panel"))
    return AglShellSurfaceType::kPanel;

  return AglShellSurfaceType::kNone;
}

static enum AglShellPanelEdge GetSurfacePanelType(const char* panel_type) {
  if (!strcmp(panel_type, "top"))
    return AglShellPanelEdge::kTop;
  if (!strcmp(panel_type, "bottom"))
    return AglShellPanelEdge::kBottom;
  if (!strcmp(panel_type, "left"))
    return AglShellPanelEdge::kLeft;
  if (!strcmp(panel_type, "right"))
    return AglShellPanelEdge::kRight;

  return AglShellPanelEdge::kNotFound;
}

static bool IsSharedBrowserProcess(Args* args) {
  if (args->is_set_flag(Args::FLAG_APP_ID))
    return false;

  // if 'http://' param is not present then assume shared browser process
  if (args->is_set_flag(Args::FLAG_HTTP_LINK))
    return false;

  return true;
}

static bool IsWaitForHostService(void) {
  return util::GetEnvVar("WAIT_FOR_HOST_SERVICE") == "1";
}

class AGLMainDelegateWAM : public webos::WebOSMainDelegate {
 public:
  void AboutToCreateContentBrowserClient() override {
    WebAppManagerServiceAGL::Instance()->StartService();
    WebAppManager::Instance()->SetPlatformModules(
        std::make_unique<PlatformModuleFactoryImpl>());
  }
};

class AGLRendererDelegateWAM : public webos::WebOSMainDelegate {
 public:
  void AboutToCreateContentBrowserClient() override {}  // do nothing
};

int SingleBrowserProcessWebAppLauncher::Launch(const std::string& id,
                                               const std::string& uri,
                                               const std::string& surface_role,
                                               const std::string& panel_type,
                                               const std::string& width,
                                               const std::string& height) {
  rid_ = static_cast<int>(getpid());

  WebAppManagerServiceAGL::Instance()->SetStartupApplication(
      id, uri, rid_, AglShellSurfaceType::kNone, AglShellPanelEdge::kNotFound,
      0, 0);
  return rid_;
}

int SingleBrowserProcessWebAppLauncher::Loop(int argc,
                                             const char** argv,
                                             volatile sig_atomic_t& e_flag) {
  AGLMainDelegateWAM delegate;
  webos::WebOSMain webOSMain(&delegate);
  return webOSMain.Run(argc, argv);
}

int SharedBrowserProcessWebAppLauncher::Launch(const std::string& id,
                                               const std::string& uri,
                                               const std::string& surface_role,
                                               const std::string& panel_type,
                                               const std::string& width,
                                               const std::string& height) {
  if (!WebAppManagerServiceAGL::Instance()->InitializeAsHostClient()) {
    LOG_DEBUG("Failed to initialize as host client");
    return -1;
  }

  rid_ = static_cast<int>(getpid());
  std::string rid_s = std::to_string(rid_);

  std::vector<const char*> data;
  data.push_back(kStartApp);
  data.push_back(id.c_str());
  data.push_back(uri.c_str());
  data.push_back(rid_s.c_str());
  data.push_back(surface_role.c_str());
  data.push_back(panel_type.c_str());

  data.push_back(width.c_str());
  data.push_back(height.c_str());

  WebAppManagerServiceAGL::Instance()->LaunchOnHost(data.size(), data.data());
  return rid_;
}

int SharedBrowserProcessWebAppLauncher::Loop(int argc,
                                             const char** argv,
                                             volatile sig_atomic_t& e_flag) {
  // TODO: wait for a pid
  while (e_flag)
    sleep(1);

  std::string app_id = GetAppId(Args::Instance(), argv[0]);
  LOG_DEBUG("App finished, sending event: %s app: %s", kKilledApp,
            app_id.c_str());

  std::vector<const char*> data;
  data.push_back(kKilledApp);
  data.push_back(app_id.c_str());
  WebAppManagerServiceAGL::Instance()->SendEvent(data.size(), data.data());

  return 0;
}

static void AglShellActivateApp(const std::string& app_id) {
  if (!WebAppManagerServiceAGL::Instance()->InitializeAsHostClient()) {
    LOG_DEBUG("Failed to initialize as host client");
    return;
  }

  std::vector<const char*> data;

  data.push_back(kActivateEvent);
  data.push_back(app_id.c_str());

  WebAppManagerServiceAGL::Instance()->SendEvent(data.size(), data.data());
}

int WebAppLauncherRuntime::Run(int argc, const char** argv) {
  bool is_wait_host_service = IsWaitForHostService();
  std::string app_id = IsActivateApp(Args::Instance());

  if (is_wait_host_service) {
    while (!WebAppManagerServiceAGL::Instance()->IsHostServiceRunning()) {
      LOG_DEBUG("WebAppLauncherRuntime::run - waiting for host service");
      sleep(1);
    }
  }

  if (is_wait_host_service ||
      WebAppManagerServiceAGL::Instance()->IsHostServiceRunning()) {
    launcher_ = new SharedBrowserProcessWebAppLauncher();
  } else {
    LOG_DEBUG(
        "WebAppLauncherRuntime::run - creating "
        "SingleBrowserProcessWebAppLauncher");
    launcher_ = new SingleBrowserProcessWebAppLauncher();
  }

  if (!app_id.empty()) {
    app_id.erase(0, 15);
    AglShellActivateApp(app_id);
    return launcher_->Loop(argc, argv, e_flag);
  }

  id_ = GetAppId(Args::Instance(), argv[0]);
  url_ = GetAppUrl(Args::Instance());

  SetupSignals();

  if (!Init(Args::Instance()))
    return -1;

  std::string surface_role_str =
      std::to_string(static_cast<int>(surface_type_));
  std::string panel_type_str = std::to_string(static_cast<int>(panel_type_));

  /* Launch WAM application */
  launcher_->rid_ = launcher_->Launch(id_, url_, surface_role_str,
                                      panel_type_str, width_, height_);

  if (launcher_->rid_ < 0) {
    LOG_DEBUG("cannot launch WAM app (%s)", id_.c_str());
  }

  // take care 1st time launch
  LOG_DEBUG("waiting for notification: surface created");
  pending_create_ = true;

  return launcher_->Loop(argc, argv, e_flag);
}

void WebAppLauncherRuntime::SetupSignals() {
  auto sig_term_handler = [](int sig_num) {
    LOG_DEBUG("WebAppLauncherRuntime::run - received SIGTERM signal");
    e_flag = 0;
  };
  signal(SIGTERM, sig_term_handler);
}

bool WebAppLauncherRuntime::Init(Args* args) {
  // based on https://tools.ietf.org/html/rfc3986#page-50
  std::regex url_regex(
      R"(^(([^:\/?#]+):)?(//([^\/?#]*))?([^?#]*)(\?([^#]*))?(#(.*))?)",
      std::regex::extended);

  std::smatch url_match_result;
  if (std::regex_match(url_, url_match_result, url_regex)) {
    unsigned counter = 0;
    for (const auto& res : url_match_result) {
      LOG_DEBUG("    %d: %s", counter++, res.str().c_str());
    }

    if (url_match_result[4].length()) {
      std::string authority = url_match_result[4].str();
      std::size_t n = authority.find(':');
      if (n != std::string::npos) {
        std::string sport = authority.substr(n + 1);
        host_ = authority.substr(0, n);
        port_ = util::StrToIntWithDefault(sport, 0);
      } else {
        host_ = authority;
      }
    }

    if (!args->is_set_flag(Args::FLAG_APP_DIR)) {
      LOG_DEBUG("Application directory missing.");
      return false;
    }
    std::string path = args->app_dir_;
    path = path + "/" + kWebAppConfig_appinfo;

    if (!ParseJsonConfig(path.c_str())) {
      LOG_DEBUG("Error in appinfo.json");
      return false;
    }

    LOG_DEBUG(
        "id=[%s], name=[%s], url=[%s], host=[%s], port=%d, "
        "width=[%s], height[%s], surface_type[%d], panel_type[%d]",
        id_.c_str(), name_.c_str(), url_.c_str(), host_.c_str(), port_,
        width_.c_str(), height_.c_str(),
        static_cast<int>(surface_type_), static_cast<int>(panel_type_));

    return true;
  } else {
    LOG_DEBUG("Malformed url.");
    return false;
  }
}

bool WebAppLauncherRuntime::ParseJsonConfig(const char* path_to_config) {
  Json::Value root;
  Json::CharReaderBuilder builder;
  JSONCPP_STRING errs;

  std::ifstream ifs;
  ifs.open(path_to_config);

  if (!parseFromStream(builder, ifs, &root, &errs)) {
    LOG_DEBUG("Failed parse %s configuration file", path_to_config);
    return false;
  }

  name_ = root["name"].asString();
  std::string id = root["id"].asString();
  std::string version = root["version"].asString();
  std::string icon = root["icon"].asString();
  std::string content = root["content"].asString();
  std::string description = root["description"].asString();
  std::string author = root["author"].asString();

  std::string surface_type =
      root["surface"]
          .get("role", static_cast<int>(AglShellSurfaceType::kNone))
          .asString();
  std::string panel_type =
      root["surface"]
          .get("panel_edge", static_cast<int>(AglShellPanelEdge::kNotFound))
          .asString();

  height_ = root["surface"].get("height", std::string("0")).asString();
  width_ = root["surface"].get("width", std::string("0")).asString();

  surface_type_ = AglShellSurfaceType::kNone;
  panel_type_ = AglShellPanelEdge::kNotFound;

  if (surface_type !=
      std::to_string(static_cast<int>(AglShellSurfaceType::kNone))) {
    surface_type_ = GetSurfaceType(surface_type.c_str());

    if (panel_type !=
        std::to_string(static_cast<int>(AglShellPanelEdge::kNotFound))) {
      panel_type_ = GetSurfacePanelType(panel_type.c_str());
      if (panel_type_ == AglShellPanelEdge::kNotFound) {
        LOG_DEBUG("Failed to get a valid panel edge");
        return false;
      }
    }
  }

  return true;
}

int SharedBrowserProcessRuntime::Run(int argc, const char** argv) {
  if (WebAppManagerServiceAGL::Instance()->InitializeAsHostService()) {
    AGLMainDelegateWAM delegate;
    webos::WebOSMain webOSMain(&delegate);
    return webOSMain.Run(argc, argv);
  } else {
    LOG_DEBUG(
        "Trying to start shared browser process but process is already "
        "running");
    return -1;
  }
}

int RenderProcessRuntime::Run(int argc, const char** argv) {
  AGLMainDelegateWAM delegate;
  webos::WebOSMain webOSMain(&delegate);
  return webOSMain.Run(argc, argv);
}

static void print_help(void) {
  fprintf(stderr, "WAM: Web Application Manager\n");
  fprintf(stderr,
          "\t[--activate_app=appid] -- activate application. Interrnal "
          "usage.\n\tNot needing for starting applications.\n");
  fprintf(stderr,
          "\t[--type=zygote|utility] -- used to determine if WAM instance is a "
          "browser one.\n\tDo not use if starting application.\n");
  fprintf(
      stderr,
      "\t[--appid=appid] name of an application id.\n\tRequired if starting a "
      "web application.\n");
  fprintf(stderr,
          "\t[--app-install-dir=/path/to/root_index] installation path for web "
          "application.\n\tRequired if starting a web application.\n");
  fprintf(stderr, "\t-h -- this help message \n");
  exit(EXIT_FAILURE);
}

void Args::parse_args(int argc, const char** argv) {
  int c;
  int option_index;
  opterr = 0;

  copy_cmdline(argc, argv);

  struct option long_opts[] = {{"help", no_argument, 0, 'h'},
                               {"type", required_argument, 0, 't'},
                               {"activate-app", required_argument, 0, 'x'},
                               {"appid", required_argument, 0, 'a'},
                               {"app-install-dir", required_argument, 0, 'd'},
                               {0, 0, 0, 0}};

  while ((c = getopt_long(new_argc, new_argv, "ht:a:i:d:", long_opts,
                          &option_index)) != -1) {
    switch (c) {
      case 'h':
        print_help();
        break;
      case 't':
        set_flag(FLAG_APP_TYPE);
        type_ = std::string(optarg);
        break;
      case 'x':
        set_flag(FLAG_ACTIVATE_APP);
        activate_app_ = optarg;
        break;
      case 'a':
        set_flag(FLAG_APP_ID);
        app_id_ = std::string(optarg);
        break;
      case 'd':
        set_flag(FLAG_APP_DIR);
        app_dir_ = std::string(optarg);
        break;
      default:
        break;
    }
  }

  if (optind < new_argc) {
    // check for 'http://'
    int p = optind;
    while (p < new_argc) {
      if (!strcmp(new_argv[p], "http://")) {
        set_flag(FLAG_HTTP_LINK);
        http_link_ = std::string(new_argv[p]);
        break;
      }
      p++;
    }
  }
}

void Args::copy_cmdline(int argc, const char** argv) {
  new_argc = argc;
  new_argv = static_cast<char**>(calloc(new_argc + 1, sizeof(*new_argv)));

  for (int i = 0; i < new_argc; i++) {
    size_t len = strlen(argv[i]) + 1;
    new_argv[i] = static_cast<char*>(calloc(len, sizeof(char)));
    memcpy(new_argv[i], argv[i], len);
  }

  new_argv[argc] = nullptr;
}

void Args::clear_cmdline(void) {
  for (int i = 0; i < new_argc; i++)
    free(new_argv[i]);
  free(new_argv);
}

int WebRuntimeAGL::Run(int argc, const char** argv) {
  int ret;
  Args::Instance()->parse_args(argc, argv);

  LOG_DEBUG("WebRuntimeAGL::run");
  if (IsBrowserProcess(Args::Instance())) {
    if (IsSharedBrowserProcess(Args::Instance())) {
      LOG_DEBUG("WebRuntimeAGL - creating SharedBrowserProcessRuntime");
      runtime_ = new SharedBrowserProcessRuntime();
    } else {
      LOG_DEBUG("WebRuntimeAGL - creating WebAppLauncherRuntime");
      runtime_ = new WebAppLauncherRuntime();
    }
  } else {
    LOG_DEBUG("WebRuntimeAGL - creating RenderProcessRuntime");
    runtime_ = new RenderProcessRuntime();
  }

  ret = runtime_->Run(argc, argv);
  Args::Instance()->clear_cmdline();
  return ret;
}

std::unique_ptr<WebRuntime> WebRuntime::Create() {
  return std::make_unique<WebRuntimeAGL>();
}
