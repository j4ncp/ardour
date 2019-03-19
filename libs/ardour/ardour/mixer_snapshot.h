/*
    Copyright (C) 20XX Paul Davis

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this program; if not, write to the Free Software
    Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.

*/

#ifndef __ardour_mixer_snapshot_h__
#define __ardour_mixer_snapshot_h__

#include <vector>
#include <ctime>

#include "ardour/session.h"
#include "ardour/route.h"
#include "ardour/vca.h"
#include "ardour/route_group.h"

class MixerSnapshot //: public PBD::Stateful
{
    public:
        MixerSnapshot(ARDOUR::Session*);
        ~MixerSnapshot();

        void snap();
        void snap(ARDOUR::RouteList);
        void snap(ARDOUR::RouteGroup*);
        void snap(boost::shared_ptr<ARDOUR::VCA>);
        void snap(boost::shared_ptr<ARDOUR::Route>);
        void recall();
        void clear();
        void write();
        void load();

        int id;
        std::string label;
        std::time_t timestamp;

    private:
        ARDOUR::Session* _session;

        void reassign_masters(boost::shared_ptr<ARDOUR::Slavable>, XMLNode);

        struct State {
            std::string id;
            std::string name;
            XMLNode     node;
        };

        std::vector<State> route_states;
        std::vector<State> group_states;
        std::vector<State> vca_states;
};

#endif /* __ardour_mixer_snapshot_h__ */