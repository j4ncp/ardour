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

#include <cstdlib>
#include <sstream>
#include <algorithm>

#include <stdint.h>

#include <glibmm/fileutils.h>
#include <glibmm/miscutils.h>

#include "pbd/error.h"
#include "pbd/failed_constructor.h"
#include "pbd/file_utils.h"
#include "pbd/pthread_utils.h"
#include "pbd/compose.h"
#include "pbd/xml++.h"

#include "midi++/port.h"

#include "ardour/async_midi_port.h"
#include "ardour/audioengine.h"
#include "ardour/amp.h"
#include "ardour/bundle.h"
#include "ardour/debug.h"
#include "ardour/filesystem_paths.h"
#include "ardour/midi_port.h"
#include "ardour/midiport_manager.h"
#include "ardour/monitor_processor.h"
#include "ardour/profile.h"
#include "ardour/rc_configuration.h"
#include "ardour/record_enable_control.h"
#include "ardour/stripable.h"
#include "ardour/session.h"
#include "ardour/session_configuration.h"
#include "ardour/track.h"

#include "launchkey_mk3.h"

using namespace ARDOUR;
using namespace ArdourSurface;
using namespace PBD;
using namespace Glib;
using namespace std;

#include "pbd/i18n.h"

#include "pbd/abstract_ui.cc" // instantiate template


//----------------------------------------------------------------------------------------------------------
LaunchkeyMk3::LaunchkeyMk3 (Session& s)
	: ControlProtocol (s, X_("Novation Launchkey MK3"))
	, AbstractUI<LaunchkeyMk3Request> (name())
	, gui (0)
	, connection_state (0)
	, device_active (false)
	, in_daw_mode (false)
	, has_faders (false)
	, current_pad_mode (LkPadMode::SESSION)
	, current_pot_mode (LkPotMode::PAN)
	, current_fader_mode (LkFaderMode::VOLUME)
{
	DEBUG_TRACE (DEBUG::LaunchkeyMk3, "registering in/out ports");

	// create midi ports
	auto inp = AudioEngine::instance()->register_input_port (DataType::MIDI, string_compose ("%1 recv", PORT_NAME_PREFIX), true);
	auto outp = AudioEngine::instance()->register_output_port (DataType::MIDI, string_compose ("%1 send", PORT_NAME_PREFIX), true);

	_input_port = std::dynamic_pointer_cast<AsyncMIDIPort>(inp);
	_output_port = std::dynamic_pointer_cast<AsyncMIDIPort>(outp);

	if (!_input_port || !_output_port) {
		throw failed_constructor();
	}

	// TBD: connect some signals from audioengine and potentially others
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

	// probe for connected Launchkey
	std::string  pn_in, pn_out;
	if (probe (pn_in, pn_out)) {
		inp->connect (pn_in);
		outp->connect (pn_out);
	}
}

