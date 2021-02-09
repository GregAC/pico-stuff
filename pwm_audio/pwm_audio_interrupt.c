#include <stdio.h>
#include "pico/stdlib.h"
#include "hardware/dma.h"
#include "hardware/irq.h"
#include "hardware/pwm.h"
#include "hardware/sync.h"
#include "hardware/adc.h"

#define AUDIO_PIN 2
#define ADC_CHANNEL 2

#include "rock.h"

int cur_sample = 0;

int led_pins[] = {
    16,
    19,
    22,
    15,
    12
};

int num_led_pins = 5;

void pwm_irh() {
    pwm_clear_irq(pwm_gpio_to_slice_num(AUDIO_PIN));
    pwm_set_gpio_level(AUDIO_PIN, audio_buffer[cur_sample >> 2]);

    uint8_t sample = audio_buffer[cur_sample >> 2];

    if (sample > 128) {
        gpio_put(led_pins[0], true);
    } else {
        gpio_put(led_pins[0], false);
    }

    if (sample > 137) {
        gpio_put(led_pins[1], true);
    } else {
        gpio_put(led_pins[1], false);
    }

    if (sample > 147) {
        gpio_put(led_pins[2], true);
    } else {
        gpio_put(led_pins[2], false);
    }

    if (sample > 157) {
        gpio_put(led_pins[3], true);
    } else {
        gpio_put(led_pins[3], false);
    }

    if (sample > 168) {
        gpio_put(led_pins[4], true);
    } else {
        gpio_put(led_pins[4], false);
    }

    if (cur_sample < (AUDIO_SAMPLES * 4) - 1) {
        ++cur_sample;
    } else {
        cur_sample = 0;
    }
}

int main(void) {
    stdio_init_all();

    for(int i = 0;i < num_led_pins; ++i) {
        gpio_init(led_pins[i]);
        gpio_set_dir(led_pins[i], GPIO_OUT);
        gpio_put(led_pins[i], true);
    }

    gpio_set_function(AUDIO_PIN, GPIO_FUNC_PWM);

    int audio_pin_slice = pwm_gpio_to_slice_num(AUDIO_PIN);

    // Setup PWM interrupt to fire when PWM cycle is complete
    pwm_clear_irq(audio_pin_slice);
    pwm_set_irq_enabled(audio_pin_slice, true);
    irq_set_exclusive_handler(PWM_IRQ_WRAP, pwm_irh);
    irq_set_enabled(PWM_IRQ_WRAP, true);

    // Setup PWM for audio output
    pwm_config config = pwm_get_default_config();
    pwm_config_set_clkdiv(&config, 5.5f);
    pwm_config_set_wrap(&config, 254);
    pwm_init(audio_pin_slice, &config, true);

    pwm_set_gpio_level(AUDIO_PIN, 0);

    while(1) {
        __wfi();
    }
}
