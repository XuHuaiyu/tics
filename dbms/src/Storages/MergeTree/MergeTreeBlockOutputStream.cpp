#include <Interpreters/PartLog.h>
#include <Storages/MergeTree/MergeTreeBlockOutputStream.h>
#include <Storages/StorageMergeTree.h>


namespace DB
{
Block MergeTreeBlockOutputStream::getHeader() const
{
    return storage.getSampleBlock();
}


void MergeTreeBlockOutputStream::write(const Block & block)
{
    storage.data.delayInsertIfNeeded();

    auto part_blocks = storage.writer.splitBlockIntoParts(block);
    if (part_blocks.size() > 0)
    {
        LOG_TRACE(log, "Writing block, origin rows: " << block.rows() << ", splitted into " << part_blocks.size() << " parts");
    }

    for (auto & current_block : part_blocks)
    {
        Stopwatch watch;

        MergeTreeData::MutableDataPartPtr part = storage.writer.writeTempPart(current_block);
        storage.data.renameTempPartAndAdd(part, &storage.increment);

        PartLog::addNewPartToTheLog(storage.context, *part, watch.elapsed());

        /// Initiate async merge - it will be done if it's good time for merge and if there are space in 'background_pool'.
        storage.merge_task_handle->wake();
    }
}

} // namespace DB
