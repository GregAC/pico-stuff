#include <stdio.h>
#include "pico/stdlib.h"
#include "hardware/pio.h"
#include "hardware/pio_instructions.h"
#include "hardware/sync.h"
#include "hardware/dma.h"
#include "hardware/irq.h"
#include "video_first.pio.h"

// Buffers to DMA to the sync and line SMs.
// Align sync_timing_buffer to 16 byte boundary so we can use DMA address
// wrapping functionality
uint32_t sync_timing_buffer[4] __attribute__((aligned (16)));
uint16_t line_data_buffer[40];

void setup_line_timing_buffer() {
    // vsync always 0 for this test
    // Execute nop, delay 10, hsync = 1
    sync_timing_buffer[0] =
        (pio_encode_nop() << 16) | (10 << 2) | 1;
    // Execute nop, delay 15, hsync = 0
    sync_timing_buffer[1] =
        (pio_encode_nop() << 16) | (15 << 2) | 0;
    // Execute nop, delay 15, hsync = 1
    sync_timing_buffer[2] =
        (pio_encode_nop() << 16) | (15 << 2) | 1;
    // Execute `irq 4` to start pixel output, delay 100, hsync = 1
    // The visible line has 10 pixels, with a new pixel every 10 cycles
    // needing 10 * 10 = 100 cycles. Set our delay to that for now so we'll
    // roughly hit the right timings.
    sync_timing_buffer[3] =
        (pio_encode_irq_set(false, 4) << 16) | (100 << 2) | 0;
}

void setup_line_data_buffer() {
    // Test pixels are just numbers from 1 - 40
    for(int i = 1;i <= 40; ++i) {
        line_data_buffer[i-1] = i;
    }
}

uint32_t sync_dma_chan;
uint32_t line_dma_chan;

// Specify where sync and pixel pins are on the pico, this gives
// hsync == 0
// vsync == 1
// pixel = 2 - 7
// (Only 6 bits per pixel in this test)
const uint VID_PINS_BASE_SYNC = 0;
const uint VID_PINS_BASE_LINE = 2;
const uint NUM_VID_PINS_LINE = 6;

const uint CAPTURE_PIN_BASE = 0;
const uint CAPTURE_PIN_COUNT = 8;
const uint CAPTURE_N_SAMPLES = 1000;

const uint sync_sm = 0;
const uint line_sm = 1;
const uint analyzer_sm = 2;

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
    printf("Capture:\n");
    for (int sample = 0; sample < n_samples; ++sample) {
        for (int pin = pin_count - 1; pin >= 0; --pin) {
            uint bit_index = pin + sample * pin_count;
            bool level = !!(buf[bit_index / 32] & 1u << (bit_index % 32));
            printf("%d", level ? 1 : 0);

            if (pin != 0){
                printf(",");
            }
        }
        printf("\n");
    }
    printf("Capture complete\n");
}

void video_programs_init(PIO pio);
void video_dma_init(PIO pio);

int main()
{
    // Some delay before we start to allow time for a USB serial connection to
    // be made
    stdio_init_all();
    sleep_ms(5000);

    printf("Here we go\n");
    sleep_ms(1000);

    // Setup the various buffers, PIO and DMA
    setup_line_timing_buffer();
    setup_line_data_buffer();

    PIO pio = pio0;

    uint logic_dma_chan = dma_claim_unused_channel(true);

    video_programs_init(pio);
    video_dma_init(pio);

    // Setup the logic analyzer
    uint32_t capture_buf[(CAPTURE_PIN_COUNT * CAPTURE_N_SAMPLES + 31) / 32];
    logic_analyser_init(pio, analyzer_sm, CAPTURE_PIN_BASE, CAPTURE_PIN_COUNT, 1.f);

    logic_analyser_arm(pio, analyzer_sm, logic_dma_chan, capture_buf, //;
        (CAPTURE_PIN_COUNT * CAPTURE_N_SAMPLES + 31) / 32,
        CAPTURE_PIN_BASE, true);

    // Set everything off
    dma_channel_start(sync_dma_chan);
    dma_channel_start(line_dma_chan);

    pio_sm_set_enabled(pio, line_sm, true);
    pio_sm_set_enabled(pio, sync_sm, true);

    // Wait til we've got our logic analyzer capture
    dma_channel_wait_for_finish_blocking(logic_dma_chan);

    // Dump it out over UART
    print_capture_buf(capture_buf, CAPTURE_PIN_BASE, CAPTURE_PIN_COUNT, CAPTURE_N_SAMPLES);

    printf("Capture dump finished\n");

    while(1) {
        __wfi();
    }

    return 0;
}

