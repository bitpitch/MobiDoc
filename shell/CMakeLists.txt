
include_directories(
  ${CMAKE_CURRENT_SOURCE_DIR}/..
  ${CMAKE_CURRENT_BINARY_DIR}/../
)

# okular

set(okular_SRCS
   main.cpp
   okular_main.cpp
   shell.cpp
   shellutils.cpp
   welcomescreen.cpp

   ../gui/recentitemsmodel.cpp

   shell.qrc
)

ki18n_wrap_ui(okular_SRCS
    welcomescreen.ui)

file(GLOB ICONS_SRCS "${CMAKE_CURRENT_SOURCE_DIR}/../icons/*-apps-mobidoc.png")
if(APPLE)
    # Generate the bundle icon directly from the provided 512px PNG. This
    # avoids depending on iconutil to validate an intermediate .iconset.
    set(MOBIDOC_ICNS "${CMAKE_CURRENT_BINARY_DIR}/okular_SRCS.icns")
    add_custom_command(OUTPUT "${MOBIDOC_ICNS}"
        COMMAND /usr/bin/sips -s format icns "${CMAKE_CURRENT_SOURCE_DIR}/../icons/512-apps-mobidoc.png" --out "${MOBIDOC_ICNS}"
        DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/../icons/512-apps-mobidoc.png"
        VERBATIM)
    set_source_files_properties("${MOBIDOC_ICNS}" PROPERTIES MACOSX_PACKAGE_LOCATION Resources)
    list(APPEND okular_SRCS "${MOBIDOC_ICNS}")
elseif(WIN32)
    set(MOBIDOC_WINDOWS_ICON "${CMAKE_CURRENT_SOURCE_DIR}/../icons/icona.ico")
    file(TO_CMAKE_PATH "${MOBIDOC_WINDOWS_ICON}" MOBIDOC_WINDOWS_ICON_RC)
    set(MOBIDOC_WINDOWS_RC "${CMAKE_CURRENT_BINARY_DIR}/mobidoc_icon.rc")
    configure_file(mobidoc_icon.rc.in "${MOBIDOC_WINDOWS_RC}" @ONLY)
    set_source_files_properties("${MOBIDOC_WINDOWS_RC}" PROPERTIES OBJECT_DEPENDS "${MOBIDOC_WINDOWS_ICON}")
    list(APPEND okular_SRCS "${MOBIDOC_WINDOWS_RC}")
else()
    ecm_add_app_icon(okular_SRCS ICONS ${ICONS_SRCS})
endif()

add_executable(okular ${okular_SRCS})
set_target_properties(okular PROPERTIES OUTPUT_NAME MobiDoc)

target_link_libraries(okular KF6::I18n KF6::Parts KF6::WindowSystem KF6::Crash KF6::IconThemes KF6::KIOGui)
if (TARGET Qt6::DBus)
    target_link_libraries(okular Qt6::DBus)
endif()
if (HAVE_X11)
    target_link_libraries(okular Qt6::GuiPrivate)
endif()

install(TARGETS okular ${KDE_INSTALL_TARGETS_DEFAULT_ARGS})

if(APPLE)
    target_sources(okular PRIVATE nativemenus_mac.mm)
    set_source_files_properties(nativemenus_mac.mm PROPERTIES COMPILE_OPTIONS "-fobjc-arc")
    find_library(MOBIDOC_APPKIT_FRAMEWORK AppKit REQUIRED)
    target_link_libraries(okular ${MOBIDOC_APPKIT_FRAMEWORK})
    set_target_properties(okular PROPERTIES
        MACOSX_BUNDLE_DISPLAY_NAME "MobiDoc"
        MACOSX_BUNDLE_BUNDLE_NAME "MobiDoc"
        MACOSX_BUNDLE_ICON_FILE "okular_SRCS"
        MACOSX_BUNDLE_LONG_VERSION_STRING "MobiDoc ${RELEASE_SERVICE_VERSION}"
        MACOSX_BUNDLE_SHORT_VERSION_STRING "${RELEASE_SERVICE_VERSION}"
        MACOSX_BUNDLE_BUNDLE_VERSION "${RELEASE_SERVICE_VERSION}"
        MACOSX_BUNDLE_INFO_PLIST "${CMAKE_CURRENT_SOURCE_DIR}/MacOSXBundleInfo.plist.in"
        MACOSX_BUNDLE_GUI_IDENTIFIER "com.mobicod.mobidoc"
        MACOSX_BUNDLE_COPYRIGHT "2002-2026 The Okular Developers")
endif()

# okular shell data files

install( PROGRAMS com.mobicod.mobidoc.desktop DESTINATION ${KDE_INSTALL_APPDIR} )
install( FILES com.mobicod.mobidoc.metainfo.xml DESTINATION ${KDE_INSTALL_METAINFODIR} )
