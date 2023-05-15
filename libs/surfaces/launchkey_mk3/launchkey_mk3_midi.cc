/*
 * Copyright (C) 2023 Jan Peters <devel@jancp.de>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 */


/* This file contains all the directly MIDI-related methods of the LaunchkeyMk3 class.
 *
 */

#include "pbd/error.h"
#include "pbd/failed_constructor.h"
#include "pbd/file_utils.h"
#include "pbd/pthread_utils.h"
#include "pbd/compose.h"
#include "pbd/xml++.h"

#include "midi++/port.h"

#include "ardour/async_midi_port.h"
#include "ardour/audioengine.h"
#include "ardour/debug.h"
#include "ardour/midi_port.h"

#include "launchkey_mk3.h"

using namespace ARDOUR;
using namespace ArdourSurface;
using namespace PBD;
using namespace Glib;

//----------------------------------------------------------------------------------------------------------
void
LaunchkeyMk3::send_midi (const std::vector<MIDI::byte>& data)
{
	_output_port->write (&data[0], data.size(), 0);
}

//----------------------------------------------------------------------------------------------------------
void
LaunchkeyMk3::start_midi_handling ()
{
	// Ask device for identification.
	// Check if it really is a launchkey, and if so, which one.
	DEBUG_TRACE (DEBUG::LaunchkeyMk3, "sending device inquiry message...\n");
	send_midi ({ 0xF0, 0x7E, 0x7F, 0x06, 0x01, 0xF7 });
}

//----------------------------------------------------------------------------------------------------------
void
LaunchkeyMk3::stop_midi_handling ()
{
	// NOTE: exiting DAW mode might not work if the launchkey has been disconnected already
	if (device_active && in_daw_mode) {
		// try to leave DAW mode

		// DAW mode
		DEBUG_TRACE (DEBUG::LaunchkeyMk3, "Resetting Launchkey to standalone mode\n");
		send_midi ({ 0x8F, 0x0C, 0x00 });    // send note off for DAW mode (0x0C) on channel 16
		in_daw_mode = false;

		// Continuous control pot pickup
		DEBUG_TRACE (DEBUG::LaunchkeyMk3, "Disable pot continuous control pot pickup\n");
		send_midi ({ 0x8F, 0x0A, 0x00 });    // send note off for pickup (0x0A) on channel 16
	}
}

//----------------------------------------------------------------------------------------------------------
bool
LaunchkeyMk3::handle_incoming_midi (IOCondition ioc, std::weak_ptr<AsyncMIDIPort> wport)
{
	std::shared_ptr<AsyncMIDIPort> port (wport.lock());

	if (!port || !_input_port) {
		return false;
	}

	if (ioc & ~IO_IN) {
		DEBUG_TRACE (DEBUG::LaunchkeyMk3, "MIDI port closed\n");
		return false;
	}

	if (ioc & IO_IN) {

		//DEBUG_TRACE (DEBUG::LaunchkeyMk3, string_compose ("something happened on  %1\n", port->name()));
		port->clear ();

		//DEBUG_TRACE (DEBUG::LaunchkeyMk3, string_compose ("data available on %1\n", port->name()));
		if (device_active) {
			samplepos_t now = AudioEngine::instance()->sample_time();
			port->parse (now);
		}
	}

	return true;
}

