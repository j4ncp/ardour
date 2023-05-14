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

	Gtk::Table    table;
	Gtk::ComboBox input_combo;
	Gtk::ComboBox output_combo;
	Gtk::Image    image;

	void update_port_combos ();
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
};

}

#endif /* __ardour_launchkey_mk3_gui_h__ */
