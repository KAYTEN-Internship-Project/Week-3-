#include "Ifx_Types.h"
#include "IfxCpu.h"
#include "IfxScuWdt.h"
#include "IfxCan_Can.h"
#include "IfxPort.h"
#include "Bsp.h"
#include <string.h>

#define TX_ID 0x123  /* not used but kept */

typedef struct {
    IfxCan_Can      mod;
    IfxCan_Can_Node node;
    IfxCan_Message  tx;
    IfxCan_Message  rx;
    uint32          txData[2];
    uint32          rxData[2];
} App;

static App g;
IFX_ALIGN(4) IfxCpu_syncEvent cpuSyncEvent = 0;

/* ---- Bit helpers (Intel/LSB0) ---- */
static inline void set_bits_le(uint8 *buf, uint32 startBit, uint32 len, uint32 value)
{
    for (uint32 i = 0; i < len; ++i) {
        uint32 bitPos = startBit + i;
        uint32 byteIx = bitPos >> 3;
        uint32 bitIx  = bitPos & 7;
        uint8 mask = (uint8)(1u << bitIx);
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

/* ---- Signals ---- */
#define VOLT_START   6u
#define VOLT_LEN     12u
#define VOLT_DIV     100.0f   /* V = raw/100 */

#define CURR_START   16u      /* byte2 */
#define CURR_LEN     8u
#define CURR_DIV     10.0f    /* A = raw/10 */

#define TEMP_START   32u
#define TEMP_LEN     12u
#define TEMP_DIV     10.0f    /* C = raw/10 */

/* Test values (change if desired) */
static float txVolt_V = 12.00f;
static float txCurr_A = 8.7f;
static float txTemp_C = 25.3f;

/* List of IDs to be sent (no longer used but kept) */
static const uint32 kTxIds[] = { 0x111, 0x123, 0x456, 0x555 };
#define K_NUM_TX_IDS (sizeof(kTxIds)/sizeof(kTxIds[0]))
static uint32 seq = 0;

static void can_init(void)
{
    IfxCan_Can_Config mcfg;
    IfxCan_Can_initModuleConfig(&mcfg, &MODULE_CAN0);
    IfxCan_Can_initModule(&g.mod, &mcfg);

    /* TLE9251V STB -> LOW (Normal)  P20.6 */
    IfxPort_setPinModeOutput(&MODULE_P20, 6, IfxPort_OutputMode_pushPull, IfxPort_OutputIdx_general);
    IfxPort_setPinLow(&MODULE_P20, 6);

    /* Node config */
    IfxCan_Can_NodeConfig n;
    IfxCan_Can_initNodeConfig(&n, &g.mod);

    n.nodeId                   = IfxCan_NodeId_0;
    n.frame.type               = IfxCan_FrameType_transmitAndReceive;
    n.frame.mode               = IfxCan_FrameMode_standard;   /* Classic CAN */
    n.calculateBitTimingValues = TRUE;
    n.baudRate.baudrate        = 500000;                      /* 500 kbit/s */
    n.baudRate.samplePoint     = 8000;                        /* 80.00 % */
    n.baudRate.syncJumpWidth   = 3;

    /* TX: 1 dedicated buffer */
    n.txConfig.txMode                   = IfxCan_TxMode_dedicatedBuffers;
    n.txConfig.dedicatedTxBuffersNumber = 1;
    n.txConfig.txBufferDataFieldSize    = IfxCan_DataFieldSize_8;
    n.txConfig.txEventFifoSize          = 0;

    /* RX: Accept-all → FIFO0 */
    n.rxConfig.rxMode               = IfxCan_RxMode_fifo0;
    n.rxConfig.rxFifo0Size          = 8;
    n.rxConfig.rxFifo0DataFieldSize = IfxCan_DataFieldSize_8;
    n.rxConfig.rxFifo0OperatingMode = IfxCan_RxFifoMode_blocking;

    n.filterConfig.messageIdLength                    = IfxCan_MessageIdLength_both;
    n.filterConfig.standardListSize                   = 0;
    n.filterConfig.extendedListSize                   = 0;
    n.filterConfig.standardFilterForNonMatchingFrames = IfxCan_NonMatchingFrame_acceptToRxFifo0;
    n.filterConfig.extendedFilterForNonMatchingFrames = IfxCan_NonMatchingFrame_acceptToRxFifo0;

    /* Pins: P20.8 TXD, P20.7 RXD */
    static const IfxCan_Can_Pins pins = {
        .txPin     = &IfxCan_TXD00_P20_8_OUT,
        .txPinMode = IfxPort_OutputMode_pushPull,
        .rxPin     = &IfxCan_RXD00B_P20_7_IN,
        .rxPinMode = IfxPort_InputMode_pullUp,
        .padDriver = IfxPort_PadDriver_cmosAutomotiveSpeed2
    };
    n.pins = &pins;

    IfxCan_Can_initNode(&g.node, &n);

    /* RX template: read from FIFO0 */
    IfxCan_Can_initMessage(&g.rx);
    g.rx.readFromRxFifo0  = TRUE;
    g.rx.dataLengthCode   = IfxCan_DataLengthCode_8;

    /* TX template: 8-byte */
    IfxCan_Can_initMessage(&g.tx);
    g.tx.dataLengthCode   = IfxCan_DataLengthCode_8;

    g.txData[0] = 0x11223344;
    g.txData[1] = 0x55667788;
}

/* --- SEND: 0x400 Voltage, 0x500 Current, 0x600 Temperature --- */
static void can_send_signals(void)
{
    uint8 *payload = (uint8*)g.txData;

    /* 0x400 */
    memset(payload, 0, 8);
    g.tx.messageId = 0x400;
    uint32 v_raw = (uint32)(txVolt_V * VOLT_DIV + 0.5f);
    set_bits_le(payload, VOLT_START, VOLT_LEN, v_raw);
    while (IfxCan_Can_sendMessage(&g.node, &g.tx, (uint32*)payload) == IfxCan_Status_notSentBusy) {}
    wait(1);

    /* 0x500 */
    memset(payload, 0, 8);
    g.tx.messageId = 0x500;
    uint32 i_raw = (uint32)(txCurr_A * CURR_DIV + 0.5f);
    set_bits_le(payload, CURR_START, CURR_LEN, i_raw);
    while (IfxCan_Can_sendMessage(&g.node, &g.tx, (uint32*)payload) == IfxCan_Status_notSentBusy) {}
    wait(1);

    /* 0x600 */
    memset(payload, 0, 8);
    g.tx.messageId = 0x600;
    uint32 t_raw = (uint32)(txTemp_C * TEMP_DIV + 0.5f);
    set_bits_le(payload, TEMP_START, TEMP_LEN, t_raw);
    while (IfxCan_Can_sendMessage(&g.node, &g.tx, (uint32*)payload) == IfxCan_Status_notSentBusy) {}

    /* Simple sweep (optional test) */
    txVolt_V += 0.02f; if (txVolt_V > 14.0f) txVolt_V = 12.0f;
    txCurr_A += 0.1f;  if (txCurr_A > 12.0f) txCurr_A = 8.0f;
    txTemp_C += 0.1f;  if (txTemp_C > 35.0f) txTemp_C = 25.0f;

    ++seq;
}

/* --- RECEIVE: decode incoming 0x400/0x500/0x600 --- */
static void can_poll_rx(void)
{
    if (IfxCan_Node_getRxFifo0FillLevel(g.node.node) == 0) return;

    (void)IfxCan_Can_readMessage(&g.node, &g.rx, (uint32*)g.rxData);
    const uint8 *p = (const uint8*)g.rxData;

    switch (g.rx.messageId)
    {
        case 0x400: {
            uint32 raw = get_bits_le(p, VOLT_START, VOLT_LEN);
            float volt_V = raw / VOLT_DIV;
            (void)volt_V; /* use here */
        } break;

        case 0x500: {
            uint32 raw = get_bits_le(p, CURR_START, CURR_LEN);
            float curr_A = raw / CURR_DIV;
            (void)curr_A;
        } break;

        case 0x600: {
            uint32 raw = get_bits_le(p, TEMP_START, TEMP_LEN);
            float temp_C = raw / TEMP_DIV;
            (void)temp_C;
        } break;

        default:
            break;
    }
}

int core0_main(void)
{
    IfxCpu_enableInterrupts();
    IfxScuWdt_disableCpuWatchdog(IfxScuWdt_getCpuWatchdogPassword());
    IfxScuWdt_disableSafetyWatchdog(IfxScuWdt_getSafetyWatchdogPassword());

    IfxCpu_emitEvent(&cpuSyncEvent);
    IfxCpu_waitEvent(&cpuSyncEvent, 1);

    can_init();

    while (1)
    {
        can_send_signals();  /* 0x400(V), 0x500(I), 0x600(T) */
        can_poll_rx();
        wait(100);
    }
    return 1;
}
