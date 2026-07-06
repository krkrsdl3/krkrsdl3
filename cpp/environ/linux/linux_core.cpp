#include "tjsCommHead.h"

#include "Platform.h"
#include "TVPApplication.h"

#include <sys/utsname.h>
#include <fstream>

//---------------------------------------------------------------------------
tTVPMemoryStream* GetResourceStream(const ttstr& filename)
{
    tTJSBinaryStream* tmp = TVPCreateBinaryStreamForRead(ExePath() + ttstr("/") + filename, 0);
    tTVPMemoryStream* ret = new tTVPMemoryStream(nullptr, tmp->GetSize());
    tmp->ReadBuffer(ret->GetInternalBuffer(), tmp->GetSize());
    delete tmp;
    return ret;
}
//---------------------------------------------------------------------------

//---------------------------------------------------------------------------
std::string TVPGetPackageVersionString()
{
    return "linux";
}
ttstr TVPGetOSName()
{
    // Linux发行版检测
    std::string id;
    std::string version_id;
    std::string pretty_name;
    std::ifstream file("/etc/os-release");
    std::string line;
    while (std::getline(file, line))
    {
        if (line.find("ID=") == 0)
        {
            id = line.substr(3);
            if (!id.empty() && id.front() == '"')
            {
                id = id.substr(1, id.size() - 2);
            }
        }
        else if (line.find("VERSION_ID=") == 0)
        {
            version_id = line.substr(11);
            if (!version_id.empty() && version_id.front() == '"')
            {
                version_id = version_id.substr(1, version_id.size() - 2);
            }
        }
        else if (line.find("PRETTY_NAME=") == 0)
        {
            pretty_name = line.substr(12);
            if (!pretty_name.empty() && pretty_name.front() == '"')
            {
                pretty_name = pretty_name.substr(1, pretty_name.size() - 2);
            }
        }
    }
    if (!pretty_name.empty())
    {
        return pretty_name;
    }
    if (!id.empty())
    {
        if (id == "ubuntu")
            return "Ubuntu " + version_id;
        if (id == "debian")
            return "Debian " + version_id;
        if (id == "fedora")
            return "Fedora " + version_id;
        if (id == "centos")
            return "CentOS " + version_id;
        if (id == "rhel")
            return "Red Hat " + version_id;
        if (id == "arch")
            return "Arch Linux";
        return id + " " + version_id;
    }
    struct utsname buffer;
    if (uname(&buffer) == 0)
    {
        return std::string(buffer.sysname) + " " + buffer.release;
    }

    return "Linux";
}
//---------------------------------------------------------------------------

//---------------------------------------------------------------------------
void TVPInvokeMenu(int x, int y, void* _menu = NULL)
{

}
//---------------------------------------------------------------------------