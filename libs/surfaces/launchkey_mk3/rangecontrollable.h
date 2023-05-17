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

#ifndef __launchkey_mk3_rangecontrollable_h__
#define __launchkey_mk3_rangecontrollable_h__

#include <string>

#include "midi++/types.h"

#include "pbd/controllable.h"
#include "pbd/signals.h"
#include "pbd/stateful.h"

#include "ardour/types.h"

namespace MIDI {
	class Channel;
	class Parser;
}

//class GenericMidiControlProtocol;

namespace ARDOUR {
	class AsyncMIDIPort;
}

/* A helper class to deal with the messaging of pots and faders.
 *
 *
 *
 *
 *
 *
 *
 */
class RangeControllable// : public PBD::Stateful
{
public:
	RangeControllable (MIDI::Parser&, uint8_t cc, bool fader);
	virtual ~RangeControllable ();



	// IDs for all the different possible modes
	enum ControllableMode {
		MODE_VOLUME = 1,
		MODE_DEVICE = 2,
		MODE_PAN = 3,      // not available for faders
		MODE_SEND_A = 4,
		MODE_SEND_B = 5,
		MODE_CUSTOM0 = 6,
		MODE_CUSTOM1 = 7,
		MODE_CUSTOM2 = 8,
		MODE_CUSTOM3 = 9
	};

	using ValueChangeSignal = PBD::Signal1<void, uint8_t>;

	ValueChangeSignal new_value_received;


protected:

	uint8_t    _cc;        // which CC this controllable reacts to
	bool       _fader;     // whether this controllable is a fader (if false, it is a pot)

	PBD::ScopedConnection controllable_midi_connection;
	PBD::ScopedConnection controllable_mode_connection;


	Glib::Threads::Mutex controllable_lock;


	// the callback registered for the incoming midi messages on the specified channel
	void midi_cc_receiver (MIDI::Parser &, MIDI::EventTwoBytes *);

	// helper methods called by the cc receiver
	void pot_mode_switch (ControllableMode new_mode);
	void fader_mode_switch (ControllableMode new_mode);

	// callbacks for new values
	void new_value_volume (uint8_t new_value);
	void new_value_device (uint8_t new_value);
	void new_value_pan (uint8_t new_value);
	void new_value_send_a (uint8_t new_value);
	void new_value_send_b (uint8_t new_value);

};

#endif // __launchkey_mk3_rangecontrollable_h__
