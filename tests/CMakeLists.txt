apply_include_paths("${JRTPLIB_INTERNAL_INCLUDES}")
apply_include_paths("${JRTPLIB_EXTERNAL_INCLUDES}")

foreach(T testmultiplex testexistingsockets testautoportbase srtptest rtcpdump readlogfile
	  timetest timeinittest abortdesctest abortdescipv6 tcptest sigintrtest
	  testexttrans testrawpacket clentservertest_linux)

	if(${T} STREQUAL clentservertest_linux)
		add_executable(${T} ${T}.cpp log.c)
	else()
		add_executable(${T} ${T}.cpp)
	endif()

	if (NOT MSVC OR JRTPLIB_COMPILE_STATIC)
		target_link_libraries(${T} jrtplib-static)
	else ()
		target_link_libraries(${T} jrtplib-shared)
	endif ()
endforeach(T)

