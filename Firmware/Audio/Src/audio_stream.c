#include "audio_stream.h"
#include "bt_rx_watchdog.h"

#include "main.h"
#include "stm32h5xx_hal_dma_ex.h"

#include <string.h>

/* 4 ms circular DMA buffer; half callbacks occur every 2 ms. */
#define AUDIO_DMA_FRAMES       192U
#define AUDIO_DMA_HALF_FRAMES   96U
#define AUDIO_DMA_SAMPLES      (AUDIO_DMA_FRAMES * 2U)
#define AUDIO_DMA_HALF_SAMPLES (AUDIO_DMA_HALF_FRAMES * 2U)

/* ~171 ms of stereo audio. Keep a useful reserve between the asynchronous BM83
 * I2S clock and the MCU codec clock so short transition/jitter gaps do not turn
 * into audible dropouts. */
#define AUDIO_FIFO_FRAMES 8192U
#define AUDIO_FIFO_MASK   (AUDIO_FIFO_FRAMES - 1U)
#define AUDIO_BT_TARGET_FRAMES 1536U
#define AUDIO_BT_LOW_FRAMES     768U
#define AUDIO_BT_HIGH_FRAMES   2304U

#if (AUDIO_FIFO_FRAMES & (AUDIO_FIFO_FRAMES - 1U)) != 0
#error AUDIO_FIFO_FRAMES must be a power of two
#endif

static int16_t s_tx_dma[AUDIO_DMA_SAMPLES] __attribute__((aligned(32)));
static int16_t s_fm_rx_dma[AUDIO_DMA_SAMPLES] __attribute__((aligned(32)));
static int32_t s_bt_rx_dma[AUDIO_DMA_SAMPLES] __attribute__((aligned(32)));
static int16_t s_fifo[AUDIO_FIFO_FRAMES * 2U] __attribute__((aligned(32)));

static DMA_NodeTypeDef s_spi1_rx_node __attribute__((aligned(32)));
static DMA_NodeTypeDef s_spi1_tx_node __attribute__((aligned(32)));
static DMA_NodeTypeDef s_spi2_rx_node __attribute__((aligned(32)));
static DMA_QListTypeDef s_spi1_rx_queue;
static DMA_QListTypeDef s_spi1_tx_queue;
static DMA_QListTypeDef s_spi2_rx_queue;

static volatile uint32_t s_fifo_head;
static volatile uint32_t s_fifo_tail;
static volatile audio_source_t s_source = AUDIO_SOURCE_NONE;
static volatile uint8_t s_volume_percent = 45U;
static volatile uint16_t s_transition_gain_permille = 1000U;
static volatile bool s_tx_started;
static volatile bool s_dma_error;
static volatile bool s_bt_rebuffer_requested;
static volatile uint32_t s_bt_rx_progress_count;
static bt_rx_watchdog_t s_bt_rx_watchdog;
static bool s_initialised;

volatile audio_stream_diag_t g_audio_stream_diag;

static int16_t scale_sample(int16_t sample)
{
    int64_t scaled = (int64_t)sample * (int64_t)s_volume_percent *
                     (int64_t)s_transition_gain_permille;
    return (int16_t)(scaled / 100000LL);
}

static uint32_t fifo_count(void)
{
    return (uint32_t)(s_fifo_head - s_fifo_tail);
}

size_t audio_stream_free_frames(void)
{
    uint32_t used = fifo_count();
    if (used >= AUDIO_FIFO_FRAMES) return 0U;
    return (size_t)(AUDIO_FIFO_FRAMES - used);
}

size_t audio_stream_queued_frames(void)
{
    uint32_t used = fifo_count();
    return used > AUDIO_FIFO_FRAMES ? AUDIO_FIFO_FRAMES : (size_t)used;
}

static void fifo_reset(void)
{
    s_fifo_head = 0U;
    s_fifo_tail = 0U;
}

static bool fifo_push_frame(int16_t left, int16_t right)
{
    uint32_t head = s_fifo_head;
    if ((uint32_t)(head - s_fifo_tail) >= AUDIO_FIFO_FRAMES) {
        /* s_fifo_tail is owned exclusively by the codec-DMA consumer ISR.
         * Advancing it here would create a two-writer read/modify/write race
         * with fifo_pop_frame(). Bluetooth cannot back-pressure its external
         * I2S clock, so on the exceptional full condition drop this newest
         * input frame instead. The adaptive high-water path normally prevents
         * this from occurring at all. */
        ++g_audio_stream_diag.fifo_overrun_count;
        return false;
    }
    uint32_t i = head & AUDIO_FIFO_MASK;
    s_fifo[i * 2U] = left;
    s_fifo[i * 2U + 1U] = right;
    s_fifo_head = head + 1U;
    return true;
}

