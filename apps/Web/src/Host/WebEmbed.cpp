// ── 网页版嵌入别的网页：桥接接口 window.MiniCAD（协作批注） ─────────
//
// 外层页面（同源 iframe）直接调用，不走 postMessage：
//   open(url)                 → Promise<void>      取回并打开服务器上的图纸（与 ?open= 同一套实现），取回完成时兑现
//   getView()                 → ViewState | null   { layout, mode: "2d" | "3d", center: [x, y, z], viewHeight }
//   setView(view)                                  切布局（按名称）、恢复视图中心与缩放
//   zoomExtents()                                  缩放到全图
//   capture({ maxWidth })     → Promise<Blob>      当前视图截图 PNG（含批注，不含状态栏）
//   setMarkups(list)                               整批替换显示的批注：{ type, points: [[x, y]...], text?, issueId | id, label?, color? }
//   highlightMarkup(id)                            高亮并闪烁一个批注；null 取消
//   startMarkup(type)         → Promise<Markup | null>  画一个批注（pin / rect / cloud / arrow / text / ink），画完返回世界坐标，Esc 或取消返回 null
//   cancelMarkup()                                 取消正在画的批注
//   setCompact(bool) / isCompact()                 紧凑布局（只看图，默认）与完整布局（功能区、命令行等）之间切换
//   on(event, cb)             → 取消订阅的函数      事件 viewChanged(ViewState) / layoutChanged(ViewState) / markupClick(id)
//
// 事件与 startMarkup 的结果都在当前调用栈结束后（微任务）才交给页面：页面的回调可以放心地再调用桥接接口。
// EM_JS 的代码经 C 预处理器变成 C 字符串再编译：字符串引号以外不能写反斜杠转义，这里的脚本没有用到
#include "Host/WebEmbed.h"
#include "Host/WebApp.h"
#include "GUI/MainFrame.h"
#include "GUI/MarkupLayer.h"
#include "Data/Json.h"
#include <emscripten.h>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <optional>
#include <string>

namespace MiniCAD
{
    namespace
    {
        WebApp* g_embedApp = nullptr;

        MainFrame* Frame() { return g_embedApp ? &g_embedApp->GetFrame() : nullptr; }

        // 交给 JavaScript 的字符串：malloc 分配，由 JavaScript 用 _free 释放
        char* NewCString(const std::string& s)
        {
            char* p = static_cast<char*>(std::malloc(s.size() + 1));
            if (p)
                std::memcpy(p, s.c_str(), s.size() + 1);
            return p;
        }

        bool ParseColor(const std::string& s, MiniGUI::Color32& out)
        {
            if (s.size() != 7 || s[0] != '#')
                return false;
            unsigned r = 0, g = 0, b = 0;
            if (std::sscanf(s.c_str() + 1, "%02x%02x%02x", &r, &g, &b) != 3)
                return false;
            out = MiniGUI::RGBA(static_cast<uint8_t>(r), static_cast<uint8_t>(g), static_cast<uint8_t>(b));
            return true;
        }

        MiniGUI::JsonValue ViewToJson(const MainFrame::EmbedView& v)
        {
            using MiniGUI::JsonValue;
            JsonValue o = JsonValue::MakeObject();
            o.Set("layout", JsonValue(v.layout));
            o.Set("mode", JsonValue(v.plan ? "2d" : "3d"));
            JsonValue c = JsonValue::MakeArray();
            for (double d : v.center)
                c.Push(JsonValue(d));
            o.Set("center", std::move(c));
            o.Set("viewHeight", JsonValue(v.viewHeight));
            return o;
        }

        std::string MarkupToJson(const Markup& m)
        {
            using MiniGUI::JsonValue;
            JsonValue o = JsonValue::MakeObject();
            o.Set("type", JsonValue(MarkupLayer::TypeName(m.type)));
            JsonValue pts = JsonValue::MakeArray();
            for (size_t i = 0; i + 1 < m.points.size(); i += 2)
            {
                JsonValue p = JsonValue::MakeArray();
                p.Push(JsonValue(m.points[i]));
                p.Push(JsonValue(m.points[i + 1]));
                pts.Push(std::move(p));
            }
            o.Set("points", std::move(pts));
            if (m.type == MarkupType::Text)
                o.Set("text", JsonValue(m.text));
            return o.Dump();
        }

