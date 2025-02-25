#pragma once

#include <Core/QueryProcessingStage.h>
#include <Interpreters/Context.h>
#include <Interpreters/IInterpreter.h>
#include <Interpreters/ExpressionAnalyzer.h>
#include <Interpreters/ExpressionActions.h>
#include <DataStreams/IBlockInputStream.h>
#include <Storages/Transaction/Types.h>


namespace Poco { class Logger; }

namespace DB
{

class ExpressionAnalyzer;
class ASTSelectQuery;
struct SubqueryForSet;


/** Interprets the SELECT query. Returns the stream of blocks with the results of the query before `to_stage` stage.
  */
class InterpreterSelectQuery : public IInterpreter
{
public:
    /**
     * query_ptr
     * - A query AST to interpret.
     *
     * to_stage
     * - the stage to which the query is to be executed. By default - till to the end.
     *   You can perform till the intermediate aggregation state, which are combined from different servers for distributed query processing.
     *
     * subquery_depth
     * - to control the limit on the depth of nesting of subqueries. For subqueries, a value that is incremented by one is passed;
     *   for INSERT SELECT, a value 1 is passed instead of 0.
     *
     * input
     * - if given - read not from the table specified in the query, but from prepared source.
     *
     * required_result_column_names
     * - don't calculate all columns except the specified ones from the query
     *  - it is used to remove calculation (and reading) of unnecessary columns from subqueries.
     *   empty means - use all columns.
     */

    InterpreterSelectQuery(
        const ASTPtr & query_ptr_,
        const Context & context_,
        const Names & required_result_column_names = Names{},
        QueryProcessingStage::Enum to_stage_ = QueryProcessingStage::Complete,
        size_t subquery_depth_ = 0,
        const BlockInputStreamPtr & input = nullptr,
        bool only_analyze = false);

    ~InterpreterSelectQuery();

    /// Execute a query. Get the stream of blocks to read.
    BlockIO execute() override;

    /// Execute the query and return multuple streams for parallel processing.
    BlockInputStreams executeWithMultipleStreams();

    Block getSampleBlock();

    static Block getSampleBlock(
        const ASTPtr & query_ptr_,
        const Context & context_);

    void ignoreWithTotals();

private:
    struct Pipeline
    {
        /** Streams of data.
          * The source data streams are produced in the executeFetchColumns function.
          * Then they are converted (wrapped in other streams) using the `execute*` functions,
          *  to get the whole pipeline running the query.
          */
        BlockInputStreams streams;

        /** When executing FULL or RIGHT JOIN, there will be a data stream from which you can read "not joined" rows.
          * It has a special meaning, since reading from it should be done after reading from the main streams.
          * It is appended to the main streams in UnionBlockInputStream or ParallelAggregatingBlockInputStream.
          */
        BlockInputStreamPtr stream_with_non_joined_data;

        BlockInputStreamPtr & firstStream() { return streams.at(0); }

        template <typename Transform>
        void transform(Transform && transform)
        {
            for (auto & stream : streams)
                transform(stream);

            if (stream_with_non_joined_data)
                transform(stream_with_non_joined_data);
        }

        bool hasMoreThanOneStream() const
        {
            return streams.size() + (stream_with_non_joined_data ? 1 : 0) > 1;
        }
    };

    struct OnlyAnalyzeTag {};
    InterpreterSelectQuery(
        OnlyAnalyzeTag,
        const ASTPtr & query_ptr_,
        const Context & context_);

    void init(const Names & required_result_column_names);

    void getAndLockStorageWithSchemaVersion(const String & database_name, const String & table_name, Int64 schema_version);

    void executeImpl(Pipeline & pipeline, const BlockInputStreamPtr & input, bool dry_run);


    struct AnalysisResult
    {
        bool has_join       = false;
        bool has_where      = false;
        bool need_aggregate = false;
        bool has_having     = false;
        bool has_order_by   = false;
        bool has_limit_by   = false;

        ExpressionActionsPtr before_join;   /// including JOIN
        ExpressionActionsPtr before_where;
        ExpressionActionsPtr before_aggregation;
        ExpressionActionsPtr before_having;
        ExpressionActionsPtr before_order_and_select;
        ExpressionActionsPtr before_limit_by;
        ExpressionActionsPtr final_projection;

        /// Columns from the SELECT list, before renaming them to aliases.
        Names selected_columns;

        /// Do I need to perform the first part of the pipeline - running on remote servers during distributed processing.
        bool first_stage = false;
        /// Do I need to execute the second part of the pipeline - running on the initiating server during distributed processing.
        bool second_stage = false;

        SubqueriesForSets subqueries_for_sets;
    };

    AnalysisResult analyzeExpressions(QueryProcessingStage::Enum from_stage);


    /** From which table to read. With JOIN, the "left" table is returned.
     */
    void getDatabaseAndTableNames(String & database_name, String & table_name);

    /// Different stages of query execution.

    /// dry_run - don't read from table, use empty header block instead.
    void executeWithMultipleStreamsImpl(Pipeline & pipeline, const BlockInputStreamPtr & input, bool dry_run);

    /// Fetch data from the table. Returns the stage to which the query was processed in Storage.
    QueryProcessingStage::Enum executeFetchColumns(Pipeline & pipeline, bool dry_run);

    void executeWhere(Pipeline & pipeline, const ExpressionActionsPtr & expression);
    void executeAggregation(Pipeline & pipeline, const ExpressionActionsPtr & expression, const FileProviderPtr & file_provider, bool overflow_row, bool final);
    void executeMergeAggregated(Pipeline & pipeline, bool overflow_row, bool final);
    void executeTotalsAndHaving(Pipeline & pipeline, bool has_having, const ExpressionActionsPtr & expression, bool overflow_row);
    void executeHaving(Pipeline & pipeline, const ExpressionActionsPtr & expression);
    void executeExpression(Pipeline & pipeline, const ExpressionActionsPtr & expression);
    void executeOrder(Pipeline & pipeline);
    void executeMergeSorted(Pipeline & pipeline);
    void executePreLimit(Pipeline & pipeline);
    void executeUnion(Pipeline & pipeline);
    void executeLimitBy(Pipeline & pipeline);
    void executeLimit(Pipeline & pipeline);
    void executeProjection(Pipeline & pipeline, const ExpressionActionsPtr & expression);
    void executeDistinct(Pipeline & pipeline, bool before_order, Names columns);
    void executeExtremes(Pipeline & pipeline);
    void executeSubqueriesInSetsAndJoins(Pipeline & pipeline, std::unordered_map<String, SubqueryForSet> & subqueries_for_sets);

    /** If there is a SETTINGS section in the SELECT query, then apply settings from it.
      *
      * Section SETTINGS - settings for a specific query.
      * Normally, the settings can be passed in other ways, not inside the query.
      * But the use of this section is justified if you need to set the settings for one subquery.
      */
    void initSettings();

    ASTPtr query_ptr;
    ASTSelectQuery & query;
    Context context;
    QueryProcessingStage::Enum to_stage;
    size_t subquery_depth;
    std::unique_ptr<ExpressionAnalyzer> query_analyzer;

    /// How many streams we ask for storage to produce, and in how many threads we will do further processing.
    size_t max_streams = 1;

    /// The object was created only for query analysis.
    bool only_analyze = false;

    /// Table from where to read data, if not subquery.
    StoragePtr storage;
    TableLockHolder table_lock;

    /// Used when we read from prepared input, not table or subquery.
    BlockInputStreamPtr input;

    Poco::Logger * log;
};

}
