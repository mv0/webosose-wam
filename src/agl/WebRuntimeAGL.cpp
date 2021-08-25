#include <cassert>
#include <regex>
#include <unistd.h>

#include <glib.h>
#include <libxml/parser.h>

#include <libhomescreen.hpp>

#include <webos/app/webos_main.h>

#include "WebRuntimeAGL.h"
#include "LogManager.h"
#include "PlatformModuleFactoryImpl.h"
#include "StringUtils.h"
#include "WebAppManager.h"
#include "WebAppManagerServiceAGL.h"
#include "AglShellSurface.h"


#define WEBAPP_CONFIG "config.xml"

volatile sig_atomic_t e_flag = 1;

/*
 *   std::vector<const char*> data;
 *   data.push_back(kDeactivateEvent);
 *   data.push_back(this->m_id.c_str());
 *   WebAppManagerServiceAGL::instance()->sendEvent(data.size(), data.data());
 *
 *   used to send data
 */

static std::string getAppId(const std::vector<std::string>& args) {
  const char *afm_id = getenv("AFM_ID");
  if (afm_id == nullptr || !afm_id[0]) {
    return args[0];
  } else {
    return std::string(afm_id);
  }
}

static std::string getAppUrl(const std::vector<std::string>& args) {
  for (size_t i=0; i < args.size(); i++) {
    std::size_t found = args[i].find(std::string("http://"));
    if (found != std::string::npos)
        return args[i];
  }
  return std::string();
}

static bool isBrowserProcess(const std::vector<std::string>& args) {
  // if type is not given then we are browser process
  for (size_t i=0; i < args.size(); i++) {
    std::string param("--type=");
    std::size_t found = args[i].find(param);
    if (found != std::string::npos)
        return false;
  }
  return true;
}

static std::string
is_activate_app(const std::vector<std::string>& args)
{
    for (size_t i = 0; i < args.size(); i++) {
        if (args[i].find("--activate-app=") != std::string::npos) {
            return args[i];
        }
    }
    return std::string();
}

static bool isSharedBrowserProcess(const std::vector<std::string>& args) {
  // if 'http://' param is not present then assume shared browser process
  for (size_t i=0; i < args.size(); i++) {
    std::size_t found = args[i].find(std::string("http://"));
    if (found != std::string::npos)
        return false;
  }
  return true;
}

static bool isWaitForHostService(const std::vector<std::string>& args) {
  const char *value = getenv("WAIT_FOR_HOST_SERVICE");
  if (value == nullptr || !value[0]) {
    return false;
  } else {
    return (strcmp(value, "1") == 0);
  }
}

static std::string
surface_compose_entry_point(std::string src, std::string host,
							int port, std::string token)
{
	std::string entryPoint = std::string("http://");

	entryPoint.append(host);
	entryPoint.append(":");
	entryPoint.append(std::to_string(port));

	auto found = src.find("/");

	if (found == std::string::npos) {
		entryPoint.append("/");
		entryPoint.append(src);
		entryPoint.append("/");
	} else {
		if (found > 0) {
			entryPoint.append("/");
			entryPoint.append(src);
		} else {
			entryPoint.append("/");
		}
	}


	entryPoint.append("index.html");
	entryPoint.append("?token=");
	entryPoint.append(token);

	return entryPoint;
}


class AGLMainDelegateWAM : public webos::WebOSMainDelegate {
public:
    void AboutToCreateContentBrowserClient() override {
      WebAppManagerServiceAGL::instance()->startService();
      WebAppManager::instance()->setPlatformModules(std::unique_ptr<PlatformModuleFactoryImpl>(new PlatformModuleFactoryImpl()));
    }
};

class AGLRendererDelegateWAM : public webos::WebOSMainDelegate {
public:
    void AboutToCreateContentBrowserClient() override {
      // do nothing
    }
};

void Launcher::register_surfpid(pid_t app_pid, pid_t surf_pid)
{
  if (app_pid != m_rid)
    return;
  bool result = m_pid_map.insert({app_pid, surf_pid}).second;
  if (!result) {
    LOG_DEBUG("register_surfpid, (app_pid=%d) already registered surface_id with (surface_id=%d)",
            (int)app_pid, (int)surf_pid);
  }
}

void Launcher::unregister_surfpid(pid_t app_pid, pid_t surf_pid)
{
  size_t erased_count = m_pid_map.erase(app_pid);
  if (erased_count == 0) {
    LOG_DEBUG("unregister_surfpid, (app_pid=%d) doesn't have a registered surface",
            (int)app_pid);
  }
}

pid_t Launcher::find_surfpid_by_rid(pid_t app_pid)
{
  auto surface_id = m_pid_map.find(app_pid);
  if (surface_id != m_pid_map.end()) {
    LOG_DEBUG("found return(%d, %d)", (int)app_pid, (int)surface_id->second);
    return surface_id->second;
  }
  return -1;
}

