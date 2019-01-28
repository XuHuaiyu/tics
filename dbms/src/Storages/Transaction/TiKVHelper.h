#pragma once

#include <string>

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

namespace DB
{


/// Algorithm from http://home.thep.lu.se/~bjorn/crc/

//inline uint32_t crc32_for_byte(uint32_t r)
//{
//    for (int j = 0; j < 8; ++j)
//        r = (r & 1 ? 0 : (uint32_t)0xEDB88320L) ^ r >> 1;
//    return r ^ (uint32_t)0xFF000000L;
//}
//
//inline void crc32(const void * data, size_t n_bytes, uint32_t * crc)
//{
//    static uint32_t table[0x100];
//    if (!*table)
//        for (size_t i = 0; i < 0x100; ++i)
//            table[i] = crc32_for_byte(i);
//    for (size_t i = 0; i < n_bytes; ++i)
//        *crc = table[(uint8_t)*crc ^ ((uint8_t *)data)[i]] ^ *crc >> 8;
//}

class Crc32
{
public:
    Crc32()
    {
        for (size_t i = 0; i < 0x100; ++i)
            table[i] = crc32_for_byte(i);
    }

    void put(const void * data, size_t n_bytes)
    {
        for (size_t i = 0; i < n_bytes; ++i)
            crc = table[(uint8_t)crc ^ ((uint8_t *)data)[i]] ^ crc >> 8;
    }

    uint32_t checkSum() { return crc; }

private:
    inline uint32_t crc32_for_byte(uint32_t r)
    {
        for (int j = 0; j < 8; ++j)
            r = (r & 1 ? 0 : (uint32_t)0xEDB88320L) ^ r >> 1;
        return r ^ (uint32_t)0xFF000000L;
    }

    uint32_t crc = 0;
    uint32_t table[0x100];
};

inline std::string escapeString(const std::string & data)
{
    std::string escaped;
    escaped.reserve(data.size());
    for (const char & c : data)
    {
        switch (c)
        {
            case '\n':
                escaped += "\\n";
                break;
            case '\r':
                escaped += "\\r";
                break;
            case '\t':
                escaped += "\\t";
                break;
            case '"':
                escaped += "\"";
                break;
            case '\\':
                escaped += "\\\\";
                break;
            default:
                if (c >= 0x20 && c < 0x7f)
                {
                    // c is printable
                    escaped += c;
                }
                else
                {
                    escaped += '\\';
                    escaped += ((char)'0') + (c >> 6);
                    escaped += ((char)'0') + ((c >> 3) & 7);
                    escaped += ((char)'0') + (c & 7);
                }
        }
    }
    return escaped;
}

} // namespace DB