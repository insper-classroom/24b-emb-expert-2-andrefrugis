#include "ssd1306.h"
#include "ssd1306.h"
#include "pico/stdlib.h"
#include "hardware/spi.h"
#include "hardware/dma.h"
#include "hardware/gpio.h"

#define OLED_DMA_CHANNEL 0

inline void spi_cs_select(void) {
    asm volatile("nop \n nop \n nop");
    gpio_put(PIN_CS, 0); // Active low
    asm volatile("nop \n nop \n nop");
}

inline void spi_cs_deselect(void) {
    asm volatile("nop \n nop \n nop");
    gpio_put(PIN_CS, 1);
    asm volatile("nop \n nop \n nop");
}

void ssd1306_set_display_start_line_address(uint8_t address) {
    // Make sure address is 6 bits
    address &= 0x3F;
    ssd1306_write_command(SSD1306_CMD_SET_DISPLAY_START_LINE(address));
}

void ssd1306_set_column_address(uint8_t address) {
    // Make sure the address is 7 bits
    address &= 0x7F;
    ssd1306_write_command(SSD1306_CMD_COL_ADD_SET_MSB(address >> 4));
    ssd1306_write_command(SSD1306_CMD_COL_ADD_SET_LSB(address & 0x0F));
}

void ssd1306_set_page_address(uint8_t address) {
    // Make sure that the address is 4 bits (only 8 pages)
    address &= 0x0F;
    ssd1306_write_command(SSD1306_CMD_SET_PAGE_START_ADDRESS(address));
}

void ssd1306_display_on(void) {
    ssd1306_write_command(SSD1306_CMD_SET_DISPLAY_ON);
}

void ssd1306_display_off(void) {
    ssd1306_write_command(SSD1306_CMD_SET_DISPLAY_OFF);
}

uint8_t ssd1306_set_contrast(uint8_t contrast) {
    ssd1306_write_command(SSD1306_CMD_SET_CONTRAST_CONTROL_FOR_BANK0);
    ssd1306_write_command(contrast);
    return contrast;
}

void ssd1306_display_invert_enable(void) {
    ssd1306_write_command(SSD1306_CMD_SET_INVERSE_DISPLAY);
}

void ssd1306_display_invert_disable(void) {
    ssd1306_write_command(SSD1306_CMD_SET_NORMAL_DISPLAY);
}

void gfx_mono_ssd1306_put_byte(uint8_t page, uint8_t column, uint8_t data,
                               bool force) {

    ssd1306_set_page_address(page);
    ssd1306_set_column_address(column);

    ssd1306_write_data(data);
}

void ssd1306_interface_init(void) {
    // Inicializa o pino de Reset
    gpio_init(SSD1306_RST_PIN);
    gpio_set_dir(SSD1306_RST_PIN, GPIO_OUT);
    gpio_put(SSD1306_RST_PIN, 1);

    // Data / command select para o OLED display. High = data, Low = command.
    gpio_init(SSD1306_DATA_CMD_SEL);
    gpio_set_dir(SSD1306_DATA_CMD_SEL, GPIO_OUT);
    gpio_put(SSD1306_DATA_CMD_SEL, 0); // Inicialmente comandos

    // Inicializa o SPI
    spi_init(SPI_PORT, 2000000); // 2 MHz (ajuste conforme necessário)
    spi_set_format(SPI_PORT, 8, SPI_CPOL_0, SPI_CPHA_0, SPI_MSB_FIRST);
    gpio_set_function(PIN_SCK, GPIO_FUNC_SPI);
    gpio_set_function(PIN_TX, GPIO_FUNC_SPI);
    gpio_set_function(PIN_CS, GPIO_FUNC_SPI);
    gpio_set_dir(PIN_CS, GPIO_OUT);
    gpio_put(PIN_CS, 1); // Deseleciona o SPI

    // Configura o canal DMA para o OLED
    dma_channel_config c = dma_channel_get_default_config(OLED_DMA_CHANNEL);
    channel_config_set_transfer_data_size(&c, DMA_SIZE_8);
    channel_config_set_read_increment(&c, true);  // Incrementa o endereço de leitura (dados na memória)
    channel_config_set_write_increment(&c, false); // Mantém o endereço de escrita fixo (registrador SPI)
    channel_config_set_dreq(&c, spi_get_dreq(SPI_PORT, true)); // Sincroniza com o SPI TX FIFO

    // Configura o canal DMA
    dma_channel_configure(
        OLED_DMA_CHANNEL,     // Canal DMA
        &c,                   // Configuração
        &spi_get_hw(SPI_PORT)->dr, // Endereço de escrita (registrador SPI)
        NULL,                 // Endereço de leitura será configurado durante a transferência
        0,                    // Número de transferências (será configurado na transferência)
        false                 // Não iniciar ainda
    );
}


