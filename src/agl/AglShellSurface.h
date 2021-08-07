/*
 * Copyright 2021 Collabora, Ltd.
 *
 * Permission is hereby granted, free of charge, to any person obtaining
 * a copy of this software and associated documentation files (the
 * "Software"), to deal in the Software without restriction, including
 * without limitation the rights to use, copy, modify, merge, publish,
 * distribute, sublicense, and/or sell copies of the Software, and to
 * permit persons to whom the Software is furnished to do so, subject to
 * the following conditions:
 *
 * The above copyright notice and this permission notice (including the
 * next paragraph) shall be included in all copies or substantial
 * portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 * NONINFRINGEMENT.  IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS
 * BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN
 * ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
 * CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */
#pragma once

#include <string>
#include "ShellSurface.h"

class AglShellSurface : public ShellSurface {
public:
        AglShellSurface() {}
	~AglShellSurface() {}

        AglShellSurface(Surface sType, Panel pType, std::string source, std::string entryPoint)
            : m_surface(sType), m_panel(pType), m_src(source), m_entryPoint(entryPoint)
        {
        }

        Surface GetSurface() { return m_surface; }
        Panel GetPanel() { return m_panel; }
        std::string GetSrc() { return m_src; }
        std::string GetEntryPoint() { return m_entryPoint; }

        void SetPanel(Panel panel)
        {
            m_panel = panel;
        }

        void SetSurface(Surface surface)
        {
            m_surface = surface;
        }
        void SetSrc(std::string src)
        {
            m_src = src;
        }
        void SetEntryPoint(std::string entryPoint)
        {
            m_entryPoint = entryPoint;
        }

private:
        Surface m_surface;
        Panel m_panel;
        std::string m_src;
        std::string m_entryPoint;
};
