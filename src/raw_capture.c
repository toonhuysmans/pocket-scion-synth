#include "raw_capture.h"

#include <string.h>

#include "board_pins.h"
#include "hardware/clocks.h"
#include "hardware/dma.h"
#include "hardware/irq.h"
#include "hardware/structs/io_bank0.h"
#include "hardware/sync.h"
#include "synth.h"

#define RAW_CAPTURE_BUFFER_COUNT 2u
#define RAW_CAPTURE_FRAME_COUNT 256u

static int capture_dma_channels[RAW_CAPTURE_BUFFER_COUNT];
static int capture_dma_timer;
static uint32_t capture_buffers[RAW_CAPTURE_BUFFER_COUNT][RAW_CAPTURE_FRAME_COUNT];
static volatile uint8_t completed_buffer;
static volatile bool capture_ready;

static void capture_dma_irq(void) {
    uint32_t status = dma_hw->ints1;
    for (unsigned i = 0; i < RAW_CAPTURE_BUFFER_COUNT; ++i) {
        uint32_t bit = 1u << (uint)capture_dma_channels[i];
        if (!(status & bit)) continue;

        dma_hw->ints1 = bit;
        completed_buffer = (uint8_t)i;
        capture_ready = true;

        // The other channel is now collecting the next window. Rearm this
        // channel before it is chained again 256 samples later.
        uint channel = (uint)capture_dma_channels[i];
        dma_channel_set_write_addr(channel, capture_buffers[i], false);
        dma_channel_set_trans_count(channel, RAW_CAPTURE_FRAME_COUNT, false);
    }
}

void raw_capture_init(void) {
    memset(capture_buffers, 0, sizeof(capture_buffers));

    capture_dma_timer = dma_claim_unused_timer(true);
    uint32_t system_hz = clock_get_hz(clk_sys);
    uint32_t denominator = system_hz / SYNTH_SAMPLE_RATE;
    hard_assert(denominator <= UINT16_MAX);
    hard_assert(denominator * SYNTH_SAMPLE_RATE == system_hz);
    dma_timer_set_fraction((uint)capture_dma_timer, 1u,
                           (uint16_t)denominator);

    for (unsigned i = 0; i < RAW_CAPTURE_BUFFER_COUNT; ++i) {
        capture_dma_channels[i] = dma_claim_unused_channel(true);
    }
    for (unsigned i = 0; i < RAW_CAPTURE_BUFFER_COUNT; ++i) {
        uint channel = (uint)capture_dma_channels[i];
        dma_channel_config config = dma_channel_get_default_config(channel);
        channel_config_set_transfer_data_size(&config, DMA_SIZE_32);
        channel_config_set_read_increment(&config, false);
        channel_config_set_write_increment(&config, true);
        channel_config_set_dreq(&config,
                                dma_get_timer_dreq((uint)capture_dma_timer));
        channel_config_set_chain_to(
            &config,
            (uint)capture_dma_channels[(i + 1u) % RAW_CAPTURE_BUFFER_COUNT]);
        dma_channel_configure(
            channel,
            &config,
            capture_buffers[i],
            &io_bank0_hw->io[PIN_SENSOR].status,
            RAW_CAPTURE_FRAME_COUNT,
            false
        );
        dma_channel_set_irq1_enabled(channel, true);
    }

    irq_set_exclusive_handler(DMA_IRQ_1, capture_dma_irq);
    irq_set_enabled(DMA_IRQ_1, true);
    dma_start_channel_mask(1u << (uint)capture_dma_channels[0]);
}

bool raw_capture_copy_latest(uint32_t *destination, uint32_t frame_count) {
    if (frame_count > RAW_CAPTURE_FRAME_COUNT) return false;

    uint32_t irq_state = save_and_disable_interrupts();
    bool ready = capture_ready;
    unsigned index = completed_buffer;
    restore_interrupts(irq_state);
    if (!ready) return false;

    memcpy(destination, capture_buffers[index],
           frame_count * sizeof(destination[0]));
    return true;
}
