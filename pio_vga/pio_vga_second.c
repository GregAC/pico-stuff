#include <stdio.h>
#include "pico/stdlib.h"
#include "hardware/pio.h"
#include "hardware/pio_instructions.h"
#include "hardware/sync.h"
#include "hardware/dma.h"
#include "hardware/irq.h"
#include "video_second.pio.h"

// Buffers containing command words for sync SM
uint32_t visible_line_timing_buffer[4];
uint32_t vblank_porch_buffer[4];
uint32_t vblank_sync_buffer[4];

// Buffers containing line pixel data, 2 16-bit pixels per 32-bit element
// Repeated red, blue, green, black pixel pattern
uint32_t pattern_line_data_buffer[160];
// All white pixels
uint32_t white_line_data_buffer[160];

void setup_line_timing_buffer() {
    // Sync command words for visible lines
    // Hsync pulse Execute nop, delay 474, vsync = 1, hsync = 0
    visible_line_timing_buffer[0] =
        (pio_encode_nop() << 16) | (474 << 2) | 2;
    // Back porch, execute nop, delay 223, vsync = 1, hsync = 1
    visible_line_timing_buffer[1] =
        (pio_encode_nop() << 16) | (223 << 2) | 3;
    // Visible line, execute irq 4, delay 3205, vsync = 1, hsync = 1
    visible_line_timing_buffer[2] =
        (pio_encode_irq_set(false, 4) << 16) | (3205 << 2) | 3;
    // Front porch, execute nop, delay 74, vsync = 1, hsync = 1
    visible_line_timing_buffer[3] =
        (pio_encode_nop() << 16) | (74 << 2) | 3;

    // Non visible lines use same timings for uniformity, always execute nop as
    // no pixels are output.
    // Sync command words for vsync front and back porch
    vblank_porch_buffer[0] =
        (pio_encode_nop() << 16) | (474 << 2) | 2;
    vblank_porch_buffer[1] =
        (pio_encode_nop() << 16) | (223 << 2) | 3;
    vblank_porch_buffer[2] =
        (pio_encode_nop() << 16) | (3205 << 2) | 3;
    vblank_porch_buffer[3] =
        (pio_encode_nop() << 16) | (74 << 2) | 3;

    // Sync command words for vsync sync pulse
    vblank_sync_buffer[0] =
        (pio_encode_nop() << 16) | (474 << 2) | 0;
    vblank_sync_buffer[1] =
        (pio_encode_nop() << 16) | (223 << 2) | 1;
    vblank_sync_buffer[2] =
        (pio_encode_nop() << 16) | (3205 << 2) | 1;
    vblank_sync_buffer[3] =
        (pio_encode_nop() << 16) | (74 << 2) | 1;
}

#define ENCODE_RGB(R, G, B) (((B & 0x1f) << 10) | ((G & 0x1f) << 5) | (R & 0x1f))

void setup_line_data_buffer() {
    for(int i = 0;i < 160; ++i) {
        // Fill `line_pattern_data_buffer` with repeated red, green, blue, black
        // pattern
        if (i & 1) {
            pattern_line_data_buffer[i] = ENCODE_RGB(0, 0, 31);
        } else {
            pattern_line_data_buffer[i] = ENCODE_RGB(31, 0, 0) | (ENCODE_RGB(0, 31, 0) << 16);
        }

        // Fill `white_line_data_buffer` with white pixels
        white_line_data_buffer[i] = 0xffffffff;
    }

    // Set first and last pixels of `pattern_line_data_buffer` to white
    pattern_line_data_buffer[0] |= 0x0000ffff;
    pattern_line_data_buffer[159] |= 0xffff0000;
}


uint32_t sync_dma_chan;
uint32_t line_dma_chan;

int current_timing_line = 0;
int current_display_line = 0;

