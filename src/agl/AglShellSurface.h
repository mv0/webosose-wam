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

enum panelEdge {
    NONE,
    TOP,
    BOTTOM,
    LEFT,
    RIGHT
};

enum surfaceType {
    BACKGROUND,
    PANEL,
};

class Panel {
public:
        Panel() {}
        Panel(panelEdge _edge) : m_edge(_edge)
        {
        }

        panelEdge getPanelEdge() { return m_edge; }
        int getPanelWidth() { return m_width; }
        void setPanelEdge(panelEdge _edge) { m_edge = _edge; }
        void setPanelWidth(int _width) { m_width = _width; }

        /* conversion helpers */
        void setPanelEdge(const char *_edge)
        {
            panelEdge edge;
            if (!strcmp(_edge, "top"))
                edge = TOP;
            else if (!strcmp(_edge, "bottom"))
                edge = BOTTOM;
            else if (!strcmp(_edge, "left"))
                edge = LEFT;
            else if (!strcmp(_edge, "right"))
                edge = RIGHT;
            else
                edge = NONE;

            setPanelEdge(edge);
        }

        void setPanelWidth(const char *_width)
        {
            int width = strtoul(_width, NULL, 10);
            setPanelWidth(width);
        }
private:
        panelEdge m_edge;
        int m_width;
};

class Surface {
public:
        Surface() {}
        Surface(surfaceType sType) : m_type(sType)
        {
        }

        surfaceType getSurfaceType() { return m_type; }
        void setSurfaceType(surfaceType _type) { m_type = _type; }
private:
        surfaceType m_type;
};

class AglShellSurface : public ShellSurface {
public:
        AglShellSurface() {}

        AglShellSurface(Surface sType, Panel pType, std::string source, std::string entryPoint)
            : m_surface(sType), m_panel(pType), m_src(source), m_entryPoint(entryPoint)
        {
        }

        Surface getSurface() { return m_surface; }
        Panel getPanel() { return m_panel; }
        std::string getSrc() { return m_src; }
        std::string getEntryPoint() { return m_entryPoint; }

        void setPanel(Panel _panel)
        {
            m_panel = _panel;
        }

        void setSurface(Surface _surface)
        {
            m_surface = _surface;
        }
        void setSrc(std::string _src)
        {
            m_src = _src;
        }
        void setEntryPoint(std::string _entryPoint)
        {
            m_entryPoint = _entryPoint;
        }

private:
        Surface m_surface;
        Panel m_panel;
        std::string m_src;
        std::string m_entryPoint;
};

