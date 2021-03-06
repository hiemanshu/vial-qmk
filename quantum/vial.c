/* Copyright 2020 Ilya Zhuravlev
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include "vial.h"

#include <string.h>
#include "protocol/usb_descriptor.h"

#include "vial_generated_keyboard_definition.h"
#include "dynamic_keymap.h"
#include "quantum.h"

enum {
    vial_get_keyboard_id = 0x00,
    vial_get_size = 0x01,
    vial_get_def = 0x02,
    vial_get_encoder = 0x03,
    vial_set_encoder = 0x04,
    vial_get_keymap_fast = 0x05,
};

void vial_handle_cmd(uint8_t *msg, uint8_t length) {
    /* All packets must be fixed 32 bytes */
    if (length != RAW_EPSIZE)
        return;

    /* msg[0] is 0xFE -- prefix vial magic */
    switch (msg[1]) {
        /* Get keyboard ID and Vial protocol version */
        case vial_get_keyboard_id: {
            uint8_t keyboard_uid[] = VIAL_KEYBOARD_UID;

            msg[0] = VIAL_PROTOCOL_VERSION & 0xFF;
            msg[1] = (VIAL_PROTOCOL_VERSION >> 8) & 0xFF;
            msg[2] = (VIAL_PROTOCOL_VERSION >> 16) & 0xFF;
            msg[3] = (VIAL_PROTOCOL_VERSION >> 24) & 0xFF;
            memcpy(&msg[4], keyboard_uid, 8);
            break;
        }
        /* Retrieve keyboard definition size */
        case vial_get_size: {
            uint32_t sz = sizeof(keyboard_definition);
            msg[0] = sz & 0xFF;
            msg[1] = (sz >> 8) & 0xFF;
            msg[2] = (sz >> 16) & 0xFF;
            msg[3] = (sz >> 24) & 0xFF;
            break;
        }
        /* Retrieve 32-bytes block of the definition, page ID encoded within 2 bytes */
        case vial_get_def: {
            uint32_t page = msg[2] + (msg[3] << 8);
            uint32_t start = page * RAW_EPSIZE;
            uint32_t end = start + RAW_EPSIZE;
            if (end < start || start >= sizeof(keyboard_definition))
                return;
            if (end > sizeof(keyboard_definition))
                end = sizeof(keyboard_definition);
            memcpy_P(msg, &keyboard_definition[start], end - start);
            break;
        }
#ifdef VIAL_ENCODERS_ENABLE
        case vial_get_encoder: {
            uint8_t layer = msg[2];
            uint8_t idx = msg[3];
            uint16_t keycode = dynamic_keymap_get_encoder(layer, idx, 0);
            msg[0]  = keycode >> 8;
            msg[1]  = keycode & 0xFF;
            keycode = dynamic_keymap_get_encoder(layer, idx, 1);
            msg[2] = keycode >> 8;
            msg[3] = keycode & 0xFF;
            break;
        }
        case vial_set_encoder: {
            dynamic_keymap_set_encoder(msg[2], msg[3], msg[4], (msg[5] << 8) | msg[6]);
            break;
        }
#endif
        /*
         * Retrieve up to 16 keycodes at once.
         * First byte: layer to retrieve
         * Second byte: row to retrieve
         * 16 more bytes: columns to retrieve (at that layer/row). 0xFF padding used to ignore that position.
         */
        case vial_get_keymap_fast: {
            uint8_t req[16];
            uint8_t layer = msg[2];
            uint8_t row = msg[3];
            memcpy(req, &msg[4], sizeof(req));

            for (int i = 0; i < 16; ++i) {
                if (req[i] != 0xFF) {
                    uint16_t keycode = dynamic_keymap_get_keycode(layer, row, req[i]);
                    msg[2 * i] = keycode >> 8;
                    msg[2 * i + 1] = keycode & 0xFF;
                }
            }
        }
    }
}

#ifdef VIAL_ENCODERS_ENABLE
void vial_encoder_update(uint8_t index, bool clockwise) {
    uint16_t code;

    layer_state_t layers = layer_state | default_layer_state;
    /* check top layer first */
    for (int8_t i = MAX_LAYER - 1; i >= 0; i--) {
        if (layers & (1UL << i)) {
            code = dynamic_keymap_get_encoder(i, index, clockwise);
            if (code != KC_TRNS) {
                tap_code16(code);
                return;
            }
        }
    }
    /* fall back to layer 0 */
    code = dynamic_keymap_get_encoder(0, index, clockwise);
    tap_code16(code);
}
#endif