static bool fifo_pop_frame(int16_t *left, int16_t *right)
{
    uint32_t tail = s_fifo_tail;
    if (tail == s_fifo_head) return false;
    uint32_t i = tail & AUDIO_FIFO_MASK;
    *left = s_fifo[i * 2U];
    *right = s_fifo[i * 2U + 1U];
    s_fifo_tail = tail + 1U;
    return true;
}

static void fill_tx_half(unsigned half, bool adaptive_bt)
{
    const size_t base = (size_t)half * AUDIO_DMA_HALF_SAMPLES;
    uint32_t fill = fifo_count();

    /* Long-term BM83/codec clock drift is corrected one stereo frame at a
     * time, far below the rate of the programme material. This prevents an
     * eventual FIFO over/under-run without assuming the two crystals match. */
    if (adaptive_bt && fill > AUDIO_BT_HIGH_FRAMES) {
        int16_t l, r;
        (void)fifo_pop_frame(&l, &r); /* consume one extra frame */
        fill = fifo_count();
    }
    const bool stretch_one = adaptive_bt && fill != 0U && fill < AUDIO_BT_LOW_FRAMES;

    int16_t last_l = 0, last_r = 0;
    bool have_last = false;
    bool underrun = false;
    for (size_t frame = 0U; frame < AUDIO_DMA_HALF_FRAMES; ++frame) {
        int16_t l = 0, r = 0;
        if (stretch_one && have_last && frame == (AUDIO_DMA_HALF_FRAMES - 1U)) {
            l = last_l;
            r = last_r; /* output one duplicate without consuming FIFO */
        } else if (fifo_pop_frame(&l, &r)) {
            last_l = l;
            last_r = r;
            have_last = true;
        } else if (adaptive_bt) {
            underrun = true;
        }
        s_tx_dma[base + frame * 2U] = scale_sample(l);
        s_tx_dma[base + frame * 2U + 1U] = scale_sample(r);
    }

    /* Once Bluetooth has genuinely run dry, continuing the independent codec
     * clock from an empty FIFO leaves pause/resume recovery phase-dependent.
     * Ask task context to stop only the codec side and build a fresh prebuffer;
     * the BM83-facing receiver remains armed and continues collecting samples. */
    if (underrun && adaptive_bt && s_tx_started && !s_bt_rebuffer_requested) {
        ++g_audio_stream_diag.bt_underrun_count;
        s_bt_rebuffer_requested = true;
    }
}

static void push_bt_half(unsigned half)
{
    const size_t base = (size_t)half * AUDIO_DMA_HALF_SAMPLES;
    for (size_t frame = 0U; frame < AUDIO_DMA_HALF_FRAMES; ++frame) {
        /* I2S2 is 24-bit, right-aligned in a 32-bit receive word. Sign extend
         * the 24-bit sample, then retain its most significant 16 audio bits. */
        uint32_t raw_l = (uint32_t)s_bt_rx_dma[base + frame * 2U];
        uint32_t raw_r = (uint32_t)s_bt_rx_dma[base + frame * 2U + 1U];
        int32_t s24_l = ((int32_t)(raw_l << 8)) >> 8;
        int32_t s24_r = ((int32_t)(raw_r << 8)) >> 8;
        int16_t l = (int16_t)(s24_l >> 8);
        int16_t r = (int16_t)(s24_r >> 8);
        uint16_t al = (uint16_t)(l < 0 ? -(int32_t)l : l);
        uint16_t ar = (uint16_t)(r < 0 ? -(int32_t)r : r);
        uint16_t peak = al > ar ? al : ar;
        if (peak > g_audio_stream_diag.bt_peak_abs) g_audio_stream_diag.bt_peak_abs = peak;
        (void)fifo_push_frame(l, r);
    }
}

static void process_fm_half(unsigned half)
{
    const size_t base = (size_t)half * AUDIO_DMA_HALF_SAMPLES;
    for (size_t i = 0U; i < AUDIO_DMA_HALF_SAMPLES; ++i) {
        s_tx_dma[base + i] = scale_sample(s_fm_rx_dma[base + i]);
    }
}

static void dma_audio_error(DMA_HandleTypeDef *hdma)
{
    (void)hdma;
    ++g_audio_stream_diag.dma_error_count;
    s_dma_error = true;
}

