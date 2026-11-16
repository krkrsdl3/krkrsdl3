#include "tjsCommHead.h"

#include "TVPMovieBackend.h"

#include <algorithm>
#include <cstring>

namespace krkrsdl3
{

tTVPMovieBackends& tTVPMovieBackends::Get()
{
    static tTVPMovieBackends instance;
    return instance;
}

void tTVPMovieBackends::Add(std::unique_ptr<iTVPMovieBackend> backend)
{
    if (!backend)
        return;

    for (const auto& existing : backends_)
    {
        if (std::strcmp(existing->Name(), backend->Name()) == 0)
            return;
    }

    backend->InstallAccelerators();

    backends_.push_back(std::move(backend));
    // 优先级降序；stable 保证同优先级维持注册顺序（先注册的先试）。
    std::stable_sort(backends_.begin(), backends_.end(),
                     [](const std::unique_ptr<iTVPMovieBackend>& lhs,
                        const std::unique_ptr<iTVPMovieBackend>& rhs) {
                         return lhs->Priority() > rhs->Priority();
                     });
}

iTVPMovieDemuxer* tTVPMovieBackends::CreateDemuxer(tTVPMovieIO* io)
{
    for (const auto& backend : backends_)
    {
        iTVPMovieDemuxer* demuxer = backend->CreateDemuxer();
        if (!demuxer)
            continue;

        if (demuxer->Open(io))
            return demuxer;

        delete demuxer;
    }
    return nullptr;
}

iTVPMovieDecoder* tTVPMovieBackends::CreateDecoder(const tTVPMovieStream& stream)
{
    for (const auto& backend : backends_)
    {
        iTVPMovieDecoder* decoder = backend->CreateDecoder(stream);
        if (!decoder)
            continue;

        if (decoder->Open(stream))
            return decoder;

        delete decoder;
    }
    return nullptr;
}

std::vector<std::string> tTVPMovieBackends::Names() const
{
    std::vector<std::string> names;
    names.reserve(backends_.size());
    for (const auto& backend : backends_)
        names.push_back(backend->Name());
    return names;
}

void TVPRegisterMovieBackends()
{
    static bool registered = false;
    if (registered)
        return;
    registered = true;

    tTVPMovieBackends& backends = tTVPMovieBackends::Get();

#if defined(_KRKRSDL3_USE_THEORA)
    TVPRegisterTheoraBackend(backends);
#endif
#if defined(_KRKRSDL3_USE_FFMPEG)
    TVPRegisterFFmpegBackend(backends);
#endif
}

} // namespace krkrsdl3
