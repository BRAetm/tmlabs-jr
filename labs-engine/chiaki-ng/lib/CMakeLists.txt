
set(HEADER_FILES
		include/labs/session.h
		include/labs/common.h
		include/labs/sock.h
		include/labs/thread.h
		include/labs/base64.h
		include/labs/http.h
		include/labs/log.h
		include/labs/ctrl.h
		include/labs/rpcrypt.h
		include/labs/takion.h
		include/labs/senkusha.h
		include/labs/streamconnection.h
		include/labs/ecdh.h
		include/labs/launchspec.h
		include/labs/random.h
		include/labs/gkcrypt.h
		include/labs/audio.h
		include/labs/audioreceiver.h
		include/labs/audiosender.h
		include/labs/video.h
		include/labs/videoreceiver.h
		include/labs/frameprocessor.h
		include/labs/packetstats.h
		include/labs/seqnum.h
		include/labs/discovery.h
		include/labs/congestioncontrol.h
		include/labs/stoppipe.h
		include/labs/reorderqueue.h
		include/labs/discoveryservice.h
		include/labs/feedback.h
		include/labs/feedbacksender.h
		include/labs/controller.h
		include/labs/takionsendbuffer.h
		include/labs/time.h
		include/labs/fec.h
		include/labs/regist.h
		include/labs/opusdecoder.h
		include/labs/opusencoder.h
		include/labs/orientation.h
		include/labs/bitstream.h
		include/labs/remote/holepunch.h
		include/labs/remote/rudp.h
		include/labs/remote/rudpsendbuffer.h)

set(SOURCE_FILES
		src/common.c
		src/sock.c
		src/session.c
		src/thread.c
		src/base64.c
		src/http.c
		src/log.c
		src/ctrl.c
		src/rpcrypt.c
		src/takion.c
		src/senkusha.c
		src/utils.h
		src/pb_utils.h
		src/streamconnection.c
		src/ecdh.c
		src/launchspec.c
		src/random.c
		src/gkcrypt.c
		src/audio.c
		src/audioreceiver.c
		src/audiosender.c
		src/videoreceiver.c
		src/frameprocessor.c
		src/packetstats.c
		src/discovery.c
		src/congestioncontrol.c
		src/stoppipe.c
		src/reorderqueue.c
		src/discoveryservice.c
		src/feedback.c
		src/feedbacksender.c
		src/controller.c
		src/takionsendbuffer.c
		src/time.c
		src/fec.c
		src/regist.c
		src/opusdecoder.c
		src/opusencoder.c
		src/orientation.c
		src/bitstream.c
		src/remote/holepunch.c
		src/remote/rudp.c
		src/remote/rudpsendbuffer.c
		src/session_shims.c)

if(LABS_ENABLE_FFMPEG_DECODER)
	list(APPEND HEADER_FILES include/labs/ffmpegdecoder.h)
	list(APPEND SOURCE_FILES src/ffmpegdecoder.c)
endif()
set(LABS_LIB_ENABLE_FFMPEG_DECODER "${LABS_ENABLE_FFMPEG_DECODER}")
set(LABS_LIB_ENABLE_PI_DECODER "${LABS_ENABLE_FFMPEG_DECODER}")

if(LABS_ENABLE_PI_DECODER)
	list(APPEND HEADER_FILES include/labs/pidecoder.h)
	list(APPEND SOURCE_FILES src/pidecoder.c)
endif()
set(LABS_LIB_ENABLE_PI_DECODER "${LABS_ENABLE_PI_DECODER}")

add_subdirectory(protobuf)
set_source_files_properties(${LABS_LIB_PROTO_SOURCE_FILES} ${LABS_LIB_PROTO_HEADER_FILES} PROPERTIES GENERATED TRUE)
include_directories("${LABS_LIB_PROTO_INCLUDE_DIR}")

if(LABS_LIB_ENABLE_OPUS)
	find_package(Opus REQUIRED)
	include_directories(${Opus_INCLUDE_DIRS})
endif()

add_library(labs-lib ${HEADER_FILES} ${SOURCE_FILES} ${LABS_LIB_PROTO_SOURCE_FILES} ${LABS_LIB_PROTO_HEADER_FILES})
configure_file(config.h.in include/labs/config.h)
target_include_directories(labs-lib PUBLIC "${CMAKE_CURRENT_BINARY_DIR}/include")

if(CMAKE_C_COMPILER_ID STREQUAL GNU OR CMAKE_C_COMPILER_ID STREQUAL Clang)
	target_compile_options(labs-lib PRIVATE -Wall)
endif()

add_dependencies(labs-lib labs-pb)
set_target_properties(labs-lib PROPERTIES
    OUTPUT_NAME labs
    WINDOWS_EXPORT_ALL_SYMBOLS ON)

if(WIN32)
	target_link_libraries(labs-lib wsock32 ws2_32 bcrypt iphlpapi)
endif()