/* SPI1 is configured as master full-duplex I2S because the TLV320AIC3104
 * shares one clock domain for DAC playback and ADC capture.  Even for local
 * and Bluetooth playback the receive side must be drained continuously;
 * otherwise RX can overrun and the H5 SPI/I2S engine can stop issuing TX DMA
 * requests while the DMA handle still looks healthy.  Drive both directions
 * for every source and use the RX DMA boundary as the authoritative point at
 * which the matching TX half is safe to refill. */
static void dma_codec_half(DMA_HandleTypeDef *hdma)
{
    (void)hdma;
    ++g_audio_stream_diag.tx_half_count;
    if (s_source == AUDIO_SOURCE_LOCAL) fill_tx_half(0U, false);
    else if (s_source == AUDIO_SOURCE_BLUETOOTH) fill_tx_half(0U, true);
    else if (s_source == AUDIO_SOURCE_FM) process_fm_half(0U);
}

static void dma_codec_full(DMA_HandleTypeDef *hdma)
{
    (void)hdma;
    ++g_audio_stream_diag.tx_full_count;
    if (s_source == AUDIO_SOURCE_LOCAL) fill_tx_half(1U, false);
    else if (s_source == AUDIO_SOURCE_BLUETOOTH) fill_tx_half(1U, true);
    else if (s_source == AUDIO_SOURCE_FM) process_fm_half(1U);
}

static void dma_bt_rx_half(DMA_HandleTypeDef *hdma)
{
    (void)hdma;
    ++s_bt_rx_progress_count;
    ++g_audio_stream_diag.bt_rx_half_count;
    if (s_source == AUDIO_SOURCE_BLUETOOTH) push_bt_half(0U);
}

static void dma_bt_rx_full(DMA_HandleTypeDef *hdma)
{
    (void)hdma;
    ++s_bt_rx_progress_count;
    ++g_audio_stream_diag.bt_rx_full_count;
    if (s_source == AUDIO_SOURCE_BLUETOOTH) push_bt_half(1U);
}

static HAL_StatusTypeDef configure_circular_channel(DMA_HandleTypeDef *hdma,
                                                     DMA_NodeTypeDef *node,
                                                     DMA_QListTypeDef *queue,
                                                     uint32_t request,
                                                     uint32_t direction,
                                                     uint32_t src_inc,
                                                     uint32_t dest_inc,
                                                     uint32_t src_width,
                                                     uint32_t dest_width)
{
    if (hdma == NULL || node == NULL || queue == NULL || hdma->Instance == NULL) return HAL_ERROR;
    DMA_Channel_TypeDef *instance = hdma->Instance;
    void *parent = hdma->Parent;

    (void)HAL_DMA_DeInit(hdma);
    memset(node, 0, sizeof(*node));
    memset(queue, 0, sizeof(*queue));

    DMA_NodeConfTypeDef cfg = {0};
    cfg.NodeType = DMA_GPDMA_LINEAR_NODE;
    cfg.Init.Request = request;
    cfg.Init.BlkHWRequest = DMA_BREQ_SINGLE_BURST;
    cfg.Init.Direction = direction;
    cfg.Init.SrcInc = src_inc;
    cfg.Init.DestInc = dest_inc;
    cfg.Init.SrcDataWidth = src_width;
    cfg.Init.DestDataWidth = dest_width;
    cfg.Init.Priority = DMA_HIGH_PRIORITY;
    cfg.Init.SrcBurstLength = 1U;
    cfg.Init.DestBurstLength = 1U;
    cfg.Init.TransferAllocatedPort = DMA_SRC_ALLOCATED_PORT0 | DMA_DEST_ALLOCATED_PORT0;
    cfg.Init.TransferEventMode = DMA_TCEM_BLOCK_TRANSFER;
    cfg.Init.Mode = DMA_NORMAL;
    cfg.TriggerConfig.TriggerPolarity = DMA_TRIG_POLARITY_MASKED;
    cfg.DataHandlingConfig.DataExchange = DMA_EXCHANGE_NONE;
    cfg.DataHandlingConfig.DataAlignment = DMA_DATA_RIGHTALIGN_ZEROPADDED;

    if (HAL_DMAEx_List_BuildNode(&cfg, node) != HAL_OK) return HAL_ERROR;
    if (HAL_DMAEx_List_InsertNode_Tail(queue, node) != HAL_OK) return HAL_ERROR;
    if (HAL_DMAEx_List_SetCircularMode(queue) != HAL_OK) return HAL_ERROR;

    memset(&hdma->InitLinkedList, 0, sizeof(hdma->InitLinkedList));
    hdma->Instance = instance;
    hdma->Parent = parent;
    hdma->InitLinkedList.Priority = DMA_HIGH_PRIORITY;
    hdma->InitLinkedList.LinkStepMode = DMA_LSM_FULL_EXECUTION;
    hdma->InitLinkedList.LinkAllocatedPort = DMA_LINK_ALLOCATED_PORT0;
    hdma->InitLinkedList.TransferEventMode = DMA_TCEM_BLOCK_TRANSFER;
    hdma->InitLinkedList.LinkedListMode = DMA_LINKEDLIST_CIRCULAR;
    if (HAL_DMAEx_List_Init(hdma) != HAL_OK) return HAL_ERROR;
    if (HAL_DMAEx_List_LinkQ(hdma, queue) != HAL_OK) return HAL_ERROR;
    hdma->Parent = parent;
    return HAL_DMA_ConfigChannelAttributes(hdma, DMA_CHANNEL_NPRIV);
}