        bool ParseMarkup(const MiniGUI::JsonValue& v, Markup& m)
        {
            if (!v.IsObject() || !MarkupLayer::ParseType(v["type"].AsString(), m.type))
                return false;
            for (const MiniGUI::JsonValue& p : v["points"].GetArray())
            {
                const auto& xy = p.GetArray();
                if (xy.size() < 2 || !xy[0].IsNumber() || !xy[1].IsNumber())
                    return false;
                m.points.push_back(xy[0].AsNumber());
                m.points.push_back(xy[1].AsNumber());
            }
            if (m.points.empty())
                return false;
            const MiniGUI::JsonValue& issue = v["issueId"];
            m.id    = issue.IsString() ? issue.AsString() : v["id"].AsString();
            if (issue.IsNumber())
                m.id = std::to_string(static_cast<long long>(issue.AsNumber()));
            m.text  = v["text"].AsString();
            m.label = v["label"].AsString();
            ParseColor(v["color"].AsString(), m.color);
            return true;
        }
    }

    EM_JS_DEPS(MiniCADWebEmbed, "$stringToNewUTF8,$UTF8ToString");

    EM_JS(int, MiniCADEmbed_UrlFlag, (), {
        try { const v = new URLSearchParams(location.search).get("embed"); return v !== null && v !== "0" && v !== "false" ? 1 : 0; }
        catch (e) { return 0; }
    });

    // 事件交给页面（微任务里，见文件开头）
    EM_JS(void, MiniCADEmbed_Emit, (const char* eventPtr, const char* jsonPtr), {
        if (Module.miniCADEmbed) Module.miniCADEmbed.emit(UTF8ToString(eventPtr), jsonPtr ? UTF8ToString(jsonPtr) : "");
    });

    // 画完一个批注（jsonPtr 为空 = 取消）
    EM_JS(void, MiniCADEmbed_Placed, (const char* jsonPtr), {
        if (Module.miniCADEmbed) Module.miniCADEmbed.placed(jsonPtr ? UTF8ToString(jsonPtr) : "");
    });