void __not_in_flash_func(dma_irh)() {
    if (dma_hw->ints0 & (1u << sync_dma_chan)) {
        dma_hw->ints0 = 1u << sync_dma_chan;

        if (current_timing_line < 524) {
            current_timing_line++;
        } else {
            current_timing_line = 0;
        }

        // `current_timing_line` is the line we're about to stream out sync
        // command words for
        if (current_timing_line == 0 || (current_timing_line == 1)) {
            // VSync pulse for lines 0 and 1
            dma_channel_set_read_addr(sync_dma_chan, vblank_sync_buffer, true);
        } else if (current_timing_line < 35) {
            // VGA back porch following VSync pulse (lines 2 - 34)
            dma_channel_set_read_addr(sync_dma_chan, vblank_porch_buffer, true);
        } else if (current_timing_line < 515) {
            // Visible lines following back porch (lines 35 - 514)
            dma_channel_set_read_addr(sync_dma_chan, visible_line_timing_buffer, true);
        } else {
            // Front porch following visible lines (lines 515 - 524)
            dma_channel_set_read_addr(sync_dma_chan, vblank_porch_buffer, true);
        }
    }

    if (dma_hw->ints0 & (1u << line_dma_chan)) {
        dma_hw->ints0 = 1u << line_dma_chan;

        if (current_display_line < 479) {
            ++current_display_line;
        } else {
            current_display_line = 0;
        }

        // `current_display_line` is the line we're about to stream pixel data
        // out for
        if ((current_display_line <= 1) || (current_display_line >= 478)) {
            // Output white border line for lines 0, 1, 478, 479 (two lines as
            // ultimately producing 320x240 resolution so each line is repeated
            // twice).
            dma_channel_set_read_addr(line_dma_chan, white_line_data_buffer, true);
        } else {
            // Otherwise use a pattern line
            dma_channel_set_read_addr(line_dma_chan, pattern_line_data_buffer, true);
        }
    }
}

// Specify where sync and pixel pins are on the pico, this gives
// hsync == 0
// vsync == 1
// pixel = 2 - 16
const uint VID_PINS_BASE_SYNC = 0;
const uint VID_PINS_BASE_LINE = 2;
const uint NUM_VID_PINS_LINE = 15;

const uint sync_sm = 0;
const uint line_sm = 1;

void video_programs_init(PIO pio);
void video_dma_init(PIO pio);

int main()
{
    // Setup the various buffers, PIO and DMA
    setup_line_timing_buffer();
    setup_line_data_buffer();

    PIO pio = pio0;

    video_programs_init(pio);
    video_dma_init(pio);

    // Set everything off
    dma_channel_start(sync_dma_chan);
    dma_channel_start(line_dma_chan);

    pio_sm_set_enabled(pio, line_sm, true);
    pio_sm_set_enabled(pio, sync_sm, true);

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
    // Setup autopull, pull new word after 32 bits shifted out (one pull per two
    // pixels)
    sm_config_set_out_shift(&line_c, true, false, 32);
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
    // Transfer when there's space in the sync SM FIFO
    channel_config_set_dreq(&sync_dma_chan_config, pio_get_dreq(pio, sync_sm, true));

    // Setup the channel and set it going
    dma_channel_configure(
        sync_dma_chan,
        &sync_dma_chan_config,
        &pio->txf[sync_sm], // Write to PIO TX FIFO
        vblank_sync_buffer, // Begin with vblank sync line
        4, // 4 command words in each sync buffer
        false // Don't start yet
    );

    // Setup channel to feed line SM
    line_dma_chan = dma_claim_unused_channel(true);

    dma_channel_config line_dma_chan_config = dma_channel_get_default_config(line_dma_chan);
    // Transfer 32 bits at a time
    channel_config_set_transfer_data_size(&line_dma_chan_config, DMA_SIZE_32);
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
        white_line_data_buffer, // First line output will be white line
        160, // Transfer complete contents of `line_data_buffer`
        false // Don't start yet
    );

    // Setup interrupt handler for line and sync DMA channels
    dma_channel_set_irq0_enabled(line_dma_chan, true);
    dma_channel_set_irq0_enabled(sync_dma_chan, true);
    irq_set_exclusive_handler(DMA_IRQ_0, dma_irh);
    irq_set_enabled(DMA_IRQ_0, true);
}
