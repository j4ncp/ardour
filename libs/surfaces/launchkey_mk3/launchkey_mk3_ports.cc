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


/* This file contains all the directly port-related methods of the LaunchkeyMk3 class.
 * Creating & destroying them, checking for connected device ports, connecting and disconnecting those.
 */

#include "pbd/error.h"
#include "pbd/failed_constructor.h"
#include "pbd/file_utils.h"
#include "pbd/pthread_utils.h"
#include "pbd/compose.h"
#include "pbd/xml++.h"

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
bool
LaunchkeyMk3::probe (std::string& inp, std::string& outp)
{
	// check existing MIDI ports for strings like "Launchkey MK3", "LKMK3", and "DAW In" or "DAW Out".
	// if found, return true and set parameters to found input and output ports.
	std::vector<std::string> midi_inputs;
	std::vector<std::string> midi_outputs;
	AudioEngine::instance()->get_ports ("", DataType::MIDI, PortFlags (IsOutput|IsPhysical), midi_inputs);
	AudioEngine::instance()->get_ports ("", DataType::MIDI, PortFlags (IsInput|IsPhysical), midi_outputs);

	auto has_lkmk3_daw = [](std::string const& s) {
		std::string pn = AudioEngine::instance()->get_hardware_port_name_by_name (s);

		return (pn.find ("Launchkey MK3") != std::string::npos ||
		        pn.find ("LKMK3") != std::string::npos) &&
		       pn.find ("DAW") != std::string::npos;
	};

	auto pi = std::find_if (midi_inputs.begin (), midi_inputs.end (), has_lkmk3_daw);
	auto po = std::find_if (midi_outputs.begin (), midi_outputs.end (), has_lkmk3_daw);

	if (pi == midi_inputs.end () || po == midi_outputs.end ()) {
		return false;
	}

	inp = *pi;
	outp = *po;

	DEBUG_TRACE (DEBUG::LaunchkeyMk3, string_compose ("Probe successful: %1, %2\n", inp, outp));

	return true;
}

//----------------------------------------------------------------------------------------------------------
std::shared_ptr<ARDOUR::Port>
LaunchkeyMk3::input_port() const
{
	return std::static_pointer_cast<ARDOUR::Port>(_input_port);
}

//----------------------------------------------------------------------------------------------------------
std::shared_ptr<ARDOUR::Port>
LaunchkeyMk3::output_port() const
{
	return std::static_pointer_cast<ARDOUR::Port>(_output_port);
}

//----------------------------------------------------------------------------------------------------------
bool
LaunchkeyMk3::init_ports ()
{
	DEBUG_TRACE (DEBUG::LaunchkeyMk3, "registering in/out ports");

	// create midi ports
	auto inp = AudioEngine::instance()->register_input_port (DataType::MIDI, string_compose ("%1 recv", PORT_NAME_PREFIX), true);
	auto outp = AudioEngine::instance()->register_output_port (DataType::MIDI, string_compose ("%1 send", PORT_NAME_PREFIX), true);

	_input_port = std::dynamic_pointer_cast<AsyncMIDIPort>(inp);
	_output_port = std::dynamic_pointer_cast<AsyncMIDIPort>(outp);

	if (!_input_port || !_output_port) {
		return false;
	}

	// connect some signals from audioengine and potentially others
	ARDOUR::AudioEngine::instance()->PortRegisteredOrUnregistered.connect (
		port_connections,
		MISSING_INVALIDATOR,
		boost::bind (&LaunchkeyMk3::port_registration_handler, this),
		this
	);
	ARDOUR::AudioEngine::instance()->PortConnectedOrDisconnected.connect (
		port_connections,
		MISSING_INVALIDATOR,
		boost::bind (&LaunchkeyMk3::port_connection_handler, this, _1, _2, _3, _4, _5),
		this
	);

	// connect midi handling signals
	DEBUG_TRACE (DEBUG::LaunchkeyMk3, string_compose ("connecting MIDI signals on port %1\n", input_port()->name()));
	MIDI::Parser* p = _input_port->parser();

	// Incoming sysex (only used for device identification)
	p->sysex.connect_same_thread (
		midi_connections,
		boost::bind (&LaunchkeyMk3::handle_midi_sysex, this, _1, _2, _3)
	);

	// Incoming CC on channel 1 (used by a handful of buttons)
	p->channel_controller[0].connect_same_thread (
		midi_connections,
		boost::bind (&LaunchkeyMk3::handle_midi_controller_channel1, this, _1, _2)
	);

	// Incoming CC on channel 16 (used by all other buttons, and by pots and faders in all modes)
	p->channel_controller[15].connect_same_thread (
		midi_connections,
		boost::bind (&LaunchkeyMk3::handle_midi_controller_channel16, this, _1, _2)
	);

	// Incoming NOTE ON on channel 1 (used by pads in session mode)
	p->channel_note_on[0].connect_same_thread (
		midi_connections,
		boost::bind (&LaunchkeyMk3::handle_midi_note_on_channel1, this, _1, _2)
	);

	// Incoming NOTE ON on channel 10 (used by pads in drum mode)
	p->channel_note_on[9].connect_same_thread (
		midi_connections,
		boost::bind (&LaunchkeyMk3::handle_midi_note_on_channel10, this, _1, _2)
	);

	// Incoming POLY PRESSURE on channel 1 (used by pads in session mode)
	p->channel_poly_pressure[0].connect_same_thread (
		midi_connections,
		boost::bind (&LaunchkeyMk3::handle_midi_polypressure_channel1, this, _1, _2)
	);

	// Incoming POLY PRESSURE on channel 10 (used by pads in drum mode)
	p->channel_poly_pressure[9].connect_same_thread (
		midi_connections,
		boost::bind (&LaunchkeyMk3::handle_midi_polypressure_channel10, this, _1, _2)
	);

	/* This connection means that whenever data is ready from the input
	 * port, the relevant thread will invoke our ::handle_incoming_midi()
	 * method, which will read the data, and invoke the parser.
	 */
	_input_port->xthread().set_receive_handler (
		sigc::bind (sigc::mem_fun (this, &LaunchkeyMk3::handle_incoming_midi), _input_port)
	);
	_input_port->xthread().attach (main_loop()->get_context());

	// probe for connected Launchkey
	std::string  pn_in, pn_out;
	if (probe (pn_in, pn_out)) {
		inp->connect (pn_in);
		outp->connect (pn_out);
	}

    return true;
}

