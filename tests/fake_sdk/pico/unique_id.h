#ifndef FAKE_PICO_UNIQUE_ID_H
#define FAKE_PICO_UNIQUE_ID_H

#define PICO_UNIQUE_BOARD_ID_SIZE_BYTES 8

// fake::board_id
void pico_get_unique_board_id_string(char* id_out, unsigned len);

#endif