static HAL_StatusTypeDef start_codec_dma(void)
{
    /* Always run SPI1 in full-duplex mode.  For local/Bluetooth the RX buffer
     * is intentionally discarded; for FM it contains the codec ADC samples.
     * STM32H5 HAL interprets Size as the number of 16-bit data samples here. */
    memset(s_fm_rx_dma, 0, sizeof(s_fm_rx_dma));
    /* HAL_I2S_DMAStop() does not clear sticky SPI/I2S underrun/overrun flags.
     * A previous source transition or debugger stop can therefore poison the
     * next stream even though both DMA channels start successfully. */
    __HAL_I2S_CLEAR_OVRFLAG(&hi2s1);
    __HAL_I2S_CLEAR_UDRFLAG(&hi2s1);
    HAL_StatusTypeDef st = HAL_I2SEx_TransmitReceive_DMA(&hi2s1,
                                                        (uint16_t *)s_tx_dma,
                                                        (uint16_t *)s_fm_rx_dma,
                                                        (uint16_t)AUDIO_DMA_SAMPLES);
    if (st != HAL_OK) return st;
    /* HAL installs its normal-transfer callbacks even though these H5 GPDMA
     * channels are linked-list circular. Replace them before the first 2 ms
     * boundary so HAL never tears down the stream at a full-buffer event. */
    hdma_spi1_tx.XferHalfCpltCallback = NULL;
    hdma_spi1_tx.XferCpltCallback = NULL;
    hdma_spi1_tx.XferErrorCallback = dma_audio_error;
    hdma_spi1_rx.XferHalfCpltCallback = dma_codec_half;
    hdma_spi1_rx.XferCpltCallback = dma_codec_full;
    hdma_spi1_rx.XferErrorCallback = dma_audio_error;
    s_tx_started = true;
    return HAL_OK;
}

static HAL_StatusTypeDef start_bt_rx_dma(void)
{
    /* A stopped external BM83 clock can leave a stale receive-overrun flag on
     * SPI2. Clear it before arming the next linked-list transaction. */
    __HAL_I2S_CLEAR_OVRFLAG(&hi2s2);
    HAL_StatusTypeDef st = HAL_I2S_Receive_DMA(&hi2s2, (uint16_t *)s_bt_rx_dma,
                                              (uint16_t)AUDIO_DMA_SAMPLES);
    if (st != HAL_OK) return st;
    hdma_spi2_rx.XferHalfCpltCallback = dma_bt_rx_half;
    hdma_spi2_rx.XferCpltCallback = dma_bt_rx_full;
    hdma_spi2_rx.XferErrorCallback = dma_audio_error;
    return HAL_OK;
}

static void stop_bt_rx_dma(void)
{
    /* A stale linked-list transfer can leave the I2S handle READY even though
     * the GPDMA channel is still enabled. Include the hardware enable bit in the
     * stop decision so a watchdog rearm always starts from a known state. */
    if (hi2s2.State != HAL_I2S_STATE_READY ||
        (hdma_spi2_rx.Instance != NULL && (hdma_spi2_rx.Instance->CCR & DMA_CCR_EN) != 0U)) {
        (void)HAL_I2S_DMAStop(&hi2s2);
    }
    __HAL_I2S_CLEAR_OVRFLAG(&hi2s2);
}

