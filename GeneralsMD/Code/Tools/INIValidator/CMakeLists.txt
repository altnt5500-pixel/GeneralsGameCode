add_executable(z_inivalidator)
set_target_properties(z_inivalidator PROPERTIES OUTPUT_NAME "inivalidator${RTS_BUILD_OUTPUT_SUFFIX}")

target_sources(z_inivalidator PRIVATE
	INIValidator.cpp
)

target_link_libraries(z_inivalidator PRIVATE
	core_debug
	core_profile_legacy
	imm32
	vfw32
	winmm
	z_gameengine
	z_gameenginedevice
	zi_always
)
