add_executable(main main.c)

# pull in common dependencies
target_link_libraries(main
    pico_stdlib
    hardware_spi
    hardware_dma
    hardware_gpio
    freertos
    hardware_adc
    oled1_lib
)

# create map/bin/hex/uf2 file etc.
pico_add_extra_outputs(main)