int
SingleBrowserProcessWebAppLauncher::launch(const std::string& id,
                                           const std::string& uri,
                                           std::list<AglShellSurface> surfaces,
                                           const std::string& width,
                                           const std::string& height)
{
    m_rid = (int) getpid();

    WebAppManagerServiceAGL::instance()->setStartupApplication(id, uri, m_rid, 0, 0, surfaces);
    return m_rid;
}

int SingleBrowserProcessWebAppLauncher::loop(int argc, const char** argv, volatile sig_atomic_t& e_flag) {
  AGLMainDelegateWAM delegate;
  webos::WebOSMain webOSMain(&delegate);
  return webOSMain.Run(argc, argv);
}

void
SharedBrowserProcessWebAppLauncher::send_ready(void)
{
    if (ready_timer_id_.empty()) {
            return;
    }

    std::vector<const char*> ndata;

    ndata.push_back(kSendAglReady);
    ndata.push_back(ready_timer_id_.c_str());

    LOG_DEBUG("SharedBrowserProcessWebAppLauncher::send_ready() before doing sendEvent with verb %s", kSendAglReady);
    WebAppManagerServiceAGL::instance()->sendEvent(ndata.size(), ndata.data());
}

int
SharedBrowserProcessWebAppLauncher::launch(const std::string& id,
                                           const std::string& uri,
                                           std::list<AglShellSurface> surfaces,
                                           const std::string& width,
                                           const std::string& height)
{
    if (!WebAppManagerServiceAGL::instance()->initializeAsHostClient()) {
        LOG_DEBUG("Failed to initialize as host client");
        return -1;
    }

    m_rid = (int)getpid();
    std::string m_rid_s = std::to_string(m_rid);

    std::vector<const char*> data;

    data.push_back(kStartApp);
    data.push_back(id.c_str());
    data.push_back(uri.c_str());
    data.push_back(m_rid_s.c_str());
    data.push_back(width.c_str());
    data.push_back(height.c_str());

    WebAppManagerServiceAGL::instance()->launchOnHost(data.size(), data.data(), surfaces);

    if (!surfaces.empty()) {
        ready_timer_id_ = id;
        send_ready();
    }

    return m_rid;
}

int SharedBrowserProcessWebAppLauncher::loop(int argc, const char** argv, volatile sig_atomic_t& e_flag) {
  // TODO: wait for a pid
  while (e_flag)
    sleep(1);

  std::vector<std::string> args(argv + 1, argv + argc);
  std::string app_id = getAppId(args);
  LOG_DEBUG("App finished, sending event: %s app: %s", kKilledApp, app_id.c_str());

  std::vector<const char*> data;
  data.push_back(kKilledApp);
  data.push_back(app_id.c_str());
  WebAppManagerServiceAGL::instance()->sendEvent(data.size(), data.data());

  return 0;
}


static void
agl_shell_activate_app(std::string &app_id)
{
	if (!WebAppManagerServiceAGL::instance()->initializeAsHostClient()) {
		LOG_DEBUG("Failed to initialize as host client");
		return;
	}

	std::vector<const char*> data;

	data.push_back(kActivateEvent);
	data.push_back(app_id.c_str());

	WebAppManagerServiceAGL::instance()->sendEvent(data.size(), data.data());
}

int WebAppLauncherRuntime::run(int argc, const char** argv) {
  std::vector<std::string> args(argv + 1, argv + argc);
  bool isWaitHostService = isWaitForHostService(args);
  std::string app_id = is_activate_app(args);

  if(isWaitHostService) {
    while(!WebAppManagerServiceAGL::instance()->isHostServiceRunning()) {
      LOG_DEBUG("WebAppLauncherRuntime::run - waiting for host service");
      sleep(1);
    }
  }

  if(isWaitHostService || WebAppManagerServiceAGL::instance()->isHostServiceRunning()) {
    LOG_DEBUG("WebAppLauncherRuntime::run - creating SharedBrowserProcessWebAppLauncher");
    m_launcher = new SharedBrowserProcessWebAppLauncher();
  } else {
    LOG_DEBUG("WebAppLauncherRuntime::run - creating SingleBrowserProcessWebAppLauncher");
    m_launcher = new SingleBrowserProcessWebAppLauncher();
  }

  if (!app_id.empty()) {
	  app_id.erase(0, 15);
	  agl_shell_activate_app(app_id);
	  return m_launcher->loop(argc, argv, e_flag);
  }

  m_id = getAppId(args);
  m_url = getAppUrl(args);
  m_role = "WebApp";

  setup_signals();

  if (!init())
    return -1;

  /* Launch WAM application */
  m_launcher->m_rid = m_launcher->launch(m_id, m_url, surfaces, m_width, m_height);

  if (m_launcher->m_rid < 0) {
    LOG_DEBUG("cannot launch WAM app (%s)", m_id.c_str());
  }

  // take care 1st time launch
  LOG_DEBUG("waiting for notification: surface created");
  m_pending_create = true;

  return m_launcher->loop(argc, argv, e_flag);
}

