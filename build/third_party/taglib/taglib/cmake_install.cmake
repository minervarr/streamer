# Install script for directory: C:/Users/incxiuefb/Documents/Files/clone/streamer/third_party/taglib/taglib

# Set the install prefix
if(NOT DEFINED CMAKE_INSTALL_PREFIX)
  set(CMAKE_INSTALL_PREFIX "C:/Program Files (x86)/streamer")
endif()
string(REGEX REPLACE "/$" "" CMAKE_INSTALL_PREFIX "${CMAKE_INSTALL_PREFIX}")

# Set the install configuration name.
if(NOT DEFINED CMAKE_INSTALL_CONFIG_NAME)
  if(BUILD_TYPE)
    string(REGEX REPLACE "^[^A-Za-z0-9_]+" ""
           CMAKE_INSTALL_CONFIG_NAME "${BUILD_TYPE}")
  else()
    set(CMAKE_INSTALL_CONFIG_NAME "Release")
  endif()
  message(STATUS "Install configuration: \"${CMAKE_INSTALL_CONFIG_NAME}\"")
endif()

# Set the component getting installed.
if(NOT CMAKE_INSTALL_COMPONENT)
  if(COMPONENT)
    message(STATUS "Install component: \"${COMPONENT}\"")
    set(CMAKE_INSTALL_COMPONENT "${COMPONENT}")
  else()
    set(CMAKE_INSTALL_COMPONENT)
  endif()
endif()