void ssd1306_hard_reset(void) {
    gpio_put(SSD1306_RST_PIN, 0);
    busy_wait_us(SSD1306_LATENCY);
    gpio_put(SSD1306_RST_PIN, 1);
    busy_wait_us(SSD1306_LATENCY);
}

void ssd1306_write_command(uint8_t command) {
    gpio_put(SSD1306_DATA_CMD_SEL, 0);
    spi_cs_select();
    spi_write_blocking(SPI_PORT, &command, 1);
    // spi_cs_deselect();
    busy_wait_us_32(4);
}

void ssd1306_write_data(uint8_t data) {
    gpio_put(SSD1306_DATA_CMD_SEL, 1);
    spi_cs_select();
    spi_write_blocking(SPI_PORT, &data, 1);
    // spi_cs_deselect();
    busy_wait_us_32(4);
}

void ssd1306_put_page(uint8_t *data, uint8_t page, uint8_t column, uint8_t width) {
    ssd1306_set_page_address(page);
    ssd1306_set_column_address(column);

    // Indica que estamos enviando dados (não comandos)
    gpio_put(SSD1306_DATA_CMD_SEL, 1);

    // Seleciona o SPI (CS low)
    spi_cs_select();

    // Configura o endereço de leitura e a contagem de transferências
    dma_channel_set_read_addr(OLED_DMA_CHANNEL, data, false);
    dma_channel_set_trans_count(OLED_DMA_CHANNEL, width, false);

    // Inicia a transferência DMA
    dma_channel_start(OLED_DMA_CHANNEL);

    // Aguarda a transferência concluir
    dma_channel_wait_for_finish_blocking(OLED_DMA_CHANNEL);

    // Deseleciona o SPI (CS high)
    spi_cs_deselect();
}


void ssd1306_init(void) {
    ssd1306_interface_init();
    ssd1306_hard_reset();
    // 1/32 Duty (0x0F~0x3F)
    ssd1306_write_command(SSD1306_CMD_SET_MULTIPLEX_RATIO);
    ssd1306_write_command(0x1F);

    // Shift Mapping RAM Counter (0x00~0x3F)
    ssd1306_write_command(SSD1306_CMD_SET_DISPLAY_OFFSET);
    ssd1306_write_command(0x00);

    // Set Mapping RAM Display Start Line (0x00~0x3F)
    ssd1306_write_command(SSD1306_CMD_SET_DISPLAY_START_LINE(0x40));

    // Set Mapping RAM Display Start Line (0x00~0x3F)
    ssd1306_write_command(SSD1306_CMD_SET_MEMORY_ADDRESSING_MODE);
    ssd1306_write_command(0);

    // Set Column Address 0 Mapped to SEG0
    ssd1306_write_command(SSD1306_CMD_SET_SEGMENT_RE_MAP_COL127_SEG0);

    // Set COM/Row Scan Scan from COM63 to 0
    ssd1306_write_command(0xC8);

    // Set COM Pins hardware configuration
    ssd1306_write_command(SSD1306_CMD_SET_COM_PINS);
    ssd1306_write_command(0x02);

    ssd1306_set_contrast(0x8F);

    // Disable Entire display On
    ssd1306_write_command(SSD1306_CMD_ENTIRE_DISPLAY_AND_GDDRAM_ON);

    ssd1306_display_invert_disable();

    // Set Display Clock Divide Ratio / Oscillator Frequency (Default => 0x80)
    ssd1306_write_command(SSD1306_CMD_SET_DISPLAY_CLOCK_DIVIDE_RATIO);
    ssd1306_write_command(0x80);

    // Enable charge pump regulator
    ssd1306_write_command(SSD1306_CMD_SET_CHARGE_PUMP_SETTING);
    ssd1306_write_command(0x14);

    // Set VCOMH Deselect Level
    ssd1306_write_command(SSD1306_CMD_SET_VCOMH_DESELECT_LEVEL);
    ssd1306_write_command(0x40); // Default => 0x20 (0.77*VCC)

    // Set Pre-Charge as 15 Clocks & Discharge as 1 Clock
    ssd1306_write_command(SSD1306_CMD_SET_PRE_CHARGE_PERIOD);
    ssd1306_write_command(0xF1);

    ssd1306_display_on();
}
