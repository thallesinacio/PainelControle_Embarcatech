#include "pico/stdlib.h"
#include "FreeRTOS.h"
#include "task.h"
#include "semphr.h"
#include <stdio.h>
#include "hardware/i2c.h"
#include "lib/ssd1306.h"
#include "lib/font.h"
#include <string.h>

#define MAXUSUARIOS 10

#define BOTAO_A 5 // Gera evento
#define BOTAO_B 6 // BOOTSEL
#define JOY_BUT 22 // Reseta
#define BUZZER_A 21 // Buzzer
#define LED_VERMELHO 13
#define LED_VERDE 11
#define LED_AZUL 12

// Display
#define I2C_PORT i2c1
#define I2C_SDA 14
#define I2C_SCL 15
#define endereco 0x3C
static ssd1306_t ssd;

SemaphoreHandle_t xContagemEntradas;
SemaphoreHandle_t xContagemSaidas;
SemaphoreHandle_t xBinarioReset;
SemaphoreHandle_t xMutexDisplay;
SemaphoreHandle_t xBuzzLotadoSem;
SemaphoreHandle_t xBuzzResetSem;

int eventosProcessados = 0;
char status[30];

// Variável de debouncing
static volatile uint32_t last_time = 0;

static void gpio_irq_handler(uint gpio, uint32_t events) {
    uint32_t current_time = to_us_since_boot(get_absolute_time());
    if (current_time - last_time > 600000){

        last_time = current_time; 
        BaseType_t xHigherPriorityTaskWoken = pdFALSE;

            if(gpio == BOTAO_A) { // Aumenta número de usuários ativos
                xSemaphoreGiveFromISR(xContagemEntradas, &xHigherPriorityTaskWoken); // Dei permissão
                    if(eventosProcessados == MAXUSUARIOS){
                        xSemaphoreGiveFromISR(xBuzzLotadoSem, &xHigherPriorityTaskWoken);
                    }
            } 
            else if (gpio == BOTAO_B) { // Reduz o número de usuários ativos
                xSemaphoreGiveFromISR(xContagemSaidas, &xHigherPriorityTaskWoken); // Dei permissão
            } 
            else if(gpio == JOY_BUT) {  //Nenhum contexto de tarefa foi despertado
                xSemaphoreGiveFromISR(xBinarioReset, &xHigherPriorityTaskWoken);    //Libera o semáforo
                xSemaphoreGiveFromISR(xBuzzResetSem, &xHigherPriorityTaskWoken);
            }

        portYIELD_FROM_ISR(xHigherPriorityTaskWoken);
    }
}


void vTaskEntrada () { // Controlada pelo semáforo de contagem
    char buffer[5], buffer2[5];

    while(true){
        if (xSemaphoreTake(xContagemEntradas, portMAX_DELAY) == pdTRUE) {
                if(eventosProcessados < MAXUSUARIOS){
                    eventosProcessados++;
                    if(eventosProcessados == 0) {
                        gpio_put(LED_AZUL, false);
                        gpio_put(LED_VERDE, false);
                        gpio_put(LED_VERMELHO, false);
                        gpio_put(LED_AZUL, true);
                    } else if(eventosProcessados > 0 && eventosProcessados <= MAXUSUARIOS - 2) {
                        gpio_put(LED_AZUL, false);
                        gpio_put(LED_VERDE, false);
                        gpio_put(LED_VERMELHO, false);
                        gpio_put(LED_VERDE, true);
                    } else if(eventosProcessados == MAXUSUARIOS - 1) {
                        gpio_put(LED_AZUL, false);
                        gpio_put(LED_VERDE, false);
                        gpio_put(LED_VERMELHO, false);
                        gpio_put(LED_VERDE, true);
                        gpio_put(LED_VERMELHO, true);
                    } else if(eventosProcessados == MAXUSUARIOS) {
                        gpio_put(LED_AZUL, false);
                        gpio_put(LED_VERDE, false);
                        gpio_put(LED_VERMELHO, false);
                        gpio_put(LED_VERMELHO, true);
                    }
                } else {
                    continue;
                }
        }

        sprintf(buffer, "%d", eventosProcessados);

        if (xSemaphoreTake(xMutexDisplay, portMAX_DELAY)==pdTRUE) { // Mutex protegendo acesso ao display
            sprintf(buffer, "%d", eventosProcessados);
            ssd1306_divide_em_4_linhas(&ssd);
            int percentual = (eventosProcessados * 100) / MAXUSUARIOS;
            sprintf(buffer2, "%d%%", percentual);
                if (eventosProcessados == MAXUSUARIOS)
                    strcpy(status, "Status: Lotado");
                else if (eventosProcessados == MAXUSUARIOS - 1)
                    strcpy(status, "Status: 1 vaga");
                else
                    strcpy(status, "Status: livre");

            ssd1306_draw_string_escala(&ssd, status, 4, 36, 0.9);
            ssd1306_draw_string_escala(&ssd, "Percentual: ", 4,  20, 0.9);
            ssd1306_draw_string_escala(&ssd, buffer2, 95, 20, 0.9);
            ssd1306_draw_string_escala(&ssd, "Alunos em sala: ", 4,  4, 0.9);
            ssd1306_draw_string_escala(&ssd, buffer, 112,  4, 0.9);
            ssd1306_draw_string_escala(&ssd, "Aguardando...", 4, 52, 0.9);
            ssd1306_send_data(&ssd);
            xSemaphoreGive(xMutexDisplay);
        }
        vTaskDelay(pdMS_TO_TICKS(100));
    }
}