# Is this installation the result of a crosscompile?
if(NOT DEFINED CMAKE_CROSSCOMPILING)
  set(CMAKE_CROSSCOMPILING "FALSE")
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/lib" TYPE STATIC_LIBRARY FILES "C:/Users/incxiuefb/Documents/Files/clone/streamer/build/third_party/taglib/taglib/tag.lib")
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/include/taglib" TYPE FILE FILES
    "C:/Users/incxiuefb/Documents/Files/clone/streamer/third_party/taglib/taglib/tag.h"
    "C:/Users/incxiuefb/Documents/Files/clone/streamer/third_party/taglib/taglib/fileref.h"
    "C:/Users/incxiuefb/Documents/Files/clone/streamer/third_party/taglib/taglib/audioproperties.h"
    "C:/Users/incxiuefb/Documents/Files/clone/streamer/third_party/taglib/taglib/taglib_export.h"
    "C:/Users/incxiuefb/Documents/Files/clone/streamer/build/third_party/taglib/taglib/../taglib_config.h"
    "C:/Users/incxiuefb/Documents/Files/clone/streamer/third_party/taglib/taglib/toolkit/taglib.h"
    "C:/Users/incxiuefb/Documents/Files/clone/streamer/third_party/taglib/taglib/toolkit/tstring.h"
    "C:/Users/incxiuefb/Documents/Files/clone/streamer/third_party/taglib/taglib/toolkit/tlist.h"
    "C:/Users/incxiuefb/Documents/Files/clone/streamer/third_party/taglib/taglib/toolkit/tlist.tcc"
    "C:/Users/incxiuefb/Documents/Files/clone/streamer/third_party/taglib/taglib/toolkit/tstringlist.h"
    "C:/Users/incxiuefb/Documents/Files/clone/streamer/third_party/taglib/taglib/toolkit/tbytevector.h"
    "C:/Users/incxiuefb/Documents/Files/clone/streamer/third_party/taglib/taglib/toolkit/tbytevectorlist.h"
    "C:/Users/incxiuefb/Documents/Files/clone/streamer/third_party/taglib/taglib/toolkit/tvariant.h"
    "C:/Users/incxiuefb/Documents/Files/clone/streamer/third_party/taglib/taglib/toolkit/tbytevectorstream.h"
    "C:/Users/incxiuefb/Documents/Files/clone/streamer/third_party/taglib/taglib/toolkit/tiostream.h"
    "C:/Users/incxiuefb/Documents/Files/clone/streamer/third_party/taglib/taglib/toolkit/tfile.h"
    "C:/Users/incxiuefb/Documents/Files/clone/streamer/third_party/taglib/taglib/toolkit/tfilestream.h"
    "C:/Users/incxiuefb/Documents/Files/clone/streamer/third_party/taglib/taglib/toolkit/tmap.h"
    "C:/Users/incxiuefb/Documents/Files/clone/streamer/third_party/taglib/taglib/toolkit/tmap.tcc"
    "C:/Users/incxiuefb/Documents/Files/clone/streamer/third_party/taglib/taglib/toolkit/tpicturetype.h"
    "C:/Users/incxiuefb/Documents/Files/clone/streamer/third_party/taglib/taglib/toolkit/tpropertymap.h"
    "C:/Users/incxiuefb/Documents/Files/clone/streamer/third_party/taglib/taglib/toolkit/tdebuglistener.h"
    "C:/Users/incxiuefb/Documents/Files/clone/streamer/third_party/taglib/taglib/toolkit/tversionnumber.h"
    "C:/Users/incxiuefb/Documents/Files/clone/streamer/third_party/taglib/taglib/mpeg/mpegfile.h"
    "C:/Users/incxiuefb/Documents/Files/clone/streamer/third_party/taglib/taglib/mpeg/mpegproperties.h"
    "C:/Users/incxiuefb/Documents/Files/clone/streamer/third_party/taglib/taglib/mpeg/mpegheader.h"
    "C:/Users/incxiuefb/Documents/Files/clone/streamer/third_party/taglib/taglib/mpeg/xingheader.h"
    "C:/Users/incxiuefb/Documents/Files/clone/streamer/third_party/taglib/taglib/mpeg/id3v1/id3v1tag.h"
    "C:/Users/incxiuefb/Documents/Files/clone/streamer/third_party/taglib/taglib/mpeg/id3v1/id3v1genres.h"
    "C:/Users/incxiuefb/Documents/Files/clone/streamer/third_party/taglib/taglib/mpeg/id3v2/id3v2.h"
    "C:/Users/incxiuefb/Documents/Files/clone/streamer/third_party/taglib/taglib/mpeg/id3v2/id3v2extendedheader.h"
    "C:/Users/incxiuefb/Documents/Files/clone/streamer/third_party/taglib/taglib/mpeg/id3v2/id3v2frame.h"
    "C:/Users/incxiuefb/Documents/Files/clone/streamer/third_party/taglib/taglib/mpeg/id3v2/id3v2header.h"
    "C:/Users/incxiuefb/Documents/Files/clone/streamer/third_party/taglib/taglib/mpeg/id3v2/id3v2synchdata.h"
    "C:/Users/incxiuefb/Documents/Files/clone/streamer/third_party/taglib/taglib/mpeg/id3v2/id3v2footer.h"
    "C:/Users/incxiuefb/Documents/Files/clone/streamer/third_party/taglib/taglib/mpeg/id3v2/id3v2framefactory.h"
    "C:/Users/incxiuefb/Documents/Files/clone/streamer/third_party/taglib/taglib/mpeg/id3v2/id3v2tag.h"
    "C:/Users/incxiuefb/Documents/Files/clone/streamer/third_party/taglib/taglib/mpeg/id3v2/frames/attachedpictureframe.h"
    "C:/Users/incxiuefb/Documents/Files/clone/streamer/third_party/taglib/taglib/mpeg/id3v2/frames/commentsframe.h"
    "C:/Users/incxiuefb/Documents/Files/clone/streamer/third_party/taglib/taglib/mpeg/id3v2/frames/eventtimingcodesframe.h"
    "C:/Users/incxiuefb/Documents/Files/clone/streamer/third_party/taglib/taglib/mpeg/id3v2/frames/generalencapsulatedobjectframe.h"
    "C:/Users/incxiuefb/Documents/Files/clone/streamer/third_party/taglib/taglib/mpeg/id3v2/frames/ownershipframe.h"
    "C:/Users/incxiuefb/Documents/Files/clone/streamer/third_party/taglib/taglib/mpeg/id3v2/frames/popularimeterframe.h"
    "C:/Users/incxiuefb/Documents/Files/clone/streamer/third_party/taglib/taglib/mpeg/id3v2/frames/privateframe.h"
    "C:/Users/incxiuefb/Documents/Files/clone/streamer/third_party/taglib/taglib/mpeg/id3v2/frames/relativevolumeframe.h"
    "C:/Users/incxiuefb/Documents/Files/clone/streamer/third_party/taglib/taglib/mpeg/id3v2/frames/synchronizedlyricsframe.h"
    "C:/Users/incxiuefb/Documents/Files/clone/streamer/third_party/taglib/taglib/mpeg/id3v2/frames/textidentificationframe.h"
    "C:/Users/incxiuefb/Documents/Files/clone/streamer/third_party/taglib/taglib/mpeg/id3v2/frames/uniquefileidentifierframe.h"
    "C:/Users/incxiuefb/Documents/Files/clone/streamer/third_party/taglib/taglib/mpeg/id3v2/frames/unknownframe.h"
    "C:/Users/incxiuefb/Documents/Files/clone/streamer/third_party/taglib/taglib/mpeg/id3v2/frames/unsynchronizedlyricsframe.h"
    "C:/Users/incxiuefb/Documents/Files/clone/streamer/third_party/taglib/taglib/mpeg/id3v2/frames/urllinkframe.h"
    "C:/Users/incxiuefb/Documents/Files/clone/streamer/third_party/taglib/taglib/mpeg/id3v2/frames/chapterframe.h"
    "C:/Users/incxiuefb/Documents/Files/clone/streamer/third_party/taglib/taglib/mpeg/id3v2/frames/tableofcontentsframe.h"
    "C:/Users/incxiuefb/Documents/Files/clone/streamer/third_party/taglib/taglib/mpeg/id3v2/frames/podcastframe.h"
    "C:/Users/incxiuefb/Documents/Files/clone/streamer/third_party/taglib/taglib/ogg/oggfile.h"
    "C:/Users/incxiuefb/Documents/Files/clone/streamer/third_party/taglib/taglib/ogg/oggpage.h"
    "C:/Users/incxiuefb/Documents/Files/clone/streamer/third_party/taglib/taglib/ogg/oggpageheader.h"
    "C:/Users/incxiuefb/Documents/Files/clone/streamer/third_party/taglib/taglib/ogg/xiphcomment.h"
    "C:/Users/incxiuefb/Documents/Files/clone/streamer/third_party/taglib/taglib/ogg/vorbis/vorbisfile.h"
    "C:/Users/incxiuefb/Documents/Files/clone/streamer/third_party/taglib/taglib/ogg/vorbis/vorbisproperties.h"
    "C:/Users/incxiuefb/Documents/Files/clone/streamer/third_party/taglib/taglib/ogg/flac/oggflacfile.h"
    "C:/Users/incxiuefb/Documents/Files/clone/streamer/third_party/taglib/taglib/ogg/speex/speexfile.h"
    "C:/Users/incxiuefb/Documents/Files/clone/streamer/third_party/taglib/taglib/ogg/speex/speexproperties.h"
    "C:/Users/incxiuefb/Documents/Files/clone/streamer/third_party/taglib/taglib/ogg/opus/opusfile.h"
    "C:/Users/incxiuefb/Documents/Files/clone/streamer/third_party/taglib/taglib/ogg/opus/opusproperties.h"
    "C:/Users/incxiuefb/Documents/Files/clone/streamer/third_party/taglib/taglib/flac/flacfile.h"
    "C:/Users/incxiuefb/Documents/Files/clone/streamer/third_party/taglib/taglib/flac/flacpicture.h"
    "C:/Users/incxiuefb/Documents/Files/clone/streamer/third_party/taglib/taglib/flac/flacproperties.h"
    "C:/Users/incxiuefb/Documents/Files/clone/streamer/third_party/taglib/taglib/flac/flacmetadatablock.h"
    "C:/Users/incxiuefb/Documents/Files/clone/streamer/third_party/taglib/taglib/ape/apefile.h"
    "C:/Users/incxiuefb/Documents/Files/clone/streamer/third_party/taglib/taglib/ape/apeproperties.h"
    "C:/Users/incxiuefb/Documents/Files/clone/streamer/third_party/taglib/taglib/ape/apetag.h"
    "C:/Users/incxiuefb/Documents/Files/clone/streamer/third_party/taglib/taglib/ape/apefooter.h"
    "C:/Users/incxiuefb/Documents/Files/clone/streamer/third_party/taglib/taglib/ape/apeitem.h"
    "C:/Users/incxiuefb/Documents/Files/clone/streamer/third_party/taglib/taglib/mpc/mpcfile.h"
    "C:/Users/incxiuefb/Documents/Files/clone/streamer/third_party/taglib/taglib/mpc/mpcproperties.h"
    "C:/Users/incxiuefb/Documents/Files/clone/streamer/third_party/taglib/taglib/wavpack/wavpackfile.h"
    "C:/Users/incxiuefb/Documents/Files/clone/streamer/third_party/taglib/taglib/wavpack/wavpackproperties.h"
    "C:/Users/incxiuefb/Documents/Files/clone/streamer/third_party/taglib/taglib/trueaudio/trueaudiofile.h"
    "C:/Users/incxiuefb/Documents/Files/clone/streamer/third_party/taglib/taglib/trueaudio/trueaudioproperties.h"
    "C:/Users/incxiuefb/Documents/Files/clone/streamer/third_party/taglib/taglib/riff/rifffile.h"
    "C:/Users/incxiuefb/Documents/Files/clone/streamer/third_party/taglib/taglib/riff/aiff/aifffile.h"
    "C:/Users/incxiuefb/Documents/Files/clone/streamer/third_party/taglib/taglib/riff/aiff/aiffproperties.h"
    "C:/Users/incxiuefb/Documents/Files/clone/streamer/third_party/taglib/taglib/riff/wav/wavfile.h"
    "C:/Users/incxiuefb/Documents/Files/clone/streamer/third_party/taglib/taglib/riff/wav/wavproperties.h"
    "C:/Users/incxiuefb/Documents/Files/clone/streamer/third_party/taglib/taglib/riff/wav/infotag.h"
    "C:/Users/incxiuefb/Documents/Files/clone/streamer/third_party/taglib/taglib/asf/asffile.h"
    "C:/Users/incxiuefb/Documents/Files/clone/streamer/third_party/taglib/taglib/asf/asfproperties.h"
    "C:/Users/incxiuefb/Documents/Files/clone/streamer/third_party/taglib/taglib/asf/asftag.h"
    "C:/Users/incxiuefb/Documents/Files/clone/streamer/third_party/taglib/taglib/asf/asfattribute.h"
    "C:/Users/incxiuefb/Documents/Files/clone/streamer/third_party/taglib/taglib/asf/asfpicture.h"
    "C:/Users/incxiuefb/Documents/Files/clone/streamer/third_party/taglib/taglib/mp4/mp4file.h"
    "C:/Users/incxiuefb/Documents/Files/clone/streamer/third_party/taglib/taglib/mp4/mp4atom.h"
    "C:/Users/incxiuefb/Documents/Files/clone/streamer/third_party/taglib/taglib/mp4/mp4tag.h"
    "C:/Users/incxiuefb/Documents/Files/clone/streamer/third_party/taglib/taglib/mp4/mp4item.h"
    "C:/Users/incxiuefb/Documents/Files/clone/streamer/third_party/taglib/taglib/mp4/mp4properties.h"
    "C:/Users/incxiuefb/Documents/Files/clone/streamer/third_party/taglib/taglib/mp4/mp4coverart.h"
    "C:/Users/incxiuefb/Documents/Files/clone/streamer/third_party/taglib/taglib/mp4/mp4stem.h"
    "C:/Users/incxiuefb/Documents/Files/clone/streamer/third_party/taglib/taglib/mp4/mp4itemfactory.h"
    "C:/Users/incxiuefb/Documents/Files/clone/streamer/third_party/taglib/taglib/mp4/mp4chapter.h"
    "C:/Users/incxiuefb/Documents/Files/clone/streamer/third_party/taglib/taglib/mp4/mp4chapterholder.h"
    "C:/Users/incxiuefb/Documents/Files/clone/streamer/third_party/taglib/taglib/mp4/mp4nerochapterlist.h"
    "C:/Users/incxiuefb/Documents/Files/clone/streamer/third_party/taglib/taglib/mp4/mp4qtchapterlist.h"
    "C:/Users/incxiuefb/Documents/Files/clone/streamer/third_party/taglib/taglib/mod/modfilebase.h"
    "C:/Users/incxiuefb/Documents/Files/clone/streamer/third_party/taglib/taglib/mod/modfile.h"
    "C:/Users/incxiuefb/Documents/Files/clone/streamer/third_party/taglib/taglib/mod/modtag.h"
    "C:/Users/incxiuefb/Documents/Files/clone/streamer/third_party/taglib/taglib/mod/modproperties.h"
    "C:/Users/incxiuefb/Documents/Files/clone/streamer/third_party/taglib/taglib/it/itfile.h"
    "C:/Users/incxiuefb/Documents/Files/clone/streamer/third_party/taglib/taglib/it/itproperties.h"
    "C:/Users/incxiuefb/Documents/Files/clone/streamer/third_party/taglib/taglib/s3m/s3mfile.h"
    "C:/Users/incxiuefb/Documents/Files/clone/streamer/third_party/taglib/taglib/s3m/s3mproperties.h"
    "C:/Users/incxiuefb/Documents/Files/clone/streamer/third_party/taglib/taglib/xm/xmfile.h"
    "C:/Users/incxiuefb/Documents/Files/clone/streamer/third_party/taglib/taglib/xm/xmproperties.h"
    "C:/Users/incxiuefb/Documents/Files/clone/streamer/third_party/taglib/taglib/dsf/dsffile.h"
    "C:/Users/incxiuefb/Documents/Files/clone/streamer/third_party/taglib/taglib/dsf/dsfproperties.h"
    "C:/Users/incxiuefb/Documents/Files/clone/streamer/third_party/taglib/taglib/dsdiff/dsdifffile.h"
    "C:/Users/incxiuefb/Documents/Files/clone/streamer/third_party/taglib/taglib/dsdiff/dsdiffproperties.h"
    "C:/Users/incxiuefb/Documents/Files/clone/streamer/third_party/taglib/taglib/dsdiff/dsdiffdiintag.h"
    "C:/Users/incxiuefb/Documents/Files/clone/streamer/third_party/taglib/taglib/shorten/shortenfile.h"
    "C:/Users/incxiuefb/Documents/Files/clone/streamer/third_party/taglib/taglib/shorten/shortenproperties.h"
    "C:/Users/incxiuefb/Documents/Files/clone/streamer/third_party/taglib/taglib/shorten/shortentag.h"
    "C:/Users/incxiuefb/Documents/Files/clone/streamer/third_party/taglib/taglib/matroska/matroskaattachedfile.h"
    "C:/Users/incxiuefb/Documents/Files/clone/streamer/third_party/taglib/taglib/matroska/matroskaattachments.h"
    "C:/Users/incxiuefb/Documents/Files/clone/streamer/third_party/taglib/taglib/matroska/matroskachapter.h"
    "C:/Users/incxiuefb/Documents/Files/clone/streamer/third_party/taglib/taglib/matroska/matroskachapteredition.h"
    "C:/Users/incxiuefb/Documents/Files/clone/streamer/third_party/taglib/taglib/matroska/matroskachapters.h"
    "C:/Users/incxiuefb/Documents/Files/clone/streamer/third_party/taglib/taglib/matroska/matroskaelement.h"
    "C:/Users/incxiuefb/Documents/Files/clone/streamer/third_party/taglib/taglib/matroska/matroskafile.h"
    "C:/Users/incxiuefb/Documents/Files/clone/streamer/third_party/taglib/taglib/matroska/matroskaproperties.h"
    "C:/Users/incxiuefb/Documents/Files/clone/streamer/third_party/taglib/taglib/matroska/matroskasimpletag.h"
    "C:/Users/incxiuefb/Documents/Files/clone/streamer/third_party/taglib/taglib/matroska/matroskatag.h"
    "C:/Users/incxiuefb/Documents/Files/clone/streamer/third_party/taglib/taglib/matroska/matroskawritestyle.h"
    )
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  if(EXISTS "$ENV{DESTDIR}${CMAKE_INSTALL_PREFIX}/lib/cmake/taglib/taglib-targets.cmake")
    file(DIFFERENT _cmake_export_file_changed FILES
         "$ENV{DESTDIR}${CMAKE_INSTALL_PREFIX}/lib/cmake/taglib/taglib-targets.cmake"
         "C:/Users/incxiuefb/Documents/Files/clone/streamer/build/third_party/taglib/taglib/CMakeFiles/Export/398eef5e047a0959864f2888198961bf/taglib-targets.cmake")
    if(_cmake_export_file_changed)
      file(GLOB _cmake_old_config_files "$ENV{DESTDIR}${CMAKE_INSTALL_PREFIX}/lib/cmake/taglib/taglib-targets-*.cmake")
      if(_cmake_old_config_files)
        string(REPLACE ";" ", " _cmake_old_config_files_text "${_cmake_old_config_files}")
        message(STATUS "Old export file \"$ENV{DESTDIR}${CMAKE_INSTALL_PREFIX}/lib/cmake/taglib/taglib-targets.cmake\" will be replaced.  Removing files [${_cmake_old_config_files_text}].")
        unset(_cmake_old_config_files_text)
        file(REMOVE ${_cmake_old_config_files})
      endif()
      unset(_cmake_old_config_files)
    endif()
    unset(_cmake_export_file_changed)
  endif()
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/lib/cmake/taglib" TYPE FILE FILES "C:/Users/incxiuefb/Documents/Files/clone/streamer/build/third_party/taglib/taglib/CMakeFiles/Export/398eef5e047a0959864f2888198961bf/taglib-targets.cmake")
  if(CMAKE_INSTALL_CONFIG_NAME MATCHES "^([Rr][Ee][Ll][Ee][Aa][Ss][Ee])$")
    file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/lib/cmake/taglib" TYPE FILE FILES "C:/Users/incxiuefb/Documents/Files/clone/streamer/build/third_party/taglib/taglib/CMakeFiles/Export/398eef5e047a0959864f2888198961bf/taglib-targets-release.cmake")
  endif()
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/lib/cmake/taglib" TYPE FILE FILES
    "C:/Users/incxiuefb/Documents/Files/clone/streamer/build/third_party/taglib/taglib-config.cmake"
    "C:/Users/incxiuefb/Documents/Files/clone/streamer/build/third_party/taglib/taglib-config-version.cmake"
    )
endif()

string(REPLACE ";" "\n" CMAKE_INSTALL_MANIFEST_CONTENT
       "${CMAKE_INSTALL_MANIFEST_FILES}")
if(CMAKE_INSTALL_LOCAL_ONLY)
  file(WRITE "C:/Users/incxiuefb/Documents/Files/clone/streamer/build/third_party/taglib/taglib/install_local_manifest.txt"
     "${CMAKE_INSTALL_MANIFEST_CONTENT}")
endif()
