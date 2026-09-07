#pragma once

#include "util.hpp"

#include <sstream>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>

namespace aster {

class Json {
public:
    Json() : raw_("null") {}

    static Json null() { return Json("null"); }
    static Json boolean(bool value) { return Json(value ? "true" : "false"); }
    static Json number(int value) { return Json(std::to_string(value)); }
    static Json number(long long value) { return Json(std::to_string(value)); }
    static Json number(unsigned long long value) { return Json(std::to_string(value)); }
    static Json number(double value, int precision = 4) {
        std::ostringstream out;
        out << std::fixed << std::setprecision(precision) << value;
        return Json(out.str());
    }
    static Json string(const std::string& value) {
        return Json("\"" + json_escape(value) + "\"");
    }
    static Json raw(std::string json) { return Json(std::move(json)); }

    static Json array(const std::vector<Json>& items) {
        std::string out = "[";
        for (std::size_t i = 0; i < items.size(); ++i) {
            if (i) {
                out += ",";
            }
            out += items[i].raw_;
        }
        out += "]";
        return Json(std::move(out));
    }

    static Json object(const std::vector<std::pair<std::string, Json>>& fields) {
        std::string out = "{";
        for (std::size_t i = 0; i < fields.size(); ++i) {
            if (i) {
                out += ",";
            }
            out += "\"";
            out += json_escape(fields[i].first);
            out += "\":";
            out += fields[i].second.raw_;
        }
        out += "}";
        return Json(std::move(out));
    }

    const std::string& str() const { return raw_; }

    class Obj {
    public:
        Obj& kv(const std::string& key, const std::string& value) {
            return raw_field(key, "\"" + json_escape(value) + "\"");
        }
        Obj& kv(const std::string& key, const char* value) { return kv(key, std::string(value)); }
        template <typename T, typename = std::enable_if_t<std::is_integral<T>::value &&
                                                          !std::is_same<T, bool>::value>>
        Obj& kv(const std::string& key, T value) {
            return raw_field(key, std::to_string(value));
        }
        Obj& kv(const std::string& key, double value) {
            std::ostringstream out;
            out << std::fixed << std::setprecision(4) << value;
            return raw_field(key, out.str());
        }
        Obj& kv(const std::string& key, bool value) {
            return raw_field(key, value ? "true" : "false");
        }
        Obj& kv(const std::string& key, const Json& value) { return raw_field(key, value.str()); }
        Obj& kv_raw(const std::string& key, const std::string& json) {
            return raw_field(key, json);
        }
        Json done() const { return Json(body_ + "}"); }

    private:
        Obj& raw_field(const std::string& key, const std::string& json) {
            if (!first_) {
                body_ += ",";
            }
            first_ = false;
            body_ += "\"";
            body_ += json_escape(key);
            body_ += "\":";
            body_ += json;
            return *this;
        }

        std::string body_ = "{";
        bool first_ = true;
    };

    class Arr {
    public:
        Arr& push(const Json& value) { return push_raw(value.str()); }
        Arr& push(const std::string& value) {
            return push_raw("\"" + json_escape(value) + "\"");
        }
        Arr& push(int value) { return push_raw(std::to_string(value)); }
        Arr& push(long long value) { return push_raw(std::to_string(value)); }
        Arr& push(double value) {
            std::ostringstream out;
            out << std::fixed << std::setprecision(4) << value;
            return push_raw(out.str());
        }
        Arr& push(bool value) { return push_raw(value ? "true" : "false"); }
        Json done() const { return Json(body_ + "]"); }

    private:
        Arr& push_raw(const std::string& json) {
            if (!first_) {
                body_ += ",";
            }
            first_ = false;
            body_ += json;
            return *this;
        }

        std::string body_ = "[";
        bool first_ = true;
    };

private:
    explicit Json(std::string raw) : raw_(std::move(raw)) {}
    std::string raw_;
};

}  // namespace aster
