#include "jss/dictionary.hpp"

#include <charconv>
#include <fstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace jss {

namespace {

[[noreturn]] void fail(std::size_t line_no, const std::string& message) {
    throw std::runtime_error("dict.tsv line " + std::to_string(line_no) + ": " + message);
}

// Splits on every separator, keeping empty fields: "a\t\tb" -> {"a", "", "b"}.
std::vector<std::string_view> split(std::string_view text, char sep) {
    std::vector<std::string_view> parts;
    std::size_t start = 0;
    while (true) {
        std::size_t end = text.find(sep, start);
        if (end == std::string_view::npos) {
            parts.push_back(text.substr(start));
            return parts;
        }
        parts.push_back(text.substr(start, end - start));
        start = end + 1;
    }
}

// A comma-joined list field. An empty field is an empty list.
std::vector<std::string> split_list(std::string_view field) {
    std::vector<std::string> items;
    if (field.empty()) {
        return items;
    }
    for (std::string_view item : split(field, ',')) {
        items.emplace_back(item);
    }
    return items;
}

// The whole field must be a non-negative integer.
int parse_int(std::string_view field, std::size_t line_no) {
    int value = 0;
    const char* first = field.data();
    const char* last = field.data() + field.size();
    auto [ptr, ec] = std::from_chars(first, last, value);
    if (field.empty() || ec != std::errc() || ptr != last || value < 0) {
        fail(line_no, "expected a number, got '" + std::string(field) + "'");
    }
    return value;
}

void expect_fields(const std::vector<std::string_view>& fields, std::size_t count,
                   std::size_t line_no) {
    if (fields.size() != count) {
        fail(line_no, "expected " + std::to_string(count) + " fields, got " +
                          std::to_string(fields.size()));
    }
}

void expect_text(std::string_view text, std::size_t line_no) {
    if (text.empty()) {
        fail(line_no, "empty form");
    }
}

// Called once an entry's lines are all read: checks it and adds its keys.
void finish_entry(Dictionary& dict, std::size_t entry_line) {
    const Entry& entry = dict.entries.back();
    if (entry.readings.empty()) {
        fail(entry_line, "entry " + std::to_string(entry.jmdict_id) + " has no readings");
    }
    if (entry.senses.empty()) {
        fail(entry_line, "entry " + std::to_string(entry.jmdict_id) + " has no senses");
    }

    int index = static_cast<int>(dict.entries.size() - 1);

    // A spelling can appear as both a kanji form and a reading (or twice in
    // one list). Insert each key once per entry, so find() never returns
    // the same entry twice.
    std::vector<std::string_view> keys;
    for (const KanjiForm& form : entry.kanji) {
        keys.push_back(form.text);
    }
    for (const Reading& reading : entry.readings) {
        keys.push_back(reading.text);
    }

    std::vector<std::string_view> inserted;
    for (std::string_view key : keys) {
        bool seen = false;
        for (std::string_view done : inserted) {
            if (done == key) {
                seen = true;
                break;
            }
        }
        if (!seen) {
            dict.trie.insert(key, index);
            inserted.push_back(key);
            ++dict.key_count;
        }
    }
}

}  // namespace

Dictionary load_dictionary(std::istream& in) {
    Dictionary dict;
    std::string line;
    std::size_t line_no = 0;
    std::size_t entry_line = 0;  // line of the current entry's E line; 0 = none yet

    while (std::getline(in, line)) {
        ++line_no;
        // Tolerate CRLF, e.g. a fixture file checked out on Windows.
        if (!line.empty() && line.back() == '\r') {
            line.pop_back();
        }
        if (!line.empty() && line[0] == '#') {
            continue;  // header comment
        }

        std::vector<std::string_view> fields = split(line, '\t');
        std::string_view type = fields[0];

        if (type == "E") {
            expect_fields(fields, 2, line_no);
            if (entry_line != 0) {
                finish_entry(dict, entry_line);
            }
            Entry entry;
            entry.jmdict_id = parse_int(fields[1], line_no);
            dict.entries.push_back(std::move(entry));
            entry_line = line_no;
            continue;
        }

        if (type != "K" && type != "R" && type != "S") {
            fail(line_no, "unknown line type '" + std::string(type) + "'");
        }
        if (entry_line == 0) {
            fail(line_no, "'" + std::string(type) + "' line before the first E line");
        }
        Entry& entry = dict.entries.back();

        if (type == "K") {
            // K <form> <priority>
            expect_fields(fields, 3, line_no);
            expect_text(fields[1], line_no);
            KanjiForm form;
            form.text = std::string(fields[1]);
            form.priority = split_list(fields[2]);
            entry.kanji.push_back(std::move(form));
        } else if (type == "R") {
            // R <reading> <priority> <kanji forms: "*", a list, or empty> <pitch>
            expect_fields(fields, 5, line_no);
            expect_text(fields[1], line_no);
            Reading reading;
            reading.text = std::string(fields[1]);
            reading.priority = split_list(fields[2]);
            if (fields[3] == "*") {
                reading.applies_to_all = true;
            } else {
                reading.applies_to = split_list(fields[3]);
            }
            for (const std::string& value : split_list(fields[4])) {
                reading.pitch.push_back(parse_int(value, line_no));
            }
            entry.readings.push_back(std::move(reading));
        } else {
            // S <parts of speech> <gloss> <gloss>...
            if (fields.size() < 3) {
                fail(line_no, "sense line needs parts of speech and at least one gloss");
            }
            Sense sense;
            sense.pos = split_list(fields[1]);
            for (std::size_t i = 2; i < fields.size(); ++i) {
                sense.glosses.emplace_back(fields[i]);
            }
            entry.senses.push_back(std::move(sense));
        }
    }

    if (in.bad()) {
        throw std::runtime_error("dict.tsv: read error after line " + std::to_string(line_no));
    }
    if (entry_line != 0) {
        finish_entry(dict, entry_line);
    }
    return dict;
}

Dictionary load_dictionary_file(const std::string& path) {
    std::ifstream file(path, std::ios::binary);
    if (!file) {
        throw std::runtime_error("cannot open dictionary file: " + path);
    }
    return load_dictionary(file);
}

}  // namespace jss
