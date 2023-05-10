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

#ifndef __ardour_launchkey_mk3_gui_h__
#define __ardour_launchkey_mk3_gui_h__

#include <vector>
#include <string>

#include <gtkmm/box.h>
#include <gtkmm/combobox.h>
#include <gtkmm/image.h>
#include <gtkmm/table.h>
#include <gtkmm/treestore.h>

namespace Gtk {
	class CellRendererCombo;
	class ListStore;
}

#include "launchkey_mk3.h"

namespace ActionManager {
        class ActionModel;
}

namespace ArdourSurface {

class LKGUI : public Gtk::VBox
{
public:
	LKGUI (LaunchkeyMk3&);
	~LKGUI ();

private:
	LaunchkeyMk3& lk;

	Gtk::HBox hpacker;

	Gtk::Table table;
	/*
	Gtk::Table action_table;
	*/
	Gtk::ComboBox input_combo;
	Gtk::ComboBox output_combo;
	Gtk::Image    image;

/*
	Gtk::ComboBox mix_combo[3];
	Gtk::ComboBox proj_combo[3];
	Gtk::ComboBox trns_combo[3];
	Gtk::ComboBox user_combo[2];
	Gtk::ComboBox foot_combo[3];
*/
	void update_port_combo ();
	PBD::ScopedConnectionList _port_connections;
	void connection_handler ();

	struct MidiPortColumns : public Gtk::TreeModel::ColumnRecord {
		MidiPortColumns() {
			add (short_name);
			add (full_name);
		}
		Gtk::TreeModelColumn<std::string> short_name;
		Gtk::TreeModelColumn<std::string> full_name;
	};

	MidiPortColumns midi_port_columns;
	bool ignore_active_change;

	Glib::RefPtr<Gtk::ListStore> build_midi_port_list (std::vector<std::string> const & ports);
	void active_inport_changed (Gtk::ComboBox*);
	void active_outport_changed (Gtk::ComboBox*);
/*
	const ActionManager::ActionModel& action_model;
*/
	std::map<std::string,std::string> action_map; // map from action names to paths
/*
	void build_action_combo (Gtk::ComboBox& cb, std::vector<std::pair<std::string,std::string> > const & actions, FaderPort::ButtonID, FaderPort::ButtonState);
	void build_mix_action_combo (Gtk::ComboBox&, FaderPort::ButtonState);
	void build_proj_action_combo (Gtk::ComboBox&, FaderPort::ButtonState);
	void build_trns_action_combo (Gtk::ComboBox&, FaderPort::ButtonState);
	void build_user_action_combo (Gtk::ComboBox&, FaderPort::ButtonState);
	void build_foot_action_combo (Gtk::ComboBox&, FaderPort::ButtonState);

	void action_changed (Gtk::ComboBox*, FaderPort::ButtonID, FaderPort::ButtonState);
*/
};

}

#endif /* __ardour_launchkey_mk3_gui_h__ */
