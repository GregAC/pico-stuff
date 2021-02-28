#include <stdio.h>
#include "pico/stdlib.h"
#include "hardware/pio.h"
#include "hardware/sync.h"
#include "hardware/dma.h"
#include "hardware/irq.h"
#include "pin_ctrl.pio.h"

void pin_ctrl_run(PIO pio, uint sm, uint offset, uint pin);

// Ensure `timing_buffer` is aligned to 16-bytes so we can use DMA address
// wrapping
uint32_t timing_buffer[4] __attribute__((aligned (16))) = {
    (4 << 2) | 0x1,
    (3 << 2) | 0x2,
    (8 << 2) | 0x3,
    (5 << 2) | 0x0
};

uint32_t pio_dma_chan;

void __not_in_flash_func(dma_irh)() {
    dma_hw->ints0 = (1u << pio_dma_chan);
    dma_hw->ch[pio_dma_chan].al3_read_addr_trig = timing_buffer;
}

const uint TEST_PIN_BASE = 2;

const uint CAPTURE_PIN_BASE = 2;
const uint CAPTURE_PIN_COUNT = 2;
const uint CAPTURE_N_SAMPLES = 1000;

void logic_analyser_init(PIO pio, uint sm, uint pin_base, uint pin_count, float div) {
    // Load a program to capture n pins. This is just a single `in pins, n`
    // instruction with a wrap.
    uint16_t capture_prog_instr = pio_encode_in(pio_pins, pin_count);
    struct pio_program capture_prog = {
            .instructions = &capture_prog_instr,
            .length = 1,
            .origin = -1
    };
    uint offset = pio_add_program(pio, &capture_prog);

    // Configure state machine to loop over this `in` instruction forever,
    // with autopush enabled.
    pio_sm_config c = pio_get_default_sm_config();
    sm_config_set_in_pins(&c, pin_base);
    sm_config_set_wrap(&c, offset, offset);
    sm_config_set_clkdiv(&c, div);
    sm_config_set_in_shift(&c, true, true, 32);
    sm_config_set_fifo_join(&c, PIO_FIFO_JOIN_RX);
    pio_sm_init(pio, sm, offset, &c);
}

void logic_analyser_arm(PIO pio, uint sm, uint dma_chan, uint32_t *capture_buf, size_t capture_size_words,
                        uint trigger_pin, bool trigger_level) {
    pio_sm_set_enabled(pio, sm, false);
    pio_sm_clear_fifos(pio, sm);

    dma_channel_config c = dma_channel_get_default_config(dma_chan);
    channel_config_set_read_increment(&c, false);
    channel_config_set_write_increment(&c, true);
    channel_config_set_dreq(&c, pio_get_dreq(pio, sm, false));

    dma_channel_configure(dma_chan, &c,
        capture_buf,        // Destinatinon pointer
        &pio->rxf[sm],      // Source pointer
        capture_size_words, // Number of transfers
        true                // Start immediately
    );

    pio_sm_exec(pio, sm, pio_encode_wait_gpio(trigger_level, trigger_pin));
    pio_sm_set_enabled(pio, sm, true);
}

void print_capture_buf(const uint32_t *buf, uint pin_base, uint pin_count, uint32_t n_samples) {
    // Output capture in a CSV format, each line gives individual bit values
    // seperated by a comma
    printf("Capture:\n");
    for (int sample = 0; sample < n_samples; ++sample) {
        for (int pin = 0; pin < pin_count; ++pin) {
            uint bit_index = pin + sample * pin_count;
            bool level = !!(buf[bit_index / 32] & 1u << (bit_index % 32));
            printf("%d", level ? 1 : 0);

            if (pin < pin_count - 1){
                printf(",");
            }
        }
        printf("\n");
    }
    printf("Capture complete\n");
}


