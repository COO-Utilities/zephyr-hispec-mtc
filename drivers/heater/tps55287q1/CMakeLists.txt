zephyr_library()
# zephyr_library_sources(tps55287q1.c)
zephyr_library_sources_ifdef(CONFIG_TPS55287Q1 tps55287q1.c)