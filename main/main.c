/*
 * LED blink with FreeRTOS
 */
#include <FreeRTOS.h>
#include <task.h>
#include <semphr.h>
#include <queue.h>

#include "ssd1306.h"
#include "gfx.h"

#include "pico/stdlib.h"
#include <stdio.h>

const uint BTN_1_OLED = 28;
const uint BTN_2_OLED = 26;
const uint BTN_3_OLED = 27;

const uint LED_1_OLED = 20;
const uint LED_2_OLED = 21;
const uint LED_3_OLED = 22;

const int TRIG_PIN = 16;
const int ECHO_PIN = 18;

SemaphoreHandle_t xSemaphoreTrigger;
QueueHandle_t xQueueTime;
QueueHandle_t xQueueDistance;


void echo_callback(uint gpio, uint32_t events){
    if (events == GPIO_IRQ_EDGE_RISE) {
        uint32_t start_time = to_us_since_boot(get_absolute_time());
        xQueueSendFromISR(xQueueTime, &start_time, 0);
    } else if (events == GPIO_IRQ_EDGE_FALL) {
        uint32_t end_time = to_us_since_boot(get_absolute_time());
        xQueueSendFromISR(xQueueTime, &end_time, 0);  
    }
}

void trigger_task(void *p){
    gpio_init(TRIG_PIN);
    gpio_set_dir(TRIG_PIN, GPIO_OUT);

    while(1){
        gpio_put(TRIG_PIN, 1);
        vTaskDelay(pdMS_TO_TICKS(1));
        gpio_put(TRIG_PIN, 0);
        vTaskDelay(pdMS_TO_TICKS(100));
    }
}

void echo_task(void *p){
    gpio_init(ECHO_PIN);
    gpio_set_dir(ECHO_PIN, GPIO_IN);
    gpio_set_irq_enabled_with_callback(ECHO_PIN, GPIO_IRQ_EDGE_RISE | GPIO_IRQ_EDGE_FALL, true, &echo_callback);

    double distance;
    uint32_t start_time = 0;
    uint32_t end_time = 0;
    while(1){
        if(xQueueReceive(xQueueTime, &start_time, 100)){
            if(xQueueReceive(xQueueTime, &end_time, 100)){
                distance = (end_time - start_time)*0.034/2;
                xSemaphoreGive(xSemaphoreTrigger);
                xQueueSend(xQueueDistance, &distance, 0);
            }
        }
    }
}

void oled_task(void *p){
    printf("Inicializando Driver\n");
    ssd1306_init();
    printf("Inicializando GLX\n");
    ssd1306_t disp;
    gfx_init(&disp, 128, 32);
    gfx_clear_buffer(&disp);
    gfx_draw_string(&disp, 0, 0, 1, "LED 1 - ON");
    gfx_show(&disp);

    double distance = 0;
    while(true){
        if(xSemaphoreTake(xSemaphoreTrigger, 100)){
            if(xQueueReceive(xQueueDistance, &distance, 100)){
                gfx_clear_buffer(&disp);
                gfx_draw_string(&disp, 0, 0, 1, "Distancia: ");
                char str[15];
                sprintf(str, "%.2f cm", distance);
                gfx_draw_string(&disp, 0, 10, 1, str);
                gfx_draw_line(&disp, 0, 20, distance*2, 20);
                gfx_show(&disp);
                vTaskDelay(pdMS_TO_TICKS(100));
            }
        }
        else{
            gfx_clear_buffer(&disp);
            gfx_draw_string(&disp, 0, 0, 1, "Sensor Falhou");
            gfx_show(&disp);
            vTaskDelay(pdMS_TO_TICKS(100));
        }
    }
}


int main() {
    stdio_init_all();

    xSemaphoreTrigger = xSemaphoreCreateBinary();
    xQueueTime = xQueueCreate(10, sizeof(uint32_t));
    xQueueDistance = xQueueCreate(10, sizeof(double));

    xTaskCreatePinnedToCore(
        trigger_task,          // Função da task
        "Trigger Task",        // Nome da task
        4096,                  // Stack size em palavras (ajuste conforme necessário)
        NULL,                  // Parâmetros passados para a task
        1,                     // Prioridade da task
        NULL,                  // Handle da task
        0                      // CORE 0
    );
    xTaskCreatePinnedToCore(
        echo_task,             // Função da task
        "Echo Task",           // Nome da task
        4096,                  // Stack size em palavras (ajuste conforme necessário)
        NULL,                  // Parâmetros passados para a task
        1,                     // Prioridade da task
        NULL,                  // Handle da task
        0                      // CORE 0 (pode ser ajustado para outro core se necessário)
    );
    xTaskCreatePinnedToCore(
        oled_task,             // Função da task
        "OLED Task",           // Nome da task
        4096,                  // Stack size em palavras (ajuste conforme necessário)
        NULL,                  // Parâmetros passados para a task
        1,                     // Prioridade da task
        NULL,                  // Handle da task
        1                      // CORE 1
    );

    vTaskStartScheduler();

    while (true)
        ;
}