//----------------------------------------------------------------------------------------------------------
LaunchkeyMk3::~LaunchkeyMk3 ()
{
	// stop UI and processing connections
	stop();

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

	// delete UI elements
	tear_down_gui ();
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
LaunchkeyMk3::probe (std::string& inp, std::string& outp)
{
	// check existing MIDI ports for strings like "Launchkey MK3", "LKMK3", and "DAW In" or "DAW Out".
	// if found, return true and set parameters to found input and output ports.
	vector<string> midi_inputs;
	vector<string> midi_outputs;
	AudioEngine::instance()->get_ports ("", DataType::MIDI, PortFlags (IsOutput|IsPhysical), midi_inputs);
	AudioEngine::instance()->get_ports ("", DataType::MIDI, PortFlags (IsInput|IsPhysical), midi_outputs);

	auto has_lkmk3_daw = [](string const& s) {
		std::string pn = AudioEngine::instance()->get_hardware_port_name_by_name (s);

		return (pn.find ("Launchkey MK3") != string::npos ||
		        pn.find ("LKMK3") != string::npos) &&
		       pn.find ("DAW") != string::npos;
	};

	auto pi = std::find_if (midi_inputs.begin (), midi_inputs.end (), has_lkmk3_daw);
	auto po = std::find_if (midi_outputs.begin (), midi_outputs.end (), has_lkmk3_daw);

	if (pi == midi_inputs.end () || po == midi_outputs.end ()) {
		return false;
	}

	inp = *pi;
	outp = *po;
	return true;
}


//----------------------------------------------------------------------------------------------------------
void
LaunchkeyMk3::stop ()
{
	DEBUG_TRACE (DEBUG::LaunchkeyMk3, "LaunchKeyMk3::stop ()\n");

	// stop UI
	BaseUI::quit();

	// TBD: stop midi handling, drop signal/slot connections
}

//----------------------------------------------------------------------------------------------------------
void
LaunchkeyMk3::do_request (LaunchkeyMk3Request* req)
{
	if (req->type == CallSlot) {
		call_slot (MISSING_INVALIDATOR, req->the_slot);
	} else if (req->type == Quit) {
		stop ();
		disconnected ();
	}
}

//----------------------------------------------------------------------------------------------------------
void
LaunchkeyMk3::thread_init ()
{
	pthread_set_name (event_loop_name().c_str());

	PBD::notify_event_loops_about_thread_creation (pthread_self(), event_loop_name(), 2048);
	ARDOUR::SessionEvent::create_per_thread_pool (event_loop_name(), 128);

	set_thread_priority ();
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

//----------------------------------------------------------------------------------------------------------
void
LaunchkeyMk3::connected ()
{
	// TBD
}

//----------------------------------------------------------------------------------------------------------
void
LaunchkeyMk3::disconnected ()
{
	// TBD
}

//----------------------------------------------------------------------------------------------------------
void
LaunchkeyMk3::handle_midi_controller_message (MIDI::Parser &, MIDI::EventTwoBytes* tb)
{
	//if (tb->controller_number)

	/*
	bool was_fader = false;

	if (tb->controller_number == 0x0) {
		fader_msb = tb->value;
		was_fader = true;
	} else if (tb->controller_number == 0x20) {
		fader_lsb = tb->value;
		was_fader = true;
	}

	if (was_fader) {
		if (_current_stripable) {
			std::shared_ptr<AutomationControl> gain = _current_stripable->gain_control ();
			if (gain) {
				int ival = (fader_msb << 7) | fader_lsb;
				float val = gain->interface_to_internal (ival/16383.0);
				/* even though the faderport only controls a
				   single stripable at a time, allow the fader to
				   modify the group, if appropriate.
				*
				_current_stripable->gain_control()->set_value (val, Controllable::UseGroup);
			}
		}
	}*/
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

			// put device in DAW mode
			daw_mode_on ();

			// TBD: potentially other init work
		}
	}
}

//----------------------------------------------------------------------------------------------------------
int
LaunchkeyMk3::set_active (bool yn)
{
	DEBUG_TRACE (DEBUG::LaunchkeyMk3, string_compose("LaunchkeyMk3::set_active (%1)\n", yn));

	// check if already active. If so, do nothing.
	if (yn == active()) {
		return 0;
	}

	if (yn) {

		// start event loop

		BaseUI::run ();

//		connect_session_signals ();
/*
		Glib::RefPtr<Glib::TimeoutSource> blink_timeout = Glib::TimeoutSource::create (200); // milliseconds
		blink_connection = blink_timeout->connect (sigc::mem_fun (*this, &FaderPort::blink));
		blink_timeout->attach (main_loop()->get_context());

		Glib::RefPtr<Glib::TimeoutSource> periodic_timeout = Glib::TimeoutSource::create (100); // milliseconds
		periodic_connection = periodic_timeout->connect (sigc::mem_fun (*this, &FaderPort::periodic));
		periodic_timeout->attach (main_loop()->get_context());
*/
	} else {
		/* Control Protocol Manager never calls us with false, but
		 * insteads destroys us.
		 */
	}

	ControlProtocol::set_active (yn);

	DEBUG_TRACE (DEBUG::LaunchkeyMk3, string_compose("LaunchkeyMk3::set_active (%1) done\n", yn));

	return 0;
}

//----------------------------------------------------------------------------------------------------------
void
LaunchkeyMk3::daw_mode_on()
{/*
	// DAW mode
	DEBUG_TRACE (DEBUG::LaunchkeyMk3, "Putting Launchkey in DAW mode\n");
	{
		// send note on for DAW mode (0x0C) on channel 16
		const MIDI::byte daw_on[] = { 0x9F, 0x0C, 0x7F };
		MIDISurface::write (daw_on, 3);
		in_daw_mode = true;
	}

	// Continuous control pot pickup
	DEBUG_TRACE (DEBUG::LaunchkeyMk3, "Enable pot continuous control pot pickup\n");
	{
		// send note on for continuous control pot pickup (0x0A) on channel 16
		const MIDI::byte pickup_on[] = { 0x9F, 0x0A, 0x7F };
		MIDISurface::write (pickup_on, 3);
	}

	// init default modes
	current_pad_mode = LkPadMode::SESSION;
	current_pot_mode = LkPotMode::PAN;
	current_fader_mode = LkFaderMode::VOLUME;
	*/
}

