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

#include "audio_tone.h"

int pwm_dma_chan;

void dma_irh() {
    dma_hw->ch[pwm_dma_chan].al3_read_addr_trig = audio_buffer;
    dma_hw->ints0 = (1u << pwm_dma_chan);
}

int main(void) {
    stdio_init_all();

    gpio_set_function(AUDIO_PIN, GPIO_FUNC_PWM);

    int audio_pin_slice = pwm_gpio_to_slice_num(AUDIO_PIN);

    pwm_config config = pwm_get_default_config();
    pwm_config_set_clkdiv(&config, 22.1f);
    pwm_config_set_wrap(&config, 254);
    pwm_init(audio_pin_slice, &config, true);

    pwm_set_gpio_level(AUDIO_PIN, 0);

    // Setup DMA channel to drive the PWM
    pwm_dma_chan = dma_claim_unused_channel(true);

    dma_channel_config pwm_dma_chan_config = dma_channel_get_default_config(pwm_dma_chan);
    // Transfer 16 bits at once, increment read address to go through sample
    // buffer, always write to the same address (PWM slice CC register).
    channel_config_set_transfer_data_size(&pwm_dma_chan_config, DMA_SIZE_16);
    channel_config_set_read_increment(&pwm_dma_chan_config, true);
    channel_config_set_write_increment(&pwm_dma_chan_config, false);
    // Transfer on PWM cycle end
    channel_config_set_dreq(&pwm_dma_chan_config, DREQ_PWM_WRAP0 + audio_pin_slice);

    // Setup the channel and set it going
    dma_channel_configure(
        pwm_dma_chan,
        &pwm_dma_chan_config,
        &pwm_hw->slice[audio_pin_slice].cc, // Write to PWM counter compare
        audio_buffer, // Read values from audio buffer
        AUDIO_SAMPLES,
        true // Start immediately.
    );

    // Setup interrupt handler to fire when PWM DMA channel has gone through the
    // whole audio buffer
    dma_channel_set_irq0_enabled(pwm_dma_chan, true);
    irq_set_exclusive_handler(DMA_IRQ_0, dma_irh);
    irq_set_enabled(DMA_IRQ_0, true);

    // Setup ADC to measure audio output and DMA to stream ADC samples to memory
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

    // DMA copies from ADC to sample_buffer, using ADC DREQ to control transfers
    uint dma_adc_chan = dma_claim_unused_channel(true);
    dma_channel_config dma_adc_cfg = dma_channel_get_default_config(dma_adc_chan);
    channel_config_set_transfer_data_size(&dma_adc_cfg, DMA_SIZE_16);
    channel_config_set_read_increment(&dma_adc_cfg, false);
    channel_config_set_write_increment(&dma_adc_cfg, true);
    channel_config_set_dreq(&dma_adc_cfg, DREQ_ADC);

    dma_channel_configure(dma_adc_chan, &dma_adc_cfg,
        sample_buffer,
        &adc_hw->fifo,
        NUM_SAMPLES,
        true
    );

    printf("Starting capture\n");

    adc_run(true);

    dma_channel_wait_for_finish_blocking(dma_adc_chan);

    printf("Capture done\n");

    for(int i = 0;i < NUM_SAMPLES; ++i) {
        printf("%d\n", sample_buffer[i]);
    }
}
