#include <stdio.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/uart.h"
#include "driver/gpio.h"
#include "esp_log.h"
#include "driver/adc.h"
#include "esp_adc_cal.h"
#include <math.h>

/* GPIO Definitions */
#define LED_RED 5
#define LED_GREEN 18
#define LED_BLUE 19
#define BUTTON 16
#define TILT_SENSOR_GPIO 21

/* Variables for Thermistor and Temperature Conversion */
#define T0 25
#define B 3435
#define R0 10000

#define DEFAULT_VREF    1100        //Use adc2_vref_to_gpio() to obtain a better estimate
#define NO_OF_SAMPLES   10          //Multisampling

/* UART VARIABLES */
#define UART_NUM UART_NUM_0
#define BUF_SIZE 1024
#define ECHO_TASK_STACK_SIZE 4096

/* ADC variables */
static esp_adc_cal_characteristics_t *adc_chars;
static const adc_channel_t channel_temp = ADC_CHANNEL_6;        // GPIO34 if ADC1, GPIO14 if ADC2
static const adc_channel_t battery_channel = ADC_CHANNEL_3;     // GPIO39
static const adc_channel_t channel_photo = ADC_CHANNEL_0;        // GPIO36
static const adc_atten_t atten = ADC_ATTEN_DB_11;
static const adc_unit_t unit = ADC_UNIT_1;

/* Global Variables for data display*/
float Temperature = 0.0;
float final_voltage = 0.0;
uint32_t lux;
int process_state = 0; // 0 is ready, 1 is reading, 2 is done reading
bool button_pressed = false; // Flag to indicate button press
uint32_t tilt_button_pressed = 0; // 0 is vertical, 1 is horizontal
uint32_t time_elapsed = 0;
char tilt[15] = "Vertical";

static void blink_led(int LED) {
    static bool led_state = false; // Maintains the LED state between function calls
    led_state = !led_state; // Toggle LED state
    gpio_set_level(LED, led_state); // Set the GPIO level
}

static void IRAM_ATTR button_isr_handler(void* arg) {
    // Handle button press event
    button_pressed = true; // Set flag (you can remove this if the flag is not needed elsewhere)
    process_state = 0;     // Reset process state to "Ready"
    time_elapsed = 0;      // Reset time
}

static void IRAM_ATTR tilt_isr_handler(void* arg) {
    tilt_button_pressed = !tilt_button_pressed;  // Toggle the tilt state
    if (tilt_button_pressed == 0) {
        strcpy(tilt, "Vertical");
    } else {
        strcpy(tilt, "Horizontal");
    }
}

static void configure_ADC_photo() {
    //Configure ADC
    if (unit == ADC_UNIT_1) {
        adc1_config_width(ADC_WIDTH_BIT_12);
        adc1_config_channel_atten(channel_photo, atten);
    } else {
        adc2_config_channel_atten((adc2_channel_t)channel_photo, atten);
    }

    //Characterize ADC
    adc_chars = calloc(1, sizeof(esp_adc_cal_characteristics_t));
    esp_adc_cal_value_t val_type = esp_adc_cal_characterize(unit, atten, ADC_WIDTH_BIT_12, DEFAULT_VREF, adc_chars);
}

static void configure_ADC_temp(){
    //Configure ADC
    if (unit == ADC_UNIT_1) {
        adc1_config_width(ADC_WIDTH_BIT_12);
        adc1_config_channel_atten(channel_temp, atten);
    } else {
        adc2_config_channel_atten((adc2_channel_t)channel_temp, atten);
    }

    //Characterize ADC
    adc_chars = calloc(1, sizeof(esp_adc_cal_characteristics_t));
    esp_adc_cal_value_t val_type = esp_adc_cal_characterize(unit, atten, ADC_WIDTH_BIT_12, DEFAULT_VREF, adc_chars);
    
}

