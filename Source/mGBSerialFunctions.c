
volatile UBYTE serialBuffer[256];
volatile UBYTE serialBufferPosition;
volatile UBYTE serialBufferReadPosition;

void serialReceiveHandler(void) NONBANKED;

void serialSetup(void)
{
	serialBufferPosition = 0U;
	serialBufferReadPosition = 0U;
	SB_REG = 0x00U;
	SC_REG = SIOF_XFER_START | SIOF_CLOCK_EXT;
	add_SIO(serialReceiveHandler);
}