static void stop_codec_dma(void)
{
    if (hi2s1.State != HAL_I2S_STATE_READY) (void)HAL_I2S_DMAStop(&hi2s1);
    __HAL_I2S_CLEAR_OVRFLAG(&hi2s1);
    __HAL_I2S_CLEAR_UDRFLAG(&hi2s1);
    s_tx_started = false;
}

static dev_status_t rearm_bluetooth_rx(bool preserve_stall_watchdog)
{
    if (!s_initialised) return DEV_ENOTREADY;
    if (s_source != AUDIO_SOURCE_BLUETOOTH) return DEV_ESTATE;

    /* Report_Type_Codec/Prepare marks an I2S clock-domain transition. A stale
     * slave receive transaction does not reliably survive that gap on H523,
     * and retaining old FIFO audio makes resume timing phase-dependent. Stop
     * both DMA sides at this transition, discard stale PCM, then arm SPI2 and
    * let normal prebuffering restart codec output cleanly. */
    stop_codec_dma();
    stop_bt_rx_dma();
    fifo_reset();
    memset(s_tx_dma, 0, sizeof(s_tx_dma));
    s_bt_rebuffer_requested = false;
    ++g_audio_stream_diag.bt_rearm_count;
    dev_status_t st = start_bt_rx_dma() == HAL_OK ? DEV_OK : DEV_EIO;
    if (!preserve_stall_watchdog) {
        /* A Type_Codec/Prepare rearm can itself be followed by a long clock
         * gap. Keep a bounded recovery watch active until RX really prebuffers
         * enough audio to restart codec TX; this also covers a later resume for
         * which the BM83 omits the matching Prepare notification. */
        bt_rx_watchdog_arm_rebuffer(&s_bt_rx_watchdog, HAL_GetTick(),
                                    s_bt_rx_progress_count);
    }
    return st;
}

dev_status_t audio_stream_rearm_bluetooth_rx(void)
{
    return rearm_bluetooth_rx(false);
}

static void stop_dma(void)
{
    stop_bt_rx_dma();
    stop_codec_dma();
}

dev_status_t audio_stream_init(void)
{
    if (s_initialised) return DEV_OK;
    if (configure_circular_channel(&hdma_spi1_rx, &s_spi1_rx_node, &s_spi1_rx_queue,
                                   GPDMA1_REQUEST_SPI1_RX, DMA_PERIPH_TO_MEMORY,
                                   DMA_SINC_FIXED, DMA_DINC_INCREMENTED,
                                   DMA_SRC_DATAWIDTH_HALFWORD, DMA_DEST_DATAWIDTH_HALFWORD) != HAL_OK) return DEV_EIO;
    if (configure_circular_channel(&hdma_spi1_tx, &s_spi1_tx_node, &s_spi1_tx_queue,
                                   GPDMA1_REQUEST_SPI1_TX, DMA_MEMORY_TO_PERIPH,
                                   DMA_SINC_INCREMENTED, DMA_DINC_FIXED,
                                   DMA_SRC_DATAWIDTH_HALFWORD, DMA_DEST_DATAWIDTH_HALFWORD) != HAL_OK) return DEV_EIO;
    if (configure_circular_channel(&hdma_spi2_rx, &s_spi2_rx_node, &s_spi2_rx_queue,
                                   GPDMA1_REQUEST_SPI2_RX, DMA_PERIPH_TO_MEMORY,
                                   DMA_SINC_FIXED, DMA_DINC_INCREMENTED,
                                   DMA_SRC_DATAWIDTH_WORD, DMA_DEST_DATAWIDTH_WORD) != HAL_OK) return DEV_EIO;

    fifo_reset();
    memset(s_tx_dma, 0, sizeof(s_tx_dma));
    memset(s_fm_rx_dma, 0, sizeof(s_fm_rx_dma));
    memset(s_bt_rx_dma, 0, sizeof(s_bt_rx_dma));
    s_dma_error = false;
    s_bt_rebuffer_requested = false;
    s_bt_rx_progress_count = 0U;
    s_source = AUDIO_SOURCE_NONE;
    memset((void *)&g_audio_stream_diag, 0, sizeof(g_audio_stream_diag));
    bt_rx_watchdog_reset(&s_bt_rx_watchdog, HAL_GetTick(), s_bt_rx_progress_count);
    s_initialised = true;
    return DEV_OK;
}

void audio_stream_set_volume(uint8_t percent)
{
    s_volume_percent = percent > 100U ? 100U : percent;
}

void audio_stream_set_transition_gain(uint16_t permille)
{
    s_transition_gain_permille = permille > 1000U ? 1000U : permille;
}