static void configure_ADC_battery(){
    //Configure ADC
    if (unit == ADC_UNIT_1) {
        adc1_config_width(ADC_WIDTH_BIT_12);
        adc1_config_channel_atten(battery_channel, atten);
    } else {
        adc2_config_channel_atten((adc2_channel_t) battery_channel, atten);
    }

    //Characterize ADC
    adc_chars = calloc(1, sizeof(esp_adc_cal_characteristics_t));
    esp_adc_cal_value_t val_type = esp_adc_cal_characterize(unit, atten, ADC_WIDTH_BIT_12, DEFAULT_VREF, adc_chars);
    
}

static void configure_GPIO() {
    // Configure LEDs
    gpio_reset_pin(LED_RED);
    gpio_reset_pin(LED_GREEN);
    gpio_reset_pin(LED_BLUE);

    /* Set the GPIO as a push/pull output */
    gpio_set_direction(LED_RED, GPIO_MODE_OUTPUT);
    gpio_set_direction(LED_GREEN, GPIO_MODE_OUTPUT);
    gpio_set_direction(LED_BLUE, GPIO_MODE_OUTPUT);

    // Configure button
    gpio_reset_pin(BUTTON);
    gpio_set_direction(BUTTON, GPIO_MODE_INPUT);
    gpio_set_pull_mode(BUTTON, GPIO_PULLUP_ONLY);

    // Configure tilt button
    gpio_reset_pin(TILT_SENSOR_GPIO);
    gpio_set_direction(TILT_SENSOR_GPIO, GPIO_MODE_INPUT);
    gpio_set_pull_mode(TILT_SENSOR_GPIO, GPIO_PULLUP_ONLY);

    // Configure the interrupt on the button pin (falling edge, active-low)
    gpio_set_intr_type(BUTTON, GPIO_INTR_NEGEDGE);
    gpio_install_isr_service(0);
    gpio_isr_handler_add(BUTTON, button_isr_handler, NULL);

    // Configure the interrupt button for tilt
    gpio_set_intr_type(TILT_SENSOR_GPIO, GPIO_INTR_NEGEDGE);
    gpio_isr_handler_add(TILT_SENSOR_GPIO, tilt_isr_handler, NULL);
}

static void report_temperature() {
    //Continuously sample ADC1
    while (1) {
        uint32_t adc_reading = 0;
        //Multisampling
        for (int i = 0; i < NO_OF_SAMPLES; i++) {
            if (unit == ADC_UNIT_1) {
                adc_reading += adc1_get_raw((adc1_channel_t)channel_temp);
            } else {
                int raw;
                adc2_get_raw((adc2_channel_t)channel_temp, ADC_WIDTH_BIT_12, &raw);
                adc_reading += raw;
            }
            vTaskDelay(pdMS_TO_TICKS(100));
        }
        adc_reading /= NO_OF_SAMPLES;

        /* Resistance Calculation */
        uint32_t voltage = esp_adc_cal_raw_to_voltage(adc_reading, adc_chars);  // get voltage reading
        float Resistance = (3300 - voltage) * 1000.0 / voltage;                 // using voltage divider equation solve for thermistor Resistance

        /* Temperature Conversion */
        Temperature = Resistance / R0;
        Temperature = log(Temperature);
        Temperature = (1.0 / B) * Temperature;
        Temperature = (1.0 / (T0 + 273.15)) + Temperature;
        Temperature = (1.0 / Temperature) - 273.15;
    }
}

static void report_lux() {
    while (1) {
        uint32_t adc_reading = 0;
        //Multisampling
        for (int i = 0; i < 10; i++) {
            if (unit == ADC_UNIT_1) {
                adc_reading += adc1_get_raw((adc1_channel_t)channel_photo);
            } else {
                int raw;
                adc2_get_raw((adc2_channel_t)channel_photo, ADC_WIDTH_BIT_12, &raw);
                adc_reading += raw;
            }
            vTaskDelay(pdMS_TO_TICKS(100));;
        }
        
        adc_reading /= 10;

        //Convert adc_reading to voltage in mV
        uint32_t voltage = esp_adc_cal_raw_to_voltage(adc_reading, adc_chars);

        lux = 0.259 * voltage - 36.8; // Convert voltage to Lux

    }
}

