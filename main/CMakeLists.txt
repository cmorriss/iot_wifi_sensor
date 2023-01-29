set(srcs "main.c"
        "volf_misc.c"
        "battery_state.c"
        "volf_ota_update.c"
        "volf_wifi_connect.c"
        "volf_error.c"
        "volf_log.c"
        "sensors/ds18b20.c"
        "sensors/soil_moisture_sensor.c"
        "sensors/temperature_sensor.c"
        "sensors/ac_current_sensor.c"
        "sensors/sht40_sensor.c")
idf_build_get_property(project_dir PROJECT_DIR)
idf_component_register(SRCS "${srcs}"
        INCLUDE_DIRS "."
        EMBED_TXTFILES ${project_dir}/server-certs/ca_cert.pem ${project_dir}/aws-certs/aws-root-ca.pem ${project_dir}/aws-certs/certificate.pem.crt ${project_dir}/aws-certs/private.pem.key)

