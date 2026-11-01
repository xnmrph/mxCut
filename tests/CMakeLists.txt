find_package(Qt6 REQUIRED COMPONENTS Test Network)

# The core classes, built once for every test.
add_library(trimmer_core STATIC
    ../src/TrimJob.h ../src/TrimJob.cpp
    ../src/ExportQueue.h ../src/ExportQueue.cpp
    ../src/VideoFolderModel.h ../src/VideoFolderModel.cpp
    ../src/Settings.h ../src/Settings.cpp
    ../src/Encoders.h ../src/Encoders.cpp
    ../src/EncoderProbe.h ../src/EncoderProbe.cpp
    ../src/MediaPreview.h ../src/MediaPreview.cpp
    ../src/Updater.h ../src/Updater.cpp
)
set_target_properties(trimmer_core PROPERTIES AUTOMOC ON)
target_include_directories(trimmer_core PUBLIC ../src)
target_link_libraries(trimmer_core PUBLIC Qt6::Core Qt6::Network Qt6::Qml)
if(WIN32)
    target_link_libraries(trimmer_core PUBLIC shell32)
endif()

foreach(test tst_trimjob tst_exportqueue tst_videofoldermodel tst_settings tst_encoderprobe tst_mediapreview tst_updater)
    qt_add_executable(${test} ${test}.cpp testclip.h)
    target_link_libraries(${test} PRIVATE trimmer_core Qt6::Test)
    # qt_add_executable makes Windows GUI programs, which print nothing.
    set_target_properties(${test} PROPERTIES WIN32_EXECUTABLE FALSE)
    add_test(NAME ${test} COMMAND ${test})
    set_tests_properties(${test} PROPERTIES TIMEOUT 300)
endforeach()
