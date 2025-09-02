#include "Ifx_Types.h"
#include "IfxCpu.h"
#include "IfxScuWdt.h"
#include "IfxAsclin_Asc.h"
#include "IfxPort.h"
#include "IfxCpu_Irq.h"
#include "IfxCan_Can.h"
#include "Bsp.h"
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

/********************************* Board / Pins *********************************/
/* UART0: P14.0 (TX), P14.1 (RX) */
#define UART_BAUDRATE        115200
#define UART_TX_PIN          IfxAsclin0_TX_P14_0_OUT
#define UART_RX_PIN          IfxAsclin0_RXA_P14_1_IN
#define INTPRIO_ASCLIN_TX    19
#define INTPRIO_ASCLIN_RX    18
#define ASC_TX_BUFFER_SIZE   256
#define ASC_RX_BUFFER_SIZE   64

/* CAN0: TXD P20.8, RXD P20.7; TLE9251V STB on P20.6 (LOW = Normal) */
#define CAN_BAUDRATE         500000

/* Enable this to transmit test frames periodically so you can see them in PCAN-View. */
#define ENABLE_TEST_TX       1

/********************************* Signals *********************************/
/* Bit layout (Intel/LSB0) */
#define VOLT_START   6u
#define VOLT_LEN     12u
#define VOLT_DIV     100.0f   /* V = raw/100 */

#define CURR_START   16u
#define CURR_LEN     8u
#define CURR_DIV     10.0f    /* A = raw/10 */

#define TEMP_START   32u
#define TEMP_LEN     12u
#define TEMP_DIV     10.0f    /* C = raw/10 */

#define ID_VOLT      0x400
#define ID_CURR      0x500
#define ID_TEMP      0x600
/* ---- WARNING thresholds ---- */
#define VOLT_WARN  15.0f   /* V */
#define CURR_WARN  10.0f   /* A */
#define TEMP_WARN  42.0f   /* C */

/******************************* Globals ***********************************/
static IfxAsclin_Asc g_asc;
static uint8 g_ascTxBuf[ASC_TX_BUFFER_SIZE + sizeof(Ifx_Fifo) + 8];
static uint8 g_ascRxBuf[ASC_RX_BUFFER_SIZE + sizeof(Ifx_Fifo) + 8];

static IfxCan_Can      g_canMod;
static IfxCan_Can_Node g_canNode;
static IfxCan_Message  g_rxMsg;
static uint32          g_rxData[2];   /* 8 byte */

IFX_ALIGN(4) IfxCpu_syncEvent cpuSyncEvent = 0;

/******************************* UART ****************************************/
IFX_INTERRUPT(asclin0_Tx_ISR, 0, INTPRIO_ASCLIN_TX);
void asclin0_Tx_ISR(void) { IfxAsclin_Asc_isrTransmit(&g_asc); }

IFX_INTERRUPT(asclin0_Rx_ISR, 0, INTPRIO_ASCLIN_RX);
void asclin0_Rx_ISR(void) { IfxAsclin_Asc_isrReceive(&g_asc); }

static void uart_init(void)
{
    IfxAsclin_Asc_Config cfg;  IfxAsclin_Asc_initModuleConfig(&cfg, &MODULE_ASCLIN0);
    cfg.baudrate.baudrate   = UART_BAUDRATE;
    cfg.baudrate.oversampling = IfxAsclin_OversamplingFactor_16;
    cfg.interrupt.txPriority = INTPRIO_ASCLIN_TX;
    cfg.interrupt.rxPriority = INTPRIO_ASCLIN_RX;
    cfg.interrupt.typeOfService = IfxCpu_Irq_getTos(IfxCpu_getCoreIndex());
    cfg.rxBuffer = g_ascRxBuf; cfg.rxBufferSize = ASC_RX_BUFFER_SIZE;
    cfg.txBuffer = g_ascTxBuf; cfg.txBufferSize = ASC_TX_BUFFER_SIZE;

    static const IfxAsclin_Asc_Pins pins = {
        .cts       = NULL_PTR,                  .ctsMode = IfxPort_InputMode_pullUp,
        .rx        = &UART_RX_PIN,              .rxMode  = IfxPort_InputMode_pullUp,
        .rts       = NULL_PTR,                  .rtsMode = IfxPort_OutputMode_pushPull,
        .tx        = &UART_TX_PIN,              .txMode  = IfxPort_OutputMode_pushPull
    };
    cfg.pins = &pins;
    IfxAsclin_Asc_initModule(&g_asc, &cfg);
}

static void uart_write(const uint8 *data, Ifx_SizeT len)
{ IfxAsclin_Asc_write(&g_asc, data, &len, TIME_INFINITE); }

static void uart_printf(const char *fmt, ...)
{
    char buf[160];
    va_list ap; va_start(ap, fmt);
    (void)vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    Ifx_SizeT n = (Ifx_SizeT)strlen(buf);
    uart_write((const uint8*)buf, n);
}

