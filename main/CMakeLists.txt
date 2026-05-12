idf_component_register(SRCS "main.c" "MFRC522.c"
                       PRIV_REQUIRES spi_flash esp_driver_ledc esp_driver_gpio esp_driver_spi
                       INCLUDE_DIRS "")
