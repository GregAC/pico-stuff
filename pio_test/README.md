PIO Test
--------

This is the code described in my blog:
https://gregchadwick.co.uk/blog/playing-with-the-pico-pt4/. Read that for full
details.

The files present are:

* `pio_test.c` - A program used to demonstrate feeding commands to the
  `pin_ctrl.pio` PIO program
* `pin_ctrl.pio` - Assembly for the PIO program being demonstrated, it takes
  command words with two pin settings and a delay. Each command sets the pins as
  requested then waits the number of cycles given by the delay.

If building these using the Pico SDK's CMake build system note you will want to
add a line to build the pio program to CMakeLists.txt

```
pico_generate_pio_header(pio_test ${CMAKE_CURRENT_LIST_DIR}/pin_ctrl.pio)
```

You also need to ensure you have the `hardware_dma` and `hardware_pio` libraries
made available:

```
target_link_libraries(pio_test
        hardware_dma
        hardware_pio
        )
```
