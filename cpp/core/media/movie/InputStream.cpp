#include "InputStream.h"

NS_KRMOVIE_BEGIN

InputStream::InputStream(tTJSBinaryStream* s, const std::string& filename)
  : m_pSource(s),
    m_strFileName(filename)
{
    m_nFileSize = s->GetSize();
}

InputStream::~InputStream()
{
    if (m_pSource)
    {
        delete m_pSource;
        (m_pSource) = NULL;
    }
}

int InputStream::Read(uint8_t* buf, int buf_size)
{
    if (m_pSource == NULL)
        return 0;
    return m_pSource->Read(buf, buf_size);
}

int64_t InputStream::Seek(int64_t offset, int whence)
{
    if (m_pSource == NULL)
        return 0;
    return m_pSource->Seek(offset, whence);
}

NS_KRMOVIE_END
