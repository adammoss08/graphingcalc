#include "app/GraphState.hpp"

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <map>
#include <memory>

namespace calc {

namespace {

// Minimal JSON value + recursive-descent parser (enough for graph state files).
struct Json {
    enum Type { Null, Bool, Num, Str, Arr, Obj } type = Null;
    bool b = false;
    double n = 0;
    std::string s;
    std::vector<Json> arr;
    std::map<std::string, Json> obj;

    const Json* get(const char* k) const {
        if (type != Obj) return nullptr;
        auto it = obj.find(k);
        return it == obj.end() ? nullptr : &it->second;
    }
    double num(double def) const {
        if (type == Num) return n;
        if (type == Str) { char* end; double v = std::strtod(s.c_str(), &end); return end != s.c_str() ? v : def; }
        return def;
    }
};

struct JsonParser {
    const std::string& t;
    size_t i = 0;
    std::string err;

    void ws() { while (i < t.size() && (t[i] == ' ' || t[i] == '\n' || t[i] == '\r' || t[i] == '\t')) ++i; }
    bool fail(const char* m) { if (err.empty()) err = m; return false; }

    bool str(std::string& out) {
        if (t[i] != '"') return fail("expected string");
        ++i;
        while (i < t.size() && t[i] != '"') {
            char c = t[i++];
            if (c != '\\') { out += c; continue; }
            if (i >= t.size()) return fail("bad escape");
            char e = t[i++];
            switch (e) {
            case 'n': out += '\n'; break;
            case 't': out += '\t'; break;
            case 'r': out += '\r'; break;
            case 'b': out += '\b'; break;
            case 'f': out += '\f'; break;
            case 'u': {
                if (i + 4 > t.size()) return fail("bad \\u");
                unsigned cp = std::strtoul(t.substr(i, 4).c_str(), nullptr, 16);
                i += 4;
                if (cp >= 0xD800 && cp < 0xDC00 && i + 6 <= t.size() && t[i] == '\\' && t[i + 1] == 'u') {
                    unsigned lo = std::strtoul(t.substr(i + 2, 4).c_str(), nullptr, 16);
                    cp = 0x10000 + ((cp - 0xD800) << 10) + (lo - 0xDC00);
                    i += 6;
                }
                if (cp < 0x80) out += char(cp);
                else if (cp < 0x800) { out += char(0xC0 | (cp >> 6)); out += char(0x80 | (cp & 0x3F)); }
                else if (cp < 0x10000) { out += char(0xE0 | (cp >> 12)); out += char(0x80 | ((cp >> 6) & 0x3F)); out += char(0x80 | (cp & 0x3F)); }
                else { out += char(0xF0 | (cp >> 18)); out += char(0x80 | ((cp >> 12) & 0x3F)); out += char(0x80 | ((cp >> 6) & 0x3F)); out += char(0x80 | (cp & 0x3F)); }
                break;
            }
            default: out += e; break;
            }
        }
        if (i >= t.size()) return fail("unterminated string");
        ++i;
        return true;
    }