//----------------------------------------------------------------------------------------------------------
void
LaunchkeyMk3::handle_midi_sysex (MIDI::Parser &p, MIDI::byte *buf, size_t sz)
{
	DEBUG_TRACE (DEBUG::LaunchkeyMk3, string_compose ("sysex message received, size = %1\n", sz));

	// check if it is a system identification answer
	if (sz >= 5 &&
	    buf[0] == 0xF0 &&
	    buf[1] == 0x7E &&
	    buf[3] == 0x06 &&
	    buf[4] == 0x02) {

		// it is. check if it is a launchkey.
		if (sz >= 17 &&
		    buf[5] == 0x00 &&
		    buf[6] == 0x20 &&
		    buf[7] == 0x29 &&
		    buf[10] == 0x00 &&
		    buf[11] == 0x00 &&
		    buf[16] == 0xF7) {

			// it very much seems that it is.
			// extract version and type information.
			const MIDI::byte launchkeySize = buf[8];
			const MIDI::byte modeIndicator = buf[9];

			// NOTE: the +0x30 converts from int digits 0-9 to ASCII chars '0' - '9'
			const char versionInfo[] = {
				static_cast<char>(0x30 + buf[12]),
				static_cast<char>(0x30 + buf[13]),
				static_cast<char>(0x30 + buf[14]),
				static_cast<char>(0x30 + buf[15]),
				0
			};

			switch (launchkeySize) {
				case 0x34:
					DEBUG_TRACE(DEBUG::LaunchkeyMk3, "Launchkey Mk3 25 identified via MIDI device inquiry response\n");
					has_faders = false;
					break;
				case 0x35:
					DEBUG_TRACE(DEBUG::LaunchkeyMk3, "Launchkey Mk3 37 identified via MIDI device inquiry response\n");
					has_faders = false;
					break;
				case 0x36:
					DEBUG_TRACE(DEBUG::LaunchkeyMk3, "Launchkey Mk3 49 identified via MIDI device inquiry response\n");
					has_faders = true;
					break;
				case 0x37:
					DEBUG_TRACE(DEBUG::LaunchkeyMk3, "Launchkey Mk3 61 identified via MIDI device inquiry response\n");
					has_faders = true;
					break;
				case 0x40:
					DEBUG_TRACE(DEBUG::LaunchkeyMk3, "Launchkey Mk3 88 identified via MIDI device inquiry response\n");
					has_faders = true;
					break;
			}

			DEBUG_TRACE(DEBUG::LaunchkeyMk3, string_compose("Firmware version is %1\n", versionInfo));
			DEBUG_TRACE(DEBUG::LaunchkeyMk3, string_compose("Currently in %1 mode\n", modeIndicator==0x01 ? "APP" : "BOOT"));

			// enter DAW mode and set default state variables

			// DAW mode
			DEBUG_TRACE (DEBUG::LaunchkeyMk3, "Putting Launchkey in DAW mode\n");
			send_midi ({ 0x9F, 0x0C, 0x7F });    // send note on for DAW mode (0x0C) on channel 16
			in_daw_mode = true;

			// Continuous control pot pickup
			DEBUG_TRACE (DEBUG::LaunchkeyMk3, "Enable pot continuous control pot pickup\n");
			send_midi ({ 0x9F, 0x0A, 0x7F });    // send note on for pickup (0x0C) on channel 16

			// init default modes
			current_pad_mode = LkPadMode::SESSION;
			current_pot_mode = LkPotMode::PAN;
			current_fader_mode = LkFaderMode::VOLUME;

			// TBD: potentially other init work
		}
	}
}

//----------------------------------------------------------------------------------------------------------
void
LaunchkeyMk3::handle_midi_controller_channel1 (MIDI::Parser &, MIDI::EventTwoBytes* tb)
{
	// TBD
	DEBUG_TRACE (DEBUG::LaunchkeyMk3, "MIDI: CC on channel 1\n");
}

//----------------------------------------------------------------------------------------------------------
void
LaunchkeyMk3::handle_midi_controller_channel16 (MIDI::Parser &, MIDI::EventTwoBytes* tb)
{
	// TBD
	DEBUG_TRACE (DEBUG::LaunchkeyMk3, "MIDI: CC on channel 16\n");
}

//----------------------------------------------------------------------------------------------------------
void
LaunchkeyMk3::handle_midi_note_on_channel1 (MIDI::Parser &, MIDI::EventTwoBytes* tb)
{
	// TBD
	DEBUG_TRACE (DEBUG::LaunchkeyMk3, "MIDI: NOTE ON on channel 1\n");
}

//----------------------------------------------------------------------------------------------------------
void
LaunchkeyMk3::handle_midi_polypressure_channel1 (MIDI::Parser &, MIDI::EventTwoBytes* tb)
{
	// TBD
	DEBUG_TRACE (DEBUG::LaunchkeyMk3, "MIDI: POLY PRESS on channel 1\n");
}

//----------------------------------------------------------------------------------------------------------
void
LaunchkeyMk3::handle_midi_note_on_channel10 (MIDI::Parser &, MIDI::EventTwoBytes* tb)
{
	// TBD
	DEBUG_TRACE (DEBUG::LaunchkeyMk3, "MIDI: NOTE ON on channel 10\n");
}

//----------------------------------------------------------------------------------------------------------
void
LaunchkeyMk3::handle_midi_polypressure_channel10 (MIDI::Parser &, MIDI::EventTwoBytes* tb)
{
	// TBD
	DEBUG_TRACE (DEBUG::LaunchkeyMk3, "MIDI: POLY PRESS on channel 10\n");
}
