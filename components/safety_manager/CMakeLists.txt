idf_component_register(SRCS "safety_manager.c" INCLUDE_DIRS "include" REQUIRES motor_control servo_control settings_manager control_core esp_timer esp_system)
