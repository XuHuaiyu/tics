#include <Columns/Collator.h>
#include <Core/SortDescription.h>
#include <IO/Operators.h>
#include <IO/WriteBufferFromString.h>

#include <sstream>


namespace DB
{
std::string SortColumnDescription::getID() const
{
    WriteBufferFromOwnString out;
    out << column_name << ", " << column_number << ", " << direction << ", " << nulls_direction;
    if (collator)
        out << ", collation locale: " << collator->getLocale();
    return out.str();
}

} // namespace DB
