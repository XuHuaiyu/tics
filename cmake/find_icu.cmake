option (ENABLE_ICU "Enable ICU" 1)

if (ENABLE_ICU)
    if (APPLE)
        # ClickHouse's internal icudata lib is only available on Linux, so Mac can only use system icu.
        # Run `brew install icu4c`.
        option (USE_INTERNAL_ICU_LIBRARY "Use system ICU library" 0)
    else ()
        option (USE_INTERNAL_ICU_LIBRARY "Set to FALSE to use system ICU library instead of bundled" ${NOT_UNBUNDLED})
    endif ()

    if (NOT EXISTS "${ClickHouse_SOURCE_DIR}/contrib/icu/icu4c/LICENSE")
        if (USE_INTERNAL_ICU_LIBRARY)
            message (WARNING "submodule contrib/icu is missing. to fix try run: \n git submodule update --init --recursive")
            set (USE_INTERNAL_ICU_LIBRARY 0)
        endif ()
        set (MISSING_INTERNAL_ICU_LIBRARY 1)
    endif ()

    if (NOT USE_INTERNAL_ICU_LIBRARY)
        if (APPLE)
            set(ICU_ROOT "/usr/local/opt/icu4c" CACHE STRING "")
        endif ()
        find_package (ICU COMPONENTS i18n uc data) # TODO: remove Modules/FindICU.cmake after cmake 3.7
        #set (ICU_LIBRARIES ${ICU_I18N_LIBRARY} ${ICU_UC_LIBRARY} ${ICU_DATA_LIBRARY} CACHE STRING "")
        if (ICU_FOUND)
            set (USE_ICU 1)
        endif ()
    endif ()

    if (ICU_LIBRARY AND ICU_INCLUDE_DIR)
        set (USE_ICU 1)
    elseif (NOT MISSING_INTERNAL_ICU_LIBRARY)
        set (USE_INTERNAL_ICU_LIBRARY 1)
        set (ICU_INCLUDE_DIR "${ClickHouse_SOURCE_DIR}/contrib/icu/icu4c/source/common" "${ClickHouse_SOURCE_DIR}/contrib/icu/icu4c/source/i18n")
        set (ICU_LIBRARIES icui18n icuuc icudata)
        set (USE_ICU 1)
    endif ()

endif()

if (USE_ICU)
    message (STATUS "Using icu=${USE_ICU}: ${ICU_INCLUDE_DIR} : ${ICU_LIBRARIES}")
else ()
    message (FATAL "Could not find icu")
endif ()