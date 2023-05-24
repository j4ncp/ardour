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
#include "ardour/debug.h"
#include "ardour/midi_ui.h"
#include "ardour/plugin_insert.h"
#include "ardour/track.h"


#include "pbd/controllable.h"

#include "rangecontrollableset.h"

using namespace std;
using namespace MIDI;
using namespace PBD;
using namespace ARDOUR;

//----------------------------------------------------------------------------------------------------------
RangeControllableSet::RangeControllableSet (MIDI::Parser& p, ARDOUR::Session& session, bool faders)
	: _session (session)
	, _faders (faders)
	, _starting_cc (faders ? 0x35 : 0x15)
{
	// connect callbacks. All launchkey mk3 faders and pots send touch events on channel 15 in all modes
	p.channel_controller[14].connect_same_thread (
		controllable_midi_connection,
		boost::bind (&RangeControllableSet::midi_cc_receiver_15, this, _1, _2)
	);

	// All launchkey mk3 faders and pots send values on channel 16 in all modes
	p.channel_controller[15].connect_same_thread (
		controllable_midi_connection,
		boost::bind (&RangeControllableSet::midi_cc_receiver_16, this, _1, _2)
	);

	// pots initialize in PAN mode, faders in VOLUME mode
	if (_faders) {
		mode_switch (MODE_VOLUME);
	} else {
		mode_switch (MODE_PAN);
	}

}

//----------------------------------------------------------------------------------------------------------
RangeControllableSet::~RangeControllableSet ()
{

}

//----------------------------------------------------------------------------------------------------------
void
RangeControllableSet::midi_cc_receiver_15 (MIDI::Parser &p, MIDI::EventTwoBytes *tb)
{
	// check for touch events
	if (tb->controller_number < _starting_cc) {
		// not our CC -> do nothing.
		return;
	}

	const uint8_t id = tb->controller_number - _starting_cc;

	if (id >= 9 || (id >= 8 && !_faders)) {
		// not our CC
		return;
	}

	// touch event received, notify
	// NOTE: value is 0x7F if on, 0x00 if off
	touch_event (id, tb->value > 64);
}

//----------------------------------------------------------------------------------------------------------
void
RangeControllableSet::midi_cc_receiver_16 (MIDI::Parser &p, MIDI::EventTwoBytes *tb)
{
	// check for mode changes
	if ((!_faders && tb->controller_number == 0x09) ||
	    (_faders && tb->controller_number == 0x0A)) {
		// pot mode switch
		ControllableMode m = (tb->value == 0) ? MODE_CUSTOM0 : static_cast<ControllableMode>(tb->value);
		mode_switch (m);
		return;
	}

	if (tb->controller_number < _starting_cc) {
		// not our CC -> do nothing.
		return;
	}

	const uint8_t id = tb->controller_number - _starting_cc;

	if (id >= 9 || (id >= 8 && !_faders)) {
		// not our CC
		return;
	}

	// new value received, call signal
	new_value_received (id, tb->value);
}

//----------------------------------------------------------------------------------------------------------
void
RangeControllableSet::mode_switch (ControllableMode new_mode)
{
	_current_mode = new_mode;
	reassign_stripables ();
}

//----------------------------------------------------------------------------------------------------------
void
RangeControllableSet::reassign_stripables ()
{
	controllables.clear();


	// enumerate stripables
	/*
	StripableList all, filtered;
	std::shared_ptr<ARDOUR::Stripable> master;
	_session.get_stripables (all);

	// filter stripables for usable ones
	for (auto s : all) {
		if (s->is_auditioner ()) { continue; }
		if (s->is_hidden ()) { continue; }

		if (s->is_master ()) { master = s; continue; }

		filtered.push_back (s);
	}
	filtered.sort (Stripable::Sorter (true));

	size_t num_strips = std::min (size_t(8), filtered.size ());
*/
	// for almost all modes, each knob/fader corresponds to one track
	if (_current_mode != MODE_DEVICE) {

		for (uint8_t i = 0; i < 8; ++i) {

			auto s = _session.get_remote_nth_stripable (i, PresentationInfo::Track);
			if (!s) {
				break;
			}

			// cast to track. We know it is one.
			auto t = std::dynamic_pointer_cast<Track>(s);

			// see what control we need to connect
			std::shared_ptr<AutomationControl> c;

			switch (_current_mode) {
				case MODE_VOLUME:
					c = t->gain_control ();
					break;
				case MODE_PAN:
					c = t->pan_azimuth_control ();
					break;
				case MODE_SEND_A:
					c = t->send_level_controllable (0);
					break;
				case MODE_SEND_B:
					c = t->send_level_controllable (1);
					break;
				// TBD: probably dont need to update anything for these modes:
				case MODE_CUSTOM0: break;
				case MODE_CUSTOM1: break;
				case MODE_CUSTOM2: break;
				case MODE_CUSTOM3: break;
			}

			controllables.push_back (c);
		}
	} else {
		// _current_mode == MODE_DEVICE

		// TBD: figure out how to get correct stripable, for now use the first one
		auto s = _session.get_remote_nth_stripable (0, PresentationInfo::Track);
		if (!s) {
			return;
		}

		// cast to track. We know it is one.
		auto t = std::dynamic_pointer_cast<Track>(s);

		// get nth plugin
		// TBD: n
		auto proc = std::dynamic_pointer_cast<Processor>(t->nth_plugin (0));
		auto plugin_ins = std::dynamic_pointer_cast<PluginInsert>(proc);

		if (proc && plugin_ins) {
			// enumerate parameters of plugin
			uint32_t num_params = std::min(8u, plugin_ins->plugin()->parameter_count ());

			for (uint32_t i = 0; i < num_params; ++i) {
				bool ok = false;
				uint32_t param_id = plugin_ins->plugin()->nth_parameter (i, ok);
				std::shared_ptr<AutomationControl> c;
				// TBD: plugin_ins->plugin()->parameter_label (i) for readable name
				if (ok) {
					c = std::dynamic_pointer_cast<AutomationControl> (proc->control (Evoral::Parameter (PluginAutomation, 0, param_id)));
				}
				controllables.push_back (c);
			}

		}
	}
}

//----------------------------------------------------------------------------------------------------------
void
RangeControllableSet::touch_event (uint8_t id, bool on)
{
	if (id < controllables.size() && controllables[id]) {
		timepos_t now (_session.transport_sample());
		if (on) {
			controllables[id]->start_touch (now);
		} else {
			controllables[id]->stop_touch (now);
		}
	}
}

//----------------------------------------------------------------------------------------------------------
void
RangeControllableSet::new_value_received (uint8_t id, uint8_t value)
{
	if (id < controllables.size() && controllables[id]) {
		double v = value / 127.0;

		timepos_t now (_session.transport_sample());
		controllables[id]->start_touch (now);
		controllables[id]->set_value (controllables[id]->interface_to_internal (v), PBD::Controllable::NoGroup);
	}
}
