#pragma once
//---------------------------------------------------------------------------
// TVPMovieIO.h — TJS 流到解复用器的桥
//
// 只做三件事：读、定位、报名字。解复用器后端通过它拿到字节，
// 不必知道 XP3 / 物理文件 / 内存流的区别。
//---------------------------------------------------------------------------

#include <cstdint>
#include <string>

namespace TJS
{
class tTJSBinaryStream;
}

namespace krkrsdl3
{

class tTVPMovieIO
{
public:
    // 接管 stream 的所有权（析构时 delete）。
    tTVPMovieIO(TJS::tTJSBinaryStream* stream, std::string name);
    ~tTVPMovieIO();

    tTVPMovieIO(const tTVPMovieIO&) = delete;
    tTVPMovieIO& operator=(const tTVPMovieIO&) = delete;

    // 返回读到的字节数；0 表示流结束，<0 表示已被 Abort 中断。
    int Read(uint8_t* buffer, int size);
    int64_t Seek(int64_t offset, int whence);

    int64_t Length() const { return length_; }
    const std::string& Name() const { return name_; }

    // 让正在阻塞的 Read 立刻返回。解码线程退出时用。
    void Abort() { aborted_ = true; }
    bool Aborted() const { return aborted_; }

private:
    TJS::tTJSBinaryStream* source_ = nullptr;
    std::string name_;
    int64_t length_ = 0;
    volatile bool aborted_ = false;
};

} // namespace krkrsdl3
