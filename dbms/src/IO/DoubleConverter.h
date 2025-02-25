#pragma once

#include <double-conversion/double-conversion.h>


namespace DB
{
template <bool emit_decimal_point>
struct DoubleToStringConverterFlags
{
    static constexpr auto flags = double_conversion::DoubleToStringConverter::NO_FLAGS;
};

template <>
struct DoubleToStringConverterFlags<true>
{
    static constexpr auto flags = double_conversion::DoubleToStringConverter::EMIT_TRAILING_DECIMAL_POINT;
};

template <bool emit_decimal_point>
class DoubleConverter
{
    DoubleConverter() = default;

public:
    /** @todo Add commentary on how this constant is deduced.
     *    e.g. it's minus sign, integral zero, decimal point, up to 5 leading zeros and kBase10MaximalLength digits. */
    static constexpr auto MAX_REPRESENTATION_LENGTH = 26;
    using BufferType = char[MAX_REPRESENTATION_LENGTH];

    static const auto & instance()
    {
        // clang-format off
        static const double_conversion::DoubleToStringConverter instance{
            DoubleToStringConverterFlags<emit_decimal_point>::flags, "inf", "nan", 'e', -6, 21, 6, 1
        };
        // clang-format on

        return instance;
    }

    DoubleConverter(const DoubleConverter &) = delete;
    DoubleConverter & operator=(const DoubleConverter &) = delete;
};

} // namespace DB
