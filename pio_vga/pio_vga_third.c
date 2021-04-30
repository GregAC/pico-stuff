#include <stdio.h>
#include "pico/stdlib.h"
#include "hardware/pio.h"
#include "hardware/pio_instructions.h"
#include "hardware/sync.h"
#include "hardware/dma.h"
#include "hardware/irq.h"
#include "video_second.pio.h"

uint32_t __attribute__ ((aligned (4))) visible_line_timing_buffer[4];
uint32_t __attribute__ ((aligned (4))) vblank_porch_buffer[4];
uint32_t __attribute__ ((aligned (4))) vblank_sync_buffer[4];

uint32_t __attribute__ ((aligned (4))) line_data_zero_buffer = 0xffffffff;
uint16_t __attribute__ ((aligned (4))) line_data_buffer_even[320];
uint16_t __attribute__ ((aligned (4))) line_data_buffer_odd[320];

uint32_t sync_dma_chan;
uint32_t line_dma_chan;
dma_channel_config line_dma_chan_config;

volatile bool new_frame;
volatile bool new_line_needed;
volatile int next_line;

int current_timing_line;
int current_display_line;

void setup_line_timing_buffers() {
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
        } else if (current_timing_line < 32) {
            // VGA back porch following VSync pulse (lines 2 - 34).
            // Stops early for 3 dummy lines (at line 31).
            dma_channel_set_read_addr(sync_dma_chan, vblank_porch_buffer, true);
        } else if (current_timing_line < 515) {
            // Dummy lines for final 3 lines of back porch (32 - 34) and visible
            // lines following back porch (lines 35 - 514)
            dma_channel_set_read_addr(sync_dma_chan, visible_line_timing_buffer, true);
        } else {
            // Front porch following visible lines (lines 515 - 524)
            dma_channel_set_read_addr(sync_dma_chan, vblank_porch_buffer, true);
        }
    }

    if (dma_hw->ints0 & (1u << line_dma_chan)) {
        dma_hw->ints0 = 1u << line_dma_chan;

        if (current_display_line == 479) {
            // Final line of this frame has completed so signal new frame and setup for next.
            new_frame = true;

            // 3 dummy lines before real lines
            current_display_line = -3;

            // Setup Line DMA channel to read zero lines for dummy lines and set it going.
            // Disable read increment so just read zero over and over for dummy lines.
            // DMA won't actually begin until line PIO starts consuming it in the next
            // frame.
            channel_config_set_read_increment(&line_dma_chan_config, false);
            dma_channel_set_config(line_dma_chan, &line_dma_chan_config, false);
            dma_channel_set_read_addr(line_dma_chan, &line_data_zero_buffer, true);
            return;
        }

        current_display_line++;

        // Need a new line every two display lines
        if ((current_display_line & 1) == 0) {
            // At display lines 478 & 479 we're drawing the final line so don't need
            // to request a new line
            if (current_display_line != 478) {
                new_line_needed = true;
                next_line = (current_display_line / 2) + 1;
            }
        }

        if (current_display_line == 0) {
            // Beginning visible lines, turn on read increment for line DMA
            channel_config_set_read_increment(&line_dma_chan_config, true);
            dma_channel_set_config(line_dma_chan, &line_dma_chan_config, false);
        }

        // Negative lines are dummy lines so output from zero buffer, otherwise
        // choose even or odd line depending upon current display line
        if (current_display_line < 0) {
            dma_channel_set_read_addr(line_dma_chan, &line_data_zero_buffer, true);
        } else if (current_display_line & 2) {
            dma_channel_set_read_addr(line_dma_chan, line_data_buffer_odd, true);
        } else {
            dma_channel_set_read_addr(line_dma_chan, line_data_buffer_even, true);
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

#define video_pio pio0_hw

void video_pio_init(PIO pio) {
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

    line_dma_chan_config = dma_channel_get_default_config(line_dma_chan);
    // Transfer 32 bits at a time
    channel_config_set_transfer_data_size(&line_dma_chan_config, DMA_SIZE_32);
    // Increment read to go through the line data buffer
    channel_config_set_read_increment(&line_dma_chan_config, false);
    // Don't increment write so we always transfer to the PIO FIFO
    channel_config_set_write_increment(&line_dma_chan_config, false);
    // Transfer when there's space in the line SM FIFO
    channel_config_set_dreq(&line_dma_chan_config, pio_get_dreq(pio, line_sm, true));

    // Setup the channel and set it going
    dma_channel_configure(
        line_dma_chan,
        &line_dma_chan_config,
        &pio->txf[line_sm], // Write to PIO TX FIFO
        &line_data_zero_buffer, // First line output will be white line
        160, // Transfer complete contents of `line_data_buffer`
        false // Don't start yet
    );

    // Setup interrupt handler for line and sync DMA channels
    dma_channel_set_irq0_enabled(line_dma_chan, true);
    dma_channel_set_irq0_enabled(sync_dma_chan, true);
    irq_set_exclusive_handler(DMA_IRQ_0, dma_irh);
    irq_set_enabled(DMA_IRQ_0, true);
}

void setup_video() {
    new_frame = false;
    new_line_needed = false;
    current_timing_line = 0;
    current_display_line = -3;

    setup_line_timing_buffers();
    video_pio_init(video_pio);
    video_dma_init(video_pio);
}

void start_video() {
    dma_channel_start(sync_dma_chan);
    dma_channel_start(line_dma_chan);

    pio_sm_set_enabled(video_pio, line_sm, true);
    pio_sm_set_enabled(video_pio, sync_sm, true);
}

void draw_line(int line_y, uint16_t* line_buffer);
void end_of_frame();

void video_loop() {
    while(1) {
        // Wait for an interrupt to occur
        __wfi();

        // Temporarily disable interrupts to avoid race conditions around
        // `new_line_needed` being written
        uint32_t interrupt_status = save_and_disable_interrupts();

        // Check if a new line is needed, if so clear the flag (so the loop doesn't immediately try to
        // draw it again)
        bool do_draw_line = new_line_needed;
        if (new_line_needed) {
            new_line_needed = false;
        }

        // Check if a new frame is needed, if so clean the flag (so the loop doesn't immediately signal
        // a new frame again)
        bool do_end_of_frame = new_frame;
        if (new_frame) {
            new_frame = false;
        }

        // Reenable interrupts
        restore_interrupts(interrupt_status);

        // If a new line is required call `draw_line` to fill the relevant line buffer
        if (do_draw_line) {
            if (next_line & 1) {
                draw_line(next_line, line_data_buffer_odd);
            } else {
                draw_line(next_line, line_data_buffer_even);
            }
        }

        if (do_end_of_frame) {
            end_of_frame();
        }
    }
}

#define ENCODE_RGB(R, G, B) (((B & 0x1f) << 10) | ((G & 0x1f) << 5) | (R & 0x1f))

int start_colour_val = 0;
bool start_colour_val_inc = true;

void draw_line(int line_y, uint16_t* line_buffer) {
    if ((line_y == 0) || (line_y == 239)) {
        // Top and bottom lines are white
        for (int i = 0;i < 320; ++i) {
            line_buffer[i] = 0xffff;
        }

        return;
    }

    // Each colour area is 32 pixels high and rotate around red, green and blue. Determine whether
    // we're in a red, green or blue colour area (colour_area == 0, 1 or 2).
    int colour_area = (line_y / 32) % 3;
    // Within the colour area where are we in the gradient
    int colour_val = (line_y + start_colour_val) % 32;

    // Produce R, G, B values given the colour_area and colour_val
    int r, g, b;
    if (colour_area == 0) {
        r = colour_val;g = 0; b = 0;
    } else if (colour_area == 1) {
        r = 0;g = colour_val; b = 0;
    } else if (colour_area == 2) {
        r = 0;g = 0; b = colour_val;
    }

    // Fill line with calculated R, G, B values
    for (int i = 0; i < 320; ++i) {
        line_buffer[i] = ENCODE_RGB(r, g, b);
    }

    // Set first and last pixels to white for the border
    line_buffer[0] = 0xffff;
    line_buffer[319] = 0xffff;
}

void end_of_frame() {
    // Every frame increment or decrement `start_colour_val`
    if (start_colour_val_inc) {
        start_colour_val++;
    } else {
        start_colour_val--;
    }

    if (start_colour_val == 0) {
        start_colour_val_inc = true;
    } else if (start_colour_val == 31) {
        start_colour_val_inc = false;
    }
}

int main() {
    setup_video();
    start_video();

    video_loop();
}