void video_programs_init(PIO pio) {
    // Setup sync and pixel pins to be accessible to the PIO block
    pio_gpio_init(pio, VID_PINS_BASE_SYNC);
    pio_gpio_init(pio, VID_PINS_BASE_SYNC+1);

    for(int i = 0;i < NUM_VID_PINS_LINE; ++i) {
        pio_gpio_init(pio, VID_PINS_BASE_LINE + i);
    }

    // Setup sync SM
    uint sync_prog_offset = pio_add_program(pio, &sync_out_program);
    pio_sm_set_consecutive_pindirs(pio, sync_sm, VID_PINS_BASE_SYNC, 2, true);
    pio_sm_config sync_c = sync_out_program_get_default_config(sync_prog_offset);
    sm_config_set_out_pins(&sync_c, VID_PINS_BASE_SYNC, 2);
    sm_config_set_clkdiv(&sync_c, 1.0f);
    // Join FIFOs together to get an 8 entry TX FIFO
    sm_config_set_fifo_join(&sync_c, PIO_FIFO_JOIN_TX);
    pio_sm_init(pio, sync_sm, sync_prog_offset, &sync_c);

    // Setup line SM
    uint line_prog_offset = pio_add_program(pio, &line_out_program);
    pio_sm_set_consecutive_pindirs(pio, line_sm, VID_PINS_BASE_LINE, NUM_VID_PINS_LINE, true);
    pio_sm_config line_c = line_out_program_get_default_config(line_prog_offset);
    sm_config_set_out_pins(&line_c, VID_PINS_BASE_LINE, NUM_VID_PINS_LINE);
    sm_config_set_clkdiv(&line_c, 1.0f);
    // Join FIFOs together to get an 8 entry TX FIFO
    sm_config_set_fifo_join(&line_c, PIO_FIFO_JOIN_TX);
    pio_sm_init(pio, line_sm, line_prog_offset, &line_c);
}

void video_dma_init(PIO pio) {
    // Setup channel to feed sync SM
    sync_dma_chan = dma_claim_unused_channel(true);

    dma_channel_config sync_dma_chan_config = dma_channel_get_default_config(sync_dma_chan);
    // Transfer 32 bits at a time
    channel_config_set_transfer_data_size(&sync_dma_chan_config, DMA_SIZE_32);
    // Increment read to go through the sync timing command buffer
    channel_config_set_read_increment(&sync_dma_chan_config, true);
    // Don't increment write so we always transfer to the PIO FIFO
    channel_config_set_write_increment(&sync_dma_chan_config, false);
    // Wrap read address on 16 byte boundary
    channel_config_set_ring(&sync_dma_chan_config, false, 4);
    // Transfer when there's space in the sync SM FIFO
    channel_config_set_dreq(&sync_dma_chan_config, pio_get_dreq(pio, sync_sm, true));

    // Setup the channel and set it going
    dma_channel_configure(
        sync_dma_chan,
        &sync_dma_chan_config,
        &pio->txf[sync_sm], // Write to PIO TX FIFO
        sync_timing_buffer, // Read values from timing buffer
        16, // Transfer `sync_timing_buffer` 4 times, 16 = 4 * 4
        false // Don't start yet
    );

    // Setup channel to feed line SM
    line_dma_chan = dma_claim_unused_channel(true);

    dma_channel_config line_dma_chan_config = dma_channel_get_default_config(line_dma_chan);
    // Transfer 16 bits at a time
    channel_config_set_transfer_data_size(&line_dma_chan_config, DMA_SIZE_16);
    // Increment read to go through the line data buffer
    channel_config_set_read_increment(&line_dma_chan_config, true);
    // Don't increment write so we always transfer to the PIO FIFO
    channel_config_set_write_increment(&line_dma_chan_config, false);
    // Transfer when there's space in the line SM FIFO
    channel_config_set_dreq(&line_dma_chan_config, pio_get_dreq(pio, line_sm, true));

    // Setup the channel and set it going
    dma_channel_configure(
        line_dma_chan,
        &line_dma_chan_config,
        &pio->txf[line_sm], // Write to PIO TX FIFO
        line_data_buffer, // Read values from line buffer
        40, // Transfer complete contents of `line_data_buffer`
        false // Don't start yet
    );
}
