#ifndef WEBRUNTIME_AGL_H
#define WEBRUNTIME_AGL_H

#include <unordered_map>
#include <list>
#include <signal.h>
#include <string>
#include <unordered_map>

#include "WebRuntime.h"
#include <libxml/parser.h>

class LibHomeScreen;

enum agl_shell_surface_type {
    AGL_SHELL_TYPE_BACKGROUND,
    AGL_SHELL_TYPE_PANEL,
};

enum agl_shell_desktop_surface_type {
    AGL_SHELL_TYPE_DESKTOP,
    AGL_SHELL_TYPE_POPUP,
    AGL_SHELL_TYPE_FULLSCREEN,
    AGL_SHELL_TYPE_SPLIT_V,
    AGL_SHELL_TYPE_SPLIT_H,
    AGL_SHELL_TYPE_REMOTE
};

enum agl_shell_panel_edge {
    AGL_SHELL_PANEL_TOP,
    AGL_SHELL_PANEL_BOTTOM,
    AGL_SHELL_PANEL_LEFT,
    AGL_SHELL_PANEL_RIGHT,
};

struct agl_shell_panel {
    void to_edge(const char *edge);
    void init(const char *edge, const char *width);

    enum agl_shell_panel_edge edge;
    int width;
};

struct agl_shell_surface {
    enum agl_shell_surface_type surface_type;
    struct agl_shell_panel panel;
    std::string src;
};

class Launcher {
public:
  virtual void register_surfpid(pid_t app_pid, pid_t surf_pid);
  virtual void unregister_surfpid(pid_t app_pid, pid_t surf_pid);
  virtual pid_t find_surfpid_by_rid(pid_t app_pid);
  virtual int launch(const std::string& id, const std::string& uri,
                     std::list<struct agl_shell_surface> surfaces,
                     const std::string& width, const std::string& height) = 0;
  virtual int loop(int argc, const char** argv, volatile sig_atomic_t& e_flag) = 0;

  int m_rid = 0;
  std::unordered_map<pid_t, pid_t> m_pid_map; // pair of <app_pid, pid which creates a surface>
};

class SharedBrowserProcessWebAppLauncher : public Launcher {
public:
  int launch(const std::string& id, const std::string& uri, std::list<struct agl_shell_surface> surfaces, const std::string& width, const std::string& height) override;
  int loop(int argc, const char** argv, volatile sig_atomic_t& e_flag) override;
};

class SingleBrowserProcessWebAppLauncher : public Launcher {
public:
  int launch(const std::string& id, const std::string& uri, std::list<struct agl_shell_surface> surfaces, const std::string& width, const std::string& height) override;
  int loop(int argc, const char** argv, volatile sig_atomic_t& e_flag) override;
};

class WebAppLauncherRuntime  : public WebRuntime {
public:
  int run(int argc, const char** argv) override;

private:

  bool init();
  bool init_wm();
  bool init_hs();
  int parse_config(const char *file);
  void parse_config_client_shell(xmlNode *root_node);
  void setup_surface (int id);
  void setup_signals();

  std::string m_id;
  std::string m_role;
  std::string m_url;
  std::string m_name;
  std::string m_host;
  std::string m_width;
  std::string m_height;

  std::list<struct agl_shell_surface> surfaces;	/* this runtime manages these
                                                   surfaces, if the surfaces
                                                   list is empty we're just a
                                                   simple runtime */

  int m_port;
  std::string m_token;

  Launcher *m_launcher;

  LibHomeScreen *m_hs = nullptr;

  std::unordered_map<int, int> m_surfaces;  // pair of <afm:rid, ivi:id>
  bool m_pending_create = false;
};

class SharedBrowserProcessRuntime  : public WebRuntime {
public:
  int run(int argc, const char** argv) override;
};

class RenderProcessRuntime  : public WebRuntime {
public:
  int run(int argc, const char** argv) override;
};

class WebRuntimeAGL : public WebRuntime {
public:
  int run(int argc, const char** argv) override;

private:

  WebRuntime *m_runtime;
};

#endif // WEBRUNTIME_AGL_H
