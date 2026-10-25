#pragma once
// Save/load graphs as Desmos-compatible state JSON:
// {"version":11,"graph":{"viewport":{...},"degreeMode":true,...},"expressions":{"list":[...]}}
// List items: "expression" (sliders, domains, styles), "text" (notes), "folder", "table", "image".
#include "ui/RowModel.hpp"

#include <string>
#include <vector>

namespace calc {

struct SavedColumn {
    std::string header;
    std::vector<std::string> values;
    Style style;
};

struct SavedRow {
    RowType type = RowType::Expression;
    std::string id;           // Desmos ids are strings
    std::string folderId;     // "" = top level
    std::string latex;        // expression
    std::string text;         // note text / folder title
    bool collapsed = false;
    Style style;
    bool hasSlider = false;
    std::string sliderMin = "-10", sliderMax = "10", sliderStep;
    std::string domainMin, domainMax;
    std::vector<SavedColumn> columns;
    std::string imagePath, imageCenter = "\\left(0,0\\right)", imageWidth = "10", imageHeight = "10";
    float imageOpacity = 1;
};

struct SavedGraph {
    std::vector<SavedRow> rows;
    bool hasViewport = false;
    double xmin = -10, xmax = 10, ymin = -10, ymax = 10;
    bool degrees = false, showGrid = true, minorGrid = true, axisNumbers = true;
    bool threeD = false;  // "threeDMode": our 3D mode (Desmos 3D is a separate calculator)
    bool hasCamera3D = false;           // "camera3D": orbit camera (azimuth, elevation, box half-size)
    double camAzimuth = 0, camElevation = 0, camHalfSize = 10;
    int focused = -1;   // undo snapshots only
};

std::string serializeGraph(const SavedGraph& g);
bool parseGraph(const std::string& json, SavedGraph& out, std::string& error);

std::string colorToHex(Color c);
bool colorFromHex(const std::string& s, Color& out);

} // namespace calc
