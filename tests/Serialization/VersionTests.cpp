#include "Database/CadVersion.h"
#include "TestFramework.h"

using namespace MiniDWG;

TEST(Version_ParseAndFormat_RoundTrip)
{
    const CadVersion all[] = {
        CadVersion::AC1009, CadVersion::AC1012, CadVersion::AC1014,
        CadVersion::AC1015, CadVersion::AC1018, CadVersion::AC1021,
        CadVersion::AC1024, CadVersion::AC1027, CadVersion::AC1032,
    };

    for (CadVersion v : all)
    {
        CHECK(ParseVersionString(VersionString(v)) == v);
        CHECK(!VersionDisplayName(v).empty());
    }
}

TEST(Version_UnknownStrings)
{
    CHECK(ParseVersionString("") == CadVersion::Unknown);
    CHECK(ParseVersionString("AC1099") == CadVersion::Unknown);
    CHECK(ParseVersionString("ac1018") == CadVersion::Unknown);
    CHECK(VersionString(CadVersion::Unknown).empty());
}

TEST(Version_OrderedByTime)
{
    CHECK(CadVersion::AC1015 < CadVersion::AC1018);
    CHECK(CadVersion::AC1027 < CadVersion::AC1032);
    CHECK(VersionDisplayName(CadVersion::AC1018) == "R2004");
}