int main()
{
    // Some delay before we start to allow time for a USB serial connection to
    // be made
    stdio_init_all();
    sleep_ms(5000);
    printf("Here we go\n");
    sleep_ms(1000);

    PIO pio = pio0;

    // Load the pin_ctrl program into the PIO
    uint offset = pio_add_program(pio, &pin_ctrl_program);

    // Setup logic analyzer PIO and DMA
    uint logic_sm = 1;
    uint logic_dma_chan = dma_claim_unused_channel(true);

    uint32_t capture_buf[(CAPTURE_PIN_COUNT * CAPTURE_N_SAMPLES + 31) / 32];
    logic_analyser_init(pio, logic_sm, CAPTURE_PIN_BASE, CAPTURE_PIN_COUNT, 1.f);

    logic_analyser_arm(pio, logic_sm, logic_dma_chan, capture_buf,
        (CAPTURE_PIN_COUNT * CAPTURE_N_SAMPLES + 31) / 32,
        CAPTURE_PIN_BASE, true);

    // Run the pin_ctrl program
    pin_ctrl_run(pio, 0, offset, TEST_PIN_BASE);

    // Wait until logic analyzer has filled up its buffer
    dma_channel_wait_for_finish_blocking(logic_dma_chan);

    // Dump buffer as CSV for analyis
    print_capture_buf(capture_buf, CAPTURE_PIN_BASE, CAPTURE_PIN_COUNT, CAPTURE_N_SAMPLES);

    while(1) {
        __wfi();
    }

    return 0;
}

void pin_ctrl_program_init(PIO pio, uint sm, uint offset, uint pin) {
    // Setup pin and pin+1 to be accessible from the PIO
    pio_gpio_init(pio, pin);
    pio_gpio_init(pio, pin+1);

    // Setup PIO SM to control pin and pin + 1
    pio_sm_set_consecutive_pindirs(pio, sm, pin, 2, true);

    // Configure PIO SM with pin_ctrl program
    pio_sm_config c = pin_ctrl_program_get_default_config(offset);
    // pin and pin + 1 are controlled by `out pins, ...` instructions
    sm_config_set_out_pins(&c, pin, 2);
    sm_config_set_clkdiv(&c, 1.0f);
    // Join FIFOs together to get an 8 entry TX FIFO
    sm_config_set_fifo_join(&c, PIO_FIFO_JOIN_TX);
    pio_sm_init(pio, sm, offset, &c);
}

void pin_ctrl_run(PIO pio, uint sm, uint offset, uint pin) {
    // Allocate a DMA channel to feed the pin_ctrl SM its command words
    pio_dma_chan = dma_claim_unused_channel(true);

    dma_channel_config pio_dma_chan_config = dma_channel_get_default_config(pio_dma_chan);
    // Transfer 32 bits each time
    channel_config_set_transfer_data_size(&pio_dma_chan_config, DMA_SIZE_32);
    // Increment read address (a different command word from `timing_buffer`
    // each time)
    channel_config_set_read_increment(&pio_dma_chan_config, true);
    // Write to the same address (the PIO SM TX FIFO)
    channel_config_set_write_increment(&pio_dma_chan_config, false);
    // Set read address to wrap on a 16-byte boundary
    channel_config_set_ring(&pio_dma_chan_config, false, 4);
    // Transfer when PIO SM TX FIFO has space
    channel_config_set_dreq(&pio_dma_chan_config, pio_get_dreq(pio, sm, true));

    // Setup the channel and set it going
    dma_channel_configure(
        pio_dma_chan,
        &pio_dma_chan_config,
        &pio->txf[sm], // Write to PIO TX FIFO
        timing_buffer, // Read values from timing buffer
        16, // `timing_buffer` has 4 entries, so 16 will go through it 4 times
        false // don't start yet
    );

    // Setup IRQ for DMA transfer end
    dma_channel_set_irq0_enabled(pio_dma_chan, true);
    irq_set_exclusive_handler(DMA_IRQ_0, dma_irh);
    irq_set_enabled(DMA_IRQ_0, true);

    // Initialise PIO SM with pin_ctrl program
    pin_ctrl_program_init(pio, sm, offset, pin);
    // Start the DMA (must do this after program init or DMA won't do anything)
    dma_channel_start(pio_dma_chan);
    // Start the PIO
    pio_sm_set_enabled(pio, sm, true);
}
