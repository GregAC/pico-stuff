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

// The fixed location the sample DMA channel writes to and the PWM DMA channel
// reads from
uint32_t single_sample = 0;
uint32_t* single_sample_ptr = &single_sample;

int pwm_dma_chan, trigger_dma_chan, sample_dma_chan;

#define REPETITION_RATE 4

void dma_irh() {
    dma_hw->ch[sample_dma_chan].al1_read_addr = audio_buffer;
    dma_hw->ch[trigger_dma_chan].al3_read_addr_trig = &single_sample_ptr;

    dma_hw->ints0 = (1u << trigger_dma_chan);
}

int main(void) {
    stdio_init_all();

    gpio_set_function(AUDIO_PIN, GPIO_FUNC_PWM);

    int audio_pin_slice = pwm_gpio_to_slice_num(AUDIO_PIN);

    pwm_config config = pwm_get_default_config();
    pwm_config_set_clkdiv(&config, 22.1f / REPETITION_RATE);
    pwm_config_set_wrap(&config, 254);
    pwm_init(audio_pin_slice, &config, true);

    pwm_dma_chan = dma_claim_unused_channel(true);
    trigger_dma_chan = dma_claim_unused_channel(true);
    sample_dma_chan = dma_claim_unused_channel(true);

    // Setup PWM DMA channel
    dma_channel_config pwm_dma_chan_config = dma_channel_get_default_config(pwm_dma_chan);
    // Transfer 32-bits at a time
    channel_config_set_transfer_data_size(&pwm_dma_chan_config, DMA_SIZE_32);
    // Read from a fixed location, always writes to the same address
    channel_config_set_read_increment(&pwm_dma_chan_config, false);
    channel_config_set_write_increment(&pwm_dma_chan_config, false);
    // Chain to sample DMA channel when done
    channel_config_set_chain_to(&pwm_dma_chan_config, sample_dma_chan);
    // Transfer on PWM cycle end
    channel_config_set_dreq(&pwm_dma_chan_config, DREQ_PWM_WRAP0 + audio_pin_slice);

    dma_channel_configure(
        pwm_dma_chan,
        &pwm_dma_chan_config,
        // Write to PWM slice CC register
        &pwm_hw->slice[audio_pin_slice].cc,
        // Read from single_sample
        &single_sample,
        // Transfer once per desired sample repetition
        REPETITION_RATE,
        // Don't start yet
        false
    );

    // Setup trigger DMA channel
    dma_channel_config trigger_dma_chan_config = dma_channel_get_default_config(trigger_dma_chan);
    // Transfer 32-bits at a time
    channel_config_set_transfer_data_size(&trigger_dma_chan_config, DMA_SIZE_32);
    // Always read and write from and to the same address
    channel_config_set_read_increment(&trigger_dma_chan_config, false);
    channel_config_set_write_increment(&trigger_dma_chan_config, false);
    // Transfer on PWM cycle end
    channel_config_set_dreq(&trigger_dma_chan_config, DREQ_PWM_WRAP0 + audio_pin_slice);

    dma_channel_configure(
        trigger_dma_chan,
        &trigger_dma_chan_config,
        // Write to PWM DMA channel read address trigger
        &dma_hw->ch[pwm_dma_chan].al3_read_addr_trig,
        // Read from location containing the address of single_sample
        &single_sample_ptr,
        // Need to trigger once for each audio sample but as the PWM DREQ is
        // used need to multiply by repetition rate
        REPETITION_RATE * AUDIO_SAMPLES,
        false
    );

    // Fire interrupt when trigger DMA channel is done
    dma_channel_set_irq0_enabled(trigger_dma_chan, true);
    irq_set_exclusive_handler(DMA_IRQ_0, dma_irh);
    irq_set_enabled(DMA_IRQ_0, true);

    // Setup sample DMA channel
    dma_channel_config sample_dma_chan_config = dma_channel_get_default_config(sample_dma_chan);
    // Transfer 8-bits at a time
    channel_config_set_transfer_data_size(&sample_dma_chan_config, DMA_SIZE_8);
    // Increment read address to go through audio buffer
    channel_config_set_read_increment(&sample_dma_chan_config, true);
    // Always write to the same address
    channel_config_set_write_increment(&sample_dma_chan_config, false);

    dma_channel_configure(
        sample_dma_chan,
        &sample_dma_chan_config,
        // Write to single_sample
        &single_sample,
        // Read from audio buffer
        audio_buffer,
        // Only do one transfer (once per PWM DMA completion due to chaining)
        1,
        // Don't start yet
        false
    );

    // Kick things off with the trigger DMA channel
    dma_channel_start(trigger_dma_chan);

    while(1) {
        __wfi();
    }
}
