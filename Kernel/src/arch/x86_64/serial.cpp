#include <system.h>
#include <serial.h>

#define PORT 0x3F8 // COM 1

void initialize_serial() {
	outportb(PORT + 1, 0x00);    // Disable all interrupts
	outportb(PORT + 3, 0x80);    // Enable DLAB (set baud rate divisor)
	outportb(PORT + 0, 0x03);    // Set divisor to 3 (lo byte) 38400 baud
	outportb(PORT + 1, 0x00);    //                  (hi byte)
	outportb(PORT + 3, 0x03);    // 8 bits, no parity, one stop bit
	outportb(PORT + 2, 0xC7);    // Enable FIFO, clear them, with 14-byte threshold
	outportb(PORT + 4, 0x0B);    // IRQs enabled, RTS/DSR set
}

int is_transmit_empty() {
	return inportb(PORT + 5) & 0x20;
}

bool lock = false;

void write_serial(const char c) {
	while (is_transmit_empty() == 0);

	outportb(PORT, c);
}

void write_serial(const char* s) {
	while (*s != '\0'){
		while(is_transmit_empty() == 0);
		outportb(PORT, *s++);
	}
}