    EM_JS(void, MiniCADEmbed_InstallJs, (), {
        const withStr = (s, f) => {
            const p = stringToNewUTF8(s === null || s === undefined ? "" : String(s));
            try { return f(p); } finally { _free(p); }
        };
        const takeStr = (p) => {
            if (!p) return "";
            const s = UTF8ToString(p);
            _free(p);
            return s;
        };
        const parse = (s) => { try { return s ? JSON.parse(s) : null; } catch (e) { return null; } };
        const listeners = { viewChanged: new Set(), layoutChanged: new Set(), markupClick: new Set() };
        const opening = new Map();
        let placing = null;

        Module.miniCADEmbed = {
            emit(event, json) {
                const set = listeners[event];
                if (!set || set.size === 0) return;
                const arg = event === "markupClick" ? json : parse(json);
                queueMicrotask(() => {
                    for (const cb of Array.from(set)) {
                        try { cb(arg); } catch (e) { console.error(e); }
                    }
                });
            },
            placed(json) {
                const resolve = placing;
                placing = null;
                if (resolve) queueMicrotask(() => resolve(parse(json)));
            },
            opened(id) {
                const resolve = opening.get(id);
                opening.delete(id);
                if (resolve) resolve();
            },
        };

        window.MiniCAD = {
            open(url) {
                return new Promise((resolve) => {
                    const id = withStr(JSON.stringify([String(url)]), (p) => _MiniCADEmbed_Open(p));
                    if (id > 0) opening.set(id, resolve); else resolve();
                });
            },
            getView() { return parse(takeStr(_MiniCADEmbed_GetView())); },
            setView(view) { if (view) withStr(JSON.stringify(view), (p) => _MiniCADEmbed_SetView(p)); },
            zoomExtents() { _MiniCADEmbed_ZoomExtents(); },
            setMarkups(list) { withStr(JSON.stringify(Array.isArray(list) ? list : []), (p) => _MiniCADEmbed_SetMarkups(p)); },
            highlightMarkup(id) { withStr(id === null || id === undefined ? "" : id, (p) => _MiniCADEmbed_Highlight(p)); },
            startMarkup(type) {
                return new Promise((resolve) => {
                    // 正在画的先取消：它的 Promise 在 Start 里经 placed("") 兑现为 null
                    const ok = withStr(type, (p) => _MiniCADEmbed_StartMarkup(p));
                    if (ok) placing = resolve; else resolve(null);
                });
            },
            cancelMarkup() { _MiniCADEmbed_CancelMarkup(); },
            setCompact(compact) { _MiniCADEmbed_SetCompact(compact ? 1 : 0); },
            isCompact() { return _MiniCADEmbed_IsCompact() !== 0; },
            capture(opts) {
                const maxWidth = opts && opts.maxWidth > 0 ? opts.maxWidth : 0;
                // 同步画一帧再立即拷贝画布：WebGL / WebGPU 的画布内容只在本次任务内有效
                const r = parse(takeStr(_MiniCADEmbed_RenderForCapture()));
                const canvas = Module.canvas;
                if (!r || !canvas || r.w <= 0 || r.h <= 0) return Promise.reject(new Error("MiniCAD: nothing to capture"));
                const k = canvas.width / (canvas.clientWidth || canvas.width);
                const sx = Math.max(0, Math.round(r.x * k)), sy = Math.max(0, Math.round(r.y * k));
                const sw = Math.min(canvas.width - sx, Math.round(r.w * k)), sh = Math.min(canvas.height - sy, Math.round(r.h * k));
                const scale = maxWidth > 0 && sw > maxWidth ? maxWidth / sw : 1;
                const out = document.createElement("canvas");
                out.width = Math.max(1, Math.round(sw * scale));
                out.height = Math.max(1, Math.round(sh * scale));
                const g = out.getContext("2d");
                g.imageSmoothingQuality = "high";
                g.drawImage(canvas, sx, sy, sw, sh, 0, 0, out.width, out.height);
                return new Promise((resolve, reject) =>
                    out.toBlob((b) => (b ? resolve(b) : reject(new Error("MiniCAD: capture failed"))), "image/png"));
            },
            on(event, cb) {
                const set = listeners[event];
                if (!set || typeof cb !== "function") return () => {};
                set.add(cb);
                return () => { set.delete(cb); };
            },
        };
    });

    bool IsWebEmbedRequested()
    {
        return MiniCADEmbed_UrlFlag() != 0;
    }

    void InstallWebEmbed(WebApp& app)
    {
        g_embedApp = &app;
        MainFrame& frame = app.GetFrame();
        MarkupLayer& layer = frame.GetMarkupLayer();
        layer.onPlaced = [](const std::optional<Markup>& m)
        {
            MiniCADEmbed_Placed(m ? MarkupToJson(*m).c_str() : nullptr);
        };
        layer.onClick = [](const std::string& id)
        {
            MiniCADEmbed_Emit("markupClick", id.c_str());
        };
        auto emitView = [](const char* event)
        {
            MainFrame* f = Frame();
            MainFrame::EmbedView v;
            if (f && f->GetEmbedView(v))
                MiniCADEmbed_Emit(event, ViewToJson(v).Dump().c_str());
        };
        frame.onEmbedViewChanged   = [emitView] { emitView("viewChanged"); };
        frame.onEmbedLayoutChanged = [emitView] { emitView("layoutChanged"); };
        MiniCADEmbed_InstallJs();
    }