/**************************** Bit helpers (Intel/LSB0) *************************/
static inline void set_bits_le(uint8 *buf, uint32 startBit, uint32 len, uint32 value)
{
    for (uint32 i = 0; i < len; ++i) {
        uint32 bitPos = startBit + i;
        uint32 byteIx = bitPos >> 3;
        uint32 bitIx  = bitPos & 7;
        uint8  mask   = (uint8)(1u << bitIx);
        if (value & (1u << i)) buf[byteIx] |= mask; else buf[byteIx] &= (uint8)~mask;
    }
}

static inline uint32 get_bits_le(const uint8 *buf, uint32 startBit, uint32 len)
{
    uint32 v = 0;
    for (uint32 i = 0; i < len; ++i) {
        uint32 bitPos = startBit + i;
        uint32 byteIx = bitPos >> 3;
        uint32 bitIx  = bitPos & 7;
        v |= ((buf[byteIx] >> bitIx) & 1u) << i;
    }
    return v;
}

/******************************* CAN *****************************************/
static void can_init(void)
{
    /* CAN module */
    IfxCan_Can_Config mcfg; IfxCan_Can_initModuleConfig(&mcfg, &MODULE_CAN0);
    IfxCan_Can_initModule(&g_canMod, &mcfg);

    /* Enable transceiver: STB -> LOW */
    IfxPort_setPinModeOutput(&MODULE_P20, 6, IfxPort_OutputMode_pushPull, IfxPort_OutputIdx_general);
    IfxPort_setPinLow(&MODULE_P20, 6);

    /* Node config */
    IfxCan_Can_NodeConfig n; IfxCan_Can_initNodeConfig(&n, &g_canMod);
    n.nodeId                   = IfxCan_NodeId_0;
    n.frame.type               = IfxCan_FrameType_transmitAndReceive;
    n.frame.mode               = IfxCan_FrameMode_standard;          /* Classic */
    n.calculateBitTimingValues = TRUE;
    n.baudRate.baudrate        = CAN_BAUDRATE;                       /* 500k */
    n.baudRate.samplePoint     = 8000;                               /* 80.00% */
    n.baudRate.syncJumpWidth   = 3;

    /* TX buffers */
    n.txConfig.txMode                   = IfxCan_TxMode_dedicatedBuffers;
    n.txConfig.dedicatedTxBuffersNumber = 1;
    n.txConfig.txBufferDataFieldSize    = IfxCan_DataFieldSize_8;

    /* RX FIFO0 accept-all */
    n.rxConfig.rxMode               = IfxCan_RxMode_fifo0;
    n.rxConfig.rxFifo0Size          = 16;
    n.rxConfig.rxFifo0DataFieldSize = IfxCan_DataFieldSize_8;
    n.rxConfig.rxFifo0OperatingMode = IfxCan_RxFifoMode_blocking;

    n.filterConfig.messageIdLength                    = IfxCan_MessageIdLength_both;
    n.filterConfig.standardListSize                   = 0;
    n.filterConfig.extendedListSize                   = 0;
    n.filterConfig.standardFilterForNonMatchingFrames = IfxCan_NonMatchingFrame_acceptToRxFifo0;
    n.filterConfig.extendedFilterForNonMatchingFrames = IfxCan_NonMatchingFrame_acceptToRxFifo0;

    static const IfxCan_Can_Pins pins = {
        .txPin     = &IfxCan_TXD00_P20_8_OUT,
        .txPinMode = IfxPort_OutputMode_pushPull,
        .rxPin     = &IfxCan_RXD00B_P20_7_IN,
        .rxPinMode = IfxPort_InputMode_pullUp,
        .padDriver = IfxPort_PadDriver_cmosAutomotiveSpeed2
    };
    n.pins = &pins;

    IfxCan_Can_initNode(&g_canNode, &n);

    /* RX template: FIFO0 */
    IfxCan_Can_initMessage(&g_rxMsg);
    g_rxMsg.readFromRxFifo0 = TRUE;
    g_rxMsg.dataLengthCode  = IfxCan_DataLengthCode_8;
}

