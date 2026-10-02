cmake_minimum_required(VERSION 3.12)
project(EZ4Connect)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_AUTOUIC ON)

option(NIGHTLY_BUILD    "Set if built nightly.")

if(NIGHTLY_BUILD)
    set(PROJ_VER "${GIT_HASH}-nightly")
elseif(DEFINED REL_VER)
    set(PROJ_VER "${REL_VER}")
else()
    set(PROJ_VER "dev")
endif()

add_compile_definitions(PROJ_VER="${PROJ_VER}")
message("PROJ_VER=${PROJ_VER}")

option(EZ4CONNECT_BUILD_GUI
    "Build the graphical app. Turn off to build only the command-line client, which needs none of the GUI Qt modules."
    ON
)

find_package(Qt6 6.5 COMPONENTS
    Core
    Concurrent
    Network
    Core5Compat
    REQUIRED
)
if(EZ4CONNECT_BUILD_GUI)
    find_package(Qt6 6.5 COMPONENTS
        Gui
        Widgets
        Svg
        WebEngineWidgets
        REQUIRED
    )
endif()

include_directories(${CMAKE_CURRENT_SOURCE_DIR})

# Everything below the user interface: the connection domain, the use cases
# and the platform implementations. It does not use Qt Widgets, so the GUI,
# the command-line client and the tests are all built on this one library.
set(ENGINE_SOURCES
    core/connectionsessionstate.cpp
    application/applicationlogger.cpp
    application/commandlineoptions.cpp
    application/coreprocess.h
    application/connectionsession.cpp
    application/defaultsettings.cpp
    application/profileimport.cpp
    application/profileservice.cpp
    application/profilesettings.cpp
    application/settingsmigrator.cpp
    application/systemproxysession.cpp
    infrastructure/coreprocess/consoleoutputdecoder.cpp
    infrastructure/coreprocess/corecommandbuilder.cpp
    infrastructure/coreprocess/coreexecutable.cpp
    infrastructure/coreprocess/coreoutputbuffer.cpp
    infrastructure/coreprocess/coreoutputparser.cpp
    infrastructure/coreprocess/devicetrust.cpp
    infrastructure/coreprocess/zjuconnectprocess.cpp
    infrastructure/logging/applicationlogfile.cpp
    infrastructure/platform/autostart.cpp
    infrastructure/platform/platformsystemproxybackend.cpp
    infrastructure/platform/privileges.cpp
    infrastructure/platform/systemproxyfunctions.cpp
    infrastructure/settings/profilemanager.cpp
    infrastructure/settings/settingsprofileloader.cpp
    infrastructure/storage/applicationpaths.cpp
    infrastructure/update/updatechecker.cpp
)

# Saved passwords go to the system credential store when QtKeychain is
# installed. Without it they stay in the profile file, as before.
find_package(Qt6Keychain QUIET)
if(Qt6Keychain_FOUND)
    message("Credential storage: system keychain (QtKeychain)")
    list(APPEND ENGINE_SOURCES infrastructure/secrets/keychainsecretstore.cpp)
else()
    message("Credential storage: profile file (QtKeychain not found)")
endif()

if(WIN32)
    set(PLATFORM_LIBS wininet rasapi32 shell32)
endif()

add_library(ez4connect_engine STATIC ${ENGINE_SOURCES})
target_link_libraries(ez4connect_engine PUBLIC
    Qt::Core
    Qt::Concurrent
    Qt::Network
    Qt::Core5Compat
    ${PLATFORM_LIBS}
)
if(Qt6Keychain_FOUND)
    target_link_libraries(ez4connect_engine PUBLIC Qt6Keychain::Qt6Keychain)
    target_compile_definitions(ez4connect_engine PUBLIC EZ4CONNECT_HAS_KEYCHAIN)
endif()

if(EZ4CONNECT_BUILD_GUI)

set(GUI_SOURCES
    main.cpp
    presentation/main/mainwindow.cpp
    presentation/main/mainwindowcoordinator.cpp
    presentation/coordinators/authdialogcoordinator.cpp
    presentation/coordinators/connectionuicontroller.cpp
    presentation/presentationhelpers.cpp
    presentation/dialogs/extrasettingwindow/extrasettingwindow.cpp
    presentation/dialogs/settingwindow/settingwindow.cpp
    presentation/dialogs/loginwindow/loginwindow.cpp
    presentation/dialogs/sudowindow/sudowindow.cpp
    presentation/dialogs/ssologinwebview/ssologinwebview.cpp
    presentation/dialogs/authinfowindow/authinfowindow.cpp
    presentation/dialogs/configurationguidedialog/configurationguidedialog.cpp
    presentation/dialogs/graphcaptchawindow/graphcaptchawindow.cpp
    presentation/dialogs/graphcaptchawindow/captchacanvas.cpp
    resource.qrc
)

if (WIN32)
    message("Building for Windows")

    enable_language("RC")

    add_executable(${PROJECT_NAME} WIN32
        ${GUI_SOURCES}
        ${CMAKE_CURRENT_SOURCE_DIR}/resource/icon.rc
    )