dev_status_t audio_stream_set_source(audio_source_t source)
{
    if (!s_initialised) return DEV_ENOTREADY;
    if (source > AUDIO_SOURCE_BLUETOOTH) return DEV_EINVAL;
    if (source == AUDIO_SOURCE_NONE) return audio_stream_suspend();
    if (s_source == source && !s_dma_error) return DEV_OK;

    stop_dma();
    fifo_reset();
    memset(s_tx_dma, 0, sizeof(s_tx_dma));
    s_dma_error = false;
    s_bt_rebuffer_requested = false;
    s_source = source;
    g_audio_stream_diag.source = (uint8_t)source;
    g_audio_stream_diag.bt_peak_abs = 0U;

    if (source == AUDIO_SOURCE_LOCAL) {
        return start_codec_dma() == HAL_OK ? DEV_OK : DEV_EIO;
    }
    if (source == AUDIO_SOURCE_BLUETOOTH) {
        /* Prebuffer before starting the independent MCU-master TX clock. */
        bt_rx_watchdog_reset(&s_bt_rx_watchdog, HAL_GetTick(), s_bt_rx_progress_count);
        return start_bt_rx_dma() == HAL_OK ? DEV_OK : DEV_EIO;
    }
    return start_codec_dma() == HAL_OK ? DEV_OK : DEV_EIO;
}

dev_status_t audio_stream_suspend(void)
{
    if (!s_initialised) return DEV_ENOTREADY;
    stop_dma();
    fifo_reset();
    memset(s_tx_dma, 0, sizeof(s_tx_dma));
    s_dma_error = false;
    s_bt_rebuffer_requested = false;
    s_source = AUDIO_SOURCE_NONE;
    g_audio_stream_diag.source = (uint8_t)AUDIO_SOURCE_NONE;
    g_audio_stream_diag.fifo_frames = 0U;
    g_audio_stream_diag.tx_started = 0U;
    return DEV_OK;
}

dev_status_t audio_stream_write_stereo(const int16_t *interleaved, size_t frames)
{
    if (interleaved == NULL || frames == 0U) return DEV_EINVAL;
    if (!s_initialised || s_source != AUDIO_SOURCE_LOCAL) return DEV_ESTATE;
    if (frames > audio_stream_free_frames()) return DEV_EBUSY;
    for (size_t i = 0U; i < frames; ++i) {
        /* Free-space is checked for the whole block above. Local playback is
         * the only producer in this source mode, so a push failure here would
         * indicate an invariant violation rather than normal back-pressure. */
        if (!fifo_push_frame(interleaved[i * 2U], interleaved[i * 2U + 1U])) {
            return DEV_EBUSY;
        }
    }
    return DEV_OK;
}