static void report_battery() {
    //Continuously sample ADC1
    while (1) {
        uint32_t adc_reading = 0;
        //Multisampling
        for (int i = 0; i < NO_OF_SAMPLES; i++) {
            if (unit == ADC_UNIT_1) {
                adc_reading += adc1_get_raw((adc1_channel_t)battery_channel);
            } else {
                int raw;
                adc2_get_raw((adc2_channel_t)battery_channel, ADC_WIDTH_BIT_12, &raw);
                adc_reading += raw;
            }
            
            vTaskDelay(pdMS_TO_TICKS(100));
        }

        adc_reading /= NO_OF_SAMPLES;

        //Convert adc_reading to voltage
        uint32_t voltage = esp_adc_cal_raw_to_voltage(adc_reading, adc_chars);
        final_voltage = voltage * 2.0 / 1000.0;
    }
}

static void led_status() {
    int led = LED_GREEN; // Default to Green LED

    while (1) {
        if (process_state == 0) {
            // Ready state: If the light level is in the light range, it's ready (Green)
            if (lux >= 200 && lux <= 500) {
                led = LED_GREEN; // Set Green LED for "ready to swallow"
            } 
            // If the lux drops to dark range, transition to sensing state (Blue)
            else if (lux >= 0 && lux <= 20) {
                process_state = 1;  // Transition to sensing state
                led = LED_BLUE;     // Set Blue LED for "sensing"
            }
        } 
        else if (process_state == 1) {
            // Sensing state: Keep Blue LED until lux rises to light range
            if (lux >= 200 && lux <= 500) {
                process_state = 2;  // Transition to done state (light detected again)
                led = LED_RED;      // Set Red LED for "done sensing"
            } else {
                led = LED_BLUE;     // Keep Blue while still in the dark range
            }
        } 
        else if (process_state == 2) {
            // Done state: The process is complete (Red LED remains on)
            led = LED_RED;
        }

        // Force LED update immediately after state change
        gpio_set_level(LED_RED, led == LED_RED);
        gpio_set_level(LED_GREEN, led == LED_GREEN);
        gpio_set_level(LED_BLUE, led == LED_BLUE);

        blink_led(led); 
        vTaskDelay(pdMS_TO_TICKS(2000));
    }
}

static void display_info() {
    uart_config_t uart_config = {
        .baud_rate = 115200,
        .data_bits = UART_DATA_8_BITS,
        .parity = UART_PARITY_DISABLE,
        .stop_bits = UART_STOP_BITS_1,
        .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
        .source_clk = UART_SCLK_DEFAULT,
    };

    ESP_ERROR_CHECK(uart_driver_install(UART_NUM, BUF_SIZE * 2, 0, 0, NULL, 0));
    ESP_ERROR_CHECK(uart_param_config(UART_NUM, &uart_config));
    ESP_ERROR_CHECK(uart_set_pin(UART_NUM, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE));

    while (1) {
        char buffer[100];
        sprintf(buffer, "Time: %ld s, Temp: %.2f C, Light: %ld Lux, Battery: %.1f V, Tilt: %s\n", time_elapsed, Temperature, lux, final_voltage, tilt);
        uart_write_bytes(UART_NUM, buffer, strlen(buffer));
        vTaskDelay(pdMS_TO_TICKS(2000)); // report information every 2 seconds
    }
}

static void time() {
    while (1) {
        time_elapsed++;
        vTaskDelay(pdMS_TO_TICKS(1000)); // Increment time every one second
    }
}

static void init() {
    // configure all ADC channels and necessary GPIO pins
    configure_ADC_temp();
    configure_ADC_battery();
    configure_ADC_photo();
    configure_GPIO();
}

void app_main(void) {
    init();

    // create tasks for all necessary parallel procedures
    xTaskCreate(report_temperature, "report_temperature", 1024*2, NULL, 1, NULL);
    xTaskCreate(report_battery, "report_battery", 1024*2, NULL, 1, NULL);
    xTaskCreate(report_lux, "report_lux", 1024*2, NULL, 1, NULL);
    xTaskCreate(led_status, "led_status", 1024*2, NULL, 1, NULL);
    xTaskCreate(time, "time_task", 1024*2, NULL, 1, NULL);
    xTaskCreate(display_info, "display_info", 1024*2, NULL, 1, NULL);
}