/******************************* RX → UART ***********************************/
static void log_frame_to_uart(uint32 id, const uint8 *p)
{
    switch (id)
    {
        case ID_VOLT: {
            uint32 raw = get_bits_le(p, VOLT_START, VOLT_LEN);
            float volt = raw / VOLT_DIV;

            if (volt >= VOLT_WARN) {
                uart_printf("RX 0x%03X  VOLT=%.2f V  (raw=%lu)  WARNING\r\n",
                            id, volt, (unsigned long)raw);
            } else {
                uart_printf("RX 0x%03X  VOLT=%.2f V  (raw=%lu)\r\n",
                            id, volt, (unsigned long)raw);
            }
        } break;

        case ID_CURR: {
            uint32 raw = get_bits_le(p, CURR_START, CURR_LEN);
            float curr = raw / CURR_DIV;

            if (curr >= CURR_WARN) {
                uart_printf("RX 0x%03X  CURR=%.1f A  (raw=%lu)  WARNING\r\n",
                            id, curr, (unsigned long)raw);
            } else {
                uart_printf("RX 0x%03X  CURR=%.1f A  (raw=%lu)\r\n",
                            id, curr, (unsigned long)raw);
            }
        } break;

        case ID_TEMP: {
            uint32 raw = get_bits_le(p, TEMP_START, TEMP_LEN);
            float temp = raw / TEMP_DIV;

            if (temp > TEMP_WARN) {
                uart_printf("RX 0x%03X  TEMP=%.1f C  (raw=%lu)  WARNING\r\n",
                            id, temp, (unsigned long)raw);
            } else {
                uart_printf("RX 0x%03X  TEMP=%.1f C  (raw=%lu)\r\n",
                            id, temp, (unsigned long)raw);
            }
        } break;

        default: {
            uart_printf("RX 0x%03X  [ignored]\r\n", id);
        } break;
    }
}

static void can_poll_and_print(void)
{
    while (IfxCan_Node_getRxFifo0FillLevel(g_canNode.node) > 0)
    {
        (void)IfxCan_Can_readMessage(&g_canNode, &g_rxMsg, (uint32*)g_rxData);
        const uint8 *p = (const uint8*)g_rxData;
        log_frame_to_uart(g_rxMsg.messageId, p);
    }
}

/*************************** Optional test TX ********************************/
#if ENABLE_TEST_TX
static void can_send_test_signals(void)
{
    IfxCan_Message tx; IfxCan_Can_initMessage(&tx);
    tx.dataLengthCode = IfxCan_DataLengthCode_8;
    uint8 payload[8];

    static float v=12.0f, i=8.0f, t=25.0f;

    /* 0x400 Volt */
    memset(payload, 0, 8);
    tx.messageId = ID_VOLT;
    uint32 v_raw = (uint32)(v*VOLT_DIV + 0.5f);
    set_bits_le(payload, VOLT_START, VOLT_LEN, v_raw);
    while (IfxCan_Can_sendMessage(&g_canNode, &tx, (uint32*)payload) == IfxCan_Status_notSentBusy) {}

    /* 0x500 Curr */
    memset(payload, 0, 8);
    tx.messageId = ID_CURR;
    uint32 i_raw = (uint32)(i*CURR_DIV + 0.5f);
    set_bits_le(payload, CURR_START, CURR_LEN, i_raw);
    while (IfxCan_Can_sendMessage(&g_canNode, &tx, (uint32*)payload) == IfxCan_Status_notSentBusy) {}

    /* 0x600 Temp */
    memset(payload, 0, 8);
    tx.messageId = ID_TEMP;
    uint32 t_raw = (uint32)(t*TEMP_DIV + 0.5f);
    set_bits_le(payload, TEMP_START, TEMP_LEN, t_raw);
    while (IfxCan_Can_sendMessage(&g_canNode, &tx, (uint32*)payload) == IfxCan_Status_notSentBusy) {}

    /* small sweep */
    v += 0.02f; if (v>14.0f) v=12.0f;
    i += 0.10f; if (i>12.0f) i=8.0f;
    t += 0.10f; if (t>35.0f) t=25.0f;
}
#endif

/******************************* MAIN ***************************************/
int core0_main(void)
{
    IfxCpu_enableInterrupts();
    IfxScuWdt_disableCpuWatchdog(IfxScuWdt_getCpuWatchdogPassword());
    IfxScuWdt_disableSafetyWatchdog(IfxScuWdt_getSafetyWatchdogPassword());

    /* Make sure STM0 clock is on if you later use delays based on STM */
    MODULE_STM0.CLC.U = 0;

    IfxCpu_emitEvent(&cpuSyncEvent);
    IfxCpu_waitEvent(&cpuSyncEvent, 1);

    uart_init();
    can_init();

    uart_printf("TC375 Lite CAN→UART Gateway ready. Baud=%u, CAN=%u\r\n", (unsigned)UART_BAUDRATE, (unsigned)CAN_BAUDRATE);
    uart_printf("Listening IDs: 0x%03X (V), 0x%03X (I), 0x%03X (T)\r\n", ID_VOLT, ID_CURR, ID_TEMP);

    uint32 tick = 0;
    for(;;)
    {
        can_poll_and_print();
#if ENABLE_TEST_TX
        if ((++tick % 10) == 0) { /* ~100 ms if wait(10) below */
            can_send_test_signals();
        }
#endif
        /* Small idle to avoid busy-looping when bus is silent */
        wait(10); /* 10 ms */
    }
}
