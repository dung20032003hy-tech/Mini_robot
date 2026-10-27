idf_component_register(SRCS "web_server.c" INCLUDE_DIRS "include"
    REQUIRES esp_http_server json command_protocol settings_manager safety_manager
    diagnostics wifi_manager robot_eyes audio_input audio_output battery_monitor test_runner voice_assistant
    EMBED_FILES "../../web/index.html" "../../web/style.css" "../../web/app.js")
