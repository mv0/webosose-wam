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
#include <cstring>

#include "ShellSurface.h"

enum class PanelEdge : int {
    NONE,
    TOP,
    BOTTOM,
    LEFT,
    RIGHT
};

enum class SurfaceType : int {
    BACKGROUND,
    PANEL,
};

class Panel {
public:
        Panel() {}
        Panel(PanelEdge edge) : m_edge(edge)
        {
        }

	int GetPanelEdgeData() {
		return static_cast<int>(m_edge);
	}

        PanelEdge GetPanelEdge() { return m_edge; }
        int GetPanelWidth() { return m_width; }
        void SetPanelEdge(PanelEdge edge) { m_edge = edge; }
        void SetPanelWidth(int width) { m_width = width; }

        /* conversion helpers */
        void SetPanelEdge(const char *edge)
        {
            PanelEdge _edge;
            if (!strcmp(edge, "top"))
                _edge = PanelEdge::TOP;
            else if (!strcmp(edge, "bottom"))
                _edge = PanelEdge::BOTTOM;
            else if (!strcmp(edge, "left"))
                _edge = PanelEdge::LEFT;
            else if (!strcmp(edge, "right"))
                _edge = PanelEdge::RIGHT;
            else
                _edge = PanelEdge::NONE;

            SetPanelEdge(_edge);
        }

        void SetPanelWidth(const char *width)
        {
            int _width = strtoul(width, NULL, 10);
            SetPanelWidth(_width);
        }
private:
        PanelEdge m_edge;
        int m_width;
};

class Surface {
public:
        Surface() {}
        Surface(SurfaceType sType) : m_type(sType)
        {
        }

        SurfaceType GetSurfaceType() { return m_type; }
        void SetSurfaceType(SurfaceType type) { m_type = type; }
private:
        SurfaceType m_type;
};

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

