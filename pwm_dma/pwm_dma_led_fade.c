#include <stdio.h>
#include "pico/stdlib.h"
#include "hardware/dma.h"
#include "hardware/pwm.h"

int main()
{
    gpio_set_function(PICO_DEFAULT_LED_PIN, GPIO_FUNC_PWM);

    int led_pwm_slice_num = pwm_gpio_to_slice_num(PICO_DEFAULT_LED_PIN);

    pwm_config config = pwm_get_default_config();
    pwm_config_set_clkdiv(&config, 8.f);
    pwm_init(led_pwm_slice_num, &config, true);

    uint32_t fade[256];

    for (int i = 0; i < 256; ++i) {
        // We need a value from 0 - (2^16 - 1), i ranges from 0 - 255. Squaring here gives us
        // almost the full range of values whilst provided some gamma correction to give a more
        // linear fade effect.
        fade[i] = (i * i) << 16;
    }

    // Setup DMA channel to drive the PWM
    int pwm_dma_chan = dma_claim_unused_channel(true);

    dma_channel_config pwm_dma_chan_config = dma_channel_get_default_config(pwm_dma_chan);
    // Transfers 32-bits at a time, increment read address so we pick up a new fade value each
    // time, don't increment writes address so we always transfer to the same PWM register.
    channel_config_set_transfer_data_size(&pwm_dma_chan_config, DMA_SIZE_32);
    channel_config_set_read_increment(&pwm_dma_chan_config, true);
    channel_config_set_write_increment(&pwm_dma_chan_config, false);
    // Transfer when PWM slice that is connected to the LED asks for a new value
    channel_config_set_dreq(&pwm_dma_chan_config, DREQ_PWM_WRAP0 + led_pwm_slice_num);

    // Setup the channel and set it going
    dma_channel_configure(
        pwm_dma_chan,
        &pwm_dma_chan_config,
        &pwm_hw->slice[led_pwm_slice_num].cc, // Write to PWM counter compare
        fade, // Read values from fade buffer
        256, // 256 values to copy
        true // Start immediately.
    );

    while(true) {
        tight_loop_contents();
    }
}