    // ── 由 window.MiniCAD 调用 ────────────────────────────────────
    extern "C"
    {
        EMSCRIPTEN_KEEPALIVE int MiniCADEmbed_Open(const char* urlsJson)
        {
            return g_embedApp && urlsJson ? g_embedApp->OpenUrls(urlsJson) : 0;
        }

        EMSCRIPTEN_KEEPALIVE char* MiniCADEmbed_GetView()
        {
            MainFrame* f = Frame();
            MainFrame::EmbedView v;
            if (!f || !f->GetEmbedView(v))
                return nullptr;
            return NewCString(ViewToJson(v).Dump());
        }

        EMSCRIPTEN_KEEPALIVE void MiniCADEmbed_SetView(const char* json)
        {
            MainFrame* f = Frame();
            MiniGUI::JsonValue  v;
            MiniGUI::JsonError  error;
            if (!f || !json || !MiniGUI::ParseJson(json, v, error) || !v.IsObject())
                return;
            // 在当前视图上改宿主给出的字段；图纸还没读完时从空视图开始（MainFrame 先记下，读完再恢复）
            MainFrame::EmbedView view;
            f->GetEmbedView(view);
            if (const auto* layout = v.Find("layout"); layout && layout->IsString())
                view.layout = layout->AsString();
            if (const auto* mode = v.Find("mode"); mode && mode->IsString())
                view.plan = mode->AsString() != "3d";
            const auto& center = v["center"].GetArray();
            for (size_t i = 0; i < 3 && i < center.size(); ++i)
            {
                if (center[i].IsNumber() && std::isfinite(center[i].AsNumber()))
                    view.center[i] = center[i].AsNumber();
            }
            if (const auto* h = v.Find("viewHeight"); h && h->IsNumber() && h->AsNumber() > 0.0)
                view.viewHeight = h->AsNumber();
            f->SetEmbedView(view);
        }

        EMSCRIPTEN_KEEPALIVE void MiniCADEmbed_ZoomExtents()
        {
            if (MainFrame* f = Frame())
                f->ZoomAllMain();
        }

        EMSCRIPTEN_KEEPALIVE void MiniCADEmbed_SetMarkups(const char* json)
        {
            MainFrame* f = Frame();
            MiniGUI::JsonValue v;
            MiniGUI::JsonError error;
            if (!f || !json || !MiniGUI::ParseJson(json, v, error))
                return;
            std::vector<Markup> list;
            for (const MiniGUI::JsonValue& item : v.GetArray())
            {
                Markup m;
                if (ParseMarkup(item, m))
                    list.push_back(std::move(m));
            }
            f->GetMarkupLayer().SetMarkups(std::move(list));
        }

        EMSCRIPTEN_KEEPALIVE void MiniCADEmbed_Highlight(const char* id)
        {
            if (MainFrame* f = Frame())
                f->GetMarkupLayer().Highlight(id ? id : "", static_cast<double>(MainFrame::NowTicks()) / static_cast<double>(MainFrame::kTicksPerSecond));
        }

        EMSCRIPTEN_KEEPALIVE int MiniCADEmbed_StartMarkup(const char* type)
        {
            MainFrame* f = Frame();
            MarkupType t;
            if (!f || !type || !MarkupLayer::ParseType(type, t))
                return 0;
            f->GetMarkupLayer().Start(t);
            return 1;
        }

        EMSCRIPTEN_KEEPALIVE void MiniCADEmbed_SetCompact(int compact)
        {
            if (g_embedApp)
                g_embedApp->SetEmbedCompact(compact != 0);
        }

        EMSCRIPTEN_KEEPALIVE int MiniCADEmbed_IsCompact()
        {
            return g_embedApp && g_embedApp->IsEmbedCompact() ? 1 : 0;
        }

        EMSCRIPTEN_KEEPALIVE void MiniCADEmbed_CancelMarkup()
        {
            if (MainFrame* f = Frame())
                f->GetMarkupLayer().Cancel();
        }

        // 截图：同步画一帧（含批注），返回要截取的范围 { x, y, w, h }（画布内的逻辑像素）
        EMSCRIPTEN_KEEPALIVE char* MiniCADEmbed_RenderForCapture()
        {
            MainFrame* f = Frame();
            if (!f)
                return nullptr;
            f->RenderFrame();
            const MiniGUI::Rect r = f->GetMainViewportRect();
            char buf[160];
            std::snprintf(buf, sizeof(buf), "{\"x\":%.3f,\"y\":%.3f,\"w\":%.3f,\"h\":%.3f}",
                          static_cast<double>(r.min.x), static_cast<double>(r.min.y),
                          static_cast<double>(r.Width()), static_cast<double>(r.Height()));
            return NewCString(buf);
        }
    }
}
