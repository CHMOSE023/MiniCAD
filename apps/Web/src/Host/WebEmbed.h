#pragma once

namespace MiniCAD
{
    class WebApp;

    // 网页版嵌入别的网页（协作批注）：网址带 ?embed=1 时
    //   - 用紧凑布局（WebApp::ForcesCompactLayout），页面自己提供工具
    //   - 界面就绪后导出桥接接口 window.MiniCAD（同源 iframe 里由外层页面直接调用，见 WebEmbed.cpp 开头的接口说明）
    bool IsWebEmbedRequested();
    void InstallWebEmbed(WebApp& app);
}
