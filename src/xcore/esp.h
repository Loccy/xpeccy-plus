#pragma once

// An ESP-01 with Espressif's NONOS AT 1.x firmware on the Next's UART 0, its network the
// host's. What it answers is what the Next's own software needs.

struct Computer;

void esp_flash_file(const char* path);		// what its flash is kept in; before esp_attach
void esp_set_lan(bool on);			// its server listens on every interface (else 127.0.0.1)
void esp_attach(Computer* comp);		// plugged in: it boots
void esp_detach(Computer* comp);		// taken out; comp may be NULL
void esp_byte(Computer* comp, int byte, long long start);	// a byte the guest sends
void esp_frame(Computer* comp);			// frame end: timers, the network, the line
void esp_forget_snapshot();			// a snapshot's module state waiting for one plugged in: none is
