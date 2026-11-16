#include "tjsCommHead.h"

#include "TVPMovieIO.h"

#include <utility>

namespace krkrsdl3
{

tTVPMovieIO::tTVPMovieIO(TJS::tTJSBinaryStream* stream, std::string name)
  : source_(stream),
    name_(std::move(name))
{
    length_ = source_ ? static_cast<int64_t>(source_->GetSize()) : 0;
}

tTVPMovieIO::~tTVPMovieIO()
{
    delete source_;
    source_ = nullptr;
}

int tTVPMovieIO::Read(uint8_t* buffer, int size)
{
    if (aborted_ || source_ == nullptr || size <= 0)
        return aborted_ ? -1 : 0;

    const tjs_uint read = source_->Read(buffer, static_cast<tjs_uint>(size));
    return static_cast<int>(read);
}

int64_t tTVPMovieIO::Seek(int64_t offset, int whence)
{
    if (source_ == nullptr)
        return 0;
    return source_->Seek(offset, static_cast<tjs_uint>(whence));
}

} // namespace krkrsdl3
