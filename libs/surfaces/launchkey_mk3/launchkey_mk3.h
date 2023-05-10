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

#include "midi_surface/midi_byte_array.h"
#include "midi_surface/midi_surface.h"

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

namespace ArdourSurface {

class LaunchkeyMk3 : public MIDISurface {
  public:
	LaunchkeyMk3 (ARDOUR::Session&);
	virtual ~LaunchkeyMk3();

	int set_active (bool yn) override;

	/* we probe for a device when our ports are connected. Before that,
	   there's no way to know if the device exists or not.
	 */
	static bool probe() { return true; }

	std::string input_port_name () const override;
	std::string output_port_name () const override;

//	XMLNode& get_state () const;
//	int set_state (const XMLNode&, int version);

	bool has_editor () const override { return true; }
	void* get_gui () const override;
	void  tear_down_gui () override;

	/* Note: because the Launchkey speaks an inherently duplex protocol,
	   we do not implement get/set_feedback() since this aspect of
	   support for the protocol is not optional.
	*/

/*
	void set_action (ButtonID, std::string const& action_name, bool on_press, FaderPort::ButtonState = ButtonState (0));
	std::string get_action (ButtonID, bool on_press, FaderPort::ButtonState = ButtonState (0));
*/
  private:
/*
	std::shared_ptr<ARDOUR::Stripable> _current_stripable;
	std::weak_ptr<ARDOUR::Stripable> pre_master_stripable;
	std::weak_ptr<ARDOUR::Stripable> pre_monitor_stripable;
*/
	// GUI
	mutable void *gui;
	void build_gui ();


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


	// helper functions to put Launchkey into and out of DAW mode
	void daw_mode_on();
	void daw_mode_off();


/*
	int fader_msb;
	int fader_lsb;
	bool fader_is_touched;

	PBD::microseconds_t last_encoder_time;
	int last_good_encoder_delta;
	int last_encoder_delta, last_last_encoder_delta;
*/
	void handle_midi_sysex (MIDI::Parser &p, MIDI::byte *, size_t) override;
	void handle_midi_controller_message (MIDI::Parser &, MIDI::EventTwoBytes* tb) override;
/*
	void handle_midi_polypressure_message (MIDI::Parser &, MIDI::EventTwoBytes* tb);
	void handle_midi_pitchbend_message (MIDI::Parser &, MIDI::pitchbend_t pb);


*/

	// event handlers called by superclass
	int begin_using_device () override;
	int stop_using_device () override;

	int device_acquire () override { return 0; }
	void device_release () override {}

//	ButtonState button_state;
/*
	friend class Button;

	class Button {
	  public:

		enum ActionType {
			NamedAction,
			InternalFunction,
		};

		Button (FaderPort& f, std::string const& str, ButtonID i, int o)
			: fp (f)
			, name (str)
			, id (i)
			, out (o)
			, flash (false)
		{}

		void set_action (std::string const& action_name, bool on_press, FaderPort::ButtonState = ButtonState (0));
		void set_action (boost::function<void()> function, bool on_press, FaderPort::ButtonState = ButtonState (0));
		std::string get_action (bool press, FaderPort::ButtonState bs = ButtonState (0));

		void set_led_state (bool onoff);
		bool invoke (ButtonState bs, bool press);
		bool uses_flash () const { return flash; }
		void set_flash (bool yn) { flash = yn; }

		XMLNode& get_state () const;
		int set_state (XMLNode const&);

		sigc::connection timeout_connection;

	  private:
		FaderPort& fp;
		std::string name;
		ButtonID id;
		int out;
		bool flash;

		struct ToDo {
			ActionType type;
			/* could be a union if boost::function didn't require a
			 * constructor
			 *
			std::string action_name;
			boost::function<void()> function;
		};

		typedef std::map<FaderPort::ButtonState,ToDo> ToDoMap;
		ToDoMap on_press;
		ToDoMap on_release;
	};

	typedef std::map<ButtonID,Button> ButtonMap;

	ButtonMap buttons;
	Button& get_button (ButtonID) const;

	std::set<ButtonID> buttons_down;
	std::set<ButtonID> consumed;

	bool button_long_press_timeout (ButtonID id);
	void start_press_timeout (Button&, ButtonID);

	void all_lights_out ();

	void map_recenable_state ();
	void map_transport_state ();

	sigc::connection periodic_connection;
	bool periodic ();

	sigc::connection blink_connection;
	typedef std::list<ButtonID> Blinkers;
	Blinkers blinkers;
	bool blink_state;
	bool blink ();
	void start_blinking (ButtonID);
	void stop_blinking (ButtonID);

	void set_current_stripable (std::shared_ptr<ARDOUR::Stripable>);
	void drop_current_stripable ();
	void use_master ();
	void use_monitor ();
*/

	void stripable_selection_changed () override;

/*
	PBD::ScopedConnection selection_connection;
	PBD::ScopedConnectionList stripable_connections;

	void map_stripable_state ();
	void map_solo ();
	void map_mute ();
	bool rec_enable_state;
	void map_recenable ();
	void map_gain ();
	void map_cut ();
	void map_auto ();
	void parameter_changed (std::string);

	/* operations (defined in operations.cc) *

	void read ();
	void write ();

	void left ();
	void right ();

	void touch ();
	void off ();

	void undo ();
	void redo ();
	void solo ();
	void mute ();
	void rec_enable ();

	void pan_azimuth (int);
	void pan_width (int);

	void punch ();
*/
};

}

#endif /* ardour_surface_launchkey_mk3_h */