if(APPLE AND NOT IOS AND NOT TVOS)
	target_link_libraries(labs-lib "-framework CoreServices")
endif()

target_include_directories(labs-lib PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/include")

find_package(Threads REQUIRED)
target_link_libraries(labs-lib Threads::Threads)

if(LABS_LIB_JSONC_EXTERNAL_PROJECT)
	target_link_libraries(labs-lib json-c)
	target_include_directories(labs-lib PRIVATE "${JSONC_PREFIX_INCLUDE_DIR}")
	target_include_directories(labs-lib PRIVATE "${json-c_BINARY_DIR}")
elseif(LABS_IS_SWITCH)
	find_library(JSONC_LIB json-c ${PORTLIBS}/lib)
	target_link_libraries(labs-lib ${JSONC_LIB})
	target_include_directories(labs-lib PUBLIC "${PORTLIBS}/include/json-c")
else()
	# Prefer vcpkg CMake config — pkg-config mangles paths containing spaces.
	find_package(json-c CONFIG QUIET)
	if(TARGET json-c::json-c)
		target_link_libraries(labs-lib json-c::json-c)
	else()
		find_package(PkgConfig REQUIRED)
		pkg_search_module(json-c REQUIRED json-c IMPORTED_TARGET)
		target_include_directories(labs-lib PRIVATE ${json-c_INCLUDE_DIRS})
		target_link_libraries(labs-lib PkgConfig::json-c)
	endif()
endif()

if(LABS_LIB_MINIUPNPC_EXTERNAL_PROJECT)
	target_link_libraries(labs-lib libminiupnpc-static)
	target_include_directories(labs-lib PRIVATE "${MINIUPNPC_PREFIX_INCLUDE_DIR}")
else()
	find_package(miniupnpc CONFIG QUIET)
	if(TARGET miniupnpc::miniupnpc)
		target_link_libraries(labs-lib miniupnpc::miniupnpc)
	else()
		find_package(PkgConfig REQUIRED)
		pkg_search_module(MINIUPNPC REQUIRED miniupnpc IMPORTED_TARGET)
		target_link_libraries(labs-lib PkgConfig::MINIUPNPC)
	endif()
endif()

if(NOT LABS_IS_SWITCH AND NOT LABS_ENABLE_ANDROID)
	# vcpkg ships a CMake config (LibeventConfig.cmake) — try that first.
	find_package(Libevent CONFIG QUIET)
	if(TARGET event OR TARGET libevent::core)
		if(TARGET libevent::core)
			target_link_libraries(labs-lib libevent::core libevent::extra)
		else()
			target_link_libraries(labs-lib event)
		endif()
	else()
		find_program(PKG_CONFIG_EXECUTABLE pkg-config)
		if(PKG_CONFIG_EXECUTABLE)
			pkg_search_module(LIBEVENT libevent QUIET IMPORTED_TARGET)
		endif()
		if(TARGET PkgConfig::LIBEVENT)
			target_link_libraries(labs-lib PkgConfig::LIBEVENT)
		else()
			find_package(libevent REQUIRED)
			target_include_directories(labs-lib PRIVATE ${LIBEVENT_INCLUDE_DIRS})
			target_link_libraries(labs-lib ${LIBEVENT_LIBRARIES})
		endif()
	endif()
endif()

target_link_libraries(labs-lib CURL::libcurl)

if(LABS_LIB_ENABLE_MBEDTLS)
	if(LABS_LIB_MBEDTLS_EXTERNAL_PROJECT)
		target_link_libraries(labs-lib mbedtls mbedx509 mbedcrypto)
	else()
		# provided by mbedtls-static (mbedtls-devel)
		find_library(MBEDTLS mbedtls)
		find_library(MBEDX509 mbedx509)
		find_library(MBEDCRYPTO mbedcrypto)
		target_link_libraries(labs-lib ${MBEDTLS} ${MBEDX509} ${MBEDCRYPTO})
	endif()
elseif(LABS_LIB_OPENSSL_EXTERNAL_PROJECT)
	target_link_libraries(labs-lib OpenSSL_Crypto)
else()
	# default
	find_package(OpenSSL REQUIRED)
	target_link_libraries(labs-lib OpenSSL::Crypto)
endif()

target_link_libraries(labs-lib Nanopb::nanopb)
target_link_libraries(labs-lib Jerasure::Jerasure)

if(LABS_ENABLE_FFMPEG_DECODER)
	target_link_libraries(labs-lib FFMPEG::avcodec FFMPEG::avutil)
endif()

if(LABS_ENABLE_PI_DECODER)
	target_link_libraries(labs-lib ILClient::ILClient)
endif()

if(LABS_LIB_ENABLE_OPUS)
	target_link_libraries(labs-lib ${Opus_LIBRARIES})
endif()

#set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -fanalyzer")
if(LABS_ENABLE_TESTS)
	add_executable(holepunch-test include/labs/remote/holepunch.h src/remote/holepunch-test.c)
	target_link_libraries(holepunch-test labs-lib)
endif()
