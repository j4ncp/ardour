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

#include <gtkmm/alignment.h>
#include <gtkmm/label.h>
#include <gtkmm/liststore.h>

#include "pbd/file_utils.h"
#include "pbd/strsplit.h"
#include "pbd/unwind.h"

#include "gtkmm2ext/actions.h"
#include "gtkmm2ext/action_model.h"
#include "gtkmm2ext/bindings.h"
#include "gtkmm2ext/gtk_ui.h"
#include "gtkmm2ext/gui_thread.h"
#include "gtkmm2ext/utils.h"

#include "ardour/audioengine.h"
#include "ardour/filesystem_paths.h"

#include "launchkey_mk3.h"
#include "gui.h"

#include "pbd/i18n.h"

using namespace PBD;
using namespace ARDOUR;
using namespace ArdourSurface;
using namespace std;
using namespace Gtk;
using namespace Gtkmm2ext;

//----------------------------------------------------------------------------------------------------------
void*
LaunchkeyMk3::get_gui () const
{
	if (!gui) {
		const_cast<LaunchkeyMk3*>(this)->build_gui ();
	}
	static_cast<Gtk::VBox*>(gui)->show_all();
	return gui;
}

//----------------------------------------------------------------------------------------------------------
void
LaunchkeyMk3::tear_down_gui ()
{
	if (gui) {
		Gtk::Widget *w = static_cast<Gtk::VBox*>(gui)->get_parent();
		if (w) {
			w->hide();
			delete w;
		}
	}
	delete static_cast<LKGUI*> (gui);
	gui = 0;
}

//----------------------------------------------------------------------------------------------------------
void
LaunchkeyMk3::build_gui ()
{
	gui = (void*) new LKGUI (*this);
}


//----------------------------------------------------------------------------------------------------------
//----------------------------------------------------------------------------------------------------------
LKGUI::LKGUI (LaunchkeyMk3& lkmk3)
	: lk (lkmk3)
	, table (2, 5)
	, ignore_active_change (false)
{
	set_border_width (12);

	table.set_row_spacings (4);
	table.set_col_spacings (6);
	table.set_border_width (12);
	table.set_homogeneous (false);

	// put an image of the launchkey to the left
	std::string data_file_path;
	string name = "launchkeymk3-small.png";
	Searchpath spath(ARDOUR::ardour_data_search_path());
	spath.add_subdirectory_to_paths ("icons");
	find_file (spath, name, data_file_path);
	if (!data_file_path.empty()) {
		image.set (data_file_path);
		hpacker.pack_start (image, false, false);
	}

	// build small table, consisting of two combo boxes (one for input and one for output port each),
	// along with two labels for the comboboxes
	Gtk::Label* l;
	Gtk::Alignment* align;
	int row = 0;

	input_combo.pack_start (midi_port_columns.short_name);
	input_combo.signal_changed().connect (sigc::bind (sigc::mem_fun (*this, &LKGUI::active_inport_changed), &input_combo));

	output_combo.pack_start (midi_port_columns.short_name);
	output_combo.signal_changed().connect (sigc::bind (sigc::mem_fun (*this, &LKGUI::active_outport_changed), &output_combo));

	l = manage (new Gtk::Label);
	l->set_markup (string_compose ("<span weight=\"bold\">%1</span>", _("Launchkey DAW port incoming:")));
	l->set_alignment (1.0, 0.5);
	table.attach (*l, 0, 1, row, row+1, AttachOptions(FILL|EXPAND), AttachOptions(0));
	table.attach (input_combo, 1, 2, row, row+1, AttachOptions(FILL|EXPAND), AttachOptions(0), 0, 0);
	row++;

	l = manage (new Gtk::Label);
	l->set_markup (string_compose ("<span weight=\"bold\">%1</span>", _("Launchkey DAW port outgoing:")));
	l->set_alignment (1.0, 0.5);
	table.attach (*l, 0, 1, row, row+1, AttachOptions(FILL|EXPAND), AttachOptions(0));
	table.attach (output_combo, 1, 2, row, row+1, AttachOptions(FILL|EXPAND), AttachOptions(0), 0, 0);
	row++;

	hpacker.pack_start (table, true, true);
	pack_start (hpacker, false, false);

	// update the port connection combos

	update_port_combos ();

	/* catch future changes to connection state */

	ARDOUR::AudioEngine::instance()->PortRegisteredOrUnregistered.connect (_port_connections, invalidator (*this), boost::bind (&LKGUI::connection_handler, this), gui_context());
	ARDOUR::AudioEngine::instance()->PortPrettyNameChanged.connect (_port_connections, invalidator (*this), boost::bind (&LKGUI::connection_handler, this), gui_context());
	lk.ConnectionChange.connect (_port_connections, invalidator (*this), boost::bind (&LKGUI::connection_handler, this), gui_context());
}