    bool value(Json& v, int depth = 0) {
        if (depth > 64) return fail("too deep");
        ws();
        if (i >= t.size()) return fail("unexpected end");
        char c = t[i];
        if (c == '{') {
            v.type = Json::Obj;
            ++i; ws();
            if (i < t.size() && t[i] == '}') { ++i; return true; }
            while (true) {
                ws();
                std::string k;
                if (!str(k)) return false;
                ws();
                if (i >= t.size() || t[i] != ':') return fail("expected ':'");
                ++i;
                if (!value(v.obj[k], depth + 1)) return false;
                ws();
                if (i < t.size() && t[i] == ',') { ++i; continue; }
                if (i < t.size() && t[i] == '}') { ++i; return true; }
                return fail("expected ',' or '}'");
            }
        }
        if (c == '[') {
            v.type = Json::Arr;
            ++i; ws();
            if (i < t.size() && t[i] == ']') { ++i; return true; }
            while (true) {
                v.arr.emplace_back();
                if (!value(v.arr.back(), depth + 1)) return false;
                ws();
                if (i < t.size() && t[i] == ',') { ++i; continue; }
                if (i < t.size() && t[i] == ']') { ++i; return true; }
                return fail("expected ',' or ']'");
            }
        }
        if (c == '"') { v.type = Json::Str; return str(v.s); }
        if (t.compare(i, 4, "true") == 0) { v.type = Json::Bool; v.b = true; i += 4; return true; }
        if (t.compare(i, 5, "false") == 0) { v.type = Json::Bool; i += 5; return true; }
        if (t.compare(i, 4, "null") == 0) { i += 4; return true; }
        char* end;
        v.n = std::strtod(t.c_str() + i, &end);
        if (end == t.c_str() + i) return fail("unexpected character");
        v.type = Json::Num;
        i = end - t.c_str();
        return true;
    }
};

void escape(const std::string& s, std::string& out) {
    out += '"';
    for (char c : s) {
        switch (c) {
        case '"': out += "\\\""; break;
        case '\\': out += "\\\\"; break;
        case '\n': out += "\\n"; break;
        case '\t': out += "\\t"; break;
        default: out += c;
        }
    }
    out += '"';
}

std::string numf(float v) {  // style values: short and exact for what the UI can set
    char buf[24];
    std::snprintf(buf, sizeof buf, "%g", double(v));
    return buf;
}

std::string num(double v) {
    char buf[40];
    std::snprintf(buf, sizeof buf, "%.17g", v);
    return buf;
}

} // namespace

std::string colorToHex(Color c) {
    char buf[16];
    std::snprintf(buf, sizeof buf, "#%02x%02x%02x", int(std::lround(c.r * 255)), int(std::lround(c.g * 255)),
                  int(std::lround(c.b * 255)));
    return buf;
}

bool colorFromHex(const std::string& s, Color& out) {
    if (s.size() != 7 || s[0] != '#') return false;
    out = Color::hex(uint32_t(std::strtoul(s.c_str() + 1, nullptr, 16)));
    return true;
}

namespace {

void styleFields(const Style& st, bool isPointList, std::string& o) {
    o += ",\"color\":\"" + colorToHex(st.color) + "\"";
    if (st.hidden) o += ",\"hidden\":true";
    if (st.line != LineStyle::Solid) o += std::string(",\"lineStyle\":\"") + lineStyleName(st.line) + "\"";
    if (st.lineWidth != theme::CurveWidth) o += ",\"lineWidth\":\"" + numf(st.lineWidth) + "\"";
    if (st.lineOpacity != theme::CurveOpacity) o += ",\"lineOpacity\":\"" + numf(st.lineOpacity) + "\"";
    if (st.fillOpacity != theme::FillOpacity) o += ",\"fillOpacity\":\"" + numf(st.fillOpacity) + "\"";
    if (st.point != PointStyle::Point) o += std::string(",\"pointStyle\":\"") + pointStyleName(st.point) + "\"";
    if (st.pointSize != theme::PointDiameter) o += ",\"pointSize\":\"" + numf(st.pointSize) + "\"";
    if (st.showLabel) o += ",\"showLabel\":true";
    if (!st.label.empty()) { o += ",\"label\":"; escape(st.label, o); }
    if (isPointList) {
        o += std::string(",\"lines\":") + (st.showLines ? "true" : "false");
        o += std::string(",\"points\":") + (st.showPoints ? "true" : "false");
    }
}

void readStyle(const Json& e, Style& st) {
    if (const Json* c = e.get("color")) colorFromHex(c->s, st.color);
    if (const Json* h = e.get("hidden")) st.hidden = h->type == Json::Bool && h->b;
    if (const Json* v = e.get("lineStyle")) st.line = lineStyleFromName(v->s);
    if (const Json* v = e.get("lineWidth")) st.lineWidth = float(v->num(st.lineWidth));
    if (const Json* v = e.get("lineOpacity")) st.lineOpacity = float(v->num(st.lineOpacity));
    if (const Json* v = e.get("fillOpacity")) st.fillOpacity = float(v->num(st.fillOpacity));
    if (const Json* v = e.get("pointStyle")) st.point = pointStyleFromName(v->s);
    if (const Json* v = e.get("pointSize")) st.pointSize = float(v->num(st.pointSize));
    if (const Json* v = e.get("lines")) st.showLines = v->type == Json::Bool ? v->b : st.showLines;
    if (const Json* v = e.get("points")) st.showPoints = v->type == Json::Bool ? v->b : st.showPoints;
    if (const Json* v = e.get("showLabel")) st.showLabel = v->type == Json::Bool && v->b;
    if (const Json* v = e.get("label")) st.label = v->type == Json::Str ? v->s : st.label;
}

std::string str(const Json* j, const std::string& def = "") {
    if (!j) return def;
    if (j->type == Json::Str) return j->s;
    if (j->type == Json::Num) return num(j->n);
    return def;
}

} // namespace

std::string serializeGraph(const SavedGraph& g) {
    std::string o = "{\"version\":11,\"graph\":{";
    if (g.hasViewport)
        o += "\"viewport\":{\"xmin\":" + num(g.xmin) + ",\"ymin\":" + num(g.ymin) + ",\"xmax\":" + num(g.xmax) +
             ",\"ymax\":" + num(g.ymax) + "},";
    o += std::string("\"degreeMode\":") + (g.degrees ? "true" : "false");
    o += std::string(",\"showGrid\":") + (g.showGrid ? "true" : "false");
    o += std::string(",\"showMinorGridlines\":") + (g.minorGrid ? "true" : "false");
    o += std::string(",\"xAxisNumbers\":") + (g.axisNumbers ? "true" : "false");
    o += std::string(",\"yAxisNumbers\":") + (g.axisNumbers ? "true" : "false");
    o += "},\"expressions\":{\"list\":[";
    for (size_t i = 0; i < g.rows.size(); ++i) {
        const SavedRow& r = g.rows[i];
        if (i) o += ",";
        auto common = [&](const char* type) {
            o += std::string("{\"type\":\"") + type + "\",\"id\":";
            escape(r.id, o);
            if (!r.folderId.empty()) { o += ",\"folderId\":"; escape(r.folderId, o); }
        };
        switch (r.type) {
        case RowType::Note:
            common("text");
            o += ",\"text\":";
            escape(r.text, o);
            break;
        case RowType::Folder:
            common("folder");
            o += ",\"title\":";
            escape(r.text, o);
            if (r.collapsed) o += ",\"collapsed\":true";
            if (r.style.hidden) o += ",\"hidden\":true";
            break;
        case RowType::Table:
            common("table");
            o += ",\"columns\":[";
            for (size_t c = 0; c < r.columns.size(); ++c) {
                const SavedColumn& col = r.columns[c];
                if (c) o += ",";
                o += "{\"latex\":";
                escape(col.header, o);
                o += ",\"values\":[";
                for (size_t k = 0; k < col.values.size(); ++k) {
                    if (k) o += ",";
                    escape(col.values[k], o);
                }
                o += "]";
                styleFields(col.style, true, o);
                o += "}";
            }
            o += "]";
            break;
        case RowType::Image:
            common("image");
            o += ",\"image_url\":";
            escape(r.imagePath, o);
            o += ",\"center\":";
            escape(r.imageCenter, o);
            o += ",\"width\":";
            escape(r.imageWidth, o);
            o += ",\"height\":";
            escape(r.imageHeight, o);
            o += ",\"opacity\":\"" + numf(r.imageOpacity) + "\"";
            if (r.style.hidden) o += ",\"hidden\":true";
            break;
        default:
            common("expression");
            styleFields(r.style, false, o);
            o += ",\"latex\":";
            escape(r.latex, o);
            if (r.hasSlider) {
                o += ",\"slider\":{\"hardMin\":true,\"hardMax\":true,\"min\":";
                escape(r.sliderMin, o);
                o += ",\"max\":";
                escape(r.sliderMax, o);
                if (!r.sliderStep.empty()) { o += ",\"step\":"; escape(r.sliderStep, o); }
                o += "}";
            }
            if (!r.domainMin.empty() || !r.domainMax.empty()) {
                o += ",\"parametricDomain\":{\"min\":";
                escape(r.domainMin, o);
                o += ",\"max\":";
                escape(r.domainMax, o);
                o += "}";
            }
            break;
        }
        o += "}";
    }
    o += "]}}\n";
    return o;
}

bool parseGraph(const std::string& text, SavedGraph& out, std::string& error) {
    Json root;
    JsonParser p{text};
    if (!p.value(root)) { error = "Invalid graph file: " + p.err; return false; }
    // Accept either a bare state or Desmos API wrappers ({"state": {...}}).
    const Json* st = root.get("state") ? root.get("state") : &root;
    const Json* exprs = st->get("expressions");
    const Json* list = exprs ? exprs->get("list") : nullptr;
    if (!list || list->type != Json::Arr) { error = "Invalid graph file: no expressions list"; return false; }
    out = SavedGraph{};
    if (const Json* g = st->get("graph")) {
        if (const Json* v = g->get("viewport")) {
            out.hasViewport = true;
            out.xmin = v->get("xmin") ? v->get("xmin")->num(-10) : -10;
            out.xmax = v->get("xmax") ? v->get("xmax")->num(10) : 10;
            out.ymin = v->get("ymin") ? v->get("ymin")->num(-10) : -10;
            out.ymax = v->get("ymax") ? v->get("ymax")->num(10) : 10;
        }
        auto flag = [&](const char* k, bool def) { const Json* j = g->get(k); return j && j->type == Json::Bool ? j->b : def; };
        out.degrees = flag("degreeMode", false);
        out.showGrid = flag("showGrid", true);
        out.minorGrid = flag("showMinorGridlines", true);
        out.axisNumbers = flag("xAxisNumbers", true);
    }
    for (const Json& e : list->arr) {
        std::string type = str(e.get("type"), "expression");
        SavedRow r;
        r.id = str(e.get("id"));
        r.folderId = str(e.get("folderId"));
        r.style.color = Color::hex(0xC74440);
        if (type == "text") {
            r.type = RowType::Note;
            r.text = str(e.get("text"));
        } else if (type == "folder") {
            r.type = RowType::Folder;
            r.text = str(e.get("title"));
            if (const Json* c = e.get("collapsed")) r.collapsed = c->type == Json::Bool && c->b;
            if (const Json* h = e.get("hidden")) r.style.hidden = h->type == Json::Bool && h->b;
        } else if (type == "table") {
            r.type = RowType::Table;
            if (const Json* cols = e.get("columns")) {
                for (const Json& c : cols->arr) {
                    SavedColumn col;
                    col.header = str(c.get("latex"));
                    if (const Json* vals = c.get("values"))
                        for (const Json& v : vals->arr) col.values.push_back(str(&v));
                    col.style.color = Color::hex(0xC74440);
                    readStyle(c, col.style);
                    r.columns.push_back(std::move(col));
                }
            }
        } else if (type == "image") {
            r.type = RowType::Image;
            r.imagePath = str(e.get("image_url"));
            r.imageCenter = str(e.get("center"), r.imageCenter);
            r.imageWidth = str(e.get("width"), r.imageWidth);
            r.imageHeight = str(e.get("height"), r.imageHeight);
            if (const Json* o = e.get("opacity")) r.imageOpacity = float(o->num(1));
            if (const Json* h = e.get("hidden")) r.style.hidden = h->type == Json::Bool && h->b;
        } else if (type == "expression") {
            r.latex = str(e.get("latex"));
            readStyle(e, r.style);
            if (const Json* s = e.get("slider")) {
                r.hasSlider = true;
                r.sliderMin = str(s->get("min"), "-10");
                r.sliderMax = str(s->get("max"), "10");
                r.sliderStep = str(s->get("step"));
            }
            const Json* dom = e.get("parametricDomain");
            if (!dom) dom = e.get("polarDomain");
            if (dom) {
                r.domainMin = str(dom->get("min"));
                r.domainMax = str(dom->get("max"));
            }
        } else {
            continue;  // unknown item types are skipped
        }
        out.rows.push_back(std::move(r));
    }
    return true;
}

} // namespace calc
