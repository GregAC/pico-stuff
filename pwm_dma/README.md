PWM DMA Pico Demo
-----------------

This is the code described in my (Playing with the Pico Part 2
blog)[https://gregchadwick.co.uk/blog/playing-with-the-pico-pt2]. Read that for
full details.

The files present are:

* `pwm_dma_led_fade.c` - Fades up the Pico's onboard LED using DMA
* `pwm_dma_loop_sequence.c` - Runs a test sequence of colour values across some
  RGB LEDs using DMA with a basic loop with a sleep to repeat it
* `pwm_dma_interrupt_sequence.c` - Runs the same test sequence or a nicer
  rainbow sequence using interrupts to do repeats