void vTaskSaida () { // Controlada pelo semáforo de contagem

    char buffer[5], buffer2[5];

    while(true){
        if (xSemaphoreTake(xContagemSaidas, portMAX_DELAY) == pdTRUE) {
                if(eventosProcessados > 0){
                    eventosProcessados--;
                    if(eventosProcessados == 0) {
                        gpio_put(LED_AZUL, false);
                        gpio_put(LED_VERDE, false);
                        gpio_put(LED_VERMELHO, false);
                        gpio_put(LED_AZUL, true);
                    } else if(eventosProcessados > 0 && eventosProcessados <= MAXUSUARIOS - 2) {
                        gpio_put(LED_AZUL, false);
                        gpio_put(LED_VERDE, false);
                        gpio_put(LED_VERMELHO, false);
                        gpio_put(LED_VERDE, true);
                    } else if(eventosProcessados == MAXUSUARIOS - 1) {
                        gpio_put(LED_AZUL, false);
                        gpio_put(LED_VERDE, false);
                        gpio_put(LED_VERMELHO, false);
                        gpio_put(LED_VERDE, true);
                        gpio_put(LED_VERMELHO, true);
                    } else if(eventosProcessados == MAXUSUARIOS) {
                        gpio_put(LED_AZUL, false);
                        gpio_put(LED_VERDE, false);
                        gpio_put(LED_VERMELHO, false);
                        gpio_put(LED_VERMELHO, true);
                    }
                } else {
                    continue;
                }
        }

        sprintf(buffer, "%d", eventosProcessados);

        if (xSemaphoreTake(xMutexDisplay, portMAX_DELAY)==pdTRUE) { // Mutex protegendo acesso ao display
            sprintf(buffer, "%d", eventosProcessados);
            int percentual = (eventosProcessados * 100) / MAXUSUARIOS;
            sprintf(buffer2, "%d%%", percentual);
            if (eventosProcessados == MAXUSUARIOS)
                    strcpy(status, "Status: Lotado");
                else if (eventosProcessados == MAXUSUARIOS - 1)
                    strcpy(status, "Status: 1 vaga");
                else
                    strcpy(status, "Status: livre");
            ssd1306_draw_string_escala(&ssd, status, 4, 36, 0.9);
            ssd1306_draw_string_escala(&ssd, "Percentual: ", 4,  20, 0.9);
            ssd1306_draw_string_escala(&ssd, buffer2, 95, 20, 0.9);
            ssd1306_divide_em_4_linhas(&ssd);
            ssd1306_draw_string_escala(&ssd, "Alunos em sala: ", 4,  4, 0.9);
            ssd1306_draw_string_escala(&ssd, buffer, 112,  4, 0.9);
            ssd1306_draw_string_escala(&ssd, "Aguardando...", 4, 52, 0.9);
            ssd1306_send_data(&ssd);
            xSemaphoreGive(xMutexDisplay);
        }
        vTaskDelay(pdMS_TO_TICKS(100));
    }
}
void inicializa_saida (int pino) {

    gpio_init(pino);
    gpio_set_dir(pino,GPIO_OUT);

}

