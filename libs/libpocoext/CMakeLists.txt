add_library (pocoext ${SPLIT_SHARED}
    src/LevelFilterChannel.cpp
    src/ThreadNumber.cpp
    src/SessionPoolHelpers.cpp
    src/ReloadableSplitterChannel.cpp
    src/TiFlashLogFileChannel.cpp

    include/Poco/Ext/LevelFilterChannel.h
    include/Poco/Ext/ThreadNumber.h
    include/Poco/Ext/SessionPoolHelpers.h
    include/Poco/Ext/ReloadableSplitterChannel.h
    include/Poco/Ext/TiFlashLogFileChannel.h
    include/Poco/Ext/TiFlashArchiveByTimestampsStrategy.h)

if (Poco_Data_FOUND)
    target_include_directories (pocoext PRIVATE ${Poco_Data_INCLUDE_DIRS})
    target_link_libraries(pocoext ${Poco_Data_LIBRARY})
endif()

target_include_directories (pocoext PUBLIC include PRIVATE ${COMMON_INCLUDE_DIR})

if (NOT USE_INTERNAL_POCO_LIBRARY)
    target_include_directories (pocoext BEFORE PUBLIC ${Poco_INCLUDE_DIRS})
endif ()

target_link_libraries(pocoext ${Poco_Util_LIBRARY} ${Poco_Net_LIBRARY} ${Poco_XML_LIBRARY} ${Poco_Foundation_LIBRARY})
