#pragma once

#include <Parsers/ASTQueryWithOutput.h>
#include <Parsers/IAST.h>

#include <optional>

namespace DB
{
/** RENAME query
  */
class ASTRenameQuery : public ASTQueryWithOutput
{
public:
    struct Table
    {
        String database;
        String table;

        Table() = default;
        Table(String db, String tbl)
            : database(std::move(db))
            , table(std::move(tbl))
        {}
    };

    struct Element
    {
        Table from;
        Table to;
        // The display database, table name in TiDB
        std::optional<Table> tidb_display;
    };

    using Elements = std::vector<Element>;
    Elements elements;

    /** Get the text that identifies this element. */
    String getID() const override { return "Rename"; };

    ASTPtr clone() const override
    {
        auto res = std::make_shared<ASTRenameQuery>(*this);
        cloneOutputOptions(*res);
        return res;
    }

protected:
    void formatQueryImpl(const FormatSettings & settings, FormatState &, FormatStateStacked) const override
    {
        settings.ostr << (settings.hilite ? hilite_keyword : "") << "RENAME TABLE " << (settings.hilite ? hilite_none : "");

        for (auto it = elements.cbegin(); it != elements.cend(); ++it)
        {
            if (it != elements.cbegin())
                settings.ostr << ", ";

            settings.ostr << (!it->from.database.empty() ? backQuoteIfNeed(it->from.database) + "." : "") << backQuoteIfNeed(it->from.table)
                          << (settings.hilite ? hilite_keyword : "") << " TO " << (settings.hilite ? hilite_none : "")
                          << (!it->to.database.empty() ? backQuoteIfNeed(it->to.database) + "." : "") << backQuoteIfNeed(it->to.table);
        }
    }
};

} // namespace DB
