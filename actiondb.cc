/*
 * Copyright (c) 2008, Thomas Jaeger <ThJaeger@gmail.com>
 *
 * Permission to use, copy, modify, and/or distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY
 * SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN ACTION
 * OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF OR IN
 * CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */
#include "actiondb.h"
#include "main.h"
#include "win.h"

#include <iostream>
#include <fstream>
#include <boost/archive/text_oarchive.hpp>
#include <boost/archive/text_iarchive.hpp>
#include <boost/serialization/map.hpp>
#include <boost/serialization/set.hpp>
#include <boost/serialization/list.hpp>
#include <boost/serialization/vector.hpp>
#include <boost/serialization/export.hpp>
#include <boost/serialization/shared_ptr.hpp>

#include <X11/extensions/XTest.h>

BOOST_CLASS_EXPORT(StrokeSet)

BOOST_CLASS_EXPORT(Action)
BOOST_CLASS_EXPORT(Command)
BOOST_CLASS_EXPORT(ModAction)
BOOST_CLASS_EXPORT(SendKey)
BOOST_CLASS_EXPORT(Scroll)
BOOST_CLASS_EXPORT(Ignore)
BOOST_CLASS_EXPORT(Button)
BOOST_CLASS_EXPORT(Misc)

template<class Archive> void Unique::serialize(Archive & ar, const unsigned int version) {}

template<class Archive> void Action::serialize(Archive & ar, const unsigned int version) {}

template<class Archive> void Command::serialize(Archive & ar, const unsigned int version) {
	ar & boost::serialization::base_object<Action>(*this);
	ar & cmd;
}

template<class Archive> void ModAction::serialize(Archive & ar, const unsigned int version) {
	ar & boost::serialization::base_object<Action>(*this);
	ar & mods;
}

template<class Archive> void SendKey::load(Archive & ar, const unsigned int version) {
	ar & boost::serialization::base_object<ModAction>(*this);
	ar & key;
	ar & code;
	if (version < 1) {
		bool xtest;
		ar & xtest;
	}
	compute_code();
}

template<class Archive> void SendKey::save(Archive & ar, const unsigned int version) const {
	ar & boost::serialization::base_object<ModAction>(*this);
	ar & key;
	ar & code;
}

template<class Archive> void Scroll::serialize(Archive & ar, const unsigned int version) {
	ar & boost::serialization::base_object<ModAction>(*this);
}

template<class Archive> void Ignore::serialize(Archive & ar, const unsigned int version) {
	ar & boost::serialization::base_object<ModAction>(*this);
}

template<class Archive> void Button::serialize(Archive & ar, const unsigned int version) {
	ar & boost::serialization::base_object<ModAction>(*this);
	ar & button;
}

template<class Archive> void Misc::serialize(Archive & ar, const unsigned int version) {
	ar & boost::serialization::base_object<Action>(*this);
	ar & type;
}

template<class Archive> void StrokeSet::serialize(Archive & ar, const unsigned int version) {
	ar & boost::serialization::base_object<std::set<RStroke> >(*this);
}

template<class Archive> void StrokeInfo::serialize(Archive & ar, const unsigned int version) {
	ar & strokes;
	ar & action;
	if (version == 0) return;
	ar & name;
}

using namespace std;

void SendKey::compute_code() {
	if (key)
		code = XKeysymToKeycode(dpy, key);
}

bool Command::run() {
	if (cmd == "")
		return false;
	pid_t pid = fork();
	switch (pid) {
		case 0:
			execlp("/bin/sh", "sh", "-c", cmd.c_str(), NULL);
			exit(1);
		case -1:
			printf("Error: can't execute command %s: fork failed\n", cmd.c_str());
	}
	return true;
}

ButtonInfo Button::get_button_info() const {
	ButtonInfo bi;
	bi.button = button;
	bi.state = mods;
	return bi;
}


const Glib::ustring Button::get_label() const {
	return get_button_info().get_button_text();
}

const char *Misc::types[5] = { "None", "Unminimize", "Show/Hide", "Disable (Enable)", NULL };

template<class Archive> void ActionListDiff::serialize(Archive & ar, const unsigned int version) {
	ar & parent;
	ar & deleted;
	ar & added;
	ar & name;
	ar & children;
	ar & app;
}

