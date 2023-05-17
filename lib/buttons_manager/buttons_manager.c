#include "buttons_manager.h"
#include <stdlib.h>
#include <driver/gpio.h>
#include <esp_log.h>

ButtonInfo_t **buttons;
unsigned int buttons_count = 0;
unsigned int expected_buttons_count = 0;

void buttons_init(unsigned int buttons_number){
    buttons = calloc(buttons_number, sizeof(ButtonInfo_t*));
    if(buttons == NULL){
        ESP_LOGE(BUTTONS_MANAGER_TAG, "Failed allocating memory for %d buttons", buttons_number);
        return;
    }
    expected_buttons_count = buttons_number;
}

void buttons_add_button(
    unsigned char mode,
    unsigned char pull_up_en,
    unsigned char pull_down_en,
    unsigned char intr_type,
    unsigned short int id,
    int button_pin,
    int state,
    int max_states,
    long int last_pressed,
    ButtonCb cb){

    if(buttons_count == expected_buttons_count){
        ESP_LOGE(BUTTONS_MANAGER_TAG, "Maximum number of buttons already created: %d buttons", expected_buttons_count);
        return;
    }

    // Check if pin is already used by any button
    if(buttons_count != 0){
        for(unsigned int i = 0; i<buttons_count; i++){
            if(buttons[i]->button_pin == button_pin){
                ESP_LOGE(BUTTONS_MANAGER_TAG, "Pin %d already used", button_pin);
                return;
            }
        }
    }

    buttons[buttons_count] = calloc(1, sizeof(ButtonInfo_t));
    if(buttons[buttons_count] == NULL){
        ESP_LOGE(BUTTONS_MANAGER_TAG, "Failed allocating memory for button ID %d", id);
        return;
    }
    
    buttons[buttons_count]->mode = mode;
    buttons[buttons_count]->pull_up_en = pull_up_en;
    buttons[buttons_count]->pull_down_en = pull_down_en;
    buttons[buttons_count]->intr_type = intr_type;
    buttons[buttons_count]->id = id;
    buttons[buttons_count]->button_pin = button_pin;
    buttons[buttons_count]->state = state;
    buttons[buttons_count]->max_states = max_states;
    buttons[buttons_count]->last_pressed = 0;
    buttons[buttons_count]->cb = cb;

    buttons_count++;
}


void buttons_start(void){
    if(buttons_count != expected_buttons_count){
        ESP_LOGE(BUTTONS_MANAGER_TAG, "Expected %d buttons, but only %d buttons are created", expected_buttons_count, buttons_count);
    }

    for(unsigned int i = 0; i<buttons_count; i++){

        gpio_config_t config = {
            .mode = buttons[i]->mode,
            .pull_up_en = buttons[i]->pull_up_en,
            .pull_down_en = buttons[i]->pull_down_en,
            .intr_type = buttons[i]->intr_type,
            .pin_bit_mask = (1ULL << buttons[i]->button_pin)
        };

        gpio_config(&config);
    }

    gpio_install_isr_service(0);

    for(unsigned int i = 0; i<buttons_count; i++){

        gpio_isr_handler_add(buttons[i]->button_pin, buttons[i]->cb, (void*)buttons[i]);

    }
}