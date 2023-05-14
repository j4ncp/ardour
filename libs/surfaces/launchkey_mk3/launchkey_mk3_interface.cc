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

#include <pbd/failed_constructor.h>

#include "control_protocol/control_protocol.h"
#include "launchkey_mk3.h"

using namespace ARDOUR;
using namespace ArdourSurface;

static ControlProtocol*
new_launchkey_mk3_midi_protocol (Session* s)
{
	LaunchkeyMk3* lk3;

	try {
		lk3 =  new LaunchkeyMk3 (*s);
	} catch (failed_constructor& err) {
		return 0;
	}

	if (lk3->set_active (true)) {
		delete lk3;
		return 0;
	}

	return lk3;
}

static void
delete_launchkey_mk3_midi_protocol (ControlProtocol* cp)
{
	delete cp;
}

static bool
probe_launchkey_mk3_midi_protocol ()
{
	std::string i, o;
	return LaunchkeyMk3::probe (i, o);
}

static ControlProtocolDescriptor launchkey_mk3_midi_descriptor = {
	/*name :              */   "Novation Launchkey MK3",
	/*id :                */   "uri://ardour.org/surfaces/launchkey_mk3:0",
	/*module :            */   0,
	/*available :         */   0,
	/*probe_port :        */   probe_launchkey_mk3_midi_protocol,
	/*match_usb :         */   0,
	/*initialize :        */   new_launchkey_mk3_midi_protocol,
	/*destroy :           */   delete_launchkey_mk3_midi_protocol,
};

extern "C" ARDOURSURFACE_API ControlProtocolDescriptor* protocol_descriptor () { return &launchkey_mk3_midi_descriptor; }

