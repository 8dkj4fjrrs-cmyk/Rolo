#pragma once

// Small vector icons drawn with DrawList lines and shapes, so they are sharp
// at any size and need no image files.

#include "rolo/ui/DrawList.hpp"

namespace rolo::ui {

enum class Icon {
    ChevronRight,  // collapsed tree row, submenu
    ChevronDown,   // expanded tree row, dropdown
    Close,         // "x" on tabs and dialogs
    Check,         // checkbox, checked menu item
    Search,        // magnifying glass in filter fields
    Dot,           // small filled circle (status, unsaved marker)
    ChevronLeft,   // back
    Plus,          // create
    Menu,          // three lines: a panel's menu
    More,          // three vertical dots: more actions
    Help,          // question mark in a circle
    Info,          // filled circle with "i"
    Warning,       // triangle with "!"
    Error,         // filled circle with a cross
    Play,          // start the game
    Stop,          // filled square: stop the game
    Pause,         // two bars: pause the game
    Step,          // a bar and a triangle: advance one frame
    Refresh,       // circular arrow: reload
    Send,          // paper plane: build / publish
    Folder,        // a folder
    Layers,        // stacked sheets: the Hierarchy panel
    Scene,         // a framed picture: the Scene / viewport panel
    Sliders,       // three sliders: the Inspector panel
    Terminal,      // a window with a prompt: the Console panel
    Eye,           // preview
    Rolo,          // Rolo's face: a scene, a project or the app itself
    Move,          // four-way arrows: move tool
    Rotate,        // rotation arc: rotate tool
    Scale,         // corner arrow out of a box: scale tool
    RectTool,      // rectangle with handles: rect tool
    Grid,          // 2x2 squares: layout / preview options
    Brush,         // paint brush: paint tiles
    Eraser,        // eraser: remove tiles
    Fill,          // paint bucket: fill an area
    Eyedropper,    // pick a tile from the map
    Save,          // a disk: save
    Undo,          // arrow curving back to the left
    Redo,          // arrow curving on to the right
    Code,          // "</>": a script
    Sound,         // a speaker with sound waves: a sound file
};

/// Draws `icon` centred in `area`.
void drawIcon(DrawList& draw, Icon icon, Rect area, Color color);

} // namespace rolo::ui