void WebAppLauncherRuntime::setup_signals() {
  auto sig_term_handler = [](int sig_num) {
    LOG_DEBUG("WebAppLauncherRuntime::run - received SIGTERM signal");
    e_flag = 0;
  };
  signal(SIGTERM, sig_term_handler);
}

bool WebAppLauncherRuntime::init() {
  // based on https://tools.ietf.org/html/rfc3986#page-50
  std::regex url_regex (
    R"(^(([^:\/?#]+):)?(//([^\/?#]*))?([^?#]*)(\?([^#]*))?(#(.*))?)",
    std::regex::extended
  );

  std::smatch url_match_result;
  if (std::regex_match(m_url, url_match_result, url_regex)) {
    unsigned counter = 0;
    for (const auto& res : url_match_result) {
      LOG_DEBUG("    %d: %s", counter++, res.str().c_str());
    }

    if (url_match_result[4].length()) {
      std::string authority = url_match_result[4].str();
      std::size_t n = authority.find(':');
      if (n != std::string::npos) {
        std::string sport = authority.substr(n+1);
        m_host = authority.substr(0, n);
        m_role.push_back('-');
        m_role.append(m_host);
        m_role.push_back('-');
        m_role.append(sport);
        m_port = stringTo<int>(sport);
      } else {
        m_host = authority;
        m_role.push_back('-');
        m_role.append(m_host);
      }
    }

    bool url_misses_token = true;
    if (url_match_result[7].length()) {
      std::string query = url_match_result[7].str();
      std::size_t n = query.find('=');
      if (n != std::string::npos) {
        m_token = query.substr(n+1);
        url_misses_token = false;
      }
    }
    if (url_misses_token) {
      char *tokenv = getenv("CYNAGOAUTH_TOKEN");
      if (tokenv) {
        m_token = tokenv;
        m_url.push_back(url_match_result[7].length() ? '&' : '?');
        m_url.append("token=");
        m_url.append(m_token);
      }
    }

    std::string path = std::string(getenv("AFM_APP_INSTALL_DIR"));
    if (path.empty()) {
	    LOG_DEBUG("Please set AFM_APP_INSTALL_DIR");
	    return false;
    }
    path = path + "/" + WEBAPP_CONFIG;

    // Parse config file of runxdg
    if (parse_config(path.c_str())) {
      LOG_DEBUG("Error in config");
      return false;
    }

    // Special cases for windowmanager roles
    if (m_id.rfind("webapps-html5-homescreen", 0) == 0)
      m_role = "homescreen";
    else if (m_id.rfind("webapps-homescreen", 0) == 0)
      m_role = "homescreen";

    LOG_DEBUG("id=[%s], name=[%s], role=[%s], url=[%s], host=[%s], port=%d, token=[%s], width=[%s], height[%s]",
              m_id.c_str(), m_name.c_str(), m_role.c_str(), m_url.c_str(),
              m_host.c_str(), m_port, m_token.c_str(), m_width.c_str(),
              m_height.c_str());

    // Setup HomeScreen API
    if (!init_hs()) {
      LOG_DEBUG("cannot setup hs API");
      return false;
    }

    return true;
  } else {
    LOG_DEBUG("Malformed url.");
    return false;
  }
}

bool WebAppLauncherRuntime::init_hs() {
  m_hs = new LibHomeScreen();
  if (m_hs->init(m_host.c_str(), m_port, m_token.c_str())) {
    LOG_DEBUG("cannot initialize homescreen");
    return false;
  }

  std::function< void(json_object*) > handler = [this] (json_object* object) {
    LOG_DEBUG("Activate app %s ", this->m_id.c_str());
    agl_shell_activate_app(this->m_id);
  };
  m_hs->set_event_handler(LibHomeScreen::Event_ShowWindow, handler);

  return true;
}