//----------------------------------------------------------------------------------------------------------
LKGUI::~LKGUI ()
{
}

//----------------------------------------------------------------------------------------------------------
void
LKGUI::connection_handler ()
{
	/* ignore all changes to combobox active strings here, because we're
	   updating them to match a new ("external") reality - we were called
	   because port connections have changed.
	*/

	PBD::Unwinder<bool> ici (ignore_active_change, true);

	update_port_combos ();
}

//----------------------------------------------------------------------------------------------------------
void
LKGUI::update_port_combos ()
{
	// get a list of all current midi input and output ports, and fill the combos with them
	vector<string> midi_inputs;
	vector<string> midi_outputs;

	ARDOUR::AudioEngine::instance()->get_ports ("", ARDOUR::DataType::MIDI, ARDOUR::PortFlags (ARDOUR::IsOutput|ARDOUR::IsTerminal), midi_inputs);
	ARDOUR::AudioEngine::instance()->get_ports ("", ARDOUR::DataType::MIDI, ARDOUR::PortFlags (ARDOUR::IsInput|ARDOUR::IsTerminal), midi_outputs);

	Glib::RefPtr<Gtk::ListStore> input = build_midi_port_list (midi_inputs);
	Glib::RefPtr<Gtk::ListStore> output = build_midi_port_list (midi_outputs);

	bool input_found = false;
	bool output_found = false;

	input_combo.set_model (input);
	output_combo.set_model (output);

	Gtk::TreeModel::Children children = input->children();
	Gtk::TreeModel::Children::iterator i;
	i = children.begin();
	++i; /* skip "Disconnected" */


	for (int n = 1;  i != children.end(); ++i, ++n) {
		string port_name = (*i)[midi_port_columns.full_name];
		if (lk.input_port()->connected_to (port_name)) {
			input_combo.set_active (n);
			input_found = true;
			break;
		}
	}

	if (!input_found) {
		input_combo.set_active (0); /* disconnected */
	}

	children = output->children();
	i = children.begin();
	++i; /* skip "Disconnected" */

	for (int n = 1;  i != children.end(); ++i, ++n) {
		string port_name = (*i)[midi_port_columns.full_name];
		if (lk.output_port()->connected_to (port_name)) {
			output_combo.set_active (n);
			output_found = true;
			break;
		}
	}

	if (!output_found) {
		output_combo.set_active (0); /* disconnected */
	}
}

//----------------------------------------------------------------------------------------------------------
Glib::RefPtr<Gtk::ListStore>
LKGUI::build_midi_port_list (vector<string> const & ports)
{
	Glib::RefPtr<Gtk::ListStore> store = ListStore::create (midi_port_columns);
	TreeModel::Row row;

	row = *store->append ();
	row[midi_port_columns.full_name] = string();
	row[midi_port_columns.short_name] = _("Disconnected");

	for (vector<string>::const_iterator p = ports.begin(); p != ports.end(); ++p) {
		row = *store->append ();
		row[midi_port_columns.full_name] = *p;
		std::string pn = ARDOUR::AudioEngine::instance()->get_pretty_name_by_name (*p);
		if (pn.empty ()) {
			pn = (*p).substr ((*p).find (':') + 1);
		}
		row[midi_port_columns.short_name] = pn;
	}

	return store;
}

//----------------------------------------------------------------------------------------------------------
void
LKGUI::active_inport_changed (Gtk::ComboBox* combo)
{
	if (ignore_active_change) {
		return;
	}

	TreeModel::iterator active = combo->get_active ();
	string new_port = (*active)[midi_port_columns.full_name];

	if (new_port.empty()) {
		lk.input_port()->disconnect_all ();
	} else {
		// connect input and output ports if necessary
		if (!lk.input_port()->connected_to (new_port)) {
			lk.input_port()->disconnect_all ();
			lk.input_port()->connect (new_port);
		}
	}
}

//----------------------------------------------------------------------------------------------------------
void
LKGUI::active_outport_changed (Gtk::ComboBox* combo)
{
	if (ignore_active_change) {
		return;
	}

	TreeModel::iterator active = combo->get_active ();
	string new_port = (*active)[midi_port_columns.full_name];

	if (new_port.empty()) {
		lk.output_port()->disconnect_all ();
	} else {
		if (!lk.output_port()->connected_to (new_port)) {
			lk.output_port()->disconnect_all ();
			lk.output_port()->connect (new_port);
		}
	}
}