void beep(uint32_t duration_ms) {
    gpio_put(BUZZER_A, 1);  // Liga o buzzer
    vTaskDelay(pdMS_TO_TICKS(duration_ms));
    gpio_put(BUZZER_A, 0);  // Desliga o buzzer
}

// Função para beep duplo
void double_beep(uint32_t beep_duration_ms, uint32_t gap_duration_ms) {
    beep(beep_duration_ms);       // Primeiro beep
    vTaskDelay(pdMS_TO_TICKS(gap_duration_ms));  // Intervalo
    beep(beep_duration_ms);       // Segundo beep
}

void buzz(uint8_t BUZZER_PIN, uint16_t freq, uint16_t duration) {
    int period = 1000000 / freq;
    int pulse = period / 2;
    int cycles = freq * duration / 1000;
    for (int j = 0; j < cycles; j++) {
        gpio_put(BUZZER_PIN, 1);
        sleep_us(pulse);
        gpio_put(BUZZER_PIN, 0);
        sleep_us(pulse);
    }
}

void vTaskBuzzLotado(void *pvParameters) {
    while (true) {
        // Espera indefinidamente pelo sinal para fazer o buzz
        if (xSemaphoreTake(xBuzzLotadoSem, portMAX_DELAY) == pdTRUE) {
            buzz(BUZZER_A, 750, 150);
        }

        vTaskDelay(pdMS_TO_TICKS(50));
    }
}

void vTaskBuzzReset(void *pvParameters) {
    while (true) {
        // Espera indefinidamente pelo sinal para fazer o buzz
        if (xSemaphoreTake(xBuzzResetSem, portMAX_DELAY) == pdTRUE) {
            buzz(BUZZER_A, 750, 150);
            vTaskDelay(pdMS_TO_TICKS(200));
            buzz(BUZZER_A, 750, 150);
            vTaskDelay(pdMS_TO_TICKS(200));
        }

        vTaskDelay(pdMS_TO_TICKS(50));
    }
}

void vTaskReset () { // Controlada pelo semáforo binário

    while(true) {

        if(xSemaphoreTake(xBinarioReset, portMAX_DELAY) == pdTRUE) {
            eventosProcessados = 0;

                // Atualiza display com zero
                if (xSemaphoreTake(xMutexDisplay, portMAX_DELAY) == pdTRUE) {
                    char buffer[5], buffer2[5];
                    sprintf(buffer, "%d", eventosProcessados);
                    int percentual = (eventosProcessados * 100) / MAXUSUARIOS;
                    sprintf(buffer2, "%d%%", percentual);
                    ssd1306_divide_em_4_linhas(&ssd);
                        if (eventosProcessados == MAXUSUARIOS)
                            strcpy(status, "Status: Lotado");
                        else if (eventosProcessados == MAXUSUARIOS - 1)
                            strcpy(status, "Status: 1 vaga");
                        else
                            strcpy(status, "Status: livre");
                    ssd1306_draw_string_escala(&ssd, status, 4, 36, 0.9);
                    ssd1306_draw_string_escala(&ssd, "Percentual: ", 4,  20, 0.9);
                    ssd1306_draw_string_escala(&ssd, buffer2, 95, 20, 0.9);
                    ssd1306_draw_string_escala(&ssd, "Alunos em sala: ", 4,  4, 0.9);
                    ssd1306_draw_string_escala(&ssd, buffer, 112,  4, 0.9);
                    ssd1306_draw_string_escala(&ssd, "Aguardando...", 4, 52, 0.9);
                    ssd1306_send_data(&ssd);
                    gpio_put(LED_AZUL, false);
                    gpio_put(LED_VERDE, false);
                    gpio_put(LED_VERMELHO, false);
                    gpio_put(LED_AZUL, true);
                    xSemaphoreGive(xMutexDisplay);
                }
            double_beep(500,200);
        }
    }
}


