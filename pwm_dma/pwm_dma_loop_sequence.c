#include <stdio.h>
#include "pico/stdlib.h"
#include "hardware/dma.h"
#include "hardware/irq.h"
#include "hardware/pwm.h"
#include "hardware/sync.h"

/*
    List of pins, LED they're connected to and which PWM slice/channel they use
    GPIO16 - L0B - S0A
    GPIO17 - L0G - S0B
    GPIO18 - L0R - S1A
    GPIO19 - L1B - S1B
    GPIO20 - L1G - S2A
    GPIO21 - L1R - S2B
    GPIO22 - L2B - S3A
    GPIO26 - L2G - S5A
    GPIO27 - L2R - S5B

    GPIO15 - L3B - S7B
    GPIO14 - L3G - S7A
    GPIO13 - L3R - S6B
    GPIO12 - L4B - S6A
    GPIO9  - L4G - S4B
    GPIO8  - L4R - S4A
*/

// Pins to set to PWM
const uint leds_pins[] = {
    16,
    17,
    18,
    19,
    20,
    21,
    22,
    26,
    27,
    15,
    14,
    13,
    12,
    9,
    8
};

#define NUM_LEDS 15

// PWM slices to use, in the order they will be written by the DMA
const uint pwm_slices[] = {
    0,
    1,
    2,
    3,
    5,
    7,
    6,
    4
};

#define NUM_PWM_SLICE 8

uint32_t pwm_data[512 * 8];

void fill_pwm_data() {
    for(int fade = 0;fade < 512; ++fade) {
        int level = fade > 255 ? 511 - fade : fade;
        level = level * level;

        if (fade > 255) {
            pwm_data[fade*NUM_PWM_SLICES] = 0;
            pwm_data[fade*NUM_PWM_SLICES + 1] = level;
            pwm_data[fade*NUM_PWM_SLICES + 2] = level;
            pwm_data[fade*NUM_PWM_SLICES + 3] = level;
            pwm_data[fade*NUM_PWM_SLICES + 4] = 0;

            pwm_data[fade*NUM_PWM_SLICES + 5] = level << 16;
            pwm_data[fade*NUM_PWM_SLICES + 6] = level << 16;
            pwm_data[fade*NUM_PWM_SLICES + 7] = (level << 16) | level;
        } else {
            pwm_data[fade*NUM_PWM_SLICES] = level;
            pwm_data[fade*NUM_PWM_SLICES + 1] = 0;
            pwm_data[fade*NUM_PWM_SLICES + 2] = level;
            pwm_data[fade*NUM_PWM_SLICES + 3] = 0;
            pwm_data[fade*NUM_PWM_SLICES + 4] = level << 16;

            pwm_data[fade*NUM_PWM_SLICES + 5] = level << 16;
            pwm_data[fade*NUM_PWM_SLICES + 6] = level << 16;
            pwm_data[fade*NUM_PWM_SLICES + 7] = (level << 16) | level;
        }
    }
}

uint32_t pwm_dma_list[NUM_PWM_SLICES+1];
uint32_t* dma_list_ptr = pwm_dma_list;

int main()
{
    stdio_init_all();
    fill_pwm_data();

    for (int i = 0;i < NUM_LEDS; ++i) {
        gpio_set_function(leds_pins[i], GPIO_FUNC_PWM);
    }

    pwm_config config = pwm_get_default_config();

    for (int i = 0;i < NUM_PWM_SLICES; ++i) {
        pwm_config_set_clkdiv(&config, 8.f);
        pwm_init(pwm_slices[i], &config, false);
    }

    pwm_hw->en = 0xffff;

    int pwm_dma_chan = dma_claim_unused_channel(true);
    int control_dma_chan = dma_claim_unused_channel(true);

    dma_channel_config pwm_dma_chan_config = dma_channel_get_default_config(pwm_dma_chan);
    channel_config_set_transfer_data_size(&pwm_dma_chan_config, DMA_SIZE_32);
    // Increment on read so we read a new element of `pwm_data` each time
    channel_config_set_read_increment(&pwm_dma_chan_config, true);
    // Don't increment on write, though it doesn't really matter as there's only a single transfer
    // and the control DMA channel gives us a new write address each time
    channel_config_set_write_increment(&pwm_dma_chan_config, false);
    channel_config_set_chain_to(&pwm_dma_chan_config, control_dma_chan);

    dma_channel_configure(
        pwm_dma_chan,
        &pwm_dma_chan_config,
        NULL, // No write address yet the control DMA channel will set one
        pwm_data, // Read from the pre-computed pwm_data
        1, // Just do a single transfer from pwm_data to a PWM slice
        false // Don't start yet, the control DMA channel will trigger it
    );

    // Setup data for the control DMA channel
    // For each slice we're using we want the address of its counter compare
    // register. The control DMA channel will read this address from the buffer
    // and write it to the PWM DMA channel write address triggering a transfer
    // to the PWM slice.
    for (int i = 0;i < NUM_PWM_SLICES; ++i) {
        pwm_dma_list[i] = (uint32_t)&pwm_hw->slice[pwm_slices[i]].cc;
    }

    // Terminate the list with a null value, when this is written to the DMA
    // Write Address for the PWM DWM channel it won't trigger anything stopping
    // the chaining and everything comes to a halt.
    pwm_dma_list[NUM_PWM_SLICES] = 0;

    dma_channel_config control_dma_chan_config = dma_channel_get_default_config(control_dma_chan);
    channel_config_set_transfer_data_size(&control_dma_chan_config, DMA_SIZE_32);
    // Increment on read so we go through all the PWM slice addresses from `pwm_dma_list`
    channel_config_set_read_increment(&control_dma_chan_config, true);
    // Don't increment on write, we always want to write to the PWM DMA channel write address
    channel_config_set_write_increment(&control_dma_chan_config, false);

    dma_channel_configure(
        control_dma_chan,
        &control_dma_chan_config,
        // Write to the PWM DMA channel write address
        &dma_hw->ch[pwm_dma_chan].al2_write_addr_trig,
        // Read from the list of PWM slice CC register addresses
        &pwm_dma_list,
        1, // Transfer a single element of `pwm_dma_list` to the PWM DMA channel write address
        false // Don't start yet
    );

    int trigger_dma_chan = dma_claim_unused_channel(true);

    dma_channel_config trigger_dma_chan_config = dma_channel_get_default_config(trigger_dma_chan);
    channel_config_set_transfer_data_size(&trigger_dma_chan_config, DMA_SIZE_32);
    // Don't increment read or write, the trigger DMA channel just writes the
    // same thing to the same place 512 times
    channel_config_set_read_increment(&trigger_dma_chan_config, false);
    channel_config_set_write_increment(&trigger_dma_chan_config, false);
    // Only do a transfer when we reach the end of a PWM cycle.
    channel_config_set_dreq(&trigger_dma_chan_config, DREQ_PWM_WRAP0);

    dma_channel_configure(
        trigger_dma_chan,
        &trigger_dma_chan_config,
        // Write to the Control DMA channel read address
        &dma_hw->ch[control_dma_chan].al3_read_addr_trig,
        // Read from location containing the address of the beginning of pwm_dma_list
        &dma_list_ptr,
        512,
        false // Don't start yet
    );

    while (true) {
        dma_hw->ch[pwm_dma_chan].al1_read_addr = pwm_data;
        dma_hw->ch[trigger_dma_chan].al3_read_addr_trig = &dma_list_ptr;
        sleep_ms(3000);
    }

    return 0;
}
