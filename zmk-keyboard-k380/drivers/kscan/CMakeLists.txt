zephyr_library()
zephyr_library_sources_ifdef(CONFIG_K380_KSCAN_NO_DIODE_MATRIX
  kscan_k380_no_diode_matrix.c
  ../../src/ghost_filter.c
)