void inicializa_botao (int gpio) {
    gpio_init(gpio);
    gpio_set_dir(gpio,GPIO_IN);
    gpio_pull_up(gpio);
}

int main()
{
    stdio_init_all();

    i2c_init(I2C_PORT, 400 * 1000);

    gpio_set_function(I2C_SDA, GPIO_FUNC_I2C);
    gpio_set_function(I2C_SCL, GPIO_FUNC_I2C);
    gpio_pull_up(I2C_SDA);
    gpio_pull_up(I2C_SCL);

    ssd1306_init(&ssd, WIDTH, HEIGHT, false, endereco, I2C_PORT);
    ssd1306_config(&ssd);
    ssd1306_fill(&ssd, false);
    ssd1306_send_data(&ssd);

    xContagemEntradas = xSemaphoreCreateCounting(10, 0);
    xContagemSaidas = xSemaphoreCreateCounting(10, 0);
    xBinarioReset = xSemaphoreCreateBinary();
    xMutexDisplay = xSemaphoreCreateMutex();
    xBuzzLotadoSem = xSemaphoreCreateBinary();
    xBuzzResetSem = xSemaphoreCreateBinary();

    inicializa_botao(BOTAO_A);
    inicializa_botao(BOTAO_B);
    inicializa_botao(JOY_BUT);
    inicializa_saida(LED_VERMELHO);
    inicializa_saida(LED_VERDE);
    inicializa_saida(LED_AZUL);
    inicializa_saida(BUZZER_A);
    
    if (xMutexDisplay != NULL && xSemaphoreTake(xMutexDisplay, pdMS_TO_TICKS(100)) == pdTRUE) {
        char buffer[5], buffer2[5];
        int percentual = (eventosProcessados * 100) / MAXUSUARIOS;
        sprintf(buffer, "%d", eventosProcessados);
        sprintf(buffer2, "%d%%", eventosProcessados);
        ssd1306_divide_em_4_linhas(&ssd);
            if (eventosProcessados == MAXUSUARIOS)
                strcpy(status, "Status: Lotado");
            else if (eventosProcessados == MAXUSUARIOS - 1)
                strcpy(status, "Status: 1 vaga");
            else
                strcpy(status, "Status: livre");
        ssd1306_draw_string_escala(&ssd, status, 4, 36, 0.9);
        ssd1306_draw_string_escala(&ssd, buffer, 112, 4, 0.9);
        ssd1306_draw_string_escala(&ssd, buffer2, 95, 20, 0.9);
        ssd1306_draw_string_escala(&ssd, "Alunos em sala: ", 4,  4, 0.9);
        ssd1306_draw_string_escala(&ssd, "Percentual: ", 4,  20, 0.9);
        ssd1306_draw_string_escala(&ssd, "Aguardando...", 4, 52, 0.9);
        ssd1306_send_data(&ssd);
        gpio_put(LED_AZUL, false);
        gpio_put(LED_VERDE, false);
        gpio_put(LED_VERMELHO, false);
        gpio_put(LED_AZUL, true);
        xSemaphoreGive(xMutexDisplay);
    }

    gpio_set_irq_enabled_with_callback(BOTAO_A, GPIO_IRQ_EDGE_FALL, true, &gpio_irq_handler);
    gpio_set_irq_enabled_with_callback(BOTAO_B, GPIO_IRQ_EDGE_FALL, true, &gpio_irq_handler);
    gpio_set_irq_enabled_with_callback(JOY_BUT, GPIO_IRQ_EDGE_FALL, true, &gpio_irq_handler);

    xTaskCreate(vTaskEntrada, "Entrada", 256, NULL, 1, NULL);
    xTaskCreate(vTaskSaida, "Saida", 256, NULL, 1, NULL);
    xTaskCreate(vTaskReset, "Reset", 256, NULL, 1, NULL);
    xTaskCreate(vTaskBuzzLotado, "Task do Buzzer", configMINIMAL_STACK_SIZE + 128, NULL, 3, NULL);
    xTaskCreate(vTaskBuzzReset, "Task do Buzzer", configMINIMAL_STACK_SIZE + 128, NULL, 3, NULL);

    vTaskStartScheduler();
    panic_unsupported();
}
