remove_definitions(-DTRANSLATION_DOMAIN="okular")
add_definitions(-DTRANSLATION_DOMAIN="okular_poppler")

find_package(qpdf CONFIG REQUIRED)
if(qpdf_VERSION VERSION_LESS 11)
    message(FATAL_ERROR "PDF bookmark editing requires QPDF 11 or newer")
endif()
set_package_properties(qpdf PROPERTIES
    DESCRIPTION "A library for preserving and editing PDF document structure"
    URL "https://qpdf.sourceforge.io/"
    PURPOSE "Saves document bookmarks inside PDF files."
    TYPE REQUIRED
)

add_subdirectory( conf )

set(CMAKE_REQUIRED_LIBRARIES Poppler::Qt6 Qt6::Core Qt6::Gui)

include_directories(
   ${CMAKE_CURRENT_SOURCE_DIR}/../..
)

########### next target ###############

set(okularGenerator_poppler_PART_SRCS
   generator_pdf.cpp
   formfields.cpp
   annots.cpp
   pdfsignatureutils.cpp
   pdfsettingswidget.cpp
   imagescaling.cpp
   pdfbookmarks.cpp
)

ki18n_wrap_ui(okularGenerator_poppler_PART_SRCS
   conf/pdfsettingswidget.ui
)

kconfig_add_kcfg_files(okularGenerator_poppler_PART_SRCS GENERATE_MOC conf/pdfsettings.kcfgc )

okular_add_generator(okularGenerator_poppler ${okularGenerator_poppler_PART_SRCS})

target_link_libraries(okularGenerator_poppler okularcore KF6::I18n KF6::Completion KF6::KIOWidgets Poppler::Qt6 Qt6::Xml qpdf::libqpdf)
target_compile_definitions(okularGenerator_poppler PRIVATE POINTERHOLDER_TRANSITION=4)
kde_target_enable_exceptions(okularGenerator_poppler PRIVATE)

########### autotests ###############

if(BUILD_TESTING)
    ecm_add_test(autotests/pdfbookmarkstest.cpp pdfbookmarks.cpp
        TEST_NAME pdfBookmarksTest
        LINK_LIBRARIES qpdf::libqpdf
    )
    target_compile_definitions(pdfBookmarksTest PRIVATE POINTERHOLDER_TRANSITION=4)
    kde_target_enable_exceptions(pdfBookmarksTest PRIVATE)
    ecm_add_test(autotests/testimagescaling.cpp
        TEST_NAME "imageScalingTest"
        LINK_LIBRARIES Qt6::Test Qt6::Gui
    )
endif()

########### install files ###############
if (BUILD_DESKTOP)
  install( PROGRAMS okularApplication_pdf.desktop DESTINATION ${KDE_INSTALL_APPDIR} )
endif()
if (BUILD_MOBILE)
  install( PROGRAMS org.kde.mobile.okular_pdf.desktop DESTINATION ${KDE_INSTALL_APPDIR} )
endif()
install( FILES org.kde.okular-poppler.metainfo.xml DESTINATION ${KDE_INSTALL_METAINFODIR} )