template<class Archive> void ActionDB::load(Archive & ar, const unsigned int version) {
	if (version == 2) {
		ar & root;
	}
	if (version == 1) {
		std::map<int, StrokeInfo> strokes;
		ar & strokes;
		for (std::map<int, StrokeInfo>::iterator i = strokes.begin(); i != strokes.end(); ++i)
			root.add(i->second);
	}
	if (version == 0) {
		std::map<std::string, StrokeInfo> strokes;
		ar & strokes;
		for (std::map<std::string, StrokeInfo>::iterator i = strokes.begin(); i != strokes.end(); ++i) {
			i->second.name = i->first;
			root.add(i->second);
		}
	}

	root.add_apps(apps);
	root.name = "Default";
}

template<class Archive> void ActionDB::save(Archive & ar, const unsigned int version) const {
	ar & root;
}

Source<bool> action_dummy;

void update_actions() {
	action_dummy.set(false);
}

void ActionDBWatcher::init() {
	std::string filename = config_dir+"actions";
	try {
		ifstream ifs(filename.c_str(), ios::binary);
		boost::archive::text_iarchive ia(ifs);
		ia >> actions;
		if (verbosity >= 2)
			printf("Loaded actions.\n");
	} catch (exception &e) {
		printf("Error: Couldn't read action database: %s.\n", e.what());
	}
	watchValue(action_dummy);
}

void ActionDBWatcher::timeout() {
	std::string filename = config_dir+"actions";
	try {
		ofstream ofs(filename.c_str());
		boost::archive::text_oarchive oa(ofs);
		oa << (const ActionDB &)actions;
		if (verbosity >= 2)
			printf("Saved actions.\n");
	} catch (exception &e) {
		printf("Error: Couldn't save action database: %s.\n", e.what());
		if (!good_state)
			return;
		good_state = false;
		new ErrorDialog("Couldn't save actions.  Your changes will be lost.  \nMake sure that "+config_dir+" is a directory and that you have write access to it.\nYou can change the configuration directory using the -c or --config-dir command line options.");
	}
}

boost::shared_ptr<std::map<Unique *, StrokeSet> > ActionListDiff::get_strokes() const {
	boost::shared_ptr<std::map<Unique *, StrokeSet> > strokes = parent ? parent->get_strokes() :
	       	boost::shared_ptr<std::map<Unique *, StrokeSet> >(new std::map<Unique *, StrokeSet>);
	for (std::set<Unique *>::const_iterator i = deleted.begin(); i != deleted.end(); i++)
		strokes->erase(*i);
	for (std::map<Unique *, StrokeInfo>::const_iterator i = added.begin(); i != added.end(); i++)
		if (i->second.strokes.size())
			(*strokes)[i->first] = i->second.strokes;
	return strokes;
}

Unique stroke_not_found, stroke_is_click, stroke_is_timeout;

Ranking *ActionListDiff::handle(RStroke s, int button_up) const {
	Ranking *r = new Ranking;
	r->stroke = s;
	r->score = -1;
	r->id = &stroke_not_found;
	bool success = false;
	if (!s)
		return r;
	boost::shared_ptr<std::map<Unique *, StrokeSet> > strokes = get_strokes();
	for (std::map<Unique *, StrokeSet>::const_iterator i = strokes->begin(); i!=strokes->end(); i++) {
		for (StrokeSet::iterator j = i->second.begin(); j!=i->second.end(); j++) {
			double score;
			bool match = Stroke::compare(s, *j, score);
			if (score < 0.25)
				continue;
			RStrokeInfo si = get_info(i->first);
			r->r.insert(pair<double, pair<std::string, RStroke> >
					(score, pair<std::string, RStroke>(si->name, *j)));
			if (score >= r->score) {
				r->score = score;
				if (match) {
					r->id = i->first;
					r->name = si->name;
					r->action = si->action;
					r->best_stroke = *j;
					success = true;
				}
			}
		}
	}
	if (!success && s->trivial()) {
		r->id = s->is_timeout() ? &stroke_is_timeout : &stroke_is_click;
		r->name = "click (default)";
		if (!s->is_timeout())
			success = true;
	}
	if (success) {
		if (verbosity >= 1)
			cout << "Excecuting Action " << r->name << "..." << endl;
		if (button_up)
			XTestFakeButtonEvent(dpy, button_up, False, CurrentTime);
		if (r->action)
			r->action->run();
	} else {
		if (verbosity >= 1)
			cout << "Couldn't find matching stroke." << endl;
	}
	return r;
}

ActionDB actions;