dev_status_t audio_stream_bench_direct_speaker_tone(uint32_t duration_ms)
{
    if (!s_initialised || duration_ms == 0U || duration_ms > 10000U) return DEV_EINVAL;

    /* Preserve the active route. The direct test intentionally strips the
     * transport down to the same essentials as the known-working Dash-style
     * MAX98357 path: master TX, Philips I2S, 16-bit stereo, no MCLK dependency,
     * and blocking writes of already-interleaved samples. */
    const audio_source_t saved_source = s_source;
    const uint32_t saved_mode = hi2s1.Init.Mode;
    const uint32_t saved_mclk = hi2s1.Init.MCLKOutput;

    stop_dma();
    fifo_reset();
    memset(s_tx_dma, 0, sizeof(s_tx_dma));

    hi2s1.Init.Mode = I2S_MODE_MASTER_TX;
    hi2s1.Init.Standard = I2S_STANDARD_PHILIPS;
    hi2s1.Init.DataFormat = I2S_DATAFORMAT_16B;
    hi2s1.Init.MCLKOutput = I2S_MCLKOUTPUT_DISABLE;
    hi2s1.Init.AudioFreq = I2S_AUDIOFREQ_48K;
    hi2s1.Init.CPOL = I2S_CPOL_LOW;
    hi2s1.Init.FirstBit = I2S_FIRSTBIT_MSB;
    hi2s1.Init.WSInversion = I2S_WS_INVERSION_DISABLE;
    if (HAL_I2S_Init(&hi2s1) != HAL_OK) {
        hi2s1.Init.Mode = saved_mode;
        hi2s1.Init.MCLKOutput = saved_mclk;
        (void)HAL_I2S_Init(&hi2s1);
        return DEV_EIO;
    }

    enum { BENCH_FRAMES = 192U };
    int16_t tone[BENCH_FRAMES * 2U];
    for (size_t frame = 0U; frame < BENCH_FRAMES; ++frame) {
        /* 1 kHz at 48 kHz, L=R. +/-12000 gives an unmistakable signal while
         * retaining almost 9 dB of digital headroom. With the fitted 634 kOhm
         * SD_MODE resistor the MAX98357A selects (L/2 + R/2), so equal left and
         * right samples reproduce the original amplitude. */
        int16_t sample = ((frame / 24U) & 1U) != 0U ? 12000 : -12000;
        tone[frame * 2U] = sample;
        tone[frame * 2U + 1U] = sample;
    }

    dev_status_t result = DEV_OK;
    uint32_t started = HAL_GetTick();
    while ((uint32_t)(HAL_GetTick() - started) < duration_ms) {
        HAL_StatusTypeDef hs = HAL_I2S_Transmit(&hi2s1, (uint16_t *)tone,
                                               (uint16_t)(BENCH_FRAMES * 2U), 50U);
        if (hs != HAL_OK) {
            result = (hs == HAL_TIMEOUT) ? DEV_ETIMEOUT : DEV_EIO;
            break;
        }
        /* The bench call occupies the application task for up to a few seconds;
         * keep the production watchdog alive while the deterministic tone runs. */
        (void)HAL_IWDG_Refresh(&hiwdg);
    }

    (void)HAL_I2S_DeInit(&hi2s1);
    hi2s1.Init.Mode = saved_mode;
    hi2s1.Init.MCLKOutput = saved_mclk;
    if (HAL_I2S_Init(&hi2s1) != HAL_OK) return DEV_EIO;

    /* HAL_I2S_DeInit() deinitialises the linked DMA handles through MSP. Rebuild
     * the two codec-side circular channels before resuming the production path. */
    if (configure_circular_channel(&hdma_spi1_rx, &s_spi1_rx_node, &s_spi1_rx_queue,
                                   GPDMA1_REQUEST_SPI1_RX, DMA_PERIPH_TO_MEMORY,
                                   DMA_SINC_FIXED, DMA_DINC_INCREMENTED,
                                   DMA_SRC_DATAWIDTH_HALFWORD, DMA_DEST_DATAWIDTH_HALFWORD) != HAL_OK) return DEV_EIO;
    if (configure_circular_channel(&hdma_spi1_tx, &s_spi1_tx_node, &s_spi1_tx_queue,
                                   GPDMA1_REQUEST_SPI1_TX, DMA_MEMORY_TO_PERIPH,
                                   DMA_SINC_INCREMENTED, DMA_DINC_FIXED,
                                   DMA_SRC_DATAWIDTH_HALFWORD, DMA_DEST_DATAWIDTH_HALFWORD) != HAL_OK) return DEV_EIO;

    s_dma_error = false;
    if (saved_source == AUDIO_SOURCE_LOCAL || saved_source == AUDIO_SOURCE_FM) {
        if (start_codec_dma() != HAL_OK) return DEV_EIO;
    } else if (saved_source == AUDIO_SOURCE_BLUETOOTH) {
        if (start_bt_rx_dma() != HAL_OK) return DEV_EIO;
    }
    return result;
}