//----------------------------------------------------------------------------------------------------------
void
LaunchkeyMk3::release_ports ()
{
	// drain and remove MIDI ports
	DEBUG_TRACE (DEBUG::LaunchkeyMk3, "unregistering in/out ports");

	_output_port->drain (10000,  500000); /* check every 10 msecs, wait up to 1/2 second for the port to drain */

	{
		Glib::Threads::Mutex::Lock em (AudioEngine::instance()->process_lock());
		AudioEngine::instance()->unregister_port (_input_port);
		AudioEngine::instance()->unregister_port (_output_port);
	}

	_input_port.reset();
	_output_port.reset();
}

//----------------------------------------------------------------------------------------------------------
void
LaunchkeyMk3::port_registration_handler ()
{
	// this is called whenever a new port is registered or an existing one is unregistered.
	DEBUG_TRACE (DEBUG::LaunchkeyMk3, "LaunchkeyMk3::port_registration_handler\n");

	// if our ports are available but not currently connected, check if now the target port
	// is available and can be connected to.
	if (!_input_port || !_output_port) {
		return;
	}

	if (_input_port->connected() && _output_port->connected()) {
		return;
	}

	DEBUG_TRACE (DEBUG::LaunchkeyMk3, "port_registration_handler: not connected yet -> probe\n");

	// check for target ports
	std::string inp, outp;
	if (LaunchkeyMk3::probe(inp, outp)) {
		// if they are found, connect my own ports to them (will trigger connection handler in turn)
		DEBUG_TRACE (DEBUG::LaunchkeyMk3, "port_registration_handler: probe success! try to connect\n");
		if (!_input_port->connected()) {
			AudioEngine::instance()->connect (input_port()->name(), inp);
		}
		if (!_output_port->connected()) {
			AudioEngine::instance()->connect (output_port()->name(), outp);
		}
	}
}

//----------------------------------------------------------------------------------------------------------
void
LaunchkeyMk3::port_connection_handler (std::weak_ptr<ARDOUR::Port>, std::string name1, std::weak_ptr<ARDOUR::Port>, std::string name2, bool yn)
{
	DEBUG_TRACE (DEBUG::LaunchkeyMk3, "LaunchkeyMk3::port_connection_handler\n");

	// if ports do not exist, do nothing
	if (!_input_port || !_output_port) {
		return;
	}

	// see if any of the two involved ports was one of mine.
	// if so, add or remove the corresponding "connected" marker from the connection_state
	std::string ni = ARDOUR::AudioEngine::instance()->make_port_name_non_relative (input_port()->name());
	std::string no = ARDOUR::AudioEngine::instance()->make_port_name_non_relative (output_port()->name());

	if (ni == name1 || ni == name2) {
		if (yn) {
			connection_state |= InputConnected;
		} else {
			connection_state &= ~InputConnected;
		}
	} else if (no == name1 || no == name2) {
		if (yn) {
			connection_state |= OutputConnected;
		} else {
			connection_state &= ~OutputConnected;
		}
	} else {
		//DEBUG_TRACE (DEBUG::LaunchkeyMk3, string_compose ("Connections between %1 and %2 changed, but I ignored it\n", name1, name2));
		// not my ports, don't care. And don't need to proceed.
		return;
	}

	// if now both input and output are connected, we can begin to send data back and forth
	if ((connection_state & (InputConnected|OutputConnected)) == (InputConnected|OutputConnected)) {
		// NOTE: copied the hack from other control surfaces
		/* XXX this is a horrible hack. Without a short sleep here,
		 * something prevents the device wakeup messages from being
		 * sent and/or the responses from being received.
		 */
		g_usleep (100000);
		DEBUG_TRACE (DEBUG::LaunchkeyMk3, "device now connected for both input and output\n");
		connected ();
		device_active = true;

	} else {
		DEBUG_TRACE (DEBUG::LaunchkeyMk3, "Device disconnected (input or output or both) or not yet fully connected\n");
		if (device_active) {
			disconnected ();
		}
		device_active = false;
	}

	ConnectionChange ();  // emit signal for our GUI
}
