/*

Copyright (C) 2013   Artem Prilutskiy, R3ABM (r3abm@dstar.su)
Copyright (C) 2013   Michael Dirska, DL1BFF (dl1bff@mdx.de)

This program is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 2 of the License, or
at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program.  If not, see <http://www.gnu.org/licenses/>.

*/

#ifndef NTP_H
#define NTP_H

#include "FreeRTOS.h"
#include "gcc_builtin.h"

void ntp_handle_packet(const uint8_t* data, int length, const uint8_t* address);
void ntp_init(void);

#endif
