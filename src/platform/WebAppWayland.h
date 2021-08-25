// Copyright (c) 2014-2019 LG Electronics, Inc.
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

#ifndef WEBAPPWAYLAND_H
#define WEBAPPWAYLAND_H

#include "DisplayId.h"
#include "Timer.h"
#include "WebAppBase.h"

#include "WebPageBlinkObserver.h"
#include "ShellSurface.h"

#include "webos/common/webos_constants.h"
#include "webos/common/webos_event.h"
#include "webos/webos_platform.h"

namespace Json {
class Value;
}

class ApplicationDescription;
class WebAppWaylandWindow;

class InputManager : public webos::InputPointer {
public:
    static InputManager* instance()
    {
        // InputManager should be globally one.
        if (!webos::WebOSPlatform::GetInstance()->GetInputPointer())
            webos::WebOSPlatform::GetInstance()->SetInputPointer(new InputManager());
        return static_cast<InputManager*>(webos::WebOSPlatform::GetInstance()->GetInputPointer());
    }

    bool globalCursorVisibility() { return IsVisible(); }

    // Overridden from weboswayland::InputPointer:
    void OnCursorVisibilityChanged(bool visible) override;
};

class WebAppWayland : public WebAppBase, WebPageBlinkObserver {
public:

    WebAppWayland(const std::string& type,
		  int surface_id,
                  int width = 0, int height = 0,
                  int displayId = kUndefinedDisplayId,
                  const std::string& location_hint = "", ShellSurface *surface = nullptr);
    WebAppWayland(const std::string& type, WebAppWaylandWindow* window,
                  int width = 0, int height = 0,
                  int displayId = kUndefinedDisplayId,
                  const std::string& location_hint = "", ShellSurface *surface = nullptr);

    ~WebAppWayland() override;

    bool isAglRoleType();

    // WebAppBase
    void init(int width, int height, int surface_id) override;
    void attach(WebPageBase*) override;
    WebPageBase* detach() override;
    void suspendAppRendering() override;
    void resumeAppRendering() override;
    bool isFocused() const override;
    void resize(int width, int height) override;
    bool isActivated() const override;
    bool isMinimized() override;
    bool isNormal() override;
    void onStageActivated() override;
    void onStageDeactivated() override;
    void configureWindow(const std::string& type) override;
    void setKeepAlive(bool keepAlive) override;
    bool isWindowed() const override { return true; }
    void setWindowProperty(const std::string& name, const std::string& value) override;
    void platformBack() override;
    void setCursor(const std::string& cursorArg, int hotspot_x = -1, int hotspot_y = -1) override;
    void setInputRegion(const Json::Value& jsonDoc) override;
    void setKeyMask(const Json::Value& jsonDoc) override;
    void setOpacity(float opacity) override;
    void hide(bool forcedHide = false) override;
    void focus() override;
    void unfocus() override;
    void raise() override;
    void goBackground() override;
    void deleteSurfaceGroup() override;
    void keyboardVisibilityChanged(bool visible, int height) override;
    void doClose() override;

    void sendAglReady() override;
    void setAglAppId(const char *app_id) override;
    void sendAglActivate(const char *app_id) override;

    // WebAppWayland
    virtual bool isKeyboardVisible() override;
    virtual void setKeyMask(webos::WebOSKeyMask keyMask, bool value);
    virtual void setKeyMask(webos::WebOSKeyMask keyMask);
    virtual void focusOwner();
    virtual void focusLayer();
    virtual void titleChanged();
    virtual void firstFrameVisuallyCommitted();
    virtual void navigationHistoryChanged();
    virtual bool hideWindow();

    std::string getWindowType() const { return m_windowType; }
    bool cursorVisibility() { return InputManager::instance()->globalCursorVisibility(); }
    void startLaunchTimer();
    void sendWebOSMouseEvent(const std::string& eventName);

    void postEvent(WebOSEvent* ev);
    void onDelegateWindowFrameSwapped();
    void onLaunchTimeout();

    void applyInputRegion();
    void forwardWebOSEvent(WebOSEvent* event) const;
    void stateAboutToChange(webos::NativeWindowState willBe);
    void setUseVirtualKeyboard(const bool enable) override;

    // from WebPageBlinkObserver
    void didSwapPageCompositorFrame();
    void didResumeDOM() override;

protected:
    // WebAppBase
    virtual void doAttach();
    virtual void showWindow();

    WebAppWaylandWindow* window() { return m_appWindow; }
    void setupWindowGroup(ApplicationDescription* desc);

    void moveInputRegion(int height);

	// WebPageObserver
    virtual void webPageLoadFailed(int errorCode);
    virtual void webViewRecreated();
    virtual void webPageLoadFinished();

private:

    WebAppWaylandWindow* m_appWindow;
    std::string m_windowType;
    int m_lastSwappedTime;

    std::vector<gfx::Rect> m_inputRegion;
    bool m_enableInputRegion;

    bool m_isFocused;
    float m_vkbHeight;

    ElapsedTimer m_elapsedLaunchTimer;
    OneShotTimer<WebAppWayland> m_launchTimeoutTimer;

    bool m_lostFocusBySetWindowProperty;

    int m_displayId;
    std::string m_locationHint;
    ShellSurface *m_surface;
};

#endif /* WEBAPPWAYLAND_H */