void
WebAppLauncherRuntime::parse_config_client_shell(xmlNode *root_node)
{
    bool bg_found = false;

    for (xmlNode *node = root_node->children; node; node = node->next) {
        if (!xmlStrcmp(node->name, (const xmlChar *) "surface")) {
            const char *c_surface_type =
                (const char *) xmlGetProp(node, (const xmlChar *) "role");

            if (!strcmp(c_surface_type, "panel")) {
                AglShellSurface aglSurface;
                Panel panel;
                Surface surface;
                std::string entryPoint;

                xmlChar *width = xmlGetProp(node,  (const xmlChar *) "width");
                xmlChar *source = xmlGetProp(node, (const xmlChar *) "src");
                xmlChar *edge = xmlGetProp(node, (const xmlChar *) "edge");

                assert(source);
                assert(width);
                assert(edge);

                surface.setSurfaceType(PANEL);
                panel.setPanelEdge((const char *) edge);
                panel.setPanelWidth((const char *) width);

                aglSurface.setPanel(panel);
                aglSurface.setSurface(surface);
                aglSurface.setSrc(std::string((char *) source));

                entryPoint =
                    surface_compose_entry_point(aglSurface.getSrc(), m_host,
                                                m_port, m_token);
                aglSurface.setEntryPoint(entryPoint);

                surfaces.push_back(aglSurface);

            } else if (!strcmp(c_surface_type, "background")) {
                AglShellSurface aglSurface;
                Panel panel;
                Surface surface;
                std::string entryPoint;

                assert(!bg_found);

                xmlChar *source = xmlGetProp(node, (const xmlChar *) "src");
                assert(source);

                surface.setSurfaceType(BACKGROUND);
                aglSurface.setSurface(surface);
                aglSurface.setSrc(std::string((char *) source));

                entryPoint =
                    surface_compose_entry_point(aglSurface.getSrc(), m_host,
                                                m_port, m_token);
                aglSurface.setEntryPoint(entryPoint);

                surfaces.push_back(aglSurface);

                bg_found = true;
            }
        }
    }

    /* need at least a background */
    assert(bg_found);
}


int WebAppLauncherRuntime::parse_config (const char *path_to_config)
{
  xmlDoc *doc = xmlReadFile(path_to_config, nullptr, 0);
  xmlNode *root = xmlDocGetRootElement(doc);

  xmlChar *id = nullptr;
  xmlChar *version = nullptr;
  xmlChar *name = nullptr;
  xmlChar *content = nullptr;
  xmlChar *description = nullptr;
  xmlChar *author = nullptr;
  xmlChar *icon = nullptr;

  xmlChar *width = nullptr;
  xmlChar *height = nullptr;

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

    if (!xmlStrcmp(node->name, (const xmlChar*) "window")) {
      width = xmlGetProp(node, (const xmlChar*) "width");
      height = xmlGetProp(node, (const xmlChar*) "height");
    }

    if (!xmlStrcmp(node->name, (const xmlChar*) "client-shell")) {
	parse_config_client_shell(node);
    }
  }

  fprintf(stdout, "...parse_config...\n");

  LOG_DEBUG("id: %s", id);
  LOG_DEBUG("version: %s", version);
  LOG_DEBUG("name: %s", name);
  LOG_DEBUG("content: %s", content);
  LOG_DEBUG("description: %s", description);
  LOG_DEBUG("author: %s", author);
  LOG_DEBUG("icon: %s", icon);
  LOG_DEBUG("width: %s", width);
  LOG_DEBUG("height %s", height);

  m_name = std::string((const char*)name);
  if (width)
	  m_width = std::string((const char *) width);
  else
	  m_width = std::string("0");

  if (height)
	  m_height = std::string((const char *) height);
  else
	  m_height = std::string("0");


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

  return 0;
}

int SharedBrowserProcessRuntime::run(int argc, const char** argv) {
  if (WebAppManagerServiceAGL::instance()->initializeAsHostService()) {
    AGLMainDelegateWAM delegate;
    webos::WebOSMain webOSMain(&delegate);
    return webOSMain.Run(argc, argv);
  } else {
    LOG_DEBUG("Trying to start shared browser process but process is already running");
    return -1;
  }
}

int RenderProcessRuntime::run(int argc, const char** argv) {
  AGLMainDelegateWAM delegate;
  webos::WebOSMain webOSMain(&delegate);
  return webOSMain.Run(argc, argv);
}

int WebRuntimeAGL::run(int argc, const char** argv) {
  LOG_DEBUG("WebRuntimeAGL::run");
  std::vector<std::string> args(argv + 1, argv + argc);
  if (isBrowserProcess(args)) {
    if (isSharedBrowserProcess(args)) {
      LOG_DEBUG("WebRuntimeAGL - creating SharedBrowserProcessRuntime");
      m_runtime = new SharedBrowserProcessRuntime();
    }  else {
      LOG_DEBUG("WebRuntimeAGL - creating WebAppLauncherRuntime");
      m_runtime = new WebAppLauncherRuntime();
    }
  } else {
    LOG_DEBUG("WebRuntimeAGL - creating RenderProcessRuntime");
    m_runtime = new RenderProcessRuntime();
  }

  return m_runtime->run(argc, argv);
}

