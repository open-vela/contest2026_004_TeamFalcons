#!/usr/bin/env python3
"""Check RGB565 low bits against MB1381 B-01 schematic sheet 3 (U6B)."""
from pathlib import Path
import re

board = Path(__file__).resolve().parents[4] / 'nuttx/boards/arm/stm32h7/stm32h750b-dk/src/stm32h750b-dk.h'
text = board.read_text().replace('\\\n', '')
for net, pin in [('GPIO_LCD_G0', 7), ('GPIO_LCD_G1', 8)]:
    body = re.search(r'^#define\s+' + net + r'\s+([^\n]+)', text, re.MULTILINE)[1]
    assert 'GPIO_PORTJ' in body and re.search(r'GPIO_PIN' + str(pin) + r'\b', body), (
        f'{net} must be PJ{pin}; PG0/PG1 are SDRAM A10/A11: {body}'
    )
print('test_board_pins: OK (LCD G0/G1 PJ7/PJ8, preserve SDRAM A10/A11)')
