#include "NodeGraph/Examples.h"
#include "Core/PathUtils.h"
#include "Serialization/JsonSerializer.h"
#include <fstream>
#include <sstream>

namespace MiniCAD::Nodes
{
    std::string ReadTextFile(const std::string& path)
    {
        std::ifstream in(Utf8ToPath(path), std::ios::binary);
        if (!in)
            return {};
        std::stringstream ss;
        ss << in.rdbuf();
        return ss.str();
    }

    std::vector<ExampleFile> ReadExampleIndex(const std::string& dir)
    {
        std::vector<ExampleFile> list;
        JsonSerializer s;
        std::string format;
        if (!s.Parse(ReadTextFile(dir + "/examples.json")) || (s.Value("format", format), format != "MiniCAD-NodeExamples"))
            return list;
        size_t n = 0;
        if (!s.BeginArray("examples", n))
            return list;
        for (size_t i = 0; i < n; ++i)
        {
            if (!s.BeginElement(i))
                continue;
            ExampleFile e;
            std::string file;
            s.Value("file", file);
            s.Value("name", e.Name);
            s.Value("description", e.Description);
            s.Value("group", e.Group);
            s.Value("kernel", e.NeedsKernel);
            s.EndElement();
            if (file.empty())
                continue;
            e.Path = dir + "/" + file;
            if (e.Name.empty())
                e.Name = file;
            list.push_back(std::move(e));
        }
        s.EndArray();
        return list;
    }
}
