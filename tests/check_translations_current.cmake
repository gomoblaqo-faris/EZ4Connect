# Refreshes copies of the translation files from the sources and checks the
# copies, so that text added to the code without a translation fails.
#   -DLUPDATE=<lupdate> -DCHECKER=<translations_test> -DSOURCE_ROOT=<repo>
#   -DSOURCE_DIRS=a|b -DTS_FILES=x.ts|y.ts -DWORK_DIR=<scratch directory>
string(REPLACE "|" ";" SOURCE_DIRS "${SOURCE_DIRS}")
string(REPLACE "|" ";" TS_FILES "${TS_FILES}")

file(REMOVE_RECURSE "${WORK_DIR}")
file(MAKE_DIRECTORY "${WORK_DIR}")
set(copies)
foreach(ts IN LISTS TS_FILES)
    get_filename_component(name "${ts}" NAME)
    configure_file("${SOURCE_ROOT}/${ts}" "${WORK_DIR}/${name}" COPYONLY)
    list(APPEND copies "${WORK_DIR}/${name}")
endforeach()

execute_process(
    COMMAND "${LUPDATE}" -silent -no-obsolete -locations none -extensions cpp,h,ui
            ${SOURCE_DIRS} -ts ${copies}
    WORKING_DIRECTORY "${SOURCE_ROOT}"
    RESULT_VARIABLE lupdateResult
)
if(NOT lupdateResult EQUAL 0)
    message(FATAL_ERROR "lupdate failed")
endif()

execute_process(COMMAND "${CHECKER}" ${copies} RESULT_VARIABLE checkResult)
if(NOT checkResult EQUAL 0)
    message(FATAL_ERROR "The translation files are not up to date with the code")
endif()
