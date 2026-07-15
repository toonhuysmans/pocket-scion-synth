#include "audio_i2s.h"

#include <string.h>

#include "board_pins.h"
#include "hardware/clocks.h"
#include "hardware/dma.h"
#include "hardware/irq.h"
#include "hardware/pio.h"
#include "hardware/sync.h"
#include "i2s_tx.pio.h"
#include "synth.h"

#define AUDIO_BUFFER_COUNT 3u

static PIO audio_pio = pio0;
static const uint audio_sm = 0;
static int dma_channels[AUDIO_BUFFER_COUNT];
static uint32_t buffers[AUDIO_BUFFER_COUNT][AUDIO_FRAMES_PER_BUFFER];
static volatile uint8_t ready_queue[AUDIO_BUFFER_COUNT];
static volatile uint8_t ready_read;
static volatile uint8_t ready_write;
static volatile uint8_t ready_count;
static volatile uint32_t underruns;

static void dma_irq(void) {
    uint32_t status = dma_hw->ints0;
    for (unsigned i = 0; i < AUDIO_BUFFER_COUNT; ++i) {
        uint32_t bit = 1u << (uint)dma_channels[i];
        if (status & bit) {
            dma_hw->ints0 = bit;
            if (ready_count < AUDIO_BUFFER_COUNT) {
                ready_queue[ready_write] = (uint8_t)i;
                ready_write = (uint8_t)((ready_write + 1u) % AUDIO_BUFFER_COUNT);
                ++ready_count;
            } else {
                ++underruns;
            }
        }
    }
}

void audio_i2s_init(void) {
    memset(buffers, 0, sizeof(buffers));

    uint offset = (uint)pio_add_program(audio_pio, &i2s_tx_program);
    pio_gpio_init(audio_pio, PIN_I2S_DATA);
    pio_gpio_init(audio_pio, PIN_I2S_BCLK);
    pio_gpio_init(audio_pio, PIN_I2S_LRCLK);
    pio_sm_set_consecutive_pindirs(audio_pio, audio_sm, PIN_I2S_DATA, 1, true);
    pio_sm_set_consecutive_pindirs(audio_pio, audio_sm, PIN_I2S_BCLK, 2, true);

    pio_sm_config config = i2s_tx_program_get_default_config(offset);
    sm_config_set_out_pins(&config, PIN_I2S_DATA, 1);
    sm_config_set_sideset_pins(&config, PIN_I2S_BCLK);
    sm_config_set_out_shift(&config, false, true, 32);
    sm_config_set_fifo_join(&config, PIO_FIFO_JOIN_TX);
    float divider = (float)clock_get_hz(clk_sys) /
        ((float)SYNTH_SAMPLE_RATE * 64.0f);
    sm_config_set_clkdiv(&config, divider);
    pio_sm_init(audio_pio, audio_sm, offset, &config);
    pio_sm_set_pins(audio_pio, audio_sm, 0);
    pio_sm_exec(
        audio_pio,
        audio_sm,
        pio_encode_jmp(offset + i2s_tx_offset_entry_point)
    );

    for (unsigned i = 0; i < AUDIO_BUFFER_COUNT; ++i) {
        dma_channels[i] = dma_claim_unused_channel(true);
    }
    for (unsigned i = 0; i < AUDIO_BUFFER_COUNT; ++i) {
        dma_channel_config dma_config = dma_channel_get_default_config((uint)dma_channels[i]);
        channel_config_set_transfer_data_size(&dma_config, DMA_SIZE_32);
        channel_config_set_read_increment(&dma_config, true);
        channel_config_set_write_increment(&dma_config, false);
        channel_config_set_dreq(&dma_config, pio_get_dreq(audio_pio, audio_sm, true));
        channel_config_set_chain_to(
            &dma_config,
            (uint)dma_channels[(i + 1u) % AUDIO_BUFFER_COUNT]
        );
        dma_channel_configure(
            (uint)dma_channels[i],
            &dma_config,
            &audio_pio->txf[audio_sm],
            buffers[i],
            AUDIO_FRAMES_PER_BUFFER,
            false
        );
        dma_channel_set_irq0_enabled((uint)dma_channels[i], true);
    }

    irq_set_exclusive_handler(DMA_IRQ_0, dma_irq);
    irq_set_enabled(DMA_IRQ_0, true);
    pio_sm_set_enabled(audio_pio, audio_sm, true);
    dma_start_channel_mask(1u << (uint)dma_channels[0]);
}

bool audio_i2s_take_buffer(uint32_t **frames) {
    uint32_t irq_state = save_and_disable_interrupts();
    if (ready_count == 0u) {
        restore_interrupts(irq_state);
        return false;
    }
    unsigned index = ready_queue[ready_read];
    ready_read = (uint8_t)((ready_read + 1u) % AUDIO_BUFFER_COUNT);
    --ready_count;
    restore_interrupts(irq_state);
    *frames = buffers[index];
    return true;
}

void audio_i2s_submit_buffer(uint32_t *frames) {
    unsigned index = 0u;
    while (index < AUDIO_BUFFER_COUNT && frames != buffers[index]) ++index;
    if (index == AUDIO_BUFFER_COUNT) return;
    uint channel = (uint)dma_channels[index];
    dma_channel_set_read_addr(channel, buffers[index], false);
    dma_channel_set_trans_count(channel, AUDIO_FRAMES_PER_BUFFER, false);
}

uint32_t audio_i2s_underruns(void) {
    return underruns;
}