elseif(APPLE)
    message("Building for MacOS")
    
    set(MACOSX_BUNDLE_ICON_FILE icon.icns)
    
    set(app_icon_macos ${CMAKE_CURRENT_SOURCE_DIR}/resource/icon.icns)
    set_source_files_properties(${app_icon_macos} PROPERTIES MACOSX_PACKAGE_LOCATION "Resources")

    add_executable(${PROJECT_NAME} MACOSX_BUNDLE
        ${GUI_SOURCES}
        ${app_icon_macos}
    )

    # Info.plist is a template: CMake fills in the version from these. Apple
    # only accepts dotted numbers there, so "1.9.1-preview.4" becomes "1.9.1",
    # and development and nightly builds, which have no release number, get
    # zeros. The full version is what the app itself shows.
    set(BUNDLE_VERSION "0.0.0")
    if(NOT NIGHTLY_BUILD AND DEFINED REL_VER)
        string(REGEX MATCH "^[0-9]+(\\.[0-9]+)*" RELEASE_NUMBER "${REL_VER}")
        if(RELEASE_NUMBER)
            set(BUNDLE_VERSION "${RELEASE_NUMBER}")
        endif()
    endif()
    set_target_properties(${PROJECT_NAME} PROPERTIES
        MACOSX_BUNDLE_INFO_PLIST "${CMAKE_CURRENT_SOURCE_DIR}/resource/Info.plist"
        MACOSX_BUNDLE_SHORT_VERSION_STRING "${BUNDLE_VERSION}"
        MACOSX_BUNDLE_BUNDLE_VERSION "${BUNDLE_VERSION}"
    )
else()
    add_executable(${PROJECT_NAME}
        ${GUI_SOURCES}
    )

    install(
        TARGETS ${PROJECT_NAME}
        RUNTIME DESTINATION bin
    )
endif()

set(QAPPLICATION_CLASS QApplication CACHE STRING "Inheritance class for SingleApplication")

add_subdirectory(singleapplication)

target_link_libraries(${PROJECT_NAME}
        ez4connect_engine
        Qt::Gui
        Qt::Widgets
        Qt::Svg
        Qt::WebEngineWidgets
        SingleApplication::SingleApplication
)

endif() # EZ4CONNECT_BUILD_GUI

include(CTest)

if(BUILD_TESTING)
    # A test is its own source file plus the engine. Tests of the user
    # interface also name the presentation sources they exercise.
    function(ez4connect_test name)
        add_executable(${name} tests/${name}.cpp ${ARGN})
        target_link_libraries(${name} PRIVATE ez4connect_engine)
        add_test(NAME ${name} COMMAND ${name})
    endfunction()

    function(ez4connect_gui_test name)
        ez4connect_test(${name} ${ARGN})
        target_link_libraries(${name} PRIVATE Qt::Gui Qt::Widgets)
        set_tests_properties(${name} PROPERTIES
            ENVIRONMENT "QT_QPA_PLATFORM=offscreen"
        )
    endfunction()

    ez4connect_test(applicationlogger_test)
    ez4connect_test(applicationlogfile_test)
    ez4connect_test(applicationpaths_test)
    ez4connect_test(autostart_test)
    ez4connect_test(connectionsession_test)
    ez4connect_test(connectionsessionstate_test)
    ez4connect_test(corecommandbuilder_test)
    ez4connect_test(coreoutputbuffer_test)
    ez4connect_test(coreoutputparser_test)
    ez4connect_test(profileimport_test)
    ez4connect_test(profilemanager_test)
    ez4connect_test(profileservice_test)
    ez4connect_test(profilesettings_test)
    ez4connect_test(settingsmigrator_test)
    ez4connect_test(settingsprofileloader_test)
    ez4connect_test(systemproxysession_test)
    ez4connect_test(updatechecker_test)

    # The stand-in cores are shell scripts.
    if(UNIX)
        ez4connect_test(zjuconnectprocess_test)
    endif()

    if(EZ4CONNECT_BUILD_GUI)
    ez4connect_gui_test(configurationguidedialog_test
        presentation/dialogs/configurationguidedialog/configurationguidedialog.cpp
        presentation/dialogs/authinfowindow/authinfowindow.cpp
    )

    ez4connect_gui_test(graphcaptchawindow_test
        presentation/dialogs/graphcaptchawindow/graphcaptchawindow.cpp
        presentation/dialogs/graphcaptchawindow/captchacanvas.cpp
    )

    ez4connect_gui_test(connectionuicontroller_test
        presentation/coordinators/connectionuicontroller.cpp
        presentation/coordinators/authdialogcoordinator.cpp
        presentation/dialogs/loginwindow/loginwindow.cpp
        presentation/dialogs/sudowindow/sudowindow.cpp
        presentation/dialogs/ssologinwebview/ssologinwebview.cpp
        presentation/dialogs/graphcaptchawindow/graphcaptchawindow.cpp
        presentation/dialogs/graphcaptchawindow/captchacanvas.cpp
        presentation/presentationhelpers.cpp
    )
    target_link_libraries(connectionuicontroller_test PRIVATE Qt::WebEngineWidgets)
    endif() # EZ4CONNECT_BUILD_GUI
endif()