//----------------------------------------------------------------------------------------------------------
void
LaunchkeyMk3::daw_mode_off()
{/*
	// DAW mode
	DEBUG_TRACE (DEBUG::LaunchkeyMk3, "Resetting Launchkey to standalone mode\n");
	{
		// send note off for DAW mode (0x0C) on channel 16
		const MIDI::byte daw_off[] = { 0x8F, 0x0C, 0x00 };
		MIDISurface::write (daw_off, 3);
		in_daw_mode = false;
	}

	// Continuous control pot pickup
	DEBUG_TRACE (DEBUG::LaunchkeyMk3, "Disable pot continuous control pot pickup\n");
	{
		// send note off for continuous control pot pickup (0x0A) on channel 16
		const MIDI::byte pickup_off[] = { 0x8F, 0x0A, 0x00 };
		MIDISurface::write (pickup_off, 3);
	}*/
}

//----------------------------------------------------------------------------------------------------------
XMLNode&
LaunchkeyMk3::get_state () const
{
	DEBUG_TRACE (DEBUG::LaunchkeyMk3, "LaunchkeyMk3::get_state\n");
	XMLNode& node (ControlProtocol::get_state());
	XMLNode* child;

	// add input & output port info
	child = new XMLNode (X_("Input"));
	child->add_child_nocopy (std::shared_ptr<ARDOUR::Port>(_input_port)->get_state());
	node.add_child_nocopy (*child);
	child = new XMLNode (X_("Output"));
	child->add_child_nocopy (std::shared_ptr<ARDOUR::Port>(_output_port)->get_state());
	node.add_child_nocopy (*child);

	return node;
}

//----------------------------------------------------------------------------------------------------------
int
LaunchkeyMk3::set_state (const XMLNode& node, int version)
{
	DEBUG_TRACE (DEBUG::LaunchkeyMk3, "LaunchkeyMk3::set_state\n");
	XMLNodeList nlist;
	XMLNode const* child;

	if (ControlProtocol::set_state (node, version)) {
		return -1;
	}

	if ((child = node.child (X_("Input"))) != 0) {
		XMLNode* portnode = child->child (Port::state_node_name.c_str());
		if (portnode) {
			portnode->remove_property ("name");
			DEBUG_TRACE (DEBUG::LaunchkeyMk3, "LaunchkeyMk3::set_state Input\n");
			std::shared_ptr<ARDOUR::Port>(_input_port)->set_state (*portnode, version);
		}
	}

	if ((child = node.child (X_("Output"))) != 0) {
		XMLNode* portnode = child->child (Port::state_node_name.c_str());
		if (portnode) {
			portnode->remove_property ("name");
			DEBUG_TRACE (DEBUG::LaunchkeyMk3, "LaunchkeyMk3::set_state Output\n");
			std::shared_ptr<ARDOUR::Port>(_output_port)->set_state (*portnode, version);
		}
	}

	return 0;
}

//----------------------------------------------------------------------------------------------------------
/*
int
LaunchkeyMk3::begin_using_device()
{
	DEBUG_TRACE (DEBUG::LaunchkeyMk3, "sending device inquiry message...\n");

	if (MIDISurface::begin_using_device ()) {
		return -1;
	}

	// Ask device for identification.
	// Check if it really is a launchkey, and if so, which one.
	const MIDI::byte buf[] = { 0xF0, 0x7E, 0x7F, 0x06, 0x01, 0xF7 };
	MIDISurface::write (buf, 6);

	return 0;
}

//----------------------------------------------------------------------------------------------------------
int
LaunchkeyMk3::stop_using_device ()
{
	// return from DAW mode if necessary
	if (in_daw_mode) {
		daw_mode_off ();
	}

	/*
	blink_connection.disconnect ();
	selection_connection.disconnect ();
	stripable_connections.drop_connections ();
	periodic_connection.disconnect ();

#if 0
	stripable_connections.drop_connections ();
#endif
*
	return 0;
}*/

//----------------------------------------------------------------------------------------------------------
void
LaunchkeyMk3::stripable_selection_changed ()
{
	//set_current_stripable (ControlProtocol::first_selected_stripable());
}
