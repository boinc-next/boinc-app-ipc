/**
 * Additional utilities for libxml++.
 * https://github.com/vorot93/xmlpp-util
 * Copyright (C) 2016 Artem Vorotnikov
 *
 * libxml++-util is free software; you can redistribute it and/or modify it
 * under the terms of the GNU Lesser General Public License
 * as published by the Free Software Foundation,
 * either version 3 of the License, or (at your option) any later version.
 *
 * libxml++-util is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public License
 * along with libxml++-util.  If not, see <http://www.gnu.org/licenses/>.
 */

#ifndef __XMLPP_UTIL__
#define __XMLPP_UTIL__

#include <functional>
#include <map>
#include <memory>

#include <libxml++/libxml++.h>

namespace xmlpp {
namespace util {

using StringMap = std::map<Glib::ustring, Glib::ustring>;
using StringVecMap = std::map<Glib::ustring, std::vector<Glib::ustring>>;
using BoolMap = std::map<Glib::ustring, bool>;

using Callback = std::function<void(const xmlpp::Node&)>;
using CallbackMap = std::map<Glib::ustring, Callback>;

inline void write_kv(xmlpp::Element& container, Glib::ustring key, Glib::ustring value) {
    container.add_child_element(key)->add_child_text(value);
}

// Wrapper around xmlpp::Document for easy creation and copying.
struct EasyDocument {
    std::unique_ptr<xmlpp::Document> data;

    void clear() {
        this->data = std::make_unique<xmlpp::Document>("1.0");
        this->data->create_root_node("");
    }
    Glib::ustring write_to_string(bool formatted = false) const {
        return formatted ? this->data->write_to_string_formatted() : this->data->write_to_string();
    }
    void parse(Glib::ustring v) {
        if (not v.validate()) {
            throw xmlpp::parse_error("Only valid UTF-8 data is supported.");
        }
        xmlpp::DomParser dp;
        dp.parse_memory(v);
        auto dp_doc = dp.get_document();
        if (not dp_doc) {
            throw xmlpp::parse_error("Resulting doc is empty.");
        }
        auto dp_root_node = dp_doc->get_root_node();
        if (not dp_root_node) {
            throw xmlpp::parse_error("Root node is empty.");
        }

        this->data->create_root_node_by_import(dp_root_node);
    }
    EasyDocument& operator=(EasyDocument v) {
        this->data = std::move(v.data);
        return *this;
    }
    operator bool() {
        const auto& root_node = *this->data->get_root_node();
        return not (root_node.get_name().empty() and root_node.get_children().empty());
    }
    xmlpp::Element& operator()() {
        return *this->data->get_root_node();
    }
    EasyDocument(Glib::ustring root_name = "") {
        this->clear();

        this->data->get_root_node()->set_name(root_name);
    }
    EasyDocument(const EasyDocument& v) {
        this->clear();

        this->data->create_root_node_by_import(v.data->get_root_node());
    }
    EasyDocument(const xmlpp::Document& v) {
        this->clear();

        this->data->create_root_node_by_import(v.get_root_node());
    }
    EasyDocument(const xmlpp::Node& v) {
        this->clear();

        this->data->create_root_node_by_import(&v);
    }
    template <typename T> EasyDocument(std::map<Glib::ustring, T> m, Glib::ustring root_name = "data") {
        this->clear();

        fill_node(*this->data->create_root_node(root_name), m);
    }
};

inline void fill_node(xmlpp::Element& container, StringMap data) {
    for (auto entry : data) {
        write_kv(container, entry.first, entry.second);
    }
}
inline void fill_node(xmlpp::Element& container, StringVecMap data) {
    for (auto entry : data) {
        for (auto v : entry.second) {
            write_kv(container, entry.first, v);
        }
    }
}
inline void fill_node(xmlpp::Element& container, BoolMap data) {
    for (auto entry : data) {
        auto v = entry.second;
        if (v) {
            container.add_child_element(entry.first);
        }
    }
}

inline void copy_children(xmlpp::Node& dest, const xmlpp::Node& src) {
    for (const auto& v : src.get_children()) {
        dest.import_node(v);
    }
}

inline bool get_boolean(const xmlpp::Node& node, Glib::ustring xpath = ".") { return node.eval_to_boolean(xpath); }
inline Glib::ustring get_string(const xmlpp::Node& node, Glib::ustring xpath = ".") { return node.eval_to_string(xpath); }
template <typename T> T get_number(const xmlpp::Node& node, Glib::ustring xpath = ".") { return T(node.eval_to_number(xpath)); }

inline void map_node(const xmlpp::Node& node, CallbackMap cb_data, std::function<void(Glib::ustring)> cb_unknown_key = nullptr) {
    for (const auto& v : node.get_children()) {
        auto k = v->get_name();
        Callback f;
        try {
            f = cb_data.at(k);
        } catch (const std::out_of_range& e) {
            if (cb_unknown_key) {
                cb_unknown_key(k);
            }
            continue;
        }
        f(*v);
    }
}
};
};
#endif
