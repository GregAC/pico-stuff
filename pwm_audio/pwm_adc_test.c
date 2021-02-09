#include <stdio.h>
#include "pico/stdlib.h"
#include "hardware/dma.h"
#include "hardware/irq.h"
#include "hardware/pwm.h"
#include "hardware/sync.h"
#include "hardware/adc.h"

#define AUDIO_PIN 2
#define ADC_CHANNEL 2

#define NUM_SAMPLES 25000

uint16_t sample_buffer[NUM_SAMPLES];

int main(void) {
    stdio_init_all();
    sleep_ms(5000);
    printf("Here we go!\n");
    sleep_ms(1000);
    gpio_set_function(AUDIO_PIN, GPIO_FUNC_PWM);

    int audio_pin_slice = pwm_gpio_to_slice_num(AUDIO_PIN);

    pwm_config config = pwm_get_default_config();
    pwm_config_set_clkdiv(&config, 1.f);
    pwm_config_set_wrap(&config, 254);
    pwm_init(audio_pin_slice, &config, true);

    pwm_set_gpio_level(AUDIO_PIN, 0);

    adc_gpio_init(26 + ADC_CHANNEL);
    adc_init();
    adc_select_input(ADC_CHANNEL);
    adc_fifo_setup(
        true,    // Write each completed conversion to the sample FIFO
        true,    // Enable DMA data request (DREQ)
        1,       // DREQ (and IRQ) asserted when at least 1 sample present
        true,    // Set sample error bit on error
        false   // Keep full 12 bits of each sample
    );

    // ADC clock is a fixed 48 MHz, a setting of 1919 gives one sample every 1920
    // clocks, resulting in 25 kSps.
    adc_set_clkdiv(1919);

    uint dma_adc_chan = dma_claim_unused_channel(true);
    dma_channel_config dma_adc_cfg = dma_channel_get_default_config(dma_adc_chan);
    channel_config_set_transfer_data_size(&dma_adc_cfg, DMA_SIZE_16);
    channel_config_set_read_increment(&dma_adc_cfg, false);
    channel_config_set_write_increment(&dma_adc_cfg, true);
    channel_config_set_dreq(&dma_adc_cfg, DREQ_ADC);

    // Setup DMA to read from ADC and write to sample_buffer, set it going
    // immediately
    dma_channel_configure(dma_adc_chan, &dma_adc_cfg,
        sample_buffer,
        &adc_hw->fifo,
        NUM_SAMPLES,
        true
    );

    // DMA runs on ADC DREQ so nothing happens til we start the ADC
    adc_run(true);

    int level = 0;
    bool up = true;

    printf("Starting capture\n");

    adc_run(true);

    for(int i = 0; i < 32; ++i) {
        int pwm_level = (level << 4);
        pwm_set_gpio_level(AUDIO_PIN, pwm_level);

        if (level == 0) {
            up = true;
        } else if (level == 15) {
            up = false;
        }

        if (up) {
            level++;
        } else {
            level--;
        }

        sleep_ms(30);
    }

    dma_channel_wait_for_finish_blocking(dma_adc_chan);

    printf("Capture done\n");

    for(int i = 0;i < NUM_SAMPLES; ++i) {
        printf("%d\n", sample_buffer[i]);
    }
}
