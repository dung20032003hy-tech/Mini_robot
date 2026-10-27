idf_component_register(SRCS "voice_assistant.c" "voice_config.c" "voice_dsp.c" "voice_message.c"
    INCLUDE_DIRS "include"
    REQUIRES settings_manager audio_input audio_output safety_manager wifi_manager
    robot_eyes tcp_transport esp-tls mbedtls lwip nvs_flash esp_timer)
