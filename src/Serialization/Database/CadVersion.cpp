#include "Database/CadVersion.h"
#include <array>

namespace MiniDWG
{
    namespace
    {
        struct VersionInfo
        {
            CadVersion       version;
            std::string_view code;
            std::string_view displayName;
        };

        constexpr std::array<VersionInfo, 9> kVersions = { {
            { CadVersion::AC1009, "AC1009", "R12" },
            { CadVersion::AC1012, "AC1012", "R13" },
            { CadVersion::AC1014, "AC1014", "R14" },
            { CadVersion::AC1015, "AC1015", "R2000" },
            { CadVersion::AC1018, "AC1018", "R2004" },
            { CadVersion::AC1021, "AC1021", "R2007" },
            { CadVersion::AC1024, "AC1024", "R2010" },
            { CadVersion::AC1027, "AC1027", "R2013" },
            { CadVersion::AC1032, "AC1032", "R2018" },
        } };
    }

    CadVersion ParseVersionString(std::string_view text)
    {
        for (const VersionInfo& info : kVersions)
        {
            if (info.code == text)
                return info.version;
        }
        return CadVersion::Unknown;
    }

    std::string_view VersionString(CadVersion version)
    {
        for (const VersionInfo& info : kVersions)
        {
            if (info.version == version)
                return info.code;
        }
        return {};
    }

    std::string_view VersionDisplayName(CadVersion version)
    {
        for (const VersionInfo& info : kVersions)
        {
            if (info.version == version)
                return info.displayName;
        }
        return {};
    }
}
