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
	init_ports ();
}

//----------------------------------------------------------------------------------------------------------
LaunchkeyMk3::~LaunchkeyMk3 ()
{
	// stop UI and processing connections
	stop();

	// delete my own ports
	release_ports ();

	// delete UI elements
	tear_down_gui ();
}

//----------------------------------------------------------------------------------------------------------
void
LaunchkeyMk3::stop ()
{
	DEBUG_TRACE (DEBUG::LaunchkeyMk3, "LaunchKeyMk3::stop ()\n");

	// stop midi
	stop_midi_handling ();

	// stop UI
	BaseUI::quit();
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
LaunchkeyMk3::connected ()
{
	start_midi_handling ();
}

//----------------------------------------------------------------------------------------------------------
void
LaunchkeyMk3::disconnected ()
{
	// NOTE: exiting DAW mode might not work if the launchkey has been disconnected already
	stop_midi_handling ();
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
void
LaunchkeyMk3::stripable_selection_changed ()
{
	//set_current_stripable (ControlProtocol::first_selected_stripable());
}
