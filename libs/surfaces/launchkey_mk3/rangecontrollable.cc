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

#include <stdint.h>
#include <cmath>
#include <climits>
#include <iostream>

#include "pbd/error.h"
#include "pbd/compose.h"
#include "pbd/types_convert.h"
#include "pbd/xml++.h"

#include "midi++/types.h"
#include "midi++/port.h"
#include "midi++/channel.h"

#include "ardour/async_midi_port.h"
#include "ardour/automation_control.h"
#include "ardour/midi_ui.h"
#include "ardour/debug.h"

#include "rangecontrollable.h"

using namespace std;
using namespace MIDI;
using namespace PBD;
using namespace ARDOUR;

//----------------------------------------------------------------------------------------------------------
RangeControllable::RangeControllable (MIDI::Parser& p, uint8_t cc, bool fader)
	: _cc (cc)
	, _fader (fader)
{
	// connect callback. All launchkey mk3 faders and pots send on channel 16 in all modes.
	p.channel_controller[15].connect_same_thread (
		controllable_midi_connection,
		boost::bind (&RangeControllable::midi_cc_receiver, this, _1, _2)
	);

	// pots initialize in PAN mode, faders in VOLUME mode
	if (fader) {
		new_value_received.connect_same_thread (
			controllable_mode_connection,
			boost::bind (&RangeControllable::new_value_volume, this, _1)
		);
	} else {
		new_value_received.connect_same_thread (
			controllable_mode_connection,
			boost::bind (&RangeControllable::new_value_pan, this, _1)
		);
	}

}

//----------------------------------------------------------------------------------------------------------
RangeControllable::~RangeControllable ()
{

}

//----------------------------------------------------------------------------------------------------------
void
RangeControllable::midi_cc_receiver (MIDI::Parser &p, MIDI::EventTwoBytes *tb)
{
	// check for mode changes
	if (!_fader && tb->controller_number == 0x09) {
		// pot mode switch
		ControllableMode m = (tb->value == 0) ? MODE_CUSTOM0 : static_cast<ControllableMode>(tb->value);
		pot_mode_switch (m);
	} else if (_fader && tb->controller_number == 0x0A) {
		// fader mode switch
		ControllableMode m = (tb->value == 0) ? MODE_CUSTOM0 : static_cast<ControllableMode>(tb->value);
		fader_mode_switch (m);
	} else if (tb->controller_number != _cc) {
		// no mode switch, but also not our CC -> do nothing.
		return;
	}

	// new value received, call signal
	new_value_received (tb->value);
}

//----------------------------------------------------------------------------------------------------------
void
RangeControllable::pot_mode_switch (ControllableMode new_mode)
{
	// TBD

	controllable_mode_connection.disconnect();

	switch (new_mode) {
		case MODE_VOLUME:
			new_value_received.connect_same_thread (
				controllable_mode_connection,
				boost::bind (&RangeControllable::new_value_volume, this, _1)
			);
			break;
		case MODE_DEVICE:
			new_value_received.connect_same_thread (
				controllable_mode_connection,
				boost::bind (&RangeControllable::new_value_device, this, _1)
			);
			break;
		case MODE_PAN:
			new_value_received.connect_same_thread (
				controllable_mode_connection,
				boost::bind (&RangeControllable::new_value_pan, this, _1)
			);
			break;
		case MODE_SEND_A:
			new_value_received.connect_same_thread (
				controllable_mode_connection,
				boost::bind (&RangeControllable::new_value_send_a, this, _1)
			);break;
		case MODE_SEND_B:
			new_value_received.connect_same_thread (
				controllable_mode_connection,
				boost::bind (&RangeControllable::new_value_send_b, this, _1)
			);
			break;
		// TBD: probably dont need to update anything for these modes:
		case MODE_CUSTOM0: break;
		case MODE_CUSTOM1: break;
		case MODE_CUSTOM2: break;
		case MODE_CUSTOM3: break;
	}
}

//----------------------------------------------------------------------------------------------------------
void
RangeControllable::fader_mode_switch (ControllableMode new_mode)
{
	// TBD
	switch (new_mode) {
		case MODE_VOLUME: break;
		case MODE_DEVICE: break;
		case MODE_SEND_A: break;
		case MODE_SEND_B: break;
		case MODE_CUSTOM0: break;
		case MODE_CUSTOM1: break;
		case MODE_CUSTOM2: break;
		case MODE_CUSTOM3: break;
	}
}

//----------------------------------------------------------------------------------------------------------
void
RangeControllable::new_value_volume (uint8_t new_value)
{
	// TBD

}

//----------------------------------------------------------------------------------------------------------
void
RangeControllable::new_value_device (uint8_t new_value)
{
	// TBD

}

//----------------------------------------------------------------------------------------------------------
void
RangeControllable::new_value_pan (uint8_t new_value)
{
	// TBD

}

//----------------------------------------------------------------------------------------------------------
void
RangeControllable::new_value_send_a (uint8_t new_value)
{
	// TBD

}

//----------------------------------------------------------------------------------------------------------
void
RangeControllable::new_value_send_b (uint8_t new_value)
{
	// TBD

}
