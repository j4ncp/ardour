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
#include "ardour/session.h"

namespace MIDI {
	class Channel;
	class Parser;
}

//class GenericMidiControlProtocol;

namespace ARDOUR {
	class AsyncMIDIPort;
}

/* A helper class to manage a set of 8 pots or 8+1 faders
 */
class RangeControllableSet
{
public:
	RangeControllableSet (MIDI::Parser&, ARDOUR::Session&, bool faders);
	virtual ~RangeControllableSet ();

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

	void reassign_stripables ();


protected:

	ARDOUR::Session& _session;       // used to enumerate stripables

	const bool     _faders;          // if true, this is a set of faders
	const uint8_t  _starting_cc;     // which set of CCs the range controllables react to [starting_cc, .., starting_cc + num_items]

	ControllableMode _current_mode;  // the mode the controllables are currently in

	PBD::ScopedConnection controllable_midi_connection;

	//
	std::vector<std::shared_ptr<ARDOUR::AutomationControl>> controllables;

	Glib::Threads::Mutex controllable_lock;


	// the callback registered for the incoming midi messages on the specified channel
	void midi_cc_receiver_15 (MIDI::Parser &, MIDI::EventTwoBytes *);
	void midi_cc_receiver_16 (MIDI::Parser &, MIDI::EventTwoBytes *);

	// helper methods called by the cc receivers
	void mode_switch (ControllableMode new_mode);
	void touch_event (uint8_t id, bool on);
	void new_value_received (uint8_t id, uint8_t value);
};

#endif // __launchkey_mk3_rangecontrollable_h__
