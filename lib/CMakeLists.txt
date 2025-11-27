add_subdirectory_ifdef(CONFIG_COO_COMMONS coo_commons)

# Thermal controller modules
add_subdirectory(config)
add_subdirectory(sensors)
add_subdirectory(heaters)
add_subdirectory(control)