void audio_stream_service(void)
{
    if (!s_initialised) return;
    const uint32_t now = HAL_GetTick();
    g_audio_stream_diag.fifo_frames = fifo_count();
    g_audio_stream_diag.tx_started = s_tx_started ? 1U : 0U;

    if (s_source == AUDIO_SOURCE_BLUETOOTH && s_bt_rebuffer_requested) {
        /* The ISR only raises the request. HAL DMA stop/reconfiguration stays
         * in task context, while SPI2 keeps filling the existing FIFO. */
        s_bt_rebuffer_requested = false;
        if (s_tx_started) {
            stop_codec_dma();
            ++g_audio_stream_diag.bt_rebuffer_count;
            bt_rx_watchdog_arm_rebuffer(&s_bt_rx_watchdog, now, s_bt_rx_progress_count);
        }
    }

    if (s_source == AUDIO_SOURCE_BLUETOOTH) {
        uint32_t rx_sr = hi2s2.Instance->SR;
        bool rx_pending = (rx_sr & (SPI_SR_RXP | SPI_SR_RXWNE | SPI_SR_OVR)) != 0U;
        g_audio_stream_diag.bt_last_rx_sr = rx_sr;
        if (bt_rx_watchdog_should_rearm(&s_bt_rx_watchdog, now,
                                        s_bt_rx_progress_count,
                                        fifo_count() < AUDIO_BT_TARGET_FRAMES,
                                        s_tx_started,
                                        rx_pending)) {
            /* This is deliberately task-context only. During a real pause the
             * first attempt occurs after 1 s, then quiet retries back off to
             * 2/4 s. If clocks return into a stale peripheral and RX data/OVR is
             * visible, retry can occur after 500 ms instead. */
            g_audio_stream_diag.bt_last_stall_rearm_ms = now;
            dev_status_t rearm_st = rearm_bluetooth_rx(true);
            if (rearm_st == DEV_OK) ++g_audio_stream_diag.bt_stall_rearm_count;
            else ++g_audio_stream_diag.bt_stall_rearm_fail_count;
        }
        g_audio_stream_diag.bt_last_progress_ms = s_bt_rx_watchdog.last_progress_ms;
        g_audio_stream_diag.bt_stall_rearm_attempts = s_bt_rx_watchdog.rearm_attempts;
    }

    /* A peripheral overrun or an unexpected HAL transition can leave an H5
     * linked-list handle BUSY while the channel itself is no longer running.
     * Detect that silent-stall state explicitly so audio recovers rather than
     * reporting success while transmitting zeros forever. */
    if (s_tx_started) {
        uint32_t fault = 0U;
        uint32_t tx_ccr = hdma_spi1_tx.Instance->CCR;
        uint32_t rx_ccr = hdma_spi1_rx.Instance->CCR;
        uint32_t cfg1 = hi2s1.Instance->CFG1;
        uint32_t cr1 = hi2s1.Instance->CR1;
        uint32_t sr = hi2s1.Instance->SR;

        /* On STM32H523 the master full-duplex I2S engine can latch UDR/OVR
         * during the first few clocks while the two GPDMA linked lists enter
         * circular operation.  The streams remain completely healthy (both
         * DMA channels enabled, DMA requests asserted and CSTART still set).
         * Treat these FIFO flags as recoverable status and clear them in
         * place; tearing the stream down here discards the Bluetooth
         * prebuffer and creates an avoidable audio dropout. */
        if ((sr & SPI_SR_OVR) != 0U) __HAL_I2S_CLEAR_OVRFLAG(&hi2s1);
        if ((sr & SPI_SR_UDR) != 0U) __HAL_I2S_CLEAR_UDRFLAG(&hi2s1);

        if ((tx_ccr & DMA_CCR_EN) == 0U) fault |= 1U << 0;
        if ((rx_ccr & DMA_CCR_EN) == 0U) fault |= 1U << 1;
        if ((cfg1 & (SPI_CFG1_TXDMAEN | SPI_CFG1_RXDMAEN)) !=
            (SPI_CFG1_TXDMAEN | SPI_CFG1_RXDMAEN)) fault |= 1U << 2;
        if ((cr1 & SPI_CR1_CSTART) == 0U) fault |= 1U << 3;
        if ((sr & (SPI_SR_TIFRE | SPI_SR_MODF)) != 0U) fault |= 1U << 4;
        if (fault != 0U) {
            g_audio_stream_diag.transport_last_fault = fault;
            g_audio_stream_diag.transport_last_cr1 = cr1;
            g_audio_stream_diag.transport_last_cfg1 = cfg1;
            g_audio_stream_diag.transport_last_sr = sr;
            g_audio_stream_diag.transport_last_tx_ccr = tx_ccr;
            g_audio_stream_diag.transport_last_rx_ccr = rx_ccr;
        ++g_audio_stream_diag.transport_recovery_count;
        s_dma_error = true;
        }
    }
    if (s_dma_error) {
        audio_source_t source = s_source;
        s_dma_error = false;
        s_source = AUDIO_SOURCE_NONE;
        if (audio_stream_set_source(source) != DEV_OK) {
            /* Preserve the recovery request if reconfiguration itself fails.
             * Without this, one transient HAL start failure leaves audio
             * permanently stopped while diagnostics report no pending error. */
            s_dma_error = true;
        }
        return;
    }
    if (s_source == AUDIO_SOURCE_BLUETOOTH && !s_tx_started &&
        fifo_count() >= AUDIO_BT_TARGET_FRAMES) {
        fill_tx_half(0U, true);
        fill_tx_half(1U, true);
        if (start_codec_dma() != HAL_OK) {
            s_dma_error = true;
        } else {
            bt_rx_watchdog_reset(&s_bt_rx_watchdog, now, s_bt_rx_progress_count);
        }
    }
}
