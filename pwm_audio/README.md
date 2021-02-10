PWM Audio Pico Demo
-------------------

This is the code described in my blog:
https://gregchadwick.co.uk/blog/playing-with-the-pico-pt3/. Read that for full
details.

The files present are:

* `pwm_adc_step_test.c` - Stepping up and down PWM levels using ADC to measure
  the result
* `pwm_adc_tone_test.c` - Outputting a 440 Hz tone via PWM audio using ADC to
  measure the result
* `pwm_audio_interrupt.c` - Using PWM interrupts to play an audio clip
* `pwm_audio_dma.c` - Using DMA to play an audio clip
