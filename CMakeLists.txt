cmake_minimum_required(VERSION 2.8)

if(NOT DEFINED CMAKE_TOOLCHAIN_FILE)
  if(DEFINED ENV{VITASDK})
    set(CMAKE_TOOLCHAIN_FILE "$ENV{VITASDK}/share/vita.toolchain.cmake" CACHE PATH "toolchain file")
  else()
    message(FATAL_ERROR "Please define VITASDK to point to your SDK path!")
  endif()
endif()

project(YoYoLoader C CXX)
include("${VITASDK}/share/vita.cmake" REQUIRED)
set(VITA_APP_NAME "YoYo Loader")
set(VITA_TITLEID  "YYOLOADER")
set(VITA_VERSION  "01.00")
set(VITA_MKSFOEX_FLAGS "-d ATTRIBUTE2=12")

execute_process(COMMAND git describe --abbrev=6 --dirty --always
	OUTPUT_VARIABLE GIT_HASH
	OUTPUT_STRIP_TRAILING_WHITESPACE
)

if (LOADER)
set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -g -Wl,-q,--wrap,memcpy,--wrap,memmove,--wrap,memset -Wno-format-overflow -D_GNU_SOURCE -Wall -O3 -mfloat-abi=softfp")
set(CMAKE_CXX_FLAGS "${CMAKE_C_FLAGS} -Wno-write-strings -DGIT_VERSION=\"${GIT_HASH}\" -std=c++11")

if (VIDEO_SUPPORT)
set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -DHAS_VIDEO_PLAYBACK_SUPPORT")
endif(VIDEO_SUPPORT)

if (STANDALONE)
set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -DSTANDALONE_MODE")
endif(STANDALONE)

add_executable(YoYoLoader
  loader/main.c
  loader/dialog.c
  loader/so_util.c
  loader/sha1.c
  loader/openal_patch.c
  loader/glsl2cg.c
  loader/gamepad.c
  loader/ioapi.c
  loader/unzip.c
  loader/zip.c
  loader/ctype_patch.c
  loader/sbrk.c
  loader/videoplayer_patch.c
  loader/net.c
)

target_link_libraries(YoYoLoader
  -Wl,--whole-archive pthread -Wl,--no-whole-archive
  openal
  m
  z
  stdc++
  vorbis
  vorbisfile
  ogg
  vitaGL
  vitashark
  mathneon
  curl
  ssl
  crypto
  kubridge_stub
  SceAppMgr_stub
  SceAppUtil_stub
  SceAudio_stub
  SceAudioIn_stub
  SceCtrl_stub
  SceCommonDialog_stub
  SceDisplay_stub
  SceKernelDmacMgr_stub
  SceFios2_stub
  SceGxm_stub
  SceMotion_stub
  SceNet_stub
  SceShaccCg_stub
  SceSysmodule_stub
  ScePower_stub
  SceTouch_stub
  SceVshBridge_stub
  SceAvPlayer_stub
)

vita_create_self(loader2.bin YoYoLoader UNSAFE)
else()
set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -g -Wl,-q,--wrap,memcpy,--wrap,memmove,--wrap,memset -Wno-format-overflow -D_GNU_SOURCE -Wall -O3 -mfloat-abi=softfp")
set(CMAKE_CXX_FLAGS "${CMAKE_C_FLAGS} -Wno-write-strings -DGIT_VERSION=\"${GIT_HASH}\" -std=c++11")

add_executable(YoYoLoader
  selector/main.cpp
  selector/dialogs.cpp
  selector/strings.cpp
  loader/ioapi.c
  loader/unzip.c
  loader/zip.c
  loader/dialog.c
)

add_dependencies(YoYoLoader loader.bin)

target_link_libraries(YoYoLoader
  imgui
  m
  z
  stdc++
  vitaGL
  vitashark
  mathneon
  curl
  ssl
  crypto
  kubridge_stub
  SceAppMgr_stub
  SceAppUtil_stub
  SceAudio_stub
  SceAudioIn_stub
  SceCtrl_stub
  SceCommonDialog_stub
  SceDisplay_stub
  SceKernelDmacMgr_stub
  SceGxm_stub
  SceNet_stub
  SceShaccCg_stub
  SceSysmodule_stub
  ScePower_stub
  SceTouch_stub
  SceVshBridge_stub
)

add_executable(loader
  loader/main.c
  loader/dialog.c
  loader/so_util.c
  loader/sha1.c
  loader/openal_patch.c
  loader/glsl2cg.c
  loader/gamepad.c
  loader/ioapi.c
  loader/unzip.c
  loader/zip.c
  loader/ctype_patch.c
  loader/sbrk.c
  loader/videoplayer_patch.c
  loader/net.c
)

target_link_libraries(loader
  -Wl,--whole-archive pthread -Wl,--no-whole-archive
  openal
  m
  z
  stdc++
  vorbis
  vorbisfile
  ogg
  vitaGL
  vitashark
  mathneon
  curl
  ssl
  crypto
  kubridge_stub
  SceAppMgr_stub
  SceAppUtil_stub
  SceAudio_stub
  SceAudioIn_stub
  SceCtrl_stub
  SceCommonDialog_stub
  SceDisplay_stub
  SceKernelDmacMgr_stub
  SceFios2_stub
  SceGxm_stub
  SceMotion_stub
  SceNet_stub
  SceShaccCg_stub
  SceSysmodule_stub
  ScePower_stub
  SceTouch_stub
  SceVshBridge_stub
)

vita_create_self(loader.bin loader UNSAFE)

vita_create_self(eboot.bin YoYoLoader UNSAFE)
vita_create_vpk(YoYoLoader.vpk ${VITA_TITLEID} eboot.bin
  VERSION ${VITA_VERSION}
  NAME ${VITA_APP_NAME}
  FILE ${CMAKE_SOURCE_DIR}/livearea/icon0.png sce_sys/icon0.png
       ${CMAKE_SOURCE_DIR}/livearea/default_gate.png sce_sys/livearea/contents/default_gate.png
       ${CMAKE_SOURCE_DIR}/livearea/bg0.png sce_sys/livearea/contents/bg0.png
       ${CMAKE_SOURCE_DIR}/livearea/template.xml sce_sys/livearea/contents/template.xml
       ${CMAKE_BINARY_DIR}/loader.bin loader.bin
       ${CMAKE_BINARY_DIR}/loader2.bin loader2.bin
       ${CMAKE_SOURCE_DIR}/translations/English.ini lang/English.ini
       ${CMAKE_SOURCE_DIR}/translations/Italian.ini lang/Italian.ini
)

add_custom_target(copy
  COMMAND cp eboot.bin D:/app/${VITA_TITLEID}/eboot.bin
  DEPENDS eboot.bin
)
endif()
