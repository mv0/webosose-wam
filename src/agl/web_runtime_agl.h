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

#ifndef AGL_WEBRUNTIME_AGL_H
#define AGL_WEBRUNTIME_AGL_H

#include <signal.h>
#include <string>
#include <unordered_map>

#include "agl_shell_types.h"
#include "web_runtime.h"

class Args {
 public:
  enum flags {
    FLAG_NONE = 0,
    FLAG_APP_TYPE = 1 << 0,
    FLAG_ACTIVATE_APP = 1 << 1,
    FLAG_HTTP_LINK = 1 << 2,
    FLAG_APP_ID = 1 << 3,
    FLAG_APP_DIR = 1 << 4,
  };

  static Args* Instance() {
    static Args* args = new Args();
    return args;
  }
  void parse_args(int argc, const char** argv);

  inline void set_flag(unsigned int flag) { flags |= flag; }

  inline void clear_flag(unsigned int flag) { flags &= ~flag; }

  inline bool is_set_flag(unsigned int flag) { return (flags & flag) == flag; }
  void clear_cmdline(void);

  std::string type_;
  std::string activate_app_;
  std::string http_link_;
  std::string app_id_;
  std::string app_dir_;

 private:
  uint32_t flags = FLAG_NONE;
  void copy_cmdline(int argc, const char** argv);
  char** new_argv;
  int new_argc;
};

class Launcher {
 public:
  virtual int Launch(const std::string& id,
                     const std::string& uri,
                     const std::string& surface_role,
                     const std::string& panel_type,
                     const std::string& width,
                     const std::string& height) = 0;
  virtual int Loop(int argc,
                   const char** argv,
                   volatile sig_atomic_t& e_flag) = 0;

  int rid_ = 0;
};

class SharedBrowserProcessWebAppLauncher : public Launcher {
 public:
  int Launch(const std::string& id,
             const std::string& uri,
             const std::string& surface_role,
             const std::string& panel_type,
             const std::string& width,
             const std::string& height) override;
  int Loop(int argc, const char** argv, volatile sig_atomic_t& e_flag) override;
};

class SingleBrowserProcessWebAppLauncher : public Launcher {
 public:
  int Launch(const std::string& id,
             const std::string& uri,
             const std::string& surface_role,
             const std::string& panel_type,
             const std::string& width,
             const std::string& height) override;
  int Loop(int argc, const char** argv, volatile sig_atomic_t& e_flag) override;
};

class WebAppLauncherRuntime : public WebRuntime {
 public:
  int Run(int argc, const char** argv) override;

 private:
  bool Init(Args* args);
  bool InitWM();
  bool InitHS();
  bool ParseJsonConfig(const char* file);
  void SetupSignals();

  std::string id_;
  std::string url_;
  std::string name_;
  std::string host_;
  std::string width_;
  std::string height_;

  AglShellSurfaceType surface_type_ = AglShellSurfaceType::kNone;
  AglShellPanelEdge panel_type_ =
      AglShellPanelEdge::kNotFound; /* only of surface_type is panel */

  int port_;
  Launcher* launcher_;

  std::unordered_map<int, int> surfaces_;  // pair of <afm:rid, ivi:id>
  bool pending_create_ = false;
};

class SharedBrowserProcessRuntime : public WebRuntime {
 public:
  int Run(int argc, const char** argv) override;
};

class RenderProcessRuntime : public WebRuntime {
 public:
  int Run(int argc, const char** argv) override;
};

class WebRuntimeAGL : public WebRuntime {
 public:
  int Run(int argc, const char** argv) override;

 private:
  WebRuntime* runtime_;
};

#endif  // WEBRUNTIME_AGL_H
