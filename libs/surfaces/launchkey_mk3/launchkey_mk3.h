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

#ifndef ardour_surface_launchkey_mk3_h
#define ardour_surface_launchkey_mk3_h

#include <list>
#include <map>
#include <set>
#include <glibmm/threads.h>

#define ABSTRACT_UI_EXPORTS
#include "pbd/abstract_ui.h"

#include "ardour/types.h"

#include "control_protocol/control_protocol.h"

namespace PBD {
	class Controllable;
}

#include <midi++/types.h>

#include "glibmm/main.h"

namespace MIDI {
	class Parser;
	class Port;
}


namespace ARDOUR {
	class AsyncMIDIPort;
	class Bundle;
	class Port;
	class Session;
	class MidiPort;
}


class MIDIControllable;
class MIDIFunction;
class MIDIAction;


class LaunchkeyMk3Request : public BaseUI::BaseRequestObject {
  public:
	LaunchkeyMk3Request () {}
	~LaunchkeyMk3Request () {}
};

namespace ArdourSurface {

class LaunchkeyMk3 : public ARDOUR::ControlProtocol, public AbstractUI<LaunchkeyMk3Request> {
  public:
	LaunchkeyMk3 (ARDOUR::Session&);
	virtual ~LaunchkeyMk3();

	// probe for existence of a connected launchkey (presence of MIDI ports with fitting names)
	static bool probe (std::string& inp, std::string& outp);


	int set_active (bool yn) override;

	// serialization
	XMLNode& get_state () const override;
	int set_state (const XMLNode&, int version) override;

	// GUI
	bool  has_editor () const override { return true; }
	void* get_gui () const override;
	PBD::Signal0<void> ConnectionChange;  // this is emitted to update the UI if the midi connections change

	// getters
	std::shared_ptr<ARDOUR::Port> input_port() const;
	std::shared_ptr<ARDOUR::Port> output_port() const;


  private:

	// constants
	static constexpr char PORT_NAME_PREFIX[] = "Launchkey Mk3";

	//----------------------------------------------------------------------------
	// private GUI methods, lifecycle management & "remote control"
	mutable void *gui;
	void build_gui ();
	void tear_down_gui () override;

	void stop ();
	void do_request (LaunchkeyMk3Request*) override;
	void thread_init () override;

	//----------------------------------------------------------------------------
	// ports handling

	// to communicate with the launchkey, those are set up in constructor and released in destructor
	std::shared_ptr<ARDOUR::AsyncMIDIPort> _input_port;
	std::shared_ptr<ARDOUR::AsyncMIDIPort> _output_port;

	// needed for callbacks from the Engine, to react to newly connected and created ports
	enum ConnectionState {
		InputConnected = 0x1,
		OutputConnected = 0x2
	};

	int connection_state;
	PBD::ScopedConnectionList port_connections;
	bool init_ports ();
	void release_ports ();
	void port_registration_handler ();
	void port_connection_handler (std::weak_ptr<ARDOUR::Port>, std::string, std::weak_ptr<ARDOUR::Port>, std::string, bool);

	bool device_active;      // changes in this status will be communicated to the GUI by ConnectionChange
	void connected ();
	void disconnected ();

	// ---------------------------------------------------------------------------------------------
	// MIDI methods

	// convenience function to send MIDI data to our output port; can use initializer list.
	void send_midi (const std::vector<MIDI::byte>& data);

	// called after ports are initialized and connected / before they are destroyed or disconnected
	void start_midi_handling ();
	void stop_midi_handling ();

	// incoming MIDI handlers
	bool handle_incoming_midi (Glib::IOCondition, std::weak_ptr<ARDOUR::AsyncMIDIPort>);  // dispatches incoming bytes to parser

	PBD::ScopedConnectionList midi_connections;

	// incoming sysex is only used to decode system identification info
	void handle_midi_sysex (MIDI::Parser &p, MIDI::byte *, size_t);

	// pots and faders send CC on channel 16 always, also most other buttons on the launchkey.
	void handle_midi_controller_channel16 (MIDI::Parser &, MIDI::EventTwoBytes* tb);
	// a few buttons report CC on channel 1
	void handle_midi_controller_channel1 (MIDI::Parser &, MIDI::EventTwoBytes* tb);
	// pads send note_on (with vel 0 for off) and poly_pressure in session and drum modes,
	//   (on channel 1 in the former and channel 10 in the latter).
	// in other modes they directly send on the MIDI port (instead of the DAW one)
	void handle_midi_note_on_channel1 (MIDI::Parser &, MIDI::EventTwoBytes* tb);
	void handle_midi_polypressure_channel1 (MIDI::Parser &, MIDI::EventTwoBytes* tb);
	void handle_midi_note_on_channel10 (MIDI::Parser &, MIDI::EventTwoBytes* tb);
	void handle_midi_polypressure_channel10 (MIDI::Parser &, MIDI::EventTwoBytes* tb);

	// ---------------------------------------------------------------------------------------------
	// Launchkey properties

	// whether Launchkey is in DAW mode. Also used as an indicator
	// whether we are actually communicating with a real Launchkey
	bool in_daw_mode;

	// whether we have a bigger model with faders (49, 61, 88)
	bool has_faders;

	// the pads, faders and pots can each be in one of several modes:
	enum class LkPadMode {
		DRUM,
		SESSION,
		SCALE_CHORDS,
		USER_CHORDS,
		CUSTOM0,
		CUSTOM1,
		CUSTOM2,
		CUSTOM3,
		DEVICE_SELECT,
		NAVIGATION
	};

	enum class LkPotMode {
		VOLUME,
		DEVICE,
		PAN,
		SEND_A,
		SEND_B,
		CUSTOM0,
		CUSTOM1,
		CUSTOM2,
		CUSTOM3
	};

	enum class LkFaderMode {
		VOLUME,
		DEVICE,
		SEND_A,
		SEND_B,
		CUSTOM0,
		CUSTOM1,
		CUSTOM2,
		CUSTOM3
	};

	LkPadMode current_pad_mode;
	LkPotMode current_pot_mode;
	LkFaderMode current_fader_mode;


	// TBD:
	void stripable_selection_changed () override;
};

}

#endif /* ardour_surface_launchkey_mk3_h